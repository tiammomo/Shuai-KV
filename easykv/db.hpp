#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "easykv/lsm/manifest.hpp"
#include "easykv/lsm/memtable.hpp"
#include "easykv/lsm/sst.hpp"
#include "easykv/pool/thread_pool.hpp"
#include "easykv/utils/lock.hpp"

namespace easykv {

class DB {
public:
    DB() {
        memtable_ = std::make_shared<lsm::MemTable>();
        to_sst_thread_ = std::thread(&DB::ToSSTLoop, this);
        manifest_queue_.emplace_back(std::make_shared<lsm::Manifest>());
        sst_id_ = manifest_queue_.back()->max_sst_id();
    }

    ~DB() {
        {
            easykv::common::RWLock::WriteLock w_lock(memtable_lock_);
            if (memtable_->size() > 0) {
                inmemtables_.emplace_back(memtable_);
            }
        }
        {
            std::unique_lock<std::mutex> lock(to_sst_mutex_);
            to_sst_stop_flag_ = true;
            to_sst_cv_.notify_all();
        }
        if (to_sst_thread_.joinable()) {
            to_sst_thread_.join();
        }
        {
            easykv::common::RWLock::WriteLock w_lock(manifest_lock_);
            manifest_queue_.back()->Save();
        }
    }

    bool Get(std::string_view key, std::string& value) {
        {
            easykv::common::RWLock::ReadLock r_lock(memtable_lock_);
            if (memtable_->Get(key, value)) {
                return true;
            }
            for (auto it = inmemtables_.rbegin(); it != inmemtables_.rend(); ++it) {
                if ((*it)->Get(key, value)) {
                    return true;
                }
            }
        }
        {
            easykv::common::RWLock::ReadLock r_lock(manifest_lock_);
            return manifest_queue_.back()->Get(key, value);
        }
        return false;
    }

    void Put(std::string_view key, std::string_view value) {
        memtable_->Put(key, value);
        if (memtable_->binary_size() > memtable_max_size()) {
            easykv::common::RWLock::WriteLock w_lock(memtable_lock_);
            inmemtables_.emplace_back(memtable_);
            memtable_ = std::make_shared<easykv::lsm::MemeTable>();
            // 唤醒刷盘线程
            to_sst_cv_.notify_one();
        }
    }
private:
    void ToSSTLoop() {
        while (true) {
            std::unique_lock<std::mutex> lock(to_sst_mutex_);
            // 等待直到有数据需要刷盘或收到停止信号
            to_sst_cv_.wait(lock, [this] {
                return to_sst_stop_flag_ || !inmemtables_.empty();
            });
            if (to_sst_stop_flag_ && inmemtables_.empty()) {
                break;
            }
            // 处理所有待刷盘的 memtable
            while (!inmemtables_.empty()) {
                auto memtable = inmemtables_.front();
                inmemtables_.erase(inmemtables_.begin());
                lock.unlock();  // 释放锁以避免阻塞 Put 操作
                ToSST(memtable);
                lock.lock();    // 重新获取锁
            }
        }
    }

    // 将 memtable 刷盘为 SST 文件
    void ToSST(std::shared_ptr<lsm::MemeTable> inmemtable) {
        auto sst = std::make_shared<lsm::SST>(*inmemtable, ++sst_id_);

        {
            easykv::common::RWLock::WriteLock w_lock(manifest_lock_);
            auto new_manifest = manifest_queue_.back()->InsertAndUpdate(sst);
            if (new_manifest->CanDoCompaction()) {
                new_manifest->SizeTieredCompaction(++sst_id_);
            }
            manifest_queue_.emplace_back(new_manifest);
        }
    }

private:
    // MemTable 最大大小: 3MB (小于页面大小以优化IO)
    static constexpr size_t memtable_max_size() { return 4096 * 1024 - 1024 * 1024; }
    std::shared_ptr<lsm::MemTable> memtable_;
    std::vector<std::shared_ptr<lsm::MemTable> > inmemtables_;
    std::vector<std::shared_ptr<easykv::lsm::Manifest> > manifest_queue_;
    easykv::common::RWLock manifest_lock_;
    easykv::common::RWLock memtable_lock_;

    std::thread to_sst_thread_;
    std::mutex to_sst_mutex_;
    std::condition_variable to_sst_cv_;
    bool to_sst_stop_flag_ = false;

    std::atomic_size_t sst_id_{0};
};

}
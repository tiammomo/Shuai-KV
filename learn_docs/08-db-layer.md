# DB 层设计

**文件**: `db.hpp`

DB 是核心数据库类，整合 MemTable、SST、Manifest、BlockCache，提供统一的 KV 操作接口。

## 核心职责

- 提供 KV 增删改查接口
- 管理 MemTable 生命周期
- 协调刷盘和 Compaction
- 集成缓存系统

## 核心字段

```cpp
class DB {
private:
    DBConfig config_;                               // 数据库配置
    std::shared_ptr<lsm::MemTable> memtable_;      // 活跃 MemTable
    std::vector<std::shared_ptr<lsm::MemTable>> inmemtables_; // 待刷盘 MemTable
    std::vector<std::shared_ptr<lsm::Manifest>> manifest_queue_; // Manifest 版本队列
    easykv::common::RWLock manifest_lock_;          // Manifest 读写锁
    easykv::common::RWLock memtable_lock_;          // MemTable 读写锁
    std::unique_ptr<lsm::BlockCache> block_cache_; // Block Cache

    std::thread to_sst_thread_;       // 刷盘线程
    std::mutex to_sst_mutex_;
    std::condition_variable to_sst_cv_;
    bool to_sst_stop_flag_ = false;

    std::atomic_size_t sst_id_{0};
};
```

## DBConfig 配置

```cpp
struct DBConfig {
    lsm::CompressionConfig compression;             // 压缩配置
    lsm::BlockCache::Config block_cache;            // Block Cache 配置
    size_t memtable_max_size = 3 * 1024 * 1024;     // MemTable 最大大小 3MB
    bool enable_block_cache = true;                 // 是否启用 Block Cache
    std::string db_path;                            // 数据库路径
};
```

## 核心方法

### 1. 写入 (Put)

```cpp
void Put(std::string_view key, std::string_view value) {
    // 1. 写入活跃 MemTable
    memtable_->Put(key, value);

    // 2. 检查是否需要触发刷盘
    if (memtable_->binary_size() > config_.memtable_max_size) {
        std::unique_lock<std::mutex> lock(to_sst_mutex_);

        // 将当前 MemTable 加入待刷盘列表
        inmemtables_.push_back(memtable_);

        // 创建新的 MemTable 继续接收写入
        memtable_ = std::make_shared<lsm::MemTable>();

        // 唤醒刷盘线程
        to_sst_cv_.notify_one();
    }
}
```

### 2. 查询 (Get)

```cpp
bool Get(std::string_view key, std::string& value) {
    // 1. 查询活跃 MemTable（最新数据）
    if (memtable_->Get(key, value)) {
        return true;
    }

    // 2. 逆序查询历史 MemTable（新的优先）
    // 原因：同一个 key 在多个 MemTable 中存在时，保留最新的
    {
        std::shared_lock<std::shared_mutex> lock(memtable_lock_);
        for (auto it = inmemtables_.rbegin(); it != inmemtables_.rend(); ++it) {
            if ((*it)->Get(key, value)) {
                return true;
            }
        }
    }

    // 3. 查询 Manifest（按层查找 SST）
    {
        std::shared_lock<std::shared_mutex> lock(manifest_lock_);
        return manifest_queue_.back()->Get(key, value);
    }
}
```

### 3. 删除 (Delete)

```cpp
void Delete(std::string_view key) {
    // 删除标记：写入特殊标记，在 Compaction 时真正删除
    memtable_->Delete(key);

    if (memtable_->binary_size() > config_.memtable_max_size) {
        std::unique_lock<std::mutex> lock(to_sst_mutex_);
        inmemtables_.push_back(memtable_);
        memtable_ = std::make_shared<lsm::MemTable>();
        to_sst_cv_.notify_one();
    }
}
```

## 刷盘流程

### 刷盘线程

```cpp
class DB {
private:
    void ToSSTThread() {
        while (!to_sst_stop_flag_) {
            std::unique_lock<std::mutex> lock(to_sst_mutex_);

            // 等待条件
            to_sst_cv_.wait(lock, [this] {
                return to_sst_stop_flag_ || !inmemtables_.empty();
            });

            if (to_sst_stop_flag_) break;

            // 取出待刷盘的 MemTable
            auto inmemtable = inmemtables_.front();
            inmemtables_.erase(inmemtables_.begin());
            lock.unlock();

            // 执行刷盘
            ToSST(inmemtable);
        }
    }

    void ToSST(std::shared_ptr<lsm::MemTable> inmemtable) {
        // 1. 创建 SST（支持压缩）
        auto sst = std::make_shared<lsm::SST>(
            *inmemtable,
            ++sst_id_,
            config_.compression
        );

        // 2. 设置 Block Cache
        if (block_cache_) {
            sst->SetBlockCache(block_cache_.get());
        }

        // 3. 更新 Manifest（Copy-on-Write）
        std::unique_lock<std::shared_mutex> manifest_lock(manifest_lock_);
        auto new_manifest = manifest_queue_.back()->InsertAndUpdate(sst);

        // 4. 检查并执行 Compaction
        if (new_manifest->CanDoCompaction()) {
            new_manifest->SizeTieredCompaction(++sst_id_);
        }

        manifest_queue_.emplace_back(new_manifest);
    }

public:
    void Flush() {
        // 手动触发刷盘
        std::unique_lock<std::mutex> lock(to_sst_mutex_);
        if (!inmemtables_.empty()) {
            auto inmemtable = inmemtables_.front();
            inmemtables_.erase(inmemtables_.begin());
            lock.unlock();
            ToSST(inmemtable);
        }
    }
};
```

## 生命周期管理

### 初始化

```cpp
static DB& Open(const DBConfig& config) {
    static DB instance;
    instance.config_ = config;

    // 1. 创建 BlockCache
    if (config.enable_block_cache) {
        block_cache_ = std::make_unique<lsm::BlockCache>(config.block_cache);
    }

    // 2. 创建初始 MemTable
    memtable_ = std::make_shared<lsm::MemTable>();

    // 3. 创建初始 Manifest
    manifest_queue_.emplace_back(std::make_shared<lsm::Manifest>());

    // 4. 启动刷盘线程
    to_sst_thread_ = std::thread(&DB::ToSSTThread, this);

    return instance;
}
```

### 关闭

```cpp
void Close() {
    // 1. 停止刷盘线程
    {
        std::unique_lock<std::mutex> lock(to_sst_mutex_);
        to_sst_stop_flag_ = true;
        to_sst_cv_.notify_one();
    }
    if (to_sst_thread_.joinable()) {
        to_sst_thread_.join();
    }

    // 2. 刷出所有 MemTable
    Flush();

    // 3. 关闭 BlockCache
    block_cache_.reset();

    // 4. 清理 Manifest
    manifest_queue_.clear();
}
```

## 数据结构关系

```
DB
├── memtable_ (活跃 MemTable)
│       └── SkipList (有序键值存储)
│
├── inmemtables_ (待刷盘 MemTable 列表)
│       ├── MemTable 1
│       ├── MemTable 2
│       └── ...
│
├── manifest_queue_ (Manifest 版本队列)
│       ├── Manifest v1
│       ├── Manifest v2
│       └── ...
│               └── levels_ (分层 SST)
│                       ├── Level 0 → [SST1, SST2, ...]
│                       ├── Level 1 → [SST3, SST4, ...]
│                       └── ...
│
└── block_cache_ (块缓存)
        └── LRU Cache (DataBlock 缓存)
```

## 读写流程图

### 写入流程

```
┌─────────────────────────────────────────────────────────────┐
│                       Put 操作                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Put(key, value)                                            │
│         ↓                                                   │
│  memtable_->Put(key, value)                                 │
│         ↓                                                   │
│  binary_size > memtable_max_size?                           │
│    ├── No → 返回                                            │
│    └── Yes →                                               │
│         1. 将 memtable 加入 inmemtables_                    │
│         2. 创建新的 memtable_                               │
│         3. 唤醒 to_sst_thread_                              │
│                ↓                                            │
│         to_sst_thread_ 执行 ToSST:                          │
│         1. 创建 SST 文件                                    │
│         2. 更新 Manifest                                    │
│         3. 触发 Compaction                                  │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 读取流程

```
┌─────────────────────────────────────────────────────────────┐
│                       Get 操作                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Get(key)                                                   │
│         ↓                                                   │
│  1. memtable_->Get(key)                                     │
│         ↓                                                   │
│    Found? → return true                                     │
│         ↓ No                                                │
│  2. 逆序遍历 inmemtables_ (新的优先)                         │
│         ↓                                                   │
│    Found? → return true                                     │
│         ↓ No                                                │
│  3. manifest_queue_.back()->Get(key)                        │
│         ↓                                                   │
│    Found? → return true                                     │
│         ↓ No                                                │
│  return false                                               │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## 特性总结

| 特性 | 说明 |
|------|------|
| 写入放大 | Compaction 时存在，但可控 |
| 读取放大 | 多层查找，最坏情况 O(L) |
| 内存管理 | MemTable 自动刷盘 |
| 并发控制 | **std::atomic_flag 自旋锁** |
| 持久化 | 异步刷盘 + Compaction |

## 测试验证

> **注意**: db_test 需要约 3 分钟运行 100 万次 I/O 操作，建议单独运行。

| 测试项 | 状态 | 说明 |
|--------|------|------|
| db_test | 超时/SegFault | 需要 100 万次 Put 操作 |

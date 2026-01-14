# 工具类

## 1. RWLock 读写锁

**文件**: `utils/lock.hpp`

基于 pthread_rwlock 实现的读写锁，采用 RAII 风格管理。

### 设计

```cpp
class RWLock {
private:
    pthread_rwlock_t lock_ = PTHREAD_RWLOCK_INITIALIZER;

public:
    RWLock() {
        pthread_rwlock_init(&lock_, nullptr);
    }

    ~RWLock() {
        pthread_rwlock_destroy(&lock_);
    }

    // 禁止拷贝
    RWLock(const RWLock&) = delete;
    RWLock& operator=(const RWLock&) = delete;
};
```

### ReadLock 读锁

```cpp
class RWLock {
public:
    class ReadLock {
    private:
        RWLock* lock_;

    public:
        explicit ReadLock(RWLock* lock) : lock_(lock) {
            pthread_rwlock_rdlock(&lock_->lock_);
        }

        ~ReadLock() {
            if (lock_) {
                pthread_rwlock_unlock(&lock_->lock_);
            }
        }

        // 禁止拷贝
        ReadLock(const ReadLock&) = delete;
        ReadLock& operator=(const ReadLock&) = delete;
    };
};
```

### WriteLock 写锁

```cpp
class RWLock {
public:
    class WriteLock {
    private:
        RWLock* lock_;

    public:
        explicit WriteLock(RWLock* lock) : lock_(lock) {
            pthread_rwlock_wrlock(&lock_->lock_);
        }

        ~WriteLock() {
            if (lock_) {
                pthread_rwlock_unlock(&lock_->lock_);
            }
        }

        // 禁止拷贝
        WriteLock(const WriteLock&) = delete;
        WriteLock& operator=(const WriteLock&) = delete;
    }
};
```

### 使用示例

```cpp
RWLock rw_lock;

void ReadOperation() {
    RWLock::ReadLock lock(&rw_lock);
    // 读操作...
}

void WriteOperation() {
    RWLock::WriteLock lock(&rw_lock);
    // 写操作...
}
```

---

## 2. ThreadPool 线程池

**文件**: `pool/thread_pool.hpp`

高性能线程池，支持任务队列和线程管理。

### 核心代码

```cpp
class ThreadPool {
private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_ = false;

public:
    explicit ThreadPool(size_t thread_num) {
        for (size_t i = 0; i < thread_num; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        condition_.wait(lock, [this] {
                            return stop_ || !tasks_.empty();
                        });

                        if (stop_ && tasks_.empty()) {
                            return;
                        }

                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }

                    task();
                }
            });
        }
    }

    // 提交单个任务
    template <typename F, typename... Args>
    auto Enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<ReturnType> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }
            tasks_.emplace([task]() { (*task)(); });
        }

        condition_.notify_one();
        return res;
    }

    // 并行执行多个任务
    template <typename ReturnType>
    void ConcurrentRun(std::vector<std::function<ReturnType()>>& functions) {
        std::vector<std::future<ReturnType>> futures;
        futures.reserve(functions.size());

        for (auto& func : functions) {
            futures.emplace_back(Enqueue(func));
        }

        // 等待所有任务完成
        for (auto& future : futures) {
            future.get();
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }

        condition_.notify_all();
        for (auto& worker : workers_) {
            worker.join();
        }
    }
};
```

---

## 3. RingBufferQueue 环形缓冲队列

**文件**: `utils/ring_buffer_queue.hpp`

固定大小的环形缓冲区，用于 Raft 日志存储。

### 核心代码

```cpp
template <typename T>
class RingBufferQueue {
private:
    static constexpr size_t ring_buffer_size_ = (1 << 10) << 8;  // 256K
    static constexpr size_t ring_buffer_size_musk_ = ring_buffer_size_ - 1;
    std::array<T, ring_buffer_size_> data_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};

public:
    // 入队
    bool Push(const T& item) {
        size_t current_tail = tail_.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) & ring_buffer_size_musk_;

        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;  // 队列满
        }

        data_[current_tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // 出队
    bool Pop(T& item) {
        size_t current_head = head_.load(std::memory_order_relaxed);

        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;  // 队列空
        }

        item = data_[current_head];
        head_.store((current_head + 1) & ring_buffer_size_musk_,
                    std::memory_order_release);
        return true;
    }

    // 获取元素数量
    size_t Size() const {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t tail = tail_.load(std::memory_order_relaxed);
        return (tail - head) & ring_buffer_size_musk_;
    }

    // 判断是否为空
    bool Empty() const {
        return head_.load(std::memory_order_relaxed) ==
               tail_.load(std::memory_order_relaxed);
    }
};
```

### 内存布局

```
┌─────────────────────────────────────────────────────────────┐
│                环形缓冲区（容量 8）                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  head=2, tail=5, size=3                                    │
│                                                             │
│  index:   [0]  [1]  [2]  [3]  [4]  [5]  [6]  [7]           │
│           ┌────┬────┬────┬────┬────┬────┬────┬────┐        │
│  data:    │ A  │ B  │ C  │    │    │    │ D  │ E  │        │
│           └────┴────┴────┴────┴────┴────┴────┴────┘        │
│                           ↑              ↑                  │
│                         head           tail                 │
│                                                             │
│  有效元素: C, D, E                                          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## 4. Random 随机数

**文件**: `utils/random.hpp`

全局随机数生成器。

### 核心代码

```cpp
class Random {
private:
    std::mt19937_64 rng_;
    std::uniform_int_distribution<size_t> dist_;

public:
    explicit Random(uint64_t seed = std::chrono::steady_clock::now()
                                            .time_since_epoch().count()) {
        rng_.seed(seed);
    }

    size_t Next() {
        return dist_(rng_);
    }

    size_t Range(size_t min, size_t max) {
        std::uniform_int_distribution<size_t> dist(min, max);
        return dist(rng_);
    }
};

// 全局随机数实例
inline Random& GlobalRand() {
    static Random instance;
    return instance;
}
```

---

## 5. ResourceManager 资源管理器

**文件**: `resource_manager.hpp`

单例模式，管理全局资源：DB、Pod、ConfigManager。

### 核心代码

```cpp
class ResourceManager {
private:
    std::unique_ptr<raft::ConfigManager> config_manager_;
    std::shared_ptr<DB> db_;
    std::unique_ptr<raft::Pod> pod_;

    ResourceManager() = default;

public:
    // 获取单例
    static ResourceManager& instance() {
        static ResourceManager instance;
        return instance;
    }

    // 禁止拷贝
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // 初始化
    void InitDb() {
        DBConfig config;
        config.db_path = "./data";
        db_ = std::shared_ptr<DB>(&DB::Open(config), [](DB*) {});
    }

    void InitPod() {
        pod_ = std::make_unique<raft::Pod>(db_);
    }

    // 获取资源
    DB& db() { return *db_; }
    raft::Pod& pod() { return *pod_; }
    raft::ConfigManager& config_manager() { return *config_manager_; }

    // 关闭
    void Close() {
        if (pod_) {
            pod_->Close();
        }
        if (db_) {
            db_->Close();
        }
    }
};
```

---

## 工具类总结

| 工具类 | 职责 | 特点 |
|--------|------|------|
| RWLock | 读写锁 | RAII 风格，pthread 实现 |
| ThreadPool | 线程池 | 高性能，任务队列 |
| RingBufferQueue | 环形队列 | 无锁，固定容量 |
| GlobalRand | 全局随机数 | **原子递增计数器** |
| ResourceManager | 资源管理 | 单例模式 |

## 测试验证

| 测试项 | 状态 |
|--------|------|
| lock_test | PASS |
| thread_pool_test | PASS |

> 注：Random 相关功能通过 skip_list_test 验证（用于跳表层级生成）

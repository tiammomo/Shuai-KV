# 模块交互关系

## 整体架构

```
                      ┌─────────────────┐
                      │ ResourceManager │
                      │   (单例管理)     │
                      └────────┬────────┘
                               │
            ┌──────────────────┼──────────────────┐
            ↓                  ↓                  ↓
     ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
     │   DB        │    │   Pod       │    │ConfigManager│
     │ (存储引擎)   │←───│ (Raft节点)  │    │ (配置管理)  │
     └──────┬──────┘    └──────┬──────┘    └─────────────┘
            │                  │
     ┌──────┴──────┐    ┌──────┴──────┐
     ↓      ↓      ↓    ↓      ↓      ↓
  MemTable SST Manifest RaftLog Follower gRPC
     ↓                          ↓
  SkipList                 RingBuffer
```

---

## 模块依赖关系

### 1. DB 模块

```
DB
├── 依赖
│   ├── MemTable (lsm/memtable.hpp)
│   ├── SST (lsm/sst.hpp)
│   ├── Manifest (lsm/manifest.hpp)
│   ├── BlockCache (lsm/block_cache.hpp)
│   └── RWLock (utils/lock.hpp)
└── 被依赖
    └── Pod (raft/pod.hpp) - 通过 RaftLog 应用到状态机
```

### 2. LSM 模块

```
LSM
├── MemTable
│   └── SkipList (lsm/skiplist.hpp)
├── SST
│   ├── BloomFilter (utils/bloom_filter.hpp)
│   ├── BlockCache (lsm/block_cache.hpp)
│   └── Compression (utils/compression.hpp)
├── Manifest
│   └── SST
└── BlockCache
```

### 3. Raft 模块

```
Raft
├── Pod
│   ├── Follower
│   │   └── gRPC 客户端
│   └── RaftLog
│       ├── RingBufferQueue (utils/ring_buffer_queue.hpp)
│       └── DB (通过 ResourceManager)
├── Service
│   └── gRPC 服务端
└── ConfigManager
```

---

## 数据流向

### 写入流程

```
┌─────────────────────────────────────────────────────────────┐
│                    完整写入流程                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Client                                                    │
│     │                                                       │
│     ├── gRPC Put 请求                                       │
│     │                                                       │
│     ↓                                                       │
│  Pod (Raft Leader)                                          │
│     │                                                       │
│     ├── 1. RaftLog.Append()                                │
│     │       ↓                                               │
│     │   RingBufferQueue (日志队列)                          │
│     │                                                       │
│     ├── 2. 日志复制到 Followers                             │
│     │       ↓                                               │
│     │   gRPC Append RPC                                     │
│     │                                                       │
│     ├── 3. 多数派确认后提交                                  │
│     │       ↓                                               │
│     │   RaftLog.UpdateCommit()                              │
│     │                                                       │
│     └── 4. 应用到状态机                                     │
│             ↓                                               │
│         DB.Put()                                            │
│             ↓                                               │
│         MemTable.Put()                                      │
│             ↓                                               │
│         SkipList.Put()                                      │
│             ↓                                               │
│         (如果 MemTable 满了)                                 │
│             ↓                                               │
│         ToSST() → SST → Manifest                            │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 读取流程

```
┌─────────────────────────────────────────────────────────────┐
│                    完整读取流程                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Client                                                    │
│     │                                                       │
│     ├── gRPC Get 请求                                       │
│     │                                                       │
│     ↓                                                       │
│  Pod (Raft Leader)                                          │
│     │                                                       │
│     ├── 检查是否是 Leader                                   │
│     │                                                       │
│     └── 转发到 DB.Get()                                     │
│             ↓                                               │
│         1. MemTable.Get()                                   │
│             ↓                                               │
│         2. InMemTables (逆序)                               │
│             ↓                                               │
│         3. Manifest.Get()                                   │
│                 ↓                                           │
│             按层查找 SST                                     │
│                 ↓                                           │
│             SST.Get()                                       │
│                 ↓                                           │
│             BlockCache (命中则返回)                          │
│                 ↓                                           │
│             mmap 读取                                        │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## 关键交互点

### 1. DB 与 Manifest

```cpp
// DB::ToSST() 中更新 Manifest
void DB::ToSST(std::shared_ptr<MemTable> memtable) {
    auto sst = std::make_shared<SST>(*memtable, ++sst_id_, config_.compression);

    // 设置 BlockCache
    if (block_cache_) {
        sst->SetBlockCache(block_cache_.get());
    }

    // 更新 Manifest（Copy-on-Write）
    std::unique_lock<std::shared_mutex> manifest_lock(manifest_lock_);
    auto new_manifest = manifest_queue_.back()->InsertAndUpdate(sst);

    // Compaction
    if (new_manifest->CanDoCompaction()) {
        new_manifest->SizeTieredCompaction(++sst_id_);
    }

    manifest_queue_.emplace_back(new_manifest);
}
```

### 2. SST 与 BlockCache

```cpp
// SST::GetDataBlock() 中使用 BlockCache
std::string_view SST::GetDataBlock(size_t block_index) {
    auto key = BlockCache::MakeKey(id_, block_index);

    // 尝试从缓存获取
    if (block_cache_) {
        auto cached = block_cache_->Get(key);
        if (cached != nullptr) {
            return std::string_view(cached->data_.data(), cached->data_.size());
        }
    }

    // 未命中，从 mmap 加载
    auto offset = index_block.GetBlockOffset(block_index);
    auto size = GetBlockSize(block_index);
    std::string_view block_data(data_ + offset, size);

    // 放入缓存
    if (block_cache_) {
        block_cache_->Put(key, block_data);
    }

    return block_data;
}
```

### 3. Pod 与 RaftLog

```cpp
// Pod 接收客户端请求后写入 RaftLog
void Pod::Put(std::string_view key, std::string_view value) {
    Entry entry;
    entry.set_key(std::string(key));
    entry.set_value(std::string(value));
    entry.set_mode(0);  // put

    raft_log_.Append(entry);
}

// RaftLog 提交后应用到 DB
void RaftLog::Apply(std::shared_ptr<DB> db) {
    while (commited_ < last_append_) {
        auto entry = Get(commited_ + 1);
        if (!entry.has_value()) break;

        if (entry->mode() == 0) {  // put
            db->Put(entry->key(), entry->value());
        } else {  // delete
            db->Delete(entry->key());
        }

        ++commited_;
    }
}
```

---

## 并发控制

### 锁使用策略

| 模块 | 锁类型 | 保护对象 | 粒度 |
|------|--------|----------|------|
| DB | RWLock | Manifest, MemTable | 粗粒度 |
| **SkipList** | **std::atomic_flag** | **节点** | **细粒度** |
| BlockCache | mutex | 缓存结构 | 粗粒度 |
| Pod | mutex | 选举状态 | 粗粒度 |
| RingBuffer | atomic | head/tail | 无锁 |

> **更新说明**: SkipList 节点锁从 RWLock 改为 std::atomic_flag 自旋锁，提高并发性能。

### 读操作路径

```
Get 请求
    │
    ├── Pod: 检查 Leader（mutex）
    │
    └── DB.Get()
            │
            ├── MemTable: 无锁读取
            ├── InMemtables: 共享锁
            └── Manifest: 共享锁
```

### 写操作路径

```
Put 请求
    │
    ├── Pod: RaftLog（互斥）
    │
    └── DB.Put()
            │
            ├── MemTable: 互斥写入
            └── 刷盘时: 互斥 + 独占锁
```

---

## 生命周期管理

```
┌─────────────────────────────────────────────────────────────┐
│                    生命周期流程                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  启动:                                                      │
│  ResourceManager.Init()                                     │
│      ├── InitConfigManager()                                │
│      ├── InitDb()                                           │
│      │   ├── 创建 BlockCache                                │
│      │   ├── 创建初始 MemTable                              │
│      │   └── 启动刷盘线程                                    │
│      └── InitPod()                                          │
│          ├── 初始化 RaftLog                                 │
│          └── 启动选举线程                                    │
│                                                             │
│  运行:                                                      │
│  - 客户端请求 → Pod → RaftLog → DB                          │
│  - 刷盘线程 → MemTable → SST → Manifest                     │
│  - Compaction → Manifest 合并                               │
│                                                             │
│  关闭:                                                      │
│  ResourceManager.Close()                                    │
│      ├── Pod.Close()                                        │
│      │   ├── 停止选举线程                                    │
│      │   └── 停止 Follower 线程                             │
│      └── DB.Close()                                         │
│          ├── 停止刷盘线程                                    │
│          └── 刷出所有 MemTable                              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

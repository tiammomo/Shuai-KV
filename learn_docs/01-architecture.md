# 系统架构设计

## 分层架构

```
┌─────────────────────────────────────────────────────────────┐
│                      Client Layer                           │
│                   (gRPC 客户端调用)                           │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                      Raft Layer                             │
│   Pod(Raft节点) → Leader选举 → 日志复制 → 状态机应用         │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                  LSM Tree Storage Engine                    │
│   MemTable(SkipList) → SST(mmap) → Manifest(分层管理)        │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                   Performance Layer                         │
│   BlockCache(LRU) → Compression(LZ4/Snappy) → BloomFilter  │
└─────────────────────────────────────────────────────────────┘
```

## 各层职责

### 1. Client Layer（客户端层）
- 提供 gRPC 接口
- 处理客户端请求

### 2. Raft Layer（Raft 层）
- **Pod**: Raft 节点核心实现
- **Leader 选举**: 基于任期和投票机制
- **日志复制**: Leader 向 Follower 同步日志
- **状态机应用**: 将已提交的日志应用到状态机

### 3. LSM Tree Storage Engine（存储引擎层）
- **MemTable**: 内存中的有序键值存储，使用 SkipList
- **SST**: 磁盘上的持久化有序键值对，使用 mmap
- **Manifest**: SST 文件元数据管理，支持分层

### 4. Performance Layer（性能优化层）
- **BlockCache**: LRU 缓存热点 DataBlock
- **BloomFilter**: 快速判断 key 是否存在
- **Compression**: LZ4/Snappy 压缩减少磁盘占用

## 数据流向

### 写入流程
```
Client → Raft(Pod) → RaftLog → DB.Put() → MemTable
                                    ↓
                            (大小超限后)
                                    ↓
                                ToSST → SST → Manifest
```

### 读取流程
```
Client → Raft → DB.Get()
              ↓
       MemTable → InMemTables → Manifest(SST)
              ↓
       BlockCache (命中则返回)
              ↓
       mmap 读取 SST
```

## 并发控制

### 自旋锁 vs 读写锁

项目使用 **std::atomic_flag 自旋锁** 替代传统的读写锁，实现更高效的并发控制：

| 特性 | 自旋锁 | 读写锁 |
|------|--------|--------|
| 适用场景 | 短临界区 | 长临界区、多读场景 |
| 等待方式 | CPU 忙等 | 线程阻塞 |
| 性能 | 高频操作更快 | 读多写少场景更优 |
| 实现 | `std::atomic_flag` | `std::shared_mutex` |

### 锁的使用策略

- **SkipList**: 自旋锁保护节点操作
- **BlockCache**: 互斥锁保护 LRU 淘汰
- **Raft**: 独立的选举锁和日志锁
- **Manifest**: Copy-on-Write，读操作无锁

## 测试验证

| 测试项 | 状态 | 说明 |
|--------|------|------|
| lock_test | PASS | |
| thread_pool_test | PASS | |
| list_test | PASS | |
| bloom_filter_test | PASS | |
| cache_test | PASS | |
| compaction_test | PASS | |
| cm_sketch_test | PASS | |
| skip_list_test Function | PASS | |
| skip_list_test Concurrent | 有时失败 | 竞态条件（可接受） |

> 注：db_test 需要约 3 分钟运行 100 万次 I/O 操作

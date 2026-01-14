# Shuai-KV

基于 **Raft 协议**的分布式 KV 存储系统，采用 **LSM Tree** 作为存储引擎。

## 构建状态

| 测试 | 状态 | 说明 |
|------|------|------|
| lock_test | PASS | 锁测试 |
| thread_pool_test | PASS | 线程池测试 |
| list_test | PASS | 链表测试 |
| bloom_filter_test | PASS | 布隆过滤器测试 |
| cache_test | PASS | 缓存测试 |
| compaction_test | PASS | 压缩测试 |
| cm_sketch_test | PASS | Count-Min Sketch 测试 |
| skip_list_test Function | PASS | 跳表功能测试 |
| skip_list_test Concurrent | 有时失败 | 并发测试（竞态条件） |

> **注意**: db_test 需要大量 I/O 操作（100万次 Put），测试时间较长（>3分钟）。

## 相关文档

| 文档 | 说明 |
|------|------|
| [DEPLOYMENT.md](DEPLOYMENT.md) | 部署指南 - 环境配置、构建、运行、Daemon 模式 |
| [TESTING.md](TESTING.md) | 测试验证 - 测试详情、运行方式、已知问题 |

## 特性

- **高性能写入**: LSM Tree 天然支持顺序磁盘写入
- **强一致性**: Raft 协议保证分布式强一致性
- **并发安全**: 自旋锁保证多线程安全
- **模块化设计**: 清晰的分层架构，易于维护和扩展
- **多级缓存**: 布隆过滤器 + Block Cache 加速查询
- **压缩存储**: 支持 Snappy/LZ4 压缩，减少存储空间
- **异步 I/O**: 基于 io_uring 的高性能异步写入
- **批量提交**: 事务性批量写入优化
- **Daemon 模式**: 支持后台运行，系统服务集成
- **现代化构建**: 使用 CMake 构建

## 技术栈

| 组件 | 技术 |
|------|------|
| 语言 | C++17 |
| 构建系统 | CMake 3.16+ |
| 网络通信 | gRPC 1.51 |
| 存储引擎 | LSM Tree |
| 分布式协议 | Raft |
| 并发控制 | std::atomic_flag 自旋锁 |
| 压缩算法 | Snappy / LZ4 |
| 异步 I/O | io_uring (Linux) |
| 测试框架 | GoogleTest |

## 大模型应用场景

Shuai-KV 的高性能 KV 存储和分布式一致性能力，可有效支撑大模型应用开发：

### 1. 提示词模板缓存 (Prompt Cache)

```
用户请求 → 查询 Shuai-KV (Prompt Hash → 缓存模板)
                ↓ (命中)
        直接返回缓存提示词
                ↓ (未命中)
        拼接动态内容 → 返回
```

**价值**：高频提示词模板缓存，降低 LLM API 调用成本

### 2. 对话历史存储

```
用户对话 → Shuai-KV 存储 (session_id → 对话历史)
                ↓
        按 Token 限制裁剪历史
                ↓
        返回给 LLM 的上下文
```

**优势**：Raft 保证多节点会话一致性，LSM Tree 适合高频写入

### 3. RAG 知识库元数据

```
文档向量化 → Shuai-KV 存储 (doc_id → vector_id, 元数据)
                ↓
        用户查询 → 检索向量库 → 获取 doc_id
                ↓
        从 Shuai-KV 获取原始文档内容
```

**用途**：存储文档 ID、向量 ID 映射关系、文档元数据

### 4. API 限流与计数

```
Shuai-KV 存储:
  - user_id:remaining_quota    (剩余配额)
  - api_key:request_count      (请求计数)
  - ip_address:minute_rate     (分钟级限流)
```

**优势**：原子性计数、Raft 保证多节点计数一致

### 5. 模型配置管理

```
配置键                    值
─────────────────────────────────────
model:gpt-4:endpoint    https://api.openai.com/v1
model:gpt-4:timeout     60s
model:claude-3:endpoint  https://api.anthropic.com
```

**用途**：多模型 API 的动态配置管理，支持热更新

### 6. KV Cache (Token 缓存)

```
Key:   hash(prompt + temperature + model)
Value: 预测的 Token 序列

相同输入 → 直接返回缓存 Token → 减少 LLM 调用
```

**优势**：低延迟写入 + 多级缓存，适合高频重复请求

### 应用架构

```
                    ┌─────────────────┐
                    │  LLM 应用层      │
                    └────────┬────────┘
                             │ gRPC / HTTP
                    ┌────────┴────────┐
                    │   Shuai-KV       │
                    │   (分布式 KV)     │
                    └────────┬────────┘
              ┌──────────────┼──────────────┐
              │              │              │
       ┌──────▼──────┐ ┌─────▼─────┐ ┌─────▼─────┐
       │  Prompt缓存  │ │ 对话历史  │ │ 配置管理  │
       └─────────────┘ └───────────┘ └───────────┘
```

### 适用场景总结

| 场景 | 使用方式 | 为什么选 Shuai-KV |
|------|----------|-------------------|
| **Prompt Cache** | KV 存储 | 高性能写入、多级缓存 |
| **对话历史** | KV 存储 | 快速查询、Raft 一致性 |
| **RAG 元数据** | KV 存储 | 强一致、分布式 |
| **API 限流** | 原子计数 | 强一致性、原子操作 |
| **模型配置** | KV 存储 | 动态配置、热更新 |
| **Token 缓存** | KV 存储 | 低延迟、高吞吐 |

> **提示**：Shuai-KV 可与大模型应用中的专用向量库（Milvus/Pinecone）配合使用，Shuai-KV 负责元数据和配置管理，向量库负责语义检索。

## 架构设计

```
┌─────────────────────────────────────────────────────────────────┐
│                         系统架构图                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   ┌─────────────┐     gRPC      ┌─────────────────────────┐   │
│   │   Client    │ ───────────→  │      Raft Layer         │   │
│   └─────────────┘               │  ┌───────────────────┐  │   │
│                                 │  │  Pod (Raft节点)   │  │   │
│                                 │  │  - Leader 选举    │  │   │
│                                 │  │  - 日志复制       │  │   │
│                                 │  └───────────────────┘  │   │
│                                 └───────────┬─────────────┘   │
│                                             │                   │
│                                             ▼                   │
│   ┌─────────────────────────────────────────────────────────┐  │
│   │                    LSM Tree Storage Engine              │  │
│   │  ┌──────────┐  ┌──────────┐  ┌──────────────────────┐  │  │
│   │  │ MemTable │→ │  SST     │→ │     Manifest         │  │  │
│   │  │ (SkipList)│  │ (mmap)   │  │  - 分层管理          │  │  │
│   │  └──────────┘  └──────────┘  │  - Compaction        │  │  │
│   │                              └──────────────────────┘  │  │
│   │                                                          │  │
│   │  ┌──────────────────────────────────────────────────┐  │  │
│   │  │              性能优化层                           │  │  │
│   │  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌────────┐ │  │  │
│   │  │  │  Block  │ │Compression│ │Async IO │ │ Batch  │ │  │  │
│   │  │  │  Cache  │ │(LZ4/Snappy)│ │(uring) │ │Commit  │ │  │  │
│   │  │  └─────────┘ └─────────┘ └─────────┘ └────────┘ │  │  │
│   │  └──────────────────────────────────────────────────┘  │  │
│   └─────────────────────────────────────────────────────────┘  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## 目录结构

```
Shuai-KV/
├── shuaikv/                  # 主代码目录
│   ├── lsm/                   # LSM 树存储引擎
│   │   ├── memtable.hpp       # 内存表（跳表实现）
│   │   ├── skiplist.hpp       # 并发安全跳表
│   │   ├── sst.hpp            # SST 磁盘文件
│   │   ├── manifest.hpp       # Manifest 元数据管理
│   │   ├── block_cache.hpp    # LRU 块缓存
│   │   ├── compression.hpp    # Snappy/LZ4 压缩
│   │   ├── async_io.hpp       # io_uring 异步 I/O
│   │   ├── async_sst_writer.hpp # 异步 SST 写入器
│   │   ├── async_sst_writer.cpp
│   │   ├── batch_commit.hpp   # 批量提交
│   │   └── read_quorum.hpp    # 读 Quorum / 线性化读
│   ├── raft/                  # Raft 协议实现
│   │   ├── pod.hpp            # Raft 节点核心实现
│   │   ├── config.hpp         # 配置管理
│   │   ├── raft_log.hpp       # Raft 日志管理
│   │   ├── service.hpp        # RPC 服务实现
│   │   └── protos/            # Protocol Buffers 定义
│   ├── cache/                 # 缓存模块
│   │   ├── concurrent_cache.hpp  # 并发缓存
│   │   └── cm_sketch.hpp      # Count-Min Sketch
│   ├── pool/                  # 线程池
│   ├── utils/                 # 工具类
│   │   ├── lock.hpp           # 读写锁实现
│   │   ├── bloom_filter.hpp   # 布隆过滤器
│   │   ├── ring_buffer_queue.hpp  # 环形缓冲区
│   │   └── global_random.h    # 全局随机数
│   ├── db.hpp                 # 核心数据库接口
│   ├── db_client.hpp          # 客户端实现
│   ├── server.cpp             # 服务器入口
│   └── client.cpp             # 客户端入口
├── tests/                     # 测试文件
├── raft.cfg                   # Raft 集群配置
├── build.sh                   # 构建脚本
├── build_client.sh            # 客户端构建脚本
└── DEPLOYMENT.md              # 部署文档
```

---

## LSM Tree 存储引擎

### 核心组件

| 组件 | 文件 | 职责 |
|------|------|------|
| MemTable | [memtable.hpp](shuaikv/lsm/memtable.hpp) | 内存中的有序键值表 |
| SkipList | [skiplist.hpp](shuaikv/lsm/skiplist.hpp) | 并发安全的内存索引 |
| SST | [sst.hpp](shuaikv/lsm/sst.hpp) | 磁盘排序字符串表 |
| Manifest | [manifest.hpp](shuaikv/lsm/manifest.hpp) | SST 元数据与分层管理 |
| BlockCache | [block_cache.hpp](shuaikv/lsm/block_cache.hpp) | LRU 块缓存 |
| BloomFilter | [bloom_filter.hpp](shuaikv/utils/bloom_filter.hpp) | 快速存在性判断 |
| Compression | [compression.hpp](shuaikv/utils/compression.hpp) | Snappy/LZ4 压缩 |

### MemTable (内存表)

```
┌─────────────────────────────────────────┐
│             MemTable 结构               │
├─────────────────────────────────────────┤
│  ┌─────────────────────────────────┐   │
│  │      ConcurrentSkipList         │   │
│  │  ┌───────────────────────────┐  │   │
│  │  │ Level 3: head ────────────┼──┼──→ nullptr        │
│  │  │ Level 2: head ────────┬───┼──┼──→ nullptr        │
│  │  │ Level 1: head ──┬─────┼───┼──┼──→ nullptr        │
│  │  │ Level 0: head ─→[A]─→[B]─→[C]─→ nullptr          │
│  │  └───────────────────────────┘  │   │
│  └─────────────────────────────────┘   │
│                                         │
│  - 阈值: 3MB (自动刷盘)                  │
│  - O(log n) 查找/插入/删除               │
└─────────────────────────────────────────┘
```

### SST 文件格式

```
┌─────────────────────────────────────────────────────────────────┐
│                       SST 文件结构                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   [IndexBlock] | [DataBlock 0] | [DataBlock 1] | ...            │
│                                                                 │
│   ┌─────────────────────────────────────────────────────────┐  │
│   │                      IndexBlock                          │  │
│   │  [size(8B)] [count(8B)] [(offset + key_size + key),...] │  │
│   └─────────────────────────────────────────────────────────┘  │
│                                                                 │
│   ┌─────────────────────────────────────────────────────────┐  │
│   │                      DataBlock                           │  │
│   │  [size(8B)] [bloom_filter] [count] [(key + value),...]  │  │
│   └─────────────────────────────────────────────────────────┘  │
│                                                                 │
│   可选压缩：                                                     │
│   [compressed_size(8B)] [flags(1B)] [compressed_data...]       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 布隆过滤器

```
┌─────────────────────────────────────────────────────────────────┐
│                   布隆过滤器原理                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   元素 "hello" 的插入过程：                                     │
│                                                                 │
│   hash1("hello") = 5    ───────→  设置 bit[5]                  │
│   hash2("hello") = 12   ───────→  设置 bit[12]                 │
│   hash3("hello") = 3    ───────→  设置 bit[3]                  │
│                                                                 │
│   查询 "hello"：                                                │
│   - 检查 bit[5], bit[12], bit[3] 是否都被设置                    │
│   - 全部设置 → 可能存在（可能是假阳性）                          │
│   - 任意一个未设置 → 一定不存在                                  │
│                                                                 │
│   查询 "world"：                                                │
│   - 检查 bit[5], bit[12], bit[3]                                │
│   - bit[5] 未设置 → world 一定不存在                            │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Block Cache (块缓存)

```
┌─────────────────────────────────────────────────────────────────┐
│                      Block Cache 结构                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐    │
│   │ Block 1 │    │ Block 2 │    │ Block 3 │    │ Block N │    │
│   │ (LRU)   │    │ (LRU)   │    │ (MRU)   │    │ (MRU)   │    │
│   └────┬────┘    └────┬────┘    └────┬────┘    └────┬────┘    │
│   │      │             │             │             │          │
│   │      └─────────────┴──────┬──────┴─────────────┘          │
│   │                          │                                 │
│   │                    ┌──────▼──────┐                         │
│   │                    │   HashMap   │                         │
│   │                    │  (O(1) 查找) │                         │
│   │                    └─────────────┘                         │
│   │                                                                 │
│   配置：                                                         │
│   - 最大容量: 256MB (默认)                                       │
│   - 淘汰策略: LRU                                                │
│   - 支持命中率统计                                               │
│   └─────────────────────────────────────────────────────────────┘
```

### SST 压缩

```
┌─────────────────────────────────────────────────────────────────┐
│                    SST 压缩流程                                  │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   数据流：                                                       │
│                                                                 │
│   MemTable ──→ 序列化 ──→ 压缩 ──→ 写入 SST                    │
│                     │                                            │
│                     ▼                                            │
│              ┌──────────────┐                                    │
│              │  LZ4 / Snappy │                                    │
│              │  压缩算法      │                                    │
│              └──────────────┘                                    │
│                     │                                            │
│                     ▼                                            │
│         典型压缩比: 2-4x                                          │
│         压缩速度: LZ4 > Snappy                                    │
│                                                                 │
│   优势：                                                         │
│   - 减少磁盘 I/O                                                 │
│   - 降低存储空间                                                 │
│   - 提高缓存效率                                                 │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 异步 I/O (io_uring)

```
┌─────────────────────────────────────────────────────────────────┐
│                      io_uring 异步 I/O                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   ┌─────────────────────────────────────────────────────────┐  │
│   │                    io_uring                             │  │
│   │  ┌─────────────────┐         ┌─────────────────┐        │  │
│   │  │   Submission   │  <--->  │     Completion  │        │  │
│   │  │     Queue      │  ring   │      Queue      │        │  │
│   │  │   (用户填写)     │         │   (内核填充)    │        │  │
│   │  └─────────────────┘         └─────────────────┘        │  │
│   │                                                         │  │
│   │   用户通过内存映射直接操作队列，避免系统调用              │
│   │   支持批量提交、SQPOLL 内核轮询                          │
│   └─────────────────────────────────────────────────────────┘  │
│                                                                 │
│   优势：                                                         │
│   - 零拷贝读取（prep_read_fixed）                                │
│   - 批量提交减少系统调用                                         │
│   - 事件驱动减少用户态/内核态切换                                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 批量提交 (Batch Commit)

```
┌─────────────────────────────────────────────────────────────────┐
│                      批量提交流程                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   1. BeginBatch() - 开始批量操作                                 │
│   │        ↓                                                    │
│   2. BatchPut(key, value) - 添加 Put 操作                        │
│   │        ↓                                                    │
│   3. BatchDelete(key) - 添加 Delete 操作                         │
│   │        ↓                                                    │
│   4. CommitBatch() - 原子性提交                                  │
│   │        ↓                                                    │
│   5. 应用到 MemTable 和 WAL                                      │
│                                                                 │
│   优势：                                                         │
│   - 减少 I/O 次数                                                │
│   - 原子性批量操作                                               │
│   - 提高写入吞吐量                                               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 读 Quorum (线性化读)

```
┌─────────────────────────────────────────────────────────────────┐
│                    Quorum 读取流程                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   1. 读取请求到达                                                │
│   │        ↓                                                    │
│   2. 获取当前读时间戳                                            │
│   │        ↓                                                    │
│   3. 从多个副本/版本读取                                         │
│   │        ↓                                                    │
│   4. 验证一致性（时间戳比较）                                    │
│   │        ↓                                                    │
│   5. 返回最新版本                                                │
│                                                                 │
│   组件：                                                         │
│   - VersionManager: 跟踪键的多个版本                             │
│   - ReadQuorum: 基于 Quorum 的读取验证                          │
│   - SnapshotRead: 时间点一致的读取视图                           │
│                                                                 │
│   保证：                                                         │
│   - 线性化读：读取最新提交的数据                                 │
│   - 强一致性：所有副本数据一致                                   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Compaction 策略

采用 **Size-Tiered Compaction**：

```
┌─────────────────────────────────────────────────────────────────┐
│                   Size-Tiered Compaction                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   Level N (满)      Level N+1                                   │
│   ┌───────────┐     ┌───────────┐                              │
│   │ SST1      │     │ SST3      │                              │
│   │ SST2      │     │ SST4      │                              │
│   └───────────┘     └───────────┘                              │
│         │                   │                                   │
│         └───────┬───────────┘                                   │
│                 ▼                                               │
│   ┌─────────────────────────────────────────────────────────┐  │
│   │              合并去重后的新 SST                          │  │
│   │  - 收集 Level N 和 Level N+1 的 SST                     │  │
│   │  - 使用最小堆合并所有键值对                              │  │
│   │  - 去除重复 key（保留最新版本）                          │  │
│   │  - 生成更大的新 SST 放到 Level N+1                       │  │
│   └─────────────────────────────────────────────────────────┘  │
│                                                                 │
│   各层阈值：                                                    │
│   - Level 0:  1 KB                                             │
│   - Level 1: 10 MB                                             │
│   - Level 2: 100 MB                                            │
│   - Level 3:  1 GB                                             │
│   - Level 4: 10 GB                                             │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 数据写入流程

```
1. Client 发起 Put 请求
           │
           ▼
2. 数据写入 MemTable (SkipList)
           │
           ▼
3. MemTable 达到阈值 (3MB)?
    ├── 否：直接返回
    ├── 是：触发刷盘
           │
           ▼
4. 将 MemTable 序列化为 SST 文件
    ├── 可选：压缩数据 (LZ4/Snappy)
    └── 异步写入 (io_uring)
           │
           ▼
5. 更新 Manifest，记录新 SST
           │
           ▼
6. 检查是否需要 Compaction?
    ├── 否：完成
    ├── 是：执行 Size-Tiered Compaction
```

### 数据查询流程

```
1. Client 发起 Get 请求
           │
           ▼
2. 查询活跃 MemTable
           │
           ▼
3. 逆序查询历史 MemTable
           │
           ▼
4. 按层查询 SST (Level 0 → Level N)
    │
    ├── Level 0: 逆序遍历（新 SST 优先）
    └── Level N+: 二分查找定位 SST
           │
           ▼
5. SST 内部查询
    │
    ├── Block Cache 查找
    ├── BloomFilter 快速过滤
    └── 二分查找 DataBlock
           │
           ▼
6. 返回结果
```

---

## Raft 分布式协议

### 核心组件

| 组件 | 文件 | 描述 |
|------|------|------|
| Pod | [pod.hpp](shuaikv/raft/pod.hpp) | Raft 节点核心实现 |
| Config | [config.hpp](shuaikv/raft/config.hpp) | 集群配置管理 |
| RaftLog | [raft_log.hpp](shuaikv/raft/raft_log.hpp) | 日志条目管理 |
| Service | [service.hpp](shuaikv/raft/service.hpp) | RPC 服务实现 |

### 节点状态机

```
┌─────────────────────────────────────────────────────────────────┐
│                      Raft 状态机                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│                    ┌───────────────┐                            │
│                    │   Candidate   │                            │
│                    │  (候选者)      │                            │
│                    └───────┬───────┘                            │
│                            │                                    │
│           赢得多数票 ←──────┼───────→ 超时重新选举               │
│                            │                                    │
│           ┌────────────────┴────────────────┐                  │
│           ↓                                ↓                   │
│   ┌───────────────┐                ┌───────────────┐          │
│   │    Leader     │                │   Follower    │          │
│   │   (领导者)     │                │  (跟随者)      │          │
│   └───────────────┘                └───────────────┘          │
│           │                                │                   │
│           └──────────→ 心跳超时 ──────────→│                   │
│                          变成 Follower                         │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### RPC 接口

```protobuf
service EasyKvService {
    // 写入键值对
    rpc Put(PutReq) returns (PutRsp)

    // 读取键值
    rpc Get(GetReq) returns (GetRsp)

    // 选举投票
    rpc RequestVote(RequestVoteReq) returns (RequestVoteRsp)

    // 日志复制
    rpc Append(AppendReq) returns (AppendRsp)
}
```

### 选举流程

```
1. Follower 等待 Leader 心跳超时 (默认 5 秒)
           │
           ▼
2. 转换为 Candidate，增加任期号 (term++)
           │
           ▼
3. 投票给自己，发起 RequestVote RPC
           │
           ▼
4. 等待其他节点响应
           │
           ├── 收到多数票 → 成为 Leader
           ├── 收到更高任期号的响应 → 变为 Follower
           └── 超时 → 重新选举
```

### 日志复制流程

```
1. Client 发送 Put 请求到 Leader
           │
           ▼
2. Leader 写入本地日志 (uncommitted)
           │
           ▼
3. Leader 发送 AppendEntries RPC 到所有 Followers
           │
           ├── Follower 写入日志，返回成功
           └── Leader 统计成功数量
           │
           ▼
4. 多数节点确认后，提交日志 (committed)
           │
           ▼
5. 将日志应用到状态机 (KV Store)
           │
           ▼
6. 通知 Client 操作成功
```

### 超时配置

| 参数 | 默认值 | 说明 |
|------|--------|------|
| heartbeat_interval | 1000ms | 心跳间隔 |
| election_timeout | 5000ms | 选举超时时间 |

---

## 配置文件

### raft.cfg 格式

```bash
# 节点数量
3

# 节点ID IP 端口
1 127.0.0.1 9001
2 127.0.0.1 9002
3 127.0.0.1 9003
```

---

## 编译与运行

### 环境要求

| 依赖 | 版本 | 说明 |
|------|------|------|
| GCC/Clang | >= 11 | C++17 编译器（推荐 GCC 13） |
| CMake | >= 3.16 | 构建系统 |
| gRPC | >= 1.51 | RPC 框架 |
| Protocol Buffers | >= 3.21 | 序列化库 |
| liburing | - | Linux 异步 I/O |
| Boost | - | 并发数据结构 |
| GoogleTest | - | 测试框架 |

### 安装依赖（Ubuntu 24.04 LTS / WSL2 Ubuntu）

```bash
# 1. 更新系统
sudo apt update && sudo apt upgrade -y

# 2. 安装基础依赖
sudo apt install -y build-essential curl wget git

# 3. 安装 gRPC 和 Protocol Buffers
sudo apt install -y libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc

# 4. 安装 liburing（异步 I/O）
sudo apt install -y liburing-dev

# 5. 安装 Boost（并发容器）
sudo apt install -y libboost-dev

# 6. 安装 GoogleTest
sudo apt install -y libgtest-dev googletest

# 7. 验证安装
grpc_cpp_plugin --version
protoc --version
```

### 构建项目

#### CMake 构建

```bash
# 克隆项目
git clone https://github.com/tiammomo/Shuai-KV.git
cd Shuai-KV

# 创建构建目录
mkdir -p build && cd build

# Debug 构建（用于测试）
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build . -j$(nproc)

# Release 构建（用于生产）
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

#### 运行测试

```bash
# 配置并构建测试
cmake .. -DBUILD_TESTING=ON
cmake --build .

# 运行所有测试
ctest --output-on-failure

# 或运行单个测试
./bin/skip_list_test
./bin/lock_test
./bin/thread_pool_test
```

### 运行服务

#### 前台运行

```bash
./build/bin/shuaikv_server
```

#### Daemon 模式运行

```bash
# 后台运行，自动创建 PID 文件和日志
./build/bin/shuaikv_server -d

# 指定日志文件和 PID 文件
./build/bin/shuaikv_server -d -l /var/log/shuaikv.log -P /var/run/shuaikv.pid

# 查看日志
tail -f shuaikv.log

# 停止服务
kill $(cat shuaikv.pid)
```

### 生产环境部署

#### 1. 准备服务器

```bash
# 在生产服务器上安装运行时依赖
sudo apt install -y libprotobuf-dev libgrpc++-dev

# 创建部署目录
sudo mkdir -p /opt/shuai-kv
sudo useradd -r -s /sbin/nologin shuaikv
```

#### 2. 部署二进制文件

```bash
# 从开发环境复制二进制文件
scp bazel-bin/shuaikv/server user@production:/opt/shuai-kv/
scp bazel-bin/shuaikv/client user@production:/opt/shuai-kv/
scp raft.cfg user@production:/opt/shuai-kv/

# 设置权限
ssh user@production "cd /opt/shuai-kv && sudo chown -R shuaikv:shuaikv ."
```

#### 3. 配置系统服务

创建 systemd 服务文件 `/etc/systemd/system/shuai-kv.service`：

```ini
[Unit]
Description=shuaikv Distributed KV Store
After=network.target

[Service]
Type=simple
User=shuaikv
Group=shuaikv
WorkingDirectory=/opt/shuai-kv
ExecStart=/opt/shuai-kv/server --config=/opt/shuai-kv/raft.cfg
Restart=always
RestartSec=5

# 资源限制
LimitNOFILE=65536
MemoryMax=4G

# 日志配置
StandardOutput=journal
StandardError=journal
SyslogIdentifier=shuai-kv

[Install]
WantedBy=multi-user.target
```

```bash
# 启用并启动服务
sudo systemctl daemon-reload
sudo systemctl enable shuai-kv
sudo systemctl start shuai-kv
sudo systemctl status shuai-kv
```

#### 4. 配置日志轮转

创建 `/etc/logrotate.d/shuai-kv`：

```
/var/log/shuai-kv/*.log {
    daily
    rotate 14
    compress
    delaycompress
    missingok
    notifempty
    create 0644 shuaikv shuaikv
}
```

#### 5. 配置防火墙

```bash
# 开放服务端口（根据 raft.cfg 配置）
sudo ufw allow 9001/tcp
sudo ufw allow 9002/tcp
sudo ufw allow 9003/tcp
sudo ufw enable
```

#### 6. 配置监控（可选）

```bash
# 安装 Prometheus node_exporter
sudo apt install prometheus-node-exporter

# 或使用 systemd-metrics
sudo apt install systemd-metrics
```

### 常见问题

#### Q1: Bazel 下载依赖超时
```bash
# 使用镜像加速
export BAZEL_REPO_CACHE=/tmp/bazel-cache
bazel build //shuaikv:server --repo_env=BAZEL_REPO_CACHE=/tmp/bazel-cache
```

#### Q2: gRPC 编译失败
```bash
# 确保安装了所有依赖
sudo apt install -y libprotobuf-dev protobuf-compiler-grpc
```

#### Q3: 内存不足导致构建失败
```bash
# 减少并行编译任务
bazel build //shuaikv:server -j 2

# 或使用本地资源管理
bazel build //shuaikv:server --local_ram_resources=4096
```

#### Q4: 找不到 `grpc_cpp_plugin`
```bash
# 确认 gRPC 安装位置
which grpc_cpp_plugin
# 如果在 /usr/local/bin/，确保在 PATH 中
export PATH=$PATH:/usr/local/bin
```

### 配置集群

编辑 `raft.cfg` 文件，配置 Raft 集群节点：

```bash
# 节点数量
3

# 节点ID IP 端口
1 127.0.0.1 9001
2 127.0.0.1 9002
3 127.0.0.1 9003
```

**配置说明：**
- 第1行：集群节点总数
- 后续每行：一个节点的配置（ID IP 端口）
- 所有节点必须配置相同的文件

### 运行集群

#### 1. 单节点模式（测试）

```bash
# 启动服务器
./build/bin/shuaikv_server

# 在另一个终端启动客户端
./build/bin/shuaikv_client
```

#### 2. 多节点模式（分布式）

```bash
# 节点1 (终端1)
./build/bin/shuaikv_server --config=raft.cfg --id=1

# 节点2 (终端2)
./build/bin/shuaikv_server --config=raft.cfg --id=2

# 节点3 (终端3)
./build/bin/shuaikv_server --config=raft.cfg --id=3
```

### 客户端使用

启动客户端后，支持以下命令：

```bash
# 写入键值
put key value

# 读取键值
get key

# 同步读取（从 Leader 读取，保证线性化）
sync_get key

# 退出
quit
```

#### 示例交互

```
$ ./build/bin/shuaikv_client
> put user:1001 "John Doe"
OK
> put user:1002 "Jane Smith"
OK
> get user:1001
"John Doe"
> sync_get user:1002
"Jane Smith"
> quit
Bye
```

### Docker 部署

#### 构建镜像

```dockerfile
# Dockerfile
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    build-essential curl gnupg cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN chmod +x build.sh
RUN ./build.sh

CMD ["./build/bin/shuaikv_server"]
```

```bash
# 构建
docker build -t shuai-kv .

# 运行
docker run -d -p 9001:9001 --name kv-node1 shuai-kv
```

---

## 解决的场景

### 场景1：高并发写入

**问题描述：**
- 传统 B+ 树存储引擎在随机写入时会产生大量随机 I/O
- 随机 I/O 的性能远低于顺序 I/O
- 高并发写入场景下，磁盘成为瓶颈

**解决方案：**
```
┌─────────────────────────────────────────────────────────────────┐
│                     高并发写入场景                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   写入流程：                                                    │
│   1. 写入请求 → MemTable (内存，O(log n))                       │
│                  │                                               │
│                  ▼                                               │
│         批量刷盘成 SST (顺序写入)                                │
│                  │                                               │
│                  ▼                                               │
│         Compaction 合并 (后台顺序读写)                           │
│                                                                 │
│   优化手段：                                                    │
│   - 异步 I/O (io_uring)                                        │
│   - 批量提交 (Batch Commit)                                     │
│   - SST 压缩 (减少 I/O 量)                                      │
│                                                                 │
│   优势：                                                        │
│   - 所有磁盘写入都是顺序的                                       │
│   - 内存操作 + 批量顺序写，吞吐量极高                            │
│   - 适合写密集型工作负载                                         │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**典型应用：**
- 日志收集系统
- 事件追踪
- 时序数据存储
- 用户行为分析

### 场景2：分布式一致性

**问题描述：**
- 单节点故障导致服务不可用
- 数据丢失风险
- 多副本数据不一致

**解决方案：**
```
┌─────────────────────────────────────────────────────────────────┐
│                    分布式一致性场景                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   Raft 复制组：                                                 │
│                                                                 │
│   ┌─────────┐     ┌─────────┐     ┌─────────┐                 │
│   │  Node 1 │     │  Node 2 │     │  Node 3 │                 │
│   │ Leader  │ ───→│ Follower│ ───→│ Follower│                 │
│   │  (写入)  │     │  (复制) │     │  (复制) │                 │
│   └────┬────┘     └─────────┘     └─────────┘                 │
│        │                                                    │
│        │ 多数派确认 (2/3)                                    │
│        ▼                                                    │
│   数据安全持久化                                               │
│                                                                 │
│   容错能力：                                                   │
│   - 3节点：容忍 1 节点故障                                     │
│   - 5节点：容忍 2 节点故障                                     │
│                                                                 │
│   读优化：                                                      │
│   - 读 Quorum 保证线性化读                                      │
│   - 版本验证确保数据一致性                                      │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**核心保证：**
- **线性化读**：读取最新提交的数据
- **强一致性**：所有副本数据一致
- **高可用**：节点故障自动切换

**典型应用：**
- 配置中心
- 服务发现
- 分布式锁
- 元数据存储

### 场景3：海量数据存储

**问题描述：**
- 单机存储容量有限
- 数据量增长后需要扩容
- 存储成本控制

**解决方案：**
```
┌─────────────────────────────────────────────────────────────────┐
│                     海量数据存储场景                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   LSM Tree 分层存储：                                           │
│                                                                 │
│   Level 0:  ~1 KB    │ 新写入的 SST                             │
│   Level 1: ~10 MB    │                                          │
│   Level 2: ~100 MB   │     逐层合并                              │
│   Level 3:   ~1 GB   │                                          │
│   Level 4:  ~10 GB   │                                          │
│                                                                 │
│   数据流动：                                                    │
│   MemTable ──→ L0 ──→ L1 ──→ L2 ──→ L3 ──→ L4                 │
│                   │       │       │       │       │             │
│                   └── Compaction ─────────────────┘             │
│                                                                 │
│   优化手段：                                                    │
│   - SST 压缩 (LZ4/Snappy): 存储空间减少 2-4x                    │
│   - Block Cache: 热数据加速读取                                 │
│                                                                 │
│   优势：                                                        │
│   - 成本效益：冷数据自动下沉到大容量存储                         │
│   - 自动管理：无需手动分区或分片                                 │
│   - 高效查询：热数据在高层，查询快                               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**典型应用：**
- 日志存储与分析
- 监控数据存储
- 审计追踪
- 大数据缓存层

### 场景4：低延迟读取

**问题描述：**
- 磁盘读取延迟高
- 热数据被冷数据淹没
- 查询响应时间长

**解决方案：**
```
┌─────────────────────────────────────────────────────────────────┐
│                      低延迟读取场景                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   多级缓存：                                                    │
│                                                                 │
│   ┌─────────────────────────────────────────────────────────┐  │
│   │                    查询路径                              │  │
│   │                                                          │  │
│   │   1. MemTable (内存，最新数据)    ──→  O(1)             │  │
│   │         │                                                │  │
│   │         ▼                                                │  │
│   │   2. 历史 MemTable (内存)          ──→  O(log n)        │  │
│   │         │                                                │  │
│   │         ▼                                                │  │
│   │   3. Block Cache (LRU 缓存)        ──→  亚毫秒级         │  │
│   │         │                                                │  │
│   │         ▼                                                │  │
│   │   4. Level 0 SST (磁盘，热数据)   ──→  毫秒级           │  │
│   │         │                                                │  │
│   │         ▼                                                │  │
│   │   5. Level N SST (磁盘，冷数据)   ──→  稍慢             │  │
│   │                                                          │  │
│   └─────────────────────────────────────────────────────────┘  │
│                                                                 │
│   优化手段：                                                    │
│   - Block Cache: LRU 淘汰策略，加速热数据                       │
│   - 布隆过滤器：快速排除不存在的 key                            │
│   - mmap：利用 OS 页缓存                                        │
│   - Compaction：清理无效数据                                    │
│   - 读 Quorum：版本验证优化读取                                 │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**典型应用：**
- 实时推荐系统
- 缓存后端
- 会话存储
- 排行榜/计数器

### 场景5：故障恢复

**问题描述：**
- 节点崩溃后数据丢失
- 恢复时间过长
- 数据不一致

**解决方案：**
```
┌─────────────────────────────────────────────────────────────────┐
│                      故障恢复场景                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   故障检测与恢复：                                              │
│                                                                 │
│   1. Follower 心跳超时                                          │
│              │                                                  │
│              ▼                                                  │
│   2. 触发选举（随机超时 5-10秒）                                 │
│              │                                                  │
│              ▼                                                  │
│   3. 选出新 Leader                                              │
│              │                                                  │
│              ▼                                                  │
│   4. 日志补齐（Leader 推送缺失日志）                             │
│              │                                                  │
│              ▼                                                  │
│   5. 恢复正常服务                                               │
│                                                                 │
│   Recovery Time Objective (RTO): ~10 秒                        │
│   Recovery Point Objective (RPO): 0（无数据丢失）               │
│                                                                 │
│   数据保护：                                                    │
│   - Raft 日志复制                                               │
│   - 定期快照                                                    │
│   - WAL 预写日志                                                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**典型应用：**
- 关键业务系统
- 金融交易系统
- 订单处理
- 消息队列

---

## 架构决策

| 决策 | 选择 | 原因 |
|------|------|------|
| 存储引擎 | LSM Tree | 适合写密集型场景，顺序写入性能高 |
| 一致性协议 | Raft | 实现简单，保证强一致性 |
| Compaction | Size-Tiered | 实现简单，适合大部分场景 |
| 文件映射 | mmap | 利用 OS 页缓存，减少拷贝 |
| 序列化 | Protocol Buffers | 高效、跨语言 |
| RPC 框架 | gRPC | 高性能、成熟稳定 |
| 构建系统 | CMake | 简单易用，生态丰富 |
| 压缩算法 | LZ4/Snappy | 速度与压缩率平衡 |
| 异步 I/O | io_uring | Linux 高性能异步 I/O |
| 缓存策略 | LRU Block Cache | 实现简单，命中率可观 |

---

## 性能基准

### 测试环境

| 配置 | 说明 |
|------|------|
| CPU | 8 核 Intel Xeon |
| 内存 | 16 GB |
| 磁盘 | NVMe SSD |
| 网络 | 千兆以太网 |

### 预期性能

| 指标 | 预期值 |
|------|--------|
| 写入吞吐量 | 10,000+ QPS |
| 读取延迟 | P99 < 10ms |
| 复制延迟 | < 100ms |
| 故障恢复 | < 10s |
| 压缩比 | 2-4x (视数据而定) |
| 缓存命中率 | > 80% (热数据) |

---

## 性能优化

### 已实现的优化

1. **MemTable 刷盘**: 使用条件变量避免忙等
2. **布隆过滤器**: 减少无效磁盘 I/O
3. **mmap 文件映射**: 避免内核态和用户态数据拷贝
4. **Copy-on-Write Manifest**: 读操作无需加锁
5. **读写锁分离**: 一写多读模式
6. **SST 压缩**: 支持 Snappy/LZ4 压缩算法，减少存储空间和 I/O
7. **Block Cache**: LRU 缓存层，加速热数据读取
8. **异步 I/O**: 基于 io_uring 的异步写入，提高吞吐量
9. **批量提交**: 事务性批量写入，减少开销
10. **读 Quorum**: 版本验证和线性化读优化

---

## 技术文档

项目提供了详细的学习文档，位于 [learn_docs](learn_docs/) 目录：

```
learn_docs/
├── README.md              # 文档索引和阅读指南
├── 00-overview.md         # 项目概述和技术栈
├── 01-architecture.md     # 系统分层架构
├── 02-skiplist.md         # SkipList 跳表实现
├── 03-memtable.md         # MemTable 内存表
├── 04-sst.md              # SST 磁盘存储
├── 05-manifest.md         # Manifest 元数据管理
├── 06-bloom-filter.md     # 布隆过滤器
├── 07-block-cache.md      # BlockCache 块缓存
├── 08-db-layer.md         # DB 层设计
├── 09-raft-protocol.md    # Raft 分布式协议
├── 10-cache-system.md     # 缓存系统 (LRU/W-TinyLFU)
├── 11-compression.md      # 压缩系统 (LZ4/Snappy)
├── 12-utils.md            # 工具类 (RWLock/ThreadPool等)
└── 13-module-relationship.md  # 模块交互关系图
```

**阅读建议**：
1. 先读 `learn_docs/README.md` 了解文档组织
2. 按 `00 → 01 → 08` 顺序入门
3. 存储引擎深入：`02 → 05 → 04`
4. 分布式深入：`09`

---

## 许可证

MIT License

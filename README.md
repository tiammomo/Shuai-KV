# Shuai-KV

基于 **Raft 协议**的分布式 KV 存储系统，采用 **LSM Tree** 作为存储引擎。

## 特性

- **高性能写入**: LSM Tree 天然支持顺序磁盘写入
- **强一致性**: Raft 协议保证分布式强一致性
- **并发安全**: 读写锁 + 细粒度锁保证多线程安全
- **模块化设计**: 清晰的分层架构，易于维护和扩展
- **布隆过滤器**: 减少无效磁盘访问，提升查询性能

## 技术栈

| 组件 | 技术 |
|------|------|
| 语言 | C++17 |
| 构建系统 | Bazel |
| 网络通信 | gRPC |
| 存储引擎 | LSM Tree |
| 分布式协议 | Raft |
| 并发控制 | 读写锁 (RWLock) |

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
│   └─────────────────────────────────────────────────────────┘  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## 目录结构

```
Shuai-KV/
├── easykv/                    # 主代码目录
│   ├── lsm/                   # LSM 树存储引擎
│   │   ├── memtable.hpp       # 内存表（跳表实现）
│   │   ├── skiplist.hpp       # 并发安全跳表
│   │   ├── sst.hpp            # SST 磁盘文件
│   │   └── manifest.hpp       # Manifest 元数据管理
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
├── raft.cfg                   # Raft 配置文件
├── build.sh                   # 构建脚本
└── MODULE.bazel               # Bazel 构建配置
```

---

## LSM Tree 存储引擎

### 核心组件

| 组件 | 文件 | 职责 |
|------|------|------|
| MemTable | [memtable.hpp](easykv/lsm/memtable.hpp) | 内存中的有序键值表 |
| SkipList | [skiplist.hpp](easykv/lsm/skiplist.hpp) | 并发安全的内存索引 |
| SST | [sst.hpp](easykv/lsm/sst.hpp) | 磁盘排序字符串表 |
| Manifest | [manifest.hpp](easykv/lsm/manifest.hpp) | SST 元数据与分层管理 |
| BloomFilter | [bloom_filter.hpp](easykv/utils/bloom_filter.hpp) | 快速存在性判断 |

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
| Pod | [pod.hpp](easykv/raft/pod.hpp) | Raft 节点核心实现 |
| Config | [config.hpp](easykv/raft/config.hpp) | 集群配置管理 |
| RaftLog | [raft_log.hpp](easykv/raft/raft_log.hpp) | 日志条目管理 |
| Service | [service.hpp](easykv/raft/service.hpp) | gRPC 服务实现 |

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

## 核心数据结构

### SkipList 节点

```cpp
struct Node {
    std::vector<Node*> nexts;   // 多层指针
    std::string key;            // 键
    std::string value;          // 值
    easykv::common::RWLock rw_lock;  // 读写锁
}
```

### Raft Entry

```protobuf
message Entry {
    int32 term;       // 任期号
    int32 index;      // 日志索引
    string key;       // 操作 key
    string value;     // 操作 value
    int32 mode;       // 0: put, 1: delete
    int32 committed;  // 提交标志
}
```

### EntryIndex (SST 索引)

```cpp
struct EntryIndex {
    std::string_view key;      // 键（指向 mmap 内存）
    std::string_view value;    // 值（指向 mmap 内存）
    size_t offset;             // 在 DataBlock 中的偏移量
}
```

---

## 编译与运行

### 环境要求

| 依赖 | 版本 | 说明 |
|------|------|------|
| GCC/Clang | >= 9 | C++17 编译器 |
| Bazel | >= 3.0 | 构建系统 |
| gRPC | >= 1.30 | RPC 框架 |
| Protocol Buffers | >= 3.12 | 序列化库 |
| CMake | >= 3.10 | 用于 gRPC 安装 |

### 安装依赖

#### Ubuntu/Debian

```bash
# 安装基础依赖
sudo apt-get update
sudo apt-get install -y build-essential curl zip unzip tar

# 安装 CMake
wget https://github.com/Kitware/CMake/releases/download/v3.25.0/cmake-3.25.0-linux-x86_64.sh
sudo sh cmake-3.25.0-linux-x86_64.sh --prefix=/usr/local --skip-license

# 安装 gRPC 和 Protocol Buffers
git clone --recurse-submodules -b v1.50.0 https://github.com/grpc/grpc
cd grpc
mkdir -p cmake/build
cd cmake/build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF \
      ../..
make -j$(nproc)
sudo make install
sudo ldconfig
```

#### macOS

```bash
# 使用 Homebrew 安装
brew install bazel cmake protobuf grpc
```

#### Windows

```bash
# 使用 vcpkg 安装
vcpkg install bazel grpc protobuf
```

### 构建项目

```bash
# 克隆项目
git clone https://github.com/yourusername/Shuai-KV.git
cd Shuai-KV

# 使用构建脚本
./build.sh

# 或者直接使用 Bazel
bazel build //easykv:server //easykv:client
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
./bazel-bin/easykv/server

# 在另一个终端启动客户端
./bazel-bin/easykv/client
```

#### 2. 多节点模式（分布式）

```bash
# 节点1 (终端1)
./bazel-bin/easykv/server --config=raft.cfg --id=1

# 节点2 (终端2)
./bazel-bin/easykv/server --config=raft.cfg --id=2

# 节点3 (终端3)
./bazel-bin/easykv/server --config=raft.cfg --id=3
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
$ ./bazel-bin/easykv/client
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
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential curl gnupg \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN chmod +x build.sh
RUN ./build.sh

CMD ["./bazel-bin/easykv/server"]
```

```bash
# 构建
docker build -t shuai-kv .

# 运行
docker run -d -p 9001:9001 --name kv-node1 shuai-kv
```

### 性能测试

```bash
# 使用 wrk 进行压力测试（需要安装 wrk）
wrk -t4 -c100 -d30s http://localhost:9001/kv?key=test
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
│   │   3. Level 0 SST (磁盘，热数据)   ──→  毫秒级           │  │
│   │         │                                                │  │
│   │         ▼                                                │  │
│   │   4. Level N SST (磁盘，冷数据)   ──→  稍慢             │  │
│   │                                                          │  │
│   └─────────────────────────────────────────────────────────┘  │
│                                                                 │
│   优化手段：                                                    │
│   - 布隆过滤器：快速排除不存在的 key                            │
│   - mmap：利用 OS 页缓存                                        │
│   - Compaction：清理无效数据                                    │
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
| 构建系统 | Bazel | 支持大规模构建，快速增量编译 |

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

---

## 性能优化

### 已实现的优化

1. **MemTable 刷盘**: 使用条件变量避免忙等
2. **布隆过滤器**: 减少无效磁盘 I/O
3. **mmap 文件映射**: 避免内核态和用户态数据拷贝
4. **Copy-on-Write Manifest**: 读操作无需加锁
5. **读写锁分离**: 一写多读模式

### 未来优化方向

- [ ] 压缩（SST 压缩）
- [ ] 缓存优化（Block Cache）
- [ ] 异步 I/O（io_uring）
- [ ] 批量提交（Batch Commit）
- [ ] 读 quorum（线性化读优化）

---

## 许可证

MIT License

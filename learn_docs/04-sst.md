# SST 文件存储

**文件**: `lsm/sst.hpp`

SST (Sorted String Table) 是磁盘上的持久化有序键值对存储，使用 mmap 映射文件。

## SST 文件格式

```
┌─────────────────────────────────────────────────────────────────┐
│  [IndexBlock] | [DataBlock 0] | [DataBlock 1] | ...             │
└─────────────────────────────────────────────────────────────────┘

IndexBlock 存储每个 DataBlock 的元信息，用于快速定位
DataBlock 存储实际的键值对数据
```

### IndexBlock 格式

```
┌────────────────────────────────────────────────────────────┐
│ [total_size: 8B] [count: 8B] [(offset + key_size + key), ...] │
└────────────────────────────────────────────────────────────┘
```

| 字段 | 大小 | 说明 |
|------|------|------|
| total_size | 8 字节 | IndexBlock 总大小 |
| count | 8 字节 | DataBlock 数量 |
| entries | 变长 | 每个 entry 包含 offset、key_size、key |

**作用**: 快速定位目标 key 所在的 DataBlock

### DataBlock 格式

```
┌────────────────────────────────────────────────────────────┐
│ [block_size: 8B] [bloom_filter] [count: 8B] [(kv_pairs)... ] │
└────────────────────────────────────────────────────────────┘
```

| 字段 | 大小 | 说明 |
|------|------|------|
| block_size | 8 字节 | DataBlock 大小 |
| bloom_filter | 变长 | 布隆过滤器 |
| count | 8 字节 | 键值对数量 |
| kv_pairs | 变长 | 键值对数据 |

**kv_pair 格式**:
```
┌─────────────────┬───────────────────┬─────────────┬───────────┐
│ key_size: 8B │ value_size: 8B │ key bytes │ value bytes │
└─────────────────┴───────────────────┴─────────────┴───────────┘
```

## 核心数据结构

### EntryIndex

```cpp
struct EntryIndex {
    std::string_view key;   // 键的视图（指向 mmap 内存）
    std::string_view value; // 值的视图（指向 mmap 内存）
    size_t offset;          // 在 DataBlock 中的偏移量
};
```

### IndexBlockIndex

```cpp
class IndexBlockIndex {
public:
    void Init(const char* data, size_t size);

    // 二分查找 key 所在的 DataBlock
    int64_t FindBlockIndex(std::string_view key);

    // 获取指定 block 的起始偏移量
    size_t GetBlockOffset(size_t block_index);

    // 获取指定 block 的起始 key
    std::string_view GetBlockFirstKey(size_t block_index);

    size_t GetBlockCount();

private:
    std::vector<size_t> block_offsets_;     // 每个 DataBlock 的起始偏移量
    std::vector<std::string> block_keys_;   // 每个 DataBlock 的起始 key
    size_t total_size_;                     // IndexBlock 总大小
    size_t count_;                          // DataBlock 数量
};
```

### SST 核心字段

```cpp
class SST {
private:
    bool ready_ = false;
    int64_t id_ = 0;                        // SST 文件 ID
    std::string name_;                      // 文件名 (id.sst)
    char* data_ = nullptr;                  // mmap 映射的内存地址
    int fd_ = -1;                           // 文件描述符
    IndexBlockIndex index_block;            // 索引块
    bool loaded_ = false;
    size_t file_size_ = 0;
    CompressionConfig compression_config_;  // 压缩配置
    size_t uncompressed_size_{0};           // 未压缩大小
    BlockCache* block_cache_{nullptr};      // Block Cache
};
```

## 核心方法

### 1. 查找 (Get)

```cpp
bool SST::Get(std::string_view key, std::string& value) {
    // 1. 在 IndexBlock 中二分查找目标 key 所在的 DataBlock
    int64_t block_index = index_block.FindBlockIndex(key);
    if (block_index < 0) return false;

    // 2. 加载对应 DataBlock（可能从 BlockCache 获取）
    std::string_view block_data = GetDataBlock(block_index);

    // 3. 使用布隆过滤器快速判断
    BloomFilter bf;
    bf.Load(block_data);
    if (!bf.MightContain(key)) return false;

    // 4. 在 DataBlock 中二分查找目标 key
    return DataBlockGet(block_data, key, value);
}
```

### 2. 创建 SST

```cpp
std::shared_ptr<SST> SST::Create(
    const MemTable& memtable,
    int64_t id,
    const CompressionConfig& config
) {
    auto sst = std::make_shared<SST>();
    sst->id_ = id;
    sst->name_ = std::to_string(id) + ".sst";
    sst->compression_config_ = config;

    // 1. 收集所有键值对
    std::vector<std::pair<std::string, std::string>> kvs;
    for (auto it = memtable.begin(); it != memtable.end(); ++it) {
        auto [k, v] = *it;
        kvs.emplace_back(std::string(k), std::string(v));
    }

    // 2. 写入 DataBlocks
    std::vector<BlockData> blocks = WriteDataBlocks(kvs, config);

    // 3. 写入 IndexBlock
    WriteIndexBlock(blocks);

    // 4. 创建布隆过滤器
    WriteBloomFilter(blocks);

    return sst;
}
```

### 3. 迭代器

```cpp
class Iterator {
public:
    Iterator(SST* sst);

    // 移动到指定 key
    void Seek(std::string_view key);

    // 移动到第一个键值对
    void SeekToFirst();

    // 是否有效
    bool Valid();

    // 当前键值对
    std::pair<std::string_view, std::string_view> operator*();

    // 移动到下一个
    void Next();

private:
    SST* sst_;
    int64_t block_index_;       // 当前 DataBlock 索引
    size_t entry_index_;        // 当前 entry 索引
    std::vector<EntryIndex> entries_;  // 当前 DataBlock 的所有 entries
};
```

## mmap 优势

```cpp
// 使用 mmap 映射文件
char* SST::MmapFile(const std::string& name, size_t size) {
    int fd = open(name.c_str(), O_RDONLY);
    char* data = static_cast<char*>(
        mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0)
    );
    close(fd);  // 关闭后仍可访问
    return data;
}
```

| 优势 | 说明 |
|------|------|
| 减少拷贝 | 直接映射用户空间，无需内核拷贝 |
| 页缓存复用 | 利用 OS 页缓存，自动管理内存 |
| 按需加载 | 只访问需要的页面 |
| 简单实现 | 不需要管理文件缓冲区 |

## BlockCache 集成

```cpp
void SST::SetBlockCache(BlockCache* cache) {
    block_cache_ = cache;
}

std::string_view SST::GetDataBlock(size_t block_index) {
    // 1. 尝试从 BlockCache 获取
    auto key = BlockCache::MakeKey(id_, block_index);
    auto block = block_cache_->Get(key);
    if (block != nullptr) {
        return std::string_view(block->data_.data(), block->data_.size());
    }

    // 2. 未命中，从 mmap 加载
    auto offset = index_block.GetBlockOffset(block_index);
    auto size = index_block.GetBlockOffset(block_index + 1) - offset;

    // 3. 放入缓存
    auto block_data = std::string_view(data_ + offset, size);
    block_cache_->Put(key, block_data);

    return block_data;
}
```

## 与 MemTable 的关系

| 特性 | MemTable | SST |
|------|----------|-----|
| 存储位置 | 内存 | 磁盘 |
| 持久化 | 否 | 是 |
| 访问速度 | 快 | 慢（需要 I/O） |
| 容量 | 受内存限制 | 受磁盘限制 |
| 触发条件 | 内存不足 | MemTable 刷盘 |

## 复杂度分析

| 操作 | 复杂度 |
|------|--------|
| 查找 | O(log num_blocks + log entries_per_block) |
| 迭代 | O(1) |

## 测试验证

SST 相关功能通过 `compaction_test` 测试：

| 测试项 | 状态 |
|--------|------|
| Compaction 测试 | PASS |

> 注：compaction_test 验证了 SST 文件的创建、读取和合并功能。

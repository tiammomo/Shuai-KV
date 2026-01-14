# 压缩系统

**文件**: `utils/compression.hpp`

提供 LZ4 和 Snappy 两种压缩算法，支持压缩接口抽象。

## 压缩类型

```cpp
enum class CompressionType {
    kNone = 0,   // 无压缩
    kSnappy = 1, // Snappy 压缩
    kLZ4 = 2     // LZ4 压缩
};
```

## 压缩器接口

```cpp
class Compressor {
public:
    virtual ~Compressor() = default;

    /**
     * 获取压缩类型
     */
    virtual CompressionType type() const = 0;

    /**
     * 压缩数据
     * @param data 原始数据
     * @param size 原始数据大小
     * @return 压缩后的数据
     */
    virtual CompressedData Compress(const char* data, size_t size) = 0;

    /**
     * 解压数据
     * @param compressed_data 压缩数据
     * @param output 输出缓冲区
     * @param output_size 输出缓冲区大小
     * @return 实际解压大小
     */
    virtual size_t Decompress(
        const CompressedData& compressed_data,
        char* output,
        size_t output_size
    ) = 0;

    /**
     * 获取压缩后的最大大小
     */
    virtual size_t GetMaxCompressedSize(size_t input_size) const = 0;
};
```

## 压缩数据格式

```cpp
struct CompressedData {
    std::vector<char> data;      // 压缩后的数据
    size_t original_size;        // 原始大小
    CompressionType type;        // 压缩类型
};
```

## LZ4 压缩器

### 特点

| 特性 | 说明 |
|------|------|
| 压缩速度 | 非常快（GB/s 级） |
| 解压速度 | 非常快（GB/s 级） |
| 压缩比 | 约 2-3x |
| 内存占用 | 低 |

### 实现

```cpp
class LZ4Compressor : public Compressor {
public:
    CompressionType type() const override {
        return CompressionType::kLZ4;
    }

    CompressedData Compress(const char* data, size_t size) override {
        // 计算最大压缩大小
        size_t max_size = LZ4_compressBound(size);

        CompressedData result;
        result.data.resize(max_size);
        result.original_size = size;
        result.type = CompressionType::kLZ4;

        // 执行压缩
        int compressed_size = LZ4_compress_default(
            data,
            result.data.data(),
            size,
            max_size
        );

        if (compressed_size > 0) {
            result.data.resize(compressed_size);
        } else {
            // 压缩失败，返回原始数据
            result.data.assign(data, data + size);
            result.type = CompressionType::kNone;
        }

        return result;
    }

    size_t Decompress(
        const CompressedData& compressed_data,
        char* output,
        size_t output_size
    ) override {
        if (compressed_data.type != CompressionType::kLZ4) {
            // 未压缩，直接拷贝
            size_t copy_size = std::min(compressed_data.data.size(), output_size);
            std::memcpy(output, compressed_data.data.data(), copy_size);
            return copy_size;
        }

        int decompressed = LZ4_decompress_safe(
            compressed_data.data.data(),
            output,
            compressed_data.data.size(),
            output_size
        );

        return decompressed > 0 ? decompressed : 0;
    }

    size_t GetMaxCompressedSize(size_t input_size) const override {
        return LZ4_compressBound(input_size) + 4;  // +4 用于存储原始大小
    }
};
```

## Snappy 压缩器

### 特点

| 特性 | 说明 |
|------|------|
| 压缩速度 | 快（100+ MB/s） |
| 解压速度 | 非常快（500+ MB/s） |
| 压缩比 | 约 1.5-2x |
| CPU 占用 | 低 |

### 实现

```cpp
class SnappyCompressor : public Compressor {
public:
    CompressionType type() const override {
        return CompressionType::kSnappy;
    }

    CompressedData Compress(const char* data, size_t size) override {
        CompressedData result;
        result.original_size = size;
        result.type = CompressionType::kSnappy;

        // 计算压缩后大小
        size_t max_size = snappy::MaxCompressedLength(size);
        result.data.resize(max_size);

        size_t compressed_size;
        snappy::RawCompress(data, size, result.data.data(), &compressed_size);
        result.data.resize(compressed_size);

        return result;
    }

    size_t Decompress(
        const CompressedData& compressed_data,
        char* output,
        size_t output_size
    ) override {
        if (compressed_data.type != CompressionType::kSnappy) {
            size_t copy_size = std::min(compressed_data.data.size(), output_size);
            std::memcpy(output, compressed_data.data.data(), copy_size);
            return copy_size;
        }

        size_t uncompressed_length;
        if (!snappy::GetUncompressedLength(
            compressed_data.data.data(),
            compressed_data.data.size(),
            &uncompressed_length
        )) {
            return 0;
        }

        if (uncompressed_length > output_size) {
            return 0;
        }

        snappy::RawUncompress(
            compressed_data.data.data(),
            compressed_data.data.size(),
            output
        );

        return uncompressed_length;
    }

    size_t GetMaxCompressedSize(size_t input_size) const override {
        return snappy::MaxCompressedLength(input_size);
    }
};
```

## 压缩工厂

```cpp
class CompressionFactory {
public:
    static std::unique_ptr<Compressor> Create(CompressionType type) {
        switch (type) {
            case CompressionType::kSnappy:
                return std::make_unique<SnappyCompressor>();
            case CompressionType::kLZ4:
                return std::make_unique<LZ4Compressor>();
            case CompressionType::kNone:
            default:
                return nullptr;
        }
    }
};
```

## 压缩配置

```cpp
struct CompressionConfig {
    CompressionType type = CompressionType::kLZ4;  // 默认 LZ4
    bool enable_compression = true;                // 是否启用压缩
    size_t min_size_to_compress = 64;              // 最小压缩大小
};
```

## 在 SST 中的应用

```cpp
class SST {
private:
    CompressionConfig compression_config_;

public:
    static std::shared_ptr<SST> Create(
        const MemTable& memtable,
        int64_t id,
        const CompressionConfig& config
    ) {
        auto sst = std::make_shared<SST>();
        sst->id_ = id;
        sst->compression_config_ = config;

        if (config.enable_compression) {
            sst->CompressData(memtable);
        } else {
            sst->WriteData(memtable);
        }

        return sst;
    }

private:
    void CompressData(const MemTable& memtable) {
        auto compressor = CompressionFactory::Create(compression_config_.type);

        // 收集所有键值对
        std::vector<std::pair<std::string, std::string>> kvs;
        for (auto it = memtable.begin(); it != memtable.end(); ++it) {
            auto [k, v] = *it;
            kvs.emplace_back(std::string(k), std::string(v));
        }

        // 写入压缩后的 DataBlock
        for (const auto& [key, value] : kvs) {
            std::string data = key + value;
            auto compressed = compressor->Compress(data.data(), data.size());

            // 写入文件
            WriteCompressedBlock(compressed);
        }
    }
};
```

## 压缩算法对比

| 特性 | LZ4 | Snappy |
|------|-----|--------|
| 压缩速度 | 极快 | 快 |
| 解压速度 | 极快 | 更快 |
| 压缩比 | 2-3x | 1.5-2x |
| 内存占用 | 低 | 中 |
| 适用场景 | 实时处理 | 通用压缩 |

## 选择建议

```
┌─────────────────────────────────────────────────────────────┐
│                    压缩算法选择                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  优先选择 LZ4:                                              │
│  - 需要高吞吐量的写入场景                                    │
│  - CPU 资源有限                                             │
│  - 数据重复率较高                                           │
│                                                             │
│  选择 Snappy:                                               │
│  - 需要更好的压缩比                                          │
│  - 读取密集型场景                                           │
│  - 存储空间紧张                                             │
│                                                             │
│  不压缩:                                                    │
│  - 数据已压缩（如 JPEG, MP3）                               │
│  - 数据非常小（< 64 字节）                                  │
│  - CPU 是瓶颈                                               │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

> **说明**: 压缩功能通过 `compaction_test` 验证，该测试包含 SST 文件的创建和读取。

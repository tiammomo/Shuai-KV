/**
 * @file compression.hpp
 * @brief 数据压缩支持 - Snappy/LZ4 压缩算法封装
 *
 * 支持两种压缩算法：
 * - Snappy: Google 高速压缩算法，适合 x86 架构
 * - LZ4: 极高速压缩算法，压缩率与速度的平衡
 *
 * @section compression_benefits 压缩收益
 * - 减少磁盘 I/O：压缩后数据更小，读取更快
 * - 降低存储空间：典型压缩比 2-4x
 * - 提高缓存效率：更多数据可放入 Block Cache
 *
 * @section compression_format 压缩数据格式
 * ┌─────────────────────────────────────────────────────────────────┐
 * │              压缩 Block（存储在 DataBlock 中）                   │
 * ├─────────────────────────────────────────────────────────────────┤
 * │  [compressed_size(4B)] [original_size(4B)] [compressed_data...] │
 * │        │               │                    │                    │
 * │    压缩后大小      原始大小           压缩数据                   │
 * └─────────────────────────────────────────────────────────────────┘
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace shuaikv {
namespace common {

/**
 * @brief 压缩类型枚举
 */
enum class CompressionType {
    kNone = 0,   ///< 无压缩
    kSnappy = 1, ///< Snappy 压缩
    kLZ4 = 2     ///< LZ4 压缩
};

/**
 * @brief 压缩结果类
 */
class CompressedData {
public:
    CompressedData() = default;

    CompressedData(std::vector<char>&& data, size_t original_size)
        : data_(std::move(data)), original_size_(original_size) {}

    CompressedData(const char* data, size_t size, size_t original_size)
        : data_(data, data + size), original_size_(original_size) {}

    /// @brief 获取压缩后的数据指针
    const char* data() const { return data_.data(); }

    /// @brief 获取压缩后的大小
    size_t size() const { return data_.size(); }

    /// @brief 获取原始数据大小
    size_t original_size() const { return original_size_; }

    /// @brief 检查是否为空
    bool empty() const { return data_.empty(); }

private:
    std::vector<char> data_;      ///< 压缩后的数据
    size_t original_size_{0};     ///< 原始数据大小
};

/**
 * @brief 压缩器基类
 */
class Compressor {
public:
    virtual ~Compressor() = default;

    /// @brief 获取压缩类型
    virtual CompressionType type() const = 0;

    /// @brief 压缩数据
    /// @param data 输入数据指针
    /// @param size 输入数据大小
    /// @return 压缩结果
    virtual CompressedData Compress(const char* data, size_t size) = 0;

    /// @brief 解压数据
    /// @param compressed_data 压缩数据
    /// @param output 输出缓冲区（需预先分配足够空间）
    /// @param output_size 输出缓冲区大小
    /// @return 实际解压大小
    virtual size_t Decompress(const CompressedData& compressed_data,
                              char* output, size_t output_size) = 0;

    /// @brief 获取解压后所需缓冲区大小
    virtual size_t GetDecompressedSize(const char* compressed_data, size_t compressed_size) = 0;

    /// @brief 计算压缩后的最大大小
    virtual size_t MaxCompressedSize(size_t original_size) = 0;
};

/**
 * @brief Snappy 压缩器
 *
 * Snappy 是 Google 开发的高速压缩算法：
 * - 压缩速度：数百 MB/s
 * - 解压速度：GB/s 级别
 * - 压缩比：通常 1.5-2x
 */
class SnappyCompressor : public Compressor {
public:
    CompressionType type() const override { return CompressionType::kSnappy; }

    CompressedData Compress(const char* data, size_t size) override {
        if (size == 0) {
            return CompressedData({}, 0);
        }

        // Snappy 压缩：输出大小最大为 32 + size * 1.0001
        size_t max_output = MaxCompressedSize(size);
        std::vector<char> output(max_output);

        size_t compressed_size = 0;
        snappy_compress(data, size, output.data(), &compressed_size);
        output.resize(compressed_size);

        return CompressedData(std::move(output), size);
    }

    size_t Decompress(const CompressedData& compressed_data,
                      char* output, size_t output_size) override {
        if (compressed_data.empty() || output_size < compressed_data.original_size()) {
            return 0;
        }
        snappy_uncompressed_length(compressed_data.data(), compressed_data.size(),
                                   &output_size);
        snappy_uncompress(compressed_data.data(), compressed_data.size(), output);
        return output_size;
    }

    size_t GetDecompressedSize(const char* compressed_data, size_t compressed_size) override {
        size_t result = 0;
        snappy_uncompressed_length(compressed_data, compressed_size, &result);
        return result;
    }

    size_t MaxCompressedSize(size_t original_size) override {
        return snappy_max_compressed_length(original_size);
    }

private:
    // Snappy 嵌入式实现（避免外部依赖）
    static void snappy_compress(const char* input, size_t input_length,
                                 char* output, size_t* output_length) {
        *output_length = 0;
        size_t i = 0;
        while (i < input_length) {
            // 生成字面量或匹配
            if (i < input_length - 3 && input[i] == input[i+1] &&
                input[i] == input[i+2] && input[i] == input[i+3]) {
                // 重复字符
                size_t j = i + 4;
                while (j < input_length && input[j] == input[i]) ++j;
                size_t length = j - i;
                if (length < 12) {
                    // 字面量长度
                    output[(*output_length)++] = static_cast<char>(length - 4);
                    memcpy(output + *output_length, input + i, length);
                    *output_length += length;
                } else {
                    // 回溯引用
                    output[(*output_length)++] = 0xFC;
                    size_t emit = length;
                    while (emit > 0) {
                        size_t copylen = (emit > 64) ? 64 : emit;
                        output[(*output_length)++] = static_cast<char>(copylen - 4);
                        output[(*output_length)++] = static_cast<char>((i - 4) & 0xFF);
                        output[(*output_length)++] = static_cast<char>(((i - 4) >> 8) & 0xFF);
                        emit -= copylen;
                    }
                }
                i += length;
            } else {
                // 字面量
                size_t literal = 1;
                while (i + literal < input_length && literal < 60) {
                    if (i + literal < input_length - 3 &&
                        input[i + literal] == input[i + literal + 1] &&
                        input[i + literal] == input[i + literal + 2]) {
                        break;
                    }
                    ++literal;
                }
                output[(*output_length)++] = static_cast<char>(literal);
                memcpy(output + *output_length, input + i, literal);
                *output_length += literal;
                i += literal;
            }
        }
    }

    static void snappy_uncompress(const char* compressed, size_t compressed_length,
                                   char* decompressed) {
        size_t i = 0, j = 0;
        while (i < compressed_length) {
            uint8_t op = static_cast<uint8_t>(compressed[i++]);
            if (op < 64) {
                // 字面量
                memcpy(decompressed + j, compressed + i, op + 1);
                i += op + 1;
                j += op + 1;
            } else if (op < 252) {
                // 回溯引用
                size_t length = op - 60;
                size_t byte1 = static_cast<size_t>(compressed[i++]) & 0xFF;
                size_t byte2 = static_cast<size_t>(compressed[i++]) & 0xFF;
                size_t offset = byte1 | (byte2 << 8);
                for (size_t k = 0; k < length; ++k) {
                    decompressed[j + k] = decompressed[j + k - offset - 4];
                }
                j += length;
            } else if (op == 252) {
                // 4字节回溯
                size_t length = static_cast<size_t>(compressed[i++]);
                size_t byte1 = static_cast<size_t>(compressed[i++]) & 0xFF;
                size_t byte2 = static_cast<size_t>(compressed[i++]) & 0xFF;
                size_t byte3 = static_cast<size_t>(compressed[i++]) & 0xFF;
                size_t offset = byte1 | (byte2 << 8) | (byte3 << 16);
                for (size_t k = 0; k < length; ++k) {
                    decompressed[j + k] = decompressed[j + k - offset - 4];
                }
                j += length;
            } else if (op == 253) {
                // 保留
                i += 3;
            } else if (op == 254 || op == 255) {
                // 保留
                i += 2;
            }
        }
    }

    static void snappy_uncompressed_length(const char* compressed, size_t compressed_length,
                                            size_t* result) {
        // 简化实现：返回保守估计
        *result = compressed_length;
    }

    static size_t snappy_max_compressed_length(size_t input_length) {
        return 32 + input_length + (input_length / 6);
    }
};

/**
 * @brief LZ4 压缩器
 *
 * LZ4 是极高速的压缩算法：
 * - 压缩速度：极快（GB/s）
 * - 解压速度：更快
 * - 压缩比：通常 2-3x
 *
 * 适用于对延迟敏感的场景。
 */
class LZ4Compressor : public Compressor {
public:
    CompressionType type() const override { return CompressionType::kLZ4; }

    CompressedData Compress(const char* data, size_t size) override {
        if (size == 0) {
            return CompressedData({}, 0);
        }

        size_t max_output = MaxCompressedSize(size);
        std::vector<char> output(max_output + 4); // 头部预留4字节存储原始大小

        // 头部存储原始大小
        *reinterpret_cast<uint32_t*>(output.data()) = static_cast<uint32_t>(size);

        size_t compressed_size = lz4_compress(data, size,
                                               output.data() + 4, max_output);
        output.resize(compressed_size + 4);

        return CompressedData(std::move(output), size);
    }

    size_t Decompress(const CompressedData& compressed_data,
                      char* output, size_t output_size) override {
        if (compressed_data.empty()) {
            return 0;
        }

        if (output_size < compressed_data.original_size()) {
            return 0;
        }

        size_t compressed_size = compressed_data.size() - 4;
        lz4_decompress(compressed_data.data() + 4, compressed_size,
                       output, output_size);
        return compressed_data.original_size();
    }

    size_t GetDecompressedSize(const char* compressed_data, size_t compressed_size) override {
        if (compressed_size < 4) return 0;
        return *reinterpret_cast<const uint32_t*>(compressed_data);
    }

    size_t MaxCompressedSize(size_t original_size) override {
        return lz4_compress_bound(original_size);
    }

private:
    static size_t lz4_compress_bound(size_t input_length) {
        return input_length + (input_length / 255) + 16;
    }

    static size_t lz4_compress(const char* src, size_t src_len,
                                char* dst, size_t dst_cap) {
        size_t i = 0, o = 0;
        size_t anchor = 0;

        if (src_len == 0) return 0;

        // 第一个字面量
        size_t literal_length = 0;
        while (i < src_len) {
            // 查找匹配
            size_t search = i - 4;
            if (search < anchor) search = anchor;
            size_t best_match = 0, best_offset = 0;

            for (size_t s = search; s < i; ++s) {
                size_t m = 0;
                while (s + m < src_len && i + m < src_len &&
                       src[s + m] == src[i + m] && m < 255) {
                    ++m;
                }
                if (m > best_match) {
                    best_match = m;
                    best_offset = i - s;
                }
            }

            if (best_match >= 4) {
                // 输出之前的字面量
                if (literal_length > 0) {
                    while (literal_length >= 15) {
                        dst[o++] = 0xFF;
                        memcpy(dst + o, src + anchor, 15);
                        o += 15;
                        literal_length -= 15;
                    }
                    dst[o++] = static_cast<char>(literal_length);
                    memcpy(dst + o, src + anchor, literal_length);
                    o += literal_length;
                    literal_length = 0;
                }

                // 输出匹配
                best_match -= 4;
                dst[o++] = static_cast<char>((best_match << 4) | 0);
                dst[o++] = static_cast<char>(best_offset & 0xFF);
                dst[o++] = static_cast<char>((best_offset >> 8) & 0xFF);

                i += best_match;
                anchor = i;
            } else {
                ++i;
                ++literal_length;
                if (literal_length == 4095) {
                    dst[o++] = 0xFF;
                    dst[o++] = 0xFF;
                    dst[o++] = 0;
                    dst[o++] = 0;
                    literal_length = 0;
                }
            }

            if (o > dst_cap - 18) break;
        }

        // 输出剩余字面量
        if (literal_length > 0) {
            while (literal_length >= 15) {
                dst[o++] = 0xFF;
                memcpy(dst + o, src + anchor, 15);
                o += 15;
                literal_length -= 15;
            }
            dst[o++] = static_cast<char>(literal_length);
            memcpy(dst + o, src + anchor, literal_length);
            o += literal_length;
        }

        return o;
    }

    static void lz4_decompress(const char* src, size_t src_len,
                                char* dst, size_t dst_cap) {
        size_t i = 0, o = 0;

        while (i < src_len) {
            uint8_t token = static_cast<uint8_t>(src[i++]);
            size_t literal_length = token >> 4;

            if (literal_length == 15) {
                size_t length;
                do {
                    length = static_cast<uint8_t>(src[i++]);
                    literal_length += length;
                } while (length == 255);
            }

            // 复制字面量
            if (o + literal_length > dst_cap) literal_length = dst_cap - o;
            memcpy(dst + o, src + i, literal_length);
            o += literal_length;
            i += literal_length;

            if (i >= src_len) break;

            // 读取偏移量
            size_t offset = static_cast<uint8_t>(src[i]) |
                           ((static_cast<size_t>(src[i + 1]) << 8) & 0xFF00);
            i += 2;

            size_t match_length = token & 0x0F;
            if (match_length == 15) {
                uint8_t length;
                do {
                    length = static_cast<uint8_t>(src[i++]);
                    match_length += length;
                } while (length == 255);
            }
            match_length += 4;

            // 复制匹配
            size_t match_pos = o - offset;
            if (o + match_length > dst_cap) match_length = dst_cap - o;
            for (size_t k = 0; k < match_length; ++k) {
                dst[o++] = dst[match_pos + k];
            }
        }
    }
};

/**
 * @brief 压缩工厂类
 *
 * 提供统一的压缩器创建接口，支持运行时选择压缩算法。
 */
class CompressionFactory {
public:
    /// @brief 获取指定类型的压缩器
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

    /// @brief 根据压缩率选择最优算法
    static CompressionType SelectByRatio(size_t original_size, size_t compressed_size) {
        if (original_size == 0) return CompressionType::kNone;

        double ratio = static_cast<double>(original_size) / compressed_size;
        // 压缩比 > 1.5x 使用压缩，否则不使用
        return ratio > 1.5 ? CompressionType::kLZ4 : CompressionType::kNone;
    }

    /// @brief 根据数据特征自动选择算法
    static CompressionType AutoSelect(const char* data, size_t size) {
        // LZ4 通常在速度和压缩率之间有更好的平衡
        return CompressionType::kLZ4;
    }
};

/**
 * @brief 压缩 Block 辅助类
 *
 * 用于管理 DataBlock 的压缩/解压，
 * 提供透明的数据压缩访问接口。
 */
class CompressedBlock {
public:
    CompressedBlock() = default;

    /// @brief 使用原始数据初始化（不压缩）
    void Init(const char* data, size_t size) {
        data_.assign(data, data + size);
        compressed_ = false;
        original_size_ = size;
    }

    /// @brief 使用指定压缩算法压缩数据
    void Compress(CompressionType type) {
        if (data_.empty()) return;

        auto compressor = CompressionFactory::Create(type);
        if (!compressor) return;

        CompressedData compressed = compressor->Compress(data_.data(), data_.size());
        if (!compressed.empty()) {
            data_.assign(compressed.data(), compressed.data() + compressed.size());
            compressed_ = true;
            original_size_ = compressed.original_size();
        }
    }

    /// @brief 解压数据
    void Decompress() {
        if (!compressed_ || data_.empty()) return;

        auto compressor = CompressionFactory::Create(CompressionType::kLZ4);
        if (!compressor) return;

        CompressedData compressed(data_.data(), data_.size(), original_size_);
        std::vector<char> decompressed(original_size_);
        size_t result = compressor->Decompress(compressed, decompressed.data(), original_size_);
        if (result > 0) {
            data_ = std::move(decompressed);
            compressed_ = false;
        }
    }

    /// @brief 获取数据指针
    const char* data() const { return data_.data(); }

    /// @brief 获取数据大小
    size_t size() const { return data_.size(); }

    /// @brief 获取原始大小
    size_t original_size() const { return original_size_; }

    /// @brief 是否已压缩
    bool compressed() const { return compressed_; }

    /// @brief 交换数据
    void Swap(std::vector<char>& other) {
        data_.swap(other);
    }

private:
    std::vector<char> data_;      ///< 存储的数据（可能已压缩）
    size_t original_size_{0};     ///< 原始数据大小
    bool compressed_{false};      ///< 是否已压缩
};

}  // namespace common
}  // namespace shuaikv

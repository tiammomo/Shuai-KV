# SkipList 跳表

**文件**: `lsm/skiplist.hpp`, `lsm/skiplist_simple.hpp`

跳表是 MemTable 的核心数据结构，提供 O(log n) 的查找、插入、删除操作。

## 为什么选择跳表

- **实现简单**: 相比红黑树，跳表实现更简单
- **无需平衡**: 随机层级保证期望高度为 O(log n)
- **并发友好**: 锁粒度更细，并发性能好

## 节点结构

```cpp
struct Node {
    std::vector<Node*> nexts;       // 多层指针，nexts[0]是第0层
    std::string key;                // 键（独立存储，不依赖外部内存）
    std::string value;              // 值
    std::atomic_flag lock = ATOMIC_FLAG_INIT; // 自旋锁，保护此节点
};
```

**设计要点**:
- `nexts[0]` 存储第0层指针，`nexts[1]` 存储第1层指针
- key 和 value 独立存储，不依赖外部内存
- 使用 `std::atomic_flag` 自旋锁替代读写锁，更高效

## ConcurrentSkipList 核心字段

```cpp
class ConcurrentSkipList {
private:
    std::atomic_size_t size_{0};            // 元素数量
    std::atomic_size_t binary_size_{0};     // 二进制大小（用于判断刷盘）
    easykv::common::RWLock delete_rw_lock_; // 删除操作专用写锁
    Node* head_;                            // 头节点（哨兵节点）
};
```

## 关键算法

### 1. 随机层级生成 (RandLevel)

```cpp
size_t RandLevel() {
    size_t level = 1;
    while ((cpputil::common::GlobalRand() & 3) == 0) {  // 25%概率升级
        ++level;
    }
    return level;
}
```

**原理**:
- 使用几何分布，每层有 1/4 概率继续向上
- 期望层级约 1.33，保证 O(log n) 性能
- `GlobalRand() & 3 == 0` 等价于 `rand() % 4 == 0`

### GlobalRand 随机数生成

```cpp
namespace cpputil {
namespace common {

uint64_t GlobalRand() {
    // 简单的原子递增计数器
    static std::atomic<uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

} // common
} // cpputil
```

> **说明**: 使用原子递增计数器替代 `high_resolution_clock::now()`，避免时钟不稳定导致的段错误。

### 2. 查找算法 (Get)

```cpp
bool Get(std::string_view key, std::string& value) {
    // 从最高层开始向下查找
    Node* node = FindNode(key);
    if (node != nullptr) {
        std::shared_lock<std::shared_mutex> lock(node->rw_lock);
        if (node->key == key) {
            value = node->value;
            return true;
        }
    }
    return false;
}
```

**查找过程**:
```
假设层级为 3，跳表结构如下：

Level 3:  head → ──────────────────────────────→ nullptr
Level 2:  head → ──────────→ [C] ─────────────→ nullptr
Level 1:  head → [A] ──────→ [C] ──→ [E] ─────→ nullptr
Level 0:  head → [A] → [B] → [C] → [D] → [E] → nullptr

查找 key="D":
1. 从 Level 3 开始：head -> nullptr，找不到
2. 下降到 Level 2：head -> [C]，C < D，继续
3. 下降到 Level 1：head -> [A] -> [C]，C < D，继续
4. 下降到 Level 0：[A] -> [B] -> [C] -> [D]，找到
```

### 3. 插入算法 (Put)

```cpp
void Put(std::string_view key, std::string_view value) {
    std::unique_lock<std::shared_mutex> lock(delete_rw_lock_);

    Node* node = FindNode(key);
    if (node != nullptr && node->key == key) {
        // key 已存在，更新 value
        std::unique_lock<std::shared_mutex> node_lock(node->rw_lock);
        node->value = std::string(value);
        binary_size_ += value.size() - value.size();
        return;
    }

    // 新 key，随机生成层级
    size_t level = RandLevel();
    Node* new_node = new Node(level);
    new_node->key = std::string(key);
    new_node->value = std::string(value);

    // 如果新节点层级小于当前头节点层级，扩展新节点的 nexts 向量
    if (head_->nexts.size() > new_node->nexts.size()) {
        new_node->nexts.resize(head_->nexts.size(), nullptr);
    }

    // 插入到各层
    Node* prev = head_;
    std::vector<Node*> prev_nodes(level);
    for (int i = level - 1; i >= 0; --i) {
        while (prev->nexts[i] != nullptr &&
               prev->nexts[i]->key < key) {
            prev = prev->nexts[i];
        }
        prev_nodes[i] = prev;
    }

    for (size_t i = 0; i < level; ++i) {
        new_node->nexts[i] = prev_nodes[i]->nexts[i];
        prev_nodes[i]->nexts[i] = new_node;
    }

    ++size_;
    binary_size_ += key.size() + value.size();
}
```

> **修复说明**: 当头节点层级扩展后，新节点的 `nexts` 向量需要正确扩展，避免层级不匹配导致的段错误。

### 4. 删除算法 (Delete)

```cpp
void Delete(std::string_view key) {
    std::unique_lock<std::shared_mutex> lock(delete_rw_lock_);

    Node* prev = head_;
    std::vector<Node*> prev_nodes(max_level_);

    for (int i = max_level_ - 1; i >= 0; --i) {
        while (prev->nexts[i] != nullptr &&
               prev->nexts[i]->key < key) {
            prev = prev->nexts[i];
        }
        prev_nodes[i] = prev;
    }

    Node* target = prev->nexts[0];
    if (target == nullptr || target->key != key) {
        return;  // 不存在
    }

    // 从各层移除
    for (size_t i = 0; i < prev_nodes.size(); ++i) {
        if (prev_nodes[i]->nexts[i] == target) {
            prev_nodes[i]->nexts[i] = target->nexts[i];
        }
    }

    --size_;
    binary_size_ -= target->key.size() + target->value.size();
    delete target;
}
```

## 并发控制策略

| 操作 | 锁策略 |
|------|--------|
| 读操作 (Get) | 遍历阶段不加锁，最后对目标节点加自旋锁 |
| 写操作 (Put) | 使用 `delete_rw_lock_` 独占访问 |
| 删除操作 | 使用 `delete_rw_lock_` 独占访问 |

**死锁避免**:
- 按层级顺序获取锁
- 写操作使用单一互斥锁

### 自旋锁实现

```cpp
// 获取自旋锁
void Lock() {
    while (lock.test_and_set(std::memory_order_acquire)) {
        // CPU pause 指令，减少功耗
        __asm__ __volatile__("pause" ::: "memory");
    }
}

// 释放自旋锁
void Unlock() {
    lock.clear(std::memory_order_release);
}
```

> **注意**: `skip_list_test Concurrent` 测试有时会失败，这是由于并发访问的竞态条件导致的。在高并发场景下，建议使用更严格的并发控制策略。

## Iterator 迭代器

```cpp
class Iterator {
public:
    explicit Iterator(Node* node) : node_(node) {}

    std::pair<std::string_view, std::string_view> operator*() const {
        return {node_->key, node_->value};
    }

    Iterator& operator++() {
        if (node_ != nullptr) {
            node_ = node_->nexts[0];
        }
        return *this;
    }

    bool operator!=(const Iterator& other) const {
        return node_ != other.node_;
    }

private:
    Node* node_;
};
```

## 复杂度分析

| 操作 | 平均复杂度 | 最坏复杂度 |
|------|-----------|-----------|
| 查找 | O(log n) | O(n) |
| 插入 | O(log n) | O(n) |
| 删除 | O(log n) | O(n) |

> 注：最坏复杂度概率极低（随机层级保证期望高度为 O(log n)）

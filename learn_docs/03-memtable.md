# MemTable 内存表

**文件**: `lsm/memtable.hpp`

MemTable 是 SkipList 的封装，提供统一的键值操作接口，是 LSM Tree 的内存组件。

## 设计职责

- 提供键值对的增删改查接口
- 维护内存中的有序数据
- 当数据量达到阈值时，触发刷盘（转换为 SST）

## 核心代码

```cpp
class MemTable {
public:
    using Iterator = ConcurrentSkipList::Iterator;

    /**
     * 查询键值对
     * @param key 键
     * @param value 输出：值
     * @return 是否找到
     */
    bool Get(std::string_view key, std::string& value);

    /**
     * 插入或更新键值对
     * @param key 键
     * @param value 值
     */
    void Put(std::string_view key, std::string_view value);

    /**
     * 删除键值对
     * @param key 键
     */
    void Delete(std::string_view key);

    /**
     * 获取二进制大小（用于判断是否需要刷盘）
     * @return 二进制大小
     */
    size_t binary_size();

    /**
     * 获取元素数量
     * @return 元素数量
     */
    size_t size();

    /**
     * 获取迭代器
     * @return 迭代器
     */
    Iterator begin();

    /**
     * 获取结束迭代器
     * @return 结束迭代器
     */
    Iterator end();

private:
    ConcurrentSkipList skip_list_;  // 内部跳表
};
```

## 实现细节

### 1. Get 查询

```cpp
bool MemTable::Get(std::string_view key, std::string& value) {
    return skip_list_.Get(key, value);
}
```

### 2. Put 插入

```cpp
void MemTable::Put(std::string_view key, std::string_view value) {
    skip_list_.Put(key, value);
}
```

### 3. Delete 删除

```cpp
void MemTable::Delete(std::string_view key) {
    skip_list_.Delete(key);
}
```

### 4. 大小查询

```cpp
size_t MemTable::binary_size() {
    return skip_list_.binary_size();
}

size_t MemTable::size() {
    return skip_list_.size();
}
```

### 5. 迭代器

```cpp
Iterator MemTable::begin() {
    return Iterator(skip_list_.Head()->nexts[0]);
}

Iterator MemTable::end() {
    return Iterator(nullptr);
}
```

## 在 LSM Tree 中的角色

```
┌─────────────────────────────────────────────────────────┐
│                      LSM Tree                           │
├─────────────────────────────────────────────────────────┤
│                                                         │
│   [MemTable 1] ←── 活跃 MemTable（当前写入）            │
│        ↓                                               │
│   [MemTable 2] ←── 待刷盘 MemTable（已满但未刷盘）       │
│        ↓                                               │
│   [MemTable 3]                                          │
│        ↓                                               │
│   ...                                                   │
│        ↓                                               │
│   [SST Files] ←── 磁盘上的持久化数据                     │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

## 刷盘流程

当 MemTable 达到阈值大小时：

1. 创建新的 MemTable 继续接收写入
2. 旧的 MemTable 标记为待刷盘
3. 后台线程将数据写入 SST 文件
4. 从待刷盘列表中移除

```cpp
// DB.hpp 中的刷盘触发逻辑
void Put(std::string_view key, std::string_view value) {
    memtable_->Put(key, value);

    // 检查是否需要触发刷盘
    if (memtable_->binary_size() > config_.memtable_max_size) {
        std::unique_lock<std::mutex> lock(to_sst_mutex_);

        // 将当前 MemTable 加入待刷盘列表
        inmemtables_.push_back(memtable_);

        // 创建新的 MemTable
        memtable_ = std::make_shared<MemTable>();

        // 唤醒刷盘线程
        to_sst_cv_.notify_one();
    }
}
```

## 与 SkipList 的关系

MemTable 是 SkipList 的轻量级包装：

| MemTable | SkipList |
|----------|----------|
| 提供公共 API | 实现核心数据结构 |
| 不管理并发 | 管理并发控制 |
| 不管理内存 | 管理节点内存 |

## 特性总结

| 特性 | 说明 |
|------|------|
| 有序存储 | 基于 SkipList，保持 key 有序 |
| O(log n) 查询 | 跳表的查找复杂度 |
| 并发安全 | **std::atomic_flag 自旋锁** |
| 快速恢复 | 崩溃后通过 SST 恢复数据 |

## 测试验证

MemTable 的功能通过 `skip_list_test` 进行测试：

| 测试项 | 状态 |
|--------|------|
| Function 测试 | PASS |
| Concurrent 测试 | 有时失败（竞态条件） |

> 注：Function 测试已修复 GlobalRand 和层级扩展问题，现可稳定通过。

# Manifest 元数据管理

**文件**: `lsm/manifest.hpp`

Manifest 管理 LSM 树的所有 SST 文件元数据，采用 Copy-on-Write 模式实现无锁读取。

## 设计目标

- 跟踪所有 SST 文件的位置和层级
- 支持多版本管理（Copy-on-Write）
- 触发和管理 Compaction

## 核心数据结构

### Level（单层 SST 管理）

```cpp
class Level {
private:
    size_t level_;                           // 层编号
    std::vector<std::shared_ptr<SST>> ssts_; // 该层的 SST 文件列表

public:
    void Add(const std::shared_ptr<SST>& sst);
    void Remove(size_t index);
    void RemoveAll();
    size_t size() const;
    const std::vector<std::shared_ptr<SST>>& GetSSTs() const;
};
```

### Manifest 核心字段

```cpp
class Manifest {
private:
    std::atomic_size_t count_{0};           // 访问计数
    size_t version_{1};                     // Manifest 版本号
    std::vector<Level> levels_;             // 各层 SST 文件列表
    easykv::common::RWLock memtable_rw_lock_; // 读写锁
    size_t max_sst_id_{0};                  // 最大 SST 文件 ID
};
```

## 分层策略

### 分层阈值配置

```
Level 0:    1 KB  (初始层，直接接收 MemTable 刷盘)
Level 1:   10 MB
Level 2:  100 MB
Level 3:    1 GB
Level 4:   10 GB
```

### 分层特点

- **Level 0**: 特殊层，接收 MemTable 直接刷盘的 SST
- **Level N+1**: 容量是 Level N 的 10 倍
- **层内无序，层间有序**: 同层 SST 区间可能重叠，不同层严格有序

## 核心方法

### 1. 插入 SST

```cpp
std::shared_ptr<Manifest> InsertAndUpdate(
    const std::shared_ptr<SST>& sst
) {
    // Copy-on-Write: 创建新版本
    auto new_manifest = std::make_shared<Manifest>(*this);
    new_manifest->version_ = version_ + 1;

    // 插入到 Level 0
    new_manifest->levels_[0].Add(sst);
    new_manifest->max_sst_id_ = std::max(max_sst_id_, sst->Id());

    return new_manifest;
}
```

### 2. 查找 (Get)

```cpp
bool Get(std::string_view key, std::string& value) {
    // 按层查找（从新到旧）
    for (auto it = levels_.rbegin(); it != levels_.rend(); ++it) {
        for (auto& sst : it->GetSSTs()) {
            if (sst->Get(key, value)) {
                return true;
            }
        }
    }
    return false;
}
```

### 3. 迭代器

```cpp
class Iterator {
public:
    Iterator(Manifest* manifest);

    void Seek(std::string_view key);
    void SeekToFirst();

    bool Valid();
    std::pair<std::string_view, std::string_view> operator*();
    void Next();

private:
    Manifest* manifest_;
    size_t level_index_;
    size_t sst_index_;
    typename SST::Iterator sst_iterator_;
};
```

## Compaction 策略

### Size-Tiered Compaction

当某一层 SST 数量达到阈值时触发：

```cpp
bool CanDoCompaction() {
    // Level 0: 4 个 SST 时触发
    // Level N: 10 个 SST 时触发
    return levels_[0].size() >= 4 ||
           (version_ % 10 == 0 && levels_[1].size() >= 10);
}
```

### Compaction 流程

```cpp
void SizeTieredCompaction(size_t level, int id) {
    // 1. 收集当前层和下一层需要合并的 SST 文件
    std::vector<std::shared_ptr<SST>> sources;
    auto& current_level = levels_[level];

    for (auto& sst : current_level.GetSSTs()) {
        sources.push_back(sst);
    }

    // 2. 使用最小堆合并所有键值对（保持有序）
    struct CompactionItem {
        SST::Iterator it;
        std::shared_ptr<SST> sst;

        bool operator<(const CompactionItem& rhs) const {
            // 最小堆：key 小的优先
            auto lhs_key = *(it);
            auto rhs_key = *(rhs.it);
            if (lhs_key.first != rhs_key.first) {
                return lhs_key.first > rhs_key.first;
            }
            // key 相等时，较新的 SST 优先（id 大的优先）
            return sst->Id() < rhs.sst->Id();
        }
    };

    std::priority_queue<CompactionItem> pq;

    // 3. 多路归并
    for (auto& sst : sources) {
        pq.push({sst->Begin(), sst});
    }

    std::vector<std::pair<std::string, std::string>> merged;

    while (!pq.empty()) {
        auto top = pq.top();
        pq.pop();

        auto [key, value] = *(top.it);

        // 跳过重复 key（保留最新的）
        if (!merged.empty() && merged.back().first == key) {
            merged.back().second = std::string(value);
        } else {
            merged.emplace_back(std::string(key), std::string(value));
        }

        // 推进迭代器
        ++top.it;
        if (top.it.Valid()) {
            pq.push(top);
        }
    }

    // 4. 写入新的 SST 文件到下一层
    auto new_sst = SST::Create(merged, id, compression_config_);
    levels_[level + 1].Add(new_sst);

    // 5. 清理被合并的 SST 文件
    current_level.RemoveAll();
}
```

### Compaction 流程图

```
┌─────────────────────────────────────────────────────────────┐
│                    Compaction 流程                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Level N     Level N+1                                     │
│  ┌─────┐     ┌─────┐                                      │
│  │ SST1│     │     │                                      │
│  ├─────┤     │     │                                      │
│  │ SST2│     │     │                                      │
│  ├─────┤     ├─────┤                                      │
│  │ SST3│     │ SST4│                                      │
│  ├─────┤     ├─────┤                                      │
│  │ SST4│     │ SST5│                                      │
│  └─────┘     └─────┘                                      │
│      ↓           ↓                                         │
│      └───────────┘                                         │
│              ↓                                             │
│      ┌───────────────┐                                     │
│      │ 多路归并排序   │                                     │
│      │ 去重(保留最新) │                                     │
│      └───────┬───────┘                                     │
│              ↓                                             │
│      ┌───────────────┐                                     │
│      │ 写入新的 SST   │                                     │
│      └───────┬───────┘                                     │
│              ↓                                             │
│  Level N     Level N+1                                     │
│  ┌─────┐     ┌─────┐                                      │
│  │     │     │ SST4│                                      │
│  ├─────┤     ├─────┤                                      │
│  │     │     │ SST5│                                      │
│  ├─────┤     ├─────┤                                      │
│  │     │     │NewSST│  ← 新 SST                            │
│  ├─────┤     └─────┘                                      │
│  │     │                                                  │
│  └─────┘                                                  │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## Copy-on-Write 优势

```cpp
// DB 中的 Manifest 管理
std::vector<std::shared_ptr<Manifest>> manifest_queue_;

void DB::Put(std::string_view key, std::string_view value) {
    memtable_->Put(key, value);

    if (memtable_->binary_size() > config_.memtable_max_size) {
        // 刷盘逻辑...
        auto sst = std::make_shared<SST>(*inmemtable, ++sst_id_, config_.compression);

        // 更新 Manifest（创建新版本）
        auto new_manifest = manifest_queue_.back()->InsertAndUpdate(sst);

        // 检查并执行 Compaction
        if (new_manifest->CanDoCompaction()) {
            new_manifest->SizeTieredCompaction(++sst_id_);
        }

        manifest_queue_.emplace_back(new_manifest);
    }
}
```

| 优势 | 说明 |
|------|------|
| 读操作无锁 | 旧版本继续提供读取服务 |
| 写操作原子性 | 新版本完全创建后才切换 |
| 崩溃恢复 | 可以恢复到任意历史版本 |
| 简化并发控制 | 不需要复杂的锁机制 |

## 与其他模块的关系

```
Manifest
    │
    ├── 管理 Level (Level[])
    │       │
    │       └── 每个 Level 管理多个 SST
    │
    ├── 被 DB 使用
    │       │
    │       └── DB.Get() 调用 Manifest.Get()
    │
    └── 触发 Compaction
            │
            └── SizeTieredCompaction()
```

## 复杂度分析

| 操作 | 复杂度 |
|------|--------|
| 插入 | O(1) |
| 查找 | O(L × S × log B)，L=层数，S=SST数/层，B=块数 |
| Compaction | O(N log N)，N=合并的键值对数 |

## 测试验证

Manifest 相关功能通过 `compaction_test` 验证：

| 测试项 | 状态 |
|--------|------|
| Compaction 测试 | PASS |
| Manifest 版本管理 | PASS |

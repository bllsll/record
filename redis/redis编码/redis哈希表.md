### 一、哈希表的核心结构体

Redis 哈希表由三个关键结构体组成，从外到内依次是 `dict`（哈希表容器）、`dictht`（哈希表实例）、`dictEntry`（哈希表节点），三者的关系如下：

#### 1. `dictEntry`：哈希表节点（存储单个键值对）

每个 `dictEntry` 存储一个键值对，结构定义（简化）：

```c
typedef struct dictEntry {
    void *key;                  // 键（如字符串对象）
    union {                     // 值（支持多种类型）
        void *val;              // 指针类型（如哈希表、列表等对象）
        uint64_t u64;           // 无符号整数
        int64_t s64;            // 有符号整数
        double d;               // 浮点数
    } v;
    struct dictEntry *next;     // 下一个节点的指针（用于解决哈希冲突，形成链表）
} dictEntry;
```

- `key`：存储键（Redis 中通常是字符串对象 `sds`）。
- `v`：存储值（支持多种类型，如字符串、整数、或其他 Redis 对象）。
- `next`：指向同哈希桶中的下一个节点，通过**拉链法**解决哈希冲突。

#### 2. `dictht`：哈希表实例（存储节点数组）

`dictht` 是实际存储哈希表节点的结构，包含一个节点数组（哈希桶），结构定义（简化）：

```c
typedef struct dictht {
    dictEntry **table;          // 哈希桶数组（元素是指向 dictEntry 的指针）
    unsigned long size;         // 哈希桶数组的长度（必须是 2^n，便于计算索引）
    unsigned long sizemask;     // 掩码，用于计算索引（sizemask = size - 1）
    unsigned long used;         // 已存储的节点数量（键值对总数）
} dictht;
```

- `table`：哈希桶数组，数组长度为 `size`（初始通常为 4，随数据量动态扩容）。
- `sizemask`：用于快速计算键在数组中的索引（`index = hash & sizemask`），因 `size` 是 2^n，`sizemask` 二进制全为 1（如 `size=8` 时 `sizemask=7`，二进制 `111`），可高效取模。
- `used`：记录当前哈希表中的键值对总数，用于计算负载因子（`load_factor = used / size`）。

#### 3. `dict`：哈希表容器（管理哈希表及 rehash 状态）

`dict` 是哈希表的顶层结构，包含两个 `dictht` 实例（用于 rehash）和 rehash 相关状态，结构定义（简化）：

```c
typedef struct dict {
    dictType *type;             // 类型特定函数（如哈希计算、键值复制等）
    void *privdata;             // 类型函数的私有数据
    dictht ht[2];               // 两个哈希表，ht[0] 是主表，ht[1] 用于 rehash
    long rehashidx;             // rehash 进度索引（-1 表示未进行 rehash）
    unsigned long iterators;    // 当前运行的迭代器数量（防止 rehash 干扰迭代）
} dict;
```

- `type` 和 `privdata`：用于适配不同类型的键值对（如字符串、整数等），提供通用接口（如哈希函数、键比较函数）。
- `ht[2]`：两个哈希表，正常情况下仅 `ht[0]` 在用；rehash 时，`ht[1]` 作为临时表，存储迁移后的数据。
- `rehashidx`：标记 rehash 进度（如 `rehashidx=3` 表示正在迁移 `ht[0].table[3]` 桶中的节点），`-1` 表示未进行 rehash。

### 二、哈希表的核心操作原理

#### 1. 哈希计算与索引定位

当插入一个键值对（`key, value`）时，Redis 会通过以下步骤确定其在哈希桶数组中的位置：

- **步骤 1：计算键的哈希值**
  调用 `dictType` 中的哈希函数（如字符串键使用 `siphash` 算法），对 `key` 计算出一个 64 位哈希值（`hash`）。
  例：`key="name"` → 计算得到 `hash=0x1a2b3c4d5e6f7g8h`。
- **步骤 2：计算数组索引**
  用哈希值 `hash` 与 `dictht` 的 `sizemask` 进行按位与运算，得到索引 `index`：
  `index = hash & sizemask`（因 `sizemask = size - 1`，且 `size` 是 2^n，等价于 `hash % size`，但运算更快）。
  例：若 `size=8`（`sizemask=7`），`hash=0x1a2b3c4d` → `index=0x1a2b3c4d & 7 = 5`，则节点存入 `table[5]`。

#### 2. 哈希冲突解决：拉链法

当两个不同的键计算出相同的 `index` 时，会产生哈希冲突。Redis 通过**拉链法**解决冲突：

- 每个哈希桶（`table[i]`）是一个链表的头指针，新节点会被插入到链表的头部（头插法，O (1) 时间）。
- 例：`key1` 和 `key2` 都计算出 `index=5`，则 `table[5]` 会指向 `key2` 的 `dictEntry`，而 `key2` 的 `next` 指针指向 `key1` 的 `dictEntry`，形成 `table[5] → key2 → key1` 的链表。

这种方式保证了即使有冲突，插入和查询的平均复杂度仍接近 O (1)（链表长度越短，效率越高）。

#### 3. rehash：动态扩容与收缩

哈希表的性能与负载因子（`load_factor = used / size`）密切相关：

- 负载因子过高（哈希桶数组太满）：冲突概率增加，链表变长，查询效率下降。
- 负载因子过低（哈希桶数组太稀疏）：内存浪费严重。

Redis 通过 **rehash** 机制动态调整哈希表大小（`size`），使负载因子维持在合理范围。

##### （1）rehash 的触发条件

- **扩容**：当 `load_factor > 1` 时（默认阈值），触发扩容，`ht[1].size` 设为大于等于 `ht[0].used * 2` 的最小 2^n（保证 `size` 是 2 的幂）。
  例：`ht[0].used=5`，`ht[0].size=4`（负载因子 1.25）→ `ht[1].size=8`（5*2=10，最小 2^n 是 16？不，实际是 `used\*2` 向上取 2^n，5*2=10，最小 2^n 是 16？不，正确是 `ht[1].size` 为第一个大于等于 `ht[0].used` 的 2^n 的两倍？更准确：扩容时 `ht[1].size` 是大于等于 `ht[0].used * 2` 的最小 2^n，确保足够空间。
- **收缩**：当 `load_factor < 0.1` 时，触发收缩，`ht[1].size` 设为大于等于 `ht[0].used` 的最小 2^n。

##### （2）rehash 的完整流程

rehash 是将 `ht[0]` 中的所有键值对迁移到 `ht[1]` 的过程，步骤如下：

1. **初始化 `ht[1]`**：根据扩容 / 收缩规则，为 `ht[1]` 分配合适的 `size`，并初始化其 `table` 数组。
2. **迁移键值对**：将 `ht[0].table` 中所有哈希桶的节点逐个迁移到 `ht[1]`（重新计算哈希值和索引）。
3. **替换哈希表**：迁移完成后，释放 `ht[0]` 的内存，将 `ht[1]` 赋值给 `ht[0]`，重置 `ht[1]` 为空表，`rehashidx` 设为 `-1`，完成 rehash。

##### （3）渐进式 rehash：避免阻塞

如果 `ht[0]` 中包含数百万个节点，一次性迁移会导致 Redis 阻塞（无法处理其他命令）。因此 Redis 采用**渐进式 rehash**，将迁移分散到多次操作中：

- **标记 rehash 开始**：设置 `rehashidx=0`（表示开始迁移 `ht[0].table[0]` 桶）。
- **分批次迁移**：每次执行添加、删除、查找、更新等操作时，除了处理当前命令，还会顺带迁移 `ht[0].table[rehashidx]` 桶中的所有节点到 `ht[1]`，迁移完成后 `rehashidx++`。
- **强制迁移**：当 `rehashidx` 达到 `ht[0].size` 时，所有桶迁移完成，rehash 结束。

在渐进式 rehash 期间：

- **查询 / 删除**：会同时检查 `ht[0]` 和 `ht[1]`（先查 `ht[0]`，再查 `ht[1]`）。
- **插入**：只插入到 `ht[1]`（保证 `ht[0]` 节点只减不增，加速迁移）。
- **迭代器**：若存在迭代器，会暂停 rehash（通过 `iterators` 计数控制），避免迭代过程中数据结构变化。

### 三、示例：哈希表的动态变化过程

假设初始状态：`ht[0].size=4`（`sizemask=3`），`used=0`，负载因子 0。

1. **插入 5 个键值对**：
   - 随着插入，`used=5`，`load_factor=5/4=1.25 > 1`，触发扩容。
   - `ht[1].size` 设为 8（5*2=10，最小 2^n 是 16？不，实际是 8 可能不够，正确应为 16？这里简化为 8 便于理解）。
2. **渐进式 rehash 开始**：
   - `rehashidx=0`，每次操作迁移 `ht[0].table[0]` 桶的节点到 `ht[1]`，迁移后 `rehashidx=1`。
   - 后续操作继续迁移 `table[1]`、`table[2]`、`table[3]` 桶。
3. **rehash 完成**：
   - 所有 5 个节点迁移到 `ht[1]`，`ht[0]` 被释放，`ht[1]` 成为新的 `ht[0]`，`size=8`，`used=5`，负载因子 0.625，处于合理范围。

### 四、总结

Redis 哈希表的核心设计特点：

- **结构层次**：`dict` 包含两个 `dictht`，`dictht` 包含哈希桶数组，数组元素是 `dictEntry` 链表（拉链法解决冲突）。
- **高效哈希**：通过 `siphash` 计算哈希值，`size` 为 2^n 配合 `sizemask` 快速定位索引。
- **动态调整**：基于负载因子触发 rehash，通过渐进式迁移避免阻塞，平衡性能与内存。
- **适用场景**：支撑哈希键、数据库等核心功能，保证 O (1) 平均复杂度的增删改查。

这种设计使 Redis 哈希表在高性能、高并发场景下仍能保持稳定的效率，是 Redis 核心竞争力的重要组成部分。
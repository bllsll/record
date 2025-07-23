Redis 的 `quicklist`（快速列表）是列表键（`list` 类型）的底层实现之一，从 Redis 3.2 版本开始取代了之前的 `ziplist`（压缩列表）和 `linkedlist`（双向链表）的混合实现，成为列表的默认底层结构。它结合了 `ziplist` 紧凑存储的内存优势和 `linkedlist` 灵活操作的性能优势，是一种 “双向链表 + 压缩列表” 的复合结构。



### 一、设计背景：为什么需要 quicklist？

在 quicklist 出现之前，Redis 列表的底层实现是 “条件切换” 的：

- 当列表元素少且小（满足 `list-max-ziplist-entries` 和 `list-max-ziplist-value` 配置）时，用 `ziplist`（紧凑连续内存，节省空间）。
- 当列表元素增多或变大时，切换为 `linkedlist`（双向链表，插入删除高效）。

但这种设计有明显缺陷：

- `ziplist` 虽然省内存，但当元素过多时，插入 / 删除中间元素需要移动大量内存，效率极低（O (N) 时间复杂度）。
- `linkedlist` 虽然操作灵活，但每个节点是独立的内存块，存在大量指针开销（每个节点至少 16 字节指针），内存利用率低。

**quicklist 的解决方案**：将列表拆分为多个小的 `ziplist`（称为 “节点”），每个节点通过双向指针连接形成链表。这样既保留了 `ziplist` 的内存紧凑性，又通过链表结构避免了单个大 `ziplist` 的操作低效问题。

### 二、quicklist 的核心结构

quicklist 的结构可分为三层：`quicklist`（总控结构）→ `quicklistNode`（节点）→ `ziplist`（节点内的元素存储）。

#### 1. `quicklist`：总控结构

`quicklist` 是整个列表的顶层结构，管理所有节点，定义（简化）如下：

```c
typedef struct quicklist {
    quicklistNode *head;       // 指向第一个节点
    quicklistNode *tail;       // 指向最后一个节点
    unsigned long count;       // 所有节点中元素的总数量（整个列表的长度）
    unsigned int len;          // 节点的数量（链表的长度）
    int fill : QL_FILL_BITS;   // 单个节点的最大填充策略（对应配置 list-max-ziplist-size）
    unsigned int compress : QL_COMPRESS_BITS;  // 压缩深度（对应配置 list-compress-depth）
    // 其他辅助字段...
} quicklist;
```

- `head`/`tail`：双向链表的头指针和尾指针，用于定位首尾节点。
- `count`：记录整个列表的元素总数（所有节点的 `ziplist` 中元素之和），支持 `LLEN` 命令快速返回结果。
- `len`：记录节点的数量（链表长度），用于管理节点。
- `fill`：控制单个节点的 `ziplist` 最大大小（来自配置 `list-max-ziplist-size`），防止节点过大。
- `compress`：控制节点的压缩深度（来自配置 `list-compress-depth`），用于减少中间节点的内存占用。

#### 2. `quicklistNode`：链表节点

每个 `quicklistNode` 是 quicklist 链表中的一个节点，内部包含一个 `ziplist`，定义（简化）如下：

```c
typedef struct quicklistNode {
    struct quicklistNode *prev;  // 前一个节点的指针
    struct quicklistNode *next;  // 后一个节点的指针
    unsigned char *zl;           // 指向节点内的 ziplist（若节点被压缩，则指向压缩数据）
    unsigned int sz;             // ziplist 的总字节数（压缩前的大小，用于快速计算）
    unsigned int count : 16;     // ziplist 中的元素数量（最大 65535）
    unsigned int encoding : 2;   // 编码方式：0=RAW（未压缩），1=LZ4（压缩）
    unsigned int container : 2;  // 容器类型：目前固定为 2（表示 ziplist）
    unsigned int recompress : 1; // 标记：若为 1，说明该节点临时解压后需重新压缩
    unsigned int attempted_compress : 1; // 标记：是否尝试过压缩（用于调试）
    // 其他辅助字段...
} quicklistNode;
```

- `prev`/`next`：双向指针，连接前后节点，形成链表结构。
- `zl`：核心字段，指向节点内部的 `ziplist`（若节点被压缩，则存储压缩后的数据）。
- `sz`：记录 `ziplist` 未压缩时的总字节数（无论是否压缩，均存储原始大小），用于快速判断节点是否需要分裂。
- `count`：记录 `ziplist` 中的元素数量（最大 65535，因 16 位无符号整数），用于快速计算 `count` 总览。
- `encoding`：标记节点是否被压缩（0 = 未压缩，1=LZ4 压缩），Redis 采用 LZF 或 LZ4 算法压缩，节省内存。
- `recompress`：临时标记，当访问压缩节点时，需先解压，使用后若 `recompress=1` 则重新压缩。

#### 3. `ziplist`：节点内的元素存储

每个 `quicklistNode` 内部的 `zl` 指向一个 `ziplist`（压缩列表），用于紧凑存储多个元素。`ziplist` 是一种连续内存的紧凑结构，元素之间无冗余指针，通过前向长度字段实现遍历（具体结构可参考之前的 ziplist 解析）。

**示例**：一个 quicklist 包含 3 个节点，每个节点的 ziplist 存储 2 个元素，则整体结构为：

```plaintext
quicklist {
  head → node1,  tail → node3,  count=6,  len=3,  fill=...,  compress=...
}

node1 {
  prev=NULL,  next=node2,  zl=ziplist1（元素 a, b）,  sz=20,  count=2,  encoding=0...
}

node2 {
  prev=node1,  next=node3,  zl=ziplist2（元素 c, d）,  sz=20,  count=2,  encoding=0...
}

node3 {
  prev=node2,  next=NULL,  zl=ziplist3（元素 e, f）,  sz=20,  count=2,  encoding=0...
}
```

### 三、核心配置：控制 quicklist 的行为

quicklist 的性能和内存占用由两个关键配置参数控制，可在 `redis.conf` 中设置：

#### 1. `list-max-ziplist-size`：单个节点的最大大小

该参数控制每个 `quicklistNode` 内 `ziplist` 的最大容量，防止节点过大导致操作低效。取值规则：

- **正数**：表示 `ziplist` 最多可存储的元素数量（最多 65535）。
  例：`list-max-ziplist-size 5` → 每个节点的 ziplist 最多存 5 个元素，超过则分裂为新节点。
- 负数：表示ziplist的最大字节数（不包含节点本身的指针等开销），取值为-1到-5
  - `-1`：≤ 4KB
  - `-2`：≤ 8KB（默认值）
  - `-3`：≤ 16KB
  - `-4`：≤ 32KB
  - `-5`：≤ 64KB

**作用**：通过限制单个 ziplist 的大小，保证插入 / 删除操作的效率（ziplist 越小，修改时移动的内存越少）。

#### 2. `list-compress-depth`：节点的压缩深度

该参数控制 quicklist 两端不压缩的节点数量，中间节点可被压缩（减少内存占用）。取值规则：

- `0`：不压缩任何节点（默认值）。
- `1`：压缩除头节点和尾节点外的所有节点（头、尾各 1 个节点不压缩）。
- `2`：压缩除头 2 个和尾 2 个节点外的所有节点，以此类推。

**压缩原理**：中间节点被访问的频率低，通过 LZF/LZ4 算法压缩后，内存占用可减少 50%~70%；当需要访问压缩节点时，临时解压，使用后根据 `recompress` 标记决定是否重新压缩。

**示例**：`list-compress-depth 1` 时，一个包含 5 个节点的 quicklist 中，节点 2、3、4 被压缩，节点 1（头）和 5（尾）不压缩，便于快速访问首尾元素（列表的常见操作是 `LPUSH`/`RPUSH`/`LPOP`/`RPOP`）。

### 四、核心操作原理

quicklist 的核心操作（插入、删除、遍历）需要结合双向链表和 ziplist 的特性，平衡效率与内存。

#### 1. 插入元素（以 `LPUSH`/`RPUSH`/`LINSERT` 为例）

插入元素时，需先确定插入位置所在的节点，再执行插入，必要时分裂节点：

- **步骤 1：定位目标节点**
  - 若从头部插入（`LPUSH`）：直接定位 `head` 节点。
  - 若从尾部插入（`RPUSH`）：直接定位 `tail` 节点。
  - 若从中间插入（`LINSERT`）：通过 `head` 或 `tail` 遍历链表，找到目标位置所在的节点（根据距离首尾的远近选择遍历方向）。
- **步骤 2：插入到节点的 ziplist 中**
  将元素插入到目标节点的 `ziplist` 中（头部、尾部或中间），ziplist 会动态调整内存（移动元素）。
- **步骤 3：检查节点是否需要分裂**
  插入后，若节点的 `ziplist` 大小（元素数或字节数）超过 `list-max-ziplist-size` 配置，则将该 `ziplist` 分裂为两个：
  - 前半部分保留在原节点，后半部分放入新节点。
  - 新节点通过 `prev`/`next` 指针插入到链表中，`quicklist.len` 加 1。

**示例**：若配置 `list-max-ziplist-size 2`（每个节点最多 2 个元素），向已有 2 个元素的节点插入第 3 个元素时，节点会分裂为两个（各 2 个和 1 个元素）。

#### 2. 删除元素（以 `LPOP`/`RPOP`/`LREM` 为例）

删除元素时，需定位节点和元素位置，删除后可能合并节点或删除空节点：

- **步骤 1：定位目标节点和元素**
  类似插入，先找到元素所在的节点，再在 `ziplist` 中定位元素位置。
- **步骤 2：从 ziplist 中删除元素**
  移除 `ziplist` 中的目标元素，调整 ziplist 内存（移动后续元素）。
- **步骤 3：处理空节点或合并节点**
  - 若删除后节点的 `ziplist` 为空，则从 quicklist 中移除该节点（释放内存），`quicklist.len` 减 1。
  - 若相邻节点的 `ziplist` 都较小（合并后仍不超过 `list-max-ziplist-size`），可能触发合并（减少节点数量，节省链表指针开销）。

#### 3. 遍历元素（以 `LRANGE` 为例）

遍历需要结合链表的双向指针和 ziplist 的内部遍历：

- **步骤 1：定位起始节点**
  根据遍历的起始索引，从 `head` 或 `tail` 遍历链表，找到包含起始元素的节点。
- **步骤 2：遍历节点内的 ziplist**
  在节点的 `ziplist` 中，从起始元素开始遍历，收集元素直到达到目标数量或节点结束。
- **步骤 3：跨节点遍历**
  若当前节点的元素不足，通过 `next` 指针（正向遍历）或 `prev` 指针（反向遍历）移动到下一个节点，继续遍历其 `ziplist`，直到完成遍历。
- **压缩节点处理**：若遍历到压缩节点，先解压（临时存储到 `zl` 字段），遍历完成后根据 `recompress` 标记重新压缩。

### 五、优势总结

quicklist 作为 Redis 列表的底层实现，核心优势在于：

1. **内存高效**：通过 `ziplist` 紧凑存储元素，减少内存碎片；中间节点可压缩，进一步降低内存占用。
2. **操作灵活**：通过双向链表结构，避免了单个大 `ziplist` 的修改低效问题，插入 / 删除中间元素的效率显著提升。
3. **自适应配置**：通过 `list-max-ziplist-size` 和 `list-compress-depth` 可根据业务场景调整，平衡性能与内存（如高频访问首尾元素时，压缩中间节点节省内存）。

因此，quicklist 完美适配了 Redis 列表 “既需要高效存取，又需要节省内存” 的核心需求，成为列表键的最优底层实现。
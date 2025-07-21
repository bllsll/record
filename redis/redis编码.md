### redis中的编码

#### **数据库结构（redisDb）**

```c
typedef struct redisDb {
    dict *dict;                 // 主哈希表，存储所有键值对
    dict *expires;              // 过期字典，存储键的过期时间
    dict *blocking_keys;        // 阻塞键（如 BLPOP）
    dict *ready_keys;           // 就绪键（阻塞解除后）
    dict *watched_keys;         // 被 WATCH 监控的键
    int id;                     // 数据库 ID
    // 其他字段...
} redisDb;
```

##### 基本内存单位https://cloud.tencent.com/developer/article/1651455

redis采用jemalloc作为默认内存分配器，分配颗粒度为8字节(64操作系统)，分配内存会向上取整到8的倍数。

![img](https://ask.qcloudimg.com/http-save/yehe-6595841/t2l8ss0qli.png)

##### redisObject结构

在Redis中有一个**「核心的对象」**叫做`redisObject` ，是用来表示所有的key和value的，用redisObject结构体来表示`String、Hash、List、Set、ZSet`五种数据类型。

```c
typedef struct redisObject {
    unsigned type:4;         // 4位：对象类型（如 STRING、LIST）
    unsigned encoding:4;     // 4位：编码方式（如 INT、EMBSTR）
    unsigned lru:24;         // 24位：LRU 时间戳
    int refcount;            // 4字节：引用计数
    void *ptr;               // 8字节：数据指针（EMBSTR 不使用）
} robj;
```

##### SDS结构

简单动态字符串 SDS 的结构（以 Redis 5.0+ 的 `sdshdr8` 为例）：

```c
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len;             // 1字节：字符串长度
    uint8_t alloc;           // 1字节：已分配空间
    unsigned char flags;     // 1字节：标志位
    char buf[];              // 实际存储的字符串，需额外 1 字节存储字符串结束符 \0
};
```

##### string

![img](https://ask.qcloudimg.com/http-save/yehe-6595841/8l2vf1w75t.png)



- Int编码

  - 存储结构：直接用8字节存储整数位
  - **适用场景**：int64 范围：-9223372036854775808 到 9223372036854775807）
  - **优势**：无需额外内存开销，操作效率极高（直接进行数值运算）

- embster编码

  - 存储结构：将将 `redisObject` 和 SDS（Simple Dynamic String）连续存储在一块内存中

  - **适用场景**：存储长度 ≤ 44 字节的短字符串（Redis 5.0 及以后版本，之前为 39 字节）

  - **优势**：减少内存碎片，单次内存分配，读写效率高

  - ```plaintext
    +-----------+-------------------+
    | redisObject | SDS (buf + len)   |
    +-----------+-------------------+
      16字节       ≤ 44字节
    ```

- raw编码

  - **存储结构**：`redisObject` 和 SDS 分开存储，通过指针关联

  - **适用场景**：存储长度 > 44 字节的长字符串或需要动态修改的字符串

  - **优势**：支持动态扩容，适合频繁修改的场景

  - ```plaintext
    +-----------+       +-------------------+
    | redisObject |------>| SDS (buf + len)   |
    +-----------+       +-------------------+
      16字节                  动态分配
    ```

  

注意，如果value本来是int编码，如果超过了int64，会自动转成embster编码, 如果后续有append操作，会自动改为raw编码。value值小于int64之后，又自动转成int编码。

```plaintext
存储整数 → INT 编码
↓
存储短字符串 → EMBSTR 编码
↓
字符串长度超过阈值 → 转换为 RAW 编码
↓
执行 APPEND 等修改操作 → 转换为 RAW 编码（EMBSTR 不可修改）
```

redis中存储浮点数是按照string存储的，为什么zset 按照浮点数排序的时候遵循 IEEE 754 双精度 64 位浮点数标准，并使用 二进制比较 进行排序。为什么不直接按照字符串排序呢？ redis是怎么知道是浮点数的



| c语言字符串                          | SDS                            |
| :----------------------------------- | :----------------------------- |
| 获取长度的时间复杂度为O(n)           | 获取长度的时间复杂度为O(1)     |
| 不是二进制安全的                     | 是二进制安全的                 |
| 只能保存字符串                       | 还可以保存二进制数据           |
| n次增长字符串必然会带来n次的内存分配 | n次增长字符串内存分配的次数<=n |

## hash

hash对象的实现方式一共有两种，分别是ziplist，hashtable，其中hashtable的存储方式key是String类型的，value也是以`key value`的形式进行存储。

![img](https://ask.qcloudimg.com/http-save/yehe-6595841/3ylgqwq15i.png)

在hash表结构定义中有四个属性分别是`dictEntry **table、unsigned long size、unsigned long sizemask、unsigned long used`，分别表示的含义就是**「哈希表数组、hash表大小、用于计算索引值，总是等于size-1、hash表中已有的节点数」**。

ht[0]是用来最开始存储数据的，当要进行扩展或者收缩时，ht[0]的大小就决定了ht[1]的大小，ht[0]中的所有的键值对就会重新散列到ht[1]中。

扩展操作：ht[1]扩展的大小是比当前 ht[0].used 值的二倍大的第一个 2 的整数幂；收缩操作：ht[0].used 的第一个大于等于的 2 的整数幂。

当ht[0]上的所有的键值对都rehash到ht[1]中，会重新计算所有的数组下标值，当[数据迁移](https://cloud.tencent.com/product/datainlong?from_column=20065&from=20065)完后ht[0]就会被释放，然后将ht[1]改为ht[0]，并新创建ht[1]，为下一次的扩展和收缩做准备。

##### **渐进式rehash**：如果rehash中数据量很大，那么会分成多步进行，直到rehash完成，具体的实现与对象中的`rehashindex`属性相关，**「若是rehashindex 表示为-1表示没有rehash操作」**。

Redis 在 rehash 期间能够保证数据一致性的核心在于其**双哈希表设计**、**原子性桶迁移**和**操作路由机制**

1. ### **二、数据一致性的核心保障机制**

   #### **1. 原子性桶迁移**

   - **迁移单位**：每次迁移一个完整的桶（bucket），包含该桶下的所有键值对。
   - **原子操作**：迁移过程中，Redis 会将整个桶的所有键值对从 `ht[0]` **一次性复制到 `ht[1]`**，然后从 `ht[0]` 删除。这个过程是原子的，不会被中断。
   - **示例**：
     若正在迁移桶 `idx=10`，Redis 会将 `ht[0][10]` 链表中的所有节点复制到 `ht[1][new_idx]`，然后清空 `ht[0][10]`。迁移期间，其他线程无法修改该桶的数据。

   #### **2. 操作路由机制**

   - **查找 / 删除**：所有操作（如 `GET`、`DEL`）都会先检查 `ht[0]`，若未找到且正在 rehash，则继续检查 `ht[1]`。

   - 插入 / 更新

     ：

     - **迁移前**：若键在 `ht[0]` 中存在，直接修改 `ht[0]`。
     - **迁移后**：若键已被迁移到 `ht[1]`，则修改 `ht[1]`。

   #### **3. 渐进式迁移的保证**

   - **强制迁移**：每次对字典进行操作（如 `GET`、`SET`）时，若正在 rehash，会强制迁移 **1 个桶**（通过 `_dictRehashStep` 函数），确保 rehash 持续推进。
   - **后台定时任务**：Redis 主循环中会定期检查并推进 rehash，避免长时间未完成。

| **数据结构**     | **内存效率** | **插入 / 删除** | **随机访问** | **适用场景**                   |
| ---------------- | ------------ | --------------- | ------------ | ------------------------------ |
| **压缩列表**     | 极高         | O(N)            | O(N)         | 小数据量列表 / 哈希 / 有序集合 |
| **双向链表**     | 低           | O(1)            | O(N)         | 大数据量列表                   |
| **哈希表**       | 中           | O(1)            | O(1)         | 大数据量哈希                   |
| **跳表（ZSet）** | 中           | O(logN)         | O(logN)      | 大数据量有序集合               |

### ziplist 压缩列表

```
<zlbytes> <zltail> <zllen> <entry> <entry> ... <entry> <zlend>
```

- **`zlbytes`**（4 字节）：整个压缩列表占用的总字节数（包括自身）。

- **`zltail`**（4 字节）：尾节点相对于压缩列表起始位置的偏移量（用于快速定位尾部）。

- **`zllen`**（2 字节）：压缩列表包含的节点数量。当节点数超过 `UINT16_MAX`（65535）时，需遍历整个列表才能获取真实数量。

- **``zlend``**（1 字节）：压缩列表的结束标记，固定值为 `0xFF`。

  ```c
  typedef struct zlentry {
      unsigned int prevrawlensize; /* 编码前一个节点长度所需的字节数 */
      unsigned int prevrawlen;     /* 前一个节点的实际长度 */
      unsigned int lensize;        /* 编码当前节点类型和长度所需的字节数 */
      unsigned int len;            /* 实际数据的长度 */
      unsigned int headersize;     /* 头部总大小 (prevrawlensize + lensize) */
      unsigned char encoding;      /* 数据编码类型 */
      unsigned char *p;            /* 指向节点起始位置的指针 */
  } zlentry;
  ```

#### 创建ziplist

```
/* Create a new empty ziplist. */
#define ZIPLIST_HEADER_SIZE     (sizeof(uint32_t)*2+sizeof(uint16_t))
/* Size of the "end of ziplist" entry. Just one byte. */
#define ZIPLIST_END_SIZE        (sizeof(uint8_t))

unsigned char *ziplistNew(void) {
    unsigned int bytes = ZIPLIST_HEADER_SIZE+ZIPLIST_END_SIZE;//10字节head + 1字节尾部
    unsigned char *zl = zmalloc(bytes);
    ZIPLIST_BYTES(zl) = intrev32ifbe(bytes);//设置zlbytes=11
    ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE);//设置zltail = 10
    ZIPLIST_LENGTH(zl) = 0;// zllen = 0
    zl[bytes-1] = ZIP_END; //zlend = 0xff
    return zl;//返回指针：返回指向压缩列表起始地址的指针（zl）
}
```

#### 压缩列表的插入流程

插入操作分为**头部插入**、**尾部插入**和**指定位置插入**，核心接口为 `ziplistPush()`（头 / 尾插入）和 `ziplistInsert()`（指定位置插入）。插入的核心原理是：压缩列表是连续内存块，插入需重新分配内存，并调整相关元素的 `prevrawlen`（前向长度）。

#### 1. 核心接口及作用

| 接口                             | 作用                               | 参数说明                                                     |
| -------------------------------- | ---------------------------------- | ------------------------------------------------------------ |
| `ziplistPush(zl, s, len, where)` | 向头部或尾部插入元素               | `zl`：压缩列表指针；`s`：元素值；`len`：值长度；`where`：`ZIPLIST_HEAD` 或 `ZIPLIST_TAIL` |
| `ziplistInsert(zl, p, s, len)`   | 向指定位置 `p` 前插入元素          | `p`：指向现有元素的指针（插入到 `p` 之前）；`s`、`len` 同前  |
| `ziplistSafeToAdd(zl, add)`      | 检查插入是否安全（不超过最大限制） | `add`：待插入元素的总字节数；返回 1 表示安全，0 表示不安全   |

#### 2. 插入流程（以尾部插入为例，步骤解析）

尾部插入的核心是 `ziplistPush(zl, s, len, ZIPLIST_TAIL)`，内部调用 `ziplistInsert` 实现，步骤如下：

##### 步骤 1：检查插入安全性

调用 `ziplistSafeToAdd(zl, new_len)` 检查插入后总大小是否超过 `ZIPLIST_MAX_SAFETY_SIZE`（1GB），若超过则拒绝插入。

##### 步骤 2：计算新元素的编码和大小

- 根据元素类型（字符串 / 整数）确定编码ZIP_STR_*或者ZIP_INT_*
- 例如：
  - 字符串：根据长度 `len` 确定 `lensize`（1、2 或 5 字节），总大小为 `prevrawlensize`（前向长度的字节数）+ `lensize` + `len`。
  - 整数：编码为 1 字节（如 4 位立即数、8/16/32/64 位整数），`lensize` 固定为 1 字节。

##### 步骤 3：分配新内存

- 计算原压缩列表长度 `old_len`（`zlbytes`）和新元素总大小 `new_entry_size`，新总长度为 `old_len + new_entry_size`。
- 重新分配内存（`zrealloc`），将原有数据复制到新内存块。

##### 步骤 4：调整前向长度（`prevrawlen`）

- 新元素的 `prevrawlen` 需设为前一个元素的总长度（尾部插入时，前一个元素是原尾节点，其总长度可通过 `zltail` 计算）。
- 若插入的是第一个元素，`prevrawlen` 设为 0（无前置元素）。

##### 步骤 5：写入新元素数据

- 在新内存块的尾部（原 `zlend` 位置）写入新元素的 `prevrawlen`、编码 / 长度头部、实际值。

##### 步骤 6：更新压缩列表元数据

- 更新 `zlbytes`：设为新总字节数（`old_len + new_entry_size`）。
- 更新 `zltail`：设为新元素的起始偏移量（原 `zltail` + 原尾节点总长度）。
- 更新 `zllen`：元素个数 +1。
- 移动 `zlend` 到新元素之后（新总字节数 -1 位置）。

#### 3. 头部插入的特殊处理

头部插入与尾部插入类似，但需额外调整**原第一个元素的 `prevrawlen`**：

- 原第一个元素的 `prevrawlen` 需更新为新元素的总长度（因为新元素成为第一个元素，原第一个元素的前向长度是新元素的长度）。
- 若原列表为空，直接插入即可（`prevrawlen` 为 0）。

### 四、插入原理：压缩列表的内存紧凑性保障

压缩列表通过以下机制保证内存紧凑：

1. **变长编码**：元素的 `prevrawlen`（前向长度）和 `len`（自身长度）使用变长编码（1、2 或 5 字节），小值用少字节存储（例如，前向长度 < 254 时用 1 字节）。
2. **整数优化**：小整数（如 0-12）直接用 4 位编码嵌入头部，无需额外存储值（`ZIP_INT_IMM`），节省空间。
3. **连续内存**：所有元素存储在连续内存块中，减少内存碎片，提升访问效率（通过指针偏移快速定位元素）。

### 五、实例：创建并插入元素的过程

假设创建一个空压缩列表，然后依次插入字符串 "a"（长度 1）和整数 100，步骤如下：

#### 1. 初始创建（`ziplistNew()`）

- 结构：`zlbytes=11`（4+4+2+1），`zltail=10`（头部结束位置），`zllen=0`，`zlend=0xFF`。
- 内存布局（简化）：`[zlbytes(4B)][zltail(4B)][zllen(2B)][zlend(1B)]`。

#### 2. 插入字符串 "a" 到尾部（`ziplistPush(zl, "a", 1, ZIPLIST_TAIL)`）

- 元素 "a" 的编码：字符串，`len=1`，`lensize=1`（因 len < 64），`prevrawlen=0`（第一个元素），`prevrawlensize=1`。
- 新元素总大小：`prevrawlensize(1) + lensize(1) + len(1) = 3` 字节。
- 插入后更新：
  - `zlbytes = 11 + 3 = 14`。
  - `zltail = 10`（原头部结束位置）→ 新元素起始偏移 10，尾偏移 10 + 3 -1 = 12（元素总长度 3，结束位置 10+3=13，偏移 12）。
  - `zllen = 1`。
  - 内存布局：`[zlbytes=14][zltail=12][zllen=1][prevrawlen=0(1B)][len=1(1B)][val="a" (1B)][zlend=0xFF]`。

#### 3. 插入整数 100 到头部（`ziplistPush(zl, NULL, 0, ZIPLIST_HEAD)`，整数 100 需转换为字符串形式传入）

- 整数 100 的编码：`ZIP_INT_16`（16 位整数），`lensize=1`，`prevrawlen=0`（头部插入，新元素是第一个），总大小：`prevrawlensize(1) + lensize(1) + len(2) = 4` 字节。
- 调整原第一个元素（"a"）的 `prevrawlen`：原 "a" 的 `prevrawlen` 从 0 改为 4（新元素的总长度）。
- 插入后更新：
  - `zlbytes = 14 + 4 = 18`。
  - `zltail` 保持 12（因尾部元素还是 "a"）。
  - `zllen = 2`。
  - 内存布局：`[zlbytes=18][zltail=12][zllen=2][prevrawlen=0(1B)][int16编码(1B)][100(2B)][prevrawlen=4(1B)][len=1(1B)][val="a" (1B)][zlend=0xFF]`。

### 六、总结

压缩列表的创建和插入流程围绕**内存紧凑性**设计，通过变长编码、整数优化和连续内存布局实现高效存储。核心接口 `ziplistNew()`、`ziplistPush()` 和 `ziplistInsert()` 封装了复杂的内存管理和元数据更新逻辑，确保插入操作安全且高效。当压缩列表过大（如元素过多或元素过大）时，Redis 会自动将其转换为其他结构（如哈希表）以平衡性能。





编辑

分享

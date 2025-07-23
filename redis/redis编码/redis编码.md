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

- 编码方式的动态切换
  - String
    - 整数值（如 SET key 123）→ 编码为 INT
    - 短字符串（长度 < 44 字节）→ 编码为 EMBSTR
    - 长字符串 → 编码为 RAW
  - Hash
    - 字段数少且值小 → 编码为 ZIPLIST（压缩列表）
    - 字段数多或值大 → 编码为 HASHTABLE
  - List
    - quicklist（Redis 3.2+）：由多个 ziplist 组成的双向链表，平衡内存和性能。
    - ziplist（旧版本）：元素少且值小时使用。
  - set无序、唯一元素集合，支持交集、并集等操作。
    - intset：当元素全为整数且数量较少时使用。
    - hashtable：元素包含字符串或数量较多时使用。
  - Sorted Set元素带分数（score），按分数排序，支持范围查询
    - ziplist：元素少且值小时使用。
    - skiplist（跳跃表）：元素多或值大时使用，平衡树和链表的优势。
  -  Bitmap（位图）
    - 本质：基于 String 类型的位操作，每个位存储 0 或 1。
    - 命令：SETBIT、GETBIT、BITCOUNT、BITOP。 应用场景：用户签到（每天占 1 位）、活跃用户统计。

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

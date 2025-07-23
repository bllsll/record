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

压缩列表中间插入数据的核心是：**定位位置→分配空间→移动节点→写入新节点→处理连锁更新→更新元数据**。其中，“连锁更新” 是最复杂的部分，可能导致插入操作的时间复杂度从 O (N)（N 为节点数）变为 O (N*M)（M 为连锁更新的节点数），但因压缩列表通常用于存储少量数据，实际影响较小。

### 六、总结

压缩列表的创建和插入流程围绕**内存紧凑性**设计，通过变长编码、整数优化和连续内存布局实现高效存储。核心接口 `ziplistNew()`、`ziplistPush()` 和 `ziplistInsert()` 封装了复杂的内存管理和元数据更新逻辑，确保插入操作安全且高效。当压缩列表过大（如元素过多或元素过大）时，Redis 会自动将其转换为其他结构（如哈希表）以平衡性能。
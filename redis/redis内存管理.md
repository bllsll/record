- redis过期数据删除

  - 1.惰性删除，访问到key的时候判断是否过期，过期则删除

  - 2.定时扫描删除
    - redis会将设置过期时间的key放到一个独立的字典中，会定时遍历这个字典来删除到期的key。
    - Redis默认每秒进行10次过期key的扫描，每次扫描不会遍历字典中的所有key，而是按照如下策略： a.从过期字典中随机选择20个key； b.删除这20个key中已经过期的数据 c.如果过期的key的比重超过1/4则重复步骤a) 另外，为了保证过期扫描不会出现过度循环，导致线程卡死，算法增加了扫描时间上限，默认是25ms。当出现大量的key设置相同的过期时间的时候，则会出现连续扫描导致读写请求出现明显的卡顿。因此，对于一些活动系统中可能会出现大量数据过期的，应为key的过期时间设置一个随机数，不能在同一时间内过期。
    - ![image-20250801194020554](/Users/shilinling/Library/Application Support/typora-user-images/image-20250801194020554.png)

  如果之前回收键逻辑超时，则在Redis触发内部事件之前再次以快模

  式运行回收过期键任务，快模式下超时时间为1毫秒且2秒内只能运行1次。

  4）快慢两种模式内部删除逻辑相同，只是执行的超时时间不同

  --这里如果cpu高了，可能是相同时间过期的key多了。

- 淘汰策略：命令：通过redis-cli CONFIG GET maxmemory-policy查看
  - 不淘汰策略noeviction，内存不足时，能读不能写
  - 最近最少使用volatile-lru：从设置了过期时间的key中选择最近最久未访问的key
  - 根据过期时间优先volatile-ttl：从设置了过期时间的key中选择剩余时间最短的key删除
  - 随机删除volatile-random：从设置了过期时间的key中随机删除
  - 全局最近最少时间allkeys-lru：从所有key中选择最少使用的key，无论是否设置过期时间
  - 全局随机删除allkeys-random：从所有key中随机删除，不管是否设置过期时间

当Redis因为内存溢出删除键时，可以通过执行info stats命令

查看evicted_keys指标找出当前Redis服务器已剔除的键数量。



`INFO memory` 是 Redis 中用于查看内存使用详细信息的命令，输出结果包含 Redis 内存分配、使用量、碎片率等关键指标。以下是对其核心字段的系统解读，帮助你全面了解 Redis 内存状态：

### 一、基础内存使用指标

| 字段                     | 含义                                    | 关键说明                                                     |
| ------------------------ | --------------------------------------- | ------------------------------------------------------------ |
| `used_memory`            | Redis 实际使用的内存总量（字节）        | 包含数据、元数据、字典表等 Redis 内部管理的所有内存，是 Redis 自身统计的 “逻辑内存”。 |
| `used_memory_human`      | 人类可读格式的 `used_memory`            | 如 `1.26G`，直观展示当前内存占用。                           |
| `used_memory_rss`        | 系统视角的内存占用（Resident Set Size） | 操作系统分配给 Redis 进程的物理内存，包含碎片、未释放的空闲块等，是 “物理内存”。 |
| `used_memory_rss_human`  | 人类可读格式的 `used_memory_rss`        | 如 `7.57G`，反映系统实际消耗的内存。                         |
| `used_memory_peak`       | 历史最高内存使用量（字节）              | 记录 Redis 运行以来 `used_memory` 的最大值，用于评估内存峰值压力。 |
| `used_memory_peak_human` | 人类可读格式的 `used_memory_peak`       | 如 `15.11G`，结合当前值判断是否存在内存泄漏风险。            |
| `used_memory_peak_perc`  | 当前内存 / 峰值内存的百分比             | 如 `8.31%`，比例过低可能说明曾有临时大内存占用（如批量操作）。 |

### 二、内存组成细分

| 字段                       | 含义                         | 关键说明                                                     |
| -------------------------- | ---------------------------- | ------------------------------------------------------------ |
| `used_memory_overhead`     | 内存开销（元数据、字典表等） | Redis 为管理数据额外消耗的内存（如键名、过期时间、哈希表结构等），正常应低于 `used_memory` 的 50%。 |
| `used_memory_dataset`      | 实际数据占用的内存           | 计算公式：`used_memory - used_memory_overhead`，反映真实业务数据的内存消耗。 |
| `used_memory_dataset_perc` | 数据内存占总内存的比例       | 如 `68.04%`，比例越高说明内存利用率越好（通常建议 >50%），过低可能是 overhead 过高（如大量小键）。 |
| `used_memory_lua`          | Lua 脚本占用的内存           | 如 `40.00K`，通常很小，若过大可能是执行了复杂 Lua 脚本。     |

### 三、内存碎片与分配器指标

| 字段                      | 含义               | 关键说明                                                     |
| ------------------------- | ------------------ | ------------------------------------------------------------ |
| `mem_fragmentation_ratio` | 内存碎片率         | 计算公式：`used_memory_rss / used_memory`，健康值 `1.0~1.5`，>2.0 表示严重碎片。 |
| `mem_fragmentation_bytes` | 碎片占用的额外内存 | 如 `6.32G`，直接反映碎片浪费的物理内存。                     |
| `allocator_allocated`     | 分配器已分配的内存 | 内存分配器（如 jemalloc）向 Redis 提供的内存总量。           |
| `allocator_active`        | 分配器活跃内存     | 分配器从系统申请的内存（包含内部碎片）。                     |
| `allocator_resident`      | 分配器驻留内存     | 分配器实际占用的物理内存（接近 `used_memory_rss`）。         |
| `allocator_frag_ratio`    | 分配器内部碎片率   | 计算公式：`allocator_active / allocator_allocated`，>1.2 说明分配器内部碎片高。 |
| `allocator_rss_ratio`     | 分配器驻留率       | 计算公式：`allocator_resident / allocator_active`，接近 1 正常，过高可能是系统内存紧张。 |

### 四、内存限制与策略

| 字段               | 含义                       | 关键说明                                                     |
| ------------------ | -------------------------- | ------------------------------------------------------------ |
| `maxmemory`        | 配置的最大内存限制（字节） | 如 `19.74G`，当 `used_memory` 达到此值，会触发 `maxmemory_policy` 淘汰策略。 |
| `maxmemory_human`  | 人类可读格式的 `maxmemory` | 直观展示内存上限。                                           |
| `maxmemory_policy` | 内存满时的淘汰策略         | 常见策略：`volatile-lru`（淘汰设过期时间的键）、`allkeys-lru`（淘汰所有键）等，需根据业务场景配置。 |

### 五、其他重要指标

| 字段                       | 含义                     | 关键说明                                                     |
| -------------------------- | ------------------------ | ------------------------------------------------------------ |
| `mem_allocator`            | 使用的内存分配器         | 如 `jemalloc-5.1.0`，Redis 推荐使用 jemalloc 或 tcmalloc，性能优于系统默认分配器。 |
| `active_defrag_running`    | 是否正在进行自动碎片整理 | `0` 表示未运行，`1` 表示正在整理（需开启 `active-defrag` 配置）。 |
| `lazyfree_pending_objects` | 等待异步释放的对象数     | 非零表示有大键正在后台异步删除（不阻塞主线程），通常很快会归 0。 |
| `mem_clients_normal`       | 普通客户端连接占用的内存 | 如 `292KB`，过高可能是连接数过多（每个连接有缓冲区开销）。   |

### 六、如何利用这些指标？

1. **判断内存是否充足**：对比 `used_memory` 与 `maxmemory`，若接近上限且 `maxmemory_policy` 频繁触发，需扩容或优化存储。
2. **检查碎片问题**：`mem_fragmentation_ratio > 1.5` 时，启用 `active-defrag` 或重启清理碎片。
3. **评估内存效率**：`used_memory_dataset_perc` 过低（如 <30%），可能是大量小键导致 overhead 过高，需合并小键（如用 hash 存储）。
4. **排查异常峰值**：`used_memory_peak` 远高于当前值，需分析是否有周期性大内存操作（如全量同步、批量导入）。

通过 `INFO memory` 提供的指标，可全面掌握 Redis 内存的使用状态、健康程度和潜在风险，是日常运维和性能优化的核心依据。









![image-20250801193140460](/Users/shilinling/Library/Application Support/typora-user-images/image-20250801193140460.png)

Redis进程内消耗主要包括：自身内存+对象内存+缓冲内存+内存碎片，

自身内存:其中Redis空进程自身内存消耗非常少，通常used_memory_rss在3MB左右，used_memory在800KB左右，

对象内存是Redis内存占用最大的一块，存储着用户所有的数据。

缓冲内存主要包括：客户端缓冲、复制积压缓冲区、AOF缓冲区。







使用object refcount{key}获取当前对象引用。

高并发写入场景中，在条件允许的情况下，建议字符串长度控制在39字节以内，减少创建redisObject内存分配次数，从而提高性能。

降低Redis内存使用最直接的方式就是缩减键（key）和值（value）的长度。 value可以将proto序列化存进去。

共享对象池是指Redis内部维护[0-9999]的整数对象池。共享对象池与maxmemory+LRU策略冲突，使用时需要注意。整数对象池在Redis中通过变量REDIS_SHARED_INTEGERS定义，不能通过配置修改。



尽量减少字符串频繁修改操作如append、setrange，改为直接使用set修改字符串，降低预分配带来的内存浪费和内存碎片化。因为那些命令会预分配一些内存。



请问一下，大量string key value 和吧这些string当到一个hash中，内存哪个用的少呢？

### 四、注意事项

1. **hash 不宜过大**：若 `hash` 包含过多键值对（超过 1 万），可能转为 `hashtable` 编码，且操作效率下降（需遍历整个 `hash`）。建议按业务维度拆分（如每 1000 个用户一个 `hash`：`users:0`、`users:1`...）。
2. **field 和 value 不宜过长**：超过 `ziplist` 阈值会导致编码升级，内存优势减弱，需控制 `field` 和 `value` 长度。
3. **操作灵活性**：独立 `string` 键支持单独过期（`EXPIRE`），而 `hash` 中的 `field` 无法单独设置过期（只能为整个 `hash` 设置过期），需根据业务需求选择。

### 总结

**在大多数场景下，将大量小 `string` 键值对合并到 `hash` 中，内存占用会显著更少**，尤其是当 `field` 和 `value` 较短且数量适中时（符合 `ziplist` 条件）。这是 Redis 优化内存的常用技巧，特别适合存储用户信息、商品属性等具有共同前缀的批量数据。

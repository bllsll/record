1.RDB:内存快照，指定间隔内将内存中的数据快照。优点：恢复速度快。rdb文件是二进制格式的数据库文件，对于AOF文件来说体积小。缺点：数据可能会丢失

- 主进程fork一个子进程，子进程来进行RDB文件生成。![img](https://api2.mubu.com/v3/document_image/d16851ad-a99e-4e52-be68-e25935173f31-14247913.jpg)

- 2.AOF
  - redis服务器启动，通过AOF文件恢复数据。流程：将客户端写请求，放到AOF缓冲区，每秒刷新缓冲区数据到AOF文件中，在AOF文件中进行AOF重写（就是将冗余的命令进行重写）
  
  - 优点：数据不会丢失那么多
  
  - 缺点：数据恢复会比RDB慢，文件大
  
- 3.redis4.0之后使用混合持久化，RDB+AOF

- 6.x之后io多线程：多线程去读和写io


save命令：阻塞当前redis服务器，知道rdb过程完成。线上环境禁止使用，会造成长时间阻塞。


bgsave命令：redis进程执行fork操作创建子进程，rdb持久化过程由子进程负责。阻塞只会发生在fork阶段，一般时间很短。是save的优化。

redis内部所有涉及到rdb的操作都是用bgsave， save已废弃。

除了执行命令手动触发之外，Redis内部还存在自动触发RDB的持久化

机制，例如以下场景：

1）使用save相关配置，如“save m n”。表示m秒内数据集存在n次修改

时，自动触发bgsave。

```ini
save 900 1    # 15分钟（900秒）内至少有1个键被修改，触发BGSAVE
save 300 10   # 5分钟（300秒）内至少有10个键被修改，触发BGSAVE
save 60 10000 # 1分钟（60秒）内至少有10000个键被修改，触发BGSAVE
```

- **规则逻辑**：多个 `save` 配置是 “或” 的关系，只要满足其中任意一条，就会自动触发后台持久化（`BGSAVE`）。
- **举例**：若 1 分钟内有 10000 次键修改，会立即触发 `BGSAVE`，无需等待 5 分钟或 15 分钟的条件。

2）如果从节点执行全量复制操作，主节点自动执行bgsave生成RDB文

319件并发送给从节点，更多细节见6.3节介绍的复制原理。

3）执行debug reload命令重新加载Redis时，也会自动触发save操作。

4）默认情况下执行shutdown命令时，如果没有开启AOF持久化功能则

自动执行bgsave。



![image-20250729191105094](/Users/shilinling/Library/Application Support/typora-user-images/image-20250729191105094.png)

1）执行bgsave命令，Redis父进程判断当前是否存在正在执行的子进

程，如RDB/AOF子进程，如果存在bgsave命令直接返回。

2）父进程执行fork操作创建子进程，fork操作过程中父进程会阻塞，通

过info stats命令查看latest_fork_usec选项，可以获取最近一个fork操作的耗

时，单位为微秒。

3213）父进程fork完成后，bgsave命令返回“Background saving started”信息

并不再阻塞父进程，可以继续响应其他命令。

4）子进程创建RDB文件，根据父进程内存生成临时快照文件，完成后

对原有文件进行原子替换。执行lastsave命令可以获取最后一次生成RDB的

时间，对应info统计的rdb_last_save_time选项。

5）进程发送信号给父进程表示完成，父进程更新统计信息，具体见

info Persistence下的rdb_*相关选项。



info Persistence 

loading:0
async_loading:0
current_cow_peak:0
current_cow_size:0
current_cow_size_age:0
current_fork_perc:0.00
current_save_keys_processed:0
current_save_keys_total:0
rdb_changes_since_last_save:113601685
rdb_bgsave_in_progress:0
rdb_last_save_time:1753749082
rdb_last_bgsave_status:ok
rdb_last_bgsave_time_sec:34
rdb_current_bgsave_time_sec:-1
rdb_saves:271
rdb_last_cow_size:79597568
rdb_last_load_keys_expired:0
rdb_last_load_keys_loaded:0
aof_enabled:0
aof_rewrite_in_progress:0
aof_rewrite_scheduled:0
aof_last_rewrite_time_sec:-1
aof_current_rewrite_time_sec:-1
aof_last_bgrewrite_status:ok
aof_rewrites:0
aof_rewrites_consecutive_failures:0
aof_last_write_status:ok
aof_last_cow_size:0
module_fork_in_progress:0
module_fork_last_cow_size:0

这段 `INFO Persistence` 输出展示了 Redis 实例的持久化状态，重点关注 RDB 持久化的关键指标，以下是详细解析：

### 一、核心 RDB 持久化指标分析

| 指标                          | 数值                | 含义与关键分析                                               |                                                              |
| ----------------------------- | ------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| `rdb_changes_since_last_save` | 113,601,685         | 上次 RDB 保存后，数据集累计修改次数                          | **异常点**：修改次数超过 1.1 亿次，说明自上次 `BGSAVE` 后数据变更极其频繁，若此时发生宕机，可能丢失大量数据。 |
| `rdb_last_save_time`          | 1753749082          | 最近一次 `BGSAVE` 完成的时间戳（可转换为具体时间）           | 结合当前时间计算间隔：若距离现在超过配置的 `save` 阈值（如默认 60 秒 / 300 秒 / 900 秒），需排查为何未触发自动 `BGSAVE`。 |
| `rdb_last_bgsave_status`      | ok                  | 最近一次 `BGSAVE` 执行结果                                   | 状态正常，无失败记录。                                       |
| `rdb_last_bgsave_time_sec`    | 34 秒               | 最近一次 `BGSAVE` 的耗时                                     | **耗时较长**：34 秒属于高耗时（正常应在秒级内），说明数据集较大或执行时系统资源（CPU/IO）紧张，`BGSAVE` 期间可能阻塞其他操作（虽然是后台执行，但 `fork` 和 IO 会占用资源）。 |
| `rdb_saves`                   | 271                 | 累计执行 `BGSAVE` 的次数                                     | 结合实例运行时间可判断保存频率，若频率过低，需检查 `save` 配置是否合理。 |
| `rdb_last_cow_size`           | 79,597,568（≈76MB） | 最近一次 `BGSAVE` 时，写时复制（Copy-On-Write）产生的内存副本大小 | 数值较大，说明 `BGSAVE` 期间有大量数据被修改，进一步验证了数据变更频繁的特点。 |

### 二、AOF 持久化状态

| 指标                               | 数值    | 含义                          |
| ---------------------------------- | ------- | ----------------------------- |
| `aof_enabled`                      | 0       | 未启用 AOF 持久化，仅依赖 RDB |
| 其他 AOF 指标（如 `aof_rewrites`） | 0 或 -1 | 因 AOF 未启用，均为默认值     |

### 三、关键问题与风险

1. **数据丢失风险高**：

   - 自上次 `BGSAVE` 后已有 1.1 亿次修改，若此时发生宕机，这些修改将全部丢失（因未启用 AOF）。
   - 需确认 `save` 配置是否被修改（如是否删除了高频触发规则），为何如此多的修改未触发自动 `BGSAVE`。

2. **`BGSAVE` 耗时过长**：

   - 34 秒的

      

     ```
     BGSAVE
     ```

      

     耗时可能导致：

     - 执行期间占用大量 CPU/IO，影响 Redis 正常响应；
     - `fork` 操作可能阻塞主线程（尤其是大内存实例），导致短暂卡顿。

3. **仅依赖 RDB 的局限性**：

   - RDB 是快照式持久化，无法做到实时数据保护，对于高频写入场景（如本例 1.1 亿次修改），数据安全性不足。





![image-20250729194549955](/Users/shilinling/Library/Application Support/typora-user-images/image-20250729194549955.png)

![image-20250729194702796](/Users/shilinling/Library/Application Support/typora-user-images/image-20250729194702796.png)



redis，持久混合化，最后也只是生成了aof文件。

1. **AOF 重写时的混合格式生成**：
   当触发 AOF 重写（自动或手动 `BGREWRITEAOF`）时：

   - 第一步：Redis 会先执行一次**内存快照**（类似 `BGSAVE`），生成 RDB 格式的全量数据，并写入新的 AOF 重写文件头部。
   - 第二步：重写过程中产生的新命令，会以增量日志的形式追加到 RDB 快照之后。
   - 最终结果：重写后的 AOF 文件结构为 `[RDB 全量数据] + [增量命令日志]`，替代旧的纯命令 AOF 文件。

   （注意：混合模式下**不会单独生成独立的 RDB 文件**，除非配置了 `save` 规则 —— 若同时配置 `save` 和混合 AOF，则会额外按 `save` 规则生成独立 RDB 文件，用于备份。）

Redis 的 AOF 重写可以手动触发，也可以自动触发1。具体如下1：

- **手动触发**：通过执行`BGREWRITEAOF`命令来手动触发 AOF 重写。该命令会在后台启动一个子进程，专门负责对 AOF 文件进行重写操作，不会阻塞主线程对客户端请求的处理。
- **自动触发**：由配置参数`auto-aof-rewrite-min-size`和`auto-aof-rewrite-percentage`控制。其中，`auto-aof-rewrite-min-size`用于设置 AOF 文件达到指定大小时才可能触发重写，默认值是 64MB；`auto-aof-rewrite-percentage`用于设置 AOF 文件大小相较于上次重写后的增长百分比，默认值为 100%。当 AOF 文件大小超过`auto-aof-rewrite-min-size`，并且自上次重写后增长超过`auto-aof-rewrite-percentage`时，Redis 会自动触发 AOF 重写。

此外，Redis 服务器内部定期执行的`serverCron`函数会检查相关条件，若 AOF 功能已开启、当前没有活跃的子进程（如 RDB 或 AOF 子进程）、设置了 AOF 重写的增长率阈值且当前 AOF 文件大小超过重写所需的最小大小，也会触发 AOF 重写2。



### 二、混合模式相比纯 AOF / 纯 RDB 的核心优势

| 维度                | 纯 RDB                   | 纯 AOF                     | 混合模式（RDB+AOF）                       |
| ------------------- | ------------------------ | -------------------------- | ----------------------------------------- |
| **数据安全性**      | 低（丢失快照后的数据）   | 高（最多丢失 1 秒数据）    | 同纯 AOF（依赖增量命令）                  |
| **文件体积**        | 小（二进制压缩）         | 大（文本命令重复）         | 小（RDB 压缩 + 增量命令）                 |
| **恢复速度**        | 快（直接加载快照）       | 慢（重放所有命令）         | 快（先加载 RDB 快照，再重放少量增量命令） |
| **重写 / 快照效率** | 快（但频繁执行影响性能） | 慢（遍历全量数据生成命令） | 快（利用 RDB 快照加速重写）               |
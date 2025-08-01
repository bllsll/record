redis阻塞分析

·API或数据结构使用不合理。

·CPU饱和的问题。

·持久化相关的阻塞。





1.通过慢查询命令发现问题，如果发现了是高耗时命令

1）修改为低算法度的命令，如hgetall改为hmget等，禁用keys、sort等命

令。

2）调整大对象：缩减大对象数据或把大对象拆分为多个小对象，防止

407一次命令操作过多的数据。大对象拆分过程需要视具体的业务决定，如用户

好友集合存储在Redis中，有些热点用户会关注大量好友，这时可以按时间

或其他维度拆分到多个集合中。

Redis本身提供发现大对象的工具，对应命令：redis-cli-h{ip}-

p{port}bigkeys。内部原理采用分段进行scan操作，把历史扫描过的最大对象

统计出来便于分析优化，





2.cpu饱和

用top命令看redis的cpu有没有比较高

`redis-cli --stat` 是一个轻量级的实时监控工具，输出简洁直观，适合快速了解 Redis 的实时负载、内存使用和连接状态。它不会对 Redis 服务器造成明显性能影响，可在生产环境中放心使用，尤其适合临时排查问题或观察短期变化趋势。

根据info commandstats统计信息分析出命令不合理开销时间

发现hset耗时比较长，明明hset时间复杂度是0(1)

是因为上面的Redis实例为了追求低内存使用量，过度放宽ziplist使用条件

（修改了hash-max-ziplist-entries和hash-max-ziplist-value配置）。进程内的

hash对象平均存储着上万个元素，而针对ziplist的操作算法复杂度在O（n）

到O（n2）之间。虽然采用ziplist编码后hash结构内存占用会变小，但是操作

变得更慢且更消耗CPU。



3.持久化阻塞

fork阻塞、AOF刷盘阻塞、

HugePage写操作阻塞。

1）fork阻塞，发生在rdb和aof重写的时候。可以执行info stats 看lastest_fork_usec指标，表示redis最近一次fork操作耗时

2）AOF刷盘阻塞：查看info persistence统计中的aof_delayed_fsync指标，每次发生fdatasync阻塞主线程时会累加。

硬盘压力可能是Redis进程引起的，也可能是其他进程引起的，可以使

用iotop查看具体是哪个进程消耗过多的硬盘资源。

直接执行 `iotop` 即可启动交互式监控界面，常用参数：

- `iotop -o`：只显示正在产生 I/O 活动的进程
- `iotop -b`：非交互式模式（适合脚本或定时任务）
- `iotop -P`：按进程而非线程显示

```bash
# CentOS 7 及以下
sudo yum install -y iotop

# CentOS 8/RHEL 8 及以上（使用 dnf）
sudo dnf install -y iotop
```

排查Redis自身原因引起的阻塞原因之后，如果还没有定位问题，需要

排查是否由外部原因引起。围绕以下三个方面进行排查：

·CPU竞争，用top看。如果redis绑定了一个cpu，并且开了持久化，可能导致。

·内存交换

·网络问题

**CPU 密集型应用**（CPU-bound application）是指程序的**主要性能瓶颈在于 CPU 的计算能力**，其运行效率高度依赖 CPU 的处理速度，而受限于内存、磁盘 I/O、网络等其他资源的影响较小。









1）客户端最先感知阻塞等Redis超时行为，加入日志监控报警工具可快

速定位阻塞问题，同时需要对Redis进程和机器做全面监控。

2）阻塞的内在原因：确认主线程是否存在阻塞，检查慢查询等信息，

发现不合理使用API或数据结构的情况，如keys、sort、hgetall等。关注CPU

使用率防止单核跑满。当硬盘IO资源紧张时，AOF追加也会阻塞主线程。

3）阻塞的外在原因：从CPU竞争、内存交换、网络问题等方面入手排

查是否因为系统层面问题引起阻塞。
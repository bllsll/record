### 一、核心原理：利用 Redis 的 “原子性” 实现互斥

Redis 分布式锁的核心逻辑是：**通过一个唯一的 “锁标识”，让多个请求竞争同一个 Redis 键，只有成功创建该键的请求能获得锁，其他请求则等待或失败**。

关键依赖 Redis 的两个特性：

1. `SET` 命令的原子性

   ```
   SET key value NX PX timeout
   ```

   命令可实现 “仅当键不存在时才设置值，并指定过期时间”，避免多个请求同时创建锁。

   - `NX`（Not Exist）：仅当键不存在时才执行设置，保证互斥性（同一时间只有一个请求能成功）。
   - `PX timeout`：为键设置过期时间（毫秒），避免因持有锁的进程崩溃导致 “死锁”（锁永远不释放）。

2. **单线程模型**：Redis 处理命令是单线程的，确保 `SET` 命令的执行不会被其他命令打断，天然具备原子性。

### 二、基本实现步骤

#### 1. 获取锁（竞争锁）

用 `SET` 命令创建一个唯一的锁键，值设为 “唯一标识”（如 UUID），确保后续释放锁时能验证 “锁的归属”（避免误删他人的锁）。

**命令示例**（Redis 客户端执行）：

```bash
# 键：lock:resource（如 lock:order:1001，标识“订单1001的锁”）  
# 值：随机UUID（如 "uuid-123456"，唯一标识当前请求）  
# NX：仅当键不存在时设置  
# PX 30000：过期时间30秒（避免死锁）  
SET lock:order:1001 "uuid-123456" NX PX 30000  
```

- 若返回 `OK`：表示成功获取锁，可执行后续业务逻辑。
- 若返回 `nil`：表示锁已被其他请求持有，当前请求需等待或失败。

#### 2. 执行业务逻辑

获取锁后，即可安全操作共享资源（如查询库存、扣减数量、更新数据库等）。

#### 3. 释放锁（关键！需验证 “锁归属”）

业务执行完成后，需释放锁（删除 Redis 键），但**必须先验证 “当前锁是否归自己所有”**（避免因锁过期后，误删其他请求的新锁）。

释放锁需保证 “验证 + 删除” 的原子性（否则可能出现 “验证通过后，锁过期被他人获取，此时删除的是他人的锁”），需用 **Lua 脚本** 实现（Redis 保证 Lua 脚本执行的原子性）。

**Lua 脚本示例**：

```lua
-- 若锁的值（UUID）与当前请求的UUID一致，则删除锁（释放）  
if redis.call("get", KEYS[1]) == ARGV[1] then  
    return redis.call("del", KEYS[1])  
else  
    return 0  
end  
```

**执行方式**（Redis 客户端调用）：

```bash
# KEYS[1] = 锁键，ARGV[1] = 当前请求的UUID  
EVAL "if redis.call('get', KEYS[1]) == ARGV[1] then return redis.call('del', KEYS[1]) else return 0 end" 1 lock:order:1001 "uuid-123456"  
```

### 三、核心要求：分布式锁的 “四要素”

一个可靠的 Redis 分布式锁需满足以下条件：

| 要求       | 说明                                                         |
| ---------- | ------------------------------------------------------------ |
| **互斥性** | 同一时间只能有一个请求持有锁（避免并发冲突）。               |
| **安全性** | 锁只能被持有它的请求释放（避免误删他人的锁）；即使持有锁的进程崩溃，锁也能自动释放（避免死锁）。 |
| **可用性** | 锁的获取和释放过程需高效（Redis 高性能保证）；即使 Redis 单点故障，锁服务仍能尽量可用（如集群部署）。 |
| **容错性** | 部分 Redis 节点宕机时，锁仍能正常工作（需配合集群或主从复制）。 |

### 四、常见问题与解决方案

#### 1. 问题：锁过期时间设置不合理，导致业务未执行完锁已释放

- **场景**：若业务执行时间（如 50 秒）长于锁过期时间（如 30 秒），锁会提前释放，其他请求可能趁机获取锁，导致并发冲突。
- 解决方案
  - 预估业务最大执行时间，设置略长的过期时间（预留冗余）。
  - 实现 “锁续约”（看门狗机制）：持有锁的进程启动一个后台线程，每隔一段时间（如过期时间的 1/3）检查锁是否仍持有，若持有则延长过期时间（如 Redisson 框架已内置该功能）。

#### 2. 问题：Redis 主从复制导致 “锁丢失”

- **场景**：Redis 通常部署主从集群（主节点写入，从节点同步）。若主节点刚写入锁，还未同步到从节点就宕机，从节点升级为主节点后，新主节点中无锁信息，其他请求会重新获取锁，导致 “双锁共存”。
- 解决方案
  - 方案 1：使用 Redis 集群的 “Redlock 算法”（复杂，争议较大）：向多个独立的 Redis 实例（至少 3 个）申请锁，只有多数实例（≥3）成功获取锁才算成功，降低单节点故障影响。
  - 方案 2：接受主从复制的 “最终一致性”，结合业务兜底逻辑（如数据库唯一索引、幂等设计），减少锁丢失的影响（实际项目中更常用）。

#### 3. 问题：释放锁时的 “非原子操作” 导致误删

- **场景**：若不使用 Lua 脚本，而是先执行 `GET` 验证锁归属，再执行 `DEL` 释放，两步操作之间可能因锁过期被他人获取，导致 `DEL` 误删新锁。
- **解决方案**：**必须用 Lua 脚本保证 “验证 + 删除” 的原子性**（如上文示例）。

### 五、实战：两种实现方式（原生命令 vs 框架封装）

#### 1. 原生 Redis 命令实现（需手动处理细节）

以 Java + Jedis 为例：

```java
import redis.clients.jedis.Jedis;  
import java.util.UUID;  

public class RedisLock {  
    private static final String LOCK_KEY = "lock:order:1001"; // 锁键（根据资源动态生成）  
    private static final int LOCK_EXPIRE = 30000; // 锁过期时间（30秒）  
    private Jedis jedis;  

    // 获取锁  
    public String acquireLock() {  
        String lockValue = UUID.randomUUID().toString(); // 唯一标识（避免误删）  
        // 执行 SET NX PX 命令  
        String result = jedis.set(LOCK_KEY, lockValue, "NX", "PX", LOCK_EXPIRE);  
        return "OK".equals(result) ? lockValue : null; // 成功返回锁值，失败返回null  
    }  

    // 释放锁（Lua脚本）  
    public boolean releaseLock(String lockValue) {  
        String luaScript = "if redis.call('get', KEYS[1]) == ARGV[1] then return redis.call('del', KEYS[1]) else return 0 end";  
        Long result = (Long) jedis.eval(luaScript, 1, LOCK_KEY, lockValue);  
        return result == 1; // 1表示释放成功  
    }  
}  
```

#### 2. 框架封装：Redisson（推荐，开箱即用）

Redisson 是 Redis 的 Java 客户端，内置了分布式锁实现，自动处理锁续约、主从切换、原子操作等细节，无需手动编码。

- **依赖引入**：

```xml
<dependency>  
    <groupId>org.redisson</groupId>  
    <artifactId>redisson</artifactId>  
    <version>3.20.0</version>  
</dependency>  
```

- **使用示例**：

```java
import org.redisson.Redisson;  
import org.redisson.api.RLock;  
import org.redisson.api.RedissonClient;  
import org.redisson.config.Config;  

public class RedissonLockDemo {  
    public static void main(String[] args) {  
        // 初始化 Redisson 客户端  
        Config config = new Config();  
        config.useSingleServer().setAddress("redis://127.0.0.1:6379");  
        RedissonClient redisson = Redisson.create(config);  

        // 获取锁（锁键）  
        RLock lock = redisson.getLock("lock:order:1001");  

        try {  
            // 尝试获取锁：最多等待10秒，100秒后自动释放（默认开启看门狗续约）  
            boolean isLocked = lock.tryLock(10, 100, java.util.concurrent.TimeUnit.SECONDS);  
            if (isLocked) {  
                // 执行业务逻辑（如扣减库存、下单等）  
                System.out.println("获取锁成功，执行业务...");  
            }  
        } catch (InterruptedException e) {  
            e.printStackTrace();  
        } finally {  
            // 释放锁（仅当当前线程持有锁时）  
            if (lock.isHeldByCurrentThread()) {  
                lock.unlock();  
            }  
        }  
        redisson.shutdown();  
    }  
}  
```

### 六、总结

Redis 分布式锁是解决分布式并发问题的实用方案，核心依赖 `SET NX PX` 命令的原子性和 Lua 脚本的释放逻辑。实际开发中，推荐使用 **Redisson 框架**（减少手动处理细节的成本），并注意：

- 合理设置锁过期时间，必要时启用续约机制；
- 结合业务场景设计兜底逻辑（如幂等性、数据库约束），降低锁异常的影响；
- 集群环境下需评估主从复制对锁一致性的影响，选择适合的方案。

通过以上设计，可在保证高性能的同时，实现可靠的分布式互斥。
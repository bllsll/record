#### redis事务

- 什么是事务 ACID特性

  - A. 原子性：要么全部成功，要么全部失败。
    - redis不接受回滚，如果事务中有一条命令执行出错了，也会继续往下执行。
  - C.一致性：完整约束的一致，用户逻辑的一致
    - key本来是set，不能把它变成zset，不能用zset命令
    - 用户逻辑是执行key + 1， 上一步结果，key应该是1000，不能中途被别人修改为别的
  - I.隔离性：各个事务之间相互影响的程度。
    - redis是单线程执行的，天然具有隔离性
    - mysql是多线程执行
  - D.持久性

- 事务命令

  - 开启事务：MULTI
    - mysql：begin/start transcation
  - 取消事务：DISCARD
    - mysql：rollback
  - 提交事务：EXEC
    - mysql：commit
    - 如果事务被取消了，提交失败，则返回nil
  - 监控：WATCH ：检测key变动，在食物执行中，key变动则取消事务
    - 事务开启前调用，乐观锁实现cas
  - ![image-20250621202031993](/Users/shilinling/Library/Application Support/typora-user-images/image-20250621202031993.png)
  - 原理：执行multi开启事务，redis会有一个队列放需要执行的命令，EXEC提交事务之后才会去执行

- 执行lua脚本，来执行多条redis命令。不具有原子性。

  - eval 命令来执行脚本
  - evalsha执行：1.script load ‘lua脚本’ //会生成一个sha1的哈希值。2.evalsha 哈希值 num key
  - ![image-20250621204155544](/Users/shilinling/Library/Application Support/typora-user-images/image-20250621204155544.png)

  
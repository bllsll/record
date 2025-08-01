Redis 的 GEO 功能基于 **Sorted Set（有序集合）** 实现，所有 GEO 命令本质上是对有序集合的操作封装。以下是 GEO 核心命令的功能及时间复杂度详解：

### 一、GEO 命令及时间复杂度

#### 1. `GEOADD`：添加地理位置

- **功能**：将经纬度坐标与名称关联，存储到指定的 GEO 集合中（底层是 Sorted Set 的 `ZADD` 操作）。
- **语法**：`GEOADD key longitude latitude member [longitude latitude member ...]`
- **时间复杂度**：`O(N log M)`，其中 `N` 是添加的成员数量，`M` 是集合中已有的成员总数。
  （与 `ZADD` 一致，因需插入有序集合并维护排序）。

#### 2. `GEOPOS`：获取地理位置的经纬度

- **功能**：返回指定成员的经纬度坐标（从 Sorted Set 的 `score` 中解码）。
- **语法**：`GEOPOS key member [member ...]`
- **时间复杂度**：`O(N)`，`N` 是查询的成员数量（底层是 `ZSCORE` 操作的封装，单个成员查询为 `O(1)`）。

#### 3. `GEODIST`：计算两个地理位置的距离

- **功能**：计算两个成员之间的直线距离（基于经纬度的球面距离公式）。
- **语法**：`GEODIST key member1 member2 [unit]`
- **时间复杂度**：`O(1)`（先通过 `GEOPOS` 获取两个成员的经纬度，再计算距离，均为常数级操作）。

#### 4. `GEORADIUS` / `GEORADIUSBYMEMBER`：范围查询（核心命令）

- 功能

  ：

  - `GEORADIUS`：以指定经纬度为中心，查询指定范围内的成员。
  - `GEORADIUSBYMEMBER`：以集合中已有成员为中心，查询指定范围内的成员。

- 语法

  ：

  bash

  

  

  

  

  

  ```bash
  GEORADIUS key longitude latitude radius unit [options]
  GEORADIUSBYMEMBER key member radius unit [options]
  ```

- 时间复杂度

  ：

  ```
  O(log M + K)
  ```

  ，其中

   

  ```
  M
  ```

   

  是集合总成员数，

  ```
  K
  ```

   

  是返回的成员数量。

  - 底层通过 Sorted Set 的范围查询（`ZRANGEBYSCORE`）实现：先将地理范围转换为 GeoHash 对应的 `score` 区间，再查询该区间内的成员，因此复杂度与有序集合的范围查询一致。

#### 5. `GEOHASH`：获取 GeoHash 编码

- **功能**：返回成员经纬度对应的 GeoHash 字符串（用于判断地理位置邻近性）。
- **语法**：`GEOHASH key member [member ...]`
- **时间复杂度**：`O(N)`，`N` 是查询的成员数量（本质是对 `GEOPOS` 结果的编码转换，单个成员为 `O(1)`）。

#### 6. 删除地理位置：`ZREM`

- **功能**：GEO 无专属删除命令，需用 Sorted Set 的 `ZREM` 移除成员。
- **语法**：`ZREM key member [member ...]`
- **时间复杂度**：`O(N log M)`，`N` 是删除的成员数量，`M` 是集合总成员数（与有序集合删除元素的复杂度一致）。

### 二、时间复杂度总结表

| 命令                | 功能              | 时间复杂度     | 核心逻辑                       |
| ------------------- | ----------------- | -------------- | ------------------------------ |
| `GEOADD`            | 添加地理位置      | `O(N log M)`   | 底层调用 `ZADD`，插入有序集合  |
| `GEOPOS`            | 获取经纬度        | `O(N)`         | 调用 `ZSCORE` 并解码经纬度     |
| `GEODIST`           | 计算距离          | `O(1)`         | 基于经纬度的数学计算           |
| `GEORADIUS`         | 按坐标范围查询    | `O(log M + K)` | 转换为 `ZRANGEBYSCORE` 操作    |
| `GEORADIUSBYMEMBER` | 按成员范围查询    | `O(log M + K)` | 先获取中心坐标，再执行范围查询 |
| `GEOHASH`           | 获取 GeoHash 编码 | `O(N)`         | 对经纬度进行 GeoHash 编码      |
| `ZREM`（删除）      | 删除地理位置      | `O(N log M)`   | 调用有序集合的删除操作         |

### 三、性能注意事项

1. **范围查询的 `K` 值影响**：`GEORADIUS` 等命令的 `O(log M + K)` 中，`K` 是返回的成员数量。若查询范围过大（如返回 10 万条结果），`K` 会成为性能瓶颈，建议用 `COUNT` 限制返回数量（如 `COUNT 100`）。
2. **数据量上限**：单 GEO 集合（Sorted Set）的成员数建议控制在百万级以内。超大规模场景（如千万级以上）需分库分表（如按城市、区域拆分集合），避免 `log M` 增长导致性能下降。
3. **精度与性能平衡**：GEO 基于 GeoHash 编码，精度适中（满足一般 LBS 场景），但无需过度追求高精度（如厘米级），避免计算成本增加。

### 总结

Redis GEO 命令的时间复杂度继承了 Sorted Set 的特性，核心操作（添加、删除、范围查询）的复杂度与数据量呈对数关系，性能优异。其中范围查询（`GEORADIUS` 等）是最常用的命令，需注意控制返回结果数量以优化性能，适合中小规模的 LBS 场景（如附近的人、商家定位）。
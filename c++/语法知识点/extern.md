`extern` 的核心作用是**声明 “变量 / 函数已在其他编译单元定义”**，实现跨文件访问；额外支持 `extern "C"` 实现 C++ 兼容 C 代码。

**核心规则**：`extern` 仅做「声明」，不做「定义」（定义必须在某个编译单元中唯一存在）



#### 场景 1：`extern` 声明全局变量（跨文件访问）

```cpp
// extern声明：告诉编译器“这些变量已在其他文件定义”
extern int g_global_num;
extern double g_global_pi;
```



#### 场景 2：`extern` 声明全局函数（跨文件访问）

```cpp
// extern声明函数（可省略extern，效果一致）
extern void print_num(int num);
```



#### 场景 3：`extern "C"`（C++ 调用 C 代码 / 让 C++ 函数被 C 调用）

##### 示例 1：C++ 调用 C 函数

```cpp
// extern "C" 声明：告诉编译器按C风格解析该函数
extern "C" {
    void c_print(const char* msg);
}
```

##### 示例 2：C++ 函数被 C 调用（需用 `extern "C"` 定义）

```cpp
// extern "C" 定义：禁用名字修饰，让C代码可调用
extern "C" void cpp_func(int num) {
    cout << "C++函数（C可调用）：" << num << endl;
}
```



#### 场景 4：`extern` 与 `const` 结合（跨文件访问 const 变量）

`const` 变量默认是「文件内静态」（等价于 `static const`），需加 `extern` 才能跨文件访问：

```cpp
// file1.cpp（定义extern const变量）
#include <iostream>
using namespace std;

// 定义：extern + const 让变量具有extern属性
extern const int MAX_NUM = 1000;
```

```cpp
// file2.cpp（访问extern const变量）
#include <iostream>
using namespace std;

// 声明：extern + const
extern const int MAX_NUM;

int main() {
    cout << MAX_NUM << endl; // 输出 1000
    // MAX_NUM = 2000; // 错误：const变量不可修改
    return 0;
}
```

### 三、`extern` 常见误区

1. 误区 1extern

   可以定义变量 → 错误！extern

   仅声明，定义必须去掉extern

   （除非加初始化）：

   ```cpp
   extern int a = 10; // 本质是“定义”（初始化触发定义），非纯声明
   extern int a;       // 纯声明（无初始化）
   ```

   

2. **误区 2**：函数必须加 `extern` 才能跨文件调用 → 错误！函数默认是 `extern` 属性，显式加 `extern` 仅为可读性；

3. **误区 3**：`extern "C"` 可修饰类 → 错误！`extern "C"` 仅支持函数 / 全局变量，不支持类、模板等 C++ 特有特性；

4. **误区 4**：匿名命名空间的内容可通过 `extern` 访问 → 错误！匿名命名空间的作用域仅限当前编译单元，`extern` 无法穿透。

### 总结

- 全局 `static` 的替代方案：C++ 优先用「匿名命名空间」，语义更清晰、支持特性更多；
- `extern` 核心用途：跨文件声明变量 / 函数、C/C++ 代码兼容；
- 关键原则：「定义唯一，声明可多次」（一个变量 / 函数只能在一个编译单元定义，可在多个编译单元声明）。


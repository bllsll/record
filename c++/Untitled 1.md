友元

在 C++ 中，`friend class CPollerObject;` 是一个**友元类声明**，表示 `CPollerObject` 类被声明为当前类（此处是 `CPollerUnit` 类）的 “友元”。这意味着 `CPollerObject` 类可以直接访问 `CPollerUnit` 类的**所有成员（包括私有成员和保护成员）**，突破了类的封装性限制。



代码中，`CPollerUnit` 类内部声明 `friend class CPollerObject;`，主要是为了让 `CPollerObject` 能够直接操作 `CPollerUnit` 的私有成员，以实现两者之间的紧密协作。

- **友元关系是单向的**：`CPollerObject` 能访问 `CPollerUnit` 的私有成员，但 `CPollerUnit` 不能自动访问 `CPollerObject` 的私有成员（除非 `CPollerObject` 也声明 `CPollerUnit` 为友元）。
- **友元关系不传递**：如果 `ClassA` 是 `ClassB` 的友元，`ClassB` 是 `ClassC` 的友元，`ClassA` 不会自动成为 `ClassC` 的友元。
- **谨慎使用**：友元会破坏类的封装性，可能导致代码耦合度升高。只有在两个类确实需要紧密协作（如框架中的核心组件）时才建议使用。
- ![image-20251125152715732](/Users/shilinling/Library/Application Support/typora-user-images/image-20251125152715732.png)
# ThreadPoolProject

# 🚀 C++ ThreadPool (From Scratch)

## 📌 项目简介

本项目基于 **C++11 标准** 从零实现一个通用线程池框架，支持任务调度与结果获取机制，并在实现过程中手动构建多个核心基础组件（如 `Any`、`Semaphore`），用于深入理解 C++ 并发模型与现代 C++ 编程范式。

项目重点不在功能堆叠，而在于：

* 并发原语的正确使用
* 类型擦除机制的实现
* RAII 思想在资源管理中的应用

---

## ✨ 技术亮点

* 🔹 基于 `std::thread` 实现线程池，支持线程复用与任务调度
* 🔹 使用 `std::mutex + std::condition_variable` 实现线程安全任务队列（生产者-消费者模型）
* 🔹 手动实现 **类型擦除容器 `Any`**（对标 C++17 `std::any`）
* 🔹 手动实现 **信号量 `Semaphore`**（基于 mutex + condition_variable）
* 🔹 支持任务返回值机制（类似 `future`），通过 `Result` 类获取执行结果
* 🔹 使用 **RAII** 管理线程、锁及资源生命周期
* 🔹 使用 **移动语义 + 完美转发** 优化任务提交路径
* 🔹 使用 **SFINAE** 解决模板构造函数劫持问题（`Any` 实现关键点）
* 🔹 处理线程同步细节问题（虚假唤醒 / 惊群效应 / 竞态条件）

---

## 🧠 核心架构

```
ThreadPool
 ├── Task              // 抽象任务类（多态执行）
 ├── Result            // 任务执行结果（支持 get）
 ├── SafeQueue         // 线程安全任务队列
 ├── Worker Threads    // 工作线程
 ├── Semaphore         // 自实现信号量
 └── Any               // 自实现通用返回值容器
```

---

## ⚙️ 核心模块说明

### 1️⃣ ThreadPool

* 管理线程生命周期
* 提供任务提交接口
* 控制任务队列容量
* 实现线程调度与资源回收

---

### 2️⃣ Task & Result

```cpp
class Task {
public:
    virtual Any run() = 0;
};
```

* `Task`：抽象任务接口，支持多态执行
* `Result`：封装任务执行结果，支持同步获取

---

### 3️⃣ Any（自实现）

实现思路：

* 基类指针 + 模板派生类
* 类型擦除（Type Erasure）
* 使用 `dynamic_cast` 做运行时类型识别

```cpp
Any a = 10;
int value = a.Any_cast<int>();
```

---

### 4️⃣ Semaphore（自实现）

```cpp
void wait();
void post();
```

实现机制：

* `mutex` 保证互斥访问
* `condition_variable` 实现线程阻塞与唤醒
* 避免竞态条件

---

### 5️⃣ 任务队列

* 基于 `std::queue`
* 使用 `mutex` 保护
* 使用 `condition_variable` 控制：

  * 队列非空（notEmpty）
  * 队列未满（notFull）

---

## 🚀 使用示例

```cpp
#include <iostream>
#include <syncstream>
#include "ThreadPool.h"
class MyTask : public Task {
public:
	MyTask(int begin, int end) :m_begin(begin), m_end(end) {}
	Any run() override {
		long long sum = 0;
		for (int i = m_begin; i <= m_end; ++i) {
			sum += i;
		}
		return sum;
	}
private:
	int m_begin;
	int m_end;
};
int main()
{
	ThreadPool poll;
	// 用户自己设置线程池的工作模式
	poll.setMode(PoolMode::MODE_CACHED);
	// 开始启动线程池，用户自己设置初始线程数量
	poll.start(4);
	Result res1 = poll.submitTask(std::make_shared<MyTask>(1, 1000000));
	Result res2 = poll.submitTask(std::make_shared<MyTask>(1000000 + 1, 2000000));
	try {
		long long sum1 = res1.get().Any_cast<long long>();
		long long sum2 = res2.get().Any_cast<long long>();
		long long allSum = sum1 + sum2;
		std::osyncstream(std::cout) << "allSum: " << allSum << std::endl;
	}
	catch (const std::exception& e) {
		std::osyncstream(std::cout) << "Exception: " << e.what() << std::endl;
	}
	catch (...) {
		std::osyncstream(std::cout) << "Unknown exception occurred." << std::endl;
	}
	std::osyncstream(std::cout) << "All tasks completed." << std::endl;
	return 0;
}
```

---

## ⚠️ 关键技术点解析

### ✔ condition_variable 使用

* `wait(lock, predicate)`：当 predicate 为 **false 时阻塞**
* 解决虚假唤醒问题

---

### ✔ notify_all 行为

* 唤醒所有等待线程
* 但只有一个线程能获取 mutex 并执行任务
* 其余线程重新阻塞（避免错误执行）

---

### ✔ RAII 思想

* 锁：`std::unique_lock`
* 内存：`unique_ptr / shared_ptr`
* 保证异常安全

---

### ✔ SFINAE 应用

用于解决 `Any` 中：

* 模板构造函数误匹配拷贝/移动构造函数问题

---

## 📊 项目意义

本项目的核心价值在于：

* 深入理解 C++ 并发模型（线程、锁、条件变量）
* 掌握类型擦除（Type Erasure）实现方式
* 熟悉现代 C++（C++11）关键特性：

  * 移动语义
  * 完美转发
  * 智能指针
* 提升对系统级组件设计的理解能力

---

## 📦 编译环境

* 编译器：支持 C++11 及以上（GCC / Clang / MSVC）
* 操作系统：Linux / Windows（已在 Ubuntu / Win11 测试）

```bash
g++ -std=c++11 -pthread main.cpp -o threadpool
```

---

## 🔧 后续优化方向

* [ ] 动态线程池（自动扩容/缩容）
* [ ] work stealing（任务窃取）
* [ ] 无锁队列（lock-free queue）
* [ ] 更完善的任务拒绝策略
* [ ] 支持优先级任务调度

---

## 📎 项目说明

> 本项目未使用 C++17 `std::any` / `std::semaphore`，
> 所有核心组件均基于 C++11 手动实现，用于加深底层理解。

---

## 👤 作者

* C++ 并发编程学习者
* 专注底层实现与高性能系统设计

---

## ⭐ 如果你觉得这个项目有帮助

欢迎点一个 Star，这是对该项目最大的认可。




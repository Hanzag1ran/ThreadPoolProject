////// ThreadPool.h
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <functional>
#include <condition_variable>

const int DEFAULT_THREAD_SIZE = 4;
const int DEFAULT_THREAD_SIZE_MAX_THRESHOLD = 32;
const int DEFAULT_TASK_QUE_MAX_THRESHOLD = 1024;

class MoveOnly
{
protected:
	MoveOnly() = default;
	~MoveOnly() = default;

public:
	MoveOnly(const MoveOnly&) = delete;
	MoveOnly& operator=(const MoveOnly&) = delete;

	MoveOnly(MoveOnly&&) = default;
	MoveOnly& operator=(MoveOnly&&) = default;
};


// Any类型：可以接收任意数据的类型
class Any : public MoveOnly {
public:
	Any() = default;
	// 构造函数是模板函数，通过隐式类型转换，可以传入任意数据类型
	template<typename T,	// SFINAE：禁止当 T 的 decayed 类型为 Any 时使用此模板构造函数，而是适配 Any 的移动构造
		typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Any>>>	// ⭐⭐⭐防止模板构造函数劫持移动构造函数
	Any(T&& data)	// 构造函数最好使用完美转发
		:m_base(std::make_unique<Derived<std::decay_t<T>>>(std::forward<T>(data))) {
	}
	~Any() = default;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;
	// 这个函数能把Any对象里面存储的data数据提取出来
	template<typename T>
	T& Any_cast() {	// 返回引用
		if (!m_base)	throw std::runtime_error("empty_Any");
		// 基类指针 ==> 派生类指针 RTTI-运行时类型识别-dynamic_cast
		using U = std::decay_t<T>;
		auto derived = dynamic_cast<Derived<U>*>(m_base.get());
		if (!derived)	throw "bad_Any_cast!";
		return derived->m_data;
	}
	template<typename T>
	const T& Any_cast() const {	// 获取数据 const版本
		if (!m_base)	throw std::runtime_error("empty_Any");
		using U = std::decay_t<T>;
		const auto derived = dynamic_cast<const Derived<U>*>(m_base.get());
		if (!derived)	throw std::runtime_error("bad_Any_cast");
		return derived->m_data;
	}
private:
	// 基类类型，用基类类型的指针指向派生类对象
	struct Base {
		virtual ~Base() = default;
	};
	// 派生类类型，模板类，封装数据
	template<typename T>	// T = 存储类型
	struct Derived : Base {
		template<typename U>	// U = 构造参数类型
		Derived(U&& data)	// ⭐⭐⭐只有在“模板参数推导发生时”，U&& 才是转发引用
			: m_data(std::forward<U>(data)) {	// 完美转发 = 转发引用 + std::forward
		}
		T m_data;
	};
	// 定义一个基类的指针
	std::unique_ptr<Base> m_base;
};

// 实现一个信号量类
class Semaphore {
public:
	explicit Semaphore(int limit = 0);
	~Semaphore() = default;
	// 获取一个信号量资源
	void wait();
	// 释放一个信号量资源
	void post();
private:
	std::mutex m_mtx;	// 实现对m_resLimit的互斥访问
	std::condition_variable m_cond;	// 条件变量
	// std::atomic_int m_resLimit;	// 资源计数，因为有mutex其实不需要atomic
	int m_resLimit;	// 资源计数-状态变量
};
class Result;	// 前置声明，Result类型在Task类中被使用了，Result类的定义在Task类的后面
// 任务抽象基类，
class Task {
public:
	Task();
	virtual ~Task() = default;
	void exec();
	void setResult(Result* result);
	// 用户可以自定义任意任务类型，从Task继承而来，重写run方法
	virtual Any run() = 0;
private:
	// Result的生命周期 > Task的生命周期
	Result* m_result = nullptr;	// 任务执行完的返回值 Result类型
};

// 实现接收提交到线程池的task任务执行完成后的返回值类型 Result
class Result : public MoveOnly {
public:
	// Result() = default;
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;
	// 问题一：setVal方法，获取任务执行完的返回值
	template<typename T>
	void setVal(T&& any)
	{
		// m_any = Any(std::forward<T>(any));
		m_any = std::forward<T>(any);
		m_sem.post();
	}

	// 问题二：get方法，用户调用这个方法获取task的返回值
	Any get();
	bool isValid() const;

private:
	Any m_any;	// 任务返回值 Any类型
	Semaphore m_sem;	// 线程通信信号量
	std::shared_ptr<Task> m_task;	// 指向对应获取返回值的任务对象
	std::atomic_bool m_isValid;	// 返回值是否有效
};


// 线程池支持的模式
enum class PoolMode
{
	MODE_FIXED,	// 固定线程数
	MODE_CACHED	// 线程数量可动态增长
};

// 线程类型
class Thread : public MoveOnly {
public:
	// 线程函数对象类型
	using ThreadFunc = std::function<void()>;
	// 线程构造函数
	Thread(ThreadFunc func);
	// 线程析构函数
	~Thread();
	// Any run();
	void stop();
	// 启动线程
	void start();
private:
	ThreadFunc m_func;	// 线程函数
	std::thread m_thread;
};

/**
example:
class MyTask : public Task {
public:
	void run() override {
		// do something
	}
};
ThreadPool poll;
poll.start(4);
poll.submitTask(std::make_shared<MyTask>());
*/

// 线程池类型
class ThreadPool : public std::enable_shared_from_this<ThreadPool> {
public:
	// 线程池构造函数
	ThreadPool();
	// 线程池析构函数
	~ThreadPool();

	// 设置线程池的工作模式-只能在start之前设置
	void setMode(PoolMode mode);

	// 设置初始的线程数量-只能在start之前设置
	void setInitThreadSize(std::size_t size);

	// 设置task任务队列的上限阈值-只能在start之前设置
	void setTaskQueMaxThreshold(std::size_t threshold);

	// 设置线程数量的上限阈值-只能在start之前设置
	void setThreadSizeMaxThreshold(std::size_t threshold);

	// 给线程池提交任务
	Result submitTask(std::shared_ptr<Task> sp);

	// 启动线程池
	void start(std::size_t initThreadSize = DEFAULT_THREAD_SIZE);
	void stop();

	// 禁止拷贝构造
	ThreadPool(const ThreadPool&) = delete;
	// 禁止拷贝赋值运算符
	ThreadPool& operator=(const ThreadPool&) = delete;

	// 定义线程函数
	void threadFunc();

private:
	bool isStarted() const { return m_isStart; }
	bool isExit() const { return m_isExit; }
	bool isFull() const { return m_taskSize >= m_taskQueMaxThreshold; }
	std::size_t m_initThreadSize;	// 初始的线程数量
	std::atomic_uint m_idleThreadSize;	// 空闲线程的数量
	std::atomic_uint m_curThreadSize;	// 记录当前线程池当中线程的总数量
	std::size_t m_threadSizeMaxThreshold;	// 线程数量的上限阈值

	std::queue<std::shared_ptr<Task>> m_taskQue;		// 任务队列
	std::atomic_uint m_taskSize;	// 任务的数量
	std::size_t m_taskQueMaxThreshold;		// 任务队列的上限阈值

	std::mutex m_taskQueMtx;	// 保证任务队列的线程安全
	std::condition_variable m_notFull;	// 任务队列非满条件变量
	std::condition_variable m_notEmpty;	// 任务队列非空条件变量

	PoolMode m_poolMode;	// 当前线程池的工作模式
	std::atomic_bool m_isStart;	// 线程池是否启动
	std::atomic_bool m_isExit;	// 线程池是否退出
	std::vector<std::unique_ptr<Thread>> m_threads;	// 线程列表-不需要线程安全
};
#endif
















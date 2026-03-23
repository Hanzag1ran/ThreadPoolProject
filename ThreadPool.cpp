// ThreadPool.cpp

#include "ThreadPool.h"
#include <thread>
#include <iostream>
#include <syncstream>


Semaphore::Semaphore(int limit)
	: m_resLimit(limit)
{
}
// 获取一个信号量资源
void Semaphore::wait() {
	std::unique_lock<std::mutex> lock(m_mtx);
	// 如果m_resLimit==0，则阻塞当前线程等待notify
	m_cond.wait(lock, [this] { return m_resLimit > 0; });
	// notify之前m_resLimit一定被++
	--m_resLimit;
}
// 释放一个信号量资源
void Semaphore::post() {
	{
		std::unique_lock<std::mutex> lock(m_mtx);
		++m_resLimit;
	}
	m_cond.notify_one();
}


// ⭐⭐先创建Task再创建Result，Result的构造函数当中将Task与Result绑定起来
// Result的构造时机是在ThreadPool::submitTask返回时
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: m_task(std::move(task))
	, m_isValid(isValid)
	, m_sem(0)

{
	m_task->setResult(this);	// 把Result对象和Task绑定
}

Any Result::get() {	// 用户调用
	if (!m_isValid) {
		return Any();
	}
	m_sem.wait();	// task任务如果没有执行完，这里就会阻塞用户的线程
	// return m_any;	// Any是move-only类型，不能被拷贝
	return std::move(m_any);
}
bool Result::isValid() const { return m_isValid; }

Task::Task()
	:m_result(nullptr) {
}

void Task::exec() {
	// run();	// 这里发生多态调用
	if (m_result != nullptr) {
		// m_result->setVal(std::move(run()));	// run的调用在线程池的线程函数当中，执行完run之后把返回值通过setVal存储到Result对象当中
		m_result->setVal(run());
	}
}
void Task::setResult(Result* result) {
	m_result = result;
}

///////////////////////// 线程池方法实现

// 线程池构造函数
ThreadPool::ThreadPool()
	: m_initThreadSize(DEFAULT_THREAD_SIZE)
	, m_curThreadSize(0)
	, m_idleThreadSize(0)
	, m_threadSizeMaxThreshold(DEFAULT_THREAD_SIZE_MAX_THRESHOLD)
	, m_taskSize(0)
	, m_taskQueMaxThreshold(DEFAULT_TASK_QUE_MAX_THRESHOLD)
	, m_poolMode(PoolMode::MODE_FIXED)
	, m_isStart(false)
	, m_isExit(false) {
}
// 线程池析构函数
ThreadPool::~ThreadPool() {
	stop();
}

// 设置线程池的工作模式-只能在start之前设置
void ThreadPool::setMode(PoolMode mode) {
	if (isStarted()) {
		std::osyncstream(std::cerr) << "thread pool has already started, can't change mode.\n";
		return;	// 线程池已经启动了，不能修改工作模式了
	}
	m_poolMode = mode;
}

// 设置初始的线程数量-只能在start之前设置
void ThreadPool::setInitThreadSize(std::size_t size) {
	if (isStarted()) {
		std::osyncstream(std::cerr) << "thread pool has already started, can't change init_thread_size.\n";
		return;	// 线程池已经启动了，不能修改初始线程数量了
	}
	m_initThreadSize = size;
}

// 设置task任务队列的上限阈值-只能在start之前设置
void ThreadPool::setTaskQueMaxThreshold(std::size_t threshold) {
	if (isStarted()) {
		std::osyncstream(std::cerr) << "thread pool has already started, can't change task_queue_max_threshold.\n";
		return;	// 线程池已经启动了，不能修改任务队列上限阈值了
	}
	m_taskQueMaxThreshold = threshold;
}

// 设置线程数量的上限阈值-只能在start之前设置
void ThreadPool::setThreadSizeMaxThreshold(std::size_t threshold) {
	if (isStarted()) {
		std::osyncstream(std::cerr) << "thread pool has already started, can't change thread_size_max_threshold.\n";
		return;	// 线程池已经启动了，不能修改线程数量上限阈值了
	}
	if (m_poolMode != PoolMode::MODE_CACHED) {
		std::osyncstream(std::cerr) << "only cached mode allows setting thread_size_max_threshold.\n";
		return;	// 只有cached模式才需要设置线程数量上限阈值，fixed模式不需要设置线程数量上限阈值
	}
	m_threadSizeMaxThreshold = threshold;
}


// 给线程池提交任务	用户调用该接口，传入任务对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
	// 获取锁
	std::unique_lock<std::mutex> lock(m_taskQueMtx);
	// 线程的通信	等待任务队列有空余位
	// while (m_taskQue.size() >= m_taskQueMaxThreshold) { m_notFull.wait(lock); }
	// lambda 返回 true  →  wait 结束
	bool ret = m_notFull.wait_for(lock,
		std::chrono::seconds(1),
		[this]()->bool { return m_taskQue.size() < m_taskQueMaxThreshold; });
	if (!ret) {
		// 等待了1s，但是任务队列仍然没有空间，提交任务失败
		std::osyncstream(std::cerr) << "task queue is full, submit task failed." << std::endl;
		return Result(sp, false);	// 返回错误
	}
	// 如果有空余，把任务放入任务队列中
	m_taskQue.emplace(sp);
	// m_taskQue.push(sp);
	++m_taskSize;
	// 因为放了新任务，任务队列肯定不空了，通知m_notEmpty信号
	m_notEmpty.notify_one();

	// cached模式：【小而快的任务】需要根据任务数量和线程数量的关系，动态增加/减少线程数量
	if(m_poolMode == PoolMode::MODE_CACHED
		&& m_taskSize > m_idleThreadSize
		&& m_curThreadSize < m_threadSizeMaxThreshold)
	{
		// 需要增加线程了
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
		m_threads.emplace_back(std::move(ptr));
		m_threads.back()->start();
		++m_idleThreadSize;
		++m_curThreadSize;
	}




	// 返回任务的Result对象
	// 写法一：Result被封装在task当中
	// return task->getResult();	// ⭐⭐线程执行完task，task就被析构掉了，那么Result也就没了

	// 写法二：task被封装在Result中
	// return Result(task);
	return Result(sp, true);
}

// 启动线程池
void ThreadPool::start(std::size_t initThreadSize) {
	m_isStart = true;
	// 记录初始线程个数
	m_initThreadSize = initThreadSize;
	m_curThreadSize = initThreadSize;
	// 创建线程对象
	for (std::size_t i = 0; i < initThreadSize; ++i) {
		// 创建Thread线程对象的时候，把线程函数给到Thread线程对象
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
		m_threads.emplace_back(std::move(ptr));
	}
	// 启动所有线程
	for (std::size_t i = 0; i < initThreadSize; ++i) {
		m_threads[i]->start();	// 需要执行一个线程函数
		++m_idleThreadSize;	// 线程池当中空闲线程数量++
	}
}

// 停止线程池
void ThreadPool::stop() {
	// 1. 停止接受新任务
	m_isExit = true;
	// 2. 通知所有线程，任务队列有任务
	m_notEmpty.notify_all();
	// 3. 等待所有线程结束
	for (auto& th : m_threads) {
		th->stop();
	}
}

// 定义线程函数	线程池的所有线程从任务队列里面消费任务
void ThreadPool::threadFunc() {
	std::osyncstream(std::cout) << "begin threadFunc tid:" << std::this_thread::get_id() << std::endl;
	while (true) {	// shutdown 时仍然会把剩余任务执行完
		std::shared_ptr<Task> task = nullptr;
		{
			// 先获取锁
			std::unique_lock<std::mutex> lock(m_taskQueMtx);
			// 等待notEmpty条件
			m_notEmpty.wait(lock, [this]()->bool {
				return m_taskSize > 0 || m_isExit;
				});
			--m_idleThreadSize;	// 线程池当中空闲线程数量++
			if (m_isExit && m_taskSize == 0)	return;

			// 从任务队列当中取一个任务出来
			task = m_taskQue.front();
			m_taskQue.pop();
			--m_taskSize;
			// m_notFull.notify_all();	// 是否导致惊群？
			m_notFull.notify_one();
		}	// 锁自动释放
		// 当前线程负责执行这个任务，但是执行任务时先提前释放锁
		if (task != nullptr) {
			// task->run();	// 多态，执行任务
			task->exec();
		}
		++m_idleThreadSize;	// 线程池当中空闲线程数量++
	}	// end-while
	std::osyncstream(std::cout) << "end threadFunc tid:" << std::this_thread::get_id() << std::endl;
}

///////////////////////// 线程方法实现

// 线程构造函数
Thread::Thread(ThreadFunc func)
	: m_func(func)
{
}
// 线程析构函数
Thread::~Thread() {
	std::osyncstream(std::cout) << "Thread::~Thread()" << std::endl;
	if (m_thread.joinable()) {
		m_thread.join();
	}
}

// 启动线程
void Thread::start() {
	// 创建一个线程来执行线程函数
	m_thread = std::thread(m_func);	// 
	// m_thread.detach();
}

// 停止线程
void Thread::stop() {
	if (m_thread.joinable()) {
		m_thread.join();
	}
}
// ThreadPoolProject.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。

#include <iostream>
#include <syncstream>
#include "ThreadPool.h"

class MyTask : public Task {
public:
	MyTask(int begin, int end) :m_begin(begin), m_end(end) {}
	Any run() override {
		std::osyncstream(std::cout)
			<< "thread id: " << std::this_thread::get_id()
			<< " begin: " << m_begin
			<< " end: " << m_end << std::endl;
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
	poll.setMode(PollMode::MODE_CACHED);

	// 开始启动线程池，用户自己设置初始线程数量
	poll.start(4);


	Result res1 = poll.submitTask(std::make_shared<MyTask>(1, 1000000));
	Result res2 = poll.submitTask(std::make_shared<MyTask>(1000000 + 1, 2000000));
	Result res3 = poll.submitTask(std::make_shared<MyTask>(2000000 + 1, 3000000));
	Result res4 = poll.submitTask(std::make_shared<MyTask>(3000000 + 1, 4000000));
	Result res5 = poll.submitTask(std::make_shared<MyTask>(4000000 + 1, 5000000));
	Result res6 = poll.submitTask(std::make_shared<MyTask>(5000000 + 1, 6000000));
	Result res7 = poll.submitTask(std::make_shared<MyTask>(6000000 + 1, 7000000));
	Result res8 = poll.submitTask(std::make_shared<MyTask>(7000000 + 1, 8000000));

	try {
		long long sum1 = res1.get().Any_cast<long long>();
		long long sum2 = res2.get().Any_cast<long long>();
		long long sum3 = res3.get().Any_cast<long long>();
		long long sum4 = res4.get().Any_cast<long long>();
		long long sum5 = res5.get().Any_cast<long long>();
		long long sum6 = res6.get().Any_cast<long long>();
		long long sum7 = res7.get().Any_cast<long long>();
		long long sum8 = res8.get().Any_cast<long long>();
		long long allSum = sum1 + sum2 + sum3 + sum4 + sum5 + sum6 + sum7 + sum8;
		std::osyncstream(std::cout) << "allSum: " << allSum << std::endl;
	}
	catch (const std::exception& e) {
		std::osyncstream(std::cout) << "Exception: " << e.what() << std::endl;
	}
	catch (...) {
		std::osyncstream(std::cout) << "Unknown exception occurred." << std::endl;
	}
	long long expectedSum = 8000000LL * (8000000LL + 1) / 2; // 计算1到8000000的和
	std::osyncstream(std::cout) << "Expected sum: " << expectedSum << std::endl;
	std::osyncstream(std::cout) << "All tasks completed." << std::endl;
	return 0;
}


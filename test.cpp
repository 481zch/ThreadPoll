#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <thread>
#include <boost/asio.hpp>
#include "threadPool.h"

constexpr int num_tasks = 10000; 

// 简单的计算任务
int compute_task(int n) {
    int sum = 0;
    int j=1;
    for (int i = 0; i < 1000000; ++i) {
        sum *= (i + n);
        sum+=j;
        j++;
    }
    return sum;
}

// 测试自定义线程池
void test_custom_thread_pool() {
    ThreadPool pool(4);  // 初始化时有4个线程
    pool.init();

    std::vector<std::future<int>> futures;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_tasks; ++i) {
        futures.emplace_back(pool.submit(compute_task, i));
    }

    int total_sum = 0;
    for (auto& future : futures) {
        total_sum += future.get(); // 等待所有任务完成
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Custom ThreadPool execution time: " << duration.count() << " milliseconds" << std::endl;
    std::cout << "Total sum (Custom ThreadPool): " << total_sum << std::endl;

    pool.shutdown();
}

// 测试 Boost.Asio 线程池
void test_boost_asio_thread_pool() {
    boost::asio::thread_pool pool(4);  // 4个线程

    std::vector<std::future<int>> futures;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_tasks; ++i) {
        futures.emplace_back(std::async(std::launch::async, [&pool, i]() {
            boost::asio::post(pool, []{});
            return compute_task(i);
        }));
    }

    int total_sum = 0;
    for (auto& future : futures) {
        total_sum += future.get(); // 等待所有任务完成
    }

    pool.join();
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Boost.Asio ThreadPool execution time: " << duration.count() << " milliseconds" << std::endl;
    std::cout << "Total sum (Boost.Asio ThreadPool): " << total_sum << std::endl;
}

int main() {
    std::cout << "Testing Custom ThreadPool Performance...\n";
    test_custom_thread_pool();

    std::cout << "\nTesting Boost.Asio ThreadPool Performance...\n";
    test_boost_asio_thread_pool();

    return 0;
}


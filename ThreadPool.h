#pragma once
#include <mutex>
#include <queue>
#include <functional>
#include <future>
#include <thread>
#include <utility>
#include <vector>
#include <condition_variable>
#include <initializer_list>

template <typename T>
class SafeQueue {
private:
    std::queue<T> m_queue;
    std::mutex m_mutex;

public:
    SafeQueue() {}
    SafeQueue(SafeQueue&& other) {
        std::lock_guard<std::mutex> lock(other.m_mutex);
        m_queue = std::move(other.m_queue);
    }
    ~SafeQueue() {}

    bool empty() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    int size() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    void enqueue(T&& t) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.emplace(std::forward<T>(t));
    }

    bool dequeue(T& t) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_queue.empty())
            return false;
        t = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }
};

class ThreadPool {
private:
    class ThreadWorker {
    private:
        int m_id;
        ThreadPool* m_pool;

    public:
        ThreadWorker(ThreadPool* pool, const int id) : m_pool(pool), m_id(id) {}

        void operator()() {
            std::function<void()> func;
            bool dequeued;
            while (!m_pool->m_shutdown) {
                {
                    std::unique_lock<std::mutex> lock(m_pool->m_conditional_mutex);
                    m_pool->m_conditional_lock.wait(lock, [this] { return !m_pool->m_queue.empty() || m_pool->m_shutdown; });
                    if (m_pool->m_shutdown) break;
                    dequeued = m_pool->m_queue.dequeue(func);
                }
                if (dequeued) func();
            }
        }
    };

    bool m_shutdown;
    SafeQueue<std::function<void()>> m_queue;
    std::vector<std::thread> m_threads;
    std::mutex m_conditional_mutex;
    std::condition_variable m_conditional_lock;

public:
    ThreadPool(const int n_threads = 4) : m_threads(std::vector<std::thread>(n_threads)), m_shutdown(false) {}

    ~ThreadPool() {
        if (!m_shutdown) shutdown();
    }

    void init() {
        for (int i = 0; i < m_threads.size(); ++i) {
            m_threads.at(i) = std::thread(ThreadWorker(this, i));
        }
    }

    void shutdown() {
        m_shutdown = true;
        m_conditional_lock.notify_all();
        
        for (auto& thread : m_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> 
    {
        std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);
        std::function<void()> wrapper_func = [task_ptr]() { (*task_ptr)(); };
        m_queue.enqueue(std::move(wrapper_func));
        m_conditional_lock.notify_one();
        return task_ptr->get_future();
    }
};

#pragma once
#include <list>
#include <mutex>
#include <queue>
#include <functional>
#include <future>
#include <thread>
#include <utility>
#include <vector>
#include <condition_variable>
#include <initializer_list>
#include <unordered_map>

#define MIN_THREADS 4
#define MAX_THREADS 40
#define DEFAULT_THREADS 4
#define DEFAULT_INTERVAL 1000

class Task {
public:
    std::function<void()> func;
    unsigned int priority;

public:
    Task() = default;
    Task(std::function<void()> f, unsigned int p) : func(std::move(f)), priority(p) {}

    bool operator<(const Task& other) const {
        return priority < other.priority;
    }
};

template <typename T>
class SafeQueue {
private:
    std::priority_queue<T, std::vector<T>, std::less<T>> m_queue;
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
        m_queue.push(std::forward<T>(t));
    }

    bool dequeue(T& t) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_queue.empty())
            return false;
        t = std::move(m_queue.top());
        m_queue.pop();
        return true;
    }
};

// 模式：在关闭线程池的时候，确保能够运行完所有的工作线程任务
class ThreadPool {
private:
    class ThreadWorker {
    private:
        ThreadPool* m_pool;

    public:
        ThreadWorker(ThreadPool* pool) : m_pool(pool) {}

        void operator()() {
            Task t;
            bool dequeued;
            while (true) {
                {
                    std::unique_lock<std::mutex> lock(m_pool->m_conditional_mutex);
                    m_pool->m_conditional_lock.wait(lock, [this] { return !m_pool->m_queue.empty() || m_pool->m_shutdown; });
                    if (m_pool->m_shutdown && m_pool->m_queue.empty()) break;
                    dequeued = m_pool->m_queue.dequeue(t);
                }
                if (dequeued)
                {
                    ++m_pool->work_nums;
                    --m_pool->sleep_nums;
                    t.func();
                    --m_pool->work_nums;
                    ++m_pool->sleep_nums;
                }
                // 在没有任务并且被通知了，说明要缩减线程
                else
                {
                    std::unique_lock<std::mutex> lock(m_pool->m_mutex);
                    std::thread::id this_id = std::this_thread::get_id();
                    if (m_pool->mp.count(this_id))
                    {
                        m_pool->lst_threads.erase(m_pool->mp[this_id]);
                        m_pool->mp.erase(this_id);
                        --m_pool->sleep_nums;
                    }
                    return;
                }
            }
        }
    };

    bool m_shutdown;
    SafeQueue<Task> m_queue;
    std::mutex m_conditional_mutex;
    std::mutex m_mutex;
    std::condition_variable m_conditional_lock;
    size_t max_threads;
    size_t min_threads;
    // 线程id和链表迭代器
    std::unordered_map<std::thread::id, std::list<std::thread>::iterator> mp;
    std::list<std::thread> lst_threads;
    std::atomic<int> work_nums;
    std::atomic<int> sleep_nums;
    // 引入定时器机制和定时器线程
    std::chrono::milliseconds timer_interval;
    std::thread timer_thread;

public:
    ThreadPool(const int n_threads = DEFAULT_THREADS, const size_t min_threads = MIN_THREADS, const size_t max_threads = MAX_THREADS, std::chrono::milliseconds interval = std::chrono::milliseconds(DEFAULT_INTERVAL)) : lst_threads(std::list<std::thread>(n_threads)), m_shutdown(false), min_threads(min_threads), max_threads(max_threads), timer_interval(interval)
    {
        work_nums = 0;
        sleep_nums = n_threads;
        timer_thread = std::thread(&ThreadPool::timer_function, this);
    }

    ~ThreadPool() {
        if (!m_shutdown) shutdown();
    }

    void init() {
        for (auto it = lst_threads.begin(); it != lst_threads.end(); ++it) {
            *it = std::thread(ThreadWorker(this));
            mp[it->get_id()] = it;
        }
    }

    void shutdown() 
    {
        {
            std::unique_lock<std::mutex> lock(m_conditional_mutex);
            m_shutdown = true;
        }

        m_conditional_lock.notify_all();
        for (auto& it : lst_threads) {
            if (it.joinable()) {
                it.join();
            }
        }
        if (timer_thread.joinable()) {
            timer_thread.join();
        }
    }

    template <typename F, typename... Args>
    auto submit(unsigned int priority, F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        auto func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);
        Task task([task_ptr]() { (*task_ptr)(); }, priority);
        m_queue.enqueue(std::move(task));
        m_conditional_lock.notify_one();
        return task_ptr->get_future();
    }

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        return submit(0, std::forward<F>(f), std::forward<Args>(args)...);
    }

private:
    void resizePool();
    void timer_function() {
        while (!m_shutdown) {
            std::this_thread::sleep_for(timer_interval);
            resizePool();
        }
    }
};

void ThreadPool::resizePool()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    size_t task_num = m_queue.size();
    size_t cur_threads = lst_threads.size();

    // 扩展线程池
    if (task_num > work_nums && cur_threads < max_threads)
    {
        size_t add_threads = std::min(max_threads - cur_threads, task_num - work_nums);
        for (size_t i = 0; i < add_threads; ++i)
        {
            auto it = lst_threads.emplace(lst_threads.end(), std::thread(ThreadWorker(this)));
            mp[it->get_id()] = it;
            ++sleep_nums;
        }
        return;
    }

    // 收缩线程池
    if (work_nums < sleep_nums && cur_threads > min_threads)
    {
        size_t remove_threads = cur_threads - std::max(2 * task_num, (size_t)min_threads);
        size_t temp = std::min((size_t)sleep_nums, cur_threads - min_threads);
        remove_threads = std::min(temp, remove_threads);

        for (size_t i = 0; i < remove_threads; ++i)
        {
            m_conditional_lock.notify_one();
        }
        return;
    }
}
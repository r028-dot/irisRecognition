#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <stdexcept>

// Fixed-size thread pool.
// Usage:  ThreadPool pool(8);
//         auto fut = pool.enqueue([]{ return 42; });
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Enqueue a callable and return a std::future for its result.
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

private:
    std::vector<std::thread>          m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex                        m_mutex;
    std::condition_variable           m_cv;
    bool                              m_stop = false;
};

// ── Template implementation ───────────────────────────────────────────────
template<typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using R = std::invoke_result_t<F, Args...>;
    auto task = std::make_shared<std::packaged_task<R()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    std::future<R> fut = task->get_future();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stop)
            throw std::runtime_error("ThreadPool: enqueue on stopped pool");
        m_tasks.emplace([task]{ (*task)(); });
    }
    m_cv.notify_one();
    return fut;
}

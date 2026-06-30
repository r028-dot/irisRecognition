#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <stdexcept>
using namespace std;
// מאגר תהליכונים (ThreadPool): מנהל קבוצה קבועה של תהליכוני עבודה ותור משימות לטיפול מקבילי בבקשות השרת.
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // מוסיף משימה לתור ומחזיר std::future לתוצאה שלה.
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

private:
    vector<thread> m_workers;
    queue<function<void()>> m_tasks;
    mutex m_mutex;
    condition_variable m_cv;
    bool m_stop = false;
};

template<typename F, typename... Args>
// מוסיף משימה לתור ומחזיר std::future לתוצאה שלה.
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

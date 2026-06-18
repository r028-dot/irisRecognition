#include "ThreadPool.h"
using namespace std;

//בנאי המאגר (Constructor): מייצר את תהליכוני העבודה ומושיב אותם בלולאה אינסופית בהמתנה למשימות חדשות בתור.
ThreadPool::ThreadPool(size_t numThreads)
{
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back([this] {
            for (;;) {
                function<void()> task;
                {
                    unique_lock<mutex> lock(m_mutex);
                    m_cv.wait(lock, [this]{
                        return m_stop || !m_tasks.empty();
                    });
                    if (m_stop && m_tasks.empty())
                        return;
                    task = std::move(m_tasks.front());
                    m_tasks.pop();
                }
                task();
            }
        });
    }
}
// מפרק המאגר (Destructor): מסמן לתהליכונים לעצור, מעיר את כולם וממתין לסיומם המסודר כדי למנוע זליגת זיכרון.
ThreadPool::~ThreadPool()
{
    {
        lock_guard<mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    for (auto& t : m_workers)
        t.join();
}

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

/* 原有的线程池耦合性太强，Task->process()方法 需要到对应的Task类那里
（http_conn类）中处理读写，处理过程还需要注册修改 监听事件（这点其实不大好，有点混乱）。
本版本的想法是把 事件循环监听事件全部放在 Server类中管理。

重写的线程池 */

#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <functional>
#include <assert.h>

class ThreadPool 
{
public:
    explicit ThreadPool(size_t threadCount = 8): m_pool(std::make_shared<Pool>()) 
    {
        assert(threadCount > 0);
        for(size_t i = 0; i < threadCount; i++) 
        {
            std::thread([pool = m_pool] 
            {
                std::unique_lock<std::mutex> locker(pool->mtx);
                while(true) 
                {
                    if(!pool->tasks.empty()) 
                    {
                        auto task = std::move(pool->tasks.front());
                        pool->tasks.pop();
                        locker.unlock();
                        task();
                        locker.lock();
                    } 
                    else if(pool->isClosed) 
                        break;
                    else 
                        pool->cond.wait(locker);
                }
            }).detach();
        }
    }

    ThreadPool() = default;

    ThreadPool(ThreadPool&&) = default;
    
    ~ThreadPool() 
    {
        if(static_cast<bool>(m_pool)) 
        {
            {
                std::lock_guard<std::mutex> locker(m_pool->mtx);
                m_pool->isClosed = true;
            }
            m_pool->cond.notify_all();
        }
    }

    template<class F>
    void AddTask(F&& task) 
    {
        {
            std::lock_guard<std::mutex> locker(m_pool->mtx);
            m_pool->tasks.emplace(std::forward<F>(task));
        }
        m_pool->cond.notify_one();
    }

private:
    struct Pool 
    {
        std::mutex mtx;
        std::condition_variable cond;
        bool isClosed;
        std::queue<std::function<void()>> tasks;
    };
    std::shared_ptr<Pool> m_pool;
};


#endif
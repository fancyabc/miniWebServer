/* 阻塞队列 */

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

#include <assert.h>
#include <deque>

#include "locker.h"

using namespace std;


/* 实现一个队列的封装 */
template <class T>
class block_queue
{
public:
    block_queue(int maxcapacity = 1000);
    ~block_queue();

    void clear();

    bool empty();

    bool full();

    bool  front(T &value);

    bool  back(T &value);

    int size();

    int capacity();

    bool push_back(const T &item);

    bool pop(T &item);

    bool pop(T &item, int timeout);

private:
    locker m_mutex;
    cond m_cond;

    deque<T> m_deque;
    int m_capacity;
};


template <class T>
block_queue<T>::block_queue(int maxcapacity) : m_capacity(maxcapacity)
{
    assert(maxcapacity > 0);
}


template <class T>
void block_queue<T>::clear()
{
    m_mutex.lock();
    m_deque.clear();
    m_mutex.unlock();
}


template <class T>
bool block_queue<T>::full()
{
    m_mutex.lock();
    if( m_deque.size() > m_capacity )
    {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}


template <class T>
bool block_queue<T>::empty()
{
    m_mutex.lock();
    if( m_deque.empty() )
    {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}


template <class T>
bool block_queue<T>::front(T &value)
{
    m_mutex.lock();
    if(m_deque.empty())
    {
        m_mutex.unlock();
        return false;
    }
    value = m_deque.front;
    m_mutex.unlock();
    return true;
}


template <class T>
bool block_queue<T>::back(T &value)
{
    m_mutex.lock();
    if(m_deque.empty())
    {
        m_mutex.unlock();
        return false;
    }
    value = m_deque.back;
    m_mutex.unlock();
    return true;
}


template <class T>
int block_queue<T>::size()
{
    int temp;
    m_mutex.lock();
    temp = m_deque.size();
    m_mutex.unlock();
    return temp;
}


/* 向队列添加元素，需要将所有使用队列的线程唤醒，当有元素push进队，相当于生产者生产了一个元素，若当前无线程等待条件变量，则无唤醒意义 */
template <class T>
bool block_queue<T>::push_back(const T &item)
{
    m_mutex.lock();
    if(m_deque.size() >= m_capacity)
    {
        m_cond.broadcast();
        m_mutex.unlock();
        return false;
    }

    m_deque.push_back(item);
    m_cond.broadcast();
    m_mutex.unlock();
    return true;
}


template <class T>
bool block_queue<T>::pop(T &item)
{
    m_mutex.lock();
    while(m_deque.size() <= 0)
    {
        if(!m_cond.wait(m_mutex.get()))
        {
            m_mutex.unlock();
            return false;
        }
    }

    item = m_deque.front();
    m_deque.pop_front();
    m_mutex.unlock();
    return true;
}

// 超时处理
template <class T>
bool block_queue<T>::pop(T &item, int timeout)
{
    struct timespec t = {0,0};
    struct timeval cur_time = {0, 0};
    gettimeofday(&cur_time, NULL);

    m_mutex.lock();
    if( m_deque.size() <= 0 )
    {
        t.tv_sec = cur_time.tv_sec + timeout/1000;
        t.tv_nsec = (timeout % 1000) * 1000;
        if( !m_cond.wait(m_mutex.get(), t))
        {
            m_mutex.unlock();
            return false;
        }
    }

    if(  m_deque.size() <= 0 )
    {
        m_mutex.unlock();
        return false;
    }

    item = m_deque.front();
    m_deque.pop_front();
    m_mutex.unlock();
    return true;
}

#endif
/* 半同步/半反应堆线程池实现 */

#ifndef THREADPOOL_H
#define THREADPOOL_H


#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

#include "locker.h"

/* 线程池类，将它定义为模板类是为了代码复用，模板T是任务类 */
template< typename T >
class threadpool
{
public:
	threadpool( int thread_number = 8, int max_requests = 10000 );
	~threadpool();

	/* 往队列中添加任务 */
	bool append( T *request );

private:
	/* 工作线程运行的函数，不断从工作队列中取出任务并执行之 */
	static void * worker( void * arg );
	void run();

private:
	int m_thread_number;			/* 线程池中的线程数 */
	int m_max_requests;			/* 请求队列中允许的最大请求数 */
	pthread_t * m_threads;		/* 描述线程池的数组，大小为 m_thread_number */
	std::list< T* > m_workqueue;	/* 请求队列 */
	locker m_queuelocker;	/* 保护请求队列的互斥锁 */
	sem m_queuestat;	/* 是否有任务需要处理 */
	bool m_stop;	/* 是否结束线程 */
};


template< typename T >
threadpool< T >::threadpool( int thread_number, int max_requests ):
	m_thread_number( thread_number ), m_max_requests( max_requests ),
	m_stop( false ), m_threads( NULL )
{
	if( (thread_number <= 0) || (max_requests <= 0) )
	{
		throw std::exception();
	}

	m_threads = new pthread_t[m_thread_number];
	if( !m_threads )
	{
		throw std::exception();
	}


	/* 创建thread_number个线程，并把他们都设置为脱离线程 */
	for( int i = 0; i < thread_number; i++ )
	{
		printf( "create the %dth thread\n", i );
		if( pthread_create( m_threads + i, NULL, worker, this ) !=0 )	// 此处将线程参数设置为this指针，然后在worker函数中获取该指针并调用其动态方法run
		{
			delete [] m_threads;
			throw std::exception();
		}
		if( pthread_detach( m_threads[i] ) )	// 设置 m_threads[i] 为脱离线程
		{
			delete [] m_threads;
			throw std::exception();
		}
	}
}

template< typename T >
threadpool<T>::~threadpool()
{
	delete [] m_threads;
	m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T *request)
{
	/* 操作工作队列时一定要加锁，因为它被所有线程共享 */
	m_queuelocker.lock();
	if( m_workqueue.size() > m_max_requests)
	{
		m_queuelocker.unlock();
		return false;
	}
	m_workqueue.push_back(request);
	m_queuelocker.unlock();
	m_queuestat.post();
	return true;
}

/*  
 静态函数使用类的动态成员（成员函数和成员变量）时，只能通过以下两种方式：
	1 通过类的静态对象来调用。比如单体模式，静态函数可以通过类的全局唯一实例来访问动态成员；
	2 将类的对象作为参数传递给该静态函数，然后在静态函数中引用这个对象，并调用其动态方法。
*/
template<typename T>
void *threadpool<T>::worker(void *arg)
{
	threadpool *pool = (threadpool *)arg;
	pool->run();
	return pool;
}

template<typename T>
void threadpool<T>::run()
{
	while( !m_stop )
	{
		m_queuestat.wait();
		m_queuelocker.lock();
		if( m_workqueue.empty() )
		{
			m_queuelocker.unlock();
			continue;
		}
		T * request = m_workqueue.front();
		m_workqueue.pop_front();
		m_queuelocker.unlock();
		if( !request )
		{
			continue;
		}
		request->process();
	}
}

#endif


#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>


class sem
{
public:
	sem()
	{
		if( sem_init( &m_sem, 0, 0 ) != 0 )		// sem_init 初始化一个未命名的信号量， pshared 指定信号量的类型，为0 表示这个信号量是当前进程的局部信号量，否则可以在多个进程间共享； value 信号量初值为0
		{
			throw std::exception();
		}
	}

	sem(int num)
	{
		if( sem_init( &m_sem, 0, num ) != 0 )
		{
			throw std::exception();
		}
	}

	~sem()
	{
		sem_destroy(&m_sem);	// 销毁信号量，以释放其占用的内核资源
	}

	bool wait()
	{
		return sem_wait( &m_sem ) == 0;		// 以原子操作方式将信号量的值减1，如果信号量的值为0，则 sem_wait 将被阻塞，知道这个信号量具有非零值
	}

	bool post()
	{
		return sem_post( &m_sem ) == 0;	  // 以原子操作的方式将信号量的值加1。当信号量值大于0时，其他调用 sem_wait 等待信号量的线程将被唤醒
	}

private:
	sem_t m_sem;
};


class locker
{
public:
	locker()
	{
		if( pthread_mutex_init( &m_mutex, NULL ) != 0)	// 初始化互斥锁，使用默认属性
		{
			throw std::exception();
		}
	}

	~locker()
	{
		pthread_mutex_destroy( &m_mutex );				// 销毁互斥锁，释放其占用的内核资源
	}

	bool lock()
	{
		return pthread_mutex_lock( &m_mutex ) == 0;		// 以原子的方式给一个互斥锁加锁，如果目标互斥锁已经被锁上，则pthread_mutex_lock 将阻塞，直到该互斥锁的占有者将其解锁
	}

	bool unlock()
	{
		return pthread_mutex_unlock( &m_mutex ) == 0;	// 以原子的方式给一个互斥锁解锁，如果此时有其他线程正在等待这个互斥锁，则这些线程中的某一个将获得它
	}

private:
	pthread_mutex_t m_mutex;
};



class cond
{
public:
	cond()
	{
		if( pthread_mutex_init( &m_mutex, NULL ) != 0)	// 初始化互斥锁，使用默认属性
		{
			throw std::exception();
		}
		if( pthread_cond_init( &m_cond, NULL ) != 0 )	// 初始化条件变量，使用默认属性
		{
			pthread_mutex_destroy( &m_mutex );	// 销毁条件变量，释放占用的内核资源
			throw std::exception();
		}
	}

	~cond()
	{
		pthread_mutex_destroy( &m_mutex );
		pthread_cond_destroy( &m_cond );
	}


	bool wait()
	{
		int ret = 0;
		pthread_mutex_lock( &m_mutex );	// 确保锁 m_mutex 已经加锁
		ret = pthread_cond_wait( &m_cond, &m_mutex );	// 等待目标条件变量。
		pthread_mutex_unlock( &m_mutex );
		return ret == 0;
	}

	bool signal()
	{
		return pthread_cond_signal( &m_cond ) == 0;	// 
	}

private:
	pthread_mutex_t m_mutex;
	pthread_cond_t m_cond;
};

#endif

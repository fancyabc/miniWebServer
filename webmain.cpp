/* 用线程池实现的web服务器 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>


#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "sql_conn_pool.h"
#include "lst_timer.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define TIME_SLOT 5			// 最小超时时间

extern int addfd( int epollfd, int fd, bool one_shot );
extern int removefd( int epollfd, int fd );
extern int setnonblocking(int fd);


/* 定时器相关参数 */
static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd;


/* 信号处理函数 */
void sig_handler(int sig)
{
	int save_errno = errno;
	int msg = sig;

	send(pipefd[1],(char *)&msg, 1, 0);	// pipefd[1] 非阻塞的，因为缓冲区满会阻塞，进一步增加信号处理函数执行时间
	errno = save_errno;
}


void addsig( int sig, void (handler)(int), bool restart = true )
{
	struct sigaction sa;
	memset( &sa, '\0', sizeof( sa ) );
	sa.sa_handler = handler;
	if( restart )
	{
		sa.sa_flags |= SA_RESTART;
	}
	sigfillset( &sa.sa_mask );
	assert( sigaction( sig, &sa, NULL ) != -1 );
}


void show_error( int connfd, const char *info )
{
	printf( "%s", info );
	send( connfd, info, strlen( info ), 0 );
	close(connfd);
}


void timer_handler()
{
	timer_lst.tick();

	alarm(TIME_SLOT);
}


void cb_func(client_data *userdata)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, userdata->sockfd, 0);
	assert(userdata);
	close(userdata->sockfd);
	http_conn::m_user_count--;
	printf("close fd %d", userdata->sockfd);
}


/* 主函数仅负责IO的读写 */
int main( int argc, char *argv[] )
{
	if( argc <= 2 )
	{
		printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
		return 1;
	}
	
	const char *ip = argv[1];
	int port = atoi( argv[2] );

	/* 忽略SIGPIPE信号 */
	addsig( SIGPIPE, SIG_IGN );

	// 创建数据库连接池
	conn_pool *connPool = conn_pool::GetInstance();
	connPool->init("localhost", "fancy", "mypass", "test", 3306, 8 );

	/* 创建线程池 */
	threadpool<http_conn> * pool = NULL;
	try
	{
		pool = new threadpool<http_conn>(connPool);
	}
	catch( ... )
	{
		return 1;
	}

	/* 预先为每个可能的客户连接分配一个http_conn 对象 */
	http_conn *users = new http_conn[MAX_FD];
	assert( users );

	/* 初始化数据库读取表 */
	users->initmysql_result(connPool);

	int user_count = 0;		// 客户端连接计数

	int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
	assert( listenfd != -1 );
	struct linger tmp = { 1, 0};
	setsockopt( listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp) );

	int ret = 0;
	struct sockaddr_in address;
	bzero( &address, sizeof(address) );
	address.sin_family = AF_INET;
	inet_pton( AF_INET, ip, &address.sin_addr );
	address.sin_port = htons( port );

	ret = bind( listenfd, (struct sockaddr *)&address, sizeof( address ) );
	assert( ret >= 0 );

	ret = listen( listenfd, 5 );
	assert( ret >= 0 );

	epoll_event events[MAX_EVENT_NUMBER];
	epollfd = epoll_create( 5 );
	assert( epollfd != -1 );
	addfd( epollfd, listenfd, false );
	http_conn::m_epollfd = epollfd;


	ret = socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd);
	assert(ret != -1);
	setnonblocking(pipefd[1]);
	addfd(epollfd, pipefd[0], false);

	addsig(SIGALRM, sig_handler, false);
	addsig(SIGTERM, sig_handler, false);

	/* 每个user http请求 对应的定时器 */
	client_data *user_timer = new client_data[MAX_FD];
	assert(user_timer);

	alarm(TIME_SLOT);
	bool timeout = false;

	bool stop_server = false;

	while( !stop_server )
	{
		int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
		if( (number < 0) && (errno != EINTR) )
		{
			printf("epoll failure\n");
			break;
		}

		for( int i=0; i < number; i++ )
		{
			int sockfd = events[i].data.fd;
			if(  sockfd == listenfd )
			{
				struct sockaddr_in client_addr;
				socklen_t client_addrlen = sizeof( client_addr );
				int connfd = accept( listenfd, ( struct sockaddr *)&client_addr, &client_addrlen );
				if( connfd < 0 )
				{
					printf( "errno is: %d\n", errno );
					continue;
				}

				/* 客户连接计数超出最大连接数 */
				if( http_conn::m_user_count >= MAX_FD )
				{
					show_error( connfd, "Internal server busy" );
					continue;
				}
				/* 初始化客户连接 */
				users[connfd].init( connfd, client_addr );

				/* 初始化client_data数据 */
				user_timer[connfd].address = client_addr;
				user_timer[connfd].sockfd = connfd;
				util_timer *timer = new util_timer;
				assert(timer);
				timer->user_data = &user_timer[connfd];
				timer->cb_func = cb_func;
				time_t c_time = time(NULL);
				timer->expire = c_time + 3*TIME_SLOT;
				user_timer[connfd].timer = timer;
				timer_lst.add_timer(timer);

			}
			else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) )
			{
				/* 如果有异常，直接关闭客户端连接，并移除对应定时器 */
				//users[sockfd].close_conn();
				util_timer *timer = user_timer[sockfd].timer;
				timer->cb_func(&user_timer[sockfd]);

				if(timer)
				{
					timer_lst.del_timer(timer);
				}
			}
			else if(sockfd == pipefd[0] && ( events[i].events & EPOLLIN ))
			{
				//int sig;
				char signals[1024];

				// 从管道读端读出信号值，成功返回字节数，失败返回-1, 正常情况下ret总返回1，只有14和15两个ASCII码对应的字符
				int ret = recv(pipefd[0], signals, sizeof(signals), 0);
				
				if(ret == -1)
				{
					continue;	// 出错
				}
				else if( ret == 0 )
				{
					continue;
				}
				else
				{
					for(int i = 0; i < ret; ++i)
					{
						switch (signals[i])
						{
						case SIGALRM:
						{
							timeout = true;
							break;
						}
						case SIGTERM:
						{
							stop_server = true;
							break;
						}
						default:
							break;
						}
					}
				}
			}
			else if( events[i].events & EPOLLIN )
			{	/* 处理客户连接上收到的数据 */
				util_timer *timer = user_timer[sockfd].timer;
				if( users[sockfd].read() )
				{
					/* log */


					pool->append( users + sockfd );

					/* 若有数据传输，则重置定时器超时时间 */
					if(timer)
					{
						time_t cur = time(NULL);
						timer->expire = cur + 3 * TIME_SLOT;
						/* Log */
						timer_lst.adjust_timer(timer);
					}
				}
				else
				{
					// 关闭连接并删除对应定时器
					//users[sockfd].close_conn();
					timer->cb_func(&user_timer[sockfd]);
					if(timer)
					{
						timer_lst.del_timer(timer);
					}
				}
			}
			else if( events[i].events & EPOLLOUT )
			{
				util_timer *timer = user_timer[sockfd].timer;
				/* 根据写的结果，决定是否关闭连接 */
				if( users[sockfd].write() )
				{
					/* log */

					/* 若有数据传输，则重置定时器超时时间 */
					if(timer)
					{
						time_t cur = time(NULL);
						timer->expire = cur + 3 * TIME_SLOT;
						/* Log */
						timer_lst.adjust_timer(timer);
					}
				}
				else
				{
					// 关闭连接并删除对应定时器
					//users[sockfd].close_conn();
					timer->cb_func(&user_timer[sockfd]);
					if(timer)
					{
						timer_lst.del_timer(timer);
					}
				}

			}
			else
			{
					// other
			}
		}
		if(timeout)
		{
			timer_handler();
			timeout = false;
		}
	}

	close( epollfd );
	close( listenfd );
	close(pipefd[0]);
	close(pipefd[1]);
	delete [] users;
	delete [] user_timer;
	delete pool;
	return 0;
}

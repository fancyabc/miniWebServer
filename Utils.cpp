# include "Utils.h"
#include "lst_timer.h"

/* 静态成员类外初始化 */
int *Utils::m_pipefd = 0;
int Utils::m_epollfd = 0;

int Utils::setNonBlock(int fd)
{
    int old_option = fcntl( fd, F_GETFL );
	int new_option = old_option | O_NONBLOCK;
	fcntl( fd, F_SETFL, new_option );
	return old_option;
}


//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(m_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}


void Utils::addfd( int epollfd, int fd, bool one_shot, int trig_mode )
{
	epoll_event event;
	event.data.fd = fd;

	// 边沿触发模式
	if(1 == trig_mode)
		event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	else	// 水平触发模式
		event.events = EPOLLIN | EPOLLRDHUP;

	if( one_shot )
	{
		event.events |= EPOLLONESHOT;
	}
	epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
	setNonBlock( fd );
}


void Utils::removefd( int epollfd, int fd )
{
	epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
	close( fd );
}


void Utils::modfd( int epollfd, int fd, int ev, int trig_mode )
{
	epoll_event event;
	event.data.fd = fd;

	// 边沿触发模式
	if(1 == trig_mode)
		event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
	else	// 水平触发模式
		event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
	
	epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}



void cb_func(client_data *userdata)
{
    assert(userdata);
    epoll_ctl(Utils::m_epollfd, EPOLL_CTL_DEL, userdata->sockfd, 0);
    close(userdata->sockfd);
//    http_conn::m_user_count--;
    
    LOG_INFO("close fd %d", userdata->sockfd);
    Log::get_instance()->flush();
}

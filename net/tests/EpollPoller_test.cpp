/* EpollPoller
    测试代码
 */

#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include <stdio.h>

#include "EpollPoller.h"

#define MAX_EVENT_NUMBER    1024
static int pipefd[2];

int setnonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return old_option;
}

/*
void addfd(int epollfd, int fd)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}
*/

void sig_handler( int sig )
{
	/* 保留原来的errno，在函数最后恢复，以保证函数的可重入性 */
	int save_errno = errno;
	int msg = sig;
	send( pipefd[1], ( char * )&msg, 1, 0 );  // 将信号值写入管道，以通知主循环
	errno = save_errno;
}

void addsig( int sig )
{
	struct sigaction sa;
	memset( &sa, '\0', sizeof( sa ) );
	sa.sa_handler = sig_handler;
	sa.sa_flags |= SA_RESTART;
	sigfillset( &sa.sa_mask );
	assert( sigaction( sig, &sa, NULL ) != -1 );
}


int main(int argc, const char *argv[])
{
    if (argc <= 2) 
    {
        fprintf(stderr, "usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char *ip = argv[1];
    int port       = atoi(argv[2]);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    if (ret == -1) 
    {
        fprintf(stderr, "bind error, errno is:%d, errstr:%s\n", errno, strerror(errno));
        return 1;
    }
    ret = listen(listenfd, 5);
    assert(ret != -1);

//    epoll_event events[MAX_EVENT_NUMBER];
//    int epollfd = epoll_create(5);
//    assert(epollfd != -1);

    net::EpollPoller ep;
    ep.AddFd(listenfd, EPOLLIN);
    //addfd(epollfd, listenfd);
    setnonblocking(listenfd);
    
    /*使用socketpair创建管道， 注册pipefd[0]上的可读事件*/
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert( ret != -1);

    setnonblocking(pipefd[1]);
    //addfd(epollfd, pipefd[0]);
    ep.AddFd(pipefd[0],EPOLLIN | EPOLLET);
    setnonblocking(pipefd[0]);

    /*设置一些信号的处理函数*/
    addsig(SIGHUP);
    addsig(SIGCHLD);
    addsig(SIGTERM);
    addsig(SIGINT);
    bool stop_server = false;

    while(!stop_server)
    {
        printf("in outer loop\n");
        //int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        int number = ep.EPollWait();
        if ((number < 0) && (errno != EINTR)) 
        {
            fprintf(stderr, "epoll wait failure!\n");
            break;
        }

        for(int i = 0; i < number; i++) 
        {
            int sockfd = ep.GetEventFd(i);
            uint32_t events = ep.GetEvents(i);
            /*如果就绪的文件描述符是listenfd，则处理新的连接*/
            if (sockfd == listenfd) 
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
                //addfd(epollfd, connfd);
                ep.AddFd(connfd, EPOLLIN | EPOLLET);
                setnonblocking(connfd);

            }
            /*如果就绪的文件描述符是pipefd[0], 则处理信号*/
            else if ((sockfd == pipefd[0]) && (events & EPOLLIN)) 
            {
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                printf("RET = %d\n", ret);
                if (ret == -1) 
                {
                    continue;
                }
                else if (ret == 0) 
                {
                    continue;
                }
                else
                {
                    /* 因为每个信号值占1字节，所以按字节来逐个接收信号。我们以SIGTERM
                     * 为例，来说明如何安全地终止服务器主循环
                     */
                    for(int i = 0; i < ret; i++) 
                    {
                        printf("i = %d\n", i);
                        switch (signals[i])
                        {
                            case SIGCHLD:
                            case SIGHUP:
                            {
                                continue;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                stop_server = true;
                                printf("in case SIGINT\n");                            }
                        }
                        printf("out of switch\n");
                    }
                }
            }
            else
            {

            }
            printf("in loop for\n");
        }
    }
    
    fprintf(stderr, "close fds\n");
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    return 0;
}

/*
fancy@fancy:net$ ./test 127.0.0.1 9000
in outer loop
in loop for
in outer loop
in loop for
in outer loop
in loop for
in outer loop
in loop for
in outer loop
^Cin outer loop
RET = 1
i = 0
in case SIGINT
out of switch
in loop for
close fds
fancy@fancy:net$ 
*/
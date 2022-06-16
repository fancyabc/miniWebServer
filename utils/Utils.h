/* 工具类 

*/

#ifndef UTILS_H
#define UTILS_H

#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <assert.h>
#include <sys/epoll.h>



class Utils
{
private:
 
public:
    Utils(/* args */){}
    ~Utils(){}

    int setNonBlock(int fd);
    static void sig_handler(int sig);
    void addsig(int sig, void(handler)(int), bool restart=true);
    void addfd( int epollfd, int fd, bool one_shot, int trig_mode );
    void removefd( int epollfd, int fd);
    void modfd(int epollfd, int fd, int ev, int trig_mod);

public:

    static int *m_pipefd;
    static int m_epollfd;

};

#endif
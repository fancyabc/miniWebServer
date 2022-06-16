#ifndef WEBSERVER_H
#define WEBSERVER_H

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
#include <memory>

//#include "net/EpollPoller.h"        // 用自己的 封装逐步替换原来的原生代码
#include "../base/locker.h"
#include "../base/thread_pool.h"
#include "http_connection.h"
#include "../base/sql_conn_pool.h"
#include "../net/heaptimer.h"
#include "../base/log.h"

#include "../utils/Utils.h"


#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define TIME_SLOT 5			// 最小超时时间

/* 事件循环还有注册、注销的管理都放在这里 */
class WebServer
{
public:
    WebServer();
    ~WebServer();
    void start();   // 对外接口，调用跑起来eventLoop()
    void init(int port, int timeOutMs,int trigMode, bool optLinger, 
        int sqlPort, string sqlUsername, string sqlPasswd, 
        string dbName, int connPoolNum, int threadNum,
        bool openLog, int logQueueSize);

private:
    bool initSocket();  // 在此 初始化监听fd 
    void initEventMode(int trigMode);
    void eventLoop();

    bool dealListen();
    bool dealSignal();
    void dealRead(httpConn *client);
    void dealWrite(httpConn *client);

    void addClient(int connfd, struct sockaddr_in addr);
    void extentTime(httpConn* client);

    void closeConn(httpConn *client);   // 关闭连接
    void onRead(httpConn* client);
    void onWrite(httpConn* client);
    void onProcess(httpConn *client);

private:
    int m_port;
    int m_listenfd;
    int m_pipefd[2];
    char *m_srcDir;         /* 指向资源文件根目录 */
    bool m_optLinger;
    Utils utils;            /* 工具类对象，调用它的方法管理要监听的事件 */
    int m_timeoutMs;     
    bool m_stop;         // 是否停止Loop（）

//  epoll 以及 触发模式
    int l_trig_mode;
    int trig_mode;
    int m_epollfd;      // 核心啊
    epoll_event events[MAX_EVENT_NUMBER];

    /* httpConn类 */
    httpConn *users;
    std::unique_ptr<HeapTimer> m_timer;
    std::unique_ptr<ThreadPool> m_threadpool;
};


#endif
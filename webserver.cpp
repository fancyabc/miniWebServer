#include "webserver.h"

WebServer::WebServer() :m_threadpool(new ThreadPool(8)) // 在此创建线程池
{
    //users = new http_conn[MAX_FD];
    users = new httpConn[MAX_FD];
    user_timer = new client_data[MAX_FD];

    /* 资源所在目录 */
    char resource_path[200];
    getcwd(resource_path, 200);
    char root[] = "/resources/";
    m_srcDir = (char *)malloc(strlen(resource_path)+strlen(root)+1);
    strcpy(m_srcDir, resource_path);
    strcat(m_srcDir, root);

}


WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    conn_pool::GetInstance()->DestroyPool();
    delete [] users;
    delete [] user_timer;

    m_stop = true;
    free(m_srcDir);
}


void WebServer::init(int port, int trigMode, bool optLinger, 
        int sqlPort, string sqlUsername, string sqlPasswd, 
        string dbName, int connPoolNum, int threadNum,
        bool openLog, int logQueueSize)
{
    m_port = port;
    httpConn::m_userCount = 0;

    initEventMode(trigMode);
    if( openLog )
    {
        Log::get_instance()->init("./log/ServerLog", 2000, 800000, logQueueSize);
        if(m_stop)
        {
            LOG_ERROR("========== Server init error!==========");
        }
        else{
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", m_port, m_optLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (l_trig_mode ? "ET": "LT"),
                            (trig_mode ? "ET": "LT"));
            LOG_INFO("srcDir: %s", httpConn::m_srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
    
    conn_pool::GetInstance()->init("localhost", sqlUsername, sqlPasswd, dbName, 3306, connPoolNum);
   //  users->initmysql_result(m_sqlConnPool);     // 初始化数据可读取表

    if( !initSocket() )
    {
        m_stop = true;
    }
}


/* 初始化并设置listenfd */
bool WebServer::initSocket()
{
    if( m_port > 65535 || m_port < 1024 )
    {
        LOG_ERROR("Port:%d error!", m_port);
        return false;
    }

    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if( m_listenfd < 0 )
    {
        LOG_ERROR("Create socket error!");
        return false;
    }

    int ret;
    struct sockaddr_in addr;
    bzero( &addr, sizeof(addr) );
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons( m_port );
    
    // 优雅关闭: 直到所剩数据发送完毕或超时
    struct linger tmp;
    if( m_optLinger )
    {
        tmp = { 0, 1};
    }
    else
    {
        tmp = { 1, 1};
    }
	ret = setsockopt( m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp) );
    if( ret < 0 )
    {
        close(m_listenfd);
        LOG_ERROR("Init linger error!");
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1) 
    {
        LOG_ERROR("set socket setsockopt error !");
        close(m_listenfd);
        return false;
    }

	ret = bind( m_listenfd, (struct sockaddr *)&addr, sizeof( addr ) );
	if( ret < 0 )
    {
        close(m_listenfd);
        LOG_ERROR("Bind error!");
        return false;
    }

	ret = listen( m_listenfd, 5 );
	if( ret < 0 )
    {
        close(m_listenfd);
        LOG_ERROR("listen error!");
        return false;
    }

    // 将监听fd纳入epoll 绑定到epollfd
//    events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);    // 在此创建epollfd
    assert( m_epollfd != -1 );

    /* 设置信号和描述符 */
    Utils::m_pipefd = m_pipefd;
    Utils::m_epollfd = m_epollfd;

    // 设置监听fd非阻塞
    utils.addfd(m_epollfd, m_listenfd, false, l_trig_mode);
    LOG_INFO("Server port: %d", m_port);

    ret = socketpair(AF_UNIX,SOCK_STREAM, 0,  m_pipefd);
    if( ret < 0 )
    {
        close(m_listenfd);
        LOG_ERROR("Add m_pipefd error!");
        return false;
    }

    utils.setNonBlock( m_pipefd[1] );

    //m_epoller->AddFd(m_pipefd[0],  EPOLLIN | EPOLLRDHUP);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIME_SLOT);

    return true;
}

void WebServer::closeConn(httpConn *client) 
{
    assert(client);
    client->Close();    // 关闭连接 的状态
    //服务器端关闭连接，移除对应的定时器
    util_timer *timer = user_timer[client->getFd()].timer;
    dealTimer(timer, client->getFd());
//    LOG_INFO("Client[%d] quit!", client->getFd());
//    utils.removefd(m_epollfd, client->getFd());
}


/* 设置触发模式 */
void WebServer::initEventMode(int trigMode)
{
    if( trigMode == 0 )     // LT + LT
    {
        l_trig_mode = 0;
        trig_mode = 0;
    }
    else if( trigMode == 1 )    // LT + ET
    {
        l_trig_mode = 0;
        trig_mode = 1;
    }
    else if( trigMode == 2 )    // ET + LT
    {
        l_trig_mode = 1;
        trig_mode = 0;
    }
    else if( trigMode == 3)     // ET + ET
    {
        l_trig_mode = 1;
        trig_mode = 1;
    }
    httpConn::m_isET = (trig_mode==1 ? 1 : 0);
}


void WebServer::start()
{
    eventLoop();
}


/* 事件循环 */
void WebServer::eventLoop()
{
    m_timeout = false;
    
    while(!m_stop)
    {
        int num = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if( (num < 0) && (errno != EINTR) )
		{
			LOG_ERROR("%s","epoll failure\n");
			break;
		}

        for(int i = 0; i < num;i++)
        {
            int fd = events[i].data.fd;

            if(fd == m_listenfd)
            {
                bool flag = false;
                flag = dealListen();
                if( false == flag )
                {
                    continue;
                }
            }
            else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR) )
            {
                closeConn(&users[fd]);      // 通过fd索引到具体的http连接
            }
            else if( (fd == m_pipefd[0]) && ( events[i].events & EPOLLIN ) )
            {
                bool flag = dealSignal();
                if (false == flag)
                    LOG_ERROR("%s", "deal clientdata failure");
            }
            else if( events[i].events & EPOLLIN )
            {
                dealRead(&users[fd]);
            }
            else if( events[i].events & EPOLLOUT )
            {
                dealWrite(&users[fd]);
            }
        }
        /*  */
        if (m_timeout)
        {
            timer_lst.tick();
            alarm(TIME_SLOT);

            LOG_INFO("%s", "timer tick");

            m_timeout = false;
        }
    }
}


bool WebServer::dealListen()
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    if( 1 == l_trig_mode )   // ET
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&addr, &addrlen);
        if( connfd < 0 )
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        // 客户连接计数超出最大连接数
        if( httpConn::m_userCount >= MAX_FD )
        {
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }

        //  初始化客户端连接 
        addClient(connfd, addr);
    }
    else                // lt
    {   
        while (1)
        {
            int connfd = accept( m_listenfd, ( struct sockaddr *)&addr, &addrlen );
            if( connfd < 0 )
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }

            // 客户连接计数超出最大连接数 
            if( httpConn::m_userCount >= MAX_FD )
            {
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            // 初始化客户端连接 
            addClient(connfd, addr);

        }
        return false;
    }  
    return true;
}


bool WebServer::dealSignal()
{
    char signals[1024];

    // 从管道读端读出信号值，成功返回字节数，失败返回-1, 正常情况下ret总返回1，只有14和15两个ASCII码对应的字符
    int ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    
    if(ret == -1)
    {
        return false;	// 出错
    }
    else if( ret == 0 )
    {
        return false;
    }
    else
    {
        for(int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                m_timeout = true;
                break;
            }
            case SIGTERM:
            {
                m_stop = true;
                break;
            }
            default:
                break;
            }
        }
    }
    return true;
}



void WebServer::dealRead(httpConn *client)
{
    /* 处理客户连接上收到的数据 */
    extentTime(client);
    m_threadpool->AddTask(std::bind(&WebServer::onRead, this, client));
}


void WebServer::extentTime(httpConn *client)
{
    assert(client);
    util_timer *timer = user_timer[client->getFd()].timer;
    if(timer)
    {
        timerAdjust(timer);
    }
}


void WebServer::onRead(httpConn* client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno);
    if(ret <= 0 && readErrno != EAGAIN) {
        closeConn(client);
        return;
    }
    onProcess(client);
}


void WebServer::dealWrite(httpConn *client)
{
    extentTime(client);
    m_threadpool->AddTask(std::bind(&WebServer::onWrite, this, client));
}


void WebServer::onWrite(httpConn *client)
{
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);
    if(client->toWriteBytes() == 0) 
    {
        /* 传输完成 */
        if(client->isKeepAlive()) 
        {
            onProcess(client);
            return;
        }
    }
    else if(ret < 0) 
    {
        if(writeErrno == EAGAIN) 
        {
            /* 继续传输 */
            //epoller_->ModFd(client->getFd(), connEvent_ | EPOLLOUT);
            utils.modfd(m_epollfd,client->getFd(), EPOLLOUT, trig_mode);
            return;
        }
    }
    closeConn(client);    
}

void WebServer::onProcess(httpConn *client)
{
    if(client->process())   // 返回真说明处理完成，可以写了，为其注册可写事件
    {
        utils.modfd(m_epollfd,client->getFd(), EPOLLOUT, trig_mode);
    }
    else
    {
        utils.modfd(m_epollfd,client->getFd(), EPOLLIN, trig_mode);
    }
}

/* 添加新连接：初始化连接，为其设置对应的定时器 */
void WebServer::addClient(int connfd, struct sockaddr_in addr)
{
    users[connfd].init(connfd, addr);
    utils.addfd(m_epollfd, connfd, true, trig_mode);    // 在此添加要监听描述符

    /* 初始化client_data数据 */
    user_timer[connfd].address = addr;
    user_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    assert(timer);
    timer->user_data = &user_timer[connfd];
    timer->cb_func = cb_func;
    time_t c_time = time(NULL);
    timer->expire = c_time + 3*TIME_SLOT;
    user_timer[connfd].timer = timer;
    timer_lst.add_timer(timer);
    LOG_INFO("Client[%d] in!", users[connfd].getFd());
}


void WebServer::timerAdjust(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIME_SLOT;
    /* Log */
    LOG_INFO("%s", "adjust timer once");
    Log::get_instance()->flush();
    timer_lst.adjust_timer(timer);
}


void WebServer::dealTimer(util_timer *timer, int sockfd)
{
    timer->cb_func(&user_timer[sockfd]);     
    if (timer)
    {
        timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", user_timer[sockfd].sockfd);
}
#include "webserver.h"

WebServer::WebServer() :m_threadpool(new ThreadPool(8)) ,m_timer(new HeapTimer())
{ 
    users = new httpConn[MAX_FD];

    /* 资源所在目录 */
    m_srcDir = getcwd(nullptr, 200);
    assert(m_srcDir);
    strncat(m_srcDir, "/resources",12);

}


WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    conn_pool::GetInstance()->DestroyPool();
    delete [] users;

    m_stop = true;
    free(m_srcDir);
}


void WebServer::init(int port, int timeOutMs,int trigMode, bool optLinger, 
        int sqlPort, string sqlUsername, string sqlPasswd, 
        string dbName, int connPoolNum, int threadNum,
        bool openLog, int logQueueSize)
{
    m_port = port;
    m_timeoutMs = timeOutMs;
    httpConn::m_userCount = 0;
    httpConn::m_srcDir = m_srcDir;

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
    utils.removefd(m_epollfd, client->getFd());
    client->Close();    // 关闭连接 的状态
    //服务器端关闭连接，移除对应的定时器
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
    if( !m_stop )
    {
        LOG_INFO("========== Server start ==========");
    }
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
            else
            {
                LOG_ERROR("Unexpected event");
            }
        }
        if (m_timeoutMs > 0)
        {
            m_timer->GetNextTick();
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
    if(m_timeoutMs > 0)
    {
        m_timer->adjust(client->getFd(), m_timeoutMs);
    }
}


void WebServer::onRead(httpConn* client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno);
    if(ret <= 0 && readErrno != EAGAIN)     // 读取失败，关闭连接
    {
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
    ret = client->write(&writeErrno);   // 往写缓冲写的字节
    if(client->toWriteBytes() == 0)     // 没有要写的
    {
        /* 传输完成 */
        if(client->isKeepAlive())       // 处理 缓冲的数据
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
    closeConn(client);    // 处理完（或者响应完、或者重新注册监听事件）后，关闭连接
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

    if(m_timeoutMs > 0)
    {
        m_timer->add(connfd, m_timeoutMs, std::bind(&WebServer::closeConn, this, &users[connfd]));  // 延长该httpconn的 expired time
    }
    utils.addfd(m_epollfd, connfd, true, trig_mode);    // 在此添加要监听描述符
    LOG_INFO("Client[%d] in!", users[connfd].getFd());
}
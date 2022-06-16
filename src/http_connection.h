
#include <sys/types.h>
#include <sys/uio.h>
#include <arpa/inet.h>

#include "http_request.h"
#include "http_response.h"
#include "../net/Buffer.h"

using namespace net;

class httpConn
{
public:
    httpConn(/* args */);
    ~httpConn();

    void init(int sockFd, const sockaddr_in& addr);

    ssize_t read(int* saveErrno);

    ssize_t write(int* saveErrno);

    void Close();

    int getFd() const;

    int getPort() const;

    const char* getIP() const;
    
    sockaddr_in getAddr() const;
    
    bool process();

    int toWriteBytes() { 
        return m_iv[0].iov_len + m_iv[1].iov_len; 
    }

    bool isKeepAlive() const {
        return m_request.isKeepAlive();
    }

public:
    static bool m_isET;
    static const char *m_srcDir;
    static int m_userCount;
//    static int m_epollfd;

private:
    int m_fd;
    struct sockaddr_in m_addr;

    bool m_isClose;
    struct iovec m_iv[2];
	int m_iv_count;

    Buffer m_readBuffer;   
    Buffer m_writeBuffer;

    httpRequest m_request;
    httpResponse m_response;

};


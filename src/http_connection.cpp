#include "http_connection.h"

#include "../base/log.h"

using namespace std;

const char* httpConn::m_srcDir;
int httpConn::m_userCount;
bool httpConn::m_isET;

httpConn::httpConn() 
{ 
    m_fd = -1;
    m_addr = { 0 };
    m_isClose = true;
};

httpConn::~httpConn() { 
    Close(); 
};

void httpConn::init(int fd, const sockaddr_in& addr) {
    assert(fd > 0);
    m_userCount++;
    m_addr = addr;
    m_fd = fd;
    m_readBuffer.retrieveAll();
    m_writeBuffer.retrieveAll();
    m_isClose = false;
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", m_fd, getIP(), getPort(), (int)m_userCount);
}

void httpConn::Close() 
{
    m_response.unmapFile();
    if(m_isClose == false)
    {
        m_isClose = true; 
        m_userCount--;
        close(m_fd);
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", m_fd, getIP(), getPort(), (int)m_userCount);
    }
}

int httpConn::getFd() const 
{
    return m_fd;
};

struct sockaddr_in httpConn::getAddr() const 
{
    return m_addr;
}

const char* httpConn::getIP() const 
{
    return inet_ntoa(m_addr.sin_addr);
}

int httpConn::getPort() const 
{
    return m_addr.sin_port;
}

ssize_t httpConn::read(int* saveErrno) {
    ssize_t len = -1;
    do {
        len = m_readBuffer.readFd(m_fd, saveErrno);
        if (len <= 0) {
            break;
        }
    } while (m_isET);
    return len;
}

ssize_t httpConn::write(int* saveErrno) 
{
    ssize_t len = -1;
    do {
        len = writev(m_fd, m_iv, m_iv_count);
        if(len <= 0) 
        {
            *saveErrno = errno;
            break;
        }
        if(m_iv[0].iov_len + m_iv[1].iov_len  == 0) 
        { 
            break; /* 传输结束 */
        } 
        else if(static_cast<size_t>(len) > m_iv[0].iov_len) 
        {
            m_iv[1].iov_base = (uint8_t*) m_iv[1].iov_base + (len - m_iv[0].iov_len);
            m_iv[1].iov_len -= (len - m_iv[0].iov_len);
            if(m_iv[0].iov_len) 
            {
                m_writeBuffer.retrieveAll();
                m_iv[0].iov_len = 0;
            }
        }
        else {
            m_iv[0].iov_base = (uint8_t*)m_iv[0].iov_base + len; 
            m_iv[0].iov_len -= len; 
            m_writeBuffer.retrieve(len);
        }
    } while(m_isET || toWriteBytes() > 10240);
    return len;
}

bool httpConn::process() 
{
    m_request.init();
    if(m_readBuffer.readableBytes() <= 0)
    {
        return false;
    }
    else if(m_request.parse(m_readBuffer)) 
    {
        LOG_DEBUG("inprocess %s", m_request.path().c_str());
        m_response.init(m_srcDir, m_request.path(), m_request.isKeepAlive(), 200);
    } 
    else 
    {
        m_response.init(m_srcDir, m_request.path(), false, 400);
    }

    m_response.makeResponse(m_writeBuffer);
    /* 响应头 */
    m_iv[0].iov_base = const_cast<char*>(m_writeBuffer.peek());
    m_iv[0].iov_len = m_writeBuffer.readableBytes();
    m_iv_count = 1;

    /* 文件 */
    if(( m_response.fileLen() > 0  ) && m_response.file()) {
        m_iv[1].iov_base = m_response.file();
        m_iv[1].iov_len = m_response.fileLen();
        m_iv_count = 2;
    }
    LOG_DEBUG("filesize:%d, %d  to %d", m_response.fileLen() , m_iv_count, toWriteBytes());
    return true;
}

/* 应用层缓冲区 */
/*
    网络层在发送数据的时候，由于TCP窗口太小，会动之数据无法发送出去，而上层可能不断产生新的数据，
    此时就需要将数据先存储起来，以便等socket可写时再次发送。
    这个存储的地方就被称为发送缓冲区。

    在收到数据后，我们可以直接对其进行解包，但是这样做并不好。

        - 为了让网络层更加通用，网络层应该与业务层解耦
        - TCP是流式协议，某一次收到的数据的大小不一定够一个完整包大小，需要缓存起来以便数据足够一个包大小时处理
        - 收到数据足够一个包，但是出于一些特殊的业务逻辑要求，需要将收到的数据暂时缓存起来，等满足一定条件时再取出来处理

    因此，网络层确实需要一个接收缓冲区，将收取的数据按需存放在该缓冲区里面。随后交由专门的业务线程或者业务逻辑从接收缓冲区中取出数据，并解包处理业务。
*/

#ifndef NET_BUFFER_H
#define NET_BUFFER_H

#include <string>

#include <vector>
#include <assert.h>
#include <iostream>

namespace net{

// 参考：
/// A buffer class modeled after org.jboss.netty.buffer.ChannelBuffer
///
/// @code
/// +-------------------+------------------+------------------+
/// | prependable bytes |  readable bytes  |  writable bytes  |
/// |                   |     (CONTENT)    |                  |
/// +-------------------+------------------+------------------+
/// |                   |                  |                  |
/// 0      <=      readerIndex   <=   writerIndex    <=     size
/// @endcode

// muduo 里面的buffer，还有《C++服务器开发精髓》里面的buffer源码，这里简化了下

class Buffer
{
public:
    static const size_t kInitialSize;   // 在此声明
    static const size_t kCheapPrepend;    // 本项目，暂不 定义缓冲区头的预留空间
    
public:
    explicit Buffer(size_t initialSize = kInitialSize );
    ~Buffer();

    size_t readableBytes() const;
    size_t writeableBytes() const;
    size_t prependableBytes() const;

    const char* peek() const;
    void ensureWriteableBytes(size_t len);
    void hasWritten(size_t len);

    void retrieve(size_t len);
    void retrieveUntil(const char* end);

    void retrieveAll();
    std::string retrieveAllAsString();
    std::string retrievetoString(int len);

    char* beginWrite();
    const char *beginWriteConst() const;

    std::string toStringPiece() const;

    void append(const std::string &str);
    void append(const char *data, size_t len);
    void append(const void *data, size_t len);
//    void append(const Buffer &buff);

    /* 缓冲区大小缩减 */
    void shrink(size_t reserve);
    void swap(Buffer &rhs);

private:
    char *begin();
    const char *begin() const;
    void makeSpace(size_t len);

private:
    std::vector<char> m_buffer;
    size_t read_Index;
    size_t write_Index;
};

const size_t Buffer::kInitialSize = 1024;   // 静态成员变量类外初始化
const size_t Buffer::kCheapPrepend = 0;     // 预留空间为0
}



#endif
# include "Buffer.h"

/* 初始化缓冲区大小和读写游标位置 */
net::Buffer::Buffer(size_t initBufferSize) : m_buffer(initBufferSize), read_Index(0), write_Index(0)
{
}

net::Buffer::~Buffer()
{
}

/* 存储数据的范围是读指针~写指针 之间的区间 */
size_t net::Buffer::readableBytes() const
{
    return write_Index - read_Index;
}


size_t net::Buffer::writeableBytes() const
{
    return m_buffer.size() - write_Index;
}


size_t net::Buffer::prependableBytes() const
{
    return read_Index;
}


ssize_t net::Buffer::readFd(int fd, int *saveErrno)
{
    char extrabuf[65536];
    const size_t writeable = writeableBytes();

    struct iovec vec[2];
	
	vec[0].iov_base = begin() + write_Index;
	vec[0].iov_len = writeable;
	vec[1].iov_base = extrabuf;
	vec[1].iov_len = sizeof(extrabuf);

	const int iovcnt = (writeable < sizeof(extrabuf)) ? 2 : 1;
	const ssize_t n = readv(fd, vec, iovcnt);

    if( n < 0 )
    {
        *saveErrno = errno;
    }
    else if( static_cast<size_t>(n) <= writeable )
    {
        write_Index += n;
    }
    else
    {
        write_Index = m_buffer.size();
        append(extrabuf, n - writeable);
    }
    return n;
}


/* 返回存储数据区间的首地址 */
const char *net::Buffer::peek() const
{
    return begin() + read_Index;
}


/* 确保缓冲区还可以写入 len 个字节 */
void net::Buffer::ensureWriteableBytes(size_t len)
{
    if(writeableBytes() < len )     // 空间不够的话，就再申请可以容纳的空间
        makeSpace(len);
    
    assert(writeableBytes() >= len );
}


/* 写入操作后，调整写指针位置 */
void net::Buffer::hasWritten(size_t len)
{
    assert( len <= writeableBytes());
    write_Index += len;
}


// retrieve returns void, to prevent
// string str(retrieve(readableBytes()), readableBytes());
// the evaluation of two functions are unspecified
/* 读取后 len 个字节后，调整读指针指向位置 */
void net::Buffer::retrieve(size_t len)
{
    assert(len <= readableBytes());
    if( len < readableBytes() )
    {
        read_Index += len;
    }
    else
    {
        retrieveAll();
    }
}

/* 进行 直到读取到 end指向的字符位置停下的操作后，调整读指针位置  */
void net::Buffer::retrieveUntil(const char* end)
{
    assert(peek() <= end);
    assert(end <= beginWrite());
    retrieve(end - peek());
}

/* 读取了所有数据后， 重置读写指针的位置到缓冲区开始 */
void net::Buffer::retrieveAll()
{
    read_Index = 0;
    write_Index = 0;
}

/* 以string格式返回缓冲区的所有可读数据 */
std::string net::Buffer::retrieveAllAsString()
{
    return retrievetoString(readableBytes());
}

/* 从缓冲区数据部分读取 len个字符，以string形式返回 */
std::string net::Buffer::retrievetoString(size_t len)
{
    assert(len <= readableBytes());
    std::string result(peek(), len);
    retrieve(len);
    return result;
}

/* 返回写指针此时指向的地址 */
char* net::Buffer::beginWrite()
{
    return begin() + write_Index;
}

/* 返回写指针此时指向的地址（作为const参数 去使用） */
const char* net::Buffer::beginWriteConst() const
{
    return begin() + write_Index;
}

/* 以string形式 返回缓冲区可读数据 */
std::string net::Buffer::toStringPiece() const
{
    return std::string(peek(), static_cast<int>(readableBytes()));
}


/* 将string类型字符串 的内容追加到缓冲区里 */
void net::Buffer::append(const std::string &str)
{
    append(str.c_str(), str.size());
}

/* 将字符串追加到缓冲区 */
void net::Buffer::append(const char *data, size_t len)
{
    ensureWriteableBytes(len);
    std::copy(data, data+len, beginWrite());    // （追加）复制数据到缓冲区
    hasWritten(len);                            // 调整写指针
}

/* 将任意类型的字符串追加到缓冲区 */
void net::Buffer::append(const void *data, size_t len)
{
    append(static_cast<const char*>(data), len);    // 这里涉及到一个 指针类型转换
}


/* 收缩缓冲区大小 */
void net::Buffer::shrink(size_t reserve)
{
    Buffer another;
    another.ensureWriteableBytes(readableBytes() + reserve);    // 将数据移到缓冲区头部位置
    another.append(toStringPiece());                            // 将原缓冲区数据复制到新缓冲区内
    swap(another);                                              // 交换新旧 buffer 的缓冲区 和 读写索引值
}


void net::Buffer::swap(Buffer &rhs)
{
    m_buffer.swap(rhs.m_buffer);
    std::swap(read_Index, rhs.read_Index);
	std::swap(write_Index, rhs.write_Index);
}


/* 返回指向缓冲区首地址的指针 */
char* net::Buffer::begin()
{
    return &*m_buffer.begin();
}

/* 返回指向缓冲区首地址的指针 */
const char* net::Buffer::begin() const
{
    return &*m_buffer.begin();
}


/* 缓冲区扩容 */
void net::Buffer::makeSpace(size_t len)
{
    if( writeableBytes() + prependableBytes() < len )   // 当 （缓冲区）可写部分 和 读指针前长度 加起来仍不能满足要写入长度时
    {
        m_buffer.resize(write_Index + len + 1);         // 直接申请，足够装下的空间
    }
    else    // 否则把有数据的区域挪到缓冲区开始位置
    {
        size_t readable = readableBytes();
        std::copy(begin() + read_Index, begin() + write_Index, begin());    // 将已存储数据的区间复制到缓冲区开始处
        read_Index = 0;
        write_Index = read_Index + readable;
        assert(readable == readableBytes());
    }
}
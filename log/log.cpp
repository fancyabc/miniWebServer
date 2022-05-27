#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}

Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}


//C++11以后,使用局部变量懒汉模式不用加锁
Log* Log::get_instance()
{
    static Log instance;
    return &instance;
}


void* Log::flush_log_thread(void *args)
{
    Log::get_instance()->async_write_log();
}


/* 异步需要设置阻塞队列的长度，同步不需要设置 */
bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size)
{
    /* 异步日志模式：将所写的日志内容先存入阻塞队列，写线程从阻塞队列中取出内容，写入日志 */
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;

        // 创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    /* 设置日志写入缓冲区 */
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);

    m_split_lines = split_lines;            // 日志最大行数

    time_t t = time(NULL);                  // 得到当前日历时间，
    struct tm *sys_tm = localtime(&t);      
    struct tm my_tm = *sys_tm;              // 得到写入 tm 结构的时间值

 
    const char *p = strrchr(file_name, '/');    // 最后一个 '/' 的位置，提取真实文件名
    char log_full_name[256] = {0};

    //printf("%s \n", p);

    if (p == NULL)      // 传过来的文件名字符串无 '/' 字符，将 时间+文件名 作为日志名
    {
        /* 当前目录下的日志文件名 */
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name); 
    }
    else                
    {
        /* 指定目录下的日志文件名 */
        strcpy(log_name, p + 1);    // 将文件名复制到文件名 成员变量log_name
        strncpy(dir_name, file_name, p - file_name + 1);    // 将路径复制 给成员变量 dir_name
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");   // 追加方式打开日志文件
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}


/* 格式化写入系统信息到日志：格式化时间 + 格式化内容 */
void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};

    switch (level)
    {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }


    /* 写入一行日志信息 */
    m_mutex.lock();
    m_count++;

    /* 日志不是今天的 或者 写入行数超过最大行数限制，创建新的文件 */
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0)
    {
        
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};
       
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
       
        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name); // 新日志路径和文件名时间 写入 new_log
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a"); // 可追加方式打开日志new_log
    }
 
    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    //写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';

    log_str = m_buf;

    m_mutex.unlock();

    /* 异步写入且队列不为满 */
    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push_back(log_str);    // 放入队列
    }
    else    // 同步写入
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);   // 写入
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}


/* 异步写入 */
void *Log::async_write_log()
{
    string single_log;
    //从阻塞队列中取出一个日志string，写入文件
    while (m_log_queue->pop(single_log))
    {
        m_mutex.lock();
        fputs(single_log.c_str(), m_fp);
        m_mutex.unlock();
    }
}
#ifndef _CONN_POOL_
#define _CONN_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <string.h>
#include <string>
#include "locker.h"


using namespace std;


/* 连接池的主要功能有：初始化、获取连接、释放连接，销毁连接池 */
class conn_pool
{

public:
    MYSQL *GetConnection();
    bool ReleaseConnection(MYSQL *conn);
    int GetFreeConn();
    void DestroyPool();         // 销毁所有连接
    

    // 局部静态变量单例模式
    static conn_pool *GetInstance();

    void init(string Ip, string User, string PassWord, string DBName, int Port, unsigned int MaxConn);

    conn_pool();
    ~conn_pool();

private:
    unsigned int MaxConn;
    unsigned int CurConn;   // 当前已使用的连接数
    unsigned int FreeConn;  // 当前空闲的连接数

private:
    locker lock;
    list<MYSQL *> connList;     // 连接池
    sem reserve;

private:
    string Ip;              // 主机IP地址
    string Port;            // 数据库端口号
    string User;            // 登录用户名
    string PassWord;        // 登录用户数据库密码
    string DataBaseName;    // 使用数据库名
};



class connctionRAII
{
public:
    connctionRAII(MYSQL **conn, conn_pool *connPool);
    ~connctionRAII();

private:
    MYSQL *connRAII;
    conn_pool *poolRAII;
};


#endif
#include<mysql/mysql.h>

#include <iostream>



#include "sql_conn_pool.h"



conn_pool::conn_pool()
{
    this->CurConn = 0;
    this->FreeConn = 0;
}

conn_pool::~conn_pool()
{
    DestroyPool();
}

conn_pool *conn_pool::GetInstance()
{
    static conn_pool connPool;
    return &connPool;
}


void conn_pool::init(string ip,  string User, string PassWord, string DBName, int Port, unsigned int MaxConn)
{
    // 初始化数据库信息
    this->Ip = ip;
    this->Port = Port;
    this->User = User;
    this->PassWord = PassWord;
    this->DataBaseName = DBName;

    for(unsigned int i=0;i < MaxConn; i++)
    {
        MYSQL *conn = NULL;
        conn = mysql_init(conn);

        if(conn == NULL)
        {
            cout << "Error:" << mysql_error(conn);  //
            exit(1);
        }

        conn = mysql_real_connect(conn, ip.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0 );

        if(conn == NULL)
        {
            cout << "Error:" << mysql_error(conn);  //
            exit(1);
        }

        // 更新连接池和空闲连接数量
        connList.push_back(conn);
        ++FreeConn;
    }

    // 信号量初值设置为最大连接次数
    reserve = sem(FreeConn);
    this->MaxConn = FreeConn;
}


/* 有请求时，从数据库连接池中返回一个可用的连接，更新使用和空闲连接数 */
MYSQL *conn_pool::GetConnection()
{
    MYSQL *conn = NULL;

    if(0 == connList.size())
    {
        return NULL;
    }

    // 取出连接， 信号量原子减1， 到0时就等待
    reserve.wait();

    lock.lock();

    conn = connList.front();
    connList.pop_front();

    --FreeConn;
    ++CurConn;


    lock.unlock();

    return conn;
}


/* 释放当前使用的连接 */
bool conn_pool::ReleaseConnection(MYSQL *conn)
{
    if(NULL == conn)
    {
        return false;
    }

    lock.lock();

    connList.push_back(conn);
    ++FreeConn;
    --CurConn;

    lock.unlock();
    reserve.post();     // 释放连接原子+1

    return true;
}


/* 销毁连接池 */
void conn_pool::DestroyPool()
{
    lock.lock();

    if(connList.size() > 0)
    {
        // 迭代器遍历列表，关闭数据库
        list<MYSQL *>::iterator it;
        for(it=connList.begin();it!=connList.end();++it)
        {
            MYSQL *conn = *it;
            mysql_close(conn);
        }

        CurConn = 0;
        FreeConn = 0;

        connList.clear();

        lock.unlock();
    }

    lock.unlock();
}


/* 获取当前空闲连接数 */
int conn_pool::GetFreeConn()
{
    return this->FreeConn;
}


/* 不直接调用获取和释放连接的接口，将其封装起来，通过RAII机制进行获取和释放 */
connctionRAII::connctionRAII(MYSQL **SQL, conn_pool *connPool)
{
    *SQL = connPool->GetConnection();

    connRAII = *SQL;
    poolRAII = connPool;
}


connctionRAII::~connctionRAII()
{
    poolRAII->ReleaseConnection(connRAII);
}
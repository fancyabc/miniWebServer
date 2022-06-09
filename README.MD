# TinyWebServer

Linux下C++轻量级Web服务器，raw_version 的开发过程如下:

- 第一次提交: 根据游双《Linux高性能服务器编程》 code,实现了一个使用 半同步/半反应堆 并发模式线程池，有限状态机 对 HTTP请求解析， 的简单服务器。
    原始版本只支持GET 方法。

- 第二次提交：增加了对POST方法的支持； 增加了数据连接池类（单例模式实现），使用RAII机制释放数据连接。 通过解析POST的请求内容，提取输入的name和password。在此基础上实现了简单的登陆注册功能。

- 第三次提交：增加了定时器处理非活动连接模块. bug:运行一会儿后会出现

```bash
    Thread 1 "server" received signal SIGSEGV, Segmentation fault.
0x0000555555558844 in main (argc=3, argv=0x7fffffffe008) at webmain.cpp:216
216                                     timer->cb_func(&user_timer[sockfd]);
(gdb)
```

- 第四次提交：解决 运行一会儿出现内存泄漏的问题

- 第五次提交： 解决了 新注册用户时程序崩溃的问题。

    调整了线程池模型，加入了数据库连接池属性，每个工作线程可以在工作的时候申请一个数据库连接，并在工作完成后释放连接。

- 第六此提交: 增加日志模块

    采用同步 和 异步两种写入模式

    生产者与消费者模式

    日志模块采用单例模式，提供全局访问




- 修复了传输大文件时候，传输无法完全的问题 （书上原代码http_conn::write()传输大文件时，需要多次调用writev函数，便会出现问题，不是文件显示不全，这段逻辑有问题。修改每次传输后的便宜文件指针和传输长度，得以解决）

- 增加 ET 和 LT 触发模式（初步，主要是检验设置后处理逻辑是否正常）


## 总结:

原始版的服务器用到的技术点如下:

- 利用IO复用函数epoll+线程池实现高效的Reactor事件处理模式
- 实现了ET 和 LT 触发模式下的两种处理逻辑
- 通过状态机方法实现对HTTP请求报文的解析，支持GET和POST方法
- 访问服务端数据库实现 简单的注册、登录功能
- 利用RAII机制实现数据库连接，减少频繁与数据库建立连接与关闭带来的开销
- 利用单例模式和阻塞队列实现异步日志，记录服务器运行状态

## 测试

* 服务器测试环境:
    
    * ubuntu 20.04
    * mysql  8.0.29
 
* 数据库准备：

```sh
create database yourdb;
USE yourdb;
CREATE TABLE user(
    username char(50) NULL,
    passwd char(50) NULL
)ENGINE=InnoDB;

INSERT INTO user(username, passwd) VALUES('name', 'passwd');

```

* 修改数据库连接池初始化数据库信息(`webmain.cpp`中)。
* 修改`http_conn.cpp`中资源文件路径

* 编译
```
make
```

* 启动server

```
./run.sh
```

* 压力测试:

```
fancy@fancy:miniWebServer$ ./webbench -c 100 -t 10 http://127.0.0.1/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://127.0.0.1/
100 clients, running 10 sec.

Speed=717726 pages/min, 133883328 bytes/sec.
Requests: 119621 susceed, 0 failed.
fancy@fancy:miniWebServer$ 
fancy@fancy:miniWebServer$ ./webbench -c 1000 -t 10 http://127.0.0.1/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://127.0.0.1/
1000 clients, running 10 sec.

Speed=551274 pages/min, 102757320 bytes/sec.
Requests: 91879 susceed, 0 failed.
fancy@fancy:miniWebServer$

fancy@fancy:miniWebServer$ ./webbench -c 5000 -t 10 http://127.0.0.1/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://127.0.0.1/
5000 clients, running 10 sec.

Speed=451230 pages/min, 83450288 bytes/sec.
Requests: 75205 susceed, 0 failed.

fancy@fancy:miniWebServer$ ./webbench -c 10000 -t 10 http://127.0.0.1/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://127.0.0.1/
10000 clients, running 10 sec.

Speed=494034 pages/min, 91447928 bytes/sec.
Requests: 82339 susceed, 0 failed.
fancy@fancy:miniWebServer$ 
```

- 测试环境:ubuntu20.04 cpu:i5-4210u 内存：8G
- QPS 10000+
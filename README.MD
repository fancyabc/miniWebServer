# TinyWebServer

Linux下C++轻量级Web服务器:

- 第一次提交: 根据游双《Linux高性能服务器编程》 code,实现了一个使用 半同步/半反应堆 并发模式线程池，有限状态机 对 HTTP请求解析， 的简单服务器。
    原始版本只支持GET 方法。

- 第二次提交：增加了对POST方法的支持； 增加了数据连接池类（单例模式实现），使用RAII机制释放数据连接。 通过解析POST的请求内容，提取输入的name和password。在此基础上实现了简单的登陆注册功能。

- 第三次提交：增加了定时器处理非活动连接模块. bug:运行一会儿后会出现

```bash
    Thread 1 "server" received signal SIGSEGV, Segmentation fault.
0x0000555555558844 in main (argc=3, argv=0x7fffffffe008) at webmain.cpp:216
216                                     timer->cb_func(&user_timer[sockfd]);
(gdb)
``

- 第四次提交：解决 运行一会儿出现内存泄漏的问题

- 第五次提交： 解决了 新注册用户时程序崩溃的问题。

    调整了线程池模型，加入了数据库连接池属性，每个工作线程可以在工作的时候申请一个数据库连接，并在工作完成后释放连接。





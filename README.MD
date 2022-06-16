# TinyWebServer

Linux下C++轻量级Web服务器:

- Buffer 模块编写，通过boost_unit进行了单元测试

## todo: 

原有的http_conn 模块太过臃肿，只需要管理连接和读写就可以了。下一步将原本的 http_conn 模块拆分成： http_conn、 http_request、 http_response 三部分。

### 将原有的程序改写后存在的问题

程序可以运行，但是到了和
```
fancy@fancy:miniWebServer$ ./server 
Segmentation fault (core dumped)
fancy@fancy:miniWebServer$ gdb server 
...

For help, type "help".
Type "apropos word" to search for commands related to "word"...
Reading symbols from server...
(gdb) r
Starting program: /home/fancy/Desktop/MyProjects/miniWebServer/server 
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".
[New Thread 0x7ffff6f71700 (LWP 4466)]
[New Thread 0x7ffff6770700 (LWP 4467)]
[New Thread 0x7ffff5f6f700 (LWP 4468)]
[New Thread 0x7ffff576e700 (LWP 4469)]
[New Thread 0x7ffff4f6d700 (LWP 4470)]
[New Thread 0x7ffff476c700 (LWP 4471)]
[New Thread 0x7ffff3f6b700 (LWP 4472)]
[New Thread 0x7ffff376a700 (LWP 4473)]
[New Thread 0x7ffff06e7700 (LWP 4474)]

Thread 6 "server" received signal SIGSEGV, Segmentation fault.
[Switching to Thread 0x7ffff4f6d700 (LWP 4470)]
sort_timer_lst::del_timer (timer=0x55555d803b20, this=0x7fffffffdee0) at lst_timer.h:148
148             timer->next->prev = timer->prev;
(gdb) bt
#0  sort_timer_lst::del_timer (timer=0x55555d803b20, this=0x7fffffffdee0) at lst_timer.h:148
#1  WebServer::dealTimer (this=0x7ffffffe09f0, timer=0x55555d803b20, sockfd=<optimized out>) at webserver.cpp:488
#2  0x0000555555579e5a in WebServer::onWrite (this=0x7ffffffe09f0, client=0x7ffff08ec9d8) at webserver.cpp:436
#3  0x000055555557ada5 in std::function<void ()>::operator()() const (this=0x7ffff4f6ce80) at /usr/include/c++/9/bits/std_function.h:683
#4  ThreadPool::ThreadPool(unsigned long)::{lambda()#1}::operator()() const (__closure=<optimized out>) at thread_pool.h:35
#5  std::__invoke_impl<void, ThreadPool::ThreadPool(unsigned long)::{lambda()#1}>(std::__invoke_other, ThreadPool::ThreadPool(unsigned long)::{lambda()#1}&&) (__f=...) at /usr/include/c++/9/bits/invoke.h:60
#6  std::__invoke<ThreadPool::ThreadPool(unsigned long)::{lambda()#1}>(std::__invoke_result&&, (ThreadPool::ThreadPool(unsigned long)::{lambda()#1}&&)...) (__fn=...) at /usr/include/c++/9/bits/invoke.h:95
#7  std::thread::_Invoker<std::tuple<ThreadPool::ThreadPool(unsigned long)::{lambda()#1}> >::_M_invoke<0ul>(std::_Index_tuple<0ul>) (
    this=<optimized out>) at /usr/include/c++/9/thread:244
#8  std::thread::_Invoker<std::tuple<ThreadPool::ThreadPool(unsigned long)::{lambda()#1}> >::operator()() (this=<optimized out>)
    at /usr/include/c++/9/thread:251
#9  std::thread::_State_impl<std::thread::_Invoker<std::tuple<ThreadPool::ThreadPool(unsigned long)::{lambda()#1}> > >::_M_run() (this=0x55555559b450)
    at /usr/include/c++/9/thread:195
#10 0x00007ffff7775de4 in ?? () from /lib/x86_64-linux-gnu/libstdc++.so.6
#11 0x00007ffff7669609 in start_thread (arg=<optimized out>) at pthread_create.c:477
#12 0x00007ffff758e133 in clone () at ../sysdeps/unix/sysv/linux/x86_64/clone.S:95
(gdb) info thread
  Id   Target Id                                 Frame 
  1    Thread 0x7ffff6f72740 (LWP 4457) "server" 0x00007ffff758e46e in epoll_wait (epfd=13, events=events@entry=0x7ffffffe0a18, maxevents=maxevents@entry=10000, 
    timeout=timeout@entry=-1) at ../sysdeps/unix/sysv/linux/epoll_wait.c:30
  2    Thread 0x7ffff6f71700 (LWP 4466) "server" futex_wait_cancelable (private=<optimized out>, expected=0, futex_word=0x55555559ac60) at ../sysdeps/nptl/futex-internal.h:183
  3    Thread 0x7ffff6770700 (LWP 4467) "server" futex_wait_cancelable (private=<optimized out>, expected=0, futex_word=0x55555559ac60) at ../sysdeps/nptl/futex-internal.h:183
  4    Thread 0x7ffff5f6f700 (LWP 4468) "server" futex_wait_cancelable (private=<optimized out>, expected=0, futex_word=0x55555559ac64) at ../sysdeps/nptl/futex-internal.h:183
  5    Thread 0x7ffff576e700 (LWP 4469) "server" futex_wait_cancelable (private=<optimized out>, expected=0, futex_word=0x55555559ac60) at ../sysdeps/nptl/futex-internal.h:183
* 6    Thread 0x7ffff4f6d700 (LWP 4470) "server" sort_timer_lst::del_timer (timer=0x55555d803b20, this=0x7fffffffdee0) at lst_timer.h:148
  7    Thread 0x7ffff476c700 (LWP 4471) "server" futex_wait_cancelable (private=<optimized out>, expected=0, futex_word=0x55555559ac64) at ../sysdeps/nptl/futex-internal.h:183
  8    Thread 0x7ffff3f6b700 (LWP 4472) "server" futex_wait_cancelable (private=<optimized out>, expected=0, futex_word=0x55555559ac64) at ../sysdeps/nptl/futex-internal.h:183
  9    Thread 0x7ffff376a700 (LWP 4473) "server" futex_wait_cancelable (private=<optimized out>, expected=0, futex_word=0x55555559ac60) at ../sysdeps/nptl/futex-internal.h:183
  10   Thread 0x7ffff06e7700 (LWP 4474) "server" futex_wait_cancelable (private=<optimized out>, expected=0, futex_word=0x55555d79baf4) at ../sysdeps/nptl/futex-internal.h:183
(gdb) 
```

查了下，可能是某处内存泄漏？

但是，这个链表操作本身好像没啥问题啊？

见 [lst_timer.h](lst_timer.h)。
这个是书上的一个 定时器链表，正常的链表操作应该是木有问题的。

难道是和 链表上定时器关联的资源木有清理？
```
commit 60d8f12540dfafa16d69391ce6b9d512e15cc3cd (HEAD -> version2)
Author: fancyabc <2310403052@qq.com>
Date:   Wed Jun 15 23:40:02 2022 +0800

    继续定位注册received signal SIGSEGV问题

commit bf47175e79225193d76836e438c6b5625fd95b76
Author: fancyabc <2310403052@qq.com>
Date:   Wed Jun 15 20:27:22 2022 +0800
```
果然，修改了定时器相关代码，让定时器和用户连接不再相互指向，注册后 signal SIGSEGV 的问题得以解决。
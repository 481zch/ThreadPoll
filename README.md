## ThreadPoll

### 介绍

此线程池支持自定义优先级调用，动态伸缩线程池中线程数量，多种可调用对象传入等功能。

使用优先队列定义任务队列，借助哈希表和双链表实现线程的添加和删除，减少时间复杂度。定义了冷却时间来动态调整线程池容量：在空闲线程数量大于任务队列中的数量时调整；在任务队列中任务数量大于工作线程时调整；同时设置有最小线程数量限制和最大线程数量限制。

默认线程数量为4，最小线程数量为4，最大线程数量为40，冷却切换间隔为1000ms。

### 环境

gcc version 9.4.0

ubuntu 22.04

### 编译

```cpp
g++ -std=c++11 -pthread -lboost_system -o threadpool_test test.cpp
```

### 性能测试

和boost库进行比较，在默认参数情况下，得到以下的结果：

```cpp
root@ubuntu:/code/thread$ g++ -std=c++11 -pthread -lboost_system -o threadpool_test main.cpp
root@ubuntu:/code/thread$ ./threadpool_test
Testing Custom ThreadPool Performance...
Custom ThreadPool execution time: 125 milliseconds
Total sum (Custom ThreadPool): -827379968

Testing Boost.Asio ThreadPool Performance...
Boost.Asio ThreadPool execution time: 1037 milliseconds
Total sum (Boost.Asio ThreadPool): -827379968
```

比boost快可能在于boost开源库考虑到了更多的异常处理等内容。

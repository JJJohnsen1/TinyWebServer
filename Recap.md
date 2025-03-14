main.cpp 通过 Config对象初始化一些 Webserver类所需的参数。
端口号 -> PORT; 日志写入方式 -> LOGWrite; 触发组合模式 -> TRIGMode; listenfd触发模式 -> LISTENTrigmode;
connfd触发模式 -> CONNTrigmode; 优雅关闭链接 -> OPT_LINGER; 数据库连接池数量 -> sql_num;
线程池内的线程数量 -> thread_num; 是否关闭日志 -> close_log; 并发模型选择 -> actor_model;
main.cpp 使用初始的参数初始化 Webserver对象。 
接下来分别调用：
1. server.log_write();      启用日志
2. server.sql_pool();       数据库池
3. server.thread_pool();    线程池
4. server.trig_mode();      
5. server.eventListen();    监听
6. server.eventLoop();      运行

## log_write()
单例模式取出Log实例，并进行初始化。包括日志文件名称和路径、日志缓冲区大小、最大行数以及**最长日志条队列** -> 如果该参数大于0，则开启阻塞队列模式，参数为阻塞队列的长度，也就是日志的异步写操作。
异步交由新建的一个线程来完成写操作。
日志会以./2025_03_11_servername的格式存储，超过最大行数也会新建日志文件。

## sql_pool()
单例模式取出connection_pool实例，init通过传入的账户密码等连接数据库，并创建多个连接存入到conlist里。
并创建信号量，用来统筹连接池的资源。
connectionRAII中connectionRAII(MYSQL **con, connection_pool *connPool);用来取得一个连接。析构函数用来释放，这样可以避免手动释放连接~connectionRAII();。

## thread_pool()
m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
创建具有m_thread_num个的线程池
m_actormoder控制proactor（主线程进行读写，模拟异步I/O）和reactor（读写操作由线程自己完成）
注意p_thread的接收函数参数为静态的，所以传入静态参数，并以this为形参。再在函数里通过this调用真正的run()函数
要使用时，将任务（http）append进队列。
线程循环读取队列中的任务（http），并通过m_actormodel进行不同模式决定是否读取或者写入数据，然后调用request->process()进行处理。

## server.trig_mode()
决定m_LISTENTrigmode和m_CONNTrigmode的方式 ET | LT

## eventListen()
创建监听套接字 m_listenfd = socket(PF_INET, SOCK_STREAM, 0)
是否优雅关闭连接，涉及到四次挥手 setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp))
初始化address，并设置其端口和ip等信息（大端小端的转换）struct sockaddr_in address;
允许m_listenfd端口复用，并绑定address
监听ret = listen(m_listenfd, 5);

utils.init(TIMESLOT);定时器的创建
其中涉及到定时器的逻辑，由于非活跃连接占用了连接资源，严重影响服务器的性能，通过实现一个服务器定时器，处理这种非活跃连接，释放连接资源。利用alarm函数周期性地触发SIGALRM信号,该信号的信号处理函数利用管道通知主循环执行定时器链表上的定时任务.
utils中会有一个sort_timer_lst对象，该对象实现了升序链表，其中有void add_timer(util_timer *timer); void adjust_timer(util_timer *timer);  void del_timer(util_timer *timer); void tick();几个操作的函数。
tick则会对超时的链表进行处理，会调用回调函数关闭连接
epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
assert(user_data);
close(user_data->sockfd);
http_conn::m_user_count--;
[链表节点跳转查看](#section1)

epoll创建内核事件表，初始化，把 m_listenfd加入 epoll_event events[MAX_EVENT_NUMBER]; m_epollfd = epoll_create(5);
utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode); http_conn::m_epollfd = m_epollfd;

创建pipe套接字，用来统一信号源处理信号。
给SIGALRM，SIGTERM重新写处理函数。处理逻辑为把信号发送给m_pipefd[1]。m_pipefd[0]则注册到epoll中。

```cpp
ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
assert(ret != -1);
// 写端非阻塞，如果缓冲区满了，则会阻塞，这时候会进一步增加信号处理函数的执行时间，为此，将其修改为非阻塞。
utils.setnonblocking(m_pipefd[1]);
utils.addfd(m_epollfd, m_pipefd[0], false, 0);

utils.addsig(SIGPIPE, SIG_IGN);
utils.addsig(SIGALRM, utils.sig_handler, false);
utils.addsig(SIGTERM, utils.sig_handler, false);
```

## eventLoop()
主线程调用eventLoop进入循环处理。
统一事件源来int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
拿到events后，对events进行判断进行不同的处理逻辑。
1. 处理新到的客户连接 dealclientdata()
检查是否能合法创建
初始化http_conn数据，包括添加套接字到epoll
users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord,m_databaseName);
创建对应的timer并加入升序链表中 

2. 处理异常事件（events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR) -> deal_timer(timer, sockfd);
删除timer

3. 处理信号 dealwithsignal(timeout, stop_server)
处理超时和客户端停止

4. 处理客户连接上接收到的数据 dealwithread(sockfd)
这里开始分为reactor或proactor模式进行处理
reactor模式中，主线程(I/O处理单元)只负责监听文件描述符上是否有事件发生，有的话立即通知工作线程(逻辑单元 )，读写数据、接受新连接及处理客户请求均在工作线程中完成。通常由同步I/O实现。
proactor模式中，主线程和内核负责处理读写数据、接受新连接等I/O操作，工作线程仅负责业务逻辑，如处理客户请求。通常由异步I/O实现。
读完后，线程池中的线程从得到m_queuestat。从m_workqueue队列中拿到http的指针，为其分配一个数据库连接。然后进入核心函数**process()**处理。
接下来process会先进入process_read()。其中有主从状态机，主状态机CHECK_STATE_REQUESTLINE = 0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT为处理http的过程。从状态机LINE_OK = 0,LINE_BAD,LINE_OPEN为读一行的状态。

离开状态机时为返回do_request(),在这个函数中会根据连接信息，文件头和内容进行解析，拿到要返回的文件，判断是否有权限，并将其映射到 m_file_address内存地址上，方便返回。

接下来进入process_write(HTTP_CODE ret)。添加status_line，headers和Content到write_buf中。其中有m_iv，这好像是为了加快写操作。

为其注册写操作到epoll上。

5. 处理客户连接上的写事件 dealwithwrite(sockfd)
跟读操作差不多，写入就好。写完后记得init。















<a id="section1"></a>

```cpp
// 为每个客户端维护一个定时器，定时器使用升序链表实现
struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

// 时间过期后，调用回调函数对客户端进行处理
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;
    
    void (* cb_func)(client_data *);
    client_data *user_data;
    util_timer *prev;
    util_timer *next;
};

class Utils;
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}
```
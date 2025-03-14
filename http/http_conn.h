#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    // 只用到GET和POST
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    // 主状态机
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    // 从状态机
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };
    //
    enum HTTP_CODE
    {
        NO_REQUEST,     // 请求不完整,需要继续读取
        GET_REQUEST,   
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    void close_conn(bool real_close = true);    // 关闭连接
    void process();                             // 处理客户请求, 入口
    bool read_once();                           // 读取浏览器发送来的数据
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);
    int timer_flag; // 定时标志查看是否超时
    int improv;     // 改善标志，用于reactor模式, 表示任务是否完成

private:
    void init();                                // 初始化连接
    HTTP_CODE process_read();                   // 解析HTTP请求
    bool process_write(HTTP_CODE ret);          // 根据HTTP请求的处理结果, 决定返回给客户端的内容
    HTTP_CODE parse_request_line(char *text);   // 解析请求行
    HTTP_CODE parse_headers(char *text);        // 解析请求头
    HTTP_CODE parse_content(char *text);        // 解析请求内容
    HTTP_CODE do_request();                     // 处理请求, 正常情况返回FILE_REQUEST
    char *get_line() { return m_read_buf + m_start_line; }; // 获取一行内容
    LINE_STATUS parse_line();                               // 从状态机, 用于分析一行内容
    void unmap();

    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state; // 读为0, 写为1

private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];  // 读缓冲区
    long m_read_idx;                    // 读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
    long m_checked_idx;                 // 当前正在分析的字符在读缓冲区中的位置
    int m_start_line;                   // 当前正在解析的行的起始位置
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    CHECK_STATE m_check_state;          // 主状态机当前所处的状态
    METHOD m_method;                    // 请求方法
    char m_real_file[FILENAME_LEN];     // 客户请求的目标文件的完整路径,其内容等于doc_root + m_url
    char *m_url;                        // 请求的目标文件的文件名
    char *m_version;                    // HTTP协议版本号
    char *m_host;                       // 主机名
    long m_content_length;              // HTTP请求的消息体的长度
    bool m_linger;                      // HTTP请求是否要求保持长连接
    char *m_file_address;               // 客户请求的目标文件被mmap到内存中的起始位置 -> do_request()
    struct stat m_file_stat;            // 路径下的文件操作
    struct iovec m_iv[2];               // 采用writev来执行写操作, m_iv[0]指向m_write_buf, m_iv[1]指向m_file_address
    int m_iv_count;                     // 表示被写内存块的数量
    int cgi;                            // 是否启用的POST
    char *m_string;                     // 存储content数据,即post请求的数据
    int bytes_to_send;                  // 剩余发送字节数
    int bytes_have_send;
    char *doc_root;                     // 网站根目录

    map<string, string> m_users;
    int m_TRIGMode; // 0 LT, 1 ET
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif

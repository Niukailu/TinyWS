#ifndef __HTTP_CONN_H__
#define __HTTP_CONN_H__

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <map>
#include <string>
#include <string.h>

#include "../mysql/sql_connection_pool.h"
#include "../lock/locker.h"

class HttpConn
{
public:
    //设置读取文件的名称m_real_file大小
    static const int FILENAME_LEN = 200;
    //设置读缓冲区m_read_buf大小
    static const int READ_BUFFER_SIZE = 2048;
    //设置写缓冲区m_write_buf大小
    static const int WRITE_BUFFER_SIZE = 1024;

    //报文的请求方法
    //GET = 0 表示默认初始值是GET
    enum METHOD{GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATH};
    //主状态机的状态(分别表示解析位置为：请求行，请求头，请求体)
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    //报文解析的结果
    //NO_REQUEST：请求不完整，需要继续读取请求报文数据；GET_REQUEST：获得了完整的HTTP请求
    //BAD_REQUEST：HTTP请求报文有语法错误；INTERNAL_ERROR：服务器内部错误
    enum HTTP_CODE{NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, 
                    FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
    //从状态机的状态(标识一行的读取状态)
    //LINE_OK：完整读取一行；LINE_BAD：报文语法有误；LINE_OPEN：读取的行不完整
    enum LINE_STATUS{LINE_OK = 0, LINE_BAD, LINE_OPEN};

    //静态变量，多个对象共享
    static int m_epollfd;    //epoll文件描述符
    static int m_user_count; //用户数量

    MYSQL* mysql;           //该Http连接对象对应的一个数据库连接，用于插入、查询等sql操作
    int m_state;            //读为0, 写为1

public:
    HttpConn() {};
    ~HttpConn() {};

    //初始化套接字地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in &addr, char* root, int TRIGMode, int close_log, std::string user, std::string passwd, std::string sqlname);
    //关闭http连接
    void close_conn(bool real_close = true);

    //读取浏览器端发来的全部数据
    bool read_once();
    //处理
    void process();
    //响应报文写入函数
    bool write();

    sockaddr_in* get_address() {
        return &m_address;
    }

    //同步线程初始化数据库读取表
    //这个函数没必要写在这个类里吧。。而且感觉可以直接写成静态成员函数，方便调用
    void init_mysqlResult(SqlConnectionPool* connPool); 

    int timer_flag;     //
    int improv;         //

private:
    int m_sockfd;           //套接字文件描述符
    sockaddr_in m_address;   //

    //存储读取的请求报文数据 (在用户缓冲区)
    char m_read_buf[READ_BUFFER_SIZE];
    //缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    long m_read_idx;
    //m_read_buf读取的位置
    long m_checked_idx;
    //m_read_buf中已经解析的字符个数
    int m_start_line;

    //存储发出的响应报文数据 (在用户缓冲区)
    char m_write_buf[WRITE_BUFFER_SIZE];
    //指示m_write_buf中的长度
    int m_write_idx;

    //主状态机的状态
    CHECK_STATE m_check_state;
    //请求方法
    METHOD m_method;

    //以下为解析请求报文中对应的6个变量
    char m_real_file[FILENAME_LEN];  //存被读取文件的完整路径
    char* m_url;                    //目标文件的url，如/log.html
    char* m_version;
    char* m_host;
    long m_content_length;
    bool m_linger;

    char* m_file_address;      //mmap将服务器上的文件映射到用户空间的地址
    struct stat m_file_stat;   //存被读取文件的状态信息

    struct iovec m_iv[2];   // 可以发送多重缓冲区的数据，和 writev()一起用  
    int m_iv_count;         //缓冲区的个数

    int cgi;          //CGI校验？代码里并没有用啊。。。
    char* m_string;    //存储请求头数据
    int bytes_to_send;   //剩余要发送字节数
    int bytes_have_send; //已发送字节数
    char *doc_root;     //被请求文件存放的路径：./root/

    std::map<std::string, std::string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];    //登录数据库用户名
    char sql_passwd[100];   //登录数据库密码
    char sql_name[100];    //数据库名

private:
    //
    void init();
    //从m_read_buf读取，并处理请求报文
    HTTP_CODE process_read();
    //向m_write_buf写入响应报文数据
    bool process_write(HTTP_CODE ret);
    //主状态机内部调用从状态机
    //主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char *text);
    //主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);
    //主状态机解析报文中的请求体内容
    HTTP_CODE parse_content(char *text);
    //生成响应报文
    HTTP_CODE do_request();

    //m_start_line是已经解析的字符
    //get_line用于将指针向后偏移，指向未处理的字符。获取一行数据
    char *get_line() { return m_read_buf + m_start_line; };

    //从状态机负责读取报文一行，分析是请求报文的哪一部分
    LINE_STATUS parse_line();

    void unmap();

    //根据响应报文格式，生成对应8个部分
    bool add_response(const char *format, ...);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char *content);

};


#endif
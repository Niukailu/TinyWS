#ifndef __SQL_CONNECTION_POOL__
#define __SQL_CONNECTION_POOL__

#include <mysql/mysql.h>
#include <string>
#include <list>
#include "../lock/locker.h"
#include "../log/log.h"

class SqlConnectionPool
{
public:
    //懒汉单例模式获取类的实例（局部静态变量）
    static SqlConnectionPool* get_instance();
    //初始化数据库连接池
    void init(std::string url, std::string user, std::string passwd, std::string database_name, int port, int max_conn, int close_log);

    MYSQL* get_connection();               //获取一个数据库连接
    bool release_connection(MYSQL* conn);  //释放一个连接

    int get_free_conn();              //获取当前空闲的连接数
    void destroy_pool();	            //销毁数据库连接池   

public:
    std::string m_url;            //mysql服务器主机地址 (localhost)
    std::string m_port;           //mysql服务器的端口号，默认是3306
    std::string m_user;            //连接登录数据库用户名
    std::string m_passwd;          //连接登录数据库密码
    std::string m_database_name;    //使用的数据库名
    int m_close_log;           //日志开关

private:
    SqlConnectionPool();
    ~SqlConnectionPool();

    //这几个变量都没有用到哎。。。
    int m_maxConn;        //最大连接数
    int m_curConn;         //当前已使用的连接数
    int m_freeConn;        //当前空闲的连接数
    
    std::list<MYSQL*> connList;  //数据库连接池
    Sem reserve;           //信号量：实现多线程同步访问连接池
    Locker lock;           //互斥锁
};

//
class ConnectionRAII
{
public:
    //数据库连接本身是指针类型，所以参数需要通过双指针才能对其进行修改
    ConnectionRAII(MYSQL **con, SqlConnectionPool *connPool);
    ~ConnectionRAII();

private:
    MYSQL *conRAII;
    SqlConnectionPool *poolRAII;
};



#endif
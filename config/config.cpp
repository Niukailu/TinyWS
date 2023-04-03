#include "config.h"

Config::Config() { //定义构造函数
    //端口号, 默认9006 (本服务器开放的端口号)
    PORT = 9006;

    //日志写入方式，默认同步
    LOGWrite = 0;

    //触发组合模式，默认 listenfd LT + connfd LT
    TRIGMode = 0;

    //listenfd触发模式，默认LT
    LISTENTrigmode = 0;

    //connfd触发模式，默认LT
    CONNTrigmode = 0;

    //优雅关闭连接，默认不使用
    OPT_LINGER = 0;

    //数据库连接池数量，默认8
    sql_num = 8;

    //线程池内的线程数量，默认8
    thread_num = 8;

    //是否关闭日志，默认不关闭
    close_log = 0;

    //并发模型选择，默认是proactor
    actor_model = 0;
}

void Config::parse_arg(int argc, char *argv[]) {
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:";
    
}
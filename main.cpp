#include "config/config.h"
#include "webserver/webserver.h"


int main(int argc, char *argv[]) {
    
    //自定义数据库信息：用户名,密码,数据库名
    std::string user = "root";
    std::string passwd = "123456";
    std::string database_name = "mywsdb";

    //命令行参数解析
    Config config;  
    //不加参数默认用构造函数中定义的值
    config.parse_arg(argc, argv);

    WebServer server;

    //初始化
    server.init(config.PORT, user, passwd, database_name, config.LOGWrite, 
                config.OPT_LINGER,config.TRIGMode, config.sql_num, config.thread_num, 
                config.close_log, config.actor_model);

    //日志
    server.log_write();

    //数据库
    server.sql_pool();

    //线程池
    server.thread_pool();

    //触发模式
    server.trig_mode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();


    return 0;
}
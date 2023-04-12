#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>
#include <string>
#include <string.h>
#include <stdarg.h>

#include "block_queue.hpp"

class Log
{
public:
    //C++11以后, 使用局部变量懒汉模式不用加锁
    static Log* get_instance();

    static void* flush_log_thread(void* args);

    ///可选参数：日志文件、日志缓冲区大小、最大行数、最长日志条队列
    bool init(const char* file_name, int close_log, int log_buf_size = 8192, 
                int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char* format, ...);

    void flush(void);

private:
    Log();
    virtual ~Log();  //为什么就这个用了virtual?

    void* async_write_log();

private:
    char dir_name[128];  //日志存放路径名
    char log_name[128];  //日志文件名
    int m_split_lines;   //日志最大行数
    int m_log_buf_size;   //日志缓冲区大小
    long long m_count;    //日志行数记录
    int m_today;          //因为按天分类, 记录该日志文件创建时间是哪一天

    FILE *m_fp;        //打开log的文件指针
    char *m_buf;       //要输出的内容
    block_queue<std::string> *m_log_queue;  //阻塞队列
    bool m_isAsync;                       //是否异步标志位
    Locker m_mutex;                 //同步
    int m_close_log;                //是否关闭日志
};

//宏定义函数, 这四个宏定义在其他文件中使用，主要用于不同类型的日志输出
//##VA_ARGS是一个预处理指令，用于将可变数量的参数传递给宏定义中的参数列表
#define LOG_DEBUG(format, ...) if(m_close_log == 0) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(m_close_log == 0) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(m_close_log == 0) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(m_close_log == 0) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif

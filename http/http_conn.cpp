#include "http_conn.h"

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

Locker m_lock;  //为什么定义在这里？
std::map<std::string, std::string> users;

//对文件描述符设置非阻塞
void setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL); //fcntl对文件描述符进行操作
    int new_option = old_option | O_NONBLOCK;  //设置非阻塞模式打开文件
    fcntl(fd, F_SETFL, new_option);
}

//将内核事件表注册读事件? ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode == 1) //如果是边缘触发模式
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    else 
        event.events = EPOLLIN | EPOLLRDHUP;
    if(one_shot) //设置EPOLLONESHOT选项，以确保每个事件只被一个线程处理。
        event.events |= EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode == 1) //如果是边缘触发模式
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP | EPOLLET;
    else 
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP; //EPOLLRDHUP半连接状态

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void HttpConn::init(int sockfd, const sockaddr_in &addr, char* root, int TRIGMode, int close_log, std::string user, std::string passwd, std::string sqlname) {
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    ++m_user_count;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

//初始化新接受的连接
void HttpConn::init() {
    mysql = nullptr;
    bytes_to_send = 0;
    bytes_have_send = 0;
    //默认为分析请求行状态
    m_check_state = CHECK_STATE_REQUESTLINE;  
    m_linger = false;  //默认不开启优雅关闭连接？短连接
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

void HttpConn::init_mysqlResult(SqlConnectionPool* connPool) {
    //先从连接池中取一个连接
    MYSQL *mysql = nullptr;
    //通过封装类的方式使连接的建立和释放与对象的周期绑定
    ConnectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索浏览器端输入的username，passwd数据
    if(mysql_query(mysql, "SELECT username,passwd FROM user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));  //错误不返回吗？
    }
    //从表中检索完整的结果集存到res
    MYSQL_RES *res = mysql_store_result(mysql);

    //返回结果集中的列数
    //int num_fields = mysql_num_fields(res);
    //返回包含该表中所有字段结构的数组
    //MYSQL_FIELD *fields = mysql_fetch_fields(res);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    MYSQL_ROW row = NULL;
    while ((row = mysql_fetch_row(res)))
    {
        users[std::string(row[0])] = std::string(row[1]);
    }
    //最后不应该释放结果集吗
    //mysql_free_result(res);
}

void HttpConn::close_conn(bool real_close) {
    if(real_close && m_sockfd != -1) {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        --m_user_count;
        m_sockfd = -1;   //为啥等-1?
    }
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool HttpConn::read_once() {
    if(m_read_idx >= READ_BUFFER_SIZE) return false;
    int bytes_read = 0;
    
    if(m_TRIGMode == 0) {  //LT读取数据
        //recv是一个系统调用函数，用于在socket上接收数据
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read; //要判断一下bytes_read吧？？
        if(bytes_read <= 0) return false;
        return true;
    }
    else {  //ET读数据
        while (true)
        {
            //读到m_read_buf缓冲区
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if(bytes_read == -1) {
                //errno不用初始化？
                if(errno == EAGAIN || errno == EWOULDBLOCK) break;
                return false;
            }
            else if(bytes_read == 0) {
                return false;   //为啥是false?
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

//从状态机，负责读取报文的一行内容(解析整个报文，把每行通过'\0'划分开)
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
HttpConn::LINE_STATUS HttpConn::parse_line() {
    char tmp;
    //m_checked_idx初始值是0, m_read_idx经过read_once()指向缓冲区m_read_buf的数据末尾的下一个字节
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) 
    {
        tmp = m_read_buf[m_checked_idx];
        //在HTTP报文中，每一行的数据由\r\n作为结束字符，空行则是仅仅是字符\r\n
        if(tmp == '\r') {
            if((m_checked_idx + 1) == m_read_idx) //表示buffer还需要继续接收，返回LINE_OPEN
                return LINE_OPEN;
            else if(m_read_buf[m_checked_idx + 1] == '\n') {  //将m_checked_idx指向下一行的开头
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';  
                return LINE_OK;
            }
            return LINE_BAD;  //语法错误
        }
        //一般是上次读取到\r就到buffer末尾了，没有接收完整，再次接收时会出现这种情况
        else if(tmp == '\n') {
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    //还没有到\r\n，需要继续接收
    return LINE_OPEN;
}

//解析http请求行，获得请求方法，目标url及http版本号(顺序固定)
//在主状态机进行解析前，从状态机已经将每一行的末尾\r\n符号改为\0\0
HttpConn::HTTP_CODE HttpConn::parse_request_line(char *text) {
    //在text中查找第一个出现在" \t"中的字符的位置(因为各个部分之间通过\t或空格分隔)
    m_url = strpbrk(text, " \t");
    if(m_url == NULL) return BAD_REQUEST;
    //先将m_url所指处改为\0，代表结束符，再++
    *m_url++ = '\0';
    //此时text到\0处结束，即取出一个字段
    char *method = text;
    //不区分大小写的字符串比较
    if(strcasecmp(method, "GET") == 0) m_method = GET;
    else if(strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;   //表示需要进行登录信息校验(就是普通的校验啊。。。)
    }
    else return BAD_REQUEST;

    //将m_url向后偏移继续跳过可能还有的空格和\t字符，指向请求行的下一个字段
    //strspn()计算一个字符串的前缀子串中包含指定字符集合中的字符的长度
    m_url += strspn(m_url, " \t");
    //同上，要找到分隔符处将其置\0
    m_version = strpbrk(m_url, " \t");
    if(m_version == NULL) return BAD_REQUEST;
    *m_version++ = '\0';
    //这里因为版本号在请求行的最后一个字段，先去找版本号
    m_version += strspn(m_version, " \t");
    //这里好像是因为本项目仅支持HTTP/1.1(目前应用最广泛)
    if(strcasecmp(m_version, "HTTP/1.1") != 0) return BAD_REQUEST;
    //对url前7或8个字符进行判断(因为一般前面没有这些，直接是"/"或 "/text.html" 这种形式)
    if(strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/'); //找到第一个 / 的位置
    }
    else if(strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }            

    if(m_url == NULL || m_url[0] != '/') return BAD_REQUEST;
    //当url为单独的/时，显示欢迎界面
    if(strlen(m_url) == 1) strcat(m_url, "judge.html");

    //请求行处理完毕，将主状态机转移至：处理请求头
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析http请求头的信息 (HTTP请求头中的字段顺序并没有严格的规定)
HttpConn::HTTP_CODE HttpConn::parse_headers(char *text) {
    //若是'\0'表示处理的是空行
    if(text[0] == '\0') {
        if(m_content_length != 0) { //POST请求报文需处理消息体
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true; //原来优雅关闭连接是保持长连接的意思。。
        }
    }
    else if(strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else { //其他字段也可以根据需求继续分析。。
        LOG_INFO("oop! unknow header: %s", text);
    }
    return NO_REQUEST;
}

//解析http请求体的信息
HttpConn::HTTP_CODE HttpConn::parse_content(char *text) {
    //判断当前m_read_buf中是否有完整的消息体
    if(m_read_idx >= (m_checked_idx + m_content_length)) {
        text[m_content_length] = '\0';
        //消息体中是用户输入的用户名、密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//读取请求报文
HttpConn::HTTP_CODE HttpConn::process_read() {
    //初始化
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        //m_start_line初始化是0, m_checked_idx指向下一行的开头
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text); //初始text是整个缓冲区内容？

        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
            ret = parse_request_line(text);
            if(ret == BAD_REQUEST) return BAD_REQUEST;
            break;
        case CHECK_STATE_HEADER:
            ret = parse_headers(text);
            if(ret == BAD_REQUEST) return BAD_REQUEST;
            else if(ret == GET_REQUEST) return do_request();
            break;
        case CHECK_STATE_CONTENT:
            ret = parse_content(text);
            if(ret == GET_REQUEST) return do_request();
            line_status = LINE_OPEN;  //表示消息体不完整，跳出循环
            break;
        default:
            return INTERNAL_ERROR;
        }
    }
    
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::do_request() {
    //doc_root为网站根目录？
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    const char *p = strchr(m_url, '/'); 

    //cgi = 1代表需要验证登录注册信息，'/'后的数字是在前端form表单的action属性自定义的
    //2代表登录校验，3代表注册校验
    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];
        //存去掉标记的url
        char *m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        //复制FILENAME_LEN - len - 1个字符到m_real_file
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来(user=***&password=***)
        char name[100], passwd[100];
        int i;
        for(i = 5; m_string[i] != '&'; ++i) { //i=5是跳过user=
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';
        i += 10;
        int j;
        for(j = 0; m_string[i] != '\0'; ++i, ++j) {
            passwd[j] = m_string[i];
        }
        passwd[j] = '\0';

        //如果是注册校验，需先检测数据库中是否有重名的
        if(*(p + 1) == '3') {
            if(!users.count(name)) { //没有重名，插入
                char *sql_insert = (char*)malloc(sizeof(char) * 200);
                strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES('");
                strcpy(sql_insert, name);
                strcpy(sql_insert, "', '");
                strcpy(sql_insert, passwd);
                strcpy(sql_insert, "')");

                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                m_lock.unlock();
                free(sql_insert);

                if(res == 0) {  //插入成功
                    strcpy(m_url, "/log.html"); //strcpy函数会覆盖目标字符串
                    users[std::string(name)] = std::string(passwd); //users是全局变量，要加锁吧？
                }
                else strcpy(m_url, "/registerError.html"); //这里也没有复制到 m_real_file 变量里啊???
            }
            else {
                strcpy(m_url, "/registerError.html");
            }
        }
        //如果是登录，直接判断浏览器端输入的用户名和密码是否可以在表中(map)查找到，找到返回1，否则返回0
        else if(*(p + 1) == '2') {
            if(users.count(name) && passwd == users[name]) {
                strcpy(m_url, "/welcome.html");
            }
            else strcpy(m_url, "/logError.html");
        }
    }
    //后面这段写的好冗余。。。
    //表示是新用户，需要跳转到注册界面
    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    //表示是已有账号， 需跳转到登录界面
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    //表示请求图片资源
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    //表示请求视频资源
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    //表示要跳转到关注界面
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    //都不是，就直接将url与网站目录拼接
    else {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    //stat函数是 C 标准库中的一个系统调用函数，用于获取文件的状态信息, 执行成功，返回值为0
    if (stat(m_real_file, &m_file_stat) < 0) return NO_RESOURCE;
    //st_mode 字段是一个权限模式，用于表示文件或目录的类型和访问权限
    //S_IROTH宏定义表示其他用户具有读取该文件或目录的权限
    if (!(m_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;
    //S_ISDIR 宏定义可以用于判断一个文件或目录是否为: 目录类型
    if (S_ISDIR(m_file_stat.st_mode)) return BAD_REQUEST;
    //不是目录，就以只读模式打开文件。
    int fd = open(m_real_file, O_RDONLY);  //不得判断下返回值？
    //PROT_READ：可读; MAP_PRIVATE：私有映射
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0); //不得判断下返回值？
    close(fd);
    return FILE_REQUEST;
}

//释放映射的内存
void HttpConn::unmap() {
    if(m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

//该函数用于更新m_write_idx指针和缓冲区m_write_buf中的内容
bool HttpConn::add_response(const char *format, ...) {
    if(m_write_idx >= WRITE_BUFFER_SIZE) return false;
    //va_list用于处理可变参数列表, 可以使用 va_list 定义一个指向参数列表的指针
    va_list arg_list;
    //初始化该指针
    va_start(arg_list, format);
    //vsnprintf将格式化的输出写入一个字符数组中, 返回写入字符数组中的字符数, 不包括结尾\0
    //如果写入字符的数量超过了指定的大小，vsnprintf 将截断输出并在字符数组的结尾添加一个\0
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);

    //如果写入的数据长度超过缓冲区剩余空间，则报错（??）
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        //va_end用于释放一个指向可变参数列表的指针
        va_end(arg_list);
        return false;
    }

    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);
    return true;
}

//添加响应行信息
bool HttpConn::add_status_line(int status, const char *title) {
    return add_response("%s % d %s\r\n", "HTTP/1.1", status, title);
}

//添加响应头信息
bool HttpConn::add_headers(int content_length) {
    return add_content_length(content_length) && add_linger() && add_blank_line();
}

//响应头中的信息之一
bool HttpConn::add_content_type() {
    return add_response("Content-Type: %s\r\n", "text/html");
}

//响应头中的信息之一
bool HttpConn::add_content_length(int content_length) {
    return add_response("Content-Length: %d\r\n", content_length);
}

//响应头中的信息之一: 是否开启长连接
bool HttpConn::add_linger() {
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

//添加空白行
bool HttpConn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool HttpConn::add_content(const char *content) {
    return add_response("%s", content);
}

//写响应报文
bool HttpConn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if(!add_content(error_500_form)) //为什么只有这里这样判断了？
            return false; 
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if(m_file_stat.st_size != 0) {
            add_headers(m_file_stat.st_size);
            //struct iovec 用于在用户空间和内核空间之间传递数据
            //可以一次性读取或写入多个缓冲区的数据，提高数据传输效率
            m_iv[0].iov_base = m_write_buf; //内存缓冲区的起始地址
            m_iv[0].iov_len = m_write_idx;   //缓冲区的长度
            m_iv[1].iov_base = m_file_address; 
            m_iv[1].iov_len = m_file_stat.st_size; 
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else { //如果请求的资源大小为0，则返回空白html文件
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string)) return false;
        }
    }
    default:
        return false;
    }
    //除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

//
bool HttpConn::write() {
    int tmp = 0;
    //表示响应报文为空，一般不会出现这种情况
    if(bytes_to_send == 0) { 
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1)
    {
        //将多个缓冲区数据写入到文件描述符 m_sockfd 
        //writev()函数返回写入的字节数，如果出现错误则返回-1
        tmp = writev(m_sockfd, m_iv, m_iv_count);

        if(tmp < 0) {
            //判断缓冲区是否满了?
            if(errno == EAGAIN) { //"资源暂时不可用"
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += tmp;
        bytes_to_send -= tmp;
        if(bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + bytes_have_send - m_write_idx;
            m_iv[1].iov_len = bytes_to_send;
        }
        else {
            
        }
    }
    

}

void HttpConn::process() {
    HTTP_CODE read_ret = process_read();
    //NO_REQUEST表示请求报文还没读取完整，还需继续读
    if(read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    //
    bool write_ret = process_write(read_ret);
    if(!write_ret) close_conn();
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode); //这里不应该else?
}
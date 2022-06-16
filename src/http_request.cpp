#include "http_request.h"

using namespace std;

const unordered_set<string> httpRequest::DEFAULT_HTML{
            "/index", "/register", "/login",
             "/welcome", "/video", "/picture", };

const unordered_map<string, int> httpRequest::DEFAULT_HTML_TAG {
            {"/register.html", 0}, {"/login.html", 1},  };

void httpRequest::init() {
    m_method = m_path = m_version = m_body = "";
    m_state = REQUEST_LINE;
    m_header.clear();
    m_post.clear();
}

bool httpRequest::isKeepAlive() const {
    if(m_header.count("Connection") == 1) {
        return m_header.find("Connection")->second == "keep-alive" && m_version == "1.1";
    }
    return false;
}


/* 主状态机 */
bool httpRequest::parse(Buffer& buff) {
    const char CRLF[] = "\r\n";
    if(buff.readableBytes() <= 0) {     // 缓冲区不可读
        return false;
    }
    while(buff.readableBytes() && m_state != FINISH) {
        const char* lineEnd = search(buff.peek(), buff.beginWriteConst(), CRLF, CRLF + 2);  // 查找当前行结尾字符地址
        std::string line(buff.peek(), lineEnd);     // line 返回代表本行的字符串
        switch(m_state)
        {
        case REQUEST_LINE:
            if(!parseRequestLine(line)) {       // 解析请求行
                return false;
            }
            parsePath();
            break;    
        case HEADERS:
            parseHeader(line);
            if(buff.readableBytes() <= 2) {     // 如果缓冲区可读字段小于2，那么就认为解析完了，状态转移到FINISH
                m_state = FINISH;
            }
            break;
        case BODY:
            parseBody(line);        // 解析body
            break;
        default:
            break;
        }
        if(lineEnd == buff.beginWrite())    // 缓冲区内容已经被读取完
        { 
            break; 
        }
        buff.retrieveUntil(lineEnd + 2);    // 调整读指针位置
    }
    LOG_DEBUG("[%s], [%s], [%s]", m_method.c_str(), m_path.c_str(), m_version.c_str());     // 打印 方法、资源路径、版本号
    return true;
}

void httpRequest::parsePath() {
    if(m_path == "/") {
        m_path = "/index.html";     // 设置默认资源
    }
    else {                          // 在默认静态资源库里查找匹配到的html
        for(auto &item: DEFAULT_HTML) {
            if(item == m_path) {
                m_path += ".html";
                break;
            }
        }
    }
}

/* 正则匹配解析请求行 */
bool httpRequest::parseRequestLine(const string& line) {
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {   
        m_method = subMatch[1];
        m_path = subMatch[2];
        m_version = subMatch[3];
        m_state = HEADERS;
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}

/* 解析头部字段 */
void httpRequest::parseHeader(const string& line) {
    regex patten("^([^:]*): ?(.*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {
        m_header[subMatch[1]] = subMatch[2];
    }
    else {                          // 头部没有，那就转移到BODY状态去试着解析
        m_state = BODY;
    }
}

/* 解析包体（POST方法） */
void httpRequest::parseBody(const string& line) {
    m_body = line;
    parsePost();
    m_state = FINISH;               // 包体解析完毕，那么转台转移到 FINISH
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}

int httpRequest::converHex(char ch) {   // 16进制数转10进制
    if(ch >= 'A' && ch <= 'F') 
        return ch -'A' + 10;
    if(ch >= 'a' && ch <= 'f') 
        return ch -'a' + 10;
    return ch;
}

void httpRequest::parsePost() 
{
    if(m_method == "POST" && m_header["Content-Type"] == "application/x-www-form-urlencoded") 
    {
        parseFromUrlencoded();
        if(DEFAULT_HTML_TAG.count(m_path))  // 要找的页面是否在默认页面里
        {
            int tag = DEFAULT_HTML_TAG.find(m_path)->second;    // 有的话，查找它
            LOG_DEBUG("Tag:%d", tag);
            if(tag == 0 || tag == 1) 
            {
                bool isLogin = (tag == 1);                  // 存在的话，设置login 为 true
                if(userVerify(m_post["username"], m_post["password"], isLogin))   // 验证通过去 欢迎页面
                {
                    m_path = "/welcome.html";
                } 
                else {                                                          // 否则去 错误页面
                    m_path = "/error.html";
                }
            }
        }
    }   
}

void httpRequest::parseFromUrlencoded() 
{
    if(m_body.size() == 0) 
    { 
        return; 
    }

    // 包体长度不为0时
    string key, value;
    int num = 0;
    int n = m_body.size();
    int i = 0, j = 0;

    for(; i < n; i++) {
        char ch = m_body[i];
        switch (ch) 
        {
            case '=':
                key = m_body.substr(j, i - j);
                j = i + 1;
                break;
            case '+':
                m_body[i] = ' ';
                break;
            case '%':
                num = converHex(m_body[i + 1]) * 16 + converHex(m_body[i + 2]);
                m_body[i + 2] = num % 10 + '0';
                m_body[i + 1] = num / 10 + '0';
                i += 2;
                break;
            case '&':
                value = m_body.substr(j, i - j);
                j = i + 1;
                m_post[key] = value;
                LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
                break;
            default:
                break;
        }
    }
    assert(j <= i);
    if(m_post.count(key) == 0 && j < i) 
    {
        value = m_body.substr(j, i - j);
        m_post[key] = value;
    }
}


/* 登陆验证 */
bool httpRequest::userVerify(const string &name, const string &pwd, bool isLogin) {
    if(name == "" || pwd == "") 
    { 
        return false; 
    }
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());

    /* RAII方式获得一个数据库连接池的连接 */
    MYSQL* sql;
    connctionRAII(&sql,  conn_pool::GetInstance());
    assert(sql);
    
    bool flag = false;
    char order[256] = { 0 };
    MYSQL_RES *res = nullptr;
    
    if(!isLogin) { 
        flag = true; 
    }
    /* 查询用户及密码 */
    snprintf(order, 256, "SELECT username, passwd FROM user WHERE username='%s' LIMIT 1", name.c_str());    // 注意要写的SQL语句正确性（要和自己建的数据库表匹配）
    LOG_DEBUG("%s", order);

    if(mysql_query(sql, order)) { 
        mysql_free_result(res);
        return false; 
    }
    res = mysql_store_result(sql);

    while(MYSQL_ROW row = mysql_fetch_row(res)) 
    {
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        string password(row[1]);
        /* 注册行为 且 用户名未被使用*/
        if(isLogin) {
            if(pwd == password) 
            { 
                flag = true; 
            }
            else {
                flag = false;
                LOG_DEBUG("pwd error!");
            }
        } 
        else { 
            flag = false; 
            LOG_DEBUG("user used!");
        }
    }
    mysql_free_result(res);

    /* 注册行为 且 用户名未被使用*/
    if(!isLogin && flag == true) 
    {
        LOG_DEBUG("regirster!");printf("regirster");
        bzero(order, 256);
        snprintf(order, 256,"INSERT INTO user(username, passwd) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG( "%s", order);
        if(mysql_query(sql, order)) 
        { 
            LOG_DEBUG( "Insert error!");
            flag = false; 
        }
        flag = true;
    }

    // 在此释放刚才登录注册占用的连接  
    conn_pool::GetInstance()->ReleaseConnection(sql);

    LOG_DEBUG( "UserVerify success!!");
    return flag;
}

std::string httpRequest::path() const{
    return m_path;
}

std::string& httpRequest::path(){
    return m_path;
}
std::string httpRequest::method() const {
    return m_method;
}

std::string httpRequest::version() const {
    return m_version;
}

std::string httpRequest::getPost(const std::string& key) const {
    assert(key != "");
    if(m_post.count(key) == 1) {
        return m_post.find(key)->second;
    }
    return "";
}

std::string httpRequest::getPost(const char* key) const {
    assert(key != nullptr);
    if(m_post.count(key) == 1) {
        return m_post.find(key)->second;
    }
    return "";
}
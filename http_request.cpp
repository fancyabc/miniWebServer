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
    if(buff.readableBytes() <= 0) {
        return false;
    }
    while(buff.readableBytes() && m_state != FINISH) {
        const char* lineEnd = search(buff.peek(), buff.beginWriteConst(), CRLF, CRLF + 2);
        std::string line(buff.peek(), lineEnd);
        switch(m_state)
        {
        case REQUEST_LINE:
            if(!parseRequestLine(line)) {
                return false;
            }
            parsePath();
            break;    
        case HEADERS:
            parseHeader(line);
            if(buff.readableBytes() <= 2) {
                m_state = FINISH;
            }
            break;
        case BODY:
            parseBody(line);
            break;
        default:
            break;
        }
        if(lineEnd == buff.beginWrite()) { break; }
        buff.retrieveUntil(lineEnd + 2);
    }
    LOG_DEBUG("[%s], [%s], [%s]", m_method.c_str(), m_path.c_str(), m_version.c_str());
    return true;
}

void httpRequest::parsePath() {
    if(m_path == "/") {
        m_path = "/index.html"; 
    }
    else {
        for(auto &item: DEFAULT_HTML) {
            if(item == m_path) {
                m_path += ".html";
                break;
            }
        }
    }
}

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

void httpRequest::parseHeader(const string& line) {
    regex patten("^([^:]*): ?(.*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {
        m_header[subMatch[1]] = subMatch[2];
    }
    else {
        m_state = BODY;
    }
}

void httpRequest::parseBody(const string& line) {
    m_body = line;
    parsePost();
    m_state = FINISH;
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}

int httpRequest::converHex(char ch) {
    if(ch >= 'A' && ch <= 'F') return ch -'A' + 10;
    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
    return ch;
}

void httpRequest::parsePost() 
{
    if(m_method == "POST" && m_header["Content-Type"] == "application/x-www-form-urlencoded") 
    {
        parseFromUrlencoded();
        if(DEFAULT_HTML_TAG.count(m_path)) 
        {
            int tag = DEFAULT_HTML_TAG.find(m_path)->second;
            LOG_DEBUG("Tag:%d", tag);
            if(tag == 0 || tag == 1) 
            {
                bool isLogin = (tag == 1);
                if(userVerify(m_post["username"], m_post["password"], isLogin)) 
                {
                    m_path = "/welcome.html";
                } 
                else {
                    m_path = "/error.html";
                }
            }
        }
    }   
}

void httpRequest::parseFromUrlencoded() {
    if(m_body.size() == 0) 
    { 
        return; 
    }

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
    if(name == "" || pwd == "") { return false; }
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());

    MYSQL* sql;
    connctionRAII(&sql,  conn_pool::GetInstance());
    assert(sql);
    
    bool flag = false;
    unsigned int j = 0;
    char order[256] = { 0 };
    MYSQL_FIELD *fields = nullptr;
    MYSQL_RES *res = nullptr;
    
    if(!isLogin) { flag = true; }
    /* 查询用户及密码 */
    snprintf(order, 256, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    LOG_DEBUG("%s", order);

    if(mysql_query(sql, order)) { 
        mysql_free_result(res);
        return false; 
    }
    res = mysql_store_result(sql);
    j = mysql_num_fields(res);
    fields = mysql_fetch_fields(res);

    while(MYSQL_ROW row = mysql_fetch_row(res)) {
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        string password(row[1]);
        /* 注册行为 且 用户名未被使用*/
        if(isLogin) {
            if(pwd == password) { flag = true; }
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
        LOG_DEBUG("regirster!");
        bzero(order, 256);
        snprintf(order, 256,"INSERT INTO user(username, password) VALUES('%s','%s')", name.c_str(), pwd.c_str());
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
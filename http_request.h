
#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>
#include <errno.h>     
#include <mysql/mysql.h>  //mysql

#include "net/Buffer.h"
#include "log/log.h"
#include "sql_conn_pool.h"

using namespace net;

class httpRequest
{
public:
    enum PARSE_STATE{
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,
    };

    enum HTTP_CODE{
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
	    FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION, 
    };

public:
    httpRequest(){init();};
    ~httpRequest() = default;

    void init();
    bool parse(Buffer &buff);

    std::string path() const;   // url
    std::string &path();
    std::string method() const;
    std::string version() const;
    std::string getPost(const std::string &key) const;
    std::string getPost(const char *key) const;

    bool isKeepAlive() const;
private:
    bool parseRequestLine(const std::string &line);
    void parseHeader(const std::string &line);
    void parseBody(const std::string &line);
    void parsePath();
    void parsePost();
    void parseFromUrlencoded();

    static bool userVerify(const std::string& name, const std::string& pwd, bool isLogin);
    static int converHex(char ch);

private:
    PARSE_STATE m_state;
    std::string m_method, m_path, m_version, m_body;
    std::unordered_map<std::string, std::string> m_header;
    std::unordered_map<std::string, std::string> m_post;

    static const std::unordered_set<std::string> DEFAULT_HTML;
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;
};


#endif
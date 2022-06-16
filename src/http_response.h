#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H


#include <unordered_map>
#include <fcntl.h>       // open
#include <unistd.h>      // close
#include <sys/stat.h>    // stat
#include <sys/mman.h>    // mmap, munmap
#include "../net/Buffer.h"
#include "../base/log.h"

using namespace net;

class httpResponse
{
public:
    httpResponse(/* args */);
    ~httpResponse();

    void init(const std::string& srcDir, std::string& path, bool isKeepAlive = false, int code = -1);
    void makeResponse(Buffer& buff);
    void unmapFile();
    char* file();
    size_t fileLen() const;
    void errorContent(Buffer& buff, std::string message);
    int code() const { return m_code; }
private:
    void addStateLine(Buffer &buff);
    void addHeader(Buffer &buff);
    void addContent(Buffer &buff);

    void errorHtml();
    std::string getFileType();
private:
    int m_code;
    bool m_isKeepAlive;

    std::string m_path;
    std::string m_srcDir;
    
    char* m_mmFile; 
    struct stat m_mmFileStat;

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;
    static const std::unordered_map<int, std::string> CODE_STATUS;
    static const std::unordered_map<int, std::string> CODE_PATH;
};

#endif
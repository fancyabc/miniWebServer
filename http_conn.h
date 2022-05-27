  #ifndef HTTPCONNECTION_H_
#define HTTPCONNECTION_H_

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include<sys/uio.h>

#include "locker.h"
#include "sql_conn_pool.h"


/* 线程池模板参数类，用以封装对逻辑任务的处理 */
class http_conn
{
	public:
		static const int FILENAME_LEN = 200;	/* 文件名的最大长度 */

		static const int READ_BUFFER_SIZE = 2048;
		static const int WRITE_BUFFER_SIZE = 1024;

		enum METHOD{ GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };

		/* 主状态机的3种可能状态，分别表示：当前正在分析请求行;当前正在分析头部信息; 当前正在分析内容  */
		enum CHECK_STATE{ CHECK_STATE_REQUESTLINE = 0,
	       CHECK_STATE_HEADER, CHECK_STATE_CONTENT };

		/* 服务器处理HTTP请求的结果结果：
			NO_REQUEST 表示请求不完整，需要继续读取客户数据
			GET_REQUEST表示获得了一个完整的客户请求
			BAD_REQUEST表示客户请求有语法错误
			FORBIDDEN_REQUEST表示客户对资源没有足够的访问权限
			INTERNAL_ERROR 表示服务器内部错误
			CLOSED_CONNECTION 表示客户端已经关闭连接
		*/
		enum HTTP_CODE{ NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE,
	       FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };

		/* 从状态机的三种可能状态，即行为的读取状态，分别表示：读取到一个完整的行、行出错和行数据尚不完整 */
		enum LINE_STATUS{ LINE_OK = 0, LINE_BAD, LINE_OPEN };	/* 行的读取状态 */

	public:
		http_conn(){};
		~http_conn(){};

	public:
		/* 初始化新接受的连接 */
		void init( int sockfd, const sockaddr_in &addr );

		/* 关闭http连接 */
		void close_conn( bool real_close = true );

		/* 处理客户端请求 */
		void process();

		/* 读取浏览器发来的数据 */
		bool read();

		/* 响应报文以此写入 */
		bool write();

		sockaddr_in *get_address(){
			return &m_address;
		};

		// 同步线程初始化数据库读取表
		void initmysql_result(conn_pool *connPool);


	private:
		/* 初始化连接 */
		void init();

		/* 解析HTTP请求 */
		HTTP_CODE process_read();

		/* 填充HTTP应答 */
		bool process_write( HTTP_CODE ret );

		/* 下面这一组函数被 process_read调用以分析HTTP请求 */
		HTTP_CODE parse_request_line( char *text );
		HTTP_CODE parse_headers( char *text );
		HTTP_CODE parse_content( char *text );
		HTTP_CODE do_request( );
		char * get_line(){ return m_read_buf + m_start_line; }		// 获取新的一行在 读缓冲区的位置
		LINE_STATUS parse_line();

		/* 下面这组函数被process_write调用以填充HTTP应答 */
		void unmap();
		bool add_response( const char * format, ... );
		bool add_content( const char * content );
		bool add_status_line( int status, const char * title );
		bool add_headers( int content_length );
		bool add_content_length( int content_length );
		bool add_linger();
		bool add_blank_line();

	public:
		static int m_epollfd;	/* 所有socket上的事件都被注册到同一个epoll内核事件表，所以将epoll文件描述符设置为静态的 */

		static int m_user_count;	/* 统计用户数量 */
		MYSQL *mysql;				/* 数据库连接 */

	private:
		/* 该HTTP连接的socket和对方的socket地址 */
		int m_sockfd;
		sockaddr_in m_address;

		/* 读缓冲区 */
		char m_read_buf[ READ_BUFFER_SIZE ];

		/* 标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置 */
		int m_read_idx;

		/* 当前正在分析的字符在读缓冲区中的位置 */
		int m_checked_idx;

		/* 当前正在解析的行的起始位置 */
		int m_start_line;

		char  m_write_buf[ WRITE_BUFFER_SIZE ];
		int m_write_idx;	// 写游标位置

		int bytes_have_send;	// 已发送字节数
		int bytes_to_send;		// 待发送字节数

		/* 主状态机当前所处的状态 */
		CHECK_STATE m_check_state;
		/* 请求方法 */
		METHOD m_method;

		/* 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url  (doc_root 是网站根目录) */
		char m_real_file[ FILENAME_LEN ];

		char * m_url;		/* 客户请求的目标文件的文件名 */

		char * m_version;
		char * m_host;
		int m_content_length;		/* HTTP请求的消息体的长度 */
		bool m_linger;			/* HTTP请求是否要保持连接 */

		char * m_file_address;	/* 客请求目标文件被mmap到内存中的起始位置 */

		struct stat m_file_stat;
		/*采用writev执行写操作，m_iv_count表示被写内存块的数量*/
		struct iovec m_iv[2];
		int m_iv_count;

		int cgi;				// 是否启动CGI
		char *m_string;			// 请求头数据
};


#endif

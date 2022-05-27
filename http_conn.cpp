/* 用线程池实现一个并发的web服务器 */

#include <map>

#include "http_conn.h"
#include "./log/log.h"



/* 定义HTTP响应的一些状态信息 */
const char * ok_200_title = "OK";
const char * error_400_title = "Bad Request";
const char * error_400_form = "Your request has bad syntax or is inherently impossible to satisffy.\n";
const char * error_403_title = "Forbidden";
const char * error_403_form = "You do not have permission to get file from this server.\n";
const char * error_404_title = "Not Found";
const char * error_404_form = "The requested file was not found on this server.\n";
const char * error_500_title = "Internal Error";
const char * error_500_form = "There was an unusual problem serving the requested file.\n";

/* 网站的根目录 */
const char * doc_root = "/home/fancy/Desktop/MyProjects/miniWebServer/root";


map<string,string> users;	// 用户名 密码
locker m_lock;


/* 将数据库的用户和密码载入到服务器的map中来 */
void http_conn::initmysql_result(conn_pool *connPool)
{
	// 从连接池取一个连接
	MYSQL *mysql = NULL;
	connctionRAII mysqlcon(&mysql, connPool);

	// user 表中检索 username,passwd 数据
	if(mysql_query(mysql, "SELECT username,passwd FROM user") != 0)
	{
		//printf("SELECT error:%s\n", mysql_error(mysql));
		LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
	}

	// 从表中检索完整的结果集
	MYSQL_RES *res = mysql_store_result(mysql);

	int num_fields = mysql_num_fields(res);	// 结果集的列数

	// 所有字段结构的数组
	MYSQL_FIELD *fields = mysql_fetch_fields(res);

	// 从结果集中获取下一行，将对应的用户名和密码存入map
	while( MYSQL_ROW row = mysql_fetch_row(res) )
	{
		string temp1(row[0]);
		string temp2(row[1]);

		users[temp1] = temp2;
	}
}

int setnonblocking( int fd )
{
	int old_option = fcntl( fd, F_GETFL );
	int new_option = old_option | O_NONBLOCK;
	fcntl( fd, F_SETFL, new_option );
	return old_option;
}

void addfd( int epollfd, int fd, bool one_shot )
{
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	if( one_shot )
	{
		event.events |= EPOLLONESHOT;
	}
	epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
	setnonblocking( fd );
}


void removefd( int epollfd, int fd )
{
	epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
	close( fd );
}


void modfd( int epollfd, int fd, int ev )
{
	epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
	epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn( bool real_close )
{
	if( real_close && ( m_sockfd != -1 ) )
	{
		removefd( m_epollfd, m_sockfd );
		m_sockfd = -1;
		m_user_count--;
	}

}


void http_conn::init( int sockfd, const sockaddr_in &addr )
{
	m_sockfd = sockfd;
	m_address = addr;

	/* 下面两行为了避免TIME_WAIT 状态，仅用于调试，实际使用应该去掉 */
	int reuse = 1;
	setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );

	addfd( m_epollfd, sockfd, true );
	m_user_count++;

	init();
}


void http_conn::init()
{
	m_check_state = CHECK_STATE_REQUESTLINE;
	m_linger = false;

	m_method = GET;
	m_url = 0;
	m_version = 0;
	m_content_length = 0;
	m_host = 0;
	m_start_line = 0;
	m_checked_idx = 0;
	m_read_idx = 0;

	m_write_idx = 0;
	memset( m_read_buf, '\0', READ_BUFFER_SIZE );
	memset( m_write_buf, '\0', WRITE_BUFFER_SIZE );
	memset( m_real_file, '\0', FILENAME_LEN );
}


/* 从状态机 */
http_conn::LINE_STATUS http_conn::parse_line()
{
	char temp;
	for( ; m_checked_idx < m_read_idx; ++m_checked_idx )
	{
		temp = m_read_buf[m_checked_idx];
		if( temp == '\r' )
		{
			if( ( m_checked_idx + 1 ) == m_read_idx )
			{
				return LINE_OPEN;
			}
			else if( m_read_buf[m_checked_idx + 1] =='\n' )
			{
				m_read_buf[m_checked_idx++] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}

			return LINE_BAD;
		}
		else if( temp == '\n' )
		{
			
			if( ( m_checked_idx > 1)  && (m_read_buf[m_checked_idx - 1] =='\r') )
			{
				m_read_buf[m_checked_idx-1] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}

			return LINE_BAD;
		}
	}

	return LINE_OPEN;
}


/* 循环读取客户数据，直到无数据可读或者对方关闭连接 */
bool http_conn::read()
{
	if( m_read_idx >= READ_BUFFER_SIZE )	// 当前游标超出读缓冲区
	{
		return false;
	}

	int bytes_read = 0;
	while( true )
	{ 
		bytes_read = recv( m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );

		if( bytes_read == -1 )
		{
			// 非阻塞ET模式下，需要一次性将数据读完
			if( errno == EAGAIN || errno == EWOULDBLOCK )
			{
				break;
			}
			return false;
		}
		else if( bytes_read == 0 )	// 对端关闭连接
		{
			return false;
		}

		m_read_idx += bytes_read;	// 更新当前游标位置
	}
	return true;
}


/* 解析HTTP请求行，获得：请求方法，目标URL 以及 HTTP版本号 */
/* HTTP请求行结构：请求方法 目标URL HTTP版本号 */
http_conn::HTTP_CODE http_conn::parse_request_line( char *text )
{
	
	m_url = strpbrk( text, " \t" );	// strpbrk是在源字符串（s1）中找出最先含有搜索字符串（s2）中任一字符的位置并返回，若找不到则返回空指针。
	//m_url=\t/chapter17/user.html HTTP/1.1
	if( !m_url )
	{
		return BAD_REQUEST;
	}
	*m_url++ = '\0';	// 将 "\t" 所在位置字符设置为字符串结束符'\0'， 并将 m_url 指向它的下一个位置 m_url = /chapter17/user.html HTTP/1.1
	
	// 因为有\0的存在，method=GET
	char * method = text;
	if( strcasecmp( method, "GET" ) == 0 )	// 忽略大小比较字符串， 确定是 GET 方法（本例支支持GET方法）
	{
		m_method = GET;
	}
	else if( strcasecmp( method, "POST" ) == 0 )
	{
		m_method = POST;
		cgi = 1;
	}
	else
	{
		return BAD_REQUEST;
	}

	m_url += strspn( m_url, " \t" );	 // strspn函数返回m_url中第一个不在 "\t" 中的下标，作为偏移量加给 m_url，从而得到 url及后面的部分  //确保第一个不是空格 m_url=/chapter17/...
	m_version = strpbrk( m_url, " \t" );	// 找到上一步得到字符串里的 '\t'的位置（ 应该在url 和 HTTP版本号之间的字符位置） //m_version=\tHTTP/1.1
	
	/* 判断后续字符串合法性 */
	if( !m_version )
	{
		return BAD_REQUEST;
	}
	*m_version++ = '\0';	// 将版本号前面的'\t'替换为 字符串结束符'\0'，并后移一位，指向版本号字符串 m_version=HTTP1.1
	m_version += strspn( m_version, " \t" );	// strspn函数返回m_version中第一个不在 "\t" 中的下标，作为偏移量加给 m_version 从而得到真正可用的版本号
	
	/* 只支持HTTP 1.1 */
	if( strcasecmp( m_version, "HTTP/1.1" ) != 0 )
	{
		return BAD_REQUEST;
	}

	/* 判断m_url 的前7个字符的子串是否是"http://"，如果是则需要去掉它，让字符串以'/'开头 */
	if( strncasecmp( m_url,"http://", 7 ) == 0 )
	{
		m_url += 7;		// 
		m_url = strchr( m_url, '/' );		// 在m_url中查找'/'的第一个匹配之处
	}

	if( strncasecmp( m_url,"https://", 8 ) == 0 )
	{
		m_url += 8;		// 
		m_url = strchr( m_url, '/' );		// 在m_url中查找'/'的第一个匹配之处
	}

	/* 如果m_url是空字符串 或者 它的第一个字符不是'/',则说明这个请求是不合法的 */
	if( !m_url || m_url[0] != '/' )
	{
		return BAD_REQUEST;
	}

	/* 在此显示请求的url */
	printf("url is %s\n", m_url);
	LOG_INFO("url is %s\n", m_url);
	Log::get_instance()->flush();

	/* 默认页面 */
	if(strlen(m_url) == 1 )
		strcat(m_url, "judge.html");

	m_check_state = CHECK_STATE_HEADER;	// 修改状态 为 CHECK_STATE_HEADER
	return NO_REQUEST;
}


/* 解析HTTP请求的一个头部信息 */
http_conn::HTTP_CODE http_conn::parse_headers( char * text ) 
{
	/* 遇到空行，表示头部字段解析完毕 */
	if( text[0] == '\0' )
	{
		/* 如果HTTP请求有消息体，则还需要读取m_content_length 字节的消息体，状态机转移到 CHECK_STATE_CONTENT 状态 */
		if( m_content_length != 0 )
		{
			m_check_state = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}

		/* 否则说明我们已经得到了一个完整的HTTP请求 */
		return GET_REQUEST;
	}
	/* 处理Connection头部字段 */
	else if( strncasecmp( text, "Connection:", 11 ) == 0 )
	{
		text += 11;
		text += strspn( text, " \t" );
		if( strcasecmp( text, "keep-alive" ) == 0 )
		{
			m_linger = true;
		}
	}
	/* 处理Content-Length头部字段 */
	else if( strncasecmp( text, "Content-Length:", 15 ) == 0 )
	{
		text += 15;
		text += strspn( text, " \t" );
		m_content_length = atoi( text );
	}
	/* 处理Host头部字段 */
	else if( strncasecmp( text, "Host:", 5 ) == 0 )
	{
		text += 5;
		text += strspn( text, " \t" );
		m_host = text;
	}
	else
	{
		//printf("oop! unknow header %s\n",text);
		LOG_INFO("oop! unknow header %s\n",text);
		Log::get_instance()->flush();
	}

	return NO_REQUEST;
} 


/* 我们没有真正解析HTTP请求的消息体， 只是它是判断它是否被完整地读入了 */
http_conn::HTTP_CODE http_conn::parse_content( char *text )
{
	if( m_read_idx >= ( m_content_length + m_checked_idx ) )
	{
		text[m_content_length] = '\0';

		// POST请求中最后为输入的用户名和密码
		m_string = text;	// 这里的 消息体我们传输的是 用户名和密码
		return GET_REQUEST;
	}
	return NO_REQUEST;
}


/* 主状态机 */
http_conn::HTTP_CODE http_conn::process_read()
{
	LINE_STATUS line_status = LINE_OK;
	HTTP_CODE ret = NO_REQUEST;
	char *text = 0;

	/* 行状态为 LINE_OK（读取到一个完整的行 ）      或者     状态是CHECK_STATE_CONTENT 并且 行状态为 LINE_OK （分析报文体 的 完整一行）  */
	while( ( (m_check_state == CHECK_STATE_CONTENT)  && ( line_status == LINE_OK ) ) || ( ( line_status = parse_line() ) == LINE_OK ) )
	{
		text = get_line();		// 获取正在解析的行起始在 读缓冲区的位置
		m_start_line = m_checked_idx;	// m_start_line：当前正在解析的行的起始位置； m_checked_idx： 当前正在分析的字符在读缓冲区中的位置。 
//		printf( "got 1 http line: %s\n", text );
		LOG_INFO( "got 1 http line: %s\n", text );
		Log::get_instance()->flush();

		/* 根据状态进行对应的处理 */
		switch( m_check_state )
		{
			case CHECK_STATE_REQUESTLINE:
			{
				ret = parse_request_line( text );
				if( ret == BAD_REQUEST )
				{
					return BAD_REQUEST;
				}
				break;
			}
			case CHECK_STATE_HEADER:
			{
				ret = parse_headers( text );
				if( ret == BAD_REQUEST )
				{
					return BAD_REQUEST;
				}
				else if( ret == GET_REQUEST )
				{
					return do_request();
				}
				break;
			}
			case CHECK_STATE_CONTENT:
			{
				ret = parse_content( text );
				if( ret == GET_REQUEST )
				{
					return do_request();
				}
				line_status = LINE_OPEN;
				break;
			}
			default:
			{
				return INTERNAL_ERROR;
			}
			
		}
	}
	return NO_REQUEST;
}



http_conn::HTTP_CODE http_conn::do_request()
{
	strcpy( m_real_file, doc_root );		// 将网站根目录复制到缓冲区 m_real_file
	int len = strlen( doc_root );

	const char *p = strchr(m_url, '/');

	if( cgi == 1 && ( *(p+1) == '2' || *(p+1) == '3' ) )
	{
		char flag = m_url[1];	// 根据标志判断是否已经登陆

		char *m_url_real = (char *)malloc(sizeof(char)*200);
		strcpy(m_url_real, "/");
		strcat(m_url_real, m_url+2);
		strncpy(m_real_file+len, m_url_real, FILENAME_LEN - len -1);
		free(m_url_real);

		/* 提取用户名和密码 */
		char name[100],password[100];
		int i;

		for(i = 5; m_string[i] != '&'; ++i)
		{
			name[i-5] = m_string[i]; 
		}
		name[i-5] = '\0';

		int j = 0;
		for(i = i+10; m_string[i] != '\0'; i++,j++ )
		{
			password[j] = m_string[i];
		}
		password[j] = '\0';


		/* 同步线程登录校验 */
		if( *(p+1) == '3' )	//注册
		{
			char *sql_insert = (char *)malloc(sizeof(char) * 200);
			strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
			strcat(sql_insert,"'");
			strcat(sql_insert,name);
			strcat(sql_insert,"', '");
			strcat(sql_insert,password);
			strcat(sql_insert,"')");

			/* 查找是否有重复用户名 */
			if( users.find(name) == users.end() )	//无重复用户名
			{
				m_lock.lock();

				int res = mysql_query(mysql, sql_insert);
				users.insert(pair<string,string>(name, password));
				m_lock.unlock();

				if( !res )
				{
					strcpy(m_url,"/login.html");
				}
				else
				{
					strcpy(m_url,"/registerError.html");
				}
			}
			else
				strcpy(m_url,"/registerError.html");

		}
		else if( *(p+1) == '2' )	// 登录
		{
			if(users.find(name) != users.end() && users[name] == password )
			{
				strcpy(m_url, "/welcome.html");
			}
			else
			{
				strcpy(m_url, "/logError.html");
			}
		}
	}

	if( *(p+1) == '0')
	{
		char *m_url_real = (char *)malloc(sizeof(char)*200);
		strcpy(m_url_real, "/register.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	else if( *(p+1) == '1')
	{
		char *m_url_real = (char *)malloc(sizeof(char)*200);
		strcpy(m_url_real, "/login.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	else if( *(p+1) == '5')
	{
		char *m_url_real = (char *)malloc(sizeof(char)*200);
		strcpy(m_url_real, "/picture.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	else if( *(p+1) == '6')
	{
		char *m_url_real = (char *)malloc(sizeof(char)*200);
		strcpy(m_url_real, "/video.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	else
	{
		// 注意 strncpy 函数： 1、不能保证目标字符串以'\0'结束（源字符串大于目标字符串时）；2、源字符串较小，目标字符串较大时，将会用大量'\0'填充
		strncpy( m_real_file + len, m_url, FILENAME_LEN - len -1 );		// 客户请求的目标文件的完整路径，其内容等于（根目录+目标文件名） doc_root + m_url
	}


	/* 获取文件信息 */
	if( stat( m_real_file, &m_file_stat ) < 0 )	// stat 返回文件相关信息（缓冲区m_file_stat指向的stat结构），返回值0表示调用成功；-1表示调用失败
	{
		return NO_RESOURCE;
	}

	/* 判断文件权限： 是否有其他用户读权限 */
	if( !( m_file_stat.st_mode & S_IROTH ) )
	{
		return FORBIDDEN_REQUEST;
	}

	/* 文件类型不能是目录 */
	if( S_ISDIR( m_file_stat.st_mode ) )		// 测试宏S_ISDIR() 判断文件类型
	{
		// printf("文件类型不能是目录\n");
		return BAD_REQUEST;
	}
	int fd = open( m_real_file, O_RDONLY );		// 以只读方式打开 文件 m_real_file

	/* 在调用进程和虚拟地址间创建一个映射：起始地址内核自定（设置为NULL时）；文件字节数为m_file_stat.st_size；映射区域内可读；共享映射； 标识映射的文件描述符为fd； 映射在文件的起点  */
	m_file_address = (char *)mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );	

	close( fd );		// 关闭不再需要使用的文件标识符
	return FILE_REQUEST;		// 返回状态（告诉调用者获取文件成功）
}


/* 解除映射区域 */
void http_conn::unmap()
{
	/* 如果有映射区域，那么解除映射区域 */
	if( m_file_address )
	{
		munmap( m_file_address, m_file_stat.st_size );
		m_file_address = 0;
	}
}


/* 写HTTP响应 */
bool http_conn::write()
{
	int temp = 0;
	int bytes_have_send = 0;
	int bytes_to_send = m_write_idx;	// 要发送的字节数是 写缓冲区开始 到 当前写位置的偏移量
	if( bytes_to_send == 0 )	// 要发送的字节数为0 ？
	{
		modfd( m_epollfd, m_sockfd, EPOLLIN );	// 绑定m_sockfd到m_epollfd，关注其读事件
		init();		// 初始化连接
		return true;	// 返回调用成功 true
	}

	while( 1 )
	{
		temp = writev( m_sockfd, m_iv, m_iv_count );
		if( temp <= -1 )
		{
			
			//如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件（虽然在此期间，服务器无法连接到同一客户的下一个请求，但是可以保证连接的完整性）
			if( errno == EAGAIN )	// TCP窗口太小，暂时发不出去
			{
				modfd( m_epollfd, m_sockfd, EPOLLOUT );	// 绑定m_sockfd到m_epollfd， 关注其写事件
				return true;
			}

			/* 不是窗口的原因，说明调用失败，那么解除映射区域，并返回调用失败false */
			unmap();	
			return false;
		}

		bytes_to_send -= temp;		// 更新需要发送的字节数： 还要发送的字节数 = 要发送的字节数 - 已经写进去的字节数temp
		bytes_have_send += temp;	// 更新已经发送的字节数： 已经发送的字节数 = 已发送的字节数 + 已经写进去的字节数temp
		
		if( bytes_to_send <= bytes_have_send )	/* 已经发送完 */
		{
			/* 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接 */
			unmap();	// 解除映射

			if( m_linger )
			{
				init();
				modfd( m_epollfd, m_sockfd, EPOLLIN );
				return true;
			}
			else
			{
				modfd( m_epollfd, m_sockfd, EPOLLIN );
				return false;
			}
		}
	}
}



/* 往写缓冲中写入待发送的数据 */
bool http_conn::add_response( const char * format, ... )
{
	/* 写入的游标超出缓冲区范围，报错 */
	if( m_write_idx >= WRITE_BUFFER_SIZE )
	{
		return false;
	}
	va_list arg_list;
	va_start( arg_list, format );

	// 返回值：执行成功，返回写入到字符数组 中的字符个数（不包含终止符），最大不超过 size；执行失败，返回负值
	int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE -1 - m_write_idx, format, arg_list );

	if( len >= ( WRITE_BUFFER_SIZE -1 - m_write_idx ) ) 
	{
		return false;
	}

	m_write_idx += len;		// 在缓冲区写入了len个字符，需要将当前游标后移len
	va_end( arg_list );

	LOG_INFO("request:%s", m_write_buf);
    Log::get_instance()->flush();
	return true;
}


/* 添加响应行：报文协议及版本 状态码及报文描述 */
bool http_conn::add_status_line( int status, const char * title )
{
	return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}


/* 生成响应头 */
bool http_conn::add_headers( int content_len )
{
	add_content_length( content_len );	// 生成 Content-Length 字段，表明本次回应的数据长度
	add_linger();		// 添加 Connection 字段
	add_blank_line();	// 添加 空行
	return true;
}


bool http_conn::add_content_length( int content_len )
{
	return add_response( "Content-Length: %d\r\n", content_len );
}


bool http_conn::add_linger( )
{
	return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}


bool http_conn::add_blank_line()
{
	return add_response( "%s", "\r\n");
}


/* 生成响应体 */
bool http_conn::add_content( const char * content )
{
	return add_response( "%s", content );
}


/* 根据服务器处理HTTP请求的结果，决定返回给客户端的内容 */
bool http_conn::process_write( HTTP_CODE ret )
{
	switch( ret )
	{
		case INTERNAL_ERROR:
		{
			add_status_line( 500, error_500_title );
			add_headers( strlen( error_500_form ) );
			if( !add_content( error_500_form ) )
			{
				return false;
			}
			break;
		}
		case BAD_REQUEST:
		{
			
			add_status_line( 400, error_400_title );
			add_headers( strlen( error_400_form ) );
			if( !add_content( error_400_form ) )
			{
				return false;
			}
			break;
		}
		case NO_RESOURCE:
		{
		
			add_status_line( 404, error_404_title );
			add_headers( strlen( error_404_form ) );
			if( !add_content( error_404_form ) )
			{
				return false;
			}
			break;
		}
		case FORBIDDEN_REQUEST:
		{
			add_status_line( 403, error_403_title );
			add_headers( strlen( error_403_form ) );
			if( !add_content( error_403_form ) )
			{
				return false;
			}
			break;
		}
		case FILE_REQUEST:		// 文件请求
		{
			add_status_line( 200, ok_200_title );
			if( m_file_stat.st_size != 0 )
			{
				add_headers( m_file_stat.st_size );		// 生成响应头，Content-Length字段的值 是文件的长度
				m_iv[0].iov_base = m_write_buf;
				m_iv[0].iov_len = m_write_idx;
				m_iv[1].iov_base = m_file_address;
				m_iv[1].iov_len = m_file_stat.st_size;
				m_iv_count = 2;
				return true;
			}
			else
			{
				const char *ok_string = "<html><body></body></html>";
				add_headers( strlen(ok_string) );
				if( !add_content( ok_string ) )
				{
					return false;
				}
			}
		}
		default:
		{
			return false;
		}
	}
	m_iv[0].iov_base = m_write_buf;
	m_iv[0].iov_len = m_write_idx;
	m_iv_count = 1;
	return true;
}


/* 由线程池中的工作线程调用，这就是处理HTTP请求的入口函数 */
void http_conn::process()
{
	HTTP_CODE read_ret = process_read();

	/* 状态是NO_REQUEST 请求不完整，需要继续读取客户数据时，重新绑定m_sockfd到m_epollfd并关注其读；然后返回 */
	if( read_ret == NO_REQUEST )
	{
		modfd( m_epollfd, m_sockfd, EPOLLIN );
		return;
	}

	/* 根据返回码进行相应处理，返回后根据其状态决定是否关闭连接；绑定m_sockfd到m_epollfd并关注其写 */
	bool write_ret = process_write( read_ret );
	if( !write_ret )
	{
		close_conn( );
	}

	modfd( m_epollfd, m_sockfd, EPOLLOUT );
}

server: 
	g++ -g -o server webmain.cpp http_conn.cpp sql_conn_pool.cpp ./log/log.cpp -lpthread -lmysqlclient

clean:
	rm -r server
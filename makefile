server: 
	g++ -g -o server webmain.cpp  webserver.cpp http_conn.cpp sql_conn_pool.cpp ./log/log.cpp Utils.cpp -lpthread -lmysqlclient

clean:
	rm -r server
#把.o目标文件和 线程库文件和数据库文件链接成可执行文件
server:server.o
	g++ -o server server.o -lpthread -lsqlite3 

#把.c++文件编译成.o文件
server.o:server.c++
	g++ -c server.c++ -o server.o 
	
#清除中间文件
clean:
	-rm *.o
#清除所有文件
cleanall: 
	-rm *.o server
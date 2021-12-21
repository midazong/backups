#把.o目标文件和 线程库文件和数据库文件链接成可执行文件
client:client.o
	g++ -o client client.o -lpthread -lsqlite3 `pkg-config --libs --cflags gtk+-3.0` -w

#把.c++文件编译成.o文件
client.o:client.c++
	g++ -c client.c++ -o client.o `pkg-config --libs --cflags gtk+-3.0` -w

#清除中间文件
clean:
	-rm *.o
#清除所有文件
cleanall:
	-rm *.o client 
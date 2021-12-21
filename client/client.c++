#include<gtk/gtk.h>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <signal.h>
#include <algorithm>
#include <sys/socket.h>
#include <pthread.h>
#include <sqlite3.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <error.h>
using namespace std;

#define PORT 2021 //端口号

void errHandle(char const* msg, int line) //异常处理函数
{
    printf("%s failed in %d line: %s\n", msg, line, strerror(errno));
    exit(-1);
}
struct Msg
{
    char from[12]; //发送方姓名
    char to[12]; //接收方姓名
    char msg[1024]; //消息内容
    char filename[50]; //收发文件名
    char msgRecords[10240]; //保存消息记录
    int type; //消息类型
    int ret; //返回值
    int flag; //状态掩码（0：管理员、1：普通用户、2：禁言）
};

class User
{
public:
    User(){}
    void init(char* Name, int sock, int f) {strcpy(name, Name); serv_sock = sock; flag = f;} //初始化信息
    static void privateChat(GtkButton  *button, GtkTextBuffer* entry);//私聊
    static void groupChat(GtkButton  *button, gpointer entry); //群聊   类成员作为回调函数声明为静态类型
    static void changePw(); //修改密码
    static void delete_user(); //注销账号
    static void getRecords(); //聊天记录
    static void silenced(); //禁言
    char name[12]; //用户名
    int serv_sock; //服务器套接字
private:
    int flag=1; //（普通用户,禁言则为2）
};
int serv_sock; //服务器套接字
char msgRecord[2048]; //保存聊天消息
int online; //在线
int deleteFlag = 1; //注销标记
int sendingFlag = 0;
int sigPopFlag = 0; //SIGUSER1标识，区分不同清空
int _argc; 
char** _argv; //保存命令行参数
static pthread_mutex_t mtx1 = PTHREAD_MUTEX_INITIALIZER; //初始化打印消息的线程互斥量

User user;
struct Msg msg; //线程共享结构体

GtkWidget* window; //注册登陆窗口
GtkWidget* entry; //
GtkWidget* home;
GtkTextBuffer* bufferText; //文本框缓冲区。
GtkTextBuffer* bufferText1; 
GtkTextBuffer* bufferText2;
GtkTextBuffer* bufferText3;
GtkTextBuffer* historyBufferText; //历史记录文本框缓冲区

void pop_up(string msg) //弹窗提醒
{
	GtkWidget *dialog;
    dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, msg.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), "");
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER); //设置窗口在显示器中的位置为居中
    gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE); //弹窗窗口保持在最前
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}
void get_time(char *nt) //获取时间
{
	time_t time_ptr;   //长整型long int,适合存储日历时间类型。
	time(&time_ptr); //获取从1970-1-1,0时0分0秒到现在时刻的秒数。
	struct tm *tm_ptr = NULL; //用来保存时间和日期的结构。
	tm_ptr = localtime(&time_ptr); //把从1970-1-1,0时0分0秒到当前时间系统所偏移的秒数时间转换为本地时间
	sprintf(nt,"%d-%d-%d %d:%d:%d", tm_ptr->tm_year+1900, tm_ptr->tm_mon+1, tm_ptr->tm_mday, tm_ptr->tm_hour, tm_ptr->tm_min, tm_ptr->tm_sec); //将内容写入nt;
}
void* clnt_pthread(void* arg)
{
    int serv_sock = *((int*)arg);
    while(1)
    {
        if (online == 0)
            pthread_exit(NULL);
        
        if (read(serv_sock, &msg, sizeof(struct Msg)) == -1)
            errHandle("read", __LINE__);

        switch(msg.type)
        {
            case 4 : //修改密码的响应
                if (msg.ret == -4)
                    printf("\n密码错误，修改失败...\n"); //本线程中所有反馈信息应该由弹窗形式出现
                if (msg.ret == 0)                     //但是由于未知原因在副线程中打不开窗口，
                    printf("\n密码修改成功！\n");        //可能是因为用的不是gtk线程库，或者本就只能在主线程开窗口
                if (msg.ret == -1)                     //所以弹窗都由主线程触发，但是这样线程间存在竞争,比如很短时间内收到两个信息
                    printf("\n数据库打开失败...\n");
                break;

            case 5 : //注销响应
                if (msg.ret == -1)
                    printf("数据库打开失败...\n");
                if (msg.ret == -4)
                    printf("密码错误，注销失败...\n");
                if (msg.ret == 0)
                {
                    online = 0; //标志下线
                    printf("注销成功！\n");
                }
                break;

            case 6 : //私聊响应
                if (msg.ret == -4)
                    printf("查无此人，用户可能已注销...\n");
                if (msg.ret == -1)
                    printf("数据库打开失败...\n");
                if (msg.ret == -3)
                    printf("用户[%s]不在线...\n", msg.to);
                if (msg.ret == 0)
                    printf("消息发送成功!\n");
                
                break;
            case 7 : //群聊响应
                if (msg.ret == -1)
                    printf("数据库打开失败...\n");
                if (msg.ret == 0)
                    printf("消息发送成功!\n"); 
                    
                break;
            case 10 : //聊天记录响应
                if (msg.ret == -1)
                    printf("数据库打开失败...\n");
                if (msg.ret == -2)
                    printf("数据库操作失败...\n");

                gtk_text_buffer_set_text(GTK_TEXT_BUFFER(historyBufferText), msg.msgRecords, -1); //填充聊天记录

                break;
            case 11 : //禁言响应
                if (msg.ret == -1)
                    printf("数据库打开失败...\n");
                if (msg.ret == -2)
                    printf("数据库操作失败...\n");
                if (msg.ret == 0)
                    printf("禁言成功!\n"); 
                break;
            case 12 : //发送文件响应
                if (msg.ret == -1)
                    printf("数据库打开失败...\n文件发送失败...\n");
                if (msg.ret == -2)
                    printf("数据库操作失败...\n文件发送失败...\n");
                if (msg.ret == -3)
                {
                    printf("对方拒绝了你的文件...\n");
                    sigPopFlag = 2; //标识为对象拒绝了文件
                    kill(getpid(), SIGUSR1);
                    break;
                }
                if (msg.ret == 0) //服务器已经做好准备
                {
                    int fd = open(msg.filename, O_RDONLY); //只读方式打开文件
                    if (fd == -1)
                        errHandle("open", __LINE__);
                    int readNums, writeNums;
                    int  readSums = 0, writeSums = 0; //从本地文件中读字节和发送到服务器的字节数
                    char _time[24];
                    get_time(_time);
                    
                    while (1)
                    {
                        memset(msg.msg, 0, sizeof(msg.msg)); //清空缓冲区
                        readNums = read(fd, msg.msg, sizeof(msg.msg)); //从本地读取字节到缓冲区
                        msg.msg[readNums] = '\0'; //在文件长度处放置结束符
                        if (readNums == 0) //刚好读完
                            break;
                        if (readNums == -1)
                            errHandle("read", __LINE__);
                        get_time(_time);
                        printf("%s 从本地读取了%d字节数据\n", _time, readNums);
                        readSums += readNums; //读文件总数据
                        
                        writeNums = write(serv_sock, msg.msg, readNums); //向服务器发送文件
                        if (writeNums == -1) 
                            errHandle("write", __LINE__);
                        get_time(_time);
                        printf("%s 向服务器发送了%d字节数据\n", _time, writeNums);
                        writeSums += writeNums; //写文件总数据
                        
                        if (read(serv_sock, msg.msg, sizeof(msg.msg)) == -1) //接收反馈
                            errHandle("read", __LINE__);

                        if (strcmp(msg.msg, "read error") == 0)
                        {
                            printf("服务器读取出错\n");
                            break;
                        }
                        if (strcmp(msg.msg, "write error") == 0)
                        {
                            printf("服务器保存出错\n");
                            break;
                        }
                        if (readNums != sizeof(msg.msg)) //读到最后块数据
                            break;
                    }
                    if (strcmp(msg.msg, "success") == 0)
                    {
                        strcpy(msg.msg, "over"); 
                        if (write(serv_sock, msg.msg, sizeof(msg.msg)) == -1) //通知服务器文件发送完成
                            errHandle("write", __LINE__);
                        get_time(_time);
                        printf("%s 发送完成！ 共读取%d字节数据，发送%d字节数据\n", _time, readSums, writeSums);
                    }
                    sendingFlag = 0; //标记文件发送结束
                    close(fd); //关闭文件夹
                }
                break;

            case 101:   //私聊消息
                printf("收到一条来自用户[%s]的私聊内容: [%s]\n", msg.from, msg.msg);
                sprintf(msgRecord, "[%s]向[%s]发了一条私聊消息: [%s]", msg.from, msg.to, msg.msg); 

                kill(getpid(), SIGUSR2); //向自己发送信号1  由于未知原因，线程中不能调用弹窗函数，所以发送信号让主线程调用
                break;
                        
            case 102 : //群聊消息
            {    
                printf("收到一条来自用户[%s]的群聊内容: [%s]\n", msg.from, msg.msg);
                char _time[24]; //保存时间
                get_time(_time); //获取时间
                sprintf(msgRecord, "\n【%s】： %s\n\n", msg.from, msg.msg); 

                string s;
                for (int i = 0; i < strlen(msgRecord); i++)
                {
                    s.push_back(msgRecord[i]);
                    if (i % 64 == 0 && i != 0)
                        s.append("\n");
                }
                
                GtkTextIter end;
                if (pthread_mutex_lock(&mtx1) != 0) //对消息框加锁，保持线程同步
                    errHandle("pthread_mutex_lock", __LINE__);

                gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(bufferText1), &end); //得到当前buffer文本结束位置的ITER。
                gtk_text_buffer_insert(GTK_TEXT_BUFFER(bufferText1), &end, _time, -1); //将时间插入到私聊框中
                gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(bufferText1), &end);
                gtk_text_buffer_insert(GTK_TEXT_BUFFER(bufferText1), &end, s.c_str(), -1); //将发送的消息插入到私聊框中

                if (pthread_mutex_unlock(&mtx1) != 0) //消息框解锁
                    errHandle("pthread_mutex_unlock", __LINE__);
                break;
            }
            case 103 : //在线用户更新
            {   
                printf("在线用户更新\n");
                user.init(user.name, user.serv_sock, msg.flag); //更新禁言状态
                string s;
                for (int i = 0; i < strlen(msg.msg); i++)
                {
                    if (msg.msg[i] == ' ')
                    {
                        s.push_back('\n');
                        continue;
                    }
                    s.push_back(msg.msg[i]);
                }
                sleep(1); //等待主窗口打开再更新信息
                gtk_text_buffer_set_text(GTK_TEXT_BUFFER(bufferText2), s.c_str(), -1); //更新在线信息窗口, 在线信息框覆盖式更新，无需加锁
                break;
            }
            case 104 : //下线通知
                sigPopFlag = 0; //标识为下线通知
                printf("服务器已断开连接，即将退出客户端...\n");
                sigPopFlag = 0;
                kill(getpid(), SIGUSR1); //向自己发送信号1  由于未知原因，线程中不能调用弹窗函数，所以发送信号让主线程调用
                sleep(3); //等待服务器退出
                online = 0;	
                break;

            case 105 : //用户信息接收
                printf("用户名信息接收成功\n");
                break;

            case 106 : //接收文件提醒
                printf("收到文件\n");
                sigPopFlag = 1; //标识为收到文件
                kill(getpid(), SIGUSR1);
                break;
            case 107 : //接收文件处理
                printf("开始处理接收文件\n");
                int fd; //文件描述符
                char* filename = basename(msg.filename); //获取文件名
                char dirname[20]; //目录名
                strcpy(dirname, "./receivedDir");
                if (opendir(dirname) == NULL) //如果不存在则创建目录
                {
                    mkdir(dirname, 0775);
                    printf("目录%s创建成功\n", dirname);
                }
                
                char dir_file_name[100];
                sprintf(dir_file_name, "%s//%s", dirname, filename);
                printf("%s\n", dir_file_name);
                if (access(dir_file_name, F_OK) == -1)
                {
                    printf("文件不存在\n");
                    fd = open(dir_file_name, O_CREAT | O_EXCL | O_RDWR, 0777); //以独占方式创建文件
                    if (fd == -1)
                        errHandle("open", __LINE__);
                }
                else 
                {
                    printf("文件已存在\n");
                    int n = 0;
                    while(1)
                    {
                        if (n == 0)
                            sprintf(dir_file_name, "%s//copy_%s", dirname, filename);
                        else 
                            sprintf(dir_file_name, "%s//copy%d_%s", dirname, n, filename);

                        if (access(dir_file_name, F_OK) == -1)
                        {
                            printf("文件不存在\n");
                            fd = open(dir_file_name, O_CREAT | O_EXCL | O_RDWR, 0777); //以独占方式创建文件
                            if (fd == -1)
                                errHandle("open", __LINE__);
                            break;
                        }
                        else 
                            n++;
                    }
                }

                msg.ret = 0; //标识准备好接收文件
                msg.type = 14;
                if (write(user.serv_sock, &msg, sizeof(struct Msg)) == -1) //通知服务器准备好了
                    errHandle("write", __LINE__);

                printf("开始接收文件【%s】\n", filename);
                int readNums, writeNums;
                int readSums = 0, writeSums = 0;
                string s;
                char _time[24];
                get_time(_time);
                while (1)
                {
                    memset(msg.msg, 0, sizeof(msg.msg));
                    readNums = read(user.serv_sock, msg.msg, sizeof(msg.msg)); //从服务器读字节
                    if (readNums == -1) 
                    {
                        strcpy(msg.msg, "read error"); //读错误
                        if (write(user.serv_sock, msg.msg, sizeof(msg.msg)) == -1) //通知服务器出错
                            errHandle("write", __LINE__);
                        errHandle("read", __LINE__);
                    }
                    if (strcmp(msg.msg, "over") == 0) //文件接收完成
                        break;

                    get_time(_time);
                    printf("%s 从服务器成功接收%d字节数据\n", _time, readNums);
                    readSums += readNums;
                        
                    writeNums = write(fd, msg.msg, readNums); //把读取的数据写入文件
                    if (writeNums == -1 || (writeNums != readNums))
                    {
                        strcpy(msg.msg, "write error"); //写错误
                        if (write(user.serv_sock, msg.msg, sizeof(msg.msg)) == -1) //通知服务器出错
                            errHandle("write", __LINE__);
                        errHandle("write", __LINE__);
                    }
                    get_time(_time);
                    printf("%s 成功保存%d字节数据到本地\n", _time, writeNums);
                    writeSums += writeNums;
                    strcpy(msg.msg, "success"); //保存成功
                    if (write(user.serv_sock, msg.msg, sizeof(msg.msg)) == -1) //通知服务器继续发送
                        errHandle("write", __LINE__);
                }
                get_time(_time);
                printf("%s 文件接收完成,共接收%d字节数据，保存%d字节数据到本地\n", _time, readSums, writeSums);
                break;
        }
    }
}
GtkWidget* pw; 
void deal_changePw(GtkWidget *button, gpointer* entry)
{
    const gchar* oldpw = gtk_entry_get_text(GTK_ENTRY(entry[0])); 
	const gchar* newpw = gtk_entry_get_text(GTK_ENTRY(entry[1])); 
	if (strlen(oldpw) == 0 || strlen(newpw) == 0)
	{
		pop_up("\n信息不能为空!\n");
        gtk_widget_destroy(pw);	//关闭修改密码窗口
		return;
	}
	else if (strchr(oldpw, ' ') != NULL || strchr(newpw, ' ') != NULL)
	{
		pop_up("\n信息不能包含空格\n");
        gtk_widget_destroy(pw);	//关闭修改密码窗口
		return;
	}
    strcpy(msg.from, user.name); //填入请求修改的用户名
    strcpy(msg.msg, oldpw); //填入旧密码
    strcpy(msg.filename, newpw); //填充新密码

    msg.type = 4; //标志本消息为修改密码请求
    if (write(serv_sock, &msg, sizeof(struct Msg)) == -1) //向服务器发送改密码请求
        errHandle("write", __LINE__);
    
    usleep(500000); //POSIX线程函数与gtk弹窗不兼容，不能在线程中弹窗，所以等待线程修改完返回信息在主线程弹窗
    gtk_widget_destroy(pw);	//关闭修改密码窗口
    if (msg.ret == -4)
        pop_up("\n密码错误，修改失败...\n");
    if (msg.ret == 0)
        pop_up("\n密码修改成功！\n");
    if (msg.ret == -1)
        pop_up("\n数据库打开失败...\n");
}
void User::changePw()
{
    gtk_init(&_argc, &_argv); // 初始化
	pw = gtk_window_new(GTK_WINDOW_TOPLEVEL); // 创建修改密码窗口
	gtk_window_set_title(GTK_WINDOW(pw), ""); //设置窗口的标题
	gtk_window_set_position(GTK_WINDOW(pw), GTK_WIN_POS_CENTER); //设置窗口在显示器中的位置为居中
	gtk_widget_set_size_request(pw, 400, 300); //设置窗口大小
	gtk_window_set_resizable(GTK_WINDOW(pw), FALSE); //固定窗口的大小
	gtk_container_set_border_width (GTK_CONTAINER (pw), 80); //设置边框宽度
	g_signal_connect(pw, "destroy", G_CALLBACK(gtk_main_quit), NULL); // "destroy" 和 gtk_main_quit 连接
	
	GtkWidget* fixed = gtk_fixed_new(); //创建固定布局
    gtk_container_add(GTK_CONTAINER(pw), fixed); //添加布局到窗口

    GtkWidget* label1 = gtk_label_new("旧密码");
    GtkWidget* label2 = gtk_label_new("新密码");
    gtk_fixed_put(GTK_FIXED(fixed), label1, 0, 5); //添加到固定布局
    gtk_fixed_put(GTK_FIXED(fixed), label2, 0, 55); //添加到固定布局

    GtkWidget *oldpw = gtk_entry_new();  //创建行编辑输入旧密码	
    gtk_entry_set_max_length(GTK_ENTRY(oldpw), 12);   //设置行编辑显示最大字符的长度
    gtk_entry_set_visibility(GTK_ENTRY(oldpw), FALSE);  //密码模式输入不可见
    gtk_fixed_put(GTK_FIXED(fixed), oldpw, 50, 0); //添加到固定布局

	GtkWidget *newpw = gtk_entry_new();  //创建行编辑输入新密码	
    gtk_entry_set_max_length(GTK_ENTRY(newpw), 12);  //设置行编辑显示最大字符的长度
	gtk_entry_set_visibility(GTK_ENTRY(newpw), FALSE);  //密码模式输入不可见
    gtk_fixed_put(GTK_FIXED(fixed), newpw, 50, 50); //添加到固定布局
    
    GtkWidget *change = gtk_button_new_with_label("修改"); //创建修改按钮
	gtk_widget_set_size_request(change, 168, 30); //设置按钮大小
    gtk_fixed_put(GTK_FIXED(fixed), change, 50, 100); //添加到固定布局

	gpointer on[2]; on[0] = oldpw; on[1] = newpw; //数组保存密码作为参数
	g_signal_connect(change, "pressed", G_CALLBACK(deal_changePw), on); //点击修改
    g_signal_connect(change, "activate", G_CALLBACK(deal_changePw), on); //回车修改
    g_signal_connect(oldpw, "activate", G_CALLBACK(deal_changePw), on);
    g_signal_connect(newpw, "activate", G_CALLBACK(deal_changePw), on);
	
	gtk_widget_show_all(pw); //显示窗口全部控件
 	gtk_main();	//启动主循环

}
GtkWidget* dl;
void deal_delete(GtkWidget *button, gpointer* entry)
{
    const gchar* oldpw = gtk_entry_get_text(GTK_ENTRY(entry)); 
	if (strlen(oldpw) == 0)
	{
		pop_up("\n信息不能为空!\n");
        gtk_widget_destroy(dl);	//关闭注销窗口
		return;
	}
	else if (strchr(oldpw, ' ') != NULL)
	{
		pop_up("\n信息不能包含空格\n");
        gtk_widget_destroy(dl);	//关闭注销窗口
		return;
	}
    strcpy(msg.from, user.name); //填入请求注销的用户名
    strcpy(msg.msg, oldpw); //填入旧密码

    msg.type = 5; //标志本消息为注销请求
    if (write(serv_sock, &msg, sizeof(struct Msg)) == -1) //向服务器发送注销请求
        errHandle("write", __LINE__);
    
    usleep(500000); //POSIX线程函数与gtk弹窗不兼容，不能在线程中弹窗，所以等待线程修改完返回信息在主线程弹窗
    gtk_widget_destroy(dl);	//关闭注销窗口
    if (msg.ret == -4)
        pop_up("\n密码错误，注销失败...\n");
    if (msg.ret == 0)
    {
        pop_up("\n注销成功！\n");
        deleteFlag = 1;
    }
    if (msg.ret == -1)
        pop_up("\n数据库打开失败...\n");
}
void User::delete_user()
{
    gtk_init(&_argc, &_argv); // 初始化
	dl = gtk_window_new(GTK_WINDOW_TOPLEVEL); // 创建注销窗口
	gtk_window_set_title(GTK_WINDOW(dl), ""); //设置窗口的标题
	gtk_window_set_position(GTK_WINDOW(dl), GTK_WIN_POS_CENTER); //设置窗口在显示器中的位置为居中
	gtk_widget_set_size_request(dl, 200, 150); //设置窗口大小
	gtk_window_set_resizable(GTK_WINDOW(dl), FALSE); //固定窗口的大小
    gtk_window_set_keep_above(GTK_WINDOW(dl), TRUE); //注销窗口保持在最前
	gtk_container_set_border_width (GTK_CONTAINER (dl), 50); //设置边框宽度
	g_signal_connect(dl, "destroy", G_CALLBACK(gtk_main_quit), NULL); // "destroy" 和 gtk_main_quit 连接
	
    GtkWidget* fixed = gtk_fixed_new(); //创建固定布局
    gtk_container_add(GTK_CONTAINER(dl), fixed); //添加布局到窗口

    GtkWidget* label = gtk_label_new("密\t码");
    gtk_fixed_put(GTK_FIXED(fixed), label, 0, 5); //添加到固定布局

    GtkWidget *oldpw = gtk_entry_new();  //创建行编辑输入旧密码	
    gtk_entry_set_max_length(GTK_ENTRY(oldpw), 12);   //设置行编辑显示最大字符的长度
    gtk_entry_set_visibility(GTK_ENTRY(oldpw), FALSE);  //密码模式输入不可见
    gtk_fixed_put(GTK_FIXED(fixed), oldpw, 50, 0); //添加到固定布局
    
    GtkWidget *dlu = gtk_button_new_with_label("注销"); //创建注销按钮
	gtk_widget_set_size_request(dlu, 168, 30); //设置按钮大小
    gtk_fixed_put(GTK_FIXED(fixed), dlu, 50, 50); //添加到固定布局

	g_signal_connect(dlu, "pressed", G_CALLBACK(deal_delete), oldpw); //点击注销
    g_signal_connect(dlu, "activate", G_CALLBACK(deal_delete), oldpw); //回车注销
    g_signal_connect(oldpw, "activate", G_CALLBACK(deal_delete), oldpw); //回车注销
	
	gtk_widget_show_all(dl); //显示窗口全部控件
 	gtk_main();	//启动主循环

    if (deleteFlag == 1) //不能在注销函数内直接关闭主窗口，原因不明，可能是嵌套了三层窗口，不能在第三层关闭第一层的窗口，否则会导致栈内数据被覆盖而返回不了原地址
        gtk_widget_destroy(home);	//关闭主窗口
}
vector<string> names; //在线用户名
vector<GtkWidget*> buttons; //在线用户按钮列表
GtkWidget* privateEntry; //私聊输入
GtkWidget* selectWindow; //私聊选择窗口
char to[12]; //保存发送对象
void privateSend(GtkButton  *button, GtkTextBuffer* privateEntry) //发送私聊信息
{
    const gchar* privateText = gtk_entry_get_text(GTK_ENTRY(privateEntry)); //获取私聊行编辑文本
    string s = privateText; //方便调用算法函数
	s.erase(remove(s.begin(), s.end(), ' '), s.end()); 
	if (strlen(privateText) == 0 || s.length() == 0) //输入为空就不处理
		return;

    strcpy(msg.from, user.name); //填充用户名
    strcpy(msg.to, to); //填充发送对象
    strcpy(msg.msg, privateText); //填充私聊消息
    msg.type = 6; //标识为私聊请求
    if (write(serv_sock, &msg, sizeof(struct Msg)) == -1) //向服务器发送私聊请求
        errHandle("write", __LINE__);

    char _time[24]; //保存时间
    get_time(_time); //获取时间
    sprintf(msgRecord, "\n【%s】： %s\n\n", user.name, privateText); //格式化
    gtk_entry_set_text(GTK_ENTRY(privateEntry), ""); //发送完清空输入框
    s.clear(); //清空
    for (int i = 0; i < strlen(msgRecord); i++)
    {
        s.push_back(msgRecord[i]);
        if (i % 32 == 0 && i != 0)
            s.append("\n");
    }
 
    GtkTextIter end;
    if (pthread_mutex_lock(&mtx1) != 0) //对消息框加锁，保持线程同步
        errHandle("pthread_mutex_lock", __LINE__);

    gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(bufferText), &end); //得到当前buffer文本结束位置的ITER。
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(bufferText), &end, _time, -1); //将时间插入到私聊框中
    gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(bufferText), &end);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(bufferText), &end, s.c_str(), -1); //将发送的消息插入到私聊框中

    if (pthread_mutex_unlock(&mtx1) != 0) //消息框解锁
        errHandle("pthread_mutex_unlock", __LINE__);

    if (msg.ret == -4)
        pop_up("查无此人，用户可能已注销...\n");
    if (msg.ret == -1)
        pop_up("数据库打开失败...\n");
    if (msg.ret == -3)
        pop_up("用户不在线...\n");
}
int chatingFlag = 0;
void chatWindow_destroy(GtkButton  *button, gpointer entry) //私聊窗口退出程序
{
    chatingFlag = 0; //标记重置
    gtk_main_quit(); //退出私聊窗口
}
void privateChatWindow(GtkButton  *button, GtkWidget* entry) //私聊窗口
{
    chatingFlag = 1;
    for (int i = 0; i < buttons.size(); i++) //遍历每个按钮
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buttons[i]))) //如果按钮激活就保存对应用户名
            strcpy(to, names[i].c_str());

    gtk_widget_destroy(selectWindow);	//关闭选择窗口
    
    gtk_init(&_argc, &_argv); // 初始化
	GtkWidget* chatWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL); // 创建顶层窗口
    char username[24]; 
    sprintf(username, "用户%s", to);
	gtk_window_set_title(GTK_WINDOW(chatWindow), username); //设置窗口的标题
	gtk_window_set_position(GTK_WINDOW(chatWindow), GTK_WIN_POS_CENTER); //设置窗口在显示器中的位置为居中
    gtk_window_set_keep_above(GTK_WINDOW(chatWindow), TRUE); //私聊窗口保持在最前
	gtk_window_set_resizable(GTK_WINDOW(chatWindow), FALSE); //固定窗口的大小
	g_signal_connect(chatWindow, "destroy", G_CALLBACK(chatWindow_destroy), NULL);

    GtkWidget* table = gtk_table_new(15, 6, TRUE); //创建表格
    gtk_container_add(GTK_CONTAINER(chatWindow), table); //添加表格

    GtkWidget* scrlled_window = gtk_scrolled_window_new(NULL,NULL); //设置滑动窗口
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrlled_window), //设置滑条可见
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_table_attach_defaults(GTK_TABLE(table), scrlled_window, 0, 6, 0, 14); //设置滑动窗口位置

    GtkWidget* view = gtk_text_view_new(); //私聊信息窗口
    gtk_text_view_set_editable (GTK_TEXT_VIEW(view), FALSE ); //聊天信息窗口不可编辑
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE); //文本可见
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrlled_window), view);  //聊天框添加到滑动窗口
    bufferText = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)); //获取文本缓冲区 

    GtkWidget* send = gtk_button_new_with_label("发送"); //创建发送按钮
    gtk_table_attach_defaults(GTK_TABLE(table), send, 5, 6, 14, 15); //添加按钮到表格

    privateEntry = gtk_entry_new(); //行编辑的创建
	gtk_entry_set_max_length(GTK_ENTRY(privateEntry),128); //最大长度
	gtk_editable_set_editable(GTK_EDITABLE(privateEntry), TRUE); //输入行可编辑
	gtk_table_attach_defaults(GTK_TABLE(table), privateEntry, 0, 5, 14, 15); //设置输入行位置
    g_signal_connect(send, "pressed", G_CALLBACK(privateSend), privateEntry); //点击发送消息
    g_signal_connect(privateEntry, "activate", G_CALLBACK(privateSend), privateEntry); //回车发送消息

    gtk_widget_show_all(chatWindow);	
	gtk_main();
    
}
void filechooser(GtkWidget* widget, gpointer data) //选择发送文件
{
    sendingFlag = 1; //标识为正在发送文件

    for (int i = 0; i < buttons.size(); i++) //遍历每个按钮
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buttons[i]))) //如果按钮激活就保存对应用户名
            strcpy(to, names[i].c_str());

    gtk_widget_destroy(selectWindow);	//关闭选择窗口
    GtkWidget *dialog = gtk_file_chooser_dialog_new("打开文件", NULL, 
            GTK_FILE_CHOOSER_ACTION_OPEN,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) //点击选择文件
    {
        char* filename;
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); //获取文件名
        printf("文件目录为%s\n", filename);
        strcpy(msg.filename, filename); //填充文件名
        strcpy(msg.from, user.name); //填充发送用户名
        strcpy(msg.to, to); //填充发送对象
        msg.type = 12; //标识为发送文件
           
        if (write(serv_sock, &msg, sizeof(struct Msg)) == -1) //向服务器打招呼要发送文件
            errHandle("write", __LINE__);                 //请做好准备
        printf("通知服务器，即将开始发送文件【%s】\n", basename(filename));

        g_free(filename);
    }
    sendingFlag = 0;
    gtk_widget_destroy(dialog);

}

int buttonFlag = 0; //选择窗口打开标识，标识是发送文件还是私聊
void openSelectWindow() //打开选择用户窗口
{
    gtk_init(&_argc, &_argv); // 初始化
	selectWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL); // 创建顶层窗口
	gtk_window_set_title(GTK_WINDOW(selectWindow), ""); //设置窗口的标题
	gtk_window_set_position(GTK_WINDOW(selectWindow), GTK_WIN_POS_CENTER); //设置窗口在显示器中的位置为居中
    gtk_window_set_keep_above(GTK_WINDOW(selectWindow), TRUE); //修改密码窗口保持在最前
	gtk_window_set_resizable(GTK_WINDOW(selectWindow), FALSE); //固定窗口的大小
	g_signal_connect(selectWindow, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget* table = gtk_table_new(8, 3, TRUE); //创建表格
    gtk_container_add(GTK_CONTAINER(selectWindow), table); //添加表格

    GtkWidget* scrlled_window = gtk_scrolled_window_new(NULL,NULL); //设置滑动窗口
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrlled_window), //设置滑条可见
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_table_attach_defaults(GTK_TABLE(table), scrlled_window, 0, 3, 0, 6); //设置滑动窗口位置

    GtkWidget* butt = gtk_button_new_with_label("确定"); //创建确定按钮
    gtk_table_attach_defaults(GTK_TABLE(table), butt, 1, 2, 6, 7); //添加按钮到表格
    if (buttonFlag == 0) //打开私聊窗口
        g_signal_connect(butt, "pressed", G_CALLBACK(privateChatWindow), nullptr);
    else if (buttonFlag == 1) //打开文件选择窗口
        g_signal_connect(butt, "pressed", G_CALLBACK(filechooser), nullptr);

    GtkWidget* frame;
    frame = gtk_frame_new("选择对象");
    gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_ETCHED_OUT);
    gtk_frame_set_label_align (GTK_FRAME (frame), 0.5, 0.0); //设置标签位置
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrlled_window), frame); //添加框架到滑动窗口

    GtkWidget* fixed = gtk_fixed_new(); //创建固定布局
    gtk_container_add(GTK_CONTAINER(frame),fixed); //固定布局添加到框架

    buttons[0] = gtk_radio_button_new_with_label(NULL, names[0].c_str()); //创建第一个按钮
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttons[0]), TRUE);//默认激活第一个按钮
    gtk_fixed_put(GTK_FIXED(fixed), buttons[0], 5, 0); //第一个按钮位置

    int y = 35; //按钮纵坐标
    for (int i = 1; i < names.size(); i++) //创建后续按钮
    {
        buttons[i] = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(buttons[i-1]), names[i].c_str()); 
        gtk_fixed_put(GTK_FIXED(fixed), buttons[i], 5, y);
        y += 35; //下调其他按钮位置
    }

    gtk_widget_show_all(selectWindow);	
	gtk_main();
}
void User::privateChat(GtkButton  *button, GtkTextBuffer* entry) //私聊选择窗口
{
    if (chatingFlag == 1)
    {
        gtk_widget_destroy(selectWindow);	//关闭选择窗口
        pop_up("\n您只能打开一个私聊窗口\n");
        return;
    }
    strcpy(msg.from, user.name); //填充发送用户名
    msg.type = 8; //标识本消息为查找在线用户
    if (write(user.serv_sock, &msg, sizeof(struct Msg)) == -1) //向服务器发送查找在线用户请求
        errHandle("write", __LINE__);
    usleep(100000); //等待线程接收返回信息

    string s; //用户名缓冲区
    int nums; //在线用户数量
    names.clear(); //清空两个容器
    buttons.clear();

    for (int i = 0; i < strlen(msg.msg); i++)
    {
        if (msg.msg[i] == ' ') //如果当前字符为空，就把用户名推入names中
        {
            if (strcmp(user.name, s.c_str()) != 0) //如果用户不为自己
                names.push_back(s);
            s.clear(); //清空缓冲区
            continue;
        }
        s.push_back(msg.msg[i]); 
    }

    nums = names.size(); //获取用户数量
    if (nums == 0) 
    {
        pop_up("其他用户还没上线，\n无法发起私聊...\n");
        return;
    }
    buttons.resize(nums); //初始化按钮个数

    buttonFlag = 0; //标识为打开私聊窗口
    openSelectWindow(); //根据在线用户情况初始化选择窗口
}
void User::groupChat(GtkButton  *button, gpointer entry)
{
    if (user.flag == 2) //禁言状态
    {
        pop_up("\n您已被禁言\n");
        gtk_entry_set_text(GTK_ENTRY(entry), ""); //清空输入框
        return;
    }
	const gchar* send = gtk_entry_get_text(GTK_ENTRY(entry)); //获取输入文本
	string s = send; //方便调用算法函数
	s.erase(remove(s.begin(), s.end(), ' '), s.end()); 
	if (strlen(send) == 0 || s.length() == 0) //输入为空就不处理
		return;
	strcpy(msg.msg, send); //填充消息内容
    strcpy(msg.from, user.name); //填充用户名
    strcpy(msg.to, "all");
    msg.type = 7; //标志本消息为群聊内容
    if(write(user.serv_sock, &msg, sizeof(struct Msg)) == -1)
        errHandle("write", __LINE__);
    gtk_entry_set_text(GTK_ENTRY(entry), ""); //发送完清空输入框
}
void getHistory(GtkWidget *widget, gpointer label)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    char *value;

    if (gtk_tree_selection_get_selected(GTK_TREE_SELECTION(widget), &model, &iter))
    {
        gtk_tree_model_get(model, &iter, 0, &value,  -1); //获取用户名
        strcpy(msg.from, user.name); //填充发送用户名

        if (strcmp(value, "群聊") == 0)
            strcpy(msg.to, "all"); //如果是群聊设置成all
        else
            strcpy(msg.to, value); //填充查找消息记录对象用户名

        msg.type = 10; //标识类型为查消息记录
        if(write(user.serv_sock, &msg, sizeof(struct Msg)) == -1) //发送查找请求
            errHandle("write", __LINE__);
        g_free(value);
    }
}
void User::getRecords() //获取消息记录
{
    strcpy(msg.from, user.name); //填充发送用户名
    msg.type = 9; //标识本消息为查找所有用户
    if (write(user.serv_sock, &msg, sizeof(struct Msg)) == -1) //向服务器发送查找在线用户请求
        errHandle("write", __LINE__);
    usleep(100000); //等待线程接收返回信息

    gtk_init(&_argc, &_argv);
    GtkWidget *recordsWindow; //消息记录窗口
    GtkWidget *hpaned; //水平分栏窗口

    recordsWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL); //顶层窗口
    gtk_window_set_title(GTK_WINDOW(recordsWindow), "消息记录"); //设置窗口的标题
	gtk_window_set_position(GTK_WINDOW(recordsWindow), GTK_WIN_POS_CENTER); //设置窗口在显示器中的位置为居中
	//gtk_window_set_resizable(GTK_WINDOW(recordsWindow), FALSE); //固定窗口的大小
    gtk_window_set_keep_above(GTK_WINDOW(recordsWindow), TRUE); //私聊窗口保持在最前
    gtk_widget_set_size_request(GTK_WIDGET(recordsWindow), 800, 400); //设置大小
	g_signal_connect(recordsWindow, "destroy", G_CALLBACK(gtk_main_quit), NULL); //"destroy" 和 gtk_main_quit 连接

    hpaned = gtk_hpaned_new(); //创建水平分栏
    gtk_container_add(GTK_CONTAINER(recordsWindow), hpaned); //添加水平分栏

    GtkWidget* scrolled_window1; //左边滑动窗口
    GtkWidget* scrolled_window2; //右边滑动窗口

    scrolled_window1 = gtk_scrolled_window_new(NULL, NULL); //左滑动窗口
    scrolled_window2 = gtk_scrolled_window_new(NULL, NULL); //右滑动窗口
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window1), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC); //设置滑条可见
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window2), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkListStore* model = gtk_list_store_new(1, G_TYPE_STRING); //设置1列列表
    GtkWidget* tree_view = gtk_tree_view_new(); //创建树形视图
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window1), tree_view); //添加视图到滑动窗口
    gtk_tree_view_set_model(GTK_TREE_VIEW(tree_view), GTK_TREE_MODEL(model)); //获取列表

    GtkTreeIter iter; //迭代器
    GtkTreeSelection *selection; //选择用户列表
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view)); //获取选项
    g_signal_connect(selection, "changed", G_CALLBACK(getHistory), NULL); //点击获取聊天记录
    
    string s;
    for (int i = 0; i < strlen(msg.msg); i++)
    {
        if (msg.msg[i] == ' ') //如果当前字符为空，就把用户名推入names中
        {
            if (strcmp(user.name, s.c_str()) != 0) //如果用户不为自己
            {
                gtk_list_store_append(GTK_LIST_STORE(model), &iter); //获取新行的iter
                gtk_list_store_set(GTK_LIST_STORE(model), &iter,0, s.c_str(), -1); //插入用户名
            }
            s.clear(); //清空缓冲区
            continue;
        }
        s.push_back(msg.msg[i]); //填充用户名
    }
    gtk_list_store_append(GTK_LIST_STORE(model), &iter); //获取新行的iter
    gtk_list_store_set(GTK_LIST_STORE(model), &iter,0, "root", -1); //插入root
    gtk_list_store_append(GTK_LIST_STORE(model), &iter); //获取新行的iter
    gtk_list_store_set(GTK_LIST_STORE(model), &iter,0, "群聊", -1); //插入群聊

    GtkCellRenderer* cell = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes("用户名", cell,"text", 0,NULL);

    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), GTK_TREE_VIEW_COLUMN(column));

    GtkWidget* view = gtk_text_view_new(); //聊天记录窗口
    gtk_text_view_set_editable (GTK_TEXT_VIEW(view), FALSE ); //聊天记录窗口不可编辑
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE); //文本可见
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window2), view);  //聊天记录框添加到滑动窗口

	historyBufferText = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)); //获取聊天记录文本缓冲区    	
	
    gtk_paned_add1(GTK_PANED(hpaned), scrolled_window1); //添加到分栏左边
    gtk_paned_add2(GTK_PANED(hpaned), scrolled_window2); //添加到分栏右边

    gtk_widget_show_all(recordsWindow);
    gtk_main();
}
void sendFile_selectUser() //选择发送对象
{
    if (sendingFlag == 1)
    {
        pop_up("正在发送文件，\n请等待处理结束...\n");
        return;
    }
    strcpy(msg.from, user.name); //填充发送用户名
    msg.type = 8; //标识本消息为查找在线用户
    if (write(user.serv_sock, &msg, sizeof(struct Msg)) == -1) //向服务器发送查找在线用户请求
        errHandle("write", __LINE__);
    usleep(100000); //等待线程接收返回信息

    string s; //用户名缓冲区
    int nums; //在线用户数量
    names.clear(); //清空两个容器
    buttons.clear();

    for (int i = 0; i < strlen(msg.msg); i++)
    {
        if (msg.msg[i] == ' ') //如果当前字符为空，就把用户名推入names中
        {
            if (strcmp(user.name, s.c_str()) != 0) //如果用户不为自己
                names.push_back(s);
            s.clear(); //清空缓冲区
            continue;
        }
        s.push_back(msg.msg[i]); 
    }

    nums = names.size(); //获取用户数量
    if (nums == 0) 
    {
        pop_up("其他用户还没上线，\n无法发送文件...\n");
        return;
    }
    buttons.resize(nums); //初始化按钮个数

    buttonFlag = 1; //标识为打开文件选择
    openSelectWindow(); //根据在线用户情况初始化选择窗口
}
void home_destroy(GtkButton  *button, gpointer entry) //客户端退出程序，由于窗口关闭时向服务器发送的信号中不会携带用户名，所以需要自定义退出
{
    strcpy(msg.from, user.name); //填充用户名
    msg.type = 3; //退出标记
    if(write(user.serv_sock, &msg, sizeof(struct Msg)) == -1)
        errHandle("write", __LINE__);
    printf("成功退出!\n");
    online = 0; //退出线程
    gtk_main_quit();
}
void userWindow() //普通用户窗口
{
 	gtk_init(&_argc, &_argv); // 初始化
	home = gtk_window_new(GTK_WINDOW_TOPLEVEL); // 创建顶层窗口
    char username[24]; 
    sprintf(username, "用户%s", user.name);
	gtk_window_set_title(GTK_WINDOW(home), username); //设置窗口的标题
	gtk_window_set_position(GTK_WINDOW(home), GTK_WIN_POS_CENTER); //设置窗口在显示器中的位置为居中
	gtk_window_set_resizable(GTK_WINDOW(home), FALSE); //固定窗口的大小
	g_signal_connect(home, "destroy", G_CALLBACK(home_destroy), NULL); //"destroy" 和 gtk_main_quit 连接
	
	GtkWidget* table = gtk_table_new(16, 8, TRUE); //创建一个表格
	gtk_container_add(GTK_CONTAINER(home), table); //表格加入窗口

	entry = gtk_entry_new(); //行编辑的创建
	gtk_entry_set_max_length(GTK_ENTRY(entry),128); //最大长度
	gtk_editable_set_editable(GTK_EDITABLE(entry), TRUE); //输入行可编辑
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 0, 5, 15, 16); //设置输入行位置

	GtkWidget* send = gtk_button_new_with_label("发送"); 
	GtkWidget* changePw = gtk_button_new_with_label("修改密码");
	GtkWidget* deleteUser = gtk_button_new_with_label("注销账号");
	GtkWidget* privateChat = gtk_button_new_with_label("私聊");
	GtkWidget* sendFile = gtk_button_new_with_label("发送文件");
	GtkWidget* getRecords = gtk_button_new_with_label("消息记录");

	g_signal_connect(send, "pressed", G_CALLBACK(user.groupChat), entry); //点击发送消息
    g_signal_connect (entry, "activate", G_CALLBACK (user.groupChat), entry); //回车发送消息
	g_signal_connect(changePw, "pressed", G_CALLBACK(user.changePw), nullptr); //点击修改密码
    g_signal_connect(deleteUser, "pressed", G_CALLBACK(user.delete_user), nullptr); //点击注销账号
    g_signal_connect(privateChat, "pressed", G_CALLBACK(user.privateChat), nullptr); //点击私聊
	g_signal_connect(getRecords, "pressed", G_CALLBACK(user.getRecords), nullptr); //点击获取消息记录
	g_signal_connect(sendFile, "pressed", G_CALLBACK(sendFile_selectUser), nullptr); //点击选择文件发送
	
	gtk_table_attach_defaults(GTK_TABLE(table), send, 5, 6, 15, 16); //设置按钮位置
	gtk_table_attach_defaults(GTK_TABLE(table), changePw, 0, 1, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), deleteUser, 1, 2, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), privateChat, 2, 3, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), sendFile, 3, 4, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), getRecords, 4, 5, 0, 1);
		
    GtkWidget* frame1; //设置框架
    GtkWidget* frame2;
    GtkWidget* frame3;
    frame1 = gtk_frame_new(NULL); //创建框架设置标题
    frame2 = gtk_frame_new("当前在线用户");
    frame3 = gtk_frame_new("私信");
    gtk_frame_set_label_align (GTK_FRAME (frame2), 0.5, 0.0); //设置标签位置
    gtk_frame_set_label_align (GTK_FRAME (frame3), 0.5, 0.0);
    gtk_frame_set_shadow_type(GTK_FRAME(frame1),GTK_SHADOW_ETCHED_OUT); //框架类型
    gtk_frame_set_shadow_type(GTK_FRAME(frame2),GTK_SHADOW_ETCHED_OUT);
    gtk_frame_set_shadow_type(GTK_FRAME(frame3),GTK_SHADOW_ETCHED_OUT);
    gtk_table_attach_defaults(GTK_TABLE(table), frame1, 0, 6, 1, 15); //设置框架位置
	gtk_table_attach_defaults(GTK_TABLE(table), frame2, 6, 8, 0, 6);
	gtk_table_attach_defaults(GTK_TABLE(table), frame3, 6, 8, 6, 16);

	GtkWidget* scrlled_window1 = gtk_scrolled_window_new(NULL,NULL); //设置滑动窗口1
	GtkWidget* scrlled_window2 = gtk_scrolled_window_new(NULL,NULL); 
	GtkWidget* scrlled_window3 = gtk_scrolled_window_new(NULL,NULL); 
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrlled_window1), //设置滑条可见
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC); 
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrlled_window2),
                                   GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrlled_window3),
                                   GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(frame1), scrlled_window1); //滑动窗口添加到框架
    gtk_container_add(GTK_CONTAINER(frame2), scrlled_window2);
    gtk_container_add(GTK_CONTAINER(frame3), scrlled_window3);

	GtkWidget* view1 = gtk_text_view_new(); //群聊天窗口
	GtkWidget* view2 = gtk_text_view_new(); //在线用户
	GtkWidget* view3 = gtk_text_view_new(); //私信窗口
    gtk_text_view_set_editable (GTK_TEXT_VIEW(view1), FALSE ); //聊天信息窗口不可编辑
    gtk_text_view_set_editable (GTK_TEXT_VIEW(view2), FALSE );
    gtk_text_view_set_editable (GTK_TEXT_VIEW(view3), FALSE );
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view1), FALSE); //文本可见
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view2), FALSE); 
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view3), FALSE);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrlled_window1), view1);  //聊天框添加到滑动窗口
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrlled_window2), view2);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrlled_window3), view3);

	bufferText1=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view1)); //获取文本缓冲区    	
	bufferText2=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view2)); 
	bufferText3=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view3));
    
	gtk_widget_show_all(home);	
	gtk_main();
}
GtkWidget* silenceWindow;
void deal_silence() //处理禁言
{
    string s;
    for (int i = 0; i < names.size(); i++) //遍历多选按钮
    {
        if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buttons[i])))
        {
            s.append(names[i]); //添加到禁言名单
            s.append(" "); //分割用户名
        }
    }
    strcpy(msg.from, user.name); //填充发送用户名
    msg.type = 11; //标识本消息为禁言请求
    strcpy(msg.msg, s.c_str()); //填充禁言对象
    if (write(user.serv_sock, &msg, sizeof(struct Msg)) == -1) //向服务器发送禁言请求
        errHandle("write", __LINE__);
    gtk_widget_destroy(silenceWindow);	//关闭禁言窗口
}
void User::silenced()
{
    strcpy(msg.from, user.name); //填充发送用户名
    msg.type = 9; //标识本消息为查找所有用户
    if (write(user.serv_sock, &msg, sizeof(struct Msg)) == -1) //向服务器发送查找所有注册用户请求
        errHandle("write", __LINE__);
    usleep(100000); //等待线程接收返回信息

    string s; //用户名缓冲区
    int nums; //用户数量
    names.clear(); //清空两个容器
    buttons.clear();

    for (int i = 0; i < strlen(msg.msg); i++)
    {
        if (msg.msg[i] == ' ') //如果当前字符为空，就把用户名推入names中
        {
            names.push_back(s);
            s.clear(); //清空缓冲区
            continue;
        }
        s.push_back(msg.msg[i]); 
    }

    nums = names.size(); //获取用户数量
    if (nums == 0) 
    {
        pop_up("\n目前还没有用户注册...\n");
        return;
    }
    buttons.resize(nums); //初始化按钮个数

    gtk_init(&_argc,&_argv);
    silenceWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL); // 创建顶层窗口
	gtk_window_set_position(GTK_WINDOW(silenceWindow), GTK_WIN_POS_CENTER); //设置窗口在显示器中的位置为居中
	gtk_window_set_resizable(GTK_WINDOW(silenceWindow), FALSE); //固定窗口的大小
	g_signal_connect(silenceWindow, "destroy", G_CALLBACK(gtk_main_quit), NULL); //"destroy" 和 gtk_main_quit 连接

    GtkWidget* table = gtk_table_new(8, 3, TRUE); //创建表格
    gtk_container_add(GTK_CONTAINER(silenceWindow), table); //添加表格

    GtkWidget* scrlled_window = gtk_scrolled_window_new(NULL,NULL); //设置滑动窗口
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrlled_window), //设置滑条可见
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_table_attach_defaults(GTK_TABLE(table), scrlled_window, 0, 3, 0, 6); //设置滑动窗口位置

    GtkWidget* butt = gtk_button_new_with_label("确定"); //创建确定按钮
    gtk_table_attach_defaults(GTK_TABLE(table), butt, 1, 2, 6, 7); //添加按钮到表格
    g_signal_connect(butt, "pressed", G_CALLBACK(deal_silence), nullptr);

    GtkWidget* frame;
    frame = gtk_frame_new("选择禁言对象");
    gtk_frame_set_shadow_type(GTK_FRAME(frame),GTK_SHADOW_ETCHED_OUT);
    gtk_frame_set_label_align (GTK_FRAME (frame), 0.5, 0.0); //设置标签位置
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrlled_window), frame); //添加框架到滑动窗口

    GtkWidget* fixed = gtk_fixed_new(); //创建固定布局
    gtk_container_add(GTK_CONTAINER(frame),fixed); //固定布局添加到框架

    int y = 0;
    for (int i = 0; i < nums; i++) //创建多选按钮
    {
        bool silenceFlag = FALSE; 
        buttons[i] = gtk_check_button_new_with_label(names[i].c_str()); //创建按钮 
        if (msg.msgRecords[i] == '2')
            silenceFlag = TRUE;
        
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttons[i]), silenceFlag);
        gtk_fixed_put(GTK_FIXED(fixed), buttons[i], 5, y);
        y += 35;
    }
    
    gtk_widget_show_all(silenceWindow);
    gtk_main();
}
void adminWindow() //管理员窗口
{
    gtk_init(&_argc, &_argv); // 初始化
	home = gtk_window_new(GTK_WINDOW_TOPLEVEL); // 创建顶层窗口
    char username[24]; 
    sprintf(username, "用户%s", user.name);
	gtk_window_set_title(GTK_WINDOW(home), username); //设置窗口的标题
	gtk_window_set_position(GTK_WINDOW(home), GTK_WIN_POS_CENTER); //设置窗口在显示器中的位置为居中
	gtk_window_set_resizable(GTK_WINDOW(home), FALSE); //固定窗口的大小
	g_signal_connect(home, "destroy", G_CALLBACK(home_destroy), NULL); //"destroy" 和 gtk_main_quit 连接
	
	GtkWidget* table = gtk_table_new(16, 8, TRUE); //创建一个表格
	gtk_container_add(GTK_CONTAINER(home), table); //表格加入窗口

	entry = gtk_entry_new(); //行编辑的创建
	gtk_entry_set_max_length(GTK_ENTRY(entry),128); //最大长度
	gtk_editable_set_editable(GTK_EDITABLE(entry), TRUE); //输入行可编辑
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 0, 5, 15, 16); //设置输入行位置

	GtkWidget* send = gtk_button_new_with_label("发送"); 
	GtkWidget* changePw = gtk_button_new_with_label("修改密码");
	GtkWidget* silence = gtk_button_new_with_label("禁言");
	GtkWidget* privateChat = gtk_button_new_with_label("私聊");
	GtkWidget* sendFile = gtk_button_new_with_label("发送文件");
	GtkWidget* getRecords = gtk_button_new_with_label("消息记录");

	g_signal_connect(send, "pressed", G_CALLBACK(user.groupChat), entry); //点击发送消息
    g_signal_connect (entry, "activate", G_CALLBACK (user.groupChat), entry); //回车发送消息
	g_signal_connect(changePw, "pressed", G_CALLBACK(user.changePw), nullptr); //点击修改密码
    g_signal_connect(silence, "pressed", G_CALLBACK(user.silenced), nullptr); //点击禁言
    g_signal_connect(privateChat, "pressed", G_CALLBACK(user.privateChat), nullptr); //点击私聊
	g_signal_connect(getRecords, "pressed", G_CALLBACK(user.getRecords), nullptr); //点击获取消息记录
    g_signal_connect(sendFile, "pressed", G_CALLBACK(sendFile_selectUser), nullptr); //点击选择文件发送
	
	gtk_table_attach_defaults(GTK_TABLE(table), send, 5, 6, 15, 16); //设置按钮位置
	gtk_table_attach_defaults(GTK_TABLE(table), changePw, 0, 1, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), silence, 1, 2, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), privateChat, 2, 3, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), sendFile, 3, 4, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), getRecords, 4, 5, 0, 1);
		
    GtkWidget* frame1; //设置框架
    GtkWidget* frame2;
    GtkWidget* frame3;
    frame1 = gtk_frame_new(NULL); //创建框架设置标题
    frame2 = gtk_frame_new("当前在线用户");
    frame3 = gtk_frame_new("私信");
    gtk_frame_set_label_align (GTK_FRAME (frame2), 0.5, 0.0); //设置标签位置
    gtk_frame_set_label_align (GTK_FRAME (frame3), 0.5, 0.0);
    gtk_frame_set_shadow_type(GTK_FRAME(frame1),GTK_SHADOW_ETCHED_OUT); //框架类型
    gtk_frame_set_shadow_type(GTK_FRAME(frame2),GTK_SHADOW_ETCHED_OUT);
    gtk_frame_set_shadow_type(GTK_FRAME(frame3),GTK_SHADOW_ETCHED_OUT);
    gtk_table_attach_defaults(GTK_TABLE(table), frame1, 0, 6, 1, 15); //设置框架位置
	gtk_table_attach_defaults(GTK_TABLE(table), frame2, 6, 8, 0, 6);
	gtk_table_attach_defaults(GTK_TABLE(table), frame3, 6, 8, 6, 16);

	GtkWidget* scrlled_window1 = gtk_scrolled_window_new(NULL,NULL); //设置滑动窗口1
	GtkWidget* scrlled_window2 = gtk_scrolled_window_new(NULL,NULL); 
	GtkWidget* scrlled_window3 = gtk_scrolled_window_new(NULL,NULL); 
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrlled_window1), //设置滑条可见
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC); 
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrlled_window2),
                                   GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrlled_window3),
                                   GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(frame1), scrlled_window1); //滑动窗口添加到框架
    gtk_container_add(GTK_CONTAINER(frame2), scrlled_window2);
    gtk_container_add(GTK_CONTAINER(frame3), scrlled_window3);

	GtkWidget* view1 = gtk_text_view_new(); //群聊天窗口
	GtkWidget* view2 = gtk_text_view_new(); //在线用户
	GtkWidget* view3 = gtk_text_view_new(); //私信窗口
    gtk_text_view_set_editable (GTK_TEXT_VIEW(view1), FALSE ); //聊天信息窗口不可编辑
    gtk_text_view_set_editable (GTK_TEXT_VIEW(view2), FALSE );
    gtk_text_view_set_editable (GTK_TEXT_VIEW(view3), FALSE );
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view1), FALSE); //文本可见
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view2), FALSE); 
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view3), FALSE);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrlled_window1), view1);  //聊天框添加到滑动窗口
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrlled_window2), view2);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrlled_window3), view3);

	bufferText1=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view1)); //获取文本缓冲区    	
	bufferText2=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view2)); 
	bufferText3=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view3));
    
	gtk_widget_show_all(home);	
	gtk_main();
}
GtkWidget *passwdLine; //便于登陆密码错误时清空密码
void deal_login(GtkWidget *button, gpointer* entry) //处理登录
{
	const gchar* name = gtk_entry_get_text(GTK_ENTRY(entry[0])); 
	const gchar* passwd = gtk_entry_get_text(GTK_ENTRY(entry[1])); 
	if (strlen(name) == 0 || strlen(passwd) == 0)
	{
		pop_up("\n信息不能为空!\n");
		return;
	}
	else if (strchr(name, ' ') != NULL || strchr(passwd, ' ') != NULL)
	{
		pop_up("\n信息不能包含空格\n");
        gtk_entry_set_text(GTK_ENTRY(passwdLine), ""); //清空密码框
		return;
	}

	strcpy(msg.from, name); //填入登录用户名
    strcpy(msg.msg, passwd); //填入登录密码
    msg.type = 2; //标志本消息为登录请求
    if (write(serv_sock, &msg, sizeof(struct Msg)) == -1) //向服务器发送登录请求
        errHandle("write", __LINE__);
    if (read(serv_sock, &msg, sizeof(struct Msg)) == -1) //从服务器接收登录回应
        errHandle("read", __LINE__);
    
    if (msg.type == 0) //成功登陆
    {
        printf("验证通过，正在登录...\n");

        pthread_t ptd;
        if (pthread_create(&ptd,nullptr, clnt_pthread, &serv_sock) != 0) //创建线程处理来自服务器的消息
            errHandle("pthread_create", __LINE__);
        if (pthread_detach(ptd) != 0)
            errHandle("pthread_detach", __LINE__);

        online = 1; //登录上线
		gtk_widget_destroy(window);	//关闭登录窗口
        user.init(msg.from, serv_sock, msg.flag); //初始化用户
		if (msg.flag == 0) //管理员 
			adminWindow(); //进入管理员操作面板

		else if (msg.flag == 1 || msg.flag == 2) //普通用户
			userWindow(); //进入普通用户操作面板

    }
    else //登陆失败
    {
        if (msg.type == -1)
            pop_up("\n验证失败，数据库打开失败...\n");
        if (msg.type == -2)
            pop_up("\n验证失败，数据库操作失败...\n");
        if (msg.type == -3)
            pop_up("\n登录失败，此账号已在别处登录...\n");
        if (msg.type == -4)
            pop_up("\n验证失败，请重新确认信息...\n");
    
	    gtk_entry_set_text(GTK_ENTRY(passwdLine), ""); //登陆失败就清空密码框
    }
}
void deal_regist(GtkWidget *button, gpointer* entry) //处理注册
{
	const gchar* name = gtk_entry_get_text(GTK_ENTRY(entry[0])); 
	const gchar* passwd = gtk_entry_get_text(GTK_ENTRY(entry[1])); 
	if (strlen(name) == 0 || strlen(passwd) == 0)
	{
		pop_up("\n信息不能为空!\n");
		return;
	}
	else if (strchr(name, ' ') != NULL || strchr(passwd, ' ') != NULL)
	{
		pop_up("\n信息不能包含空格\n");
        gtk_entry_set_text(GTK_ENTRY(passwdLine), ""); //清空密码框
		return;
	}
	
	strcpy(msg.from, name); //填入注册用户名
	strcpy(msg.msg, passwd); //填入注册密码
	msg.flag = 1; //所有人只能注册普通用户身份
	msg.type = 1; //标志本信息为注册请求
	if (write(serv_sock, &msg, sizeof(struct Msg)) == -1) //向服务器发送注册请求
        errHandle("write", __LINE__);
    if (read(serv_sock, &msg, sizeof(struct Msg)) == -1) //从服务器接收注册回应
        errHandle("read", __LINE__);
	
	if (msg.type == 0) //注册成功不清空以便直接登陆
        pop_up("\n注册成功!\n");
    else 
    {
        if (msg.type == -1)
            pop_up("\n注册失败...数据库打开失败...\n");
        else if (msg.type == -2)
            pop_up("\n注册失败，该用户已被注册...\n");

        gtk_entry_set_text(GTK_ENTRY(passwdLine), ""); //注册失败就清空密码框
    }
}
void init(int argc,char *argv[]) //初始窗口
{
    deleteFlag = 0; //注销重新打开本窗口，否则退出
	gtk_init(&argc, &argv);	// 初始化	
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL); //创建顶层窗口
	gtk_window_set_title(GTK_WINDOW(window), ""); //设置窗口的标题
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER); //设置窗口在显示器中的位置为居中
	gtk_widget_set_size_request(window, 400, 300); //设置窗口大小
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE); //固定窗口的大小
	gtk_container_set_border_width (GTK_CONTAINER (window), 80); //设置边框宽度
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL); // "destroy" 和 gtk_main_quit 连接
	
    GtkWidget* fixed = gtk_fixed_new(); //创建固定布局
    gtk_container_add(GTK_CONTAINER(window), fixed); //添加布局到窗口

    GtkWidget* label1 = gtk_label_new("用户名\t");
    GtkWidget* label2 = gtk_label_new("密\t码");
    gtk_fixed_put(GTK_FIXED(fixed), label1, 0, 5); //添加到固定布局
    gtk_fixed_put(GTK_FIXED(fixed), label2, 0, 55); //添加到固定布局

    GtkWidget *name = gtk_entry_new();  //创建行编辑输入账号	

    gtk_entry_set_max_length(GTK_ENTRY(name), 12);   //设置行编辑显示最大字符的长度
    gtk_fixed_put(GTK_FIXED(fixed), name, 50, 0); //添加到固定布局

	passwdLine = gtk_entry_new();  //创建行编辑输入密码	
    gtk_entry_set_max_length(GTK_ENTRY(passwdLine), 12);  //设置行编辑显示最大字符的长度
	gtk_entry_set_visibility(GTK_ENTRY(passwdLine), FALSE);  //密码模式输入不可见
    gtk_fixed_put(GTK_FIXED(fixed), passwdLine, 50, 50); //添加到固定布局
    
    GtkWidget *login = gtk_button_new_with_label("登录"); //创建登录按钮
    gtk_fixed_put(GTK_FIXED(fixed), login, 50, 100); //添加到固定布局
	
    GtkWidget *regist = gtk_button_new_with_label("注册"); //创建注册按钮
    gtk_fixed_put(GTK_FIXED(fixed), regist, 155, 100); //添加到固定布局

	gpointer ap[2]; ap[0] = name; ap[1] = passwdLine; //数组保存账号密码作为参数
	g_signal_connect(login, "pressed", G_CALLBACK(deal_login), ap); //点击登陆
	g_signal_connect(regist, "pressed", G_CALLBACK(deal_regist), ap); //点击注册
    g_signal_connect (login, "activate", G_CALLBACK (deal_login), ap); //回车登陆
    g_signal_connect (regist, "activate", G_CALLBACK (deal_regist), ap); //回车注册
	gtk_widget_show_all(window); //显示窗口全部控件
 	gtk_main();	//启动主循环
}
void sigHandle(int sig) //中断信号处理器
{
    strcpy(msg.from, user.name); //填充用户名
    msg.type = 3; //退出标记
    if(write(user.serv_sock, &msg, sizeof(struct Msg)) == -1)
        errHandle("write", __LINE__);
    printf("成功退出\n");
    sleep(1);
    exit(0);
}
GtkWidget* popWindow;

void refuseFile(GtkWidget *button, gpointer* entry) //拒绝文件
{
    msg.type = 13; //标识拒绝接收文件

    if(write(user.serv_sock, &msg, sizeof(struct Msg)) == -1) //通知服务器拒绝接收文件
        errHandle("write", __LINE__);

    printf("文件已拒绝\n");

    sigPopFlag = 0;
    gtk_widget_destroy(popWindow);
}
void acceptFile(GtkWidget *button, gpointer* entry) //接受文件
{
    msg.type = 14; //标识为接受文件
    if(write(user.serv_sock, &msg, sizeof(struct Msg)) == -1) //通知服务器拒绝接收文件
        errHandle("write", __LINE__);

    printf("文件已接受\n");

    sigPopFlag = 0;
    gtk_widget_destroy(popWindow);
}
void sigPop(int sig) //SIGUSER1
{
    if (sigPopFlag == 0) //服务器断开
    {
        pop_up("\n服务器已断开连接，\n即将退出客户端!\n");
        gtk_widget_destroy(home);
        close(serv_sock);
        exit(0);
    }
    if (sigPopFlag == 1) //收到文件提醒
    {
        gtk_init(&_argc, &_argv);	// 初始化	
        popWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL); //创建顶层窗口
        gtk_window_set_title(GTK_WINDOW(popWindow), ""); //设置窗口的标题
        gtk_window_set_position(GTK_WINDOW(popWindow), GTK_WIN_POS_CENTER); //设置窗口在显示器中的位置为居中
        gtk_widget_set_size_request(popWindow, 330, 200); //设置窗口大小
        gtk_window_set_resizable(GTK_WINDOW(popWindow), FALSE); //固定窗口的大小
        g_signal_connect(popWindow, "destroy", G_CALLBACK(gtk_main_quit), NULL); 
        
        GtkWidget* fixed = gtk_fixed_new(); //创建固定布局
        gtk_container_add(GTK_CONTAINER(popWindow), fixed); //添加布局到窗口

        char fileFrom[50];
        char fileName[80];
        char fileSize[20];
        sprintf(fileFrom, "来自用户 【%s】\n", msg.from);
        sprintf(fileName, "文件名  【%s】\n", basename(msg.filename));
        sprintf(fileSize, "大\t小   %s字节\n", msg.msg);
        GtkWidget* label1 = gtk_label_new("您收到一个文件");
        GtkWidget* label2 = gtk_label_new(fileFrom);
        GtkWidget* label3 = gtk_label_new(fileName);
        GtkWidget* label4 = gtk_label_new(fileSize);
        GtkWidget *accept = gtk_button_new_with_label("接受"); //创建接受按钮
        GtkWidget *refuse = gtk_button_new_with_label("拒绝"); //创建拒绝按钮
        gtk_fixed_put(GTK_FIXED(fixed), label1, 110, 20); //添加到固定布局
        gtk_fixed_put(GTK_FIXED(fixed), label2, 70, 50); //添加到固定布局
        gtk_fixed_put(GTK_FIXED(fixed), label3, 70, 80); //添加到固定布局
        gtk_fixed_put(GTK_FIXED(fixed), label4, 70, 110); //添加到固定布局
        gtk_fixed_put(GTK_FIXED(fixed), accept, 50, 140); //添加到固定布局
        gtk_fixed_put(GTK_FIXED(fixed), refuse, 220, 140); //添加到固定布局
        
        g_signal_connect(accept, "pressed", G_CALLBACK(acceptFile), nullptr); //点击接受
        g_signal_connect(refuse, "pressed", G_CALLBACK(refuseFile), nullptr); //点击拒绝

        gtk_widget_show_all(popWindow); //显示窗口全部控件
        gtk_main();	//启动主循环
    }
    if (sigPopFlag == 2) //对象拒绝文件提醒
        pop_up("\n对方拒绝了您的文件...\n");
    
}
void sigPrivateChat(int sig) //私聊信号处理器   SIGUSER2
{
    if (chatingFlag == 1) //如果用户正在私聊
    {
        if (strcmp(to, msg.from) == 0) //如果是用户当前私聊对象发来的消息
        {
            char _time[24]; //保存时间
            get_time(_time); //获取时间
            sprintf(msgRecord, "\n【%s】： %s\n\n", msg.from, msg.msg); //格式化
            string s;
            for (int i = 0; i < strlen(msgRecord); i++)
            {
                s.push_back(msgRecord[i]);
                if (i % 32 == 0 && i != 0)
                    s.append("\n");
            }

            GtkTextIter end;
            if (pthread_mutex_lock(&mtx1) != 0) //对消息框加锁，保持线程同步
                errHandle("pthread_mutex_lock", __LINE__);
            gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(bufferText), &end); //得到当前buffer文本结束位置的ITER。
            gtk_text_buffer_insert(GTK_TEXT_BUFFER(bufferText), &end, _time, -1); //将时间插入到私聊框中
            gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(bufferText), &end);
            gtk_text_buffer_insert(GTK_TEXT_BUFFER(bufferText), &end, s.c_str(), -1); //将收到的消息插入到私聊框中
            if (pthread_mutex_unlock(&mtx1) != 0) //消息框解锁
                errHandle("pthread_mutex_unlock", __LINE__);
        }
        else //如果用户正在跟别人聊天
        {
            char _time[24]; //保存时间
            get_time(_time); //获取时间
            sprintf(msgRecord, "\n【%s】： %s\n\n",msg.from, msg.msg); //格式化
            string s;
            for (int i = 0; i < strlen(msgRecord); i++)
            {
                s.push_back(msgRecord[i]);
                if (i % 16 == 0 && i != 0)
                    s.append("\n");
            }

            GtkTextIter end;
            if (pthread_mutex_lock(&mtx1) != 0) //对消息框加锁，保持线程同步
                errHandle("pthread_mutex_lock", __LINE__);
            gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(bufferText3), &end); //得到当前buffer文本结束位置的ITER。
            gtk_text_buffer_insert(GTK_TEXT_BUFFER(bufferText3), &end, _time, -1); //将时间插入到私信框中
            gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(bufferText3), &end); 
            gtk_text_buffer_insert(GTK_TEXT_BUFFER(bufferText3), &end, s.c_str(), -1); //j将消息插入私信框
            if (pthread_mutex_unlock(&mtx1) != 0) //消息框解锁
                errHandle("pthread_mutex_unlock", __LINE__);
        }
    }
    else if(chatingFlag == 0)
    {
        chatingFlag = 1; //正在私聊
        strcpy(to, msg.from); //把当前聊天用户同步
        gtk_widget_destroy(selectWindow);	//关闭选择窗口
        
        gtk_init(&_argc, &_argv); // 初始化
        GtkWidget* chatWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL); // 创建顶层窗口
        char username[24]; 
        sprintf(username, "用户%s", msg.from);
        gtk_window_set_title(GTK_WINDOW(chatWindow), username); //设置窗口的标题
        gtk_window_set_position(GTK_WINDOW(chatWindow), GTK_WIN_POS_CENTER); //设置窗口在显示器中的位置为居中
        gtk_window_set_keep_above(GTK_WINDOW(chatWindow), TRUE); //私聊窗口保持在最前
        gtk_window_set_resizable(GTK_WINDOW(chatWindow), FALSE); //固定窗口的大小
        g_signal_connect(chatWindow, "destroy", G_CALLBACK(chatWindow_destroy), NULL);

        GtkWidget* table = gtk_table_new(15, 6, TRUE); //创建表格
        gtk_container_add(GTK_CONTAINER(chatWindow), table); //添加表格

        GtkWidget* scrlled_window = gtk_scrolled_window_new(NULL,NULL); //设置滑动窗口
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrlled_window), //设置滑条可见
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
        gtk_table_attach_defaults(GTK_TABLE(table), scrlled_window, 0, 6, 0, 14); //设置滑动窗口位置

        GtkWidget* view = gtk_text_view_new(); //私聊信息窗口
        gtk_text_view_set_editable (GTK_TEXT_VIEW(view), FALSE ); //聊天信息窗口不可编辑
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE); //文本可见
        gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrlled_window), view);  //聊天框添加到滑动窗口
        bufferText = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)); //获取文本缓冲区 

        GtkWidget* send = gtk_button_new_with_label("发送"); //创建发送按钮
        gtk_table_attach_defaults(GTK_TABLE(table), send, 5, 6, 14, 15); //添加按钮到表格

        privateEntry = gtk_entry_new(); //行编辑的创建
        gtk_entry_set_max_length(GTK_ENTRY(privateEntry),100); //最大长度
        gtk_editable_set_editable(GTK_EDITABLE(privateEntry), TRUE); //输入行可编辑
        gtk_table_attach_defaults(GTK_TABLE(table), privateEntry, 0, 5, 14, 15); //设置输入行位置
        g_signal_connect(send, "pressed", G_CALLBACK(privateSend), privateEntry); //点击发送消息
        g_signal_connect(privateEntry, "activate", G_CALLBACK(privateSend), privateEntry); //回车发送消息

        gtk_widget_show_all(chatWindow);	

        kill(getpid(), SIGUSR2); //再次发送私聊信号

        gtk_main();
    }
}
int main(int argc, char* argv[])
{
	_argc = argc;
	_argv = (char**)malloc(argc * sizeof(char*)) ;
	for (int i = 0; i < argc; i++) //将命令行参数传到全局变量
        _argv[i] = argv[i];

    struct sigaction sa; //创建信号句柄
    sigemptyset(&sa.sa_mask); //阻塞掩码置空
    sa.sa_flags = 0; //信号处理过程标记
    sa.sa_handler = sigHandle; //设置信号处理函数
    if (sigaction(SIGINT, &sa, NULL) == -1) //设置中断信号处理ctrl -c
        errHandle("sigaction", __LINE__);
    if (sigaction(SIGQUIT, &sa, NULL) == -1) //设置退出信号处理 ctrl -\  /
        errHandle("sigaction", __LINE__);

    struct sigaction sa_pop; //创建弹窗信号句柄
    sigemptyset(&sa_pop.sa_mask); //阻塞掩码置空
    sa_pop.sa_flags = 0; //信号处理过程标记
    sa_pop.sa_handler = sigPop; //设置弹窗信号处理函数
    if (sigaction(SIGUSR1, &sa_pop, NULL) == -1) //设置自定义弹窗信号处理器
        errHandle("sigaction", __LINE__);

    struct sigaction sa_private; //创建私聊信号句柄
    sigemptyset(&sa_private.sa_mask); //阻塞掩码置空
    sa_private.sa_flags = 0; //信号处理过程标记
    sa_private.sa_handler = sigPrivateChat; //设置私聊信号处理函数
    if (sigaction(SIGUSR2, &sa_private, NULL) == -1) //设置自定义私聊信号处理器
        errHandle("sigaction", __LINE__);

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);//创建服务端套接字
    if (serv_sock == -1)
        errHandle("socket", __LINE__);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; //设置地址族
    serv_addr.sin_port = htons(PORT); //设置端口号
    //inet_aton("121.41.79.115", &serv_addr.sin_addr); //设置IP地址  阿里云地址
    inet_aton("127.0.0.1", &serv_addr.sin_addr); //设置IP地址  本地回环

    int ret = connect(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); //向服务器发起连接
    if (ret == -1)
        errHandle("connect", __LINE__);
    printf("服务器连接成功!\n");

    while (deleteFlag == 1) //当由于注销而退出，则重启初始界面
        init(argc, argv);
        
	close(serv_sock);
    return 0;
}
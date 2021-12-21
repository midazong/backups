#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <sqlite3.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
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
void get_time(char *nt) //获取时间
{
	time_t time_ptr;   //长整型long int,适合存储日历时间类型。
	time(&time_ptr); //获取从1970-1-1,0时0分0秒到现在时刻的秒数。
	struct tm *tm_ptr = NULL; //用来保存时间和日期的结构。
	tm_ptr = localtime(&time_ptr); //把从1970-1-1,0时0分0秒到当前时间系统所偏移的秒数时间转换为本地时间
	sprintf(nt,"%d/%d/%d %d:%d:%d", tm_ptr->tm_year+1900, tm_ptr->tm_mon+1, tm_ptr->tm_mday, tm_ptr->tm_hour, tm_ptr->tm_min, tm_ptr->tm_sec); //将内容写入nt;
}
void saveMsg(struct Msg* msg) //记录消息
{
    sqlite3* database;
    int ret = sqlite3_open("History.db", &database); //打开聊天记录数据库
    if (ret != SQLITE_OK)
    {
        printf ("\t打开数据库失败\n");
        return;
    }

    char sql[BUFSIZ]; 
    char *errmsg = NULL;
    char _time[32]; //时间缓冲区
    get_time(_time); //获取时间
    char msgRecords[2048]; //消息格式化缓冲区
    sprintf(msgRecords, "%s【%s】： %s\n\n", _time, msg->from, msg->msg); //格式化消息
    sprintf (sql, "insert into history values('%s','%s','%s')", msg->from, msg->to, msgRecords); //保存消息到数据库
    ret = sqlite3_exec(database, sql, NULL, NULL, &errmsg);
    if (ret != SQLITE_OK)
    {
        printf ("\t数据库操作失败：%s\n", errmsg);
        return;
    }
    printf("消息记录已保存\n");
}
void getHistory(int clnt_socket, struct Msg* msg) //查看消息记录
{
    sqlite3* database;
    char* errMsg;
    char** result;
    int nrow, ncol;
    int ret = sqlite3_open("History.db", &database); //打开聊天记录数据库
    if (ret != SQLITE_OK)
    {
        msg->ret = -1;
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        printf ("\t打开数据库失败\n");
        return;
    }

    char sql[BUFSIZ]; 
    if (strcmp(msg->to, "all") == 0) //如果是群聊消息
        sprintf (sql, "select msg from history where toname = 'all'"); //找出群聊消息记录
    
    else //如果是私聊记录
        sprintf (sql, "select msg from history where fromname = '%s' and toname = '%s' or fromname = '%s' and toname = '%s'", \
        msg->from, msg->to, msg->to, msg->from); //找出私聊消息记录
    
    ret = sqlite3_get_table(database, sql, &result, &nrow, &ncol, &errMsg);
    if (ret != SQLITE_OK)
    {
        ret = -2;
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }
    string s;
    int i;
    for (i = 1; i < (nrow+1)*ncol; i++) //所有消息保存到缓冲区s
        s.append(result[i]);  

    strcpy(msg->msgRecords, s.c_str()); //填充消息记录

    if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) //发送消息记录
        errHandle("write", __LINE__);

    printf("聊天记录查找成功\n");
    
}
void online(int clnt_socket, struct Msg* msg) //在线用户更新
{
    sqlite3* database;
    int ret = sqlite3_open("User.db", &database); 
    if (ret != SQLITE_OK)
    {
        printf("数据库打开失败\n");
        return;
    }
    char sql[BUFSIZ];
    char* errMsg;
    char** result;
    int nrow, ncol;
    sprintf(sql, "select name, flag, socket, online from user");
    ret = sqlite3_get_table(database, sql, &result, &nrow, &ncol, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }
    string names;
    int i;
    for (i = 7; i < (nrow+1)*ncol; i += 4)
    {
        if (strcmp(result[i], "1") == 0) //找到在线用户
        {
            names.append(result[i-3]); //把在线用户名加入names
            names.append(" "); //用户名不能含空格，用空格分割用户名
        }
    }
    strcpy(msg->msg, names.c_str()); //消息存入msg
    msg->type = 103;
    for (i = 7; i < (nrow+1)*ncol; i += 4)
        if (strcmp(result[i], "1") == 0) //找到在线用户
        {
            msg->flag = atoi(result[i-2]); //更新禁言状态
            if (write(atoi(result[i-1]), msg, sizeof(struct Msg)) == -1) //发送在线信息
                errHandle("write", __LINE__);
        }
    printf("在线信息已更新\n");
}
void allUsers(int clnt_socket,struct Msg* msg) //查找所有注册用户
{
    sqlite3* database;
    int ret = sqlite3_open("User.db", &database); 
    if (ret != SQLITE_OK)
    {
        printf("数据库打开失败\n");
        return;
    }
    char sql[BUFSIZ];
    char* errMsg;
    char** result;
    int nrow, ncol;
    sprintf(sql, "select name, flag from user");
    ret = sqlite3_get_table(database, sql, &result, &nrow, &ncol, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }
    string names;
    string silenceFlag; //用于管理员禁言
    int i;
    for (i = 2; i < (nrow+1)*ncol; i += 2)
    {
        if (strcmp(result[i], "root") == 0)
            continue;
        names.append(result[i]); //把用户名加入names
        names.append(" "); //用户名不能含空格，用空格分割用户名
        silenceFlag.append(result[i+1]); //把禁言标记加入silenceFlag
    }
    strcpy(msg->msgRecords, silenceFlag.c_str()); 
    strcpy(msg->msg, names.c_str()); //消息存入msg
    printf("%s\n%s\n", msg->msgRecords, msg->msg);
    msg->type = 105; 
    if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) //发送用户信息
        errHandle("write", __LINE__);
    printf("用户信息已发送\n");
}
void quit(int clnt_socket, struct Msg* msg) //退出时更新用户在线信息
{
    printf("%s\n", msg->from);
    sqlite3* database;
    int ret = sqlite3_open("User.db", &database);
    if (ret != SQLITE_OK)
    {
        printf("数据库打开失败\n");
        return;
    }
    char sql[BUFSIZ];
    char* errMsg;
    sprintf(sql, "update user set online = 0 where name = '%s'", msg->from);
    ret = sqlite3_exec(database, sql, nullptr, nullptr, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }
    sqlite3_close(database);
    if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) //唤醒客户端辅助线程，进入新循环读online状态
        errHandle("write", __LINE__);
    online(clnt_socket, msg); //更新下线
    printf("用户[%s]下线\n", msg->from);
}
void changePw(int clnt_socket, struct Msg* msg) //修改密码
{
    sqlite3* database;
    int ret = sqlite3_open("User.db", &database);
    if (ret != SQLITE_OK)
    {
        printf("数据库打开失败\n");
        msg->ret = -1;
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        return;
    }
    char sql[BUFSIZ];
    char* errMsg;
    char** result;
    int nrow, ncol;
    sprintf(sql, "select pw from user where name = '%s'", msg->from);
    ret = sqlite3_get_table(database, sql, &result, &nrow, &ncol, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }
    if (strcmp(result[1], msg->msg) != 0)
    {
        printf("密码错误，修改失败！\n");
        msg->ret = -4;
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        return;
    }
    sprintf(sql, "update user set pw = '%s' where name = '%s'", msg->filename, msg->from);
    ret = sqlite3_exec(database, sql, nullptr, nullptr, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }
    msg->ret = 0;
    if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
        errHandle("write", __LINE__);
    sqlite3_close(database);
    printf("用户[%s]密码已更新\n", msg->from);
}
void delete_user(int clnt_socket, struct Msg* msg) //注销
{
    sqlite3* database;
    int ret = sqlite3_open("User.db", &database); 
    if (ret != SQLITE_OK)
    {
        printf("数据库打开失败\n");
        msg->ret = -1;
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        return;
    }
    char sql[BUFSIZ];
    char* errMsg;
    char** result;
    int nrow, ncol;
    sprintf(sql, "select pw from user where name = '%s'", msg->from);
    ret = sqlite3_get_table(database, sql, &result, &nrow, &ncol, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }
    if (strcmp(result[1], msg->msg) != 0)
    {
        printf("密码错误，注销失败！\n");
        msg->ret = -4;
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        return;
    }
    sprintf(sql, "delete from user where name = '%s'", msg->from);
    ret = sqlite3_exec(database, sql, nullptr, nullptr, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }
    msg->ret = 0;
    if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
        errHandle("write", __LINE__);
    sqlite3_close(database);
    online(clnt_socket, msg); //更新下线
    printf("用户[%s]已注销!\n", msg->from);
}
void privateChat(int clnt_socket, struct Msg* msg) //私聊
{
    printf("[%s]请求向[%s]发送私聊消息\n", msg->from, msg->to);

    sqlite3* database;
    int ret = sqlite3_open("User.db", &database); 
    if (ret != SQLITE_OK)
    {
        printf("数据库打开失败\n");
        msg->ret = -1;
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        return;
    }
    char sql[BUFSIZ];
    char* errMsg;
    char** result;
    int nrow, ncol;
    sprintf(sql, "select name, socket, online from user");
    ret = sqlite3_get_table(database, sql, &result, &nrow, &ncol, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }
    int i;
    for (i = 3; i < (nrow+1)*ncol; i += 3)
    {
        if (strcmp(result[i], msg->to) == 0) //找到对象用户
        {
            if (strcmp(result[i+2], "0") == 0) //如果对方不在线
            {
                msg->ret = -3;
                if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
                    errHandle("write", __LINE__);
                return;
            }
            msg->ret = 0;
            if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
                errHandle("write", __LINE__);
            msg->type = 101; //标志私聊消息
            if (write(atoi(result[i+1]), msg, sizeof(struct Msg)) == -1) //发送消息给对象用户
                errHandle("write", __LINE__);
            printf("私聊消息发送成功!\n");
            saveMsg(msg); //保存私聊消息
            printf("私聊消息保存成功\n");
            return;
        }
    }
    msg->ret = -4;
    printf("查无此用户,私聊发送失败...\n");
    if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
        errHandle("write", __LINE__);
}
void groupChat(int clnt_socket, struct Msg* msg) //群聊
{
    printf("[%s]请求发送群聊消息\n", msg->from);

    sqlite3* database;
    int ret = sqlite3_open("User.db", &database); 
    if (ret != SQLITE_OK)
    {
        printf("数据库打开失败\n");
        msg->ret = -1;
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        return;
    }
    char sql[BUFSIZ];
    char* errMsg;
    char** result;
    int nrow, ncol;
    sprintf(sql, "select socket, online from user");
    ret = sqlite3_get_table(database, sql, &result, &nrow, &ncol, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }
    int i;
    for (i = 3; i < (nrow+1)*ncol; i += 2)
    {
        if (strcmp(result[i], "1") == 0) //找到在线用户
        {
            msg->ret = 0;
            msg->type = 102; //标志群聊消息
            if (write(atoi(result[i-1]), msg, sizeof(struct Msg)) == -1) //发送消息给对象用户
                errHandle("write", __LINE__);
        }
    }
    if (msg->ret == 0)
    {
        msg->type = 7; //回传消息标志为群聊响应
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        printf("群聊消息发送成功!\n");
        saveMsg(msg); //保存群聊消息
        printf("群聊消息保存成功\n");
    }
}
void silenced(int clnt_socket, struct Msg* msg) //禁言
{
    printf("管理员发起禁言\n");

    sqlite3* database;
    int ret = sqlite3_open("User.db", &database); 
    if (ret != SQLITE_OK)
    {
        printf("数据库打开失败\n");
        msg->ret = -1;
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        return;
    }
    char sql[BUFSIZ];
    char* errMsg;

    sprintf(sql, "update user set flag = 1");
    ret = sqlite3_exec(database, sql, NULL, NULL, &errMsg); //flag全部设置成1
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        msg->ret = -2;
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        return;
    }
    string s; //用户名缓冲区
    for (int i = 0; i < strlen(msg->msg); i++)
    {
        if (msg->msg[i] == ' ') //如果当前字符为空，就把用户名推入names中
        {
            sprintf(sql, "update user set flag = 2 where name = '%s'", s.c_str());
            ret = sqlite3_exec(database, sql, NULL, NULL, &errMsg); //flag全部设置成0
            if (ret != SQLITE_OK)
            {
                printf("数据库操作失败: %s\n", errMsg);
                msg->ret = -2;
                if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
                    errHandle("write", __LINE__);
                return;
            }
            s.clear(); //清空缓冲区
            continue;
        }
        s.push_back(msg->msg[i]); 
    }
    sprintf(sql, "update user set flag = 0 where name = 'root'");
    ret = sqlite3_exec(database, sql, NULL, NULL, &errMsg); //恢复root的flag
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        msg->ret = -2;
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        return;
    }
    msg->ret = 0;
    if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
        errHandle("write", __LINE__);

    online(clnt_socket, msg); //把禁言状态发送到所有用户实时更新
    printf("禁言操作完成\n");
}
void acceptFile(int clnt_socket, struct Msg* msg) //从发送方接收文件文件
{
    char* filename = basename(msg->filename); //获取文件名
    char to[12];
    strcpy(to, msg->to); //保存被发送用户名
    int fd = open(filename, O_CREAT | O_EXCL | O_RDWR, 0777); //以独占方式创建文件
    if (fd == -1)
        errHandle("open", __LINE__);
    msg->ret = 0; //标识准备好接收文件
    msg->type = 12;
    if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) //通知客户端准备好了
        errHandle("write", __LINE__);
    printf("开始接收文件【%s】\n", filename);
    int readNums, writeNums;
    int readSums = 0, writeSums = 0;
    string s;
    char _time[24];
    get_time(_time);
    while (1)
    {
        memset(msg->msg, 0, sizeof(msg->msg));
        readNums = read(clnt_socket, msg->msg, sizeof(msg->msg)); //从客户端读字节
        if (readNums == -1) 
        {
            strcpy(msg->msg, "read error"); //读错误
            if (write(clnt_socket, msg->msg, sizeof(msg->msg)) == -1) //通知客户端出错
                errHandle("write", __LINE__);
            errHandle("read", __LINE__);
        }
        if (strcmp(msg->msg, "over") == 0) //文件接收完成
            break;

        get_time(_time);
        printf("%s 从客户端成功接收%d字节数据\n", _time, readNums);
        readSums += readNums;
            
        writeNums = write(fd, msg->msg, readNums); //把读取的数据写入文件
        if (writeNums == -1 || (writeNums != readNums))
        {
            strcpy(msg->msg, "write error"); //写错误
            if (write(clnt_socket, msg->msg, sizeof(msg->msg)) == -1) //通知客户端出错
                errHandle("write", __LINE__);
            errHandle("write", __LINE__);
        }
        get_time(_time);
        printf("%s 成功保存%d字节数据到本地\n", _time, writeNums);
        writeSums += writeNums;
        strcpy(msg->msg, "success"); //保存成功
        if (write(clnt_socket, msg->msg, sizeof(msg->msg)) == -1) //通知客户端继续发送
            errHandle("write", __LINE__);
    }
    get_time(_time);
    printf("%s 文件接收完成,共接收%d字节数据，保存%d字节数据到本地\n", _time, readSums, writeSums);

    sprintf(msg->msg, "%d", writeSums);

    sqlite3* database;
    int ret = sqlite3_open("User.db", &database); 
    if (ret != SQLITE_OK)
    {
        printf("数据库打开失败\n");
        msg->ret = -1;
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        return;
    }
    char sql[BUFSIZ];
    char* errMsg;
    char** result;
    int nrow, ncol;
    printf("查找用户%s的套接子\n", msg->to);
    sprintf(sql, "select socket from user where name = '%s'", msg->to);
    ret = sqlite3_get_table(database, sql, &result, &nrow, &ncol, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }

    msg->type = 106; //标识为发送文件给对象
    strcpy(msg->filename, filename); //填充文件名
    if (write(atoi(result[1]), msg, sizeof(struct Msg)) == -1) //询问对象是否接受文件
        errHandle("write", __LINE__);                          //接下来的事情交给另一个线程去处理，因为服务器线程跟每个客户端是1对1处理的,否则会发生竞争
                                                               //发送的任务与本线程无关，本线程只与发送方沟通,接收文件到本地
    close(fd); //关闭文件夹
}
void refuseFile(int clnt_socket, struct Msg* msg) //通知发送方，对象拒绝文件
{
    printf("用户【%s】拒绝接收文件\n", msg->to);
    if (unlink(msg->filename) == -1)
        errHandle("unlink", __LINE__);
    printf("服务器中转缓存文件已删除\n");

    sqlite3* database;
    int ret = sqlite3_open("User.db", &database); 
    if (ret != SQLITE_OK)
    {
        printf("数据库打开失败\n");
        msg->ret = -1;
        if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) 
            errHandle("write", __LINE__);
        return;
    }
    char sql[BUFSIZ];
    char* errMsg;
    char** result;
    int nrow, ncol;
    printf("查找用户%s的套接子\n", msg->from);
    sprintf(sql, "select socket from user where name = '%s'", msg->from);
    ret = sqlite3_get_table(database, sql, &result, &nrow, &ncol, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }

    msg->type = 12; //标记会发送文件的响应
    msg->ret = -3; //标识为被拒绝
    if (write(atoi(result[1]), msg, sizeof(struct Msg)) == -1) //回应发送方，对方拒绝了文件
        errHandle("write", __LINE__);
}
void sendFile(int clnt_socket, struct Msg* msg) //向对象发送文件
{
    char filename[50];
    strcpy(filename, msg->filename);
    printf("开始向用户【%s】发送文件【%s】\n", msg->to, filename);
    msg->type = 107; //标识为处理接收文件
        
    if (write(clnt_socket, msg, sizeof(struct Msg)) == -1) //向接收方客户端打招呼要发送文件
        errHandle("write", __LINE__);                 //请做好准备

    if (read(clnt_socket, msg, sizeof(struct Msg)) == -1) //客户端已经准备好了
        errHandle("read", __LINE__);

    int fd = open(msg->filename, O_RDONLY); //只读方式打开文件
    if (fd == -1)
        errHandle("open", __LINE__);
    int readNums, writeNums;
    int  readSums = 0, writeSums = 0; //从本地文件中读字节和发送到客户端的字节数
    char _time[24];
    get_time(_time);
    
    while (1)
    {
        memset(msg->msg, 0, sizeof(msg->msg)); //清空缓冲区
        readNums = read(fd, msg->msg, sizeof(msg->msg)); //从本地读取字节到缓冲区
        msg->msg[readNums] = '\0'; //在文件长度处放置结束符
        if (readNums == 0) //刚好读完
            break;
        if (readNums == -1)
            errHandle("read", __LINE__);
        printf("%s 从本地读取了%d字节数据\n", _time, readNums);
        readSums += readNums; //读文件总数据
        
        writeNums = write(clnt_socket, msg->msg, readNums); //向客户端发送文件
        if (writeNums == -1) 
            errHandle("write", __LINE__);
        printf("%s 向客户端发送了%d字节数据\n", _time, writeNums);
        writeSums += writeNums; //写文件总数据
        
        if (read(clnt_socket, msg->msg, sizeof(msg->msg)) == -1) //接收反馈
            errHandle("read", __LINE__);

        if (strcmp(msg->msg, "read error") == 0)
        {
            printf("客户端读取出错\n");
            break;
        }
        if (strcmp(msg->msg, "write error") == 0)
        {
            printf("客户端保存出错\n");
            break;
        }
        if (readNums != sizeof(msg->msg)) //读到最后块数据
            break;
    }
    if (strcmp(msg->msg, "success") == 0)
    {
        strcpy(msg->msg, "over"); 
        if (write(clnt_socket, msg->msg, sizeof(msg->msg)) == -1) //通知客户端文件发送完成
            errHandle("write", __LINE__);
        printf("%s 发送完成！ 共读取%d字节数据，发送%d字节数据\n", _time, readSums, writeSums);
    }
    printf("文件名%s\n", filename);
    if (unlink(filename) == -1)
        errHandle("unlink", __LINE__);
    printf("服务器中转缓存文件已删除\n");
    close(fd); //关闭文件夹
}
void opt(int clnt_socket) 
{
    struct Msg msg;
    int ret;
    while(1)
    {
        ret = read(clnt_socket, &msg, sizeof(struct Msg));
        if (ret == -1)
            errHandle("read", __LINE__);
        if (ret == 0 || msg.type == 3) //如果客户端退出回到登录界面，就更新在线信息
        {
            printf("%s\n", msg.from);
            quit(clnt_socket, &msg);
            break;
        }
        switch(msg.type)
        {
            case 4 : changePw(clnt_socket, &msg); break; //修改密码
            case 6 : privateChat(clnt_socket, &msg); break; //私聊
            case 7 : groupChat(clnt_socket, &msg); break; //群聊
            case 8 : online(clnt_socket, &msg); break; //查看在线用户
            case 9 : allUsers(clnt_socket, &msg); break; //查找所有注册用户
            case 10 : getHistory(clnt_socket, &msg); break; //查看消息记录
            case 11 : silenced(clnt_socket, &msg); break; //禁言
            case 12 : acceptFile(clnt_socket, &msg); break; //从发送方接收文件
            case 13 : refuseFile(clnt_socket, &msg); break; //对象拒绝文件
            case 14 : sendFile(clnt_socket, &msg); break; //向对象发送文件
            
        }
        if (msg.type == 5) //注销函数
        {
            delete_user(clnt_socket, &msg);
            if (msg.ret == 0) //当注销成功时回到登录界面
                break;
        }
    }
}
void regist(int clnt_sock, struct Msg* msg)
{
    printf("用户[%s]开始注册\n", msg->from);
    sqlite3* database;
    int ret = sqlite3_open("User.db", &database); //打开数据库注册信息
    if (ret != SQLITE_OK)
    {
        printf("数据库User.db打开失败\n");
        msg->type = -1;
        if (write(clnt_sock, msg, sizeof(struct Msg)) == -1)
            errHandle("write", __LINE__);
        return;
    }
    char sql[BUFSIZ] = "";
    char* errMsg;
    sprintf(sql, "insert into user values('%s', '%s', %d, '%d', 0)", msg->from, msg->msg, msg->flag, clnt_sock);
    ret = sqlite3_exec(database, sql, nullptr, nullptr, &errMsg); //注册信息
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败:%s\n", errMsg);
        msg->type = -2;
        if (write(clnt_sock, msg, sizeof(struct Msg)) == -1)
            errHandle("write", __LINE__);
        return;
    }
    msg->type = 0;
    printf("[%s]注册成功！\n", msg->from);
    if (write(clnt_sock, msg, sizeof(struct Msg)) == -1) //通知客户注册成功
        errHandle("write", __LINE__);
    sqlite3_close(database); //关闭数据库
}
void login(int clnt_sock, struct Msg* msg)
{
    printf("[%s]请求登录\n", msg->from);
    sqlite3* database;
    int ret = sqlite3_open("User.db", &database); //打开数据库比对信息
    if (ret != SQLITE_OK)
    {
        printf("数据库User.db打开失败\n");
        msg->type = -1;
        if (write(clnt_sock, msg, sizeof(struct Msg)) == -1)
            errHandle("write", __LINE__);
        return;
    }
    char* errMsg;
    char** result;
    int nrow, ncol;
    char sql[BUFSIZ] = "";
    sprintf(sql, "select * from user");
    ret = sqlite3_get_table(database, sql, &result, &nrow, &ncol, &errMsg); //获取数据库信息
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败%s\n", errMsg);
        msg->type = -2;
        if (write(clnt_sock, msg, sizeof(struct Msg)) == -1)
            errHandle("write", __LINE__);
        return;
    }
    int i;
    for (i = 0+ncol; i < (nrow+1)*ncol; i += ncol)
    {
        if (strcmp(result[i], msg->from) == 0 && strcmp(result[i+1], msg->msg) == 0) //账号密码正确
        {
            if (strcmp(result[i+4], "1") == 0) //该账号在线
            {
                printf("账号[%s]已在别处登录\n", msg->from);
                msg->type = -3;
                if (write(clnt_sock, msg, sizeof(struct Msg)) == -1)
                    errHandle("write", __LINE__);
                sqlite3_free_table(result);
                sqlite3_close(database);
                return;
            }
            if (strcmp(result[i+2], "0") == 0) //管理员
            {
                msg->flag = 0;
                msg->type = 0;
                printf("管理员[%s]验证通过\n", msg->from);
                if (write(clnt_sock, msg, sizeof(struct Msg)) == -1)
                    errHandle("write", __LINE__);
            }
            else                               //普通用户
            {
                msg->flag = atoi(result[i+2]); //可能是1/2
                msg->type = 0;
                printf("用户[%s]验证通过\n", msg->from);
                if (write(clnt_sock, msg, sizeof(struct Msg)) == -1)
                    errHandle("write", __LINE__);
            }
            sprintf(sql, "update user set socket = %d, online = 1 where name = '%s'", clnt_sock, msg->from);
            ret = sqlite3_exec(database, sql, nullptr, nullptr, &errMsg);
            if (ret != SQLITE_OK)
            {
                printf("数据库操作失败:%s\n", errMsg);
                msg->type = -2;
                if (write(clnt_sock, msg, sizeof(struct Msg)) == -1)
                    errHandle("write", __LINE__);
                return;
            }
            sqlite3_free_table(result);
            sqlite3_close(database);

            online(clnt_sock, msg); //上线提醒
            printf("进入操作界面\n");
            opt(clnt_sock);
            return;
        }
    }
    printf ("[%s]验证未通过\n", msg->from);
    msg->type = -4;
    write (clnt_sock, msg, sizeof(struct Msg));
    sqlite3_free_table(result);

    sqlite3_close(database);
}
void* serv_pthread(void* arg) //工作线程处理客户端注册和登录请求
{
    int clnt_sock = *((int*)arg); 
    printf("客户端[%d]已连接！\n", clnt_sock);
    struct Msg msg;
    while(1)
    {
        int ret = read(clnt_sock, &msg, sizeof(struct Msg));
        if (ret == -1)
            errHandle("read", __LINE__);
        if (ret == 0)
        {
            printf("客户端[%d]退出!\n", clnt_sock);
            break;
        }
        switch(msg.type)
        {
            case 1 : regist(clnt_sock, &msg); break;
            case 2 : login(clnt_sock, &msg); break;
        }
    }
    close(clnt_sock); //关闭客户端套接字
    return 0;
}
void sigHandle(int sig) //信号处理器
{
    sqlite3* database;
    int ret = sqlite3_open("User.db", &database); 
    if (ret != SQLITE_OK)
    {
        printf("数据库打开失败\n");
        return;
    }
    char sql[BUFSIZ];
    char* errMsg;
    char** result;
    int nrow, ncol;
    sprintf(sql, "select socket, online from user");
    ret = sqlite3_get_table(database, sql, &result, &nrow, &ncol, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }
    struct Msg msg;
    int i;
    for (i = 3; i < (nrow+1)*ncol; i += 2)
    {
        if (strcmp(result[i], "1") == 0) //找到在线用户
        {   
            msg.type = 104;
            if (write(atoi(result[i-1]), &msg, sizeof(struct Msg)) == -1) //发送下线通知
                errHandle("write", __LINE__);
        }
    }
    sprintf(sql, "update user set online = 0"); //将所有用户标记为下线
    ret = sqlite3_get_table(database, sql, &result, &nrow, &ncol, &errMsg);
    if (ret != SQLITE_OK)
    {
        printf("数据库操作失败: %s\n", errMsg);
        return;
    }
    sleep(1); //等待1秒
    printf("服务器退出\n");
    exit(0);
}
int main()
{
    int ret;
    sqlite3* database;
    char sql[BUFSIZ];
    char* errMsg;

    ret = sqlite3_open("User.db", &database); //打开用户信息数据库
    if (ret != SQLITE_OK)
        errHandle("sqlite3_open", __LINE__);

    sprintf(sql, "create table if not exists user \
    (name TEXT, pw TEXT, flag INTEGER, socket INTEGER, online INTEGER, primary key(name))");//账号，密码，身份，套接字, 在线
    ret = sqlite3_exec(database, sql, nullptr, nullptr, &errMsg);
    if (ret != SQLITE_OK)
        errHandle("create table 'user'", __LINE__);
    sprintf(sql, "insert into user(name, pw, flag, socket, online) select 'root', 'root', 0, 0, 0 \
    where not exists(select * from user where name = 'root')"); //创建数据库时自动插入root用户信息，若存在则不插入
    ret = sqlite3_exec(database, sql, nullptr, nullptr, &errMsg);
    if (ret != SQLITE_OK)
        errHandle("insert root into 'user'", __LINE__);
    sqlite3_close(database);

    ret = sqlite3_open("History.db", &database); //打开消息记录数据库
    if (ret != SQLITE_OK)
        errHandle("sqlite3_open", __LINE__);
    
    sprintf(sql, "create table if not exists history(fromname TEXT, toname TEXT, msg TEXT);"); //创建历史记录表，发送用户，接收用户，消息内容
    ret = sqlite3_exec(database, sql, NULL, NULL, &errMsg);
    if (ret != SQLITE_OK)
        errHandle("create table 'history'", __LINE__);
    sqlite3_close(database); //关闭数据库

    struct sigaction sa; //创建信号句柄
    sigemptyset(&sa.sa_mask); //阻塞掩码置空
    sa.sa_flags = 0; //信号处理过程标记
    sa.sa_handler = sigHandle; //设置信号处理函数
    if (sigaction(SIGINT, &sa, NULL) == -1) //设置中断信号处理ctrl -c
        errHandle("sigaction", __LINE__);
    if (sigaction(SIGQUIT, &sa, NULL) == -1) //设置退出信号处理 ctrl -\  /
        errHandle("sigaction", __LINE__);

    int serv_sock, clnt_sock;
    int option = 1;
    struct sockaddr_in serv_addr, clnt_addr;
    socklen_t optlen = sizeof(option), clnt_addr_sz;
    pthread_t ptd;

    serv_sock = socket(PF_INET, SOCK_STREAM, 0); //创建服务器套接字
    if (serv_sock == -1)
        errHandle("socket", __LINE__);
    if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &option, optlen) == -1)  //消除Time-wait快速重启服务器
        errHandle("setsockopt", __LINE__);

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; //分配地址族
    serv_addr.sin_port = htons(PORT); //分配端口号
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); //分配本机IP地址

    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) //绑定服务器地址到套接字
        errHandle("bind", __LINE__);
    if (listen(serv_sock, 5) == -1) //监听套接字
        errHandle("listen", __LINE__);
    printf("服务器已开启\n");
    while(1)
    {
        clnt_addr_sz = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_sz); //获取客户端套接字
        if (clnt_sock == -1)
            errHandle("accept", __LINE__);
        
        if (pthread_create(&ptd, nullptr, serv_pthread, &clnt_sock) != 0) //创建工作线程处理客户端请求
            errHandle("pthread_create", __LINE__);
        if (pthread_detach(ptd) != 0) //线程分离
            errHandle("pthread_detach", __LINE__);
    }
    close(serv_sock); //关闭服务端套接字
    return 0;
}
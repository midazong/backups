#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <asm/termios.h>

#define DEV_NAME "/dev/ttyS0" //串口文件名

int main(int argc, char *argv[])
{
    int fd;
    int len, i, ret;
    char buf[] = "hello world!\n"; 

    fd = open(DEV_NAME, O_RDWR | O_NOCTTY); //打开串口
    if (fd < 0)
    {
        perror(DEV_NAME);
        return -1;
    }

    len = write(fd, buf, sizeof(buf)); //写入信息到串口
    if (len < 0)
    {
        printf("write data error \n");
    }

    len = read(fd, buf, sizeof(buf)); //从串口读信息
    if (len < 0)
    {
        printf("read error \n");
        return -1;
    }
    for (int i = 0; i < sizeof(buf); i++)
        buf[i] = toupper(buf[i]);
    len = write(fd, buf, sizeof(buf)); //写入信息到串口
    if (len < 0)
    {
        printf("write data error \n");
    }
    
    printf("%s", buf);

    return (0);
}

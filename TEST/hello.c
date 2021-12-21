#include <stdio.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sqlite3.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <error.h>
int main()
{
    int a = 1, b = 2;
    printf("%d\n%d\n", htonl(a), htonl(b));
}
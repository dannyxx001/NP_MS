#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <string>
#define MAXLINE 4096
#define LISTENNQ 10
using namespace std;

int main(int argc, char *argv[], char *envp[])
{
	/* env variable test
	for(int i=0;envp[i]!=(char *)0;i++)
		cout << envp[i] << endl;*/
	//cout << getenv("PATH") << endl;
	setenv("PATH","./bin",1);
	int listenfd, connfd, n;
	struct sockaddr_in servaddr,cliaddr;
	string send_buff, recv_buff;	

	if(argc != 2)
	{
		cout << "usage: ./server.exe <Port>" << endl;
		exit(-1);
	}

	//set server address
	bzero(&servaddr,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(atoi(argv[1]));

	//prepare socket
	if((listenfd = socket(AF_INET,SOCK_STREAM,0)) < 0)
	{
		cout << "socket error" << endl;
		exit(-1);
	}

	//bind socket with addr
	if(bind(listenfd,(struct sockaddr *)&servaddr,sizeof(servaddr)) < 0)
	{
		cout << "bind error" << endl;
		exit(-1);
	}

	//listen
	if(listen(listenfd,LISTENNQ) < 0)
	{
		cout << "listen error" << endl;
		exit(-1);
	}

	connfd = accept(listenfd,(struct sockaddr *)NULL,NULL);
	char tmp[1024];

	while(true)
	{
		send_buff.assign("welcome!\n");
		write(connfd,send_buff.c_str(),send_buff.length());
		if((n = read(connfd,tmp,1024)) > 0)
		{
			tmp[n] = 0;
			recv_buff.assign(tmp);
			cout << recv_buff << endl;
		}
	}
	return 0;
}
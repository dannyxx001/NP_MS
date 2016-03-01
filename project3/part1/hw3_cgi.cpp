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
#include <fstream>
#include <signal.h>
#define MAXLINE 10000
using namespace std;

typedef struct host
{
	char ip[17];
	struct  sockaddr_in addr;
	char batch_file_name[64];
	FILE *file;
	int connfd;
	bool is_connect;
	int counter;
}host;

void convert_to_html(string *buff)
{
	size_t found;
	while((found = (*buff).find('\r')) != string::npos)
		(*buff).replace(found,1,"");
	while((found = (*buff).find('<')) != string::npos)
		(*buff).replace(found,1,"&lt;");
	while((found = (*buff).find('>')) != string::npos)
		(*buff).replace(found,1,"&gt;");
	while((found = (*buff).find('\n')) != string::npos)
		(*buff).replace(found,1,"<br>");
	return;
}

int main()
{
	cout << "Content-type:text/html\r\n\r\n" << flush;
	// the following is used for shell test
	//setenv("QUERY_STRING","h1=127.0.0.1&p1=10000&f1=t1.txt&h2=127.0.0.1&p2=10000&f2=t5.txt&h3=&p3=&f3=&h4=&p4=&f4=&h5=&p5=&f5=",1);
	char *query = getenv("QUERY_STRING");
	//cout << query << endl;

	if(chdir("./test") == -1)
		cout << "Change directory error!" << endl;

	host server[5];
	int host_num = 0;
	char *host_tok = strtok(query,"=\r\n");
	for(int i=0;host_tok != NULL && i<5;i++)	// parse the host info
	{
		host_tok = strtok(NULL,"&\r\n");
		if(host_tok != NULL && host_tok[0] != 'p')
		{
			host_num++;
			server[i].connfd = -1;
			bzero(&(server[i].addr),sizeof(server[i].addr));
			server[i].addr.sin_family = AF_INET;
			if(inet_pton(AF_INET,host_tok,&(server[i].addr.sin_addr)) <= 0)
				cout << "(wrong ip) inet_pton error for " << host_tok << endl;
			strcpy(server[i].ip,host_tok);
			//cout << host_tok << endl;
			host_tok = strtok(NULL,"=\r\n");
			host_tok = strtok(NULL,"&\r\n");
			server[i].addr.sin_port = htons(atoi(host_tok));
			//cout << host_tok << endl;
			host_tok = strtok(NULL,"=\r\n");
			host_tok = strtok(NULL,"&\r\n");
			strcpy(server[i].batch_file_name,host_tok);
			server[i].file = fopen(host_tok,"r");
			//cout << host_tok << endl;
			server[i].counter = 0;
		}
		else									// the host not use
		{
			server[i].connfd = -2;
			host_tok = strtok(NULL,"h\r\n");
		}
		host_tok = strtok(NULL,"=\r\n");
	}

	cout << "<html>\n\
			<head>\n\
			<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />\n\
			<title>Network Programming Homework 3</title>\n\
			</head>\n\
			<body bgcolor=#336699>\n\
			<font face=\"Courier New\" size=2 color=#FFFF99>\n\
			<table width=\"800\" border=\"1\">\n\
			<tr>\n" << flush;

	if(host_num == 0)
	{
		cout << "no hosts</tr>\n</table>\n" << flush;
		return 0;
	}

	for(int i=0;i<5;i++)
	{
		if(server[i].connfd != -2)
			cout << "<td>" << server[i].ip << "</td>";
	}
	cout << "</tr>\n<tr>\n" << flush;
	for(int i=0;i<5;i++)
	{
		if(server[i].connfd != -2)
			cout << "<td valign=\"top\" id=\"m" << i << "\"></td>";
	}
	cout << "</tr>\n</table>\n" << flush;

	fd_set rset,allset;
	FD_ZERO(&allset);
	int max_fd = 0;

	for(int i=0;i<5;i++)
	{
		if(server[i].connfd != -2)
		{
			if((server[i].connfd = socket(AF_INET,SOCK_STREAM,0)) < 0)
				cout << "socket error" << endl;
			int flag;
			if((flag = fcntl(server[i].connfd,F_GETFL,0)) < 0)
				cout << "F_GETFL error" << endl;
			if(fcntl(server[i].connfd,F_SETFL,flag | O_NONBLOCK) < 0)
				cout << "F_SETFL error" << endl;
			if(connect(server[i].connfd,(struct sockaddr *)&(server[i].addr),sizeof(server[i].addr)) < 0)
			{
				server[i].is_connect = false;
				if(errno != EINPROGRESS)	// connect may be in progress, because it's nonblocking
					cout << "(not EINPROGRESS) connect error" << endl;
			}
			FD_SET(server[i].connfd,&allset);
			if(max_fd <= server[i].connfd)
				max_fd = server[i].connfd + 1;
		}
	}

	int n;
	string recv_buff;
	char tmp[MAXLINE+1];

	while(true)
	{
		memcpy(&rset,&allset,sizeof(allset));
		//rset = allset;
		select(max_fd,&rset,NULL,NULL,NULL);
		
		for(int i=0;i<5;i++)
		{
			if(server[i].connfd != -2)
			{
				if(server[i].is_connect == false)	// connect again make sure "is connected"
				{
					if(connect(server[i].connfd,(struct sockaddr *)&(server[i].addr),sizeof(server[i].addr)) < 0)
					{
						if(errno != EISCONN)	// "is connected" means successfully connect
						{
							cout << "host" << i <<" connect error" << endl; // real connect error
							server[i].counter += 1;
						}
						else
							server[i].is_connect = true;
					}
					if(server[i].counter == 3)	// try to connect at most three times
					{
						server[i].connfd = -2;
						host_num--;
					}
				}
				else if(FD_ISSET(server[i].connfd,&rset))
				{
					recv_buff = "";
					while((n = read(server[i].connfd,tmp,MAXLINE)) > 0)
					{
						recv_buff.append(tmp,n);
						if(tmp[n-1] != '\n' && (tmp[n-1] != ' ' && tmp[n-2] != '%'))
							continue;
						else
						{
							tmp[0] = 0;
							recv_buff.append(tmp,1);
							break;
						}
					}
					if(n == 0)
					{
						FD_CLR(server[i].connfd,&allset);
						server[i].connfd = -2;
						close(server[i].connfd);
					}
					//cout << n << " " << endl;
					convert_to_html(&recv_buff);
					printf("<script>document.all['m%d'].innerHTML += \"%s\";</script>\n",i,recv_buff.c_str());
					if(recv_buff[recv_buff.length()-3] == '%') // sub /r/n and one space
					{
						bzero(tmp,10001);
						if(server[i].file != NULL)
						{
							fgets(tmp,10001,server[i].file);
							string cmd(tmp);
							//cout << cmd << endl;
							write(server[i].connfd,cmd.c_str(),cmd.length());
							convert_to_html(&cmd);
							printf("<script>document.all['m%d'].innerHTML += \"<b>%s</b>\";</script>\n",i,cmd.c_str());
							if(strncmp(cmd.c_str(),"exit",4) == 0)
							{
								shutdown(server[i].connfd,SHUT_WR);
								FD_CLR(server[i].connfd,&allset);
								server[i].connfd = -2;
								fclose(server[i].file);
								host_num--;
							}
						}
						else
							cout << "host" << i << "fopen error" << endl;
					}
					fflush(stdout);
				}
			}
		}
		if(host_num == 0)
			break;
	}

	return 0;
}

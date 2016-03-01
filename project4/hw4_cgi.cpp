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
	char ip_or_domain[64];		// ras or rwg server's ip or domain name
	struct sockaddr_in addr;	// ras or rwg server's addr
	char batch_file_name[64];	// batch file name
	FILE *file;					// batch file fd
	int connfd;					// connection fd to socks server
	bool is_connect;			// check connection state
	int counter;				// connection retry times
	struct sockaddr_in socks;	// socks server addr
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

void str_split(char *str, const char *delimiters, char ***ret, int *count) // separate str by delimiters
{
	int counter = 1;
	for(int i=0;i<strlen(str);i++)
	{
		for(int j=0;j<strlen(delimiters);j++)
			if(str[i] == delimiters[j])
			{
				counter++;
				break;
			}
	}
	//cout << "counter:" << counter << endl;

	*ret = (char **) malloc(sizeof(char *)*counter);
	char *next_tok = strtok(str,delimiters);
	
	counter = 0;
	while(next_tok != NULL)
	{
		(*ret)[counter] = (char *)malloc(sizeof(char)*(strlen(next_tok)+1));
		strcpy((*ret)[counter],next_tok);
		counter++;
		next_tok = strtok(NULL,delimiters);
	}
	*count = counter;	// num of toks
	return;
}

void send_sock4_request(int sockfd, struct sockaddr_in addr, char *ip_or_domain)	// send sock4 request
{
	unsigned char package[9];
	
	package[0] = 4;
	package[1] = 1;
	package[2] = ntohs(addr.sin_port) / 256;
	package[3] = ntohs(addr.sin_port) % 256;
	package[4] = addr.sin_addr.s_addr & 0xFF;
	package[5] = (addr.sin_addr.s_addr  >> 8) & 0xFF;
	package[6] = (addr.sin_addr.s_addr  >> 16) & 0xFF;
	package[7] = (addr.sin_addr.s_addr  >> 24) & 0xFF;
	package[8] = 0;
	// for debug
	/*cout << "***** request *****" << endl;
	printf("VN: %u, CD: %u, DST_PORT: %u %u, DST_IP: %u.%u.%u.%u",package[0],package[1],package[2],package[3],package[4],package[5],package[6],package[7]);*/
	
	write(sockfd,package,sizeof(package));
	return;
}

bool recv_sock4_reply(int sockfd, struct sockaddr_in addr)	//recv socks4 reply
{
	unsigned char recv_buff[8];
	int read_len;
	bzero(recv_buff,8);
	
	if((read_len = read(sockfd,recv_buff,8)) > 0) // only read reply
	{
		unsigned int vn = recv_buff[0];
		unsigned int cd = recv_buff[1];
		// network byte oreder is upside down compared to host byte order, the two followings are network byte order
		uint16_t dst_port = recv_buff[2] | recv_buff[3] << 8;
		struct in_addr dst_ip;
		dst_ip.s_addr = recv_buff[4] | recv_buff[5] << 8 | recv_buff[6] << 16 | recv_buff[7] << 24;
		if(dst_port != addr.sin_port)
		{
			cout << "diff dst_port" << endl;
			return false;
		}
		if(dst_ip.s_addr != addr.sin_addr.s_addr)
		{
			cout << "diff dst_ip" << endl;
			return false;
		}
		// for debug
		/*cout << "***** reply pkt length:" << read_len <<" *****" << endl;
		cout << "VN: " << vn << ", CD: " << cd << ", DST_IP: " << inet_ntoa(dst_ip) << ", DST_PORT: " << ntohs(dst_port) << endl;*/
		if(cd == 90)
			return true;
		else if(cd == 91)
			return false;
	}
	else if(read_len == 0)
	{
		cout << "connection terminate" << endl;
		close(sockfd);
		return false;
	}
	else
	{
		cout << "sock4 request read error" << endl;
		close(sockfd);
		return false;
	}
}

int main()
{
	cout << "Content-type:text/html\r\n\r\n" << flush;
	// the following is used for shell test
	//setenv("QUERY_STRING","h1=140.113.168.191&p1=10001&f1=t1.txt&sh1=140.113.168.191&sp1=20000&h2=&p2=&f2=&sh2=&sp2=&h3=&p3=&f3=&sh3=&sp3=&h4=&p4=&f4=&sh4=&sp4=&h5=&p5=&f5=&sh5=&sp5=",1);
	char *query = getenv("QUERY_STRING");
	//cout << query << endl;

	if(chdir("./test") == -1)
		cout << "Change directory error!" << endl;

	host server[5];
	int host_num = 0;
	int num_of_tok;
	char **tok_list;
	str_split(query,"&",&tok_list,&num_of_tok);
	// for debug
	/*for(int i=0;i<num_of_tok;i++)
		cout << tok_list[i] << endl;*/
	
	for(int i=0;i<5;i++)	// parse the host info
	{
		int j = i*5;
		if(strlen(tok_list[j]) > 3) // if hx=?
		{
			host_num++;
			server[i].connfd = -1;
			bzero(&(server[i].addr),sizeof(server[i].addr));
			server[i].addr.sin_family = AF_INET;
			bzero(&(server[i].socks),sizeof(server[i].socks));
			server[i].socks.sin_family = AF_INET;
			
			if(tok_list[j][0] == 'h')
			{
				strcpy(server[i].ip_or_domain,&tok_list[j][3]);
				if(inet_pton(AF_INET,&tok_list[j][3],&(server[i].addr.sin_addr)) <= 0)
					cout << "(wrong ip) inet_pton error for " << &tok_list[j][3] << endl;
			}
			if(tok_list[j+1][0] == 'p')
				server[i].addr.sin_port = htons(atoi(&tok_list[j+1][3]));
			if(tok_list[j+2][0] == 'f')
			{
				strcpy(server[i].batch_file_name,&tok_list[j+2][3]);
				server[i].file = fopen(server[i].batch_file_name,"r");
			}
			if(tok_list[j+3][0] == 's' && tok_list[j+3][1] == 'h')
			{
				if(inet_pton(AF_INET,&tok_list[j+3][4],&(server[i].socks.sin_addr)) <= 0)
					cout << "(wrong ip) inet_pton error for " << &tok_list[j+3][4] << endl;
			}
			if(tok_list[j+4][0] == 's' && tok_list[j+4][1] == 'p')
				server[i].socks.sin_port = htons(atoi(&tok_list[j+4][4]));

			// for debug
			/*cout << &tok_list[j][3] << endl;
			cout << &tok_list[j+1][3] << endl;
			cout << &tok_list[j+2][3] << endl;
			cout << &tok_list[j+3][4] << endl;
			cout << &tok_list[j+4][4] << endl;*/

			server[i].counter = 0;
		}
		else	// the host not use
		{
			server[i].connfd = -2;
		}
	}
	//cout << "host_num:" << host_num << endl;

	cout << "<html>\n\
			<head>\n\
			<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />\n\
			<title>Network Programming Homework 4</title>\n\
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
			cout << "<td>" << server[i].ip_or_domain << "</td>";
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
			if(connect(server[i].connfd,(struct sockaddr *)&(server[i].socks),sizeof(server[i].socks)) < 0)
			{
				server[i].is_connect = false;
				if(errno != EINPROGRESS)	// connect may be in progress, because it's nonblocking
				{
					printf("<script>document.all['m%d'].innerHTML += \"(not EINPROGRESS) connect error\";</script>\n",i);
					fflush(stdout);
				}
			}
			else
			{
				send_sock4_request(server[i].connfd,server[i].addr,server[i].ip_or_domain);
				if(recv_sock4_reply(server[i].connfd,server[i].addr) == true)
				{
					FD_SET(server[i].connfd,&allset);
					if(max_fd <= server[i].connfd)
						max_fd = server[i].connfd + 1;
				}
				else
				{
					printf("<script>document.all['m%d'].innerHTML += \"socks4 reply reject !\";</script>\n",i);
					fflush(stdout);
					host_num--;
					close(server[i].connfd);
					server[i].connfd = -2;
					continue;
				}
				// nonblocking
				int flag;
				if((flag = fcntl(server[i].connfd,F_GETFL,0)) < 0)
					cout << "F_GETFL error" << endl;
				if(fcntl(server[i].connfd,F_SETFL,flag | O_NONBLOCK) < 0)
					cout << "F_SETFL error" << endl;
			}
		}
	}

	int n;
	string recv_buff;
	char tmp[MAXLINE+1];

	while(true)
	{
		memcpy(&rset,&allset,sizeof(allset));
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
							printf("<script>document.all['m%d'].innerHTML += \"host %d connect error\";</script>\n",i,i);
							fflush(stdout);
							server[i].counter += 1;
						}
						else
							server[i].is_connect = true;
					}
					/*if(server[i].counter == 3)	// try to connect at most three times
					{
						server[i].connfd = -2;
						host_num--;
					}*/
				}
				else if(FD_ISSET(server[i].connfd,&rset))
				{
					if((n = read(server[i].connfd,tmp,MAXLINE)) > 0)
					{
						tmp[n] = 0;
						recv_buff.assign(tmp);
						//cout << n << " " << endl;
						convert_to_html(&recv_buff);
						printf("<script>document.all['m%d'].innerHTML += \"%s\";</script>\n",i,recv_buff.c_str());
						if(recv_buff[recv_buff.length()-2] == '%') // sub one space
						{
							bzero(tmp,MAXLINE+1);
							if(server[i].file != NULL)
							{
								fgets(tmp,MAXLINE+1,server[i].file);
								string cmd(tmp);
								//cout << cmd << endl;
								write(server[i].connfd,cmd.c_str(),cmd.length());
								convert_to_html(&cmd);
								printf("<script>document.all['m%d'].innerHTML += \"<b>%s</b>\";</script>\n",i,cmd.c_str());
								if(strncmp(cmd.c_str(),"exit",4) == 0)
								{
									fclose(server[i].file);
									shutdown(server[i].connfd,SHUT_WR);
									/*close(server[i].connfd);
									FD_CLR(server[i].connfd,&allset);
									server[i].connfd = -2;
									host_num--;*/
								}
							}
							else
								printf("<script>document.all['m%d'].innerHTML += \"host %d fopen error\";</script>\n",i,i);
						}
					}
					else if(n == 0)
					{
						close(server[i].connfd);
						FD_CLR(server[i].connfd,&allset);
						server[i].connfd = -2;
						host_num--;
					}
					else
						printf("<script>document.all['m%d'].innerHTML += \"host %d read error\";</script>\n",i,i);
					fflush(stdout);
				}
			}
		}
		if(host_num == 0)
			break;
	}

	return 0;
}

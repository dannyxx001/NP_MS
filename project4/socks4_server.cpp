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
#include <regex>
#include <sys/stat.h>
#define SOCKMAX 1024
#define MAXLINE 100000
#define LISTENNQ 10

using namespace std;

typedef struct socks4_pkt_request
{
	// vn and cd should use int instead of char, otherwise it can't cout except for hex
	unsigned int vn;		// version
	unsigned int cd;		// cd == 1 : CONNECT ; cd == 2 : BIND
	uint16_t dst_port;		// dst port
	struct in_addr dst_ip;	// dst ip: uint32_t
	char user_id[64];		// may have user id
	char domain_name[128];	// may have domain name if no ip
}request;

void sig_chld(int signo)
{
	pid_t pid;
	int stat;
	while((pid = waitpid(-1,&stat,WNOHANG)) > 0);
		//cout << "The process " << pid << " connection terminated\n" << endl;
	return;
}

bool sock4_request(int sockfd, request *pkt)	//handle socks4 request
{
	unsigned char recv_buff[SOCKMAX];
	int read_len;
	bzero(recv_buff,SOCKMAX);
	
	if((read_len = read(sockfd,recv_buff,SOCKMAX)) > 0)
	{
		pkt->vn = recv_buff[0];
		pkt->cd = recv_buff[1];
		// network byte oreder is upside down compared to host byte order, the two followings are network byte order
		pkt->dst_port = recv_buff[2] | recv_buff[3] << 8;
		pkt->dst_ip.s_addr = recv_buff[4] | recv_buff[5] << 8 | recv_buff[6] << 16 | recv_buff[7] << 24;
		// the followings are host byte order
		/*
		unsigend int dst_port = recv_buff[2] << 8 | recv_buff[3];
		unsigned int dst_ip = recv_buff[4] << 24 | recv_buff[5] << 16 | recv_buff[6] << 8 | recv_buff[7];
		*/
		bzero(pkt->user_id,64);
		for(int i=8;i<read_len && recv_buff[i]!='\0';i++)
		{
			pkt->user_id[i-8] = recv_buff[i];
			if(recv_buff[i+1] == '\0')
				pkt->user_id[i-7] = recv_buff[i+1];
		}

		if(recv_buff[4] == 0 && recv_buff[5] == 0 && recv_buff[6] == 0)	// if dst_ip == 0.0.0.x, use domain name
		{
			int offset_of_domain_name = read_len-1;
			while(recv_buff[--offset_of_domain_name] != '\0');
			offset_of_domain_name += 1;
			bzero(pkt->domain_name,128);
			for(int i=offset_of_domain_name;i<read_len && recv_buff[i]!='\0';i++)
			{
				pkt->user_id[i-offset_of_domain_name] = recv_buff[i];
				if(recv_buff[i+1] == '\0')
					pkt->user_id[i-offset_of_domain_name+1] = recv_buff[i+1];
			}

			struct hostent *hptr;
			if((hptr = gethostbyname(pkt->domain_name)) != NULL)
			{
				if(hptr->h_addrtype == AF_INET)
				{
					struct in_addr **pptr;
					pptr = (struct in_addr **)hptr->h_addr_list;
					pkt->dst_ip.s_addr = (*pptr)->s_addr;
					for(;*pptr!=NULL;pptr++)
						cout << "dst_ip:" << inet_ntoa(**pptr) << endl;
				}
				else
					cout << "unknown address type" << endl;
			}
			else
				cout << "gethostbyname error" << endl;
		}
		// for debug
		//cout << "***** request pkt length:" << read_len  <<" *****" << endl;
		cout << "VN: " << pkt->vn << ", CD: " << pkt->cd << ", DST_IP: " << inet_ntoa(pkt->dst_ip) << ", DST_PORT: " << ntohs(pkt->dst_port) << endl;
		// print ip
		//printf("dst_ip:%u.%u.%u.%u\n",recv_buff[4],recv_buff[5],recv_buff[6],recv_buff[7]);
		if(strlen(pkt->user_id) != 0)
			cout << "USER ID:" << pkt->user_id << endl;
		if(recv_buff[4] == 0 && recv_buff[5] == 0 && recv_buff[6] == 0)
			cout << "Domain Name:" << pkt->domain_name << endl;
		return true;
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

void sock4_reply(int sockfd, request pkt, bool valid)	// send sock4 reply
{
	unsigned char package[8];
	
	package[0] = 0;
	package[1] = valid==true?90:91;
	package[2] = ntohs(pkt.dst_port) / 256;
	package[3] = ntohs(pkt.dst_port) % 256;
	package[4] = pkt.dst_ip.s_addr & 0xFF;
	package[5] = (pkt.dst_ip.s_addr >> 8) & 0xFF;
	package[6] = (pkt.dst_ip.s_addr >> 16) & 0xFF;
	package[7] = (pkt.dst_ip.s_addr >> 24) & 0xFF;
	// for debug
	/*cout << "***** reply *****" << endl;
	printf("CD:%u ",package[1]);
	printf("dst_ip:%u.%u.%u.%u\n",package[4],package[5],package[6],package[7]);*/
	
	write(sockfd,package,sizeof(package));
}

bool deny_access(char *ip)	// "return true" means "deny"; "return false" means "allow"
{
	FILE *fp = fopen("socks.conf","r");
	char rule[32];
	bool is_null = true;				// if the file is null
	while(fgets(rule,32,fp) != NULL)
	{
		is_null = false;
		//cout << "rule:" << rule << flush;
		bool pass = true;
		int i = 0;
		while(rule[i] != '*' && ip[i] != '\0' && rule[i] != '\0')
		{
			//cout << rule[i];
			if(ip[i] != rule[i])
			{
				pass = false;
				break;
			}
			i++;
		}
		//cout << endl;
		if(pass)
		{
			fclose(fp);
			return false;
		}
	}
	fclose(fp);
	if(is_null) // only white list allow
		return false;
	else
		return true;
}

void exchange_data(int sockfd1, int sockfd2, int mode)
{
	char buff[MAXLINE+1];	// if buff is too small, it's easy to transfer fail
	int n;
	fd_set rset,allset;
	int nfds = sockfd1 > sockfd2 ? sockfd1+1 : sockfd2+1;
	bool first = true, second = true;
	
	FD_ZERO(&allset);
	FD_SET(sockfd1,&allset);
	FD_SET(sockfd2,&allset);

	while(true)
	{
		if(first == false && second == false)
		{
			if(mode == 1)
				cout << "(CONNECT mode) exchange data end" << endl;
			else if(mode == 2)
				cout << "(BIND mode) exchange data end" << endl;
			break;
		}

		memcpy(&rset,&allset,sizeof(allset));
		if(select(nfds,&rset,NULL,NULL,NULL) < 0)
		{
			cout << "select error" << endl;
			close(sockfd1);
			close(sockfd2);
			break;
		}
		
		if(FD_ISSET(sockfd1,&rset))
		{
			if((n = read(sockfd1,buff,MAXLINE)) > 0)
			{
				//buff[n] = 0;
				//cout << "sockfd1 read:" << buff << endl;
				if(second == true)
					write(sockfd2,buff,n);
			}
			else if(n == 0)
			{
				// for debug
				/*if(mode == 1)
					cout << "(CONNECT mode) sockfd1 close" << endl;
				else if(mode == 2)
					cout << "(BIND mode) sockfd1 close" << endl;*/
				close(sockfd1);						// sockfd1 connection terminate
				FD_CLR(sockfd1,&allset);			// clear sockfd1 from allset
				shutdown(sockfd2,SHUT_WR);			// send FIN to sockfd2
				first = false;
			}
			else
			{
				if(mode == 1)
					cout << "(CONNECT mode) sockfd1 read error" << endl;
				else if(mode == 2)
					cout << "(BIND mode) sockfd2 read error" << endl;
			}
		}

		if(FD_ISSET(sockfd2,&rset))
		{
			if((n = read(sockfd2,buff,MAXLINE)) > 0)
			{	
				//buff[n] = 0;
				//cout << "sockfd2 read:" << buff << endl;
				if(first == true)
					write(sockfd1,buff,n);
			}
			else if(n == 0)
			{
				// for debug
				/*if(mode == 1)
					cout << "(CONNECT mode) sockfd2 close" << endl;
				else if(mode == 2)
					cout << "(BIND mode) sockfd2 close" << endl;*/
				close(sockfd2);						// sockfd2 connection terminate
				FD_CLR(sockfd2,&allset);			// clear sockfd2 from allset
				shutdown(sockfd1,SHUT_WR);			// send FIN to sockfd1
				second = false;
			}
			else
			{	
				if(mode == 1)
					cout << "(CONNECT mode) sockfd1 read error" << endl;
				else if(mode == 2)
					cout << "(BIND mode) sockfd2 read error" << endl;
			}
		}
	}
}

int main(int argc, char *argv[], char *envp[])
{
	int listenfd, connfd;
	struct sockaddr_in servaddr;

	if(argc != 2)
	{
		cout << "usage: ./socks4_server <Port>" << endl;
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
	
	struct sockaddr_in client_addr;
	int addrlen = sizeof(client_addr);
	int child_pid;

	while(true)
	{
		if((connfd = accept(listenfd,(struct sockaddr *)&client_addr,(socklen_t *)&addrlen)) < 0)
		{
			if(errno == EINTR)
				continue;
			else
				cout << "accept error" << endl;
		}
		
		child_pid = fork();
		if(child_pid == 0)		// child process
		{
			//cout << "(child) the client connection pid: " << getpid() << endl;
			close(listenfd);
			request pkt;
			if(sock4_request(connfd,&pkt))	// if have request
			{
				if(pkt.cd == 1)		// CONNECT mode
				{
					cout << "********** CONNECT mode **********" << endl;
					if(deny_access(inet_ntoa(pkt.dst_ip)))
					{
						sock4_reply(connfd,pkt,false);
						close(connfd);
						cout << "Deny Src = " << inet_ntoa(client_addr.sin_addr) << "(" << ntohs(client_addr.sin_port) << "), ";
						cout << "Dst = " << inet_ntoa(pkt.dst_ip) << "(" << ntohs(pkt.dst_port) << ")" << endl;
					}
					else
					{
						cout << "Permit Src = " << inet_ntoa(client_addr.sin_addr) << "(" << ntohs(client_addr.sin_port) << "), ";
						cout << "Dst = " << inet_ntoa(pkt.dst_ip) << "(" << ntohs(pkt.dst_port) << ")" << endl;
						struct sockaddr_in connect_addr;
						int connect_fd;
						
						//set connection address
						bzero(&connect_addr,sizeof(connect_addr));
						connect_addr.sin_family = AF_INET;
						connect_addr.sin_port = pkt.dst_port;
						connect_addr.sin_addr.s_addr = pkt.dst_ip.s_addr;

						//prepare socket
						if((connect_fd = socket(AF_INET,SOCK_STREAM,0)) < 0)
						{
							cout << "(CONNECT mode) socket error" << endl;
							sock4_reply(connfd,pkt,false);
							exit(-1);
						}
						
						if(connect(connect_fd,(struct sockaddr *)&connect_addr,sizeof(connect_addr)) < 0)
						{
							cout << "(CONNECT mode) connect error" << endl;
							sock4_reply(connfd,pkt,false);
						}
						else
						{ 
							cout << "(CONNECT mode) connect success" << endl;
							sock4_reply(connfd,pkt,true);
							exchange_data(connfd,connect_fd,1);
						}
					}
				}
				else if(pkt.cd == 2)	// BIND mode
				{
					cout << "********** BIND mode **********" << endl;
					if(deny_access(inet_ntoa(pkt.dst_ip)))
					{
						pkt.dst_port = 0;
						pkt.dst_ip.s_addr = 0;
						sock4_reply(connfd,pkt,false);
						close(connfd);
						cout << "Deny Src = " << inet_ntoa(client_addr.sin_addr) << "(" << ntohs(client_addr.sin_port) << "), ";
						cout << "Dst = " << inet_ntoa(pkt.dst_ip) << "(" << ntohs(pkt.dst_port) << ")" << endl;
					}
					else
					{
						cout << "Permit Src = " << inet_ntoa(client_addr.sin_addr) << "(" << ntohs(client_addr.sin_port) << "), ";
						cout << "Dst = " << inet_ntoa(pkt.dst_ip) << "(" << ntohs(pkt.dst_port) << ")" << endl;
						struct sockaddr_in bind_addr;
						int bind_fd,conn_fd;
						
						// if not use srand, the rand will pick the same value repeatly then the transfer will fail (important!!)
						srand(time(NULL));
						int new_port = 50000 + rand() % 10000;

						//set server address
						bzero(&bind_addr,sizeof(bind_addr));
						bind_addr.sin_family = AF_INET;
						bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
						bind_addr.sin_port = htons(new_port);

						//prepare socket
						if((bind_fd = socket(AF_INET,SOCK_STREAM,0)) < 0)
						{
							cout << "(BIND mode) socket error" << endl;
							sock4_reply(connfd,pkt,false);
							close(bind_fd);
							exit(-1);
						}

						//bind socket with addr, port may be used
						while(bind(bind_fd,(struct sockaddr *)&bind_addr,sizeof(bind_addr)) < 0)
						{
							new_port = 50000 + rand() % 10000;
							cout << "(BIND mode) bind error, try to bind another port: " << new_port << endl;
							bind_addr.sin_port = htons(new_port);
						}

						//listen
						if(listen(bind_fd,LISTENNQ) < 0)
						{
							cout << "(BIND mode) listen error" << endl;
							sock4_reply(connfd,pkt,false);
							close(bind_fd);
							exit(-1);
						}
						
						pkt.dst_port = bind_addr.sin_port;
						pkt.dst_ip.s_addr = 0;
						// need to send reply, otherwise will block
						sock4_reply(connfd,pkt,true);
						
						if((conn_fd = accept(bind_fd,(struct sockaddr *)NULL,NULL)) < 0)
						{
							cout << "(BIND mode) accept error" << endl;
							sock4_reply(connfd,pkt,false);
						}
						else
						{
							cout << "(BIND mode) accept success" << endl;
							// need to send reply again, otherwise will transfer fail
							sock4_reply(connfd,pkt,true);
							exchange_data(connfd,conn_fd,2);
						}
						close(bind_fd);
					}
				}
				else
					cout << "mode error (neither CONNECT nor BIND modes)" << endl;
			}
			return 0;
		}
		else if(child_pid > 0)	// parent process
		{
			//cout << "(parent) the listen pid: " << getpid() << endl;
			close(connfd);
			// signal must call in parent, otherwise if child process call fork again then it will catch wrong signal
			signal(SIGCHLD,sig_chld);
		}
		else					// fork fail
			cout << "fail to fork" << endl;
	}

	return 0;
}

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
#define MAXLINE 4096
#define LISTENNQ 10

using namespace std;

void sig_chld(int signo)
{
	pid_t pid;
	int stat;
	while((pid = waitpid(-1,&stat,WNOHANG)) > 0)
		cout << "The process " << pid << " connection terminated\n" << endl;
	return;
}

void print404()
{
	cout << "HTTP/1.1 404 Not Found\r\n\
	Content-type: text/html\r\n\r\n\
	<html>\n<body>\n\
	<img src=\"http://zenit.senecac.on.ca/wiki/imgs/404-not-found.gif\"></img>\n\
	</body>\n</html>\n" << flush;
}

void handle_request(char *method, char *request, int connfd)
{
	// parse the request
	char *next_tok = strtok(request,"?\r\n");
	char *script_name = next_tok;
	next_tok = strtok(NULL,"\r\n");
	char *query = next_tok;
	// for debug
	/*if(script_name != NULL)
		cout << script_name << endl;
	if(query != NULL)
		cout << query << endl;*/

	setenv("CONTENT_LENGTH","0",1);
	setenv("REQUEST_METHOD",method,1);
	setenv("SCRIPT_NAME",script_name,1);
	setenv("REMOTE_ADDR","140.113.168.191",1);
	setenv("REMOTE_HOST","npuser.nctu.edu.tw",1);
	setenv("REMOTE_USER","npuser",1);
	setenv("REMOTE_IDENT","np",1);
	setenv("AUTH_TYPE","np_auth",1);

	//dup the socket of browser to stdout
	close(1);
	dup(connfd);

	struct stat s;
	if(stat(script_name,&s) == -1) // if not find .cgi or .html file
	{
		print404();
		return;
	}
	else
		cout << "HTTP/1.1 200 OK\n" << flush;

	if(regex_match(script_name,regex(".*\\.cgi")))			// exec .cgi
	{
		setenv("PATH","/u/gcs/104/0456516/public_html/",1);
		if(query != NULL)
			setenv("QUERY_STRING",query,1);
		char *argv[] = {script_name,NULL};
		if(execvp(script_name,argv) == -1)
		{
			cout << "Content-type: text/html\r\n\r\nexec fail\r\n" << flush;
			print404();
		}
	}
	else if(regex_match(script_name,regex(".*\\.html")))	// read .html
	{
		cout << "Content-type: text/html\r\n\r\n" << flush;
		FILE *fp_html;
		fp_html = fopen(script_name,"r");
		char tmp[MAXLINE];
		bzero(tmp,MAXLINE);
		while(fgets(tmp,MAXLINE,fp_html) != NULL)
		{
			cout << tmp << flush;
			bzero(tmp,MAXLINE);
		}
		fclose(fp_html);
	}
	else
		cout << "Content-type: text/html\r\n\r\nneither .cgi nor .html\r\n" << flush;
	return;
}

int main(int argc, char *argv[], char *envp[])
{
	if(chdir("/u/gcs/104/0456516/public_html/") == -1)
		cout << "Change directory error!" << endl;

	int listenfd, connfd, n;
	struct sockaddr_in servaddr;
	string recv_buff;
	char tmp[MAXLINE+1];

	if(argc != 2)
	{
		cout << "usage: ./http_server <Port>" << endl;
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

	int child_pid;

	while(true)
	{
		if((connfd = accept(listenfd,(struct sockaddr *)NULL,NULL)) < 0)
		{
			if(errno == EINTR)
				continue;
			else
				cout << "accept error" << endl;
		}
		
		child_pid = fork();
		if(child_pid == 0)		// child process
		{
			cout << "(child) the client connection pid: " << getpid() << endl;
			close(listenfd);
			recv_buff = "";
			while((n = read(connfd,tmp,MAXLINE)) > 0)
			{
				recv_buff.append(tmp,n);
				if(tmp[n-1] != '\n')
			    	continue;
				else
				{
					tmp[0] = 0;
			    	recv_buff.append(tmp,1);
			    	break;
				}
			}
			//cout << recv_buff << endl;            // for debug
			// parse the http info
			char *next_tok = strtok((char *)recv_buff.c_str()," \r\n");	// GET or POST
			if(next_tok == NULL || strcmp(next_tok,"GET") != 0)	
			{
				cout << "no GET method" << endl;
				close(connfd);
				return 0;
			}
			char *method = next_tok;
			next_tok = strtok(NULL,"/ \r\n");							// .cgi or .html (request)
			if(next_tok == NULL || strcmp(next_tok,"HTTP") == 0)
			{
				cout << "no request for .cgi or .html" << endl;
				close(connfd);
				return 0;
			}
			char *request = next_tok;				
			next_tok = strtok(NULL," \r\n");							// HTTP/1.1
			if(next_tok == NULL)
			{
				cout << "no HTTP/1.1" << endl;
				close(connfd);
				return 0;
			}
			char *stat = next_tok;
			//cout << method << " " << request << " " << stat << endl; // for debug
			handle_request(method,request,connfd);
			close(connfd);
			return 0;
		}
		else if(child_pid > 0)	// parent process
		{
			cout << "(parent) the listen pid: " << getpid() << endl;
			close(connfd);
			// signal must call in parent, otherwise if child process call fork again then it will catch wrong signal
			signal(SIGCHLD,sig_chld);
		}
		else					// fork fail
			cout << "fail to fork" << endl;
	}

	return 0;
}
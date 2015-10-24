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
#define MAXLINE 15000
#define LISTENNQ 10
using namespace std;

typedef struct arg_list{
	char *arg;
	arg_list *next;
}arg_list;

void sig_chld(int signo)
{
	pid_t pid;
	int stat;
	while((pid = waitpid(-1,&stat,WNOHANG)) > 0)
		cout << "The process " << pid << " connection terminated\n" << endl;
	return;
}

// find end of one cmd
bool is_end_of_cmd(char *tok)
{
	if(tok == NULL || strcmp(tok,"|") == 0 || strcmp(&tok[0],"|") == 0 || strcmp(&tok[0],"!") == 0 || strcmp(tok,">") == 0)
		return true;
	else
		return false;
}

// handle arg of one cmd
void handle_arg(int *argc, char ***argv, char *cmd, char **tok)
{
	arg_list *head = (arg_list*)malloc(sizeof(arg_list));
	// current token
	arg_list *tmp_c = head;
	while(is_end_of_cmd(*tok) != true)
	{
		*argc += 1;
		tmp_c->arg = *tok;
		// next token
		arg_list *tmp_n = (arg_list*)malloc(sizeof(arg_list));
		tmp_c->next = tmp_n;
		tmp_c = tmp_n;
		*tok = strtok(NULL," \r\n");
	}
	//cout << *argc << endl;
	*argv = (char **)malloc(*argc+2);
	(*argv)[0] = (char *)malloc(sizeof(cmd));
	strcpy((*argv)[0],cmd);
	//cout << (*argv)[0] << endl;
	tmp_c = head;
	for(int i=1;i<*argc+1;i++)
	{
		arg_list *tmp_n = tmp_c->next;
		(*argv)[i] = (char *)malloc(sizeof(tmp_c->arg));
		strcpy((*argv)[i],tmp_c->arg);
		//cout << (*argv)[i] << endl;
		free(tmp_c);
		tmp_c = tmp_n;
	}
	(*argv)[*argc+1] = NULL;
}

// exec the cmd
void do_cmd(char *cmd, char **argv, string *ret_msg)
{
	int pipe1[2];
	if(pipe(pipe1) < 0)
	{
		cout << "fail to create pipe" << endl;
		return;
	}

	int child_pid = fork();
	if(child_pid == 0)				// child process write
	{
		int stdout_copy = dup(1);	// copy origin stdout
		close(1);					// close stdout
		close(2);					// close stderr
		close(pipe1[0]);			// close pipe read
		dup(pipe1[1]);				// dup pipe write to fileno(stdout)
		dup(pipe1[1]);				// dup pipe write to fileno(stderr)
		close(pipe1[1]);			// close pipe write
		if(execvp(cmd,argv) == -1)	// exec cmd
		{
			// back to origin stdout
			close(1);
			dup2(stdout_copy,1);	
			cout << "fail to exec" << endl;
		}
		exit(0);
	}
	else if(child_pid > 0)			//parent process read
	{
		int stdin_copy = dup(0);	// copy origin stdin
		close(0);					// close stdin
		close(pipe1[1]);			// close pipe write
		dup(pipe1[0]);				// dup pipe read to fileno(stdin)
		close(pipe1[0]);			// close pipe read
		pid_t pid;
		int stat;
		while((pid = wait(&stat)) != child_pid); // block
		char tmp[1024];
		int n;
		while((n = read(fileno(stdin),tmp,1024)) > 0)
		{
			tmp[n] = 0;
			(*ret_msg).append(tmp);
		}
		//back to origin stdin
		close(1);
		dup2(stdin_copy,0);
		//cout << *ret_msg << endl;
		return;
	}
	else							// fork fail
	{
		cout << "fail to fork" << endl;
		return;
	}
}

string handle_cmd(char* cmd)
{
	char *next_tok = strtok(cmd," \r\n");
	char *tok_cmd = next_tok;
	next_tok = strtok(NULL," \r\n");
	string ret_msg ("");
	int arg_c = 0;
	char **arg_v;
	
	while(tok_cmd != NULL)
	{
		if(strcmp(tok_cmd,"exit") == 0)
		{
			ret_msg = "exit";
			break;
		}
		else if(strcmp(tok_cmd,"printenv") == 0)
		{
			if(next_tok != NULL)
			{
				ret_msg = next_tok;
				ret_msg += "=";
				if(getenv(next_tok) != NULL)
					ret_msg.append(getenv(next_tok));
				ret_msg += "\n";
				break;
			}
		}
		else if(strcmp(tok_cmd,"setenv") == 0)
		{
			char *env = next_tok;
			next_tok = strtok(NULL," \r\n");
			if(env != NULL && next_tok != NULL)
				setenv(env,next_tok,1);
			break;
		}
		else if(strcmp(tok_cmd,"ls") == 0 || strcmp(tok_cmd,"cat") == 0 || strcmp(tok_cmd,"number") == 0 || strcmp(tok_cmd,"removetag") == 0 || strcmp(tok_cmd,"removetag0") == 0 || strcmp(tok_cmd,"noop") == 0)
		{
			handle_arg(&arg_c,&arg_v,tok_cmd,&next_tok);
			do_cmd(cmd,arg_v,&ret_msg);
		}
		/*else if(strcmp(tok_cmd,"cat") == 0)
		{}
		else if(strcmp(tok_cmd,"number") == 0)
		{}
		else if(strcmp(tok_cmd,"removetag") == 0)
		{}
		else if(strcmp(tok_cmd,"removetag0") == 0)
		{}
		else if(strcmp(tok_cmd,"noop") == 0)
		{}*/
		else
		{
			string unknown_cmd = tok_cmd;
			ret_msg = "Unknown command: [" + unknown_cmd + "].\n";
			break;
		}
		tok_cmd = next_tok;
	}
	return ret_msg;
}

int main(int argc, char *argv[], char *envp[])
{
	/* env variable test */
	/*for(int i=0;envp[i]!=(char *)0;i++)
		cout << envp[i] << endl;*/
	//cout << getenv("path") << endl;
	setenv("PATH","bin:.",1);
	
	int listenfd, connfd, n;
	struct sockaddr_in servaddr;
	string send_buff;
	string welcome_msg = "****************************************\n** Welcome to the information server. **\n****************************************\n% ";
	char recv_buff[MAXLINE+1];

	if(argc != 2)
	{
		cout << "usage: ./server <Port>" << endl;
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
	signal(SIGCHLD,sig_chld);
	
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
			send_buff.assign(welcome_msg);
			write(connfd,send_buff.c_str(),send_buff.length());
			while((n = read(connfd,recv_buff,15000)) > 0)
			{
				recv_buff[n] = 0;
				send_buff = handle_cmd(recv_buff);
				if(send_buff.compare("exit") == 0)
				{
					close(connfd);
					break;
				}
				send_buff.append("% ");
				//cout << send_buff << endl;
				write(connfd,send_buff.c_str(),send_buff.length());
			}
			exit(0);
		}
		else if(child_pid > 0)	// parent process
		{
			cout << "(parent) the listen pid: " << getpid() << endl;
			close(connfd);
		}
		else					// fork fail
			cout << "fail to fork" << endl;
	}
	return 0;
}
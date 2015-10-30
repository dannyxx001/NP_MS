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
#define MAXLINE 4096
#define LISTENNQ 10
using namespace std;

// arg list use for every cmd argv
typedef struct arg_list{
	char *arg;
	arg_list *next;
}arg_list;

// pipe list use for |N or !N
typedef struct pipe_list{
	int my_pipe[2];
	int counter;
	pipe_list *next;
}pipe_list;

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
	// character use '', string use ""
	if(tok == NULL || strcmp(tok,"|") == 0 || strcmp(tok,">") == 0 || tok[0] == '|' || tok[0] == '!')
		return true;
	else
		return false;
}

// read from file descriptor
string do_read(int fd)
{
	char tmp[MAXLINE+1];
	int n;
	string msg = ("");
	while((n = read(fd,tmp,MAXLINE)) > 0)
	{
		tmp[n] = 0;
		msg.append(tmp);
	}
	return msg;
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
		if((*tok = strtok(NULL," \r\n")) != NULL)
		{
			arg_list *tmp_n = (arg_list*)malloc(sizeof(arg_list));
			tmp_c->next = tmp_n;
			tmp_c = tmp_n;
		}
		else
			tmp_c->next = NULL;
	}
	//cout << *argc+2 << endl; // use for debug

	// argv include cmd, arg of cmd, NULL
	*argv = (char **)malloc(*argc+1);
	(*argv)[0] = (char *)malloc(sizeof(cmd));
	strcpy((*argv)[0],cmd);
	//cout << (*argv)[0] << endl; // use for debug
	
	tmp_c = head;
	for(int i=1;i<*argc+1;i++)
	{
		arg_list *tmp_n = tmp_c->next;
		(*argv)[i] = (char *)malloc(sizeof(tmp_c->arg));
		strcpy((*argv)[i],tmp_c->arg);
		free(tmp_c);
		tmp_c = tmp_n;
	}
	// to handle multiple argv, the last must be NULL
	(*argv)[*argc+1] = NULL;
}

int convert_to_int(char *next_tok)
{
	char *num = strtok(next_tok," !|\r\n");
	bool check_num = true;
	for(int i=0;i<strlen(num);i++)
		if(!isdigit(num[i]))
			check_num = false;
	int number = 0;
	if(check_num == true)
	{
		number = atoi(num);
		return number;
	}
	else
	{
		cout << "Pipe number error!" << endl;
		return -1;
	}
}

// do |N or !N
void do_pipe_N(pipe_list **head, int num, int stdin_copy)
{
	pipe_list *my_pipe = *head;
	while(my_pipe != NULL)
	{
		if(my_pipe->counter == num)
			break;
		my_pipe = my_pipe->next;
	}
	string msg = ("");
	if(my_pipe == NULL)											// if the num counter of pipe not exist 
	{
		my_pipe = (pipe_list*)malloc(sizeof(pipe_list));
		my_pipe->counter = num;
		my_pipe->next = NULL;
		if(pipe(my_pipe->my_pipe) < 0)
		{
			cout << "fail to create pipe" << endl;
			return;
		}
		pipe_list *pipe_c = *head;
		if(pipe_c == NULL)										// the first one
			*head = my_pipe;
		else													// the second or after
		{
			while(pipe_c != NULL)
			{
				if(pipe_c->counter > num)						// if insert in the first
				{
					my_pipe->next = pipe_c;
					*head = my_pipe;
					break;
				}
				else if(pipe_c->next == NULL)					// if insert in the last
				{	
					pipe_c->next = my_pipe;
					break;
				}
				else if(pipe_c->next->counter > num)			// insert from small to big num
				{
					my_pipe->next = pipe_c->next;
					pipe_c->next = my_pipe;
					break;
				}
				pipe_c = pipe_c->next;
			}
		}
		msg += do_read(fileno(stdin));							// read from stdin
		write(my_pipe->my_pipe[1],msg.c_str(),msg.length());
		close(my_pipe->my_pipe[1]);								// must close write, otherwise the read will block	
	}
	else														// if the num counter of pipe exist 
	{
		msg += do_read(my_pipe->my_pipe[0]);					// read from previous pipe first
		close(my_pipe->my_pipe[0]);								// close previous pipe read
		msg += do_read(fileno(stdin));							// read from stdin second
		if(pipe(my_pipe->my_pipe) < 0)							// create new pipe
		{
			cout << "fail to create pipe" << endl;
			return;
		}
		write(my_pipe->my_pipe[1],msg.c_str(),msg.length());
		close(my_pipe->my_pipe[1]);								// must close write, otherwise the read will block
	}
	//back to origin stdin
	close(0);
	dup2(stdin_copy,0);
	// release msg memmory
	msg.resize(0);
	msg.shrink_to_fit();
	return;
}

// exec the cmd
void do_cmd(char *cmd, char **argv, string *ret_msg, char **next_tok, int stdin_copy, pipe_list **head, bool first_time_do_cmd)
{
	// handle pipe N
	if(first_time_do_cmd == true && *head != NULL && (*head)->counter == 0)
	{
		close(0);
		dup((*head)->my_pipe[0]);
	}

	int pipe_out[2];				// use for pipe stdout
	int pipe_err[2];				// use for pipe stderr	
	if(pipe(pipe_out) < 0 || pipe(pipe_err) < 0)
	{
		cout << "fail to create pipe out or err" << endl;
		return;
	}

	int child_pid = fork();
	if(child_pid == 0)				// child process write
	{
		//cout << cmd << endl;		// use for debug
		//cout << "child pid: " << getpid() << endl; // use for debug
		int stdout_copy = dup(1);	// copy origin stdout
		close(1);					// close stdout
		close(2);					// close stderr
		close(pipe_out[0]);			// close pipe_out read
		close(pipe_err[0]);			// close pipe_err read
		dup(pipe_out[1]);			// dup pipe_out write to fileno(stdout)
		dup(pipe_err[1]);			// dup pipe_out write to fileno(stderr)
		close(pipe_out[1]);			// close pipe_out write
		close(pipe_err[1]);			// close pipe_err write
		if(execvp(cmd,argv) == -1)	// exec cmd
		{
			string unknown_cmd = cmd;
			string msg = "Unknown command: [" + unknown_cmd + "].\n";
			write(fileno(stdout),msg.c_str(),msg.length());
			// back to origin stdout
			close(1);
			dup2(stdout_copy,1);	
			cout << "fail to exec" << endl;
		}
		exit(0);
	}
	else if(child_pid > 0)			//parent process read
	{
		//cout << "my pid: " << getpid() << endl; // use for debug
		close(pipe_out[1]);			// close pipe_out write
		close(pipe_err[1]);			// close pipe_err write

		pid_t pid;
		int stat;
		while((pid = wait(&stat)) != child_pid); // block
		//cout << "The cmd pid: " << pid << " connection terminated\n" << endl; // use for debug

		// if next_tok is |N or !N
		if(*next_tok != NULL && strcmp(*next_tok,"|") != 0 && strcmp(*next_tok,">") != 0 && ((*next_tok)[0] == '|' || (*next_tok)[0] =='!'))
		{
			int number = 0;
			if((*next_tok)[0] == '|') // |N
			{
				if((number = convert_to_int(*next_tok)) == -1)
				{	
					cout << "Pipe number error" << endl;
					return;
				}
				close(0);			// close stdin
				dup(pipe_out[0]);	// dup pipe_out read to fileno(stdin)
				close(pipe_out[0]);	// close pipe_out read
				do_pipe_N(head,number,stdin_copy);

				*next_tok = strtok(NULL," \r\n");

				close(0);			// close stdin
				dup(pipe_err[0]);	// dup pipe_err read to fileno(stdin)
				close(pipe_err[0]);	// close pipe_err read

				if(*next_tok != NULL && (*next_tok)[0] =='!') // !N
				{
					if((number = convert_to_int(*next_tok)) == -1)
					{	
						cout << "Pipe number error" << endl;
						return;
					}
					do_pipe_N(head,number,stdin_copy);
				}
				else				// output stderr
					(*ret_msg) += do_read(fileno(stdin)); // read from stdin
			}
			else					// !N
			{	
				if((number = convert_to_int(*next_tok)) == -1)
				{	
					cout << "Pipe number error" << endl;
					return;
				}
				close(0);			// close stdin
				dup(pipe_err[0]);	// dup pipe_err read to fileno(stdin)
				close(pipe_err[0]);	// close pipe_err read
				do_pipe_N(head,number,stdin_copy);

				*next_tok = strtok(NULL," \r\n");
				
				close(0);			// close stdin
				dup(pipe_out[0]);	// dup pipe_out read to fileno(stdin)
				close(pipe_out[0]);	// close pipe_out read

				if(*next_tok != NULL && (*next_tok)[0] =='|') // |N
				{
					if((number = convert_to_int(*next_tok)) == -1)
					{	
						cout << "Pipe number error" << endl;
						return;
					}
					do_pipe_N(head,number,stdin_copy);
				}
				else				// output stdout
					(*ret_msg) += do_read(fileno(stdin)); // read from stdin
			}
			return;
		}

		close(0);					// close stdin
		dup(pipe_err[0]);			// dup pipe_err read to fileno(stdin)
		close(pipe_err[0]);			// close pipe_err read

		// read from stderr first
		(*ret_msg) += do_read(fileno(stdin));

		close(0);					// close stdin
		dup(pipe_out[0]);			// dup pipe_out read to fileno(stdin)
		close(pipe_out[0]);			// close pipe_out read

		// pipe to next cmd
		if(*next_tok != NULL && strcmp(*next_tok,"|") == 0)
		{
			*next_tok = strtok(NULL," \r\n");
			return;
		}

		// output to a file
		if(*next_tok != NULL && strcmp(*next_tok,">") == 0)
		{
			*next_tok = strtok(NULL," \r\n");
			// next token is ">" filename
			if(*next_tok != NULL)
			{
				ofstream output_file;
				char *filename = *next_tok;
				output_file.open(filename,ofstream::out);
				string file_msg = do_read(fileno(stdin));
				output_file << file_msg;
				file_msg.resize(0);
				file_msg.shrink_to_fit();
				output_file.close();
				*next_tok = strtok(NULL," \r\n");
			}
			else
				cout << "No filename!" << endl;
			return;
		}

		// read from stdout second
		(*ret_msg) += do_read(fileno(stdin));

		//back to origin stdin
		close(0);
		dup2(stdin_copy,0);
		//cout << *ret_msg << endl; // use for debug
		return;
	}
	else							// fork fail
	{
		cout << "fail to fork" << endl;
		return;
	}
}

void all_list_add_or_sub(pipe_list **head, bool check)
{
	int num = -1;
	if(check == false)			// check is normal (-1) or not (+1)
		num = 1;
	pipe_list *pipe_c = *head;
	while(pipe_c != NULL)
	{
		pipe_c->counter += num;
		pipe_c = pipe_c->next;
	}
}

string handle_cmd(char* cmd, pipe_list **head)
{
	char *next_tok = strtok(cmd," \r\n");	// always be next token of cmd token 
	char *tok_cmd = next_tok;				// record cmd token ex: ls, cat, number
	string ret_msg ("");					// return message
	int argc = 0;							// the argc of cmd
	char **argv;							// the argv of cmd
	int stdin_copy = dup(0);				// copy origin stdin

	all_list_add_or_sub(head,true);			// when doing new line cmd, all pipe lists counter sub one 
	bool first_time_do_cmd = true;			// to handle read from pipe counter 0

	while(tok_cmd != NULL)
	{
		next_tok = strtok(NULL," \r\n");
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
			}
			else
				cout << "No argv!" << endl;
			next_tok = strtok(NULL," \r\n");
			if(next_tok != NULL && strcmp(next_tok,">") == 0)
			{
				next_tok = strtok(NULL," \r\n");
				// next token is ">" filename
				if(next_tok != NULL)
				{
					ofstream output_file;
					char *filename = next_tok;
					output_file.open(filename,ofstream::out);
					output_file << ret_msg;
					output_file.close();
					ret_msg.resize(0);
					ret_msg.shrink_to_fit();
				}
				else
					cout << "No filename!" << endl;
			}
			break;
		}
		else if(strcmp(tok_cmd,"setenv") == 0)
		{
			char *env = next_tok;
			next_tok = strtok(NULL," \r\n");
			if(env != NULL && next_tok != NULL)
				setenv(env,next_tok,1);
			else
				cout << "No argv!" << endl;
			break;
		}
		else if(strcmp(tok_cmd,"ls") == 0 || strcmp(tok_cmd,"cat") == 0 || strcmp(tok_cmd,"number") == 0 || strcmp(tok_cmd,"removetag") == 0 || strcmp(tok_cmd,"removetag0") == 0 || strcmp(tok_cmd,"noop") == 0)
		{
			handle_arg(&argc,&argv,tok_cmd,&next_tok);
			do_cmd(tok_cmd,argv,&ret_msg,&next_tok,stdin_copy,head,first_time_do_cmd);
			argc = 0;
			first_time_do_cmd = false;
		}
		else
		{
			all_list_add_or_sub(head,false);
			string unknown_cmd = tok_cmd;
			ret_msg += "Unknown command: [" + unknown_cmd + "].\n";
			// only pipe can't pass unknow command
			close(0);
			dup2(stdin_copy,0);
			break;
		}
		tok_cmd = next_tok;
	}

	// clean the pipe which had done
	if(*head != NULL && (*head)->counter == 0)
	{
		close((*head)->my_pipe[0]);
		pipe_list *tmp = *head;
		*head = (*head)->next;
		free(tmp);
	}

	return ret_msg;
}

int main(int argc, char *argv[], char *envp[])
{
	// initialize directory path
	if(chdir("./ras") == -1)
		cout << "Change directory error!" << endl;
	//cout << get_current_dir_name() << endl; // for debug

	// initialize env PATH
	setenv("PATH","bin:.",1);
	
	int listenfd, connfd, n;
	struct sockaddr_in servaddr;
	string send_buff;
	string welcome_msg = "****************************************\n** Welcome to the information server. **\n****************************************\n% ";
	string recv_buff;
	char tmp[MAXLINE+1];
	pipe_list *pipe_head = NULL;

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
			while(true)
			{
				recv_buff.resize(0);
				recv_buff.shrink_to_fit();
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
				//cout << recv_buff.length() << endl;   // for debug

				send_buff.resize(0);
				send_buff.shrink_to_fit();
				send_buff = handle_cmd((char *)recv_buff.c_str(),&pipe_head);
				if(send_buff.compare("exit") == 0)
				{
					close(connfd);
					break;
				}
				send_buff.append("% ");
				write(connfd,send_buff.c_str(),send_buff.length());
			}
			exit(0);
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
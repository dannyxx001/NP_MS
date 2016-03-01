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
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <sys/stat.h>
#define MAXLINE 4096
#define LISTENNQ 10
#define SHMKEY_C ((key_t)1126)	// client list
#define SHMKEY_B ((key_t)5209)	// broadcast msg
#define PERMS 0666
using namespace std;

// pipe list use for |N or !N
typedef struct pipe_list{
	int my_pipe[2];
	int counter;
	pipe_list *next;
}pipe_list;

// define client struct
typedef struct client_info
{
	int connfd;
	char name[21];
	struct sockaddr_in addr;
	pid_t pid;
}client_info;

// arg list use for every cmd argv
typedef struct arg_list{
	char *arg;
	arg_list *next;
}arg_list;

client_info *client_list;
client_info client;
char *broadcast_addr;
int shmid_c = -1;
int shmid_b = -1;
sem_t *server_sem;
sem_t *client_sem;

void signal_term(int signo)
{
	sem_close(server_sem);
	sem_close(client_sem);
	if(shmdt(client_list) < 0)
		cout << "parent: can't detach shared memory" << endl;
	if(shmdt(broadcast_addr) < 0)
		cout << "parent: can't detach shared memory" << endl;
	if(shmctl(shmid_c,IPC_RMID,NULL) < 0)
		cout << "parent: can't remove shared memory" << endl;
	if(shmctl(shmid_b,IPC_RMID,NULL) < 0)
		cout << "parent: can't remove shared memory" << endl;
	exit(0);
}

void signal_handler(int signo)
{
	string send_buff = string(broadcast_addr);
	write(client.connfd,send_buff.c_str(),send_buff.length());
	send_buff.resize(0);
	send_buff.shrink_to_fit();
	sem_post(client_sem);
}

void sig_chld(int signo)
{
	pid_t pid;
	int stat;
	while((pid = waitpid(-1,&stat,WNOHANG)) > 0)
		cout << "The process " << pid << " connection terminated" << endl;
	return;
}

// find end of one cmd
bool is_end_of_cmd(char *tok)
{
	// character use '', string use ""
	if(tok == NULL || strcmp(tok,"|") == 0 || strcmp(tok,">") == 0 || tok[0] == '|' || tok[0] == '!' || tok[0] == '>' || tok[0] == '<')
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
	int number = 0;
	for(int i=1;i<5;i++)
	{
		if(!isdigit(next_tok[i]))
			break;
		else
		{
			number *= 10;
			number += (next_tok[i] - '0');
		}
	}
	return number;
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

// do >N
bool do_public_pipe_N(int num, int stdin_copy, int pipe_out, int pipe_err)
{
	string msg = "/tmp/fifo_jl" + to_string(num);
	const char *filename = msg.c_str();

	if(mkfifo(filename,O_CREAT|O_EXCL|PERMS) < 0)
	{
		if(errno != EEXIST)
			cout << "mkfifo error" << endl;
		return false;
	}

	int child_pid;
	child_pid = fork();
	if(child_pid == 0)	// fork a process do FIFO write block
	{	
		// avoid not clear close
		close(client.connfd);
		//cout << "filename:" << filename << endl;
		int w_fd;
		if((w_fd = open(filename,O_WRONLY)) == -1)
		{
			cout << strerror(errno) << endl;
			cout << ">N fifo write open error" << endl;
		}

		msg = "";
		close(0);
		dup(pipe_err);											// pipe stderr to public pipe
		close(pipe_err);
		msg += do_read(fileno(stdin));							// read pipe_err from stdin
		close(0);
		dup(pipe_out);											// pipe stdout to public pipe
		close(pipe_out);
		msg += do_read(fileno(stdin));							// read pipe_out from stdin

		write(w_fd,msg.c_str(),msg.length());
		msg.resize(0);
		msg.shrink_to_fit();
		close(w_fd);
		exit(0);
	}

	close(pipe_err);
	close(pipe_out);
	//back to origin stdin
	close(0);
	dup2(stdin_copy,0);
	// release msg memmory
	msg.resize(0);
	msg.shrink_to_fit();
	return true;
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
	int pipe_unk[2];				// use for checking unknown cmd
	if(pipe(pipe_out) < 0 || pipe(pipe_err) < 0 || pipe(pipe_unk) < 0)
	{
		cout << "fail to create pipe out or err or unk" << endl;
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
		close(pipe_unk[0]);			// close pipe_unk read
		dup(pipe_out[1]);			// dup pipe_out write to fileno(stdout)
		dup(pipe_err[1]);			// dup pipe_out write to fileno(stderr)
		close(pipe_out[1]);			// close pipe_out write
		close(pipe_err[1]);			// close pipe_err write
		if(execvp(cmd,argv) == -1)	// exec cmd will become another program so need't close connfd
		{
			string unknown_cmd = cmd;
			string msg = "Unknown command: [" + unknown_cmd + "].\n";
			write(pipe_unk[1],msg.c_str(),msg.length());
			// back to origin stdout
			close(1);
			dup2(stdout_copy,1);
			cout << unknown_cmd << endl;
			cout << "fail to exec" << endl;
		}
		close(pipe_unk[1]);			// close pipe_unk write
		exit(0);
	}
	else if(child_pid > 0)			//parent process read
	{
		//cout << "my pid: " << getpid() << endl; // use for debug
		close(pipe_out[1]);			// close pipe_out write
		close(pipe_err[1]);			// close pipe_err write
		close(pipe_unk[1]);			// close pipe_unk write

		pid_t pid;
		int stat;
		while((pid = wait(&stat)) != child_pid); // block
		//cout << "The cmd pid: " << pid << " connection terminated\n" << endl; // use for debug

		(*ret_msg) += do_read(pipe_unk[0]);
		if((*ret_msg).find("Unknown command:") != string::npos)
		{
			close(pipe_out[0]);
			close(pipe_err[0]);
			close(0);
			dup2(stdin_copy,0);
			return;
		}

		// if next_tok is |N or !N
		if(*next_tok != NULL && strcmp(*next_tok,"|") != 0 && strcmp(*next_tok,">") != 0 && ((*next_tok)[0] == '|' || (*next_tok)[0] =='!'))
		{
			int number = 0;
			if((*next_tok)[0] == '|') // |N
			{
				if((number = convert_to_int(*next_tok)) == 0)
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

				if(*next_tok != NULL && (*next_tok)[0] == '!') // !N
				{
					if((number = convert_to_int(*next_tok)) == 0)
					{	
						cout << "Pipe number error" << endl;
						return;
					}
					do_pipe_N(head,number,stdin_copy);
					*next_tok = strtok(NULL," \r\n");
				}
				else				// output stderr
					(*ret_msg) += do_read(fileno(stdin)); // read from stdin
			}
			else					// !N
			{	
				if((number = convert_to_int(*next_tok)) == 0)
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
					if((number = convert_to_int(*next_tok)) == 0)
					{	
						cout << "Pipe number error" << endl;
						return;
					}
					do_pipe_N(head,number,stdin_copy);
					*next_tok = strtok(NULL," \r\n");
				}
				else				// output stdout
					(*ret_msg) += do_read(fileno(stdin)); // read from stdin
			}
			return;
		}

		// >N
		if(*next_tok != NULL && strcmp(*next_tok,">") != 0 && (*next_tok)[0] == '>')
		{
			int number = 0;
			if((number = convert_to_int(*next_tok)) == 0)
			{	
				cout << "Public pipe number error" << endl;
			}
			else
			{
				if(do_public_pipe_N(number,stdin_copy,pipe_out[0],pipe_err[0]) == true)
					(*ret_msg) += "public pipe true";
				else
					(*ret_msg) += "*** Error: public pipe #" + to_string(number) + " already exists. ***\n";
				*next_tok = strtok(NULL," \r\n");
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

// broadcast to others except myself
void broadcast(client_info *client_list, string msg, int j)
{
	//cout << "login " << j+1 <<" broadcast !" << endl;
	int counter = 0;
	bzero(broadcast_addr,1024);
	strcpy(broadcast_addr,msg.c_str());
	client_info target;
	for(int i=0;i<30;i++)
	{
		memcpy(&target,&client_list[i],sizeof(client_info));
		if(target.connfd != -1 && i != j && target.pid > 0)
		{
			//cout << "connfd: " << target.connfd << " name: " << target.name << " pid:" << target.pid << endl;
			kill(target.pid,SIGUSR1);
			counter += 1;
		}
	}
	int value = 0;
	sem_getvalue(client_sem,&value);
	while(counter > 0 || value > 0)
	{
		//cout << "counter: " << counter << " login " << j+1 <<" kill wait:" << value << endl;
		// wait for all get broadcast msg
		sem_wait(client_sem);
		counter -= 1;
		sem_getvalue(client_sem,&value);
	}
}

void unicast(client_info *client_list, string msg, int id)
{
	bzero(broadcast_addr,1024);
	strcpy(broadcast_addr,msg.c_str());
	client_info target;
	memcpy(&target,&client_list[id],sizeof(client_info));
	if(target.connfd != -1)
	{
		kill(target.pid,SIGUSR1);
		// wait for target get msg
		sem_wait(client_sem);
	}
}

string handle_cmd(char* cmd, pipe_list **head, client_info *client, int j, client_info *client_list)
{
	string origin_cmd = string(cmd);
	// erase /r/n from origin cmd
	if(origin_cmd.find_last_not_of("\r\n") != string::npos)
		origin_cmd.erase(origin_cmd.find_last_not_of("\r\n")+1);
	string broadcast_msg ("");

	char *next_tok = strtok(cmd," \r\n");	// always be next token of cmd token 
	char *tok_cmd = next_tok;				// record cmd token ex: ls, cat, number
	string ret_msg ("");					// return message
	int argc = 0;							// the argc of cmd
	char **argv;							// the argv of cmd
	int stdin_copy = dup(0);				// copy origin stdin

	all_list_add_or_sub(head,true);			// when doing new line cmd, all pipe lists counter sub one 
	bool first_time_do_cmd = true;			// to handle read from pipe counter 0

	client_info other_client;

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
			{
				setenv(env,next_tok,1);
			}
			else
				cout << "No argv!" << endl;
			break;
		}
		else if(strcmp(tok_cmd,"ls") == 0 || strcmp(tok_cmd,"cat") == 0 || strcmp(tok_cmd,"number") == 0 || strcmp(tok_cmd,"removetag") == 0 || strcmp(tok_cmd,"removetag0") == 0 || strcmp(tok_cmd,"noop") == 0 || strcmp(tok_cmd,"date") == 0 || strcmp(tok_cmd,"delayedremovetag") == 0)
		{
			handle_arg(&argc,&argv,tok_cmd,&next_tok);

			char *tmp_tok = next_tok;
			if(next_tok != NULL && strcmp(next_tok,">") != 0 && (next_tok)[0] == '>')
				next_tok = strtok(NULL," \r\n");

			// do <N
			if(next_tok != NULL && strcmp(next_tok,"<") != 0 && (next_tok)[0] == '<')
			{
				int number = 0;
				if((number = convert_to_int(next_tok)) == 0)
				{	
					cout << "Public pipe number error" << endl;
				}
				else
				{
					string msg = "/tmp/fifo_jl" + to_string(number);
					const char *filename = msg.c_str();
					int r_fd;
					if((r_fd = open(filename,O_RDONLY|O_NONBLOCK)) == -1)
					{
						//cout << strerror(errno) << endl;
						//cout << "<N fifo read open error" << endl;
						all_list_add_or_sub(head,false);
						ret_msg += "*** Error: public pipe #" + to_string(number) + " does not exist yet. ***\n";
						return ret_msg;
					}
					else
					{
						close(0);
						dup(r_fd);
						close(r_fd);
						unlink(filename);
						broadcast_msg = "*** " + string(client->name) + " (#" + to_string(j+1) + ") just received via '" + origin_cmd + "' ***\n";
						broadcast(client_list,broadcast_msg,j);
						ret_msg += broadcast_msg;
					}
				}
				next_tok = strtok(NULL," \r\n");
			}
			if(tmp_tok != NULL && strcmp(tmp_tok,">") != 0 && (tmp_tok)[0] == '>')
				next_tok = tmp_tok;

			do_cmd(tok_cmd,argv,&ret_msg,&next_tok,stdin_copy,head,first_time_do_cmd);

			if(ret_msg.find("Unknown command:") != string::npos)
			{
				if(first_time_do_cmd == true)
					all_list_add_or_sub(head,false);
				return ret_msg;
			}

			// done >N
			if(ret_msg.find("public pipe true") != string::npos)
			{	
				ret_msg.erase(ret_msg.find("public pipe true"),16);
				broadcast_msg = "*** " + string(client->name) + " (#" + to_string(j+1) + ") just piped '" + origin_cmd + "' ***\n";
				broadcast(client_list,broadcast_msg,j);
				ret_msg += broadcast_msg;
				return ret_msg;
			}
			argc = 0;
			first_time_do_cmd = false;
		}
		else if(strcmp(tok_cmd,"who") == 0)
		{
			ret_msg += "<ID>	<nickname>	<IP/port>	<indicate me>\n";
			for(int i=0;i<30;i++)
			{
				memcpy(&other_client,&client_list[i],sizeof(client_info));
				if(other_client.connfd != -1)
				{
					/*char ip[32];
					inet_ntop(AF_INET,&other_client.addr.sin_addr,ip,sizeof(ip));
					ret_msg += (to_string(i+1) + "	" + 
								string(other_client.name) + "	" +
								string(ip) + "/" + to_string(ntohs(other_client.addr.sin_port)));*/
					ret_msg += (to_string(i+1) + "	" + 
								string(other_client.name) + "	" +
								"CGILAB/511");
					if(i == j)
						ret_msg += "	<-me\n";
					else
						ret_msg += "\n";
				}
			}
			break;
		}
		else if(strcmp(tok_cmd,"tell") == 0)
		{	
			char *client_id = next_tok;
			next_tok = strtok(NULL,"\r\n");
			if(client_id != NULL && next_tok != NULL)
			{
				int id = atoi(client_id)-1;
				memcpy(&other_client,&client_list[id],sizeof(client_info));
				if(other_client.connfd < 0)
					ret_msg += "*** Error: user #" + string(client_id) + " does not exist yet. ***\n";
				else
				{
					// use ret_msg as send_buff
					ret_msg += "*** " + string(client->name) + " told you ***: " + string(next_tok) +"\n";
					unicast(client_list,ret_msg,id);
					ret_msg = "";
				}
			}
			break;
		}
		else if(strcmp(tok_cmd,"yell") == 0)
		{
			if(next_tok != NULL)
			{
				ret_msg += "*** " + string(client->name) + " yelled ***: " + string(next_tok);
				next_tok = strtok(NULL,"\r\n");
				if(next_tok != NULL)
					ret_msg += " " + string(next_tok);
				ret_msg += "\n";
				broadcast(client_list,ret_msg,j);
			}
			break;
		}
		else if(strcmp(tok_cmd,"name") == 0)
		{
			if(next_tok != NULL)
			{
				bool name_check = true;
				for(int i=0;i<30;i++)
				{
					memcpy(&other_client,&client_list[i],sizeof(client_info));
					if(other_client.connfd != -1 && strcmp(next_tok,other_client.name) == 0)
					{	
						name_check = false;
						break;
					}
				}
				if(name_check == true)
				{
					strcpy(client->name,next_tok);
					/*char ip[32];
					inet_ntop(AF_INET,&client->addr.sin_addr,ip,sizeof(ip));
					ret_msg += "*** User from " + 
								string(ip) + "/" + to_string(ntohs(client->addr.sin_port)) +
								" is named '" + string(next_tok) + "'. ***\n";*/
					ret_msg += "*** User from CGILAB/511 is named '" + string(next_tok) + "'. ***\n";
					memcpy(&client_list[j],client,sizeof(client_info));
					broadcast(client_list,ret_msg,j);
				}
				else
					ret_msg = "*** User '" + string(next_tok) + "' already exists. ***\n";
			}
			break;
		}
		else
		{
			if(first_time_do_cmd == true)
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

	broadcast_msg.resize(0);
	broadcast_msg.shrink_to_fit();
	origin_cmd.resize(0);
	origin_cmd.shrink_to_fit();

	return ret_msg;
}

int main(int argc, char *argv[], char *envp[])
{
	// initialize directory path
	if(chdir("./rwg") == -1)
		cout << "Change directory error!" << endl;
	//cout << get_current_dir_name() << endl; // for debug

	// initialize env PATH
	setenv("PATH","bin:.",1);
	
	int listenfd, connfd, n;
	struct sockaddr_in servaddr;
	string send_buff;
	string welcome_msg = "****************************************\n** Welcome to the information server. **\n****************************************\n";
	string recv_buff;
	string broadcast_msg;
	char tmp[MAXLINE+1];
	pipe_list *pipe_head = NULL;
	pipe_list *public_pipe_head = NULL;

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

	//mode = PERMS, IPC_CREAT: if not exist then create, IPC_EXCL: if SHMKEY is already in use then return fail
	// shared memory for client_list
	if((shmid_c = shmget(SHMKEY_C,sizeof(client_info)*30,PERMS|IPC_CREAT|IPC_EXCL)) < 0)
		cout << "server: client_list can't get shared memory" << endl;

	//shared memory for broadcast | unicast msg
	if((shmid_b = shmget(SHMKEY_B,1024,PERMS|IPC_CREAT|IPC_EXCL)) < 0)
			cout << "server: broadcast_msg can't get shared memory" << endl;

	if((client_list = (client_info *)shmat(shmid_c,(char*)0,0)) < 0)
		cout << "server: client_list can't attach the shared memory" << endl;

	if((broadcast_addr = (char *)shmat(shmid_b,(char*)0,0)) < 0)
		cout << "server: broadcast_msg can't attach the shared memory" << endl;
	
	//use server_sem semaphore for handling race condition
	if((server_sem = sem_open("server_sem",O_CREAT|O_EXCL,PERMS,1)) == SEM_FAILED)
	{
		cout << "server_sem may exist ! will try again !" << endl;
		if(sem_unlink("server_sem") < 0)
			cout << "server_sem sem_unlink error" << endl;
		if((server_sem = sem_open("server_sem",O_CREAT|O_EXCL,PERMS,1)) == SEM_FAILED)
			cout << "server_sem sem_open error again" << endl;
	}

	//use client_sem semaphore for checking all got broadcast_msg
	if((client_sem = sem_open("client_sem",O_CREAT|O_EXCL,PERMS,0)) == SEM_FAILED)
	{	
		cout << "client_sem may exist ! will try again !" << endl;
		if(sem_unlink("client_sem") < 0)
			cout << "client_sem sem_unlink error" << endl;
		if((client_sem = sem_open("client_sem",O_CREAT|O_EXCL,PERMS,0)) == SEM_FAILED)
			cout << "client_sem sem_open error again" << endl;
	}

	// for ctrl+c to release shared memory
	signal(SIGINT,signal_term);

	// client initialize
	client_info *client_init = NULL;
	client_init = (client_info *)malloc(sizeof(client_info));
	client_init->connfd = -1;
	strcpy(client_init->name,"(no name)");
	for(int i=0;i<30;i++)
		memcpy(&client_list[i],client_init,sizeof(client_info));
	free(client_init);

	int j;						// the jth client
	fd_set rset;
	FD_ZERO(&rset);
	FD_SET(listenfd,&rset);

	int child_pid;
	
	while(true)
	{
		select(listenfd+1,&rset,NULL,NULL,NULL);

		if(errno == EINTR)
		{
			//cout << "selcet is interrupted by SIGCHLD" << endl;
			errno = 0;
			continue;
		}
		// for new client
		if(FD_ISSET(listenfd,&rset))	// parent process
		{
			// the following is for debug
			/*int value;
			sem_getvalue(server_sem,&value);
			cout << getpid() <<" parent wait:" << value << endl;*/
			
			// parent lock
			sem_wait(server_sem);
			for(j=0;j<30;j++)
			{
				// new client say hello
				memcpy(&client,&client_list[j],sizeof(client_info));
				if(client.connfd < 0)
				{
					socklen_t clilen = sizeof(client.addr);
					if((client.connfd = accept(listenfd,(struct sockaddr *)&client.addr,&clilen)) < 0)
					{
						if(errno == EINTR)
							continue;
						else
							cout << "accept error" << endl;
					}
					/*char ip[32];
					inet_ntop(AF_INET,&client.addr.sin_addr,ip,sizeof(ip));
					send_buff.assign(welcome_msg);
					broadcast_msg.assign("*** User '" + string(client.name) + "' entered from " + string(ip) + "/" + to_string(ntohs(client.addr.sin_port)) + ". ***\n");*/
					send_buff.assign(welcome_msg);
					broadcast_msg.assign("*** User '" + string(client.name) + "' entered from CGILAB/511. ***\n");
					send_buff += broadcast_msg + "% ";
					write(client.connfd,send_buff.c_str(),send_buff.length());
					broadcast(client_list,broadcast_msg,j);
					break;
				}
			}
			if(j == 30)
				cout << "too many clients" << endl;
		}

		child_pid = fork();
		if(child_pid == 0)	// child process
		{
			signal(SIGUSR1,signal_handler);
			cout << "(child) the client connection pid: " << getpid() << endl;
			client.pid = getpid();
			close(listenfd);
			while(true)
			{
				recv_buff = "";
				while((n = read(client.connfd,tmp,MAXLINE)) > 0)
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
				// the following is for debug
				/*int value;
				sem_getvalue(server_sem,&value);
				cout << getpid() <<" login" << j+1 <<" wait:" << value << endl;*/
				
				// child lock
				sem_wait(server_sem);
				send_buff = "";
				send_buff = handle_cmd((char *)recv_buff.c_str(),&pipe_head,&client,j,client_list);
				if(send_buff.compare("exit") == 0)
				{
					broadcast_msg.assign("*** User '" + string(client.name) + "' left. ***\n");
					broadcast(client_list,broadcast_msg,j);
					write(client.connfd,broadcast_msg.c_str(),broadcast_msg.length());
					close(client.connfd);
					client.connfd = -1;
					strcpy(client.name,"(no name)");
					client.pid = 0;
					memcpy(&client_list[j],&client,sizeof(client_info));
					sem_post(server_sem);
					if(shmdt(client_list) < 0)
						cout << "child: can't detach shared memory" << endl;
					if(shmdt(broadcast_addr) < 0)
						cout << "child: can't detach shared memory" << endl;
					sem_close(server_sem);
					sem_close(client_sem);
					break;
				}
				send_buff.append("% ");
				write(client.connfd,send_buff.c_str(),send_buff.length());
				sem_post(server_sem);
			}
			exit(0);
		}
		else if(child_pid > 0)	// parent process
		{
			cout << "(parent) the listen pid: " << getpid() << endl;
			client.pid = child_pid;
			memcpy(&client_list[j],&client,sizeof(client_info));
			close(client.connfd);
			
			// parent unlock
			sem_post(server_sem);
			
			// signal must call in parent, otherwise if child process call fork again then it will catch wrong signal
			signal(SIGCHLD,sig_chld);
		}
		else					// fork fail
			cout << "fail to fork" << endl;
	}
	return 0;
}

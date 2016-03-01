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

// pipe list use for |N or !N
typedef struct pipe_list{
	int my_pipe[2];
	int counter;
	pipe_list *next;
}pipe_list;

typedef struct env_list
{
	char env_var[20];
	char env_set[20];
	env_list *next;
}env_list;

// define client struct
typedef struct client_info
{
	int connfd;
	char name[21];
	struct sockaddr_in addr;
	env_list *env_head;
	pipe_list *pipe_head;
}client_info;

// arg list use for every cmd argv
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
bool do_public_pipe_N(pipe_list **public_head, int num, int stdin_copy, int pipe_out, int pipe_err)
{
	pipe_list *my_pipe = *public_head;
	while(my_pipe != NULL)
	{
		// use counter as id
		if(my_pipe->counter == num)
			return false;
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
			return false;
		}
		pipe_list *pipe_c = *public_head;
		if(pipe_c == NULL)										// the first one
			*public_head = my_pipe;
		else													// the second or after
		{
			while(pipe_c != NULL)
			{
				if(pipe_c->counter > num)						// if insert in the first
				{
					my_pipe->next = pipe_c;
					*public_head = my_pipe;
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
		close(0);
		dup(pipe_err);											// pipe stderr to public pipe
		close(pipe_err);
		msg += do_read(fileno(stdin));							// read pipe_err from stdin
		close(0);
		dup(pipe_out);											// pipe stdout to public pipe
		close(pipe_out);
		msg += do_read(fileno(stdin));							// read pipe_out from stdin
		write(my_pipe->my_pipe[1],msg.c_str(),msg.length());
		close(my_pipe->my_pipe[1]);								// must close write, otherwise the read will block	
	}
	//back to origin stdin
	close(0);
	dup2(stdin_copy,0);
	// release msg memmory
	msg.resize(0);
	msg.shrink_to_fit();
	return true;
}

// exec the cmd
void do_cmd(char *cmd, char **argv, string *ret_msg, char **next_tok, int stdin_copy, pipe_list **head, bool first_time_do_cmd, pipe_list **public_head)
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
		if(execvp(cmd,argv) == -1)	// exec cmd
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
				if(do_public_pipe_N(public_head,number,stdin_copy,pipe_out[0],pipe_err[0]) == true)
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
void broadcast(client_info *client, int max_num_of_client, string msg, int j)
{
	for(int i=0;i<=max_num_of_client;i++)
	{
		if(client[i].connfd != -1 && i != j)
			write(client[i].connfd,msg.c_str(),msg.length());
	}
}

string handle_cmd(char* cmd, pipe_list **head, client_info *client, int max_num_of_client, int j, pipe_list **public_head, env_list **env_head)
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
				env_list *my_env = *env_head;
				while(my_env != NULL)
				{
					if(strcmp(env,my_env->env_var) == 0)
						break;
					my_env = my_env->next;
				}
				if(my_env == NULL)
				{
					my_env = (env_list*)malloc(sizeof(env_list));
					strcpy(my_env->env_var,env);
					strcpy(my_env->env_set,next_tok);
					my_env->next = NULL;
					env_list *c_env = *env_head;
					while(c_env != NULL)
					{
						if(c_env->next == NULL)
						{
							c_env->next = my_env;
							break;
						}
						c_env = c_env->next;
					}
				}
				else
					strcpy(my_env->env_set,next_tok);
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
					pipe_list *c_pipe = *public_head;
					pipe_list *p_pipe = c_pipe;
					while(c_pipe != NULL)
					{
						// use counter as id
						if(c_pipe->counter == number)
							break;
						p_pipe = c_pipe;
						c_pipe = c_pipe->next;
					}
					if(c_pipe == NULL)
					{
						all_list_add_or_sub(head,false);
						ret_msg += "*** Error: public pipe #" + to_string(number) + " does not exist yet. ***\n";
						return ret_msg;
					}
					else
					{
						close(0);
						dup(c_pipe->my_pipe[0]);
						close(c_pipe->my_pipe[0]);
						p_pipe->next = c_pipe->next;
						if(c_pipe == *public_head)
							*public_head = (*public_head)->next;
						free(c_pipe);
						broadcast_msg = "*** " + string(client[j].name) + " (#" + to_string(j+1) + ") just received via '" + origin_cmd + "' ***\n";
						broadcast(client,max_num_of_client,broadcast_msg,j);
						ret_msg += broadcast_msg;
					}
				}
				next_tok = strtok(NULL," \r\n");
			}
			if(tmp_tok != NULL && strcmp(tmp_tok,">") != 0 && (tmp_tok)[0] == '>')
				next_tok = tmp_tok;

			do_cmd(tok_cmd,argv,&ret_msg,&next_tok,stdin_copy,head,first_time_do_cmd,public_head);

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
				broadcast_msg = "*** " + string(client[j].name) + " (#" + to_string(j+1) + ") just piped '" + origin_cmd + "' ***\n";
				broadcast(client,max_num_of_client,broadcast_msg,j);
				ret_msg += broadcast_msg;
				return ret_msg;
			}
			argc = 0;
			first_time_do_cmd = false;
		}
		else if(strcmp(tok_cmd,"who") == 0)
		{
			ret_msg += "<ID>	<nickname>	<IP/port>	<indicate me>\n";
			for(int i=0;i<=max_num_of_client;i++)
			{
				if(client[i].connfd != -1)
				{
					/*char ip[32];
					inet_ntop(AF_INET,&client[i].addr.sin_addr,ip,sizeof(ip));
					ret_msg += (to_string(i+1) + "	" + 
								string(client[i].name) + "	" +
								string(ip) + "/" + to_string(ntohs(client[i].addr.sin_port)));*/
					ret_msg += (to_string(i+1) + "	" + 
								string(client[i].name) + "	" +
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
				if(client[id].connfd < 0)
					ret_msg += "*** Error: user #" + string(client_id) + " does not exist yet. ***\n";
				else
				{
					// use ret_msg as send_buff
					ret_msg += "*** " + string(client[j].name) + " told you ***: " + string(next_tok) +"\n";
					write(client[id].connfd,ret_msg.c_str(),ret_msg.length());
					ret_msg = "";
				}
			}
			break;
		}
		else if(strcmp(tok_cmd,"yell") == 0)
		{
			if(next_tok != NULL)
			{
				ret_msg += "*** " + string(client[j].name) + " yelled ***: " + string(next_tok);
				next_tok = strtok(NULL,"\r\n");
				if(next_tok != NULL)
					ret_msg += " " + string(next_tok);
				ret_msg += "\n";
				broadcast(client,max_num_of_client,ret_msg,j);
			}
			break;
		}
		else if(strcmp(tok_cmd,"name") == 0)
		{
			if(next_tok != NULL)
			{
				bool name_check = true;
				for(int i=0;i<=max_num_of_client;i++)
				{
					if(strcmp(next_tok,client[i].name) == 0)
					{	
						name_check = false;
						break;
					}
				}
				if(name_check == true)
				{
					strcpy(client[j].name,next_tok);
					/*char ip[32];
					inet_ntop(AF_INET,&client[j].addr.sin_addr,ip,sizeof(ip));
					ret_msg += "*** User from " + 
								string(ip) + "/" + to_string(ntohs(client[j].addr.sin_port)) +
								" is named '" + string(next_tok) + "'. ***\n";*/
					ret_msg += "*** User from CGILAB/511 is named '" + string(next_tok) + "'. ***\n";
					broadcast(client,max_num_of_client,ret_msg,j);
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

void clear_pipe_list(pipe_list **head)
{
	pipe_list *pipe_c = *head;
	while(pipe_c != NULL)
	{
		(*head) = (*head)->next;
		close(pipe_c->my_pipe[0]);
		free(pipe_c);
		pipe_c = (*head);
	}
	(*head) = NULL;
}

void setenv_own(env_list *head)
{
	env_list *c_env = head;
	while(c_env != NULL)
	{
		setenv(c_env->env_var,c_env->env_set,1);
		c_env = c_env->next;
	}
}

void clear_env_list(env_list **head)
{
	env_list *c_env = (*head);
	while(c_env != NULL)
	{
		(*head) = (*head)->next;
		free(c_env);
		c_env = (*head);
	}
	(*head) = (env_list *)malloc(sizeof(env_list));
	strcpy((*head)->env_var,"PATH");
	strcpy((*head)->env_set,"bin:.");
	(*head)->next = NULL;
}

int main(int argc, char *argv[], char *envp[])
{
	// initialize directory path
	if(chdir("./rwg") == -1)
		cout << "Change directory error!" << endl;
	//cout << get_current_dir_name() << endl; // for debug

	// initialize env PATH
	setenv("PATH","bin:.",1);
	
	int listenfd, n;
	struct sockaddr_in servaddr;
	string send_buff;
	string welcome_msg = "****************************************\n** Welcome to the information server. **\n****************************************\n";
	string recv_buff;
	string broadcast_msg;
	char tmp[MAXLINE+1];
	//pipe_list *pipe_head = NULL;
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

	// select multiple client
	int maxfd = listenfd;		// initialize
	int max_num_of_client = -1;	// max id of clients
	int n_ready,j;				// number of ready for read ,jth client
	client_info client[30];
	socklen_t clilen;
	fd_set rset,allset;
	// client initialize
	for(int i=0;i<30;i++)
	{
		client[i].connfd = -1;
		strcpy(client[i].name,"(no name)");
		client[i].env_head = (env_list*)malloc(sizeof(env_list));
		strcpy(client[i].env_head->env_var,"PATH");
		strcpy(client[i].env_head->env_set,"bin:.");
		client[i].env_head->next = NULL;
		client[i].pipe_head = NULL;
	}
	FD_ZERO(&allset);
	FD_SET(listenfd,&allset);

	while(true)
	{
		rset = allset;
		n_ready = select(maxfd+1,&rset,NULL,NULL,NULL);
		// for new client
		if(FD_ISSET(listenfd,&rset))
		{
			for(j=0;j<30;j++)
			{
				// new client say hello
				if(client[j].connfd < 0)
				{
					clilen = sizeof(client[j].addr);
					if((client[j].connfd = accept(listenfd,(struct sockaddr *)&client[j].addr,&clilen)) < 0)
					{
						if(errno == EINTR)
							continue;
						else
							cout << "accept error" << endl;
					}
					FD_SET(client[j].connfd,&allset);
					if(client[j].connfd > maxfd)
						maxfd = client[j].connfd;
					if(j > max_num_of_client)
						max_num_of_client = j;
					/*char ip[32];
					inet_ntop(AF_INET,&client[j].addr.sin_addr,ip,sizeof(ip));
					send_buff.assign(welcome_msg);
					broadcast_msg.assign("*** User '" + string(client[j].name) + "' entered from " + string(ip) + "/" + to_string(ntohs(client[j].addr.sin_port)) + ". ***\n");*/
					send_buff.assign(welcome_msg);
					broadcast_msg.assign("*** User '" + string(client[j].name) + "' entered from CGILAB/511. ***\n");
					send_buff += broadcast_msg + "% ";
					write(client[j].connfd,send_buff.c_str(),send_buff.length());
					broadcast(client,max_num_of_client,broadcast_msg,j);
					break;
				}
			}
			if(j == 30)
				cout << "too many clients" << endl;
			if(--n_ready <= 0)
				continue;
		}
		
		// check all clients
		for(j=0;j<=max_num_of_client;j++)
		{
			if(client[j].connfd < 0)
				continue;
			if(FD_ISSET(client[j].connfd,&rset))
			{
				recv_buff = "";
				while((n = read(client[j].connfd,tmp,MAXLINE)) > 0)
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

				if(n == 0)
				{
					FD_CLR(client[j].connfd,&allset);
					broadcast_msg.assign("*** User '" + string(client[j].name) + "' left. ***\n");
					broadcast(client,max_num_of_client,broadcast_msg,j);
					write(client[j].connfd,broadcast_msg.c_str(),broadcast_msg.length());
					close(client[j].connfd);
					client[j].connfd = -1;
					strcpy(client[j].name,"(no name)");
					clear_env_list(&client[j].env_head);
					clear_pipe_list(&client[j].pipe_head);
					continue;
				}
				//cout << recv_buff << endl;            // for debug
				//cout << recv_buff.length() << endl;   // for debug
				
				setenv_own(client[j].env_head);
				send_buff = "";
				send_buff = handle_cmd((char *)recv_buff.c_str(),&client[j].pipe_head,client,max_num_of_client,j,&public_pipe_head,&client[j].env_head);
				if(send_buff.compare("exit") == 0)
				{
					FD_CLR(client[j].connfd,&allset);
					broadcast_msg.assign("*** User '" + string(client[j].name) + "' left. ***\n");
					broadcast(client,max_num_of_client,broadcast_msg,j);
					write(client[j].connfd,broadcast_msg.c_str(),broadcast_msg.length());
					close(client[j].connfd);
					client[j].connfd = -1;
					strcpy(client[j].name,"(no name)");
					clear_env_list(&client[j].env_head);
					clear_pipe_list(&client[j].pipe_head);
					send_buff = "";
					continue;
				}
				send_buff.append("% ");
				write(client[j].connfd,send_buff.c_str(),send_buff.length());
			}
		}
	}
	return 0;
}

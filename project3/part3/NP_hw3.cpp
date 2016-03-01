#include <windows.h>
#include <list>
#include <iostream>
#include <cstring>
#include <string>
#include <regex>
#include <sys/stat.h>
#include <errno.h>
using namespace std;

#include "resource.h"

#define SERVER_PORT 7799

#define MAXLINE 10000

#define WM_SOCKET_NOTIFY (WM_USER + 1)

#define CGI_SOCKET_NOTIFY (WM_USER + 2)

BOOL CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
int EditPrintf (HWND, TCHAR *, ...);
//=================================================================
//	Global Variables
//=================================================================
list<SOCKET> Socks;

typedef struct host
{
	char ip[17];
	struct  sockaddr_in addr;
	char batch_file_name[64];
	FILE *file;
	SOCKET connfd;
	bool is_connect;
	int counter;
}host;

host server[5];
int host_num;
char msg[MAXLINE+1];

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

void parse_query(char *query) // parse the QUERY_STRING
{
	host_num = 0;
	char *host_tok = strtok(query,"=\r\n");
	for(int i=0;host_tok != NULL && i<5;i++)	// parse the host info
	{
		host_tok = strtok(NULL,"&\r\n");
		if(host_tok != NULL && host_tok[0] != 'p')
		{
			host_num++;
			server[i].connfd = -1;
			memset(&(server[i].addr),0,sizeof(server[i].addr));
			server[i].addr.sin_family = AF_INET;
			server[i].addr.sin_addr.s_addr = inet_addr(host_tok);
			strcpy(server[i].ip,host_tok);
			host_tok = strtok(NULL,"=\r\n");
			host_tok = strtok(NULL,"&\r\n");
			server[i].addr.sin_port = htons(atoi(host_tok));
			host_tok = strtok(NULL,"=\r\n");
			host_tok = strtok(NULL,"&\r\n");
			strcpy(server[i].batch_file_name,host_tok);
			server[i].file = fopen(host_tok,"r");
			server[i].counter = 0;
		}
		else									// the host not use
		{
			server[i].connfd = -2;
			host_tok = strtok(NULL,"h\r\n");
		}
		host_tok = strtok(NULL,"=\r\n");
	}
}

void send_cgi_header(SOCKET websock)
{
	memset(msg,0,MAXLINE+1);
	sprintf(msg,"<html>\n\
			<head>\n\
			<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />\n\
			<title>Network Programming Homework 3</title>\n\
			</head>\n\
			<body bgcolor=#336699>\n\
			<font face=\"Courier New\" size=2 color=#FFFF99>\n\
			<table width=\"800\" border=\"1\">\n\
			<tr>\n");
	send(websock,msg,strlen(msg),0);

	for(int i=0;i<5;i++)
	{
		if(server[i].connfd != -2)
		{
			sprintf(msg,"<td>%s</td>",server[i].ip);
			send(websock,msg,strlen(msg),0);
		}
	}
	sprintf(msg,"</tr>\n<tr>\n");
	send(websock,msg,strlen(msg),0);
	for(int i=0;i<5;i++)
	{
		if(server[i].connfd != -2)
		{
			sprintf(msg,"<td valign=\"top\" id=\"m%d\"></td>",i);
			send(websock,msg,strlen(msg),0);
		}
	}
	sprintf(msg, "</tr>\n</table>\n");
	send(websock,msg,strlen(msg),0);
}

void connect_hosts(HWND hwndEdit, HWND hwnd)
{
	for(int i=0;i<5;i++)
	{
		if(server[i].connfd != -2)
		{
			if((server[i].connfd = socket(AF_INET,SOCK_STREAM,0)) == INVALID_SOCKET)
			{
				EditPrintf(hwndEdit, TEXT("=== Error: create socket error ===\r\n"));
				continue;
			}
			
			int err = WSAAsyncSelect(server[i].connfd, hwnd, CGI_SOCKET_NOTIFY, FD_CONNECT | FD_CLOSE | FD_READ | FD_WRITE);
			if ( err == SOCKET_ERROR ) 
			{
				EditPrintf(hwndEdit, TEXT("=== Error: select error ===\r\n"));
				closesocket(server[i].connfd);
				continue;
			}
			// the connect error in VS is SOCKET_ERROR
			if(connect(server[i].connfd,(struct sockaddr *)&(server[i].addr),sizeof(server[i].addr)) == SOCKET_ERROR)
			{
				server[i].is_connect = false;
				// connect may be  (Resource temporarily unavailable) or (Operation now in progress), because it's nonblocking
				if(WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAEINPROGRESS)
					EditPrintf(hwndEdit, TEXT("(not nonblocking error) connect error %d\r\n" ),WSAGetLastError());
			}
		}
	}
}

void handle_request(char *method, char *request, SOCKET websock, HWND hwndEdit, HWND hwnd) // handle the .cgi or .html request
{
	// parse the request
	char *next_tok = strtok(request,"?\r\n");
	char *script_name = next_tok;
	next_tok = strtok(NULL,"\r\n");
	char *query = next_tok;
	// for debug
	/*if(script_name != NULL)
		EditPrintf(hwndEdit,TEXT("%s\r\n"),script_name);
	if(query != NULL)
		EditPrintf(hwndEdit,TEXT("%s\r\n"),query);*/
	
	memset(msg,0,MAXLINE+1);
	if(regex_match(script_name,regex("hw3.cgi")))			// exec .cgi
	{
		sprintf(msg,"HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n");
		send(websock,msg,strlen(msg),0);
		if(query != NULL)
		{
			parse_query(query);
			if(host_num == 0)
			{
				sprintf(msg,"no hosts\r\n");
				send(websock,msg,strlen(msg),0);
				return;
			}
			send_cgi_header(websock);
			connect_hosts(hwndEdit,hwnd);
		}
	}
	else if(regex_match(script_name,regex(".*\\.html")))	// read .html
	{
		struct stat s;
		if(stat(script_name,&s) == -1) // if not find .html file
		{
			sprintf(msg,"HTTP/1.1 404 Not Found\r\n\
			Content-type: text/html\r\n\r\n\
			<html>\n<body>\n\
			<img src=\"http://zenit.senecac.on.ca/wiki/imgs/404-not-found.gif\"></img>\n\
			</body>\n</html>\n");
			send(websock,msg,strlen(msg),0);
		}
		else
		{
			sprintf(msg,"HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n");
			send(websock,msg,strlen(msg),0);
			FILE *fp_html;
			fp_html = fopen(script_name,"r");
			while(fgets(msg,MAXLINE+1,fp_html) != NULL)
			{
				send(websock,msg,strlen(msg),0);
			}
			fclose(fp_html);
		}
		closesocket(websock);
		Socks.remove(websock);
	}
	else
	{
		sprintf(msg,"HTTP/1.1 404 Not Found\r\n\
			Content-type: text/html\r\n\r\n\
			<html>\n<body>\nneither hw3.cgi nor .html\r\n\
			<img src=\"http://zenit.senecac.on.ca/wiki/imgs/404-not-found.gif\"></img>\n\
			</body>\n</html>\n");
		send(websock,msg,strlen(msg),0);
		closesocket(websock);
		Socks.remove(websock);
	}
	return;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	
	return DialogBox(hInstance, MAKEINTRESOURCE(ID_MAIN), NULL, MainDlgProc);
}

BOOL CALLBACK MainDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	WSADATA wsaData;

	static HWND hwndEdit;
	static SOCKET msock, ssock;
	static struct sockaddr_in sa;

	int err;

	string recv_buff;
	int n;
	char *next_tok;
	char *method;
	char *request;
	char *stat;

	switch(Message) 
	{
		case WM_INITDIALOG:
			hwndEdit = GetDlgItem(hwnd, IDC_RESULT);
			break;
		case WM_COMMAND:
			switch(LOWORD(wParam))
			{
				case ID_LISTEN:

					WSAStartup(MAKEWORD(2, 0), &wsaData);

					//create master socket
					msock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

					if( msock == INVALID_SOCKET ) {
						EditPrintf(hwndEdit, TEXT("=== Error: create socket error ===\r\n"));
						WSACleanup();
						return TRUE;
					}

					err = WSAAsyncSelect(msock, hwnd, WM_SOCKET_NOTIFY, FD_ACCEPT | FD_CLOSE | FD_READ | FD_WRITE);

					if ( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: select error ===\r\n"));
						closesocket(msock);
						WSACleanup();
						return TRUE;
					}

					//fill the address info about server
					sa.sin_family		= AF_INET;
					sa.sin_port			= htons(SERVER_PORT);
					sa.sin_addr.s_addr	= INADDR_ANY;

					//bind socket
					err = bind(msock, (LPSOCKADDR)&sa, sizeof(struct sockaddr));

					if( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: binding error ===\r\n"));
						WSACleanup();
						return FALSE;
					}

					err = listen(msock, 2);
		
					if( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: listen error ===\r\n"));
						WSACleanup();
						return FALSE;
					}
					else {
						EditPrintf(hwndEdit, TEXT("=== Server START ===\r\n"));
					}

					break;
				case ID_EXIT:
					EndDialog(hwnd, 0);
					break;
			};
			break;

		case WM_CLOSE:
			EndDialog(hwnd, 0);
			break;

		case WM_SOCKET_NOTIFY:
			switch( WSAGETSELECTEVENT(lParam) )
			{
				case FD_ACCEPT:
					ssock = accept(msock, NULL, NULL);
					Socks.push_back(ssock);
					EditPrintf(hwndEdit, TEXT("=== Accept one new client(%d), List size:%d ===\r\n"), ssock, Socks.size());
					break;
				case FD_READ:
				//Write your code for read event here.
					recv_buff = "";
					while((n = recv(wParam,msg,MAXLINE,0)) > 0)
					{
						recv_buff.append(msg,n);
						if(msg[n-1] != '\n')
			    			continue;
						else
						{
							msg[0] = 0;
			    			recv_buff.append(msg,1);
			    			break;
						}
					}
					//EditPrintf(hwndEdit,TEXT("%s\r\n"),recv_buff.c_str());	// for debug
					// parse the http info
					next_tok = strtok((char *)recv_buff.c_str()," \r\n");	// GET or POST
					if(next_tok == NULL || strcmp(next_tok,"GET") != 0)	
					{
						EditPrintf(hwndEdit,TEXT("=== Error: no GET method ===\r\n"));
						closesocket(wParam);
						return FALSE;
					}
					method = next_tok;
					next_tok = strtok(NULL,"/ \r\n");							// .cgi or .html (request)
					if(next_tok == NULL || strcmp(next_tok,"HTTP") == 0)
					{
						EditPrintf(hwndEdit,TEXT("=== Error: no request for .cgi or .html ===\r\n"));
						closesocket(wParam);
						return FALSE;
					}
					request = next_tok;				
					next_tok = strtok(NULL," \r\n");							// HTTP/1.1
					if(next_tok == NULL)
					{
						EditPrintf(hwndEdit,TEXT("=== Error: no HTTP/1.1 ===\r\n"));
						closesocket(wParam);
						return FALSE;
					}
					stat = next_tok;
					//EditPrintf(hwndEdit,TEXT("%s %s %s\r\n"),method,request,stat); // for debug
					handle_request(method,request,wParam,hwndEdit,hwnd);
					break;
				case FD_WRITE:
				//Write your code for write event here

					break;
				case FD_CLOSE:
					break;
			};
			break;
		case CGI_SOCKET_NOTIFY:
			switch( WSAGETSELECTEVENT(lParam) )
			{
				case FD_CONNECT:
					for(int i=0;i<5;i++)
					{
						if(server[i].connfd != -2 && server[i].connfd == wParam)
						{
							if(server[i].is_connect == false)	// connect again make sure "is connected"
							{
								if(connect(server[i].connfd,(struct sockaddr *)&(server[i].addr),sizeof(server[i].addr)) == SOCKET_ERROR)
								{
									if(WSAGetLastError() != WSAEISCONN)	// "is connected" means successfully connect
									{
										EditPrintf(hwndEdit, TEXT("host%d connect error %d\r\n" ),i,WSAGetLastError()); // real connect error
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
						}
					}
					break;
				case FD_READ:
					for(int i=0;i<5;i++)
					{
						if(server[i].connfd != -2 && server[i].connfd == wParam && server[i].is_connect == true)
						{
							recv_buff = "";
							while((n = recv(server[i].connfd,msg,MAXLINE,0)) > 0)
							{
								recv_buff.append(msg,n);
								if(msg[n-1] != '\n' && (msg[n-1] != ' ' && msg[n-2] != '%'))
									continue;
								else
								{
									msg[0] = 0;
									recv_buff.append(msg,1);
									break;
								}
							}
							convert_to_html(&recv_buff);
							sprintf(msg,"<script>document.all['m%d'].innerHTML += \"%s\";</script>\n",i,recv_buff.c_str());
							for (list<SOCKET>::iterator it = Socks.begin(); it != Socks.end(); ++it) 
								send(*it,msg,strlen(msg),0);
							if(recv_buff[recv_buff.length()-3] == '%') // sub /r/n and one space
							{
								memset(msg,0,MAXLINE+1);
								if(server[i].file != NULL)
								{
									fgets(msg,MAXLINE+1,server[i].file);
									string cmd(msg);
									//EditPrintf(hwndEdit, TEXT("%s\r\n" ),cmd.c_str());
									send(server[i].connfd,cmd.c_str(),cmd.length(),0);
									convert_to_html(&cmd);
									sprintf(msg,"<script>document.all['m%d'].innerHTML += \"<b>%s</b>\";</script>\n",i,cmd.c_str());
									for (list<SOCKET>::iterator it = Socks.begin(); it != Socks.end(); ++it) 
										send(*it,msg,strlen(msg),0);
									if(strncmp(cmd.c_str(),"exit",4) == 0)
									{
										closesocket(server[i].connfd);
										server[i].connfd = -2;
										fclose(server[i].file);
										host_num--;
									}
								}
								else
									EditPrintf(hwndEdit,TEXT("host%d fopen error\r\n"),i); 
							}
						}
					}
					if(host_num == 0) // when hosts == 0, clear the sockets of web
					{
						for (list<SOCKET>::iterator it = Socks.begin(); it != Socks.end(); ++it) 
							closesocket(*it);
						Socks.clear();
					}
					break;
			};
			break;
		default:
			return FALSE;
	};
	return TRUE;
}

int EditPrintf (HWND hwndEdit, TCHAR * szFormat, ...)
{
     TCHAR   szBuffer [1024] ;
     va_list pArgList ;

     va_start (pArgList, szFormat) ;
     wvsprintf (szBuffer, szFormat, pArgList) ;
     va_end (pArgList) ;

     SendMessage (hwndEdit, EM_SETSEL, (WPARAM) -1, (LPARAM) -1) ;
     SendMessage (hwndEdit, EM_REPLACESEL, FALSE, (LPARAM) szBuffer) ;
     SendMessage (hwndEdit, EM_SCROLLCARET, 0, 0) ;
	 return SendMessage(hwndEdit, EM_GETLINECOUNT, 0, 0); 
}
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace std;

#define B_SIZE 10

int cmd_num;
int port;
int line_num;
FILE*f;

int std_in = dup(0);
int std_out = dup(1);

/* print and flush */
void print_and_flush(char* str){
	printf("%s", str);
	fflush(stdout);
}
/* show error message and exit */
void error_and_exit(char* msg){
	print_and_flush(msg);
	fprintf(stderr, "EXIT\n");
	exit(1);
}

/* signal */
void wait_signal(){
	fprintf(stderr, "KILL!\n");
	wait(NULL);
}

/* main subroutine to maintain client connection */
void process(int sockfd){
	
	char buf[B_SIZE];
	int n = 0;
	
	int readBufferSize = 2048;
	if(setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &readBufferSize, sizeof(int)) < 0){
		fprintf(stderr, "%s\n", strerror(errno));
		exit(1);
	}
	
	/* redirect stdout to sockfd */
	dup2(sockfd, 1);
	
	/* sleep and send "% " for every 0.1 sec */
	fprintf(stderr, "SLEEP\n");
	for( int i = 0;i < cmd_num; i++ ){
		print_and_flush((char*)"% ");
		usleep(100000);							
	}

	/* use select to create non-blocking read/write event */
	fd_set fds;
	fd_set rfds;
	fd_set wfds;
	FD_ZERO(&fds);
	FD_SET(sockfd,&fds);
	int cnt = 0;
	int iCountKB = 0;
	int iCountB = 0;
	bool isSend = false;
	
	while(true){
	    memcpy(&rfds, &fds, sizeof(rfds));
		memcpy(&wfds, &fds, sizeof(wfds));
        if ( select(sockfd + 1, &rfds, &wfds, (fd_set*)0, (struct timeval*)0) < 0 ) {
            return;
        }
		
		// read event
		if (FD_ISSET(sockfd,&rfds)) {
			
			if( iCountB > 10000 ){
				iCountB = 0;
				iCountKB ++;
				fprintf(stderr, "round: %d0 KB\n", iCountKB);
				fflush(stderr);
			}
			
			//fprintf(stderr, "round: %d B\n", cnt++);
			//fflush(stderr);
			
			char c;
			int ret = read(sockfd, &c, 1);
			if( ret <= 0 ) break;
			
			buf[ n % B_SIZE ] = c;
			
			
			if(n >= 5){
				// reach last 2nd line of t7.txt
				if( buf[(n - 4) % B_SIZE] == 'l' &&
					buf[(n - 3) % B_SIZE] == 's' &&
					buf[(n - 2) % B_SIZE] == ' ' &&
					buf[(n - 1) % B_SIZE] == ' ' &&
					buf[(n - 0) % B_SIZE] == ' ')
				{
					printf("Totally read: %d bytes\n", n);
					//printf("ALMOST GET ENTIRE FILE!!\n");
					fflush(stdout);
				}
				// reach last line of t7.txt
				if( buf[(n - 3) % B_SIZE] == 'e' &&
					buf[(n - 2) % B_SIZE] == 'x' &&
					buf[(n - 1) % B_SIZE] == 'i' &&
					buf[(n - 0) % B_SIZE] == 't')
				{
					fprintf(stderr, "EXIT SUCCESS!\n");
					fflush(stderr);
					break;
				}
			}
			
			n++;
			iCountB++;
			isSend = false;
		} 
		// no read event but just write to server
		else if( FD_ISSET(sockfd,&wfds) && !isSend) {
			isSend = true;
			print_and_flush((char*)"% ");
		}
	}

	fprintf(stderr, "OK\n");
	fprintf(stderr, "Totally read: %d bytes\n", n);
}

/* main */
int main(int argc, char* argv[]){
	int    		server_fd;
	int			new_fd;
	int			child_pid;
	socklen_t	client_len;
	struct 		sockaddr_in server_addr;
	struct 		sockaddr_in client_addr;
	
	if( argc != 2 )
		error_and_exit((char*)"server: argc should larger than 1\n");
		
	port = atoi( argv[1] );
	line_num = 0;
	cmd_num = 40;//atoi( argv[2] );	
	
	/* create socket */
	if ((server_fd= socket(AF_INET, SOCK_STREAM, 0))< 0)
		error_and_exit((char*)"server: cannot open stream socket\n");

	/* setup socket variables */
	bzero((char*) &server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);		
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	/* bind the server port */
	if(bind (server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)
		error_and_exit((char*)"server: Cannot bind port\n");

	/* listen */
	listen(server_fd, 5);
	
	signal(SIGCHLD, (void (*)(int))wait_signal);
	
	/* continuously listen to client's connection */
	while(true){
		client_len = sizeof(client_addr);
		if ((new_fd = accept(server_fd, (struct sockaddr*) &client_addr, &client_len)) < 0)
			error_and_exit((char*)"server: accept error\n");
		
		printf("NEW CLIENT!\n");
		fflush(stdout);

		if((child_pid = fork())< 0){
			error_and_exit((char*)"server: fork error\n");
		}else if (child_pid == 0) { 					/* child process */
			close(server_fd);								/* close original socket */
			process(new_fd);								/* process the request */
			close(new_fd); 								/* close the connection */
			exit(0);											/* stop this process */
		}
		close(new_fd);
	}
	return 0;
}

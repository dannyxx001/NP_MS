#include<stdio.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
//#include<unistd.h>

int main(int argc, char* argv[])
{
		if( argc != 2 )
			exit(1);
		
		int port = atoi( argv[1] );
	
        int                     sockfd, newsockfd, clilen, childpid;
        struct sockaddr_in      cli_addr, serv_addr;

        //pname = argv[0];
        //(void) signal(SIGCHLD, reaper);
        /*
        * Open a TCP socket (an Internet stream socket).
        */

        if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
                printf("server: can't open stream socket");

        /*
        * Bind our local address so that the client can send to us.
        */

        bzero((char *) &serv_addr, sizeof(serv_addr));
        serv_addr.sin_family      = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        serv_addr.sin_port        = htons(port);

        if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
                printf("server: can't bind local address");

        listen(sockfd, 1);



        for ( ; ; ) {
                /*
                * Wait for a connection from a client process.
                * This is an example of a concurrent server.
                */
                clilen = sizeof(cli_addr);
                write(1, "BEFORE SLEEP\n", 13);
                sleep(10);
                write(1, "AFTER SLEEP\n", 12);
                newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr,(socklen_t*)&clilen);
                system("date");
                write(1, "accept\n", 7);
                write(newsockfd, "CONNECT\n", 8);
                close(newsockfd);
        }

        return 0;
}

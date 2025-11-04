#include	"unp.h"
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<termios.h>
#include<netdb.h>


void
str_cli(FILE *fp, int sockfd)
{

	
	char	sendline[MAXLINE], recvline[MAXLINE];

	struct sockaddr_in	client_addr;
	socklen_t client_addrlen = sizeof(client_addr);
    printf("[experiment] size of client_addr: %d\n", client_addrlen);

	if (getsockname(sockfd, (SA*) &client_addr, &client_addrlen) < 0) err_sys("getsockname error");

	char cli_IP[100];
	inet_ntop(AF_INET, &client_addr.sin_addr, cli_IP, sizeof(cli_IP));
	snprintf(sendline, sizeof(sendline), "112550032 %s\n", cli_IP);
	// printf("sendline: %s", sendline);

	Writen(sockfd, sendline, strlen(sendline));

	int n = Read(sockfd, recvline, MAXLINE);
    if (n == 0) err_quit("str_cli1: server terminated prematurely");

	recvline[n]='\0';
    printf("first host name from server: %s\n", recvline);
    
    struct hostent* host_entry_ptr;
    host_entry_ptr = gethostbyname(recvline); // 把 IP 傳進去，得到 hostent 的 sturct

    // printf("host name form server: %s\n", host_entry_ptr->h_name);
    inet_ntop(AF_INET, host_entry_ptr->h_addr_list[0], sendline, sizeof(sendline));
    printf("IP of first host name from server: %s\n", sendline);
    Writen(sockfd, sendline, strlen(sendline));

    for(;;){

        n = Read(sockfd, recvline, MAXLINE);
        if (n == 0) err_quit("str_cli2: server terminated prematurely");
        recvline[n]='\0';
        // Fputs(recvline, stdout);
        // printf("\n");

        if(recvline[0]=='g'){

            if(strlen(recvline)>5){
                printf("[not only good]\n");
                char host_name_from_server [100];
                sscanf(recvline, "good\n%s", host_name_from_server);
                printf("host name from server: %s\n", host_name_from_server);
                host_entry_ptr = gethostbyname(host_name_from_server);
                inet_ntop(AF_INET, host_entry_ptr->h_addr_list[0], sendline, sizeof(sendline));
                printf("again IP of %s: %s\n", host_name_from_server, sendline);
                Writen(sockfd, sendline, strlen(sendline));
            }
            else{

                printf("[only good]\n");

                n = Read(sockfd, recvline, MAXLINE);
                if (n == 0) err_quit("str_cli3: server terminated prematurely");
                recvline[n]='\0';
                printf("host name from server: %s\n", recvline);

                host_entry_ptr = gethostbyname(recvline);
                inet_ntop(AF_INET, host_entry_ptr->h_addr_list[0], sendline, sizeof(sendline));
                printf("again IP of %s: %s\n", recvline, sendline);

                Writen(sockfd, sendline, strlen(sendline));

            }

        }
        else if (recvline[0]=='e'){
            snprintf(sendline, sizeof(sendline), "112550032 %s\n", cli_IP);
            Writen(sockfd, sendline, strlen(sendline));
        }
        else if (recvline[0]=='o'){
            printf("ok!\n");
            return;
            // close(sockfd);
        }
        else if (recvline[0]=='b'){
            printf("bad!\n");
        }
        else if (recvline[0]=='n'){
            printf("nak!\n");

        }
    }
    

	

}



int
main(int argc, char **argv)
{
	printf("start of ass3! \n");

	int					sockfd;
	struct sockaddr_in	servaddr;
    printf("[experiment] size of servaddr: %ld\n", sizeof(servaddr));

	if (argc != 2)
		err_quit("usage: tcpcli <IPaddress>");

	sockfd = Socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(SERV_PORT+2);
	Inet_pton(AF_INET, argv[1], &servaddr.sin_addr);

	Connect(sockfd, (SA *) &servaddr, sizeof(servaddr));

	str_cli(stdin, sockfd);		/* do it all */
	close(sockfd);
	exit(0);
}
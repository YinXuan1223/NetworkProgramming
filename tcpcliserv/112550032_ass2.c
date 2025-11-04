#include	"unp.h"
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<termios.h>

struct termios oldt;

void restore_terminal() {
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

void set_terminal() {
	struct termios newt;
	tcgetattr(STDIN_FILENO, &oldt);      // 取得原本設定
	newt = oldt;

	newt.c_lflag &= ~(ICANON);           // 關閉 canonical mode
	newt.c_cc[VMIN] = 1;                 // 至少要有一個字元才回傳
	newt.c_cc[VTIME] = 0;                // 不用計時
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);

	atexit(restore_terminal);            // 程式結束時恢復終端機設定
	return;
}	

void after_ack(FILE *fp, int sockfd, char* cliIP);
void
str_cli(FILE *fp, int sockfd)
{

	
	char	sendline[MAXLINE], recvline[MAXLINE];

	struct sockaddr_in	client_addr;
	socklen_t client_addrlen = sizeof(client_addr);
	if (getsockname(sockfd, (SA*) &client_addr, &client_addrlen) < 0) err_sys("getsockname error");

	char cli_IP[100];
	inet_ntop(AF_INET, &client_addr.sin_addr, cli_IP, sizeof(cli_IP));
	snprintf(sendline, sizeof(sendline), "112550032 %s\n", cli_IP);
	// printf("sendline: %s", sendline);

	Writen(sockfd, sendline, strlen(sendline));
	int n = Read(sockfd, recvline, MAXLINE);
    if (n == 0) err_quit("str_cli: server terminated prematurely");

	recvline[n]='\0';
    
	// Fputs(recvline, stdout); // 為什麼這行不會自己印出來，而是要有按鍵輸入才會印
	// fflush(stdout);

	if(strcmp(recvline, "ack")==0){
		
		after_ack(fp, sockfd, cli_IP);
	}

}

void after_ack(FILE *fp, int sockfd, char* cliIP){

	int			maxfdp1, stdineof;
	fd_set  	rset;
	char		buf[MAXLINE];
	int			nn;
	int 		byte_sum = 0;
	char 		c='0';

	stdineof = 0;
	FD_ZERO(&rset);

	for ( ; ; ) {

		if (stdineof == 0) FD_SET(fileno(fp), &rset);
		FD_SET(sockfd, &rset);
		maxfdp1 = max(fileno(fp), sockfd) + 1;
		Select(maxfdp1, &rset, NULL, NULL, NULL);

		if (FD_ISSET(fileno(fp), &rset)) {  /* input is readable */
			if ( (c = getchar()) == EOF) {
				stdineof = 1;
				Shutdown(sockfd, SHUT_WR);	/* send FIN */
				FD_CLR(fileno(fp), &rset);
				continue;
			}
			byte_sum += c;
			sprintf(buf, "%d\n", c);

			printf("cli check, char: %c, byte_sum: %d\n", c, byte_sum);
			
			Writen(sockfd, buf, strlen(buf));
		}

		if (FD_ISSET(sockfd, &rset)) {	/* socket is readable */
			if ( (nn = Read(sockfd, buf, MAXLINE)) == 0) {
				if (stdineof == 1)
					return;		/* normal termination */
				else
					err_quit("after_ack str_cli: server terminated prematurely");
			}

			buf[nn]='\0';
			if(strcmp(buf, "bad")==0) {
				byte_sum -= c;
				printf("*****bad received, cli check, char: %c, byte_sum: %d\n", c, byte_sum);
			}
			else if (strcmp(buf, "stop")==0) {
				char chcksumbuf[100];
				sprintf(chcksumbuf, "%d %s\n", byte_sum%256, cliIP);
				printf("*****stop, checksum: %d\n", byte_sum);
				printf("buf content: %s", chcksumbuf);
				Writen(sockfd, chcksumbuf, strlen(chcksumbuf));
			}
			else if (strcmp(buf, "nak")==0)printf("***** nak\n");
			else if (strcmp(buf, "ok")==0) {
				printf("***** ok\n");
				break;
			}
		}

		
	}

}

int
main(int argc, char **argv)
{
	printf("latest version, hope it works\n");
	set_terminal();

	int					sockfd;
	struct sockaddr_in	servaddr;

	if (argc != 2)
		err_quit("usage: tcpcli <IPaddress>");

	sockfd = Socket(AF_INET, SOCK_STREAM, 0);
	// printf("in main1, sockfd: %d\n", sockfd);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(SERV_PORT+1);
	Inet_pton(AF_INET, argv[1], &servaddr.sin_addr);

	Connect(sockfd, (SA *) &servaddr, sizeof(servaddr));

	str_cli(stdin, sockfd);		/* do it all */
	close(sockfd);
	exit(0);
}

/*
 client_ncurses.c
 - Connects to multiroom_server
 - Displays welcome/waiting messages until GAME_START
 - After GAME_START, shows map updates via STATE messages
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>

#define BUF_LEN 2048
#define MAP_W 20
#define MAP_H 10

int sockfd = -1;
int game_started = 0;
char board[MAP_H][MAP_W+1];
WINDOW *mapwin;

void init_board() {
    for (int r=0;r<MAP_H;r++) {
        for (int c=0;c<MAP_W;c++) board[r][c]='.';
        board[r][MAP_W]=0;
    }
}

void draw_ui() {
    werase(mapwin);
    box(mapwin, 0, 0);
    for (int r=0;r<MAP_H;r++) {
        mvwprintw(mapwin, r+1, 1, "%s", board[r]);
    }
    wrefresh(mapwin);
}

void set_nonblock(int fd) {
    int f = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, f | O_NONBLOCK);
}

void *recv_thread(void *arg) {
    char buf[BUF_LEN];
    int n;
    while (1) {
        n = recv(sockfd, buf, sizeof(buf)-1, 0);
        if (n <= 0) {
            // server closed
            break;
        }
        buf[n]=0;
        char *line = strtok(buf, "\n");
        while (line) {
            if (strncmp(line, "WELCOME",7)==0) {
                mvprintw(MAP_H+3, 0, "%s", line);
                refresh();
            } else if (strncmp(line, "PLAYER_JOIN",11)==0) {
                mvprintw(MAP_H+4, 0, "%s", line);
                refresh();
            } else if (strncmp(line, "GAME_START",10)==0) {
                game_started = 1;
                init_board();
                mvprintw(MAP_H+5, 0, "GAME STARTED!");
                refresh();
            } else if (strncmp(line, "STATE",5)==0) {
                // ignore header
            } else if (line[0]=='P') {
                int id, role, x, y, hp;
                if (sscanf(line, "P %d %d %d %d %d", &id, &role, &x, &y, &hp)==5) {
                    if (x>=0 && x<MAP_W && y>=0 && y<MAP_H) {
                        if (hp>0) board[y][x] = (role==1)?'G':'H';
                        else board[y][x] = 'X';
                    }
                }
                draw_ui();
            } else if (strncmp(line, "BULLET",6)==0) {
                int sh, tx, ty; char dir;
                if (sscanf(line, "BULLET %d %c %d %d", &sh, &dir, &tx, &ty)==4) {
                    if (tx>=0 && tx<MAP_W && ty>=0 && ty<MAP_H) {
                        char prev = board[ty][tx];
                        board[ty][tx] = '*';
                        draw_ui();
                        usleep(150000);
                        board[ty][tx] = prev;
                        draw_ui();
                    }
                }
            } else if (strncmp(line, "HIT",3)==0) {
                mvprintw(MAP_H+6, 0, "%s", line);
                refresh();
            }
            line = strtok(NULL, "\n");
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("Usage: %s server_ip\n", argv[0]); return 1; }
    const char *server_ip = argv[1];
    struct sockaddr_in serv;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    serv.sin_family = AF_INET;
    serv.sin_port = htons(12345);
    inet_pton(AF_INET, server_ip, &serv.sin_addr);
    if (connect(sockfd, (struct sockaddr*)&serv, sizeof(serv))<0) {
        perror("connect");
        return 1;
    }
    set_nonblock(sockfd);

    initscr();
    noecho();
    cbreak();
    curs_set(0);
    mapwin = newwin(MAP_H+2, MAP_W+2, 0, 0);

    init_board();
    draw_ui();

    pthread_t rt;
    pthread_create(&rt, NULL, recv_thread, NULL);

    nodelay(stdscr, TRUE);
    int ch;
    while (1) {
        ch = getch();
        if (ch != ERR) {
            if (ch=='q') { send(sockfd, "QUIT\n",5,0); break; }
            if (!game_started) { mvprintw(MAP_H+2, 0, "Waiting for players..."); refresh(); usleep(50000); continue; }
            if (ch=='w') send(sockfd, "MOVE U\n",7,0);
            else if (ch=='s') send(sockfd, "MOVE D\n",7,0);
            else if (ch=='a') send(sockfd, "MOVE L\n",7,0);
            else if (ch=='d') send(sockfd, "MOVE R\n",7,0);
            else if (ch=='i') send(sockfd, "SHOOT U\n",8,0);
            else if (ch=='k') send(sockfd, "SHOOT D\n",8,0);
            else if (ch=='j') send(sockfd, "SHOOT L\n",8,0);
            else if (ch=='l') send(sockfd, "SHOOT R\n",8,0);
        }
        usleep(16000);
    }

    endwin();
    close(sockfd);
    return 0;
}

/*
CC = gcc
CFLAGS = -O2 -Wall
all: server client

server: multiroom_server.c
	$(CC) $(CFLAGS) -o server multiroom_server.c -lpthread

client: client_ncurses.c
	$(CC) $(CFLAGS) -o client client_ncurses.c -lncurses -lpthread

clean:
	rm -f server client
*/

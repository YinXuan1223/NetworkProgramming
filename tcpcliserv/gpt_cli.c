// client_ncurses.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>
#include <arpa/inet.h>

#define BUF_LEN 4096
#define MAP_W 20
#define MAP_H 12

int sockfd;
int game_started = 0;

char board[MAP_H][MAP_W];

void init_board() {
    for (int y=0; y<MAP_H; y++)
        for (int x=0; x<MAP_W; x++)
            board[y][x] = '.';
}

void draw_board() {
    for (int y=0; y<MAP_H; y++) {
        for (int x=0; x<MAP_W; x++) {
            mvaddch(y, x, board[y][x]);
        }
    }
}

void draw_ui() {
    clear();
    draw_board();
    mvprintw(MAP_H+1, 0, "w/a/s/d move | i/j/k/l shoot | q quit");
    refresh();
}

/* process a complete received line */
void process_line(char *line) {
    if (strncmp(line, "WELCOME", 7) == 0) {
        mvprintw(MAP_H+3, 0, "%s", line);
        refresh();
    }
    else if (strncmp(line, "New player", 10) == 0) {
        mvprintw(MAP_H+4, 0, "%s", line);
        refresh();
    }
    else if (strncmp(line, "GAME_START", 10) == 0) {
        game_started = 1;
        init_board();
        draw_ui();
    }
    else if (strncmp(line, "STATE", 5) == 0) {
        // ignore header
    }
    else if (line[0] == 'P') {
        int id, role, x, y, hp;
        if (sscanf(line, "P %d %d %d %d %d", &id, &role, &x, &y, &hp) == 5) {
            if (x>=0 && x<MAP_W && y>=0 && y<MAP_H) {
                if (hp > 0) board[y][x] = (role==1?'G':'H');
                else board[y][x] = 'X';
            }
        }
        draw_ui();
    }
    else if (strncmp(line, "BULLET", 6) == 0) {
        int sh, tx, ty; char dir;
        if (sscanf(line, "BULLET %d %c %d %d", &sh, &dir, &tx, &ty)==4) {
            if (tx>=0 && tx<MAP_W && ty>=0 && ty<MAP_H) {
                char prev = board[ty][tx];
                board[ty][tx] = '*';
                draw_ui();
                usleep(120000);
                board[ty][tx] = prev;
                draw_ui();
            }
        }
    }
    else if (strncmp(line, "HIT", 3)==0) {
        mvprintw(MAP_H+6, 0, "%s", line);
        refresh();
    }
}

/* ----- ROBUST TCP STREAM PARSER ----- */
void *recv_thread(void *arg) {
    static char buf[BUF_LEN];
    static char linebuf[BUF_LEN];
    int linepos = 0;

    while (1) {
        int n = recv(sockfd, buf, sizeof(buf), 0);
        if (n <= 0) break;

        for (int i=0; i<n; i++) {
            char c = buf[i];

            if (c == '\n') {
                linebuf[linepos] = 0;
                process_line(linebuf);
                linepos = 0;
            } else {
                if (linepos < BUF_LEN-1)
                    linebuf[linepos++] = c;
            }
        }
    }
    return NULL;
}

/* ----- input thread ----- */
void *input_thread(void *arg) {
    while (1) {
        int ch = getch();
        if (ch == 'q') {
            send(sockfd, "QUIT\n", 5, 0);
            endwin();
            exit(0);
        }
        if (!game_started) continue;

        if (ch=='w') send(sockfd, "MOVE U\n", 7, 0);
        if (ch=='s') send(sockfd, "MOVE D\n", 7, 0);
        if (ch=='a') send(sockfd, "MOVE L\n", 7, 0);
        if (ch=='d') send(sockfd, "MOVE R\n", 7, 0);

        if (ch=='i') send(sockfd, "SHOOT U\n", 8, 0);
        if (ch=='k') send(sockfd, "SHOOT D\n", 8, 0);
        if (ch=='j') send(sockfd, "SHOOT L\n", 8, 0);
        if (ch=='l') send(sockfd, "SHOOT R\n", 8, 0);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: %s <server_ip>\n", argv[0]);
        return 1;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(9877);
    inet_pton(AF_INET, argv[1], &serv.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("connect");
        return 1;
    }

    // ncurses setup
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);

    pthread_t t1, t2;
    pthread_create(&t1, NULL, recv_thread, NULL);
    pthread_create(&t2, NULL, input_thread, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    endwin();
    return 0;
}

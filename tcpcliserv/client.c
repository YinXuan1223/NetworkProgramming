// client_ncurses.c
#include "unp.h"

#include <ncurses.h>


#define MAP_W 20
#define MAP_H 12

int sockfd;
int game_started = 0;

char board[MAP_H][MAP_W];

char id_to_char(int id){
    char c;
    switch (id)
    {
    case 1:
        c = 'a';
        break;
    case 2:
        c = 'b';
        break;
    case 3:
        c = '1';
        break;
    case 4:
        c = '2';
        break;
    case 5:
        c = '3';
        break;
    case 6:
        c = '4';
        break;

    default:
        c = ' ';
        break;
    }
    return c;
}

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
    mvprintw(MAP_H+3, 0, "love u");
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
        init_board();
    }
    else if (line[0] == 'P') {
        int id, role, x, y, blood;
        if (sscanf(line, "P %d %d %d %d %d", &id, &role, &x, &y, &blood) == 5) {
            if (x>=0 && x<MAP_W && y>=0 && y<MAP_H) {
                if (blood > 0) board[y][x] = id_to_char(id);
                else board[y][x] = '.';
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


void *recv_thread(void *arg) {
    static char buf[MAXLINE];
    static char linebuf[MAXLINE];
    int linepos = 0;

    while (1) {
    
        if(Readline(sockfd, buf, MAXLINE)<0){
            err_quit("str_cli: server terminated prematurely");
        }
        process_line(buf);
        
    }
    return NULL;
}


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

    
    struct sockaddr_in	servaddr;

    if (argc < 2) {
        printf("usage: %s <server_ip>\n", argv[0]);
        return 1;
    }

   
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(9877);
    inet_pton(AF_INET, argv[1], &servaddr.sin_addr);

    Connect(sockfd, (SA*)&servaddr, sizeof(servaddr));
      

    // ncurses setup
    initscr();  // init ncurse screen
    noecho();   // 不會把鍵盤輸入顯示出來
    cbreak();   
    keypad(stdscr, TRUE);   // 可加特殊建

    mvprintw(MAP_H+10, 0, "new client\n"); // 測試的輸出

    pthread_t t1, t2;
    pthread_create(&t1, NULL, recv_thread, NULL);
    pthread_create(&t2, NULL, input_thread, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    endwin();
    return 0;
}

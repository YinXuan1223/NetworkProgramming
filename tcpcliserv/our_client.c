#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ctype.h>
#include <ncurses.h>
#include <sys/select.h>
#include <sys/time.h>

#define MAX_PLAYERS 32
#define MAP_W 40
#define MAP_H 20

// ====================================================================
//  Global game state
// ====================================================================

typedef struct {
    int id;
    int team;    // 0 = human, 1 = ghost
    int x, y;
    int hp;
    int alive;
} Player;

Player players[MAX_PLAYERS];
int my_id = -1;

char board[MAP_H][MAP_W];

// ====================================================================
//  ncurses rendering
// ====================================================================

void init_nc() {
    initscr();
    noecho();
    cbreak();
    curs_set(0);
    nodelay(stdscr, TRUE);      // non-blocking getch()
    keypad(stdscr, TRUE);
}

void end_nc() {
    endwin();
}

void clear_board() {
    for (int r = 0; r < MAP_H; r++)
        for (int c = 0; c < MAP_W; c++)
            board[r][c] = '.';
}

void render_board() {
    for (int r = 0; r < MAP_H; r++) {
        for (int c = 0; c < MAP_W; c++) {
            char ch = board[r][c];
            mvaddch(r, c, ch);
        }
    }
    refresh();
}

void mark_bullet_path(int x, int y) {
    if (x >= 0 && x < MAP_H && y >= 0 && y < MAP_W) {
        mvaddch(x, y, '*');
    }
}

void clear_cell(int x, int y, char orig) {
    if (x >= 0 && x < MAP_H && y >= 0 && y < MAP_W) {
        mvaddch(x, y, orig);
    }
}

// ====================================================================
//  Parse server messages
// ====================================================================

void handle_state_line(char *line) {
    // Format: "P id team x y hp"
    int id, team, x, y, hp;
    if (sscanf(line, "P %d %d %d %d %d", &id, &team, &x, &y, &hp) == 5) {
        players[id].id = id;
        players[id].team = team;
        players[id].x = x;
        players[id].y = y;
        players[id].hp = hp;
        players[id].alive = (hp > 0);
    }
}

void rebuild_board() {
    clear_board();

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].alive) {
            char ch = (players[i].team == 1) ? 'G' : 'H';
            if (players[i].x >= 0 && players[i].x < MAP_H &&
                players[i].y >= 0 && players[i].y < MAP_W) {
                board[players[i].x][players[i].y] = ch;
            }
        }
    }
}

// ====================================================================
//  Networking utilities
// ====================================================================

void set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void send_cmd(int sock, const char *cmd) {
    write(sock, cmd, strlen(cmd));
}

// ====================================================================
//  Main client loop
// ====================================================================

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("usage: ./client <server_ip>\n");
        return 1;
    }

    // Connect TCP
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv;
    serv.sin_family = AF_INET;
    serv.sin_port = htons(12345);
    inet_pton(AF_INET, argv[1], &serv.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("connect");
        return 1;
    }

    set_nonblock(sock);

    // ncurses init
    init_nc();

    clear_board();
    render_board();

    char recvbuf[2048];
    int recvlen = 0;

    struct timeval last_bullet_time = {0};
    int bullet_x = -1, bullet_y = -1;
    char bullet_restore = '.';

    while (1) {
        // --------------------------------------------------------------
        // Handle keyboard input
        // --------------------------------------------------------------
        int ch = getch();
        if (ch != ERR) {
            if (ch == 'q') break;

            if (ch == 'w') send_cmd(sock, "MOVE U\n");
            else if (ch == 's') send_cmd(sock, "MOVE D\n");
            else if (ch == 'a') send_cmd(sock, "MOVE L\n");
            else if (ch == 'd') send_cmd(sock, "MOVE R\n");

            else if (ch == 'i') send_cmd(sock, "SHOOT U\n");
            else if (ch == 'k') send_cmd(sock, "SHOOT D\n");
            else if (ch == 'j') send_cmd(sock, "SHOOT L\n");
            else if (ch == 'l') send_cmd(sock, "SHOOT R\n");
        }

        // --------------------------------------------------------------
        // Receive data from server (non-blocking)
        // --------------------------------------------------------------
        char temp[256];
        int n = read(sock, temp, sizeof(temp)-1);
        if (n > 0) {
            temp[n] = '\0';
            strcat(recvbuf, temp);

            // Process lines
            char *line;
            while ((line = strchr(recvbuf, '\n')) != NULL) {
                *line = '\0';
                char msg[256];
                strcpy(msg, recvbuf);
                memmove(recvbuf, line+1, strlen(line+1)+1);

                // -----------------------------
                // Parse message
                // -----------------------------
                if (strncmp(msg, "STATE", 5) == 0) {
                    // state incoming, next lines will be P ...
                }
                else if (msg[0] == 'P') {
                    handle_state_line(msg);
                }
                else if (strncmp(msg, "BULLET", 6) == 0) {
                    int sx, sy;
                    if (sscanf(msg, "BULLET %*d %*s %d %d", &sx, &sy) == 2) {
                        // Mark bullet path briefly
                        bullet_x = sx;
                        bullet_y = sy;
                        bullet_restore = board[sx][sy];

                        mark_bullet_path(sx, sy);
                        gettimeofday(&last_bullet_time, NULL);
                    }
                }

                // After parsing entire state batch: rebuild + redraw
                rebuild_board();
                render_board();
            }
        }

        // --------------------------------------------------------------
        // Clear bullet effect after 150 ms
        // --------------------------------------------------------------
        if (bullet_x >= 0) {
            struct timeval now;
            gettimeofday(&now, NULL);

            long diff_ms =
                (now.tv_sec - last_bullet_time.tv_sec) * 1000 +
                (now.tv_usec - last_bullet_time.tv_usec) / 1000;

            if (diff_ms > 150) {
                clear_cell(bullet_x, bullet_y, bullet_restore);
                bullet_x = -1;
                bullet_y = -1;
                refresh();
            }
        }

        // Avoid busy-looping
        usleep(16000); // ~60 FPS
    }

    end_nc();
    close(sock);
    return 0;
}





// /*
//  Minimal prototype client.c
//  - Connects to server, sends simple commands: MOVE/SHOOT/QUIT
//  - Renders ASCII map using ANSI escapes
//  - Non-blocking keyboard using termios
//  Build: gcc -o client client.c
//  Run: ./client 127.0.0.1
// */
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <termios.h>
// #include <fcntl.h>
// #include <sys/socket.h>
// #include <arpa/inet.h>
// #include <pthread.h>

// #define BUF_LEN 1024
// #define MAP_W 20
// #define MAP_H 10

// int sockfd = -1;
// char board[MAP_H][MAP_W+1];
// int my_id = -1;
// int my_team = -1;

// // terminal raw mode
// struct termios orig_termios;

// void disable_raw() {
//     tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
// }

// void enable_raw() {
//     tcgetattr(STDIN_FILENO, &orig_termios);
//     atexit(disable_raw);
//     struct termios raw = orig_termios;
//     raw.c_lflag &= ~(ECHO|ICANON);
//     tcsetattr(STDIN_FILENO, TCSANOW, &raw);
//     // set non-blocking
//     int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
//     fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
// }

// // simple clear screen
// void cls() {
//     printf("\033[2J\033[H");
// }

// void init_board() {
//     for (int r=0;r<MAP_H;r++) {
//         for (int c=0;c<MAP_W;c++) board[r][c]='.';
//         board[r][MAP_W]=0;
//     }
// }

// void draw_board() {
//     cls();
//     for (int r=0;r<MAP_H;r++) {
//         printf("%s\n", board[r]);
//     }
//     printf("\nYou: id=%d team=%d\n", my_id, my_team);
//     printf("Controls: wsad move | ikjl shoot | q quit\n");
//     fflush(stdout);
// }

// void *recv_thread(void *arg) {
//     char buf[BUF_LEN];
//     while (1) {
//         int n = recv(sockfd, buf, sizeof(buf)-1, 0);
//         if (n<=0) { printf("Disconnected from server\n"); exit(0); }
//         buf[n]=0;
//         // parse lines
//         char *line = strtok(buf, "\n");
//         while (line) {
//             if (strncmp(line,"WELCOME ",8)==0) {
//                 sscanf(line, "WELCOME %d %d", &my_id, &my_team);
//             } else if (strncmp(line,"STATE ",6)==0) {
//                 // ignore header count and parse following P lines from same recv may be present
//                 // we'll just reinitialize board and parse players
//                 init_board();
//             } else if (strncmp(line,"P ",2)==0) {
//                 int id, team, x, y, hp;
//                 sscanf(line, "P %d %d %d %d %d", &id, &team, &x, &y, &hp);
//                 if (x>=0 && x<MAP_W && y>=0 && y<MAP_H) {
//                     if (hp>0) board[y][x] = (team==0 ? 'H' : 'G');
//                     else board[y][x] = 'X';
//                 }
//             } else if (strncmp(line,"MOVE ",5)==0) {
//                 // MOVE id x y
//                 int id,x,y;
//                 sscanf(line, "MOVE %d %d %d", &id,&x,&y);
//                 // safe: re-draw by setting symbol (no erase of previous, but state updates each STATE)
//                 // To keep simple, just set position; STATE periodic will correct map
//             } else if (strncmp(line,"BULLET ",7)==0) {
//                 int shooter; char dir; int tx,ty;
//                 sscanf(line, "BULLET %d %c %d %d", &shooter, &dir, &tx, &ty);
//                 // temporarily mark target cell with '*'
//                 if (tx>=0 && tx<MAP_W && ty>=0 && ty<MAP_H) {
//                     char prev = board[ty][tx];
//                     board[ty][tx] = '*';
//                     draw_board();
//                     usleep(150000);
//                     board[ty][tx] = prev;
//                 }
//             } else if (strncmp(line,"HIT ",4)==0) {
//                 int at, vic, newhp;
//                 sscanf(line, "HIT %d %d %d", &at, &vic, &newhp);
//                 // just redraw will pick it up on next STATE; show message
//                 printf("PLAYER %d HIT PLAYER %d (hp=%d)\n", at, vic, newhp);
//                 fflush(stdout);
//             }
//             line = strtok(NULL, "\n");
//         }
//         draw_board();
//     }
//     return NULL;
// }

// int main(int argc, char **argv) {
//     if (argc<2) { printf("Usage: %s server_ip\n", argv[0]); return 1; }
//     const char *server_ip = argv[1];
//     struct sockaddr_in servaddr;
//     sockfd = socket(AF_INET, SOCK_STREAM, 0);
//     if (sockfd<0) { perror("socket"); return 1; }
//     servaddr.sin_family = AF_INET;
//     servaddr.sin_port = htons(12345);
//     inet_pton(AF_INET, server_ip, &servaddr.sin_addr);
//     if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr))<0) { perror("connect"); return 1; }

//     init_board();
//     enable_raw();

//     pthread_t rt;
//     pthread_create(&rt, NULL, recv_thread, NULL);

//     draw_board();

//     char outbuf[128];
//     while (1) {
//         int c = getchar();
//         if (c==EOF) { usleep(10000); continue; }
//         if (c=='q') {
//             send(sockfd, "QUIT\n", 5, 0);
//             break;
//         } else if (c=='w' || c=='a' || c=='s' || c=='d') {
//             char dir='U';
//             if (c=='w') dir='U';
//             if (c=='s') dir='D';
//             if (c=='a') dir='L';
//             if (c=='d') dir='R';
//             int len = snprintf(outbuf, sizeof(outbuf), "MOVE %c\n", dir);
//             send(sockfd, outbuf, len, 0);
//         } else if (c=='i' || c=='k' || c=='j' || c=='l') {
//             char dir='U';
//             if (c=='i') dir='U';
//             if (c=='k') dir='D';
//             if (c=='j') dir='L';
//             if (c=='l') dir='R';
//             int len = snprintf(outbuf, sizeof(outbuf), "SHOOT %c\n", dir);
//             send(sockfd, outbuf, len, 0);
//         }
//         usleep(10000);
//     }

//     disable_raw();
//     close(sockfd);
//     return 0;
// }
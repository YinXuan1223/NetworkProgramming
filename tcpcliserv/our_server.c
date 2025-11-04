/*
 Minimal prototype server.c
 - Select-based TCP server
 - Text-based protocol (newline-terminated messages)
 - Periodic STATE broadcast every 200ms
 Build: gcc -o server server.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>

#define PORT 12345
#define MAXCLIENTS 16
#define BUF_LEN 512
#define MAP_W 20
#define MAP_H 10

typedef struct {
    int used;
    int fd;
    int id;
    int team; // 0 human, 1 ghost
    int x,y;
    int hp;
    int bullets;
} Player;

Player players[MAXCLIENTS];
char map_grid[MAP_H][MAP_W+1];

int listener = -1;

void init_map() {
    for (int r=0;r<MAP_H;r++) {
        for (int c=0;c<MAP_W;c++) map_grid[r][c] = '.';
        map_grid[r][MAP_W] = 0;
    }
}

void place_players_on_map() {
    // clear map (keep walls none)
    init_map();
    for (int i=0;i<MAXCLIENTS;i++) {
        if (!players[i].used) continue;
        int r = players[i].y;
        int c = players[i].x;
        if (r>=0 && r<MAP_H && c>=0 && c<MAP_W) {
            if (players[i].hp>0) map_grid[r][c] = (players[i].team==0 ? 'H' : 'G');
            else map_grid[r][c] = 'X';
        }
    }
}

int create_listen(int port) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd<0) { perror("socket"); exit(1); }
    int opt=1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    addr.sin_family=AF_INET; addr.sin_addr.s_addr=INADDR_ANY; addr.sin_port=htons(port);
    if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr))<0) { perror("bind"); exit(1); }
    if (listen(sfd, 10)<0) { perror("listen"); exit(1); }
    return sfd;
}

void send_all(const char *s) {
    for (int i=0;i<MAXCLIENTS;i++) {
        if (!players[i].used) continue;
        if (players[i].fd>=0) {
            send(players[i].fd, s, strlen(s), 0);
        }
    }
}

void broadcast_state(int tick) {
    char buf[2048];
    int off=0;
    off += snprintf(buf+off, sizeof(buf)-off, "STATE %d %d\n", tick, MAXCLIENTS);
    for (int i=0;i<MAXCLIENTS;i++) {
        if (!players[i].used) continue;
        off += snprintf(buf+off, sizeof(buf)-off, "P %d %d %d %d %d\n",
                        players[i].id, players[i].team, players[i].x, players[i].y, players[i].hp);
    }
    send_all(buf);
}

void handle_move(Player *p, char dir) {
    if (!p) return;
    int nx=p->x, ny=p->y;
    if (dir=='U') ny--;
    else if (dir=='D') ny++;
    else if (dir=='L') nx--;
    else if (dir=='R') nx++;
    if (nx<0||nx>=MAP_W||ny<0||ny>=MAP_H) return;
    p->x = nx; p->y = ny;
}

void handle_shoot(Player *p, char dir) {
    if (!p) return;
    // simple: shoot range 1; find if any player at that cell, reduce hp
    int tx=p->x, ty=p->y;
    if (dir=='U') ty--;
    else if (dir=='D') ty++;
    else if (dir=='L') tx--;
    else if (dir=='R') tx++;
    if (tx<0||tx>=MAP_W||ty<0||ty>=MAP_H) return;
    // find hit
    for (int i=0;i<MAXCLIENTS;i++) {
        if (!players[i].used) continue;
        if (players[i].hp<=0) continue;
        if (players[i].x==tx && players[i].y==ty) {
            players[i].hp -= 1;
            if (players[i].hp<0) players[i].hp=0;
            char ev[128];
            snprintf(ev, sizeof(ev), "HIT %d %d %d\n", p->id, players[i].id, players[i].hp);
            send_all(ev);
            return;
        }
    }
    // no hit: still announce bullet event (path length 1)
    char ev[128];
    snprintf(ev, sizeof(ev), "BULLET %d %c %d %d\n", p->id, dir, tx, ty);
    send_all(ev);
}

int next_id = 1;

void add_new_client(int fd) {
    for (int i=0;i<MAXCLIENTS;i++) {
        if (!players[i].used) {
            players[i].used = 1;
            players[i].fd = fd;
            players[i].id = next_id++;
            players[i].team = (players[i].id % 2); // alternate team
            // place at random-ish spot
            players[i].x = (players[i].id * 3) % MAP_W;
            players[i].y = (players[i].id * 2) % MAP_H;
            players[i].hp = (players[i].team==0 ? 2 : 1);
            players[i].bullets = 3;
            char welcome[128];
            snprintf(welcome, sizeof(welcome), "WELCOME %d %d\n", players[i].id, players[i].team);
            send(players[i].fd, welcome, strlen(welcome), 0);
            return;
        }
    }
    // no slot
    const char *full = "FULL\n";
    send(fd, full, strlen(full), 0);
    close(fd);
}

void remove_client_fd(int fd) {
    for (int i=0;i<MAXCLIENTS;i++) {
        if (players[i].used && players[i].fd==fd) {
            players[i].used = 0;
            players[i].fd = -1;
            return;
        }
    }
}

int main() {
    for (int i=0;i<MAXCLIENTS;i++) players[i].used=0;
    init_map();
    listener = create_listen(PORT);
    printf("Server listening on %d\n", PORT);

    fd_set allset, rset;
    FD_ZERO(&allset);
    FD_SET(listener, &allset);
    int maxfd = listener;

    struct timeval tv;
    int tick = 0;
    const int TICK_MS = 200;
    struct timespec last, now;
    clock_gettime(CLOCK_MONOTONIC, &last);

    char buf[BUF_LEN];

    while (1) {
        rset = allset;

        // compute timeout to next tick
        clock_gettime(CLOCK_MONOTONIC, &now);
        long diff_ms = (now.tv_sec - last.tv_sec)*1000 + (now.tv_nsec - last.tv_nsec)/1000000;
        long to_wait = TICK_MS - diff_ms;
        if (to_wait < 0) to_wait = 0;

        tv.tv_sec = to_wait / 1000;
        tv.tv_usec = (to_wait % 1000) * 1000;

        int nready = select(maxfd+1, &rset, NULL, NULL, &tv);
        if (nready < 0) {
            if (errno==EINTR) continue;
            perror("select"); break;
        }

        // new connection
        if (FD_ISSET(listener, &rset)) {
            int conn = accept(listener, NULL, NULL);
            if (conn >= 0) {
                // set nonblocking? keep blocking for simplicity
                printf("New conn %d\n", conn);
                FD_SET(conn, &allset);
                if (conn > maxfd) maxfd = conn;
                add_new_client(conn);
            }
            nready--;
        }

        // handle client input
        for (int fd=listener+1; fd<=maxfd && nready>0; fd++) {
            if (!FD_ISSET(fd, &rset)) continue;
            nready--;
            int n = recv(fd, buf, sizeof(buf)-1, 0);
            if (n <= 0) {
                // disconnect
                remove_client_fd(fd);
                FD_CLR(fd, &allset);
                close(fd);
                continue;
            }
            buf[n]=0;
            // simple: each line is a command
            char *line = strtok(buf, "\n");
            while (line) {
                // find player by fd
                Player *p = NULL;
                for (int i=0;i<MAXCLIENTS;i++) if (players[i].used && players[i].fd==fd) p=&players[i];
                if (!p) { line = strtok(NULL, "\n"); continue; }

                if (strncmp(line, "MOVE ",5)==0) {
                    char d = line[5];
                    handle_move(p, d);
                    // announce move
                    char ev[128];
                    snprintf(ev, sizeof(ev), "MOVE %d %d %d\n", p->id, p->x, p->y);
                    send_all(ev);
                } else if (strncmp(line, "SHOOT ",6)==0) {
                    char d = line[6];
                    handle_shoot(p, d);
                } else if (strncmp(line, "QUIT",4)==0) {
                    remove_client_fd(fd);
                    FD_CLR(fd, &allset);
                    close(fd);
                }
                line = strtok(NULL, "\n");
            }
        }

        // check if it's time to broadcast state
        clock_gettime(CLOCK_MONOTONIC, &now);
        diff_ms = (now.tv_sec - last.tv_sec)*1000 + (now.tv_nsec - last.tv_nsec)/1000000;
        if (diff_ms >= TICK_MS) {
            last = now;
            tick++;
            place_players_on_map();
            broadcast_state(tick);
        }
    }

    close(listener);
    return 0;
}
/*
 multiroom_server.c
 - Supports multiple rooms (matches), each needs 6 players to start.
 - When player joins: sends Welcome message with player id, role, and current count.
 - When room reaches 6 players, starts a game loop thread for that room which broadcasts STATE periodically.
 - Simple text protocol, compatible with ncurses client.
 Build: gcc -o server multiroom_server.c -lpthread
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 12345
#define MAX_ROOMS 8
#define PLAYERS_PER_ROOM 6
#define MAP_W 20
#define MAP_H 10
#define BUF_LEN 512

typedef enum { ROOM_WAITING=0, ROOM_RUNNING=1 } RoomStatus;

typedef struct {
    int fd;
    int player_idx; // 0..5 (slot index in room)
    int role; // 0 human, 1 ghost
    int id;   // unique id across server
    int x,y;
    int hp;
    int used;
} PlayerSlot;

typedef struct {
    pthread_mutex_t lock;
    RoomStatus status;
    int player_count;
    PlayerSlot players[PLAYERS_PER_ROOM];
    char map[MAP_H][MAP_W+1];
    int room_id;
} Room;

Room rooms[MAX_ROOMS];

int listenfd = -1;
int global_next_id = 1;

void init_rooms() {
    for (int r=0;r<MAX_ROOMS;r++) {
        Room *rm = &rooms[r];
        pthread_mutex_init(&rm->lock, NULL);
        rm->status = ROOM_WAITING;
        rm->player_count = 0;
        rm->room_id = r;
        for (int i=0;i<PLAYERS_PER_ROOM;i++) {
            rm->players[i].fd = -1;
            rm->players[i].used = 0;
        }
        for (int y=0;y<MAP_H;y++) {
            for (int x=0;x<MAP_W;x++) rm->map[y][x] = '.';
            rm->map[y][MAP_W] = 0;
        }
    }
}

int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void sendf(int fd, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    send(fd, buf, strlen(buf), 0);
}

void broadcast_room(Room *rm, const char *msg) {
    for (int i=0;i<PLAYERS_PER_ROOM;i++) {
        if (!rm->players[i].used) continue;
        int fd = rm->players[i].fd;
        if (fd >= 0) {
            send(fd, msg, strlen(msg), 0);
        }
    }
}

void send_welcome(Room *rm, int slot) {
    PlayerSlot *ps = &rm->players[slot];
    int current = rm->player_count;
    const char *role = (ps->role==1) ? "ghost" : "human";
    char msg[256];
    snprintf(msg, sizeof(msg),
        "WELCOME %d %s waiting for others to join current=%d room=%d\n",
        ps->player_idx, role, current, rm->room_id);
    send(ps->fd, msg, strlen(msg), 0);
}

void send_game_start(Room *rm) {
    printf("Starting room %d:\n", rm->room_id);
    for (int i=0;i<PLAYERS_PER_ROOM;i++) {
        printf("slot %d: used=%d fd=%d\n", i, rm->players[i].used, rm->players[i].fd);
    }
    for (int i=0;i<PLAYERS_PER_ROOM;i++) {
        if (!rm->players[i].used) continue;
        int fd = rm->players[i].fd;
        char msg[256];
        snprintf(msg, sizeof(msg), "GAME_START room=%d\n", rm->room_id);
        send(fd, msg, strlen(msg), 0);
    }
}

void broadcast_state(Room *rm, int tick) {
    char buf[4096];
    int off = 0;
    off += snprintf(buf+off, sizeof(buf)-off, "STATE %d %d\n", tick, rm->player_count);
    for (int i=0;i<PLAYERS_PER_ROOM;i++) {
        if (!rm->players[i].used) continue;
        PlayerSlot *ps = &rm->players[i];
        off += snprintf(buf+off, sizeof(buf)-off, "P %d %d %d %d %d\n",
                        ps->id, ps->role, ps->x, ps->y, ps->hp);
    }
    broadcast_room(rm, buf);
}

void place_players_room(Room *rm) {
    for (int i=0;i<PLAYERS_PER_ROOM;i++) {
        if (!rm->players[i].used) continue;
        rm->players[i].x = (i*2) % MAP_W;
        rm->players[i].y = (i*3) % MAP_H;
        rm->players[i].hp = (rm->players[i].role==0) ? 2 : 1;
    }
}

void handle_player_cmd(Room *rm, PlayerSlot *ps, const char *line) {
    char cmd[128];
    strncpy(cmd, line, sizeof(cmd)-1);
    cmd[sizeof(cmd)-1]=0;
    if (strncmp(cmd, "MOVE ",5)==0) {
        char d = cmd[5];
        int nx = ps->x, ny = ps->y;
        if (d=='U') ny--;
        else if (d=='D') ny++;
        else if (d=='L') nx--;
        else if (d=='R') nx++;
        if (nx>=0 && nx<MAP_W && ny>=0 && ny<MAP_H) {
            ps->x = nx; ps->y = ny;
            char ev[128];
            snprintf(ev, sizeof(ev), "MOVE %d %d %d\n", ps->id, ps->x, ps->y);
            broadcast_room(rm, ev);
        }
    } else if (strncmp(cmd, "SHOOT ",6)==0) {
        char d = cmd[6];
        int tx = ps->x, ty = ps->y;
        if (d=='U') ty--;
        else if (d=='D') ty++;
        else if (d=='L') tx--;
        else if (d=='R') tx++;
        if (tx>=0 && tx<MAP_W && ty>=0 && ty<MAP_H) {
            int hit_id = -1;
            for (int i=0;i<PLAYERS_PER_ROOM;i++) {
                if (!rm->players[i].used) continue;
                PlayerSlot *other = &rm->players[i];
                if (other->x==tx && other->y==ty && other->hp>0) {
                    other->hp -= 1;
                    if (other->hp < 0) other->hp = 0;
                    hit_id = other->id;
                    char ev[128];
                    snprintf(ev, sizeof(ev), "HIT %d %d %d\n", ps->id, other->id, other->hp);
                    broadcast_room(rm, ev);
                    break;
                }
            }
            if (hit_id==-1) {
                char ev[128];
                snprintf(ev, sizeof(ev), "BULLET %d %c %d %d\n", ps->id, d, tx, ty);
                broadcast_room(rm, ev);
            }
        }
    } else if (strncmp(cmd, "QUIT",4)==0) {
        ps->used = 0;
        close(ps->fd);
        ps->fd = -1;
        rm->player_count--;
        char ev[128];
        snprintf(ev, sizeof(ev), "PLAYER_LEFT %d\n", ps->id);
        broadcast_room(rm, ev);
    }
}

void *room_game_loop(void *arg) {
    Room *rm = (Room*)arg;
    int tick = 0;
    const int TICK_MS = 200;

    place_players_room(rm);

    while (1) {
        pthread_mutex_lock(&rm->lock);
        if (rm->status != ROOM_RUNNING) {
            pthread_mutex_unlock(&rm->lock);
            break;
        }
        for (int i=0;i<PLAYERS_PER_ROOM;i++) {
            if (!rm->players[i].used) continue;
            int fd = rm->players[i].fd;
            char buf[BUF_LEN];
            int n = recv(fd, buf, sizeof(buf)-1, MSG_DONTWAIT);
            if (n > 0) {
                buf[n]=0;
                char *p = strtok(buf, "\n");
                while (p) {
                    handle_player_cmd(rm, &rm->players[i], p);
                    p = strtok(NULL, "\n");
                }
            } else if (n==0) {
                rm->players[i].used = 0;
                close(fd);
                rm->players[i].fd = -1;
                rm->player_count--;
            }
        }

        tick++;
        broadcast_state(rm, tick);

        pthread_mutex_unlock(&rm->lock);
        usleep(TICK_MS * 1000);
    }
    return NULL;
}

void start_room(Room *rm) {
    rm->status = ROOM_RUNNING;
    send_game_start(rm);
    pthread_t tid;
    if (pthread_create(&tid, NULL, room_game_loop, rm) != 0) {
        perror("pthread_create");
    } else {
        pthread_detach(tid);
    }
}

void assign_client_to_room(int clientfd) {
    for (int r=0;r<MAX_ROOMS;r++) {
        Room *rm = &rooms[r];
        pthread_mutex_lock(&rm->lock);
        if (rm->status == ROOM_WAITING && rm->player_count < PLAYERS_PER_ROOM) {
            int idx = -1;
            for (int i=0;i<PLAYERS_PER_ROOM;i++) if (!rm->players[i].used) { idx = i; break; }
            if (idx < 0) { pthread_mutex_unlock(&rm->lock); continue; }
            rm->players[idx].used = 1;
            rm->players[idx].fd = clientfd;
            rm->players[idx].player_idx = idx;
            rm->players[idx].id = global_next_id++;
            rm->players[idx].role = (idx==0)?1:0;
            rm->players[idx].x = 0; rm->players[idx].y = 0; rm->players[idx].hp = 1;
            rm->player_count++;

            char welcome[256];
            snprintf(welcome, sizeof(welcome),
                "WELCOME %d %s waiting for others to join current=%d room=%d\n",
                idx, (rm->players[idx].role==1)?"ghost":"human", rm->player_count, rm->room_id);
            send(clientfd, welcome, strlen(welcome), 0);

            char joinmsg[128];
            snprintf(joinmsg, sizeof(joinmsg), "PLAYER_JOIN %d current=%d\n", rm->players[idx].id, rm->player_count);
            broadcast_room(rm, joinmsg);

            if (rm->player_count == PLAYERS_PER_ROOM) {
                start_room(rm);
            }
            pthread_mutex_unlock(&rm->lock);
            return;
        }
        pthread_mutex_unlock(&rm->lock);
    }
    const char *busy = "SERVER_FULL\n";
    send(clientfd, busy, strlen(busy), 0);
    close(clientfd);
}

int create_listen(int port) {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); exit(1); }
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(sfd, 128) < 0) { perror("listen"); exit(1); }
    return sfd;
}

int main() {
    init_rooms();
    listenfd = create_listen(PORT);
    printf("Multi-room server listening on %d\n", PORT);

    while (1) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int conn = accept(listenfd, (struct sockaddr*)&cli, &clilen);
        if (conn < 0) {
            if (errno == EINTR) continue;
            perror("accept"); break;
        }
        set_nonblock(conn);
        printf("Accepted conn %d\n", conn);
        assign_client_to_room(conn);
    }

    close(listenfd);
    return 0;
}

#include	"unp.h"

#define     MAXROOM 10
#define		MAXPLAYER 3
#define     MAP_W 10
#define     MAP_H 10


typedef struct{
	int fd;
	int prid;	// player id in room
	int pgid;	// global player id
	int role;	// 0 ghost, 1 human
	int x, y;	// coordinate

	int blood;
	int extra_blood;

	int distance;
	int movement;

    int is_alive;
	int used;
} PlayerSlot;

typedef struct{
    int x, y;
    int prid;
    int role;
    int bullet;
    int shield;
} MapGrid;

typedef struct{
	int rid;
	int status;	// 0 waiting, 1 running
	int player_cnt;
	PlayerSlot players[MAXPLAYER];
	MapGrid* map[MAP_H][MAP_W];
	
	int bullet_cnt;
} Room;

Room rooms[MAXROOM];
int global_player_id = 1;


int set_nonblock(int fd){
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void broadcast_room(Room *rm, char *msg) {
    for (int i = 0; i < MAXPLAYER; i++) {
        if (!rm->players[i].used) continue;
        int fd = rm->players[i].fd;
        if (fd >= 0) {
            Write(fd, msg, strlen(msg));
        }
    }
}

void init_rooms() {
    for (int r = 0; r < MAXROOM; r++) {
        Room *rm = &rooms[r];
		rm -> rid = r+1;
        rm -> status = 0;
        rm -> player_cnt = 0;
        for (int i = 0; i < MAXPLAYER; i++) {
            rm -> players[i].fd = -1;
            rm -> players[i].used = 0;
        }
        for (int y = 0; y < MAP_H; y++) {
            for (int x = 0; x < MAP_W; x++) rm -> map[y][x] = NULL;
        }
		rm -> bullet_cnt = 50;
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
            snprintf(ev, sizeof(ev), "MOVE %d %d %d\n", ps->prid, ps->x, ps->y);
            broadcast_room(rm, ev);
        }
    } 
	else if (strncmp(cmd, "SHOOT ",6)==0) {
        char d = cmd[6];
        int tx = ps->x, ty = ps->y;
        if (d=='U') ty--;
        else if (d=='D') ty++;
        else if (d=='L') tx--;
        else if (d=='R') tx++;
        if (tx>=0 && tx<MAP_W && ty>=0 && ty<MAP_H) {
            int hit_id = -1;
            for (int i=0;i<MAXPLAYER;i++) {
                if (!rm->players[i].used) continue;
                PlayerSlot *other = &rm->players[i];
                if (other->x==tx && other->y==ty && other->blood>0) {
                    other->blood -= 1;
                    hit_id = other->prid;
                    char ev[128];
                    snprintf(ev, sizeof(ev), "HIT %d %d %d\n", ps->prid, other->prid, other->blood);
                    broadcast_room(rm, ev);
                    break;
                }
            }
            if (hit_id==-1) {
                char ev[128];
                snprintf(ev, sizeof(ev), "BULLET %d %c %d %d\n", ps->prid, d, tx, ty);
                broadcast_room(rm, ev);
            }
        }
    } 
	else if (strncmp(cmd, "QUIT",4)==0) {
        ps->used = 0;
        close(ps->fd);
        ps->fd = -1;
        rm->player_cnt--;
        char ev[128];
        snprintf(ev, sizeof(ev), "PLAYER_LEFT %d\n", ps->prid);
        broadcast_room(rm, ev);
    }
}

void update_state(int tick, Room* rm){
    char buf[MAXLINE];
    int off = 0;
    off += snprintf(buf+off, sizeof(buf)-off, "STATE %d %d\n", tick, rm->player_cnt);
    for (int i=0;i<MAXPLAYER;i++) {
        if (!rm->players[i].used) continue;
        PlayerSlot *ps = &rm->players[i];
        off += snprintf(buf+off, sizeof(buf)-off, "P %d %d %d %d %d\n",
                        ps->prid, ps->role, ps->x, ps->y, ps->blood);
    }
    broadcast_room(rm, buf);
}

void *game_loop(void *arg) {

    Room *rm = (Room*)arg;
    int tick = 0;
    const int TICK_MS = 200;

    for(int i=0 ; i<MAXPLAYER; i++){
		rm->players[i].x = rand() % MAP_W;
		rm->players[i].y = rand() % MAP_H;
		// 還沒想好如果重疊怎麼辦
	}

    for( ; ; ){
        
        if (rm->status != 1) break;

        for (int i = 0;i < MAXPLAYER; i++) {
            if (!rm->players[i].used) continue;
            int fd = rm->players[i].fd;
            char buf[MAXLINE];
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
                rm->player_cnt--;
            }
        }

        tick++;
        update_state(tick, rm);
        usleep(TICK_MS * 1000);
    }
    return NULL;
}

void start_room(Room *rm) {
    rm->status = 1;
	printf("Starting room %d:\n", rm->rid);

    char msg[256];
    snprintf(msg, sizeof(msg), "GAME_START room=%d\n", rm->rid);
    broadcast_room(rm, msg);

    pthread_t tid;
    if (pthread_create(&tid, NULL, game_loop, rm) != 0) {
        err_sys("pthread create error");
    } else {
        pthread_detach(tid); // thread 結束後，系統自行回收資源
    }
}

void assign_client_to_room(int clientfd) {
    for (int r = 0; r < MAXROOM; r++) {
        Room *rm = &rooms[r];
       
        if (rm->status == 0 && rm->player_cnt < MAXPLAYER) { //status: 0 waiting, 1 running
            
			int idx = rm->player_cnt;
            rm->players[idx].used = 1;
            rm->players[idx].is_alive = 1;
            rm->players[idx].fd = clientfd;
            rm->players[idx].prid = idx+1;
            rm->players[idx].pgid = global_player_id++;
            rm->players[idx].role = (idx==0 || idx==1) ? 0 : 1; // 前兩個是鬼
            rm->players[idx].x = 0; 
			rm->players[idx].y = 0; 
			rm->players[idx].blood = 2;
			rm->players[idx].extra_blood = 1;
            rm->players[idx].distance = 1;
            rm->players[idx].movement = -1;
            rm->player_cnt++;

            // 個別針對 clientfd 送歡迎資訊
            char welcome[256];
            snprintf(welcome, sizeof(welcome),
                "WELCOME! Your information: Room %d, ID %d, Team %s. Waiting for others to join...\n", // 這些資訊可以簡單， client 那邊可以進一步拆解、重組
				rm->rid, rm->players[idx].prid, (rm->players[idx].role==0)?"ghost":"human");
			printf("%s", welcome);
            Write(clientfd, welcome, strlen(welcome));

            // 廣播有新成員加入
            char joinmsg[128];
            snprintf(joinmsg, sizeof(joinmsg), "New player %d just joined! Now we have %d members.\n", rm->players[idx].prid, rm->player_cnt); // 這些資訊可以簡單， client 那邊可以進一步拆解、重組
            broadcast_room(rm, joinmsg);

            if (rm->player_cnt == MAXPLAYER) {
                start_room(rm);
            }
           
            return;
        }
      
    }
    char *busy = "SERVER_FULL\n";
    Write(clientfd, busy, strlen(busy));
    close(clientfd);
}



int main(int argc, char **argv){

	printf("server start!\n");

	int					listenfd, connfd;
	socklen_t			clilen;
	struct sockaddr_in	cliaddr, servaddr;
	

	init_rooms();

	listenfd = Socket(AF_INET, SOCK_STREAM, 0);
	int opt = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(SERV_PORT);

	Bind(listenfd, (SA *) &servaddr, sizeof(servaddr));
	Listen(listenfd, LISTENQ);
	printf("Multi-room server listening on %d\n", SERV_PORT);

	for ( ; ; ) {
		clilen = sizeof(cliaddr);
		if ( (connfd = accept(listenfd, (SA *) &cliaddr, &clilen)) < 0) {
			if (errno == EINTR) continue;		
			else err_sys("accept error");
		}

		set_nonblock(connfd);
		printf("Accept connfd: %d\n", connfd);
		assign_client_to_room(connfd);
	}

	close(listenfd);
	return 0;
}

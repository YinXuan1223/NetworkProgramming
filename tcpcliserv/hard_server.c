#include	"unp.h"

#define     MAXROOM 10
#define		MAXPLAYER 3
#define     MAP_W 20
#define     MAP_H 12
#define     RELOGIN_Q_SIZE 1024


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
    int is_ingame;

} PlayerSlot;

typedef struct{
    int has_init_player;
    int shield;
	int shield_timer;
} MapGrid;

typedef struct{
	int rid;
	int status;	// 0 waiting, 1 running
	int player_cnt;
	PlayerSlot players[MAXPLAYER];
	MapGrid map[MAP_H][MAP_W];
	
	int bullet_cnt;
	int global_timer;
} Room;

Room rooms[MAXROOM];
int global_player_id = 1;

int relogin_q[RELOGIN_Q_SIZE];
int rq_head = 0, rq_tail = 0;
pthread_mutex_t rq_mutex = PTHREAD_MUTEX_INITIALIZER;

void enqueue_relogin(int fd) {
    pthread_mutex_lock(&rq_mutex);
    if((rq_tail+1)%RELOGIN_Q_SIZE!=rq_head){
        relogin_q[rq_tail] = fd;
        rq_tail = (rq_tail + 1) % RELOGIN_Q_SIZE;
    }
    pthread_mutex_unlock(&rq_mutex);
}

int dequeue_relogin(void) {
    pthread_mutex_lock(&rq_mutex);
    if (rq_head == rq_tail) {
        pthread_mutex_unlock(&rq_mutex);
        return -1; // empty
    }
    int fd = relogin_q[rq_head];
    rq_head = (rq_head + 1) % RELOGIN_Q_SIZE;
    pthread_mutex_unlock(&rq_mutex);
    return fd;
}

int set_nonblock(int fd){
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void broadcast_room(Room *rm, char *msg) {
    for (int i = 0; i < MAXPLAYER; i++) {
        if (!rm->players[i].is_alive) continue;
        int fd = rm->players[i].fd;
        if (fd >= 0) {
            Write(fd, msg, strlen(msg));
        }
    }
}

void init_room(int rid) {
    
    Room *rm = &rooms[rid];
    rm -> rid = rid+1;
    rm -> status = 0;
    rm -> player_cnt = 0;
    for (int i = 0; i < MAXPLAYER; i++) {
        rm -> players[i].fd = -1;
        rm -> players[i].is_alive = 0;
        rm -> players[i].is_ingame = 0;
    }
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++){
            rm -> map[y][x].shield = 0;
            rm -> map[y][x].shield_timer = 0;
            rm -> map[y][x].has_init_player = 0;
        } 
    }
    rm -> bullet_cnt = 50;
    rm -> global_timer = 0; // 5 minutes

}

void handle_move(char* cmd, Room *rm, PlayerSlot *ps){
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

void shooting(Room *rm, int tx, int ty, PlayerSlot *ps, int is_first){
    if (tx>=0 && tx<MAP_W && ty>=0 && ty<MAP_H) {
        if(is_first) rm->bullet_cnt--;

        int hit_id = -1;
        for (int i=0;i<MAXPLAYER;i++) {
            if (!rm->players[i].is_alive) continue;
            PlayerSlot *other = &rm->players[i];
            if (other->x==tx && other->y==ty && other->blood>0 && other->role == 1 && !rm->map[ty][tx].shield) {
                other->blood -= 1;
                hit_id = other->prid;
                
                char ev[128];
                if(other->blood==0) snprintf(ev, sizeof(ev), "DIE %d %d %d %d %d\n", ps->prid, other->prid, tx, ty, rm->bullet_cnt); // 格式 : 發射的人, 被射中的人, 最後子彈x, 最後子彈y, 剩餘子彈 (有死)
                else snprintf(ev, sizeof(ev), "HIT %d %d %d %d %d\n", ps->prid, other->prid, tx, ty, rm->bullet_cnt); // 格式 : 發射的人, 被射中的人, 最後子彈x, 最後子彈y, 剩餘子彈 (沒死)
                broadcast_room(rm, ev);
                break;
            }
        }
        if (hit_id==-1) {
            char ev[128];
            if(rm->map[ty][tx].shield) snprintf(ev, sizeof(ev), "SLDBULLET %d %d %d %d %d\n", ps->prid, -100, tx, ty, rm->bullet_cnt); // 格式 : 發射的人, 被射中的人(沒人被射中 -100), 最後子彈x, 最後子彈y, 剩餘子彈
            else snprintf(ev, sizeof(ev), "BULLET %d %d %d %d %d\n", ps->prid, -100, tx, ty, rm->bullet_cnt); // 格式 : 發射的人, 被射中的人(沒人被射中 -100), 最後子彈x, 最後子彈y, 剩餘子彈
			broadcast_room(rm, ev);
        }

    }
}

void handle_shoot(char* cmd, Room *rm, PlayerSlot *ps){

    char d = cmd[6]; // SHOOT U 2
    int distant = cmd[8]; 
    int x = ps->x, y = ps->y, tx, ty, ttx, tty;
    if (d=='U') { ty = y-1; tty = y-2; tx = x, ttx =x; }
    else if (d=='D') { ty = y+1; tty = y+2; tx = x, ttx =x; }
    else if (d=='L') { ty = y; tty = y; tx = x-1, ttx =x-2; }
    else if (d=='R') { ty = y; tty = y; tx = x+1, ttx =x+2; }
    
    shooting(rm, tx, ty, ps, 1);
    if(d=='1') return;
    shooting(rm, ttx, tty, ps, 0);

}

void handle_shield(char* cmd, Room *rm, PlayerSlot *ps){

	int sld_x = ps -> x;
	int sld_y = ps -> y;
	rm -> map[sld_y][sld_x].shield = 1;
	rm -> map[sld_y][sld_x].shield_timer = 50;
	char ev[128];
	sprintf(ev, "SHIELD %d %d %d", ps->prid, sld_x, sld_y);
	broadcast_room(rm, ev);

}

void handle_invite(char* cmd, Room *rm, PlayerSlot *ps){

}

void handle_agree(char* cmd, Room *rm, PlayerSlot *ps){
    
}


void handle_player_cmd(Room *rm, PlayerSlot *ps, const char *line) {

    char cmd[128];
    strncpy(cmd, line, sizeof(cmd)-1);
    cmd[sizeof(cmd)-1]=0;
    
    if (strncmp(cmd, "MOVE ",5)==0) { //cmd 要長 "MOVE (方向)" 
        handle_move(cmd, rm, ps);
    } 
	else if (strncmp(cmd, "SHOOT ",6)==0) { //cmd 要長 "SHOOT (方向) (距離)" (目前都還是1)
        handle_shoot(cmd, rm, ps);
    } 
	else if (strncmp(cmd, "SHIELD ",7)==0) {
		handle_shield(cmd, rm, ps);
	}
    else if (strncmp(cmd, "INVITE ", 7)==0){ // cmd 要長 "INVITE (邀請人) invites (被邀請人)"
        handle_invite(cmd, rm, ps);
    }
    else if (strncmp(cmd, "AGREE ", 6)==0){ // cmd 要長 "AGREE (被邀請人) agrees (邀請人)"
        handle_agree(cmd, rm, ps);
    }
	else if (strncmp(cmd, "QUIT",4)==0) {
        ps->is_alive = 0;
        ps->is_ingame = 0;
        ps->fd = -1;
        rm->player_cnt--;
        char ev[128];
        snprintf(ev, sizeof(ev), "PLAYER_LEFT %d\n", ps->prid);
        broadcast_room(rm, ev);
        close(ps->fd);
    }
}

void update_state(int tick, Room* rm){
    char buf[MAXLINE];
    int off = 0;
    off += snprintf(buf+off, sizeof(buf)-off, "STATE %d %d\n", tick, rm->player_cnt);
    for (int i=0;i<MAXPLAYER;i++) {
        if (!rm->players[i].is_alive) continue;
        PlayerSlot *ps = &rm->players[i];
        off += snprintf(buf+off, sizeof(buf)-off, "P %d %d %d %d %d\n",
                        ps->prid, ps->role, ps->x, ps->y, ps->blood);
    }
    broadcast_room(rm, buf);
}

int check_game_state(Room* rm){ 

    char ev[128];
    int human_cnt = 0;
    for(int i=0 ; i<MAXPLAYER ; i++){
        if(rm->players[i].role==1 && rm->players[i].is_alive) human_cnt++;
    }

    if(rm->global_timer>=1500 && human_cnt){ 
        sprintf(ev, "Game Over! Human Win!\n");
        broadcast_room(rm, ev);
        return 1;
    }

    if(human_cnt==0 || rm->bullet_cnt==0){
        if(human_cnt==0) sprintf(ev, "Game Over! Ghosts Win!\n");
        else if(rm->bullet_cnt==0) sprintf(ev, "Game Over! Human Win!\n");
        broadcast_room(rm, ev);
        return 1;
    }
    return 0;
}

void *game_loop(void *arg) {

    Room *rm = (Room*)arg;
   
    for(int i=0 ; i<MAXPLAYER; i++){ // random 出每個人的起始位置

        rm->players[i].x = rand() % MAP_W;
		rm->players[i].y = rand() % MAP_H;
		
        while (rm->map[rm->players[i].y][rm->players[i].x].has_init_player == 1){ // 處理 random 出重疊位置的情況
            rm->players[i].x = rand() % MAP_W;
		    rm->players[i].y = rand() % MAP_H;
        }
        rm->map[rm->players[i].y][rm->players[i].x].has_init_player = 1;
        
	}

    fd_set readfds;
    struct timeval tv;
    int maxfd = 0;

    while (rm->status == 1) {
        FD_ZERO(&readfds);

        for (int i = 0; i < MAXPLAYER; i++) {
            if (!rm->players[i].is_alive) continue;
            FD_SET(rm->players[i].fd, &readfds);
            if (rm->players[i].fd > maxfd)
                maxfd = rm->players[i].fd;
        }

        tv.tv_sec = 0;
        tv.tv_usec = 200 * 1000; // 等同 TICK_MS = 200ms

        // 等待可讀事件或 timeout
        int ready = select(maxfd + 1, &readfds, NULL, NULL, &tv);

        if (ready < 0) {
            perror("select error");
            break;
        }

        // 處理有資料的玩家
        for (int i = 0; i < MAXPLAYER; i++) {
            if (!rm->players[i].is_alive) continue;
            int fd = rm->players[i].fd;
            if (FD_ISSET(fd, &readfds)) {
                char buf[MAXLINE];
                int n = recv(fd, buf, sizeof(buf) - 1, 0);
                if (n > 0) {
                    buf[n] = 0;
                    char *p = strtok(buf, "\n");
                    while (p) {
                        handle_player_cmd(rm, &rm->players[i], p);
                        if(check_game_state(rm)){
                            rm->status = 2;
                            break;
                        }
                        p = strtok(NULL, "\n");
                    }
                } else if (n == 0) {
                    rm->players[i].is_alive = 0;
                    close(fd);
                    rm->players[i].fd = -1;
                    rm->player_cnt--;
                }
            }
        }

        for(int i=0; i<MAP_H ; i++){
            for(int j=0; j<MAP_W ; j++){
                if(rm->map[i][j].shield) {
                    rm->map[i][j].shield_timer--;
                    if(rm->map[i][j].shield_timer==0) rm->map[i][j].shield=0;
                }
            }
        }


        if(rm->global_timer==1500){
            check_game_state(rm);
            rm->status = 2;
        }

        if(rm->status == 2) break;
        rm->global_timer++;
        update_state(rm->global_timer, rm);

    }

    while(rm->player_cnt){ // 處理要不要繼續遊戲

        char buf[256];
        for (int i = 0; i < MAXPLAYER; i++) {
            if (!rm->players[i].is_ingame) continue;

            int fd = rm->players[i].fd;
            int n = recv(fd, buf, sizeof(buf)-1, MSG_DONTWAIT);
            if (n > 0) {
                buf[n] = 0;
                if (strstr(buf, "STAY")) {
                    enqueue_relogin(rm->players[i].fd);
                } 
                else if (strstr(buf, "LEAVE")) {
                    close(rm->players[i].fd);
                } 
                else continue;
                
            } 
            else if (n == 0) {
                close(rm->players[i].fd);
            }
            rm->player_cnt--;
        }
    }
   
    init_room(rm->rid);
    
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
            rm->players[idx].is_alive = 1;
            rm->players[idx].is_ingame = 1;
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

	printf("hehe handle move server start!\n");

	int					listenfd, connfd;
	socklen_t			clilen;
	struct sockaddr_in	cliaddr, servaddr;
	

	for(int i=0 ; i<MAXROOM ; i++) init_room(i);

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

        connfd = dequeue_relogin();
        if(connfd >= 0){
            assign_client_to_room(connfd);
            continue;
        }


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

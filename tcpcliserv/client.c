#include "unp.h"

#include <ncurses.h>

#define MAP_W 20
#define MAP_H 12

int sockfd;
int game_started = 0;

char board[MAP_H][MAP_W+1];

int my_id;
char my_team[16] = "Unknown";
int my_hp = 2;
int my_bullets = 50;
int shield = 1; //1:can use 0:can't use
int blood_bag = 1;
int gun = 1; //1:can use 0:can't use
int combine = 0; //0:can't 1:can 2:combining

int bullet_x = -1, bullet_y = -1;
int bullet_timer = 0;

struct Player
{
    int live; //0:dead 1:live
    int team; //0:human 1:ghost
    char id;
    int x, y;
    int hp;
    int blood_bag;
    int shield; //1:can use 0:can't use
    int gun; //1:can use 0:can't use
    int combine; //0:can't 1:can 2:combining
    int control; //1:control combine players
};

struct Player players[7];

WINDOW *mapwin;

char id_to_char(int id){
    char c;
    switch (id)
    {
        case 1:
            c = 'A';
            break;
        case 2:
            c = 'B';
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

void init_board()
{
    for (int y = 0; y < MAP_H; y++)
    {
        for (int x = 0; x < MAP_W; x++)
        {
            board[y][x] = '.';
        }
        board[y][MAP_W] = '\0';
    }
}

void draw_board() 
{
    for (int y = 0; y < MAP_H; y++) 
    {
        for (int x = 0; x < MAP_W; x++) 
        {
            char ch = board[y][x];
            if (x == bullet_x && y == bullet_y && (x != players[1].x || y != players[1].y) && (x != players[2].x || y != players[2].y)) ch = '*';

            mvaddch(y + 2, x, ch);
        }
    }
}

void draw_waiting_screen()
{
    mvprintw(0, 0, "Waiting for other players...");
    init_board();
    draw_board();

    int start_y = 2 + MAP_H + 1;
    if (strncmp(my_team, "human", 5) == 0)
    {
        mvprintw(start_y, 0, "TIME: Waiting...      HP: 2");
        mvprintw(start_y + 1, 0, "TEAM: Human           Blood Bag: 1");
        mvprintw(start_y + 2, 0, "ID: %c                 Shield Usability: Yes", id_to_char(my_id));
    }
    else if (strncmp(my_team, "ghost", 5) == 0)
    {
        mvprintw(start_y, 0, "TIME: Waiting...      Bullet: 50");
        mvprintw(start_y + 1, 0, "TEAM: Ghost           Combinable: No");
        mvprintw(start_y + 2, 0, "ID: %c                 Gun Usability: Yes", id_to_char(my_id));
    }
    mvprintw(start_y + 4, 0, "Press 'q' to quit.");

    refresh();
}

void draw_private_status()
{
    int start_y = 2 + MAP_H + 1;
    if (strncmp(my_team, "human", 5) == 0)
    {
        mvprintw(start_y, 0, "TIME: Waiting...      HP: %d", my_hp);
        mvprintw(start_y + 1, 0, "TEAM: Human           Blood Bag: %d", blood_bag);
        mvprintw(start_y + 2, 0, "ID: %c                 Shield Usability: %s", id_to_char(my_id), (shield == 1 ? "Yes" : "No "));
    }
    else if (strncmp(my_team, "ghost", 5) == 0)
    {
        mvprintw(start_y, 0, "TIME: Waiting...      Bullet: %d ", my_bullets);
        mvprintw(start_y + 1, 0, "TEAM: Ghost           Combinable: %s", (combine == 2 ? "Yes" : "No "));
        mvprintw(start_y + 2, 0, "ID: %c                 Gun Usability: %s", id_to_char(my_id), (gun == 1 ? "Yes" : "No "));
    }
    mvprintw(start_y + 4, 0, "Press 'q' to quit.");
}

void start_set()
{
    for (int i = 1; i <= 6; i++)
    {
        players[i].live = 1;
        players[i].id = id_to_char(i);
        if (i >= 3)
        {
            players[i].hp = 2;
            players[i].blood_bag = 1;
            players[i].shield = 1;
        }
        else
        {
            players[i].gun = 1;
            players[i].control = 0;
            players[i].combine = 0;
        }
    }
}

void draw_start_screen()
{
    int start_x = MAP_W + 3;

    mvprintw(2, start_x, "Ghost");
    mvprintw(3, start_x, "Bullet: 50    Combining: No");
    mvprintw(4, start_x, "A G");
    mvprintw(5, start_x, "B G");

    mvprintw(7, start_x, "Human");
    mvprintw(8, start_x, "1 H H B S");
    mvprintw(9, start_x, "2 H H B S");
    mvprintw(10, start_x, "3 H H B S");
    mvprintw(11, start_x, "4 H H B S");
}

void draw_public_status()
{
    int start_x = MAP_W + 3;

    mvprintw(2, start_x, "Ghost");
    mvprintw(3, start_x, "Bullet: %d    Combining: %s", my_bullets, (combine == 2 ? "Yes" : "No "));

    for (int i = 1; i <= 2; i++)
    {
        if (players[i].live == 1) mvprintw(3 + i, start_x, "%c %s", players[i].id, (players[i].gun == 1 ? "G" : " "));
        else mvprintw(3 + i, start_x, "%c DEAD        ", players[i].id);
    }

    mvprintw(7, start_x, "Human");
    for (int i = 3; i <= 6; i++)
    {
        char hp_buf[10];

        if (players[i].hp == 2) strcpy(hp_buf, "H H");
        else if (players[i].hp == 1) strcpy(hp_buf, "H  ");

        char bag_buf[2];
        if (players[i].blood_bag == 1) strcpy(bag_buf, "B");
        else strcpy(bag_buf, " ");

        char shield_buf[2];
        if (players[i].shield == 1) strcpy(shield_buf, "S");
        else strcpy(shield_buf, " ");

        if (players[i].live == 1 && players[i].hp != 0) mvprintw(5 + i, start_x, "%c %s %s %s", players[i].id, hp_buf, bag_buf, shield_buf);
        else mvprintw(5 + i, start_x, "%c DEAD        ", players[i].id);
    }
}

void draw_game_screen()
{
    mvprintw(0, 0, "Game Running...             ");
    draw_board();
    draw_public_status();
    draw_private_status();
    refresh();
}

/* process a complete received line */
void process_line(char *line) {
    if (strncmp(line, "WELCOME", 7) == 0) 
    {   
        int room, id;
        char team[16];
        sscanf(line, "WELCOME! Your information: Room %d, ID %d, Team %s. Waiting for others to join...\n", &room, &id, team);
        
        my_id = id;
        strcpy(my_team, team);

        players[id].live = 1;
        players[id].id = id_to_char(id);
        if (strncmp(team, "human", 5) == 0)
        {
            players[id].team = 0;
            players[id].hp = 2;
            players[id].blood_bag = 1;
            players[id].shield = 1;
        }
        else if (strncmp(team, "ghost", 5) == 0)
        {
            players[id].team = 1;
            players[id].live = 1;
            players[id].gun = 1;
            players[id].combine = 0;
            players[id].control = 0;
        }

        draw_waiting_screen();
    }
    else if (strncmp(line, "GAME_START", 10) == 0) 
    {
        start_set();
        draw_game_screen();
        draw_start_screen();
        game_started = 1;
    }
    else if (strncmp(line, "PLAYER_LEFT", 11) == 0) {
        
    }
    else if (strncmp(line, "STATE", 5) == 0) 
    {
        init_board();
        if (bullet_timer > 0) 
        {
            bullet_timer--;
            if (bullet_timer == 0) 
            {
                bullet_x = -1;
                bullet_y = -1;
            }
        }
    }
    else if (line[0] == 'P') 
    {
        int id, role, x, y, blood;
        sscanf(line, "P %d %d %d %d %d", &id, &role, &x, &y, &blood);

        players[id].id = id_to_char(id);
        players[id].team = role;
        if (id == my_id) my_hp = blood;
        if (blood > 0) players[id].live = 1;
        players[id].x = x;
        players[id].y = y;
        players[id].hp = blood;

        if (blood > 0) board[y][x] = id_to_char(id);
        draw_game_screen();
    }
    else if (strncmp(line, "BULLET", 6) == 0) 
    {
        int shooter, hit, x, y, remain;
        sscanf(line, "BULLET %d %d %d %d %d", &shooter, &hit, &x, &y, &remain);

        bullet_x = x;
        bullet_y = y;
        my_bullets = remain;
        bullet_timer = 3;
        draw_game_screen();
    }
    else if (strncmp(line, "HIT", 3)==0) 
    {
        int shooter, hit, x, y, remain;
        sscanf(line, "HIT %d %d %d %d %d", &shooter, &hit, &x, &y, &remain);

        my_bullets = remain;

        for (int t = 0; t < 3; t++) 
        {
            standend(); 
            mvaddch(players[hit].y + 2, players[hit].x, ' ');
            refresh();
            usleep(80000);
            standend(); 

            if (players[hit].live) mvaddch(players[hit].y + 2, players[hit].x, players[hit].id);
            refresh();
            usleep(80000);
        }

        draw_game_screen();
    }
    else if (strncmp(line, "DIE", 3) == 0)
    {
        int shooter, hit, x, y, remain;
        sscanf(line, "DIE %d %d %d %d %d", &shooter, &hit, &x, &y, &remain);

        players[hit].live = 0;
        players[hit].hp = 0;
        players[hit].shield = 0;
        players[hit].blood_bag = 0;
        players[hit].combine = 0;
        players[hit].control = 0;
        players[hit].gun = 0;

        if (hit == my_id)
        {
            my_hp = 0;
            shield = 0;
            blood_bag = 0;
            combine = 0;
            gun = 0;
        }

        my_bullets = remain;
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
    while (1) 
    {
        int ch = getch();
        if (ch == 'q') {
            send(sockfd, "QUIT\n", 5, 0);
            endwin();
            exit(0);
        }
        if (!game_started) continue;


        int x = players[my_id].x;
        int y = players[my_id].y;

        if (ch == 'w') y--;
        if (ch == 's') y++;
        if (ch == 'a') x--;
        if (ch == 'd') x++;

        if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;

        int block = 0;
        for (int i = 1; i <= 6; i++) 
        {
            if (i == my_id) continue;
            if (!players[i].live) continue;
            if (players[i].x == x && players[i].y == y) 
            {
                block = 1;
                break;
            }
        }

        if (block) continue;

        if (ch=='w') send(sockfd, "MOVE U\n", 7, 0);
        if (ch=='s') send(sockfd, "MOVE D\n", 7, 0);
        if (ch=='a') send(sockfd, "MOVE L\n", 7, 0);
        if (ch=='d') send(sockfd, "MOVE R\n", 7, 0);

        if (my_id == 1 || my_id == 2)
        {
            if (ch=='i') send(sockfd, "SHOOT U\n", 8, 0);
            if (ch=='k') send(sockfd, "SHOOT D\n", 8, 0);
            if (ch=='j') send(sockfd, "SHOOT L\n", 8, 0);
            if (ch=='l') send(sockfd, "SHOOT R\n", 8, 0);
        }
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

    pthread_t t1, t2;
    pthread_create(&t1, NULL, recv_thread, NULL);
    pthread_create(&t2, NULL, input_thread, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    endwin();
    return 0;
}

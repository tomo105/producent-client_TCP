#define _GNU_SOURCE

#include <fcntl.h>


#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <poll.h>
#include <netdb.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <strings.h>

#include <sys/stat.h>

//------------------------------------------------------------------------------------ringbuffer
typedef struct {
    unsigned int pHead;
    unsigned int pTail;
    unsigned int MaxSize;
    unsigned int CurrentSize;
    char **tab;
} RingBuffer;

RingBuffer *Create(int size);

int empty(RingBuffer *q);

unsigned int size(RingBuffer *q);

unsigned int MaxSize(RingBuffer *q);

void push(RingBuffer *q, char *x);

char *pop(RingBuffer *q);

//------------------------------------------------------------------------------------KOLEJKA KOMUNIKATOW
typedef struct {
    unsigned int pHead;
    unsigned int pTail;
    unsigned int MaxSize;
    unsigned int CurrentSize;
    int *tab;
} Queue;

Queue *qCreate(int size);

int qEmpty(Queue *q);

unsigned int qSize(Queue *q);

unsigned int qMaxSize(Queue *q);

void qPush(Queue *q, int x);

int qPop(Queue *q);

int isClient(Queue *q, int fd_client);


#define BLOCK_SIZE 640
#define RING_SIZE 2048       //rozmiar RingBuffer 1.25MB czyli 640*2024
#define MAX_KOM  100
#define STORAGE_MAXSIZE 1310720
#define SEND_DATA_SIZE 114688
#define TAKE_DATA_SIZE 115200
#define COUNT_BLOCKS 180


float parametr_czas(char *arg);

int parametr_address_port(char *arg, char **addr, int *port);

int hostname_to_ip(char *host, char *ip);

void fill_pollfd(struct pollfd *fdd);

void add_fd_to_pollfd(struct pollfd *fdd, int fd, int index);

int make_timerfd(float czas);

void change_char(char *c);

void write_five_sec_raport(int clients, int magazine_size, int *potok, struct timespec raport_time,
                           struct timespec raport_time_2, int file);

void write_connect_raport(char *ip, int port, struct timespec raport_time, struct timespec raport_time_2, int file);

void write_disconnect_raport(char *ip, int port, int *ilosc_blokow, struct timespec raport_time,
                             struct timespec raport_time_2, int file);

//---------------------------------------------------------------------MAIN-----------------------------------
int main(int argc, char *argv[]) {
    int opt;
    float czas;
    char *path, *addr = "localhost";
    int port_nr, r_flag = 0, t_flag = 0;
//---------------------------------------------------------CZYTANIE DANYCH--------------------------------------------------
    while ((opt = getopt(argc, argv, "t:r:")) != -1) {
        switch (opt) {
            case 'r':
                r_flag = 1;
                path = optarg;
                break;
            case 't':
                t_flag = 1;
                czas = parametr_czas(optarg);
                break;
            default:
                fprintf(stderr,
                        "Usage: %s: obligatory  -r <path> obligatory -t <time>  float (optional)<addr> and obiligatory port_nr   \n",
                        argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if ((r_flag + t_flag != 2) || (parametr_address_port(argv[argc - 1], &addr, &port_nr) < 0)) {
        printf("Usage:  %s  (obligatory)  -r  <path> obligatory -t <float> obligatory  and <addr> (optional) port(obligatory \n ",
               argv[0]);
        return -1;
    }
//---------------------------------------------SOCKET----------------------------------
    int socket_fd, addrlen;
    struct sockaddr_in address;
    char ip[100];

    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    hostname_to_ip(addr, ip);
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_nr);
    addrlen = sizeof(address);
    if (bind(socket_fd, (struct sockaddr *) &address, addrlen) < 0) {
        perror("bind error");
        exit(EXIT_FAILURE);
    }

    if (listen(socket_fd, 32) < 0)//32 to max wartosc
    {
        perror("listen error");
        exit(EXIT_FAILURE);
    }

    struct pollfd fds[MAX_KOM];
    fill_pollfd(fds);
    add_fd_to_pollfd(fds, socket_fd, 0);   //na 0 pozycji socket_fd

//-----------------------------------------------------------------------------------TIMER-------No.1----and ------No.2-------------------------------------------
    int timer_fd1, timer_fd2, file;

    timer_fd1 = make_timerfd(czas);//do magazynu
    add_fd_to_pollfd(fds, timer_fd1, 1);
    timer_fd2 = make_timerfd(5);//na raporty
    add_fd_to_pollfd(fds, timer_fd2, 2);
    //------------------------------------------------------------------------RAPORTY---------------------------------------------------------
    if ((file = open(path, O_WRONLY | O_APPEND | O_CREAT | O_TRUNC, 00666)) < 0) {
        perror("open error");
        return -2;
    }

    int porty[1024], ilosc_blokow[2048];
    int clients = 0, magazine_size = 0, potok = 0;
    struct timespec raport_time, raport_time1, raport_time2, raport_time3, raport_time4, raport_time5;
//-------------------------------------------------------------	PRZYGOTOWANIE--------------------------------------------------
    char *kawalek = (char *) malloc(128 * sizeof(char));
    RingBuffer *kju;
    Queue *kju_kom;
    kju = Create(RING_SIZE);
    kju_kom = qCreate(MAX_KOM);
    char znak = 'a';
    char rec[4];
    uint64_t tik = 0, tik2 = 0;
    int red, active_fd, new_socket;

//--------------------------------------------------------------------------------------POLL-----------------------------------------------------------------------------------------------
    while (1) {
        active_fd = poll(fds, MAX_KOM, -1);
        if (active_fd < 0) {
            perror("poll error");
            return -7;
        } else if (active_fd > 0) {
            for (int i = 0; i < MAX_KOM; i++)  //po kazdym desprytorze i cos z nim robimy
            {

                if (fds[i].fd > 0 &&
                    (fds[i].revents & POLLIN))//nowe polaczenie!!! accept nie powinien sie zablokowac !!!!
                {
                    if (fds[i].fd == socket_fd)//nowy klient do obslugi
                    {
                        if ((new_socket = accept(socket_fd, (struct sockaddr *) &address,
                                                 (socklen_t * ) & addrlen)) < 0) {
                            perror("accept error");
                            exit(EXIT_FAILURE);
                        }

                        clock_gettime(CLOCK_REALTIME, &raport_time);
                        clock_gettime(CLOCK_MONOTONIC, &raport_time1);
                        write_connect_raport(ip, ntohs(address.sin_port), raport_time, raport_time1, file);

                        for (int i = 0; i < MAX_KOM; i++)//wrzucanie do tablicy nowego deskryptora
                        {
                            if (fds[i].fd == -1) {
                                fds[i].fd = new_socket;
                                fds[i].events =
                                        POLLIN;  //tam czytam z bufora i wrzucam do kolejki komunikatow
                                porty[i] = ntohs(address.sin_port);//pamietam port
                                clients++;    //ilosc klientow
                                break;
                            }
                        }
                    } else if (fds[i].fd == timer_fd1)//timer 1 do robienia poczek po 640
                    {

                        if ((read(fds[i].fd, &tik, sizeof(uint64_t))) > 0) {
                            for (uint64_t k = 0; k < tik; k++) {
                                if ((kju->CurrentSize) < (kju->MaxSize))//jesli bufor nie pelny
                                {
                                    char *buf_send = (char *) malloc(sizeof(char) * BLOCK_SIZE);
                                    bzero(buf_send, BLOCK_SIZE);
                                    memset(buf_send, znak, BLOCK_SIZE);
                                    change_char(&znak);//printf("%s %ld\n",buf_send,strlen(buf_send));
                                    push(kju,
                                         buf_send);//printf("ile jest paczek w magazzynnie %d \n",kju->CurrentSize);
                                    magazine_size += BLOCK_SIZE;
                                    potok += BLOCK_SIZE;
                                }
                            }
                        }
                        if ((kju->CurrentSize >= COUNT_BLOCKS) &&
                            (kju_kom->CurrentSize > 0 ) )                                                                                                       //czy sa jakies komunikaty i dane do wysylki
                        {
                            ilosc_blokow[i]++;
                            char *buf_temp = (char *) malloc(SEND_DATA_SIZE * sizeof(char));
                            for (int j = 0; j < COUNT_BLOCKS - 1; j++)
                                strcat(buf_temp, pop(kju));

                            memcpy(kawalek, pop(kju), 128);
                            strcat(buf_temp, kawalek);
                            write(qPop(kju_kom), buf_temp, SEND_DATA_SIZE);
                            magazine_size -= TAKE_DATA_SIZE;
                            potok -= TAKE_DATA_SIZE;
                            if (fds[1].events == POLLOUT)//uaktywniam timer bo zrobilo sie miejsce
                                fds[1].events = POLLIN;
                        }
                    } else if (fds[i].fd == timer_fd2)//timer 2 do raportow co 5 sek
                    {
                        if ((read(fds[i].fd, &tik2, sizeof(uint64_t))) > 0) {
                            clock_gettime(CLOCK_REALTIME, &raport_time4);
                            clock_gettime(CLOCK_MONOTONIC, &raport_time5);
                            write_five_sec_raport(clients, magazine_size, &potok, raport_time4, raport_time5, file);
                        }
                    } else {
                        red = recv(fds[i].fd, rec, 4, 0);
                        if (red < 0) {
                            perror("read error");
                            return -3;
                        } else if (red == 0) {
                            if (!isClient(kju_kom,
                                          fds[i].fd)) {                                                 //czy bedzie jeszcze otrzymywal komunikat jak nie to koniec polaczenia !!
                                clock_gettime(CLOCK_REALTIME, &raport_time2);
                                clock_gettime(CLOCK_MONOTONIC, &raport_time3);
                                write_disconnect_raport(ip, porty[i], &ilosc_blokow[i], raport_time2, raport_time3, file);

                                clients--;
                                fds[i].fd = -1;
                                fds[i].events = POLLIN;//zwalniam deskryptor
                            }
                        } else if (red > 0) {//wpisuje do kolejki_komunikatow nie jest pelna bo to sprawdzam przy wejsciu do wyzej!!
                            qPush(kju_kom, fds[i].fd);
                            ilosc_blokow[i]++;
                            }

                    }
                }//end polli
            }//end fora po deskr
        }
    }//end while
    return 0;
}
//--------------------------------------------------------------------------------------------------------------


int hostname_to_ip(char *hostname, char *ip) {
    struct in_addr **list;
    struct hostent *host;

    if ((host = gethostbyname(hostname)) == NULL) {
        perror("gethostbyname error");
        return 0;

    }

    list = (struct in_addr **) host->h_addr_list;

    for (int i = 0; list[i] != NULL; i++) {
        strcpy(ip, inet_ntoa(*list[i]));
        return 0;
    }

    return 1;

}

float parametr_czas(char *arg) {
    float czas;
    char **ptr = NULL;
    czas = strtof(arg, ptr);
    if (czas == 0) {
        perror("strtof error");
        return -1;
    }
    if (czas > 8 || czas < 1) {
        printf("number from parametr -t  is out of range <1,8>\n");
        return -1;
    }
    czas *= 60 / 96.0 ;

    return czas;
}

int parametr_address_port(char *arg, char **addr, int *port) {
    char *tmp, *tmp2;
    char **ptr = NULL;
    tmp = strtok(arg, ":");
    tmp2 = strtok(NULL, ":");
    if (tmp2 == NULL)    // podano port, addr dsomyslny
    {

        *port = strtol(tmp, ptr, 0);

        if (*port == 0 || *port <= 8) {
            return -1;
        }
    } else            //podano addr:port
    {
        *addr = tmp;
        *port = strtol(tmp2, ptr, 0);
        if (*port == 0) {
            return -1;
        }
    }
    return 0;   //-1 jak blad
}

void fill_pollfd(struct pollfd *fdd) {
    for (int i = 3; i < MAX_KOM; i++)//bo 0 indeks rezerwuje dla acceptaa 1 i 2 dla timerow
    {
        fdd[i].fd = -1;
        fdd[i].events = POLLIN;
        fdd[i].revents = 0;
    }
}

void change_char(char *c) {
    if (*c == 'z')
        *c = 'A';
    else if (*c == 'Z')
        *c = 'a';
    else
        *c = *c + 1;
}

void add_fd_to_pollfd(struct pollfd *fdd, int fd, int index) {
    fdd[index].fd = fd;
    fdd[index].events = POLLIN;
    fdd[index].revents = 0;
}

int make_timerfd(float czas) {
    int timer;
    struct itimerspec time;
    time.it_value.tv_sec = (int) czas;
    time.it_value.tv_nsec = (czas - time.it_value.tv_sec) * 1000000000L;
    time.it_interval.tv_sec = time.it_value.tv_sec;
    time.it_interval.tv_nsec = time.it_value.tv_nsec;

    if ((timer = timerfd_create(CLOCK_REALTIME, 0)) < 0) {
        perror("timer1 error");
        return -2;
    }

    if (timerfd_settime(timer, 0, &time, NULL) == -1) {
        perror("timer settime");
        return -2;
    }
    return timer;
}

void write_five_sec_raport(int clients, int magazine_size, int *potok, struct timespec raport_time,
                           struct timespec raport_time_2, int file) {
    char *five = "Five seconds raport:\n";
    char output[100], output_czas[100], output_czas_2[100];
    snprintf(output, 100, "\tclients: %d Storage size %d %lf%% data flow %d \n", clients,
             magazine_size, (double) magazine_size * 100 / STORAGE_MAXSIZE, *potok);
    *potok = 0;
    snprintf(output_czas, 100, "\tCLOCK REALTIME: sec %ld nsec %ld\n", raport_time.tv_sec,
             raport_time.tv_nsec);
    snprintf(output_czas_2, 100, "\tCLOCK MONOTONIC: sec %ld nsec %ld\n", raport_time_2.tv_sec,
             raport_time_2.tv_nsec);

    write(file, five, strlen(five));//Connect:
    write(file, output, strlen(output));
    write(file, output_czas, strlen(output_czas));
    write(file, output_czas_2, strlen(output_czas_2));
}

void write_connect_raport(char *ip, int port, struct timespec raport_time, struct timespec raport_time_2, int file) {
    char *con = "Connect:\n";
    char output[100], output_czas[100], output_czas_2[100];
    snprintf(output, 100, "\taddress: %s port:  %d \n", ip, port);
    snprintf(output_czas, 100, "\tCLOCK REALTIME: sec %ld nsec %ld\n", raport_time.tv_sec,
             raport_time.tv_nsec);
    snprintf(output_czas_2, 100, "\tCLOCK MONOTONIC: sec %ld nsec %ld\n", raport_time_2.tv_sec,
             raport_time_2.tv_nsec);

    write(file, con, strlen(con));//Connect:
    write(file, output, strlen(output));
    write(file, output_czas, strlen(output_czas));
    write(file, output_czas_2, strlen(output_czas_2));
}

void write_disconnect_raport(char *ip, int port, int *ilosc_blokow, struct timespec raport_time,
                             struct timespec raport_time_2, int file) {
    char *dis = "Disconnect:\n";
    char output[100], output_czas[100], output_czas_2[100];
    snprintf(output, 100, "\tip address: %s  port: %d  sent:%d block(s)\n", ip, port,
             *ilosc_blokow);
    snprintf(output_czas, 100, "\tCLOCK REALTIME: sec %ld nsec %ld\n", raport_time.tv_sec,
             raport_time.tv_nsec);
    snprintf(output_czas_2, 100, "\tCLOCK MONOTONIC: sec %ld nsec %ld\n",
             raport_time_2.tv_sec, raport_time_2.tv_nsec);

    write(file, dis, strlen(dis));//Disconnect
    write(file, output, strlen(output));
    write(file, output_czas, strlen(output_czas));
    write(file, output_czas_2, strlen(output_czas_2));
    *ilosc_blokow = 0;
}

///----------------------------------------------------RINGBUFFER-----------------------------------------------
RingBuffer *Create(int size) {
    RingBuffer *p = (RingBuffer *) malloc(sizeof(RingBuffer));
    if (!p) {
        printf("Error");
        return NULL;
    }
    p->tab = (char **) malloc(size * sizeof(char *));
    if (!p->tab) {
        printf("error w creat");
        return NULL;
    }
    for (int i = 0; i < size; i++) {
        p->tab[i] = (char *) malloc(sizeof(char) * 640);
    }
    p->pHead = 0;
    p->pTail = 0;
    p->MaxSize = size;
    p->CurrentSize = 0;
    return p;
}


int Empty(RingBuffer *q) {
    return (q->pHead == q->pTail);
}

unsigned int size(RingBuffer *q) {
    return (q->pHead - q->pTail);
}

unsigned int MaxSize(RingBuffer *q) {
    return q->MaxSize;
}

void push(RingBuffer *q, char *x)  //dodaj
{
    unsigned int index = q->pHead % q->MaxSize;
    if (size(q) == MaxSize(q))
        q->pTail++;
    q->pHead++;

    q->CurrentSize++;
    q->tab[index] = x;

}

char *pop(RingBuffer *q) {
    unsigned int index = q->pTail % q->MaxSize;

    q->pTail++;
    q->CurrentSize--;

    return q->tab[index];

}

//---------------------------------------------------------KOLEJKA---KOMUNIKATOW---------------------------------------------------------------------------------------------------
Queue *qCreate(int size) {
    Queue *p = (Queue *) malloc(sizeof(Queue));
    if (!p) {
        printf("Error");
        return NULL;
    }
    p->tab = (int *) malloc(size * sizeof(int));
    if (!p->tab) {
        printf("error w creat");
        return NULL;
    }
    p->pHead = 0;
    p->pTail = 0;
    p->MaxSize = size;
    p->CurrentSize = 0;
    return p;
}


int qEmpty(Queue *q) {
    return (q->pHead == q->pTail);
}

unsigned int qSize(Queue *q) {
    return (q->pHead - q->pTail);
}

unsigned int qMaxSize(Queue *q) {
    return q->MaxSize;
}

//----------------------------------------------------------------------PUSH
void qPush(Queue *q, int x)  //dodaj
{
    unsigned int index = q->pHead % q->MaxSize;
    if (qSize(q) == qMaxSize(q))
        q->pTail++;
    q->pHead++;

    q->CurrentSize++;
    q->tab[index] = x;

}

//--------------------------------------------------------------POP
int qPop(Queue *q) {
    int tmp;
    unsigned int index = q->pTail % q->MaxSize;
    q->pTail++;
    q->CurrentSize--;
    tmp = q->tab[index];
    q->tab[index] = 0;
    return tmp;

}

int isClient(Queue *q, int fd_client) {
    for (int i = 0; i < MAX_KOM; i++) {
        if (q->tab[i] == fd_client)
            return 1;
    }
    return 0;

}

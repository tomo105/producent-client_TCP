#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <limits.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <sys/timerfd.h>
#include <poll.h>
#include <openssl/md5.h>

float losuj(float range, float offset);

int hostname_to_ip(char *hostname, char *ip);

int make_timerfd(float czas);

int parametr_address_port(char *arg, char **addr, int *port);

float time_parametr(char *arg);

void add_fd_to_pollfd(struct pollfd *fdd, int fd, int index);

void md(char *cos, char **sum);

void licz_czas(struct timespec start, struct timespec koniec, int *nsec, int *sec);

void raport(int pid, char * ip, int port, struct timespec raport_time, struct timespec raport_time_2 );

void  print_raport(int num, char*sum, int sec, int nsec, int sec_2, int nsec_2);

 #define READ_DATA_SIZE 114688

int main(int argc, char **argv) {

    int opt, ilosc_kom, port_nr;
    char **ptr = NULL;
    char *addr = "localhost";
    float r_time = 0, s_time = 0;
    int r_flag = 0, s_flag = 0, hash_flag = 0;
//-------------------------------------------------------------------------------------WCZYTYWANIE-----DANYCH------------------------------------------------------------		
    while ((opt = getopt(argc, argv, "#:r:s:")) != -1) {
        switch (opt) {
            case '#':
                        hash_flag = 1;
                        ilosc_kom = strtol(optarg, ptr, 0);
                        if (ilosc_kom == 0) {
                            perror("strtof error");
                            return -1;
                        }
                        break;
            case 'r':
                        r_flag = 1;
                        if ((r_time = time_parametr(optarg)) < 0) {
                            printf(" Usage:  %s  (obligatory)  -#  <intiger> obligatory {-r <float> / float1:float2 or -s <flaot>/ <float1>:f<loat2>  } <addr> (optional) port(obligatory)\n zakladam ze prametry wzajemnie sie wykluczajace nie pojawiaja sie razem !! bo obsluguje tylko jeden z nich s lub r \n ",
                                   argv[0]);
                            return -1;
                        }
                        break;
            case 's':
                        s_flag = 1;
                        if ((s_time = time_parametr(optarg)) < 0) {
                            printf(" Usage:  %s  (obligatory)  -#  <intiger> obligatory {-r <float> / float1:float2 or -s <flaot>/ <float1>:f<loat2>  } <addr> (optional) port(obligatory)\n zakladam ze prametry wzajemnie sie wykluczajace nie pojawiaja sie razem !! bo obsluguje tylko jeden z nich s lub r \n ",
                                   argv[0]);
                            return -1;
                        }
                        break;
            default:
                fprintf(stderr, "Usage: %s: obligatory  -r <path> obligatory -t <time>  float (optional)<addr> and obiligatory port_nr   \n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    if ((hash_flag + r_flag + s_flag) != 2 || (parametr_address_port(argv[argc - 1], &addr, &port_nr)) < 0  )  {
        printf(" Usage:  %s  (obligatory)  -#  <intiger> obligatory {-r <float> / float1:float2 or -s <flaot>/ <float1>:f<loat2>    } and  <addr> (optional) port(obligatory)  \n zakladam ze prametry wzajemnie sie wykluczajace nie pojawiaja sie razem !! bo obsluguje tylko jeden z nich s lub r \n ",
               argv[0]);
        return -1;
    }
//-----------------------------------------------------------------------------------TIMER-------No.1------------------------and -----------SOCKET------------------
    int timer_fd, socket_fd;
    struct pollfd fds[2];// 0 na socket_fd i 1 na timer!
    timer_fd = make_timerfd(r_time);
    add_fd_to_pollfd(fds, timer_fd, 1);
    struct sockaddr_in address;
    char ip[100];

    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket error");
        return -2;
    }
    address.sin_family = AF_INET;
    hostname_to_ip(addr, ip);
    address.sin_addr.s_addr = inet_addr(ip);
    address.sin_port = htons(port_nr);
    if (connect(socket_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
        perror("connect error");
        return -2;
    }
    //--------------------------------------------------------------------PRZYGOTOWANIE----------------------------------------------------------------------------
    add_fd_to_pollfd(fds, socket_fd, 0);
    struct timespec time;
    time.tv_sec = (int) s_time;
    time.tv_nsec = (s_time - (int) s_time) * 1000000000L;
    char buf[4] = "OK\n";
    char *rec = (char *) malloc(READ_DATA_SIZE * sizeof(char));
    char *blok = (char *) malloc(READ_DATA_SIZE * sizeof(char));

    int fr, fo, active;
    uint64_t tik = 0;
    int number_to_read = READ_DATA_SIZE;   //112
    int wyslane = ilosc_kom, rap = ilosc_kom;
//---------------------------------do raportow--------------------------------------
    char **komunikat = (char **) malloc(ilosc_kom * sizeof(char *));  //na sumy kontrolne
    for (int i = 0; i < ilosc_kom; i++)
        komunikat[i] = (char *) malloc(sizeof(char) * 100);

    int ile = 0, send_iter = 0, rec_iter = 0, all_iter = 0;
    int time_sec[ilosc_kom], time_nano_sec[ilosc_kom], time_sec_all_read[ilosc_kom], time_nano_sec_all_read[ilosc_kom];
    struct timespec tab_time_send[ilosc_kom], tab_time_rec[ilosc_kom], tab_time_all_read[ilosc_kom];
    //-----------------------------------------------------------SENT AND RECEIVE MESSAGE---------------------------------------------------------------------------
    while ((ilosc_kom > 0) && (s_flag > 0)) {
        if ((fo = write(socket_fd, &buf, sizeof(buf))) < 0) {
            perror("write error");
            return -2;
        }
        clock_gettime(CLOCK_MONOTONIC, &tab_time_send[send_iter++]);
        ilosc_kom--;
        nanosleep(&time, NULL);

        while ((fr = read(socket_fd, rec, number_to_read)) > 0) {   //blokujacy read!!
            if (number_to_read ==
                READ_DATA_SIZE)//tylko gdy 1 paczke odbieram moze byc nawet calosc wtedy ale moze byc tez czesc nwm tego
                clock_gettime(CLOCK_MONOTONIC, &tab_time_rec[rec_iter++]);

            strcat(blok, rec);
            if (fr == number_to_read) {     //caly blok przeczytalem!!
                clock_gettime(CLOCK_MONOTONIC, &tab_time_all_read[all_iter++]);

                md(blok, &komunikat[ile++]);
                bzero(blok, READ_DATA_SIZE);
                bzero(rec, fr);
                number_to_read = READ_DATA_SIZE;
                break;
            } else {
                bzero(rec, fr);
                number_to_read -= fr;
            }
        }
    }

    while ((ilosc_kom > 0) && (r_flag > 0))               //--------------------------------------------------------------------- //gdy byl parametr -r
    {
        active = poll(fds, 2, -1);
        if (active > 0) {
            for (int i = 0; i < 2; i++) {
                if ((fds[i].fd == timer_fd) && (fds[i].revents & POLLIN) && (wyslane > 0)) {
                    read(fds[i].fd, &tik, sizeof(uint64_t));
                    if ((fo = write(socket_fd, buf, strlen(buf))) < 0) {
                        perror("write error");
                        return -2;
                    }
                    clock_gettime(CLOCK_MONOTONIC, &tab_time_send[send_iter++]);
                    wyslane--;
                    if(wyslane == 0 )
                        fds[i].fd=-1;
                } else if ((fds[i].fd == socket_fd) && (fds[i].revents & POLLIN)) {
                    if ((fr = read(socket_fd, rec, number_to_read)) > 0) {

                        if (number_to_read ==
                            READ_DATA_SIZE)//czyli 1 paczke otrzyma bo potem zmniejszam number_to_read!!
                            clock_gettime(CLOCK_MONOTONIC, &tab_time_rec[rec_iter++]);

                        strcat(blok, rec);
                        if (fr == number_to_read) {
                            clock_gettime(CLOCK_MONOTONIC, &tab_time_all_read[all_iter++]);
                            ilosc_kom--;
                            md(blok, &komunikat[ile++]);
                            bzero(blok, READ_DATA_SIZE);
                            bzero(rec, fr);
                            number_to_read = READ_DATA_SIZE;
                        } else {
                            bzero(rec, fr);
                            number_to_read -= fr;
                        }
                    }
                }
            }
        }
    }

    close(socket_fd);
    //---------------------------------------------------------------------RAPORT----------------------------------
    struct timespec raport_time, raport_time1;
    clock_gettime(CLOCK_REALTIME, &raport_time);
    clock_gettime(CLOCK_MONOTONIC, &raport_time1);
    raport(getpid(),ip,port_nr,raport_time,raport_time1);

    for (int j = 0; j < rap; j++) {
        licz_czas(tab_time_send[j], tab_time_rec[j], &time_nano_sec[j], &time_sec[j]);
        licz_czas(tab_time_rec[j], tab_time_all_read[j], &time_nano_sec_all_read[j], &time_sec_all_read[j]);
        print_raport(j,komunikat[j], time_sec[j], time_nano_sec[j],time_sec_all_read[j], time_nano_sec_all_read[j]);
    }
    return 0;
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------

float losuj(float range, float offset)
{
    int fd, tmp;
    unsigned int num;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1) {
        perror("fail to open ");
        return -2;
    }

    tmp = read(fd, &num, sizeof(int));
    if (tmp == -1) {
        perror("fail to read");
        return -2;
    }

    return (((float) num / (float) (UINT_MAX)) * range + offset);
}
// x=(num/uint_max) ==> <0,1>
//x*range +offset ==>losowa liczba z naszego przedzialu !!


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

void add_fd_to_pollfd(struct pollfd *fdd, int fd, int index) {
    fdd[index].fd = fd;
    fdd[index].events = POLLIN;
    fdd[index].revents = 0;
}

int parametr_address_port(char *arg, char **addr, int *port) {
    char *tmp, *tmp2;
    char **ptr = NULL;
    tmp = strtok(arg, ":");
    tmp2 = strtok(NULL, ":");
    if (tmp2 == NULL)    // podano port, addr dsomyslny
    {

        *port = strtol(tmp, ptr, 0);

        if (*port == 0) {
            perror("strtol error parametr is not a number !!");
            return -1;
        }
    } else            //podano addr:port
    {
        *addr = tmp;
        *port = strtol(tmp2, ptr, 0);
        if (*port == 0) {
            perror("strtol error");
            return -1;
        }
    }
    return 0;   //-1 jak blad
}
void raport(int pid, char * ip, int port, struct timespec raport_time, struct timespec raport_time_2 )
{
    fprintf(stderr, "Raport czasowy:\n\tCLOCK REALTIME: sec %ld nsec %ld\n", raport_time.tv_sec, raport_time.tv_nsec);
    fprintf(stderr, "\tCLOCK MONOTONIC: sec %ld nsec %ld\n", raport_time_2.tv_sec, raport_time_2.tv_nsec);
    fprintf(stderr, "Identyfikacja konsumenta:\n \tpid:%d adrress ip: %s %d \n", pid, ip, port);
}
void  print_raport(int num, char*sum, int sec, int nsec, int sec_2, int nsec_2)
{
    fprintf(stderr, "komunikat %d:\n\tsuma kontrolna MD5: %s\n", num + 1, sum);
    fprintf(stderr, "\tOpoznienie miedzy wyslaniem zgloszenia a odebraniem pierwszych bajtow: %d sec %d nsec\n",
            sec, nsec);
    fprintf(stderr, "\tOpoznienie miedzy odebraniem pierwszych bajtow a caloscia bloku: %d sec %d nsec \n",
            sec_2, nsec_2);
}
void md(char *cos, char **sum) {
    int n;
    MD5_CTX c;
    unsigned char out[MD5_DIGEST_LENGTH];
    char *suma_temp = (char *) malloc(100 * sizeof(char));
    char *output = (char *) malloc(100 * sizeof(char));
    MD5_Init(&c);
    MD5_Update(&c, cos, READ_DATA_SIZE);
    MD5_Final(out, &c);
    bzero(suma_temp, 100);

    for (n = 0; n < MD5_DIGEST_LENGTH; n++) {
        snprintf(output, 100, "%02x", out[n]);
        strcat(suma_temp, output);
        bzero(output, 100);
    }

    *sum = suma_temp;
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

float time_parametr(char *arg) {
    char **ptr = NULL;
    char *r1, *r2;
    float f1, f2;
    r1 = strtok(optarg, ":");
    r2 = strtok(NULL, ":");
    f1 = strtof(r1, ptr);
    if (f1 == 0) {
        return -1;
    }

    if (r2 != NULL) {

        f2 = strtof(r2, ptr);
        if (f2 == 0) {
            return -1;
        }
        if (f1 > f2) {
            printf("ERROR druga liczba z przedzialu jest mniejsza niz pierwsza\n");
            return -2;
        }

        if (f1 != f2)
            return losuj(f2 - f1, f1);                //f2-f1 przedzial,f1=>"offset"
        else
            return f1;
    } else
        return f1;

}

void licz_czas(struct timespec start, struct timespec koniec, int *nsec, int *sec) {
    if ((koniec.tv_nsec - start.tv_nsec) < 0) {
        koniec.tv_sec--;
        *nsec = (koniec.tv_nsec - start.tv_nsec) + 1000000000L;
    } else
        *nsec = koniec.tv_nsec - start.tv_nsec;


    *sec = koniec.tv_sec - start.tv_sec;
}

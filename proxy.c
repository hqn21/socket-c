#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <stdbool.h>

#define MAX_LINE 1024
#define MAX_RESPONSE_SIZE 1000000

/* readready() - check whether read ready for given file descriptor
 * return non-negative if ready, 0 if not ready, negative on errors
 */
int readready(int fd) {
    fd_set map;
    int ret;
    struct timeval _zerotimeval = {0, 0};
    do {
        FD_ZERO(&map);
        FD_SET(fd, &map);
        ret = select(fd + 1, &map, NULL, NULL, &_zerotimeval);
        if (ret >= 0) {
            return ret;
        }
    } while (errno == EINTR);
    return ret;
}

/* readline() - read a line (ended with '\n') from a file descriptor
 * return the number of chars read, -1 on errors
 */
int readline(int fd, char *ptr, int maxlen) {
    int n, rc;
    char c;
    for (n = 1; n < maxlen; n++) {
        if ((rc = read(fd, &c, 1)) == 1) {
            *ptr++ = c;
            if (c == '\n') {
                break;
            }
        } else if (rc == 0) {
            if (n == 1) {
                return (0); /* EOF, no data read */
            } else {
                break; /* EOF, some data read */
            }
        } else {
            return (-1); /* Error */
        }
    }
    *ptr = 0;
    return (n);
}

/* my_inet_addr() - convert host/IP into binary data in network byte order */
in_addr_t my_inet_addr(char *host) {
    in_addr_t inaddr;
    struct hostent *hp;
    inaddr = inet_addr(host);
    if (inaddr == INADDR_NONE && (hp = gethostbyname(host)) != NULL) {
        bcopy(hp->h_addr, (char *)&inaddr, hp->h_length);
    }
    return inaddr;
}

int tcp_open_client(char *host, char *port) {
    int sockfd;
    struct sockaddr_in serv_addr;
    /* Fill in "serv_addr" with the address of the server */
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = my_inet_addr(host);
    serv_addr.sin_port = htons(atoi(port));
    /* Open a TCP socket (an Internet stream socket). */
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 || connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        return -1;
    }
    return sockfd;
}

int tcp_open_server(char *port) {
    int sockfd;
    struct sockaddr_in serv_addr;
    /* Open a TCP socket (an Internet stream socket). */
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }
    /* Bind our local address so that the client can send to us. */
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(port));
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        return -1;
    }
    listen(sockfd, 5);
    return sockfd;
}

void do_main(int newsockfd, char* host) {
    int ret, sockfd;
    char buf[MAX_LINE];
    while ((ret = readready(newsockfd)) >= 0) {
        if (ret == 0) {
            continue;
        }
        if (readline(newsockfd, buf, MAX_LINE) <= 0) {
            break;
        }
        fputs(buf, stdout);
        if (strncmp(buf, "GET ", 4) == 0) {
            char path[MAX_LINE] = "", newBuf[MAX_LINE];
            bool startInputPath = false;
            for(int i = 0; i < strlen(buf); i++) {
                if(buf[i] == '/') {
                    startInputPath = true;
                }

                if(startInputPath) {
                    if(buf[i] == ' ') {
                        break;
                    }
                    char str[2] = {buf[i], '\0'};
                    strcat(path, str);
                }
            }

            sockfd = tcp_open_client(host, "80");
            snprintf(newBuf, sizeof(newBuf), "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", path, host);
            send(sockfd, newBuf, strlen(newBuf), 0);

            char response[MAX_RESPONSE_SIZE] = "", bufResponse[MAX_LINE];
            bool startInputHTML = false;
            while (1) {
                int ret = readready(sockfd);
                if (ret < 0) {
                    break;
                }
                if (ret > 0) {
                    if (readline(sockfd, buf, MAX_LINE) <= 0) {
                        break;
                    }
                    strcat(response, buf);
                }
            }
            close(sockfd);
            printf("%s\n", response);
            send(newsockfd, response, strlen(response), 0);
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    int sockfd, newsockfd, childpid;
    unsigned int clilen;
    struct sockaddr_in cli_addr;

    /* argv[1] => host, argv[2] => port */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        return 1;
    }

    char *host;
    if (strncmp(argv[1], "http://", 7) == 0) {
        host = argv[1] + 7;
    } else {
        host = argv[1];
    }
    sockfd = tcp_open_server(argv[2]);

    for (;;) {
        /* Wait for a connection from a client process. (Concurrent Server) */
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) {
            exit(1); /* server: accept error */
        }
        if ((childpid = fork()) < 0) {
            exit(1); /* server: fork error */
        }
        if (childpid == 0) { /* child process */
            close(sockfd);      /* close original socket*/
            do_main(newsockfd, host); /* process the request */
            exit(0);
        }
        close(newsockfd); /* parent process */
    }
}
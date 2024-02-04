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

#define MAX_LINE 1024

/* my_inet_addr() - convert host/IP into binary data in network byte order */
in_addr_t my_inet_addr(char *host) {
    in_addr_t inaddr;
    struct hostent* hp;
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

int main(int argc, char *argv[]) {
    int sockfd;
    char buf[MAX_LINE];
    char *host, *path;
    char *default_path = "/";
    char *default_port = "80";

    /* If argc is not equal to 2, then print usage */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <URL>\n", argv[0]);
        return 1;
    }

    if (strncmp(argv[1], "http://", 7) == 0) {
        host = argv[1] + 7;
    } else {
        host = argv[1];
    }

    path = strchr(host, '/');
    if (path) {
        *path = '\0';
        path++;
    } else {
        path = default_path;
    }

    /* Open TCP connection */
    sockfd = tcp_open_client(host, default_port);
    if (sockfd < 0) {
        perror("tcp_open_client");
        return 1;
    }

    /* Send HTTP GET request */
    snprintf(buf, sizeof(buf), "GET /%s HTTP/1.1\r\nHost: %s\r\n\r\n", path, host);
    send(sockfd, buf, strlen(buf), 0);

    /* Read and print the response */
    while (1) {
        int ret = readready(sockfd);
        if (ret < 0) {
            break;
        }
        if (ret > 0) {
            if (readline(sockfd, buf, MAX_LINE) <= 0) {
                break;
            }
            fputs(buf, stdout);
        }
    }

    close(sockfd);
    return 0;
}
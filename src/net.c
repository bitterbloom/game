#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#include "./net.h"
#include "./arg.h"

// taken from github: jdah/network_demo.c
bool server() {
    // create socket
    int const fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return false;
    }

    // bind to open port
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr*) &addr, sizeof (addr))) {
        perror("bind");
        return false;
    }

    // read port
    socklen_t addr_len = sizeof (addr);
    getsockname(fd, (struct sockaddr*) &addr, &addr_len);
    printf("server is on port %d\n", (int) ntohs(addr.sin_port));

    if (listen(fd, 1)) {
        perror("listen");
        return false;
    }

    // accept incoming connection
    struct sockaddr_storage caddr;
    socklen_t caddr_len = sizeof (caddr);
    int const cfd = accept(fd, (struct sockaddr*) &caddr, &caddr_len);

    // read from client with recv
    char buf[1024];
    while (true) {
        ssize_t read_n = recv(cfd, buf, sizeof (buf), 0);
        if (read_n == -1) {
            perror("recv");
            return false;
        }

        printf(">>> %.*s\n", (int) read_n, buf);

        if (buf[read_n - 1] == '\0') {
            break;
        }
    }

    close(cfd);
    close(fd);

    return true;
}

bool client(char const *const ip, char const *const port) {
    uint16_t port_num;
    if (sscanf(port, "%hu", &port_num) != 1) { // %hu is unsigned short
        fprintf(stderr, "invalid port: %s\n", port);
        return false;
    }


    int const fd = socket(PF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_num);

    // connect to local machine at specified port
    char addrstr[NI_MAXHOST + NI_MAXSERV + 1];
    snprintf(addrstr, sizeof (addrstr), "%s:%d", ip, port_num);

    // parse into address
    inet_pton(AF_INET, addrstr, &addr.sin_addr);

    // connect to server
    if (connect(fd, (struct sockaddr*) &addr, sizeof (addr))) {
        perror("connect");
        return false;
    }

    char buf[1024];
    while (true) {
        printf("<<< ");
        fflush(stdout);

        ssize_t read_n = read(STDIN_FILENO, buf, sizeof (buf));
        if (read_n == -1) {
            perror("read");
            return false;
        }

        ssize_t send_n = send(fd, buf, read_n, 0);
        if (send_n == -1) {
            perror("send");
            return false;
        }
    }

    // close(fd);

    // return true;
}

int unused(int const argc, char const *const *const argv) {
    if (argc == 2 && !strcmp(argv[1], "server")) {
        if (!server()) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    if (argc == 4 && !strcmp(argv[1], "client")) {
        char *ip;
        arg_trimn(argv[2], strlen(argv[2]), &ip);

        char *port;
        arg_trimn(argv[3], strlen(argv[3]), &port);

        if (!client(ip, port)) {
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    fprintf(stderr, "usage:\t%s server\n", argv[0]);
    fprintf(stderr, "      \t%s client <ip> <port>\n", argv[0]);
    return EXIT_FAILURE;
}


#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <threads.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>

#include "./net.h"
#include "./arg.h"

// TODO: Figure out how to remove players from the server data and close their sockets.

struct ServerData {
    uint16_t max;
    uint16_t *len;
    int *sockets;
    Player *players;
    pthread_t sender;
    pthread_t receiver; // receiver is -1 if not started
};

/// TODO: Use a packet type
// typedef struct {
//     Player players;
// } PlayerPacket;

ServerData *net_server_data_new(Player *const players, uint16_t *const len_players, uint16_t const max_players) {
    ServerData *data = malloc(sizeof (ServerData));
    if (data == NULL) {
        fprintf(stderr, "Failed to allocate server data\n");
        exit(EXIT_FAILURE);
    }

    int *sockets = malloc(max_players * sizeof (int));
    if (sockets == NULL) {
        fprintf(stderr, "Failed to allocate server sockets\n");
        exit(EXIT_FAILURE);
    }

    data->max = max_players;
    data->len = len_players;
    data->sockets = sockets;
    data->players = players;
    data->sender = -1;
    data->receiver = -1;
    return data;
}

void net_server_data_destroy(ServerData *const data) {
    if (data->sender != -1) {
        if (pthread_cancel(data->sender)) {
            fprintf(stderr, "Failed to send cancel request to server sender thread: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if (data->receiver != -1) {
        if (pthread_cancel(data->receiver)) {
            fprintf(stderr, "Failed to send cancel request to server receiver thread: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    free(data->sockets);
    free(data);
}

typedef struct {
    ServerData *data;
} ServerSenderArgs;

static noreturn void *server_thread_sender(ServerSenderArgs *const args) {
    ServerData *const data = args->data;
    free(args);

    printf("starting server sender thread\n");

    Player *const buf = malloc(data->max * sizeof (Player));
    uint16_t *const written = calloc(data->max, sizeof (uint16_t));

    while (true) {
        printf("send loop began\n");
        fflush(stdout);

        printf("data->len = %u\n", *data->len);

        thrd_sleep(&(struct timespec) {.tv_sec = 0, .tv_nsec = 10000000}, NULL); // 10ms

        fd_set write_fds;
        FD_ZERO(&write_fds);

        int max_fd = 0;
        for (uint16_t i = 0; i < *data->len; i++) {
            if (data->sockets[i] > max_fd) {
                max_fd = data->sockets[i];
            }
            FD_SET(data->sockets[i], &write_fds);
        }
        
        int ready_count = select(max_fd + 1, NULL, &write_fds, NULL, &(struct timeval) {.tv_sec = 1, .tv_usec = 0}); // blocks

        if (ready_count == -1) {
            fprintf(stderr, "Failed to select: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        // if zero are ready, then we timed out
        if (ready_count == 0) {
            printf("send loop timed out\n");
            continue;
        }

        // send packets to clients
        for (uint16_t i = 0; i < *data->len && ready_count > 0; i++) {
            if (FD_ISSET(data->sockets[i], &write_fds)) {
                //printf("sending to client %u\tat i = %u\n", data->players[i].id, (uint32_t) i);

                uint16_t const begin = written[i];
                uint16_t const size = *data->len - begin;

                //printf("begin = %u\n", begin);
                //printf("size = %u\n", size);
                //printf("len = %u\n", *data->len);

                for (uint16_t j = 0; j < size; j++) {
                    buf[j].id = htonl(data->players[begin + j].id);
                    buf[j].x = htonl(data->players[begin + j].x);
                    buf[j].y = htonl(data->players[begin + j].y);
                }

                size_t n = write(data->sockets[i], buf, size * sizeof (Player));
                if (n == -1) {
                    // TODO: maybe check for EAGAIN or EWOULDBLOCK and try again
                    fprintf(stderr, "Failed to write to client: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }

                written[i] += n / sizeof (Player);
                if (written[i] == *data->len) {
                    written[i] = 0;
                }

                ready_count--;
            }
        }
    }

    free(buf);
    free(written);
}

typedef struct {
    int serv_fd;
    ServerData *data;
} ServerReceiverArgs;

static noreturn void *server_thread_receiver(ServerReceiverArgs *const args) {
    int const serv_fd = args->serv_fd;
    ServerData *const data = args->data;
    free(args);

    printf("starting server receiver thread with serv_fd %d\n", serv_fd);

    if (*data->len != 0) {
        fprintf(stderr, "Player list must be empty\n");
        exit(EXIT_FAILURE);
    }

    if (data->max > FD_SETSIZE) {
        fprintf(stderr, "Player list is too large\n");
        exit(EXIT_FAILURE);
    }

    while (true) {
        printf("receive loop began\n");
        fflush(stdout);

        thrd_sleep(&(struct timespec) {.tv_sec = 0, .tv_nsec = 10000000}, NULL); // 10ms

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(serv_fd, &read_fds);

        int max_fd = serv_fd;
        for (uint16_t i = 0; i < *data->len; i++) {
            if (data->sockets[i] > max_fd) {
                max_fd = data->sockets[i];
            }
            FD_SET(data->sockets[i], &read_fds);
        }

        int ready_count = select(max_fd + 1, &read_fds, NULL, NULL, &(struct timeval) {.tv_sec = 1, .tv_usec = 0}); // blocks

        if (ready_count == -1) {
            fprintf(stderr, "Failed to select: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        // if zero are ready, then we timed out
        if (ready_count == 0) {
            printf("receive loop timed out\n");
            continue;
        }

        // accept new connections
        if (FD_ISSET(serv_fd, &read_fds)) {
            struct sockaddr_storage con_addr;
            socklen_t con_addr_len = sizeof (con_addr);
            int const con_fd = accept(serv_fd, (struct sockaddr*) &con_addr, &con_addr_len);
            if (con_fd == -1) {
                fprintf(stderr, "Failed to accept connection: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            fcntl(con_fd, F_SETFL, O_NONBLOCK);

            // print address
            char addrstr[INET_ADDRSTRLEN];
            struct sockaddr_in *const con_addr_in = (struct sockaddr_in*) &con_addr;
            inet_ntop(AF_INET, &con_addr_in->sin_addr, addrstr, sizeof (addrstr));
            printf("accepted connection from %s\n", addrstr);

            if (*data->len == data->max) {
                fprintf(stderr, "Player list is full\n");
                exit(EXIT_FAILURE);
            }

            data->sockets[*data->len] = con_fd;
            data->players[*data->len].id = rand();
            data->players[*data->len].x = 0;
            data->players[*data->len].y = 0;
            (*data->len)++;

            // if first connection
            if (*data->len == 1) {
                // spawn server thread to broadcast packets
                ServerSenderArgs *const sender_args = malloc(sizeof (ServerSenderArgs));
                if (sender_args == NULL) {
                    fprintf(stderr, "Failed to allocate server sender args\n");
                    exit(EXIT_FAILURE);
                }

                sender_args->data = data;

                if (pthread_create(&data->sender, NULL, (void*(*)(void*)) server_thread_sender, sender_args)) {
                    fprintf(stderr, "Failed to create server sender thread: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }

            ready_count--;
        }

        // receive packets from clients
        for (uint16_t i = 0; i < *data->len && ready_count > 0; i++) {
            if (FD_ISSET(data->sockets[i], &read_fds)) {
                printf("receiving from client %u\tat i = %u\n", data->players[i].id, (uint32_t) i);

                Player buf;
                size_t n = read(data->sockets[i], &buf, sizeof (Player));
                // ignore the count of bytes read as we will be reading the entire packet at once
                
                if (n == -1) {
                    fprintf(stderr, "Failed to read from client: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }

                data->players[i].id = ntohl(buf.id);
                data->players[i].x = ntohl(buf.x);
                data->players[i].y = ntohl(buf.y);

                ready_count--;
            }
        }
    }
}

void net_server_create(uint16_t const port, ServerData *const data) {
    // create server socket
    int serv_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_fd == -1) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    fcntl(serv_fd, F_SETFL, O_NONBLOCK);
    printf("server socket created with fd %d\n", serv_fd);

    // bind to specified port
    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(serv_fd, (struct sockaddr*) &serv_addr, sizeof (serv_addr))) {
        fprintf(stderr, "Failed to bind socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // check which port was used
    socklen_t serv_addr_len = sizeof (serv_addr);
    getsockname(serv_fd, (struct sockaddr*) &serv_addr, &serv_addr_len);
    printf("server is on port %d\n", (int) ntohs(serv_addr.sin_port));

    // queue up connections
    if (listen(serv_fd, data->max) == -1) {
        fprintf(stderr, "Failed to listen: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // spawn server thread to receive packets
    ServerReceiverArgs *const receiver_args = malloc(sizeof (ServerReceiverArgs));
    if (receiver_args == NULL) {
        fprintf(stderr, "Failed to allocate server receiver args\n");
        exit(EXIT_FAILURE);
    }

    receiver_args->serv_fd = serv_fd;
    receiver_args->data = data;

    if (pthread_create(&data->receiver, NULL, (void*(*)(void*)) server_thread_receiver, receiver_args)) {
        fprintf(stderr, "Failed to create server receiver thread: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

struct ClientData {
    Player const *player;
    pthread_t sender;
    pthread_t receiver;
};

ClientData *net_client_data_new(Player const *const player) {
    ClientData *data = malloc(sizeof (ClientData));
    if (data == NULL) {
        fprintf(stderr, "Failed to allocate client data\n");
        exit(EXIT_FAILURE);
    }

    data->player = player;
    data->sender = -1;
    data->receiver = -1;
    return data;
}

void net_client_data_destroy(ClientData *const data) {
    if (data->sender != -1) {
        if (pthread_cancel(data->sender)) {
            fprintf(stderr, "Failed to send cancel request to client sender thread: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if (data->receiver != -1) {
        if (pthread_cancel(data->receiver)) {
            fprintf(stderr, "Failed to send cancel request to client receiver thread: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    free(data);
}

typedef struct {
    int socket;
    ClientData *data;
} ClientSenderArgs;

static noreturn void *client_thread_sender(ClientSenderArgs *const args) {
    int const socket = args->socket;
    ClientData *const data = args->data;
    free(args);

    printf("starting client sender thread\n");

    while (true) {
        printf("send loop began\n");
        fflush(stdout);

        thrd_sleep(&(struct timespec) {.tv_sec = 0, .tv_nsec = 10000000}, NULL); // 10ms

        Player buf;
        buf.id = htonl(data->player->id);
        buf.x = htonl(data->player->x);
        buf.y = htonl(data->player->y);

        size_t n = write(socket, &buf, sizeof (Player)); // blocks
        if (n == -1) {
            fprintf(stderr, "Failed to write to server: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        printf("sent to server\n");
    }
}

typedef struct {
    int socket;
    ClientData *data;
} ClientReceiverArgs;

static noreturn void *client_thread_receiver(ClientReceiverArgs *const args) {
    int const socket = args->socket;
    //ClientData *const data = args->data;
    free(args);

    printf("starting client receiver thread\n");

    while (true) {
        printf("receive loop began\n");
        fflush(stdout);

        thrd_sleep(&(struct timespec) {.tv_sec = 0, .tv_nsec = 10000000}, NULL); // 10ms

        Player buf;
        size_t n = read(socket, &buf, sizeof (Player)); // blocks
        // ignore the count of bytes read as we will be reading the entire packet at once

        if (n == -1) {
            fprintf(stderr, "Failed to read from server: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        /// TODO: Make clients also store players position
        // data->player.id = ntohl(buf.id);
        // data->player.x = ntohl(buf.x);
        // data->player.y = ntohl(buf.y);

        printf("received from server\n");
    }
}

void net_client_create(uint16_t port, ClientData *data) {
    // create client socket
    int clnt_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (clnt_fd == -1) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    printf("client socket created with fd %d\n", clnt_fd);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // connect to local machine at specified port
    char addrstr[NI_MAXHOST + NI_MAXSERV + 1];
    snprintf(addrstr, sizeof (addrstr), "127.0.0.1:%d", port);

    // parse into address
    inet_pton(AF_INET, addrstr, &addr.sin_addr);

    printf("client is connecting on port %d\n", (int) ntohs(addr.sin_port));

    // connect to server
    if (connect(clnt_fd, (struct sockaddr*) &addr, sizeof (addr))) {
        fprintf(stderr, "Failed to connect to server: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    printf("client connected to server\n");

    // spawn client thread to receive packets
    ClientReceiverArgs *const receiver_args = malloc(sizeof (ClientReceiverArgs));
    if (receiver_args == NULL) {
        fprintf(stderr, "Failed to allocate client receiver args\n");
        exit(EXIT_FAILURE);
    }

    receiver_args->socket = clnt_fd;
    receiver_args->data = data;

    if (pthread_create(&data->receiver, NULL, (void*(*)(void*)) client_thread_receiver, receiver_args)) {
        fprintf(stderr, "Failed to create client receiver thread: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // spawn client thread to send packets
    ClientSenderArgs *const sender_args = malloc(sizeof (ClientSenderArgs));
    if (sender_args == NULL) {
        fprintf(stderr, "Failed to allocate client sender args\n");
        exit(EXIT_FAILURE);
    }

    sender_args->socket = clnt_fd;
    sender_args->data = data;

    if (pthread_create(&data->sender, NULL, (void*(*)(void*)) client_thread_sender, sender_args)) {
        fprintf(stderr, "Failed to create client sender thread: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

#if 0
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
#endif


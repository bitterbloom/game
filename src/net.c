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

#define SOCK_ADDR_IN_EQ(a, b) (a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port)
#define DELAY (&(struct timespec) {.tv_sec = 0, .tv_nsec = 100000000}) // 100ms

typedef enum {
    JOINING,
    PLAYING,
} clnt_state;

struct ServerData {
    uint16_t max;
    uint16_t *len;
    clnt_state *clnt_states;
    struct sockaddr_in *clnt_addrs;
    Player *players;
    pthread_t sender;
    pthread_t receiver; // receiver is -1 if not started
    int serv_fd;
};

typedef uint8_t PacketTag;

typedef struct {
    enum : PacketTag {
        JOIN,
        POSITION,
        // LEAVE,
    } tag;
    union {
        struct { // Position
            Player p_player; // TODO: Clients shouldn't need to send their player id.
        };
    };
} C2SPacket;

typedef struct {
    enum : PacketTag {
        ACCEPT,
        POSITIONS,
        // UPDATE,
        // KICK,
    } tag;
    union {
        struct { // Accept
            uint16_t a_max;
            uint32_t a_id;
        };
        struct { // Positions
            uint16_t p_len;
            Player p_players[];
        };
        // struct { // Update
        //     Player p_player;
        // };
    };
} S2CPacket;

ServerData *net_server_data_new(Player *const players, uint16_t *const len_players, uint16_t const max_players) {
    if (max_players == 0) {
        fprintf(stderr, "Player list must have at least one player\n");
        exit(EXIT_FAILURE);
    }

    ServerData *data = malloc(sizeof (ServerData));
    if (data == NULL) {
        fprintf(stderr, "Failed to allocate server data\n");
        exit(EXIT_FAILURE);
    }

    clnt_state *const clnt_states = malloc(max_players * sizeof (clnt_state));
    if (clnt_states == NULL) {
        fprintf(stderr, "Failed to allocate client states\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in *const clnt_addrs = malloc(max_players * sizeof (struct sockaddr_storage));
    if (clnt_addrs == NULL) {
        fprintf(stderr, "Failed to allocate client addresses\n");
        exit(EXIT_FAILURE);
    }

    *data = (ServerData) {
        .max = max_players,
        .len = len_players,
        .clnt_states = clnt_states,
        .clnt_addrs = clnt_addrs,
        .players = players,
        -1, -1, -1,
    };
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

    free(data->clnt_addrs);
    close(data->serv_fd);
    free(data);
}

static noreturn void *server_thread_sender(ServerData *const data) {
    printf("starting server sender thread\n");

    S2CPacket *packet = malloc(sizeof (S2CPacket) + data->max * sizeof (Player));
    size_t next_client = 0;

    while (true) {
        fflush(stdout);
        //printf("send loop began\n");

        thrd_sleep(DELAY, NULL);

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(data->serv_fd, &fds);
        int max_fd = data->serv_fd;

        int ready_count = select(max_fd + 1, NULL, &fds, NULL, &(struct timeval) {.tv_sec = 1, .tv_usec = 0}); // blocks

        if (ready_count == -1) {
            fprintf(stderr, "Failed to select: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (ready_count == 0) {
            printf("send loop timed out\n");
            continue;
        }

        if (FD_ISSET(data->serv_fd, &fds)) {
            switch (data->clnt_states[next_client]) {
                case JOINING: {
                    packet->tag = ACCEPT;
                    packet->a_max = htons(data->max);
                    packet->a_id = htonl(data->players[next_client].id);

                    printf("<<< Sending ACCEPT packet to %s:%d\n", inet_ntoa(data->clnt_addrs[next_client].sin_addr), ntohs(data->clnt_addrs[next_client].sin_port));
                } break;
                case PLAYING: {
                    packet->tag = POSITIONS;
                    packet->p_len = htons(*data->len);

                    for (uint16_t i = 0; i < *data->len; i++) {
                        packet->p_players[i].id = htonl(data->players[i].id);
                        packet->p_players[i].x = htonl(data->players[i].x);
                        packet->p_players[i].y = htonl(data->players[i].y);
                    }

                    printf("<<< Sending POSITIONS packet to %s:%d\n", inet_ntoa(data->clnt_addrs[next_client].sin_addr), ntohs(data->clnt_addrs[next_client].sin_port));
                } break;
            }

            struct sockaddr_in clnt_addr = data->clnt_addrs[next_client];
            ssize_t nwritten = sendto(data->serv_fd, packet, sizeof (S2CPacket), 0, (struct sockaddr *) &clnt_addr, sizeof clnt_addr);
            if (nwritten == -1) {
                fprintf(stderr, "Failed to send to client: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }

            printf("< Send %ld bytes to %s:%d\n", (long) nwritten, inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));
        }

        next_client = (next_client + 1) % *data->len;
    }

    // never reached
    // TODO: Maybe give some sort of signal to this thread to indicate to stop the loop
    // when the server is shutting down.
    free(packet);
}

static noreturn void *server_thread_receiver(ServerData *const data) {
    printf("starting server receiver thread\n");

    if (*data->len != 0) {
        fprintf(stderr, "Player list must be empty\n");
        exit(EXIT_FAILURE);
    }

    if (data->max > FD_SETSIZE) {
        fprintf(stderr, "Player list is too large\n");
        exit(EXIT_FAILURE);
    }

    C2SPacket packet;

    while (true) {
        fflush(stdout);
        //printf("receive loop began\n");

        //thrd_sleep(DELAY, NULL);

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(data->serv_fd, &fds);
        int max_fd = data->serv_fd;

        int ready_count = select(max_fd + 1, &fds, NULL, NULL, &(struct timeval) {.tv_sec = 1, .tv_usec = 0}); // blocks
        if (ready_count == -1) {
            fprintf(stderr, "Failed to select: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (ready_count == 0) {
            printf("receive loop timed out\n");
            continue;
        }

        if (FD_ISSET(data->serv_fd, &fds)) {
            struct sockaddr_storage clnt_addr_any = {0};
            socklen_t clnt_addr_any_len = sizeof clnt_addr_any;
            ssize_t nread = recvfrom(data->serv_fd, &packet, sizeof (C2SPacket), 0, (struct sockaddr *) &clnt_addr_any, &clnt_addr_any_len);
            if (nread == -1) {
                fprintf(stderr, "Failed to receive from client: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }

            if (clnt_addr_any.ss_family != AF_INET || clnt_addr_any_len != sizeof (struct sockaddr_in)) {
                fprintf(stderr, "Received packet from non-IPv4 address\n");
                exit(EXIT_FAILURE);
            }

            struct sockaddr_in clnt_addr = *(struct sockaddr_in *) &clnt_addr_any;

            printf("> Received %ld bytes from %s:%d\n", (long) nread, inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));

            switch (packet.tag) {
                case JOIN: {
                    printf(">>> Received JOIN packet\n");

                    // check if client is already joined
                    for (uint16_t i = 0; i < *data->len; i++) {
                        if (SOCK_ADDR_IN_EQ(data->clnt_addrs[i], clnt_addr)) {
                            printf("Client sent JOIN packet but has already joined\n");
                            goto after_switch;
                        }
                    }

                    data->clnt_addrs[*data->len] = clnt_addr;
                    data->clnt_states[*data->len] = JOINING;
                    data->players[*data->len] = (Player) {
                        .id = rand(),
                        .x = 0,
                        .y = 0,
                    };
                    printf("Added player %u\n", data->players[*data->len].id);
                    (*data->len)++;
                } break;
                case POSITION: {
                    uint32_t id = ntohl(packet.p_player.id);

                    printf(">>> Received POSITION packet for player %u\n", id);
                    
                    // find client by id
                    for (uint16_t i = 0; i < *data->len; i++) {
                        printf("checked if client id %hu == %hu", data->players[i].id, id);

                        if (data->players[i].id == id) {
                            // TODO: It never gets here???

                            if (data->clnt_addrs[i].sin_addr.s_addr != clnt_addr.sin_addr.s_addr || !SOCK_ADDR_IN_EQ(data->clnt_addrs[i], clnt_addr)) {
                                printf("Client sent POSITION packet but has either wrong address or wrong id/port\n");
                                goto after_switch;
                            }
                            data->clnt_states[i] = PLAYING;

                            data->players[i].x = ntohl(packet.p_player.x);
                            data->players[i].y = ntohl(packet.p_player.y);
                            
                            printf("Updated player %u position to (%u, %u)\n", id, data->players[i].x, data->players[i].y);
                        }
                    }
                } break;
            }
            after_switch:

            // if first connection
            if (*data->len == 1) {
                // spawn server thread to broadcast packets
                if (pthread_create(&data->sender, NULL, (void*(*)(void*)) server_thread_sender, data)) {
                    fprintf(stderr, "Failed to create server sender thread: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }

            ready_count--;
        }
    }
}

void net_server_create(uint16_t const port, ServerData *const data) {

    // create server socket
    printf("creating server socket\n");
    data->serv_fd = socket(AF_INET, SOCK_DGRAM, PF_UNSPEC);
    if (data->serv_fd == -1) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    printf("created server socket = %d\n", data->serv_fd);

    // bind to specified port
    printf("binding server socket to port %d\n", (int) port);
    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(data->serv_fd, (struct sockaddr*) &serv_addr, sizeof (serv_addr))) {
        fprintf(stderr, "Failed to bind socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    printf("server socket bound to port %d\n", (int) ntohs(serv_addr.sin_port));

    // spawn server thread to receive packets
    if (pthread_create(&data->receiver, NULL, (void*(*)(void*)) server_thread_receiver, data)) {
        fprintf(stderr, "Failed to create server receiver thread: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

struct ClientData {
    clnt_state clnt_state;
    struct sockaddr_in serv_addr;
    Player *player;
    pthread_t sender;
    pthread_t receiver;
    int clnt_fd;
};

ClientData *net_client_data_new(Player *const player) {
    ClientData *data = malloc(sizeof (ClientData));
    if (data == NULL) {
        fprintf(stderr, "Failed to allocate client data\n");
        exit(EXIT_FAILURE);
    }

    *data = (ClientData) {
        .clnt_state = JOINING,
        //.serv_addr = (struct sockaddr_in) {0},
        .player = player,
        -1, -1, -1,
    };
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

    close(data->clnt_fd);
    free(data);
}

static noreturn void *client_thread_sender(ClientData *const data) {
    printf("starting client sender thread\n");

    C2SPacket packet;

    while (true) {
        fflush(stdout);
        //printf("send loop began\n");

        thrd_sleep(DELAY, NULL);

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(data->clnt_fd, &fds);
        int max_fd = data->clnt_fd;

        int ready_count = select(max_fd + 1, NULL, &fds, NULL, &(struct timeval) {.tv_sec = 1, .tv_usec = 0}); // blocks
        if (ready_count == -1) {
            fprintf(stderr, "Failed to select: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (ready_count == 0) {
            printf("send loop timed out\n");
            continue;
        }

        if (FD_ISSET(data->clnt_fd, &fds)) {
            size_t packet_size = sizeof (C2SPacket);

            switch (data->clnt_state) {
                case JOINING: {
                    packet_size = sizeof (PacketTag);
                    packet.tag = JOIN;

                    printf("<<< Sending JOIN packet to %s:%d\n", inet_ntoa(data->serv_addr.sin_addr), ntohs(data->serv_addr.sin_port));
                } break;
                case PLAYING: {
                    //packet_size = sizeof (C2SPacket);
                    packet.tag = POSITION;
                    packet.p_player.id = htonl(data->player->id);
                    packet.p_player.x = htonl(data->player->x);
                    packet.p_player.y = htonl(data->player->y);

                    printf("<<< Sending POSITION packet to %s:%d\n", inet_ntoa(data->serv_addr.sin_addr), ntohs(data->serv_addr.sin_port));
                } break;
            }

            ssize_t nwritten = sendto(data->clnt_fd, &packet, packet_size, 0, (struct sockaddr *) &data->serv_addr, sizeof data->serv_addr);
            if (nwritten == -1) {
                fprintf(stderr, "Failed to send to server: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }

            printf("< Send %ld bytes to %s:%d\n", (long) nwritten, inet_ntoa(data->serv_addr.sin_addr), ntohs(data->serv_addr.sin_port));
        }
    }
}

static noreturn void *client_thread_receiver(ClientData *const data) {
    printf("starting client receiver thread\n");

    uint16_t players_max = 0;
    uint16_t players_len = 0;
    S2CPacket *packet = malloc(sizeof (S2CPacket));

    while (true) {
        fflush(stdout);
        //printf("receive loop began\n");

        //thrd_sleep(DELAY, NULL);

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(data->clnt_fd, &fds);
        int max_fd = data->clnt_fd;

        int ready_count = select(max_fd + 1, &fds, NULL, NULL, &(struct timeval) {.tv_sec = 1, .tv_usec = 0}); // blocks
        if (ready_count == -1) {
            fprintf(stderr, "Failed to select: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (ready_count == 0) {
            printf("receive loop timed out\n");
            continue;
        }

        if (FD_ISSET(data->clnt_fd, &fds)) {
            struct sockaddr_storage serv_addr_any = {0};
            socklen_t serv_addr_any_len = sizeof serv_addr_any;
            ssize_t nread = recvfrom(data->clnt_fd, packet, sizeof (S2CPacket) + players_len * sizeof (Player), 0, (struct sockaddr *) &serv_addr_any, &serv_addr_any_len);
            if (nread == -1) {
                fprintf(stderr, "Failed to receive from server: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }

            if (serv_addr_any.ss_family != AF_INET || serv_addr_any_len != sizeof (struct sockaddr_in)) {
                fprintf(stderr, "Received packet from non-IPv4 address\n");
                exit(EXIT_FAILURE);
            }

            struct sockaddr_in serv_addr = *(struct sockaddr_in *) &serv_addr_any;

            printf("> Received %ld bytes from %s:%d\n", (long) nread, inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));

            switch (packet->tag) {
                case ACCEPT: {
                    printf(">>> Received ACCEPT packet with id %u\n", data->player->id);

                    if (data->clnt_state != JOINING) {
                        printf("Received ACCEPT packet but is not joining\n");
                        continue;
                    }
                    players_max = ntohs(packet->a_max);
                    data->player->id = ntohl(packet->a_id);
                    data->clnt_state = PLAYING;

                    free(packet);
                    packet = malloc(sizeof (S2CPacket) + players_max * sizeof (Player));
                } break;
                case POSITIONS: {
                    printf(">>> Received POSITIONS packet with %u players\n", players_len);

                    if (data->clnt_state != PLAYING) {
                        fprintf(stderr, "Received POSITIONS packet but is not playing\n");
                        exit(EXIT_FAILURE);
                    }
                    players_len = ntohs(packet->p_len);
                    // TODO: Do something with the data

                } break;
            }
        }
    }
}

void net_client_create(uint16_t port, ClientData *data) {

    // create client socket
    printf("creating client socket\n");
    data->clnt_fd = socket(PF_INET, SOCK_DGRAM, PF_UNSPEC);
    if (data->clnt_fd == -1) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    printf("client socket created = %d\n", data->clnt_fd);

    // connect to specified address
    printf("connecting client socket to port %d\n", (int) port);
    data->serv_addr = (struct sockaddr_in) {0};
    data->serv_addr.sin_family = AF_INET;
    data->serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &data->serv_addr.sin_addr);

    // // connect with SOCK_DGRAM sets the default address to send to and receive from
    // if (connect(data->clnt_fd, (struct sockaddr*) &data->serv_addr, sizeof (data->serv_addr))) {
    //     fprintf(stderr, "Failed to connect to server: %s\n", strerror(errno));
    //     exit(EXIT_FAILURE);
    // }
    // printf("client socket connected to port %d\n", (int) ntohs(data->serv_addr.sin_port));

    // spawn client thread to send packets
    if (pthread_create(&data->sender, NULL, (void*(*)(void*)) client_thread_sender, data)) {
        fprintf(stderr, "Failed to create client sender thread: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // spawn client thread to receive packets
    if (pthread_create(&data->receiver, NULL, (void*(*)(void*)) client_thread_receiver, data)) {
        fprintf(stderr, "Failed to create client receiver thread: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

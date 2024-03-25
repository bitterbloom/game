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
#include <time.h>

#include "./net.h"
#include "./arg.h"
#include "./util.h"

#define SOCK_ADDR_IN_EQ(a, b) (a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port)
#define SENDER_DELAY (&(struct timespec) {.tv_sec = 0, .tv_nsec = 35000000}) // 35 ms
#define DISCONNECT_TIMEOUT_SEC (5)

typedef enum {
    JOINING,
    REJOINING,
    PLAYING,
} clnt_state;

struct ServerData {
    uint16_t max;
    uint16_t *len;
    pthread_mutex_t len_mutex;
    clnt_state *clnt_states;
    time_t *clnt_last;
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
        REJOIN,
        POSITION,
        // LEAVE,
    } tag;
    union {
        struct { // Position
            point p_pos; // TODO: Clients shouldn't need to send their player id.
        };
        struct { // Rejoin
            Player r_player;
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
    if (max_players == 0)
        EXIT_PRINT("Player list must have at least one player\n");

    ServerData *data = malloc(sizeof (ServerData));
    if (data == NULL)
        EXIT_PRINT("Failed to allocate server data\n");

    if (pthread_mutex_init(&data->len_mutex, NULL))
        EXIT_PRINT("Failed to initialize player list mutex: %s\n", strerror(errno));

    clnt_state *const clnt_states = malloc(max_players * sizeof (clnt_state));
    if (clnt_states == NULL)
        EXIT_PRINT("Failed to allocate client states\n");

    time_t *const clnt_last = malloc(max_players * sizeof (time_t));
    if (clnt_last == NULL)
        EXIT_PRINT("Failed to allocate client last times\n");

    struct sockaddr_in *const clnt_addrs = malloc(max_players * sizeof (struct sockaddr_storage));
    if (clnt_addrs == NULL)
        EXIT_PRINT("Failed to allocate client addresses\n");

    *data = (ServerData) {
        .max = max_players,
        .len = len_players,
        .clnt_states = clnt_states,
        .clnt_last = clnt_last,
        .clnt_addrs = clnt_addrs,
        .players = players,
        -1, -1, -1,
    };
    return data;
}

void net_server_data_delete(ServerData *const data) {
    if (data->sender != -1) {
        if (pthread_cancel(data->sender))
            EXIT_PRINT("Failed to send cancel request to server sender thread: %s\n", strerror(errno));
    }

    if (data->receiver != -1) {
        if (pthread_cancel(data->receiver))
            EXIT_PRINT("Failed to send cancel request to server receiver thread: %s\n", strerror(errno));
    }

    pthread_mutex_destroy(&data->len_mutex);
    free(data->clnt_states);
    free(data->clnt_last);
    free(data->clnt_addrs);
    close(data->serv_fd);
    free(data);
}

static void *server_thread_sender(ServerData *const data) {
    printf("starting server sender thread\n");

    S2CPacket *packet = malloc(sizeof (S2CPacket) + data->max * sizeof (Player));
    size_t next_client = 0;

    while (true) {
        fflush(stdout);

        thrd_sleep(SENDER_DELAY, NULL);

        /* Check if any clients have disconnected */ {
            struct timespec ts;
            if (clock_gettime(CLOCK_MONOTONIC, &ts))
                EXIT_PRINT("Failed to get time: %s\n", strerror(errno));

            for (uint16_t i = 0; i < *data->len; i++) {
                if (ts.tv_sec - data->clnt_last[i] > DISCONNECT_TIMEOUT_SEC) {
                    printf("Client %s:%d has timed out\n", inet_ntoa(data->clnt_addrs[i].sin_addr), ntohs(data->clnt_addrs[i].sin_port));

                    pthread_mutex_lock(&data->len_mutex);
                    uint16_t len = *data->len;
                    if (i != len - 1) {
                        data->clnt_states[i] = data->clnt_states[len - 1];
                        data->clnt_last[i]   = data->clnt_last[len - 1];
                        data->clnt_addrs[i]  = data->clnt_addrs[len - 1];
                        data->players[i]     = data->players[len - 1];
                    }

                    *data->len = --len;
                    pthread_mutex_unlock(&data->len_mutex);
                    i--;
                }
            }

            if (*data->len == 0) {
                // The `*data->len` being 0 is unlikely and locking the mutex is relatively expensive.
                // So we check it first and then lock and check again.
                pthread_mutex_lock(&data->len_mutex);
                if (*data->len == 0) {
                    printf("All clients have disconnected\n");
                    break; // the lock will be released when the function returns
                }
                pthread_mutex_unlock(&data->len_mutex);
            }
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(data->serv_fd, &fds);
        int const max_fd = data->serv_fd;

        int ready_count = select(max_fd + 1, NULL, &fds, NULL, &(struct timeval) {.tv_sec = 1, .tv_usec = 0}); // blocks

        if (ready_count == -1)
            EXIT_PRINT("Failed to select: %s\n", strerror(errno));

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

                    DEBUG_PRINT("<<< Sending ACCEPT packet to %s:%d\n", inet_ntoa(data->clnt_addrs[next_client].sin_addr), ntohs(data->clnt_addrs[next_client].sin_port));
                } break;
                case REJOINING: {
                    // The JOINING state exists so that the server knows it needs to send ACCEPT packets with the player id.
                    // But when rejoining, the client already knows its id, so the server can just send the POSITIONS packets.
                    // We therefore don't need to store the REJOINING state on the server.
                    EXIT_PRINT("Client should not be in REJOINING state on the server\n");
                } break;
                case PLAYING: {
                    packet->tag = POSITIONS;
                    packet->p_len = htons(*data->len);

                    for (uint16_t i = 0; i < *data->len; i++) {
                        packet->p_players[i] = (Player) {
                            .id = htonl(data->players[i].id),
                            .pos.x = htonl(data->players[i].pos.x),
                            .pos.y = htonl(data->players[i].pos.y),
                        };
                    }

                    DEBUG_PRINT("<<< Sending POSITIONS packet to %s:%d\n", inet_ntoa(data->clnt_addrs[next_client].sin_addr), ntohs(data->clnt_addrs[next_client].sin_port));
                } break;
            }

            struct sockaddr_in clnt_addr = data->clnt_addrs[next_client];
            ssize_t nwritten = sendto(data->serv_fd, packet, sizeof (S2CPacket), 0, (struct sockaddr *) &clnt_addr, sizeof clnt_addr);
            if (nwritten == -1)
                EXIT_PRINT("Failed to send to client: %s\n", strerror(errno));

            DEBUG_PRINT("< Send %ld bytes to %s:%d\n", (long) nwritten, inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));
        }

        next_client = (next_client + 1) % *data->len;
    }

    // never reached
    // TODO: Maybe give some sort of signal to this thread to indicate to stop the loop
    // when the server is shutting down.
    free(packet);
    
    pthread_mutex_unlock(&data->len_mutex); // from the `len == 0` check

    return NULL;
}

static noreturn void *server_thread_receiver(ServerData *const data) {
    printf("starting server receiver thread\n");

    if (*data->len != 0)
        EXIT_PRINT("Player list must be empty\n");

    if (data->max > FD_SETSIZE)
        EXIT_PRINT("Player list is too large\n");

    C2SPacket packet;

    uint32_t next_id = 0;

next_loop:
    while (true) {
        fflush(stdout);

        //thrd_sleep(DELAY, NULL);

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(data->serv_fd, &fds);
        int max_fd = data->serv_fd;

        int ready_count = select(max_fd + 1, &fds, NULL, NULL, &(struct timeval) {.tv_sec = 1, .tv_usec = 0}); // blocks
        if (ready_count == -1)
            EXIT_PRINT("Failed to select: %s\n", strerror(errno));

        if (ready_count == 0) {
            printf("receive loop timed out\n");
            continue;
        }

        if (FD_ISSET(data->serv_fd, &fds)) {
            struct sockaddr_storage clnt_addr_any = {0};
            socklen_t clnt_addr_any_len = sizeof clnt_addr_any;
            ssize_t nread = recvfrom(data->serv_fd, &packet, sizeof (C2SPacket), 0, (struct sockaddr *) &clnt_addr_any, &clnt_addr_any_len);
            if (nread == -1)
                EXIT_PRINT("Failed to receive from client: %s\n", strerror(errno));

            if (clnt_addr_any.ss_family != AF_INET || clnt_addr_any_len != sizeof (struct sockaddr_in))
                EXIT_PRINT("Received packet from non-IPv4 address\n");

            struct sockaddr_in clnt_addr = *(struct sockaddr_in *) &clnt_addr_any;

            DEBUG_PRINT("> Received %ld bytes from %s:%d\n", (long) nread, inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));

            switch (packet.tag) {
                case JOIN: {
                    DEBUG_PRINT(">>> Received JOIN packet\n");

                    if (*data->len == data->max) {
                        printf("Client sent JOIN packet but server is full\n");
                        goto next_loop;
                    }

                    uint16_t len = *data->len;
                    for (uint16_t i = 0; i < len; i++) { // len might have been decremented by the sender thread, but that's okay.
                        if (SOCK_ADDR_IN_EQ(data->clnt_addrs[i], clnt_addr)) {
                            printf("Client sent JOIN packet but has already joined\n");
                            goto next_loop;
                        }
                    }

                    struct timespec ts;
                    if (clock_gettime(CLOCK_MONOTONIC, &ts))
                        EXIT_PRINT("Failed to get time: %s\n", strerror(errno));

                    pthread_mutex_lock(&data->len_mutex);
                    len = *data->len; // in case it was changed

                    data->clnt_addrs[len] = clnt_addr;
                    data->clnt_last[len] = ts.tv_sec;
                    data->clnt_states[len] = JOINING;
                    data->players[len] = (Player) {
                        .id = next_id++,//rand(),
                        .pos.x = 0,
                        .pos.y = 0,
                    };
                    *data->len = len + 1;
                    pthread_mutex_unlock(&data->len_mutex);

                    printf("Added player %u\n", data->players[len].id);

                } break;
                case REJOIN: {
                    uint32_t id = ntohl(packet.r_player.id);

                    DEBUG_PRINT(">>> Received REJOIN packet\n");

                    if (*data->len == data->max) {
                        printf("Client sent REJOIN packet but server is full\n");
                        goto next_loop;
                    }

                    uint16_t len = *data->len;
                    for (uint16_t i = 0; i < len; i++) {
                        if (data->players[i].id == id) {
                            if (!SOCK_ADDR_IN_EQ(data->clnt_addrs[i], clnt_addr)) {
                                printf("Client sent REJOIN packet but is already joined with a different address\n");
                                goto next_loop;
                            }

                            printf("Client sent REJOIN packet and is already joined\n");
                            goto next_loop;
                        }
                    }

                    struct timespec ts;
                    if (clock_gettime(CLOCK_MONOTONIC, &ts))
                        EXIT_PRINT("Failed to get time: %s\n", strerror(errno));

                    pthread_mutex_lock(&data->len_mutex);
                    len = *data->len; // in case it was changed

                    data->clnt_addrs[len] = clnt_addr;
                    data->clnt_last[len] = ts.tv_sec;
                    data->clnt_states[len] = PLAYING; // We don't need to send ACCEPT packets, so just go straight to PLAYING.
                    data->players[len] = (Player) {
                        .id = id,
                        .pos.x = ntohl(packet.r_player.pos.x),
                        .pos.y = ntohl(packet.r_player.pos.y),
                    };
                    *data->len = len + 1;
                    pthread_mutex_unlock(&data->len_mutex);

                    printf("Rejoined player %u\n", id);
                } break;
                case POSITION: {
                    uint16_t len = *data->len;
                    for (uint16_t i = 0; i < len; i++) {
                        if (SOCK_ADDR_IN_EQ(data->clnt_addrs[i], clnt_addr)) {
                            uint32_t id = data->players[i].id;

                            DEBUG_PRINT(">>> Received POSITION packet for player %u\n", id);
                    
                            struct timespec ts;
                            if (clock_gettime(CLOCK_MONOTONIC, &ts))
                                EXIT_PRINT("Failed to get time: %s\n", strerror(errno));

                            // If the sender thread disconnects the client,
                            // then this will set the values to the wrong client.
                            // TODO: Is this a problem?

                            data->clnt_last[i] = ts.tv_sec;
                            data->clnt_states[i] = PLAYING;
                            data->players[i].pos.x = ntohl(packet.p_pos.x);
                            data->players[i].pos.y = ntohl(packet.p_pos.y);
                            
                            DEBUG_PRINT("Updated player %u position to (%u, %u)\n", id, data->players[i].pos.x, data->players[i].pos.y);
                        }
                    }

                    goto next_loop;
                } break;
            }

            // if first connection
            if (*data->len == 1) {
                if (pthread_create(&data->sender, NULL, (void*(*)(void*)) server_thread_sender, data))
                    EXIT_PRINT("Failed to create server sender thread: %s\n", strerror(errno));
            }

            ready_count--;
        }
    }
}

void net_server_create(uint16_t const port, ServerData *const data) {
    printf("creating server socket\n");
    data->serv_fd = socket(AF_INET, SOCK_DGRAM, PF_UNSPEC);
    if (data->serv_fd == -1)
        EXIT_PRINT("Failed to create socket: %s\n", strerror(errno));
    printf("created server socket = %d\n", data->serv_fd);

    printf("binding server socket to port %d\n", (int) port);
    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(data->serv_fd, (struct sockaddr*) &serv_addr, sizeof (serv_addr)))
        EXIT_PRINT("Failed to bind socket: %s\n", strerror(errno));
    printf("server socket bound to port %d\n", (int) ntohs(serv_addr.sin_port));

    if (pthread_create(&data->receiver, NULL, (void*(*)(void*)) server_thread_receiver, data))
        EXIT_PRINT("Failed to create server receiver thread: %s\n", strerror(errno));
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
    if (data == NULL)
        EXIT_PRINT("Failed to allocate client data\n");

    *data = (ClientData) {
        .clnt_state = JOINING,
        //.serv_addr = (struct sockaddr_in) {0},
        .player = player,
        -1, -1, -1,
    };
    return data;
}

void net_client_data_delete(ClientData *const data) {
    if (data->sender != -1) {
        if (pthread_cancel(data->sender))
            EXIT_PRINT("Failed to send cancel request to client sender thread: %s\n", strerror(errno));
    }

    if (data->receiver != -1) {
        if (pthread_cancel(data->receiver))
            EXIT_PRINT("Failed to send cancel request to client receiver thread: %s\n", strerror(errno));
    }

    close(data->clnt_fd);
    free(data);
}

static noreturn void *client_thread_sender(ClientData *const data) {
    printf("starting client sender thread\n");

    C2SPacket packet;

    while (true) {
        fflush(stdout);

        thrd_sleep(SENDER_DELAY, NULL);

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(data->clnt_fd, &fds);
        int max_fd = data->clnt_fd;

        int ready_count = select(max_fd + 1, NULL, &fds, NULL, &(struct timeval) {.tv_sec = 1, .tv_usec = 0}); // blocks
        if (ready_count == -1)
            EXIT_PRINT("Failed to select: %s\n", strerror(errno));

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

                    DEBUG_PRINT("<<< Sending JOIN packet to %s:%d\n", inet_ntoa(data->serv_addr.sin_addr), ntohs(data->serv_addr.sin_port));
                } break;
                case REJOINING: {
                    packet_size = sizeof (C2SPacket);
                    packet.tag = REJOIN;
                    packet.r_player = (Player) {
                        .id = htonl(data->player->id),
                        .pos.x = htonl(data->player->pos.x),
                        .pos.y = htonl(data->player->pos.y),
                    };

                    DEBUG_PRINT("<<< Sending REJOIN packet to %s:%d\n", inet_ntoa(data->serv_addr.sin_addr), ntohs(data->serv_addr.sin_port));
                } break;
                case PLAYING: {
                    //packet_size = sizeof (C2SPacket);
                    packet.tag = POSITION;
                    packet.p_pos.x = htonl(data->player->pos.x);
                    packet.p_pos.y = htonl(data->player->pos.y);

                    DEBUG_PRINT("<<< Sending POSITION packet to %s:%d\n", inet_ntoa(data->serv_addr.sin_addr), ntohs(data->serv_addr.sin_port));
                } break;
            }

            ssize_t nwritten = sendto(data->clnt_fd, &packet, packet_size, 0, (struct sockaddr *) &data->serv_addr, sizeof data->serv_addr);
            if (nwritten == -1)
                EXIT_PRINT("Failed to send to server: %s\n", strerror(errno));

            DEBUG_PRINT("< Send %ld bytes to %s:%d\n", (long) nwritten, inet_ntoa(data->serv_addr.sin_addr), ntohs(data->serv_addr.sin_port));
        }
    }
}

static noreturn void *client_thread_receiver(ClientData *const data) {
    printf("starting client receiver thread\n");

    uint16_t players_max = 0;
    uint16_t players_len = 0;
    S2CPacket *packet = malloc(sizeof (S2CPacket));

next_loop:
    while (true) {
        fflush(stdout);

        //thrd_sleep(DELAY, NULL);

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(data->clnt_fd, &fds);
        int max_fd = data->clnt_fd;

        int ready_count = select(max_fd + 1, &fds, NULL, NULL, &(struct timeval) {.tv_sec = DISCONNECT_TIMEOUT_SEC, .tv_usec = 0}); // blocks
        if (ready_count == -1)
            EXIT_PRINT("Failed to select: %s\n", strerror(errno));

        if (ready_count == 0) {
            printf("receive loop timed out\n");
            data->clnt_state = REJOINING;
            goto next_loop;
        }

        if (FD_ISSET(data->clnt_fd, &fds)) {
            struct sockaddr_storage serv_addr_any = {0};
            socklen_t serv_addr_any_len = sizeof serv_addr_any;
            ssize_t nread = recvfrom(data->clnt_fd, packet, sizeof (S2CPacket) + players_len * sizeof (Player), 0, (struct sockaddr *) &serv_addr_any, &serv_addr_any_len);
            if (nread == -1)
                EXIT_PRINT("Failed to receive from server: %s\n", strerror(errno));

            if (serv_addr_any.ss_family != AF_INET || serv_addr_any_len != sizeof (struct sockaddr_in))
                EXIT_PRINT("Received packet from non-IPv4 address\n");

            struct sockaddr_in serv_addr = *(struct sockaddr_in *) &serv_addr_any;

            DEBUG_PRINT("> Received %ld bytes from %s:%d\n", (long) nread, inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));

            switch (packet->tag) {
                case ACCEPT: {
                    DEBUG_PRINT(">>> Received ACCEPT packet with id %u\n", data->player->id);

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
                    DEBUG_PRINT(">>> Received POSITIONS packet with %u players\n", players_len);

                    if (data->clnt_state != PLAYING && data->clnt_state != REJOINING)
                        EXIT_PRINT("Received POSITIONS packet but is not playing or rejoining\n");

                    data->clnt_state = PLAYING;

                    players_len = ntohs(packet->p_len);

                    // TODO: Do something with the data
                } break;
            }
        }
    }

    // never reached
    free(packet);
}

void net_client_create(uint16_t port, ClientData *data) {
    printf("creating client socket\n");
    data->clnt_fd = socket(PF_INET, SOCK_DGRAM, PF_UNSPEC);
    if (data->clnt_fd == -1)
        EXIT_PRINT("Failed to create socket: %s\n", strerror(errno));
    printf("client socket created = %d\n", data->clnt_fd);

    printf("connecting client socket to port %d\n", (int) port);
    data->serv_addr = (struct sockaddr_in) {0};
    data->serv_addr.sin_family = AF_INET;
    data->serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &data->serv_addr.sin_addr);

    if (pthread_create(&data->sender, NULL, (void*(*)(void*)) client_thread_sender, data))
        EXIT_PRINT("Failed to create client sender thread: %s\n", strerror(errno));

    if (pthread_create(&data->receiver, NULL, (void*(*)(void*)) client_thread_receiver, data))
        EXIT_PRINT("Failed to create client receiver thread: %s\n", strerror(errno));
}

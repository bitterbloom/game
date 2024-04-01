#ifdef _WIN64
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef __linux__
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <threads.h>
#include <unistd.h>
#include <netdb.h>
#include <poll.h>

#include <fcntl.h>
#include <time.h>

#elif defined(_WIN64)
#include <Ws2tcpip.h>
#endif

#include <stdnoreturn.h> // must go after windows header files...

#include "./os/sockets.h"
#include "./os/threads.h"
#include "./net.h"
#include "./util.h"

#define SOCK_ADDR_IN_EQ(a, b) (a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port)
#define DISCONNECT_TIMEOUT (5000) // milliseconds
#define SENDER_DELAY (35)         // milliseconds
#define POLL_TIMEOUT (1000)       // milliseconds

// TODO: Once this code becomes more stable, consolidate the linux and windows code,
//       either by using macros or by abstracting over the os-specific code.

// TODO: Make the error checks more specific, i.e. use `if (function() == NULL)`
//       rather than `if (!function())`

typedef enum {
    JOINING,
    REJOINING,
    PLAYING,
} clnt_state;

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

struct Server {
    uint16_t max;
    uint16_t *len;
    Mutex len_mutex;
    clnt_state *clnt_states;
    long *clnt_last; // milliseconds
    Address *clnt_addrs;
    Player *players;
    Thread sender;
    Thread receiver;
    bool should_stop;
    Socket serv_fd;
};

struct Client {
    clnt_state clnt_state;
    Address serv_addr;
    Player *player;
    Thread sender;
    Thread receiver;
    bool should_stop;
    Socket clnt_fd;
};

#if !defined(__linux__) || !defined(HOTRELOADING)
static void server_thread_sender(Server *const data) {
    printf("starting server sender thread\n");

    S2CPacket *packet = malloc(sizeof (S2CPacket) + data->max * sizeof (Player));
    size_t next_client = 0;

    while (true) {
        if (data->should_stop) break;

        fflush(stdout);

        thread_sleep_ms(SENDER_DELAY);

        /* Check if any clients have disconnected */ {
            long now;
            if (!time_get_monotonic(&now))
                EXIT_PRINT("Failed to get time: %s", threads_get_error());

            for (uint16_t i = 0; i < *data->len; i++) {
                if (now - data->clnt_last[i] > DISCONNECT_TIMEOUT) {
                    printf("Client %s:%d has timed out\n", inet_ntoa(data->clnt_addrs[i].sin_addr), ntohs(data->clnt_addrs[i].sin_port));

                    mutex_lock(&data->len_mutex);
                    uint16_t len = *data->len;
                    if (i != len - 1) {
                        data->clnt_states[i] = data->clnt_states[len - 1];
                        data->clnt_last[i]   = data->clnt_last[len - 1];
                        data->clnt_addrs[i]  = data->clnt_addrs[len - 1];
                        data->players[i]     = data->players[len - 1];
                    }

                    *data->len = --len;
                    mutex_unlock(&data->len_mutex);
                    i--;
                }
            }

            if (*data->len == 0) {
                // The `*data->len` being 0 is unlikely and locking the mutex is relatively expensive.
                // So we check it first and then lock and check again.
                mutex_lock(&data->len_mutex);
                if (*data->len == 0) {
                    printf("All clients have disconnected\n");
                    break; // the lock will be released when the function returns
                }
                mutex_unlock(&data->len_mutex);
            }
        }

        short ev;
        if (!socket_poll(data->serv_fd, POLLOUT, &ev, POLL_TIMEOUT))
            EXIT_PRINT("Failed to poll for write on server socket: %s", sockets_get_error());

        if (ev == 0) {
            printf("send loop timed out\n");
            continue;
        }

        switch (data->clnt_states[next_client]) {
            case JOINING: {
                packet->tag = ACCEPT;
                packet->a_max = htons(data->max);
                packet->a_id = htonl(data->players[next_client].id);

                DEBUG_PRINT("<<< Sending ACCEPT packet to %s:%d", inet_ntoa(data->clnt_addrs[next_client].sin_addr), ntohs(data->clnt_addrs[next_client].sin_port));
            } break;
            case REJOINING: {
                // The JOINING state exists so that the server knows it needs to send ACCEPT packets with the player id.
                // But when rejoining, the client already knows its id, so the server can just send the POSITIONS packets.
                // We therefore don't need to store the REJOINING state on the server.
                EXIT_PRINT("Client should not be in REJOINING state on the server");
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

                DEBUG_PRINT("<<< Sending POSITIONS packet to %s:%d", inet_ntoa(data->clnt_addrs[next_client].sin_addr), ntohs(data->clnt_addrs[next_client].sin_port));
            } break;
        }

        Address *clnt_addr = &data->clnt_addrs[next_client];
        if (!socket_sendto_inet(data->serv_fd, packet, sizeof (S2CPacket), clnt_addr))
            EXIT_PRINT("Failed to send to client: %s", sockets_get_error());

        DEBUG_PRINT("< Send %llu bytes to %s:%d", sizeof (S2CPacket), inet_ntoa(clnt_addr->sin_addr), ntohs(clnt_addr->sin_port));

        next_client = (next_client + 1) % *data->len;
    }

    printf("stopping server sender thread\n");

    free(packet);
    
    mutex_unlock(&data->len_mutex); // from the `len == 0` check
}

static void server_thread_receiver(Server *const data) {
    printf("starting server receiver thread\n");

    if (*data->len != 0)
        EXIT_PRINT("Player list must be empty");

    C2SPacket packet;

    uint32_t next_id = 0;

next_loop:
    while (true) {
        if (data->should_stop) break;

        fflush(stdout);

        short ev;
        if (!socket_poll(data->serv_fd, POLLIN, &ev, POLL_TIMEOUT))
            EXIT_PRINT("Failed to poll for read on server socket: %s", sockets_get_error());

        if (ev == 0) {
            printf("receive loop timed out\n");
            continue;
        }

        Address clnt_addr;
        int nread;
        if (!socket_recvfrom_inet(data->serv_fd, &packet, sizeof (C2SPacket), &nread, &clnt_addr))
            EXIT_PRINT("Failed to receive from client: %s", sockets_get_error());

        DEBUG_PRINT("> Received %ld bytes from %s:%d", (long) nread, inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));

        switch (packet.tag) {
            case JOIN: {
                DEBUG_PRINT(">>> Received JOIN packet");

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

                long now;
                if (!time_get_monotonic(&now))
                    EXIT_PRINT("Failed to get time: %s", threads_get_error());

                mutex_lock(&data->len_mutex);
                len = *data->len; // in case it was changed

                data->clnt_addrs[len]  = clnt_addr;
                data->clnt_last[len]   = now;
                data->clnt_states[len] = JOINING;
                data->players[len]     = (Player) {
                    .id = next_id++,//rand(),
                    .pos.x = 0,
                    .pos.y = 0,
                };
                *data->len = len + 1;
                mutex_unlock(&data->len_mutex);

                printf("Added player %u\n", data->players[len].id);
            } break;
            case REJOIN: {
                uint32_t id = ntohl(packet.r_player.id);

                DEBUG_PRINT(">>> Received REJOIN packet");

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

                long now;
                if (!time_get_monotonic(&now))
                    EXIT_PRINT("Failed to get time: %s", threads_get_error());

                mutex_lock(&data->len_mutex);
                len = *data->len; // in case it was changed

                data->clnt_addrs[len]  = clnt_addr;
                data->clnt_last[len]   = now;
                data->clnt_states[len] = PLAYING; // We don't need to send ACCEPT packets, so just go straight to PLAYING.
                data->players[len]     = (Player) {
                    .id = id,
                    .pos.x = ntohl(packet.r_player.pos.x),
                    .pos.y = ntohl(packet.r_player.pos.y),
                };
                *data->len = len + 1;
                mutex_unlock(&data->len_mutex);

                printf("Rejoined player %u\n", id);
            } break;
            case POSITION: {
                uint16_t len = *data->len;
                for (uint16_t i = 0; i < len; i++) {
                    if (SOCK_ADDR_IN_EQ(data->clnt_addrs[i], clnt_addr)) {
                        uint32_t id = data->players[i].id;

                        DEBUG_PRINT(">>> Received POSITION packet for player %u", id);
                
                        long now;
                        if (!time_get_monotonic(&now))
                            EXIT_PRINT("Failed to get time: %s", threads_get_error());

                        // If the sender thread disconnects the client,
                        // then this will set the values to the wrong client.
                        // TODO: Is this a problem?

                        data->clnt_last[i]   = now;
                        data->clnt_states[i] = PLAYING;
                        data->players[i].pos.x = ntohl(packet.p_pos.x);
                        data->players[i].pos.y = ntohl(packet.p_pos.y);
                        
                        DEBUG_PRINT("Updated player %u position to (%u, %u)", id, data->players[i].pos.x, data->players[i].pos.y);
                    }
                }

                goto next_loop;
            } break;
        }

        // if first connection
        if (*data->len == 1) {
            if (!thread_spawn(&data->sender, (void (*)(void *)) server_thread_sender, data))
                EXIT_PRINT("Failed to create server sender thread: %s", threads_get_error());
        }
    }

    printf("stopping server receriver thread\n");
}

Server *net_server_spawn(Player *const players, uint16_t *const len_players, uint16_t const max_players, uint16_t const port) {
    if (max_players == 0)
        EXIT_PRINT("Player list must have at least one player");

    Server *const data = malloc(sizeof (Server));

    data->max = max_players;
    data->len = len_players;
    data->players = players;

    if (!mutex_init(&data->len_mutex))
        EXIT_PRINT("Failed to initialize player list mutex: %s", threads_get_error());

    data->clnt_states = malloc(max_players * sizeof (clnt_state));
    data->clnt_last   = malloc(max_players * sizeof (time_t));
    data->clnt_addrs  = malloc(max_players * sizeof (Address ));

    if (!socket_startup())
        EXIT_PRINT("Failed to start up socket code: %s", sockets_get_error());

    printf("creating server socket\n");
    if (!socket_init_udp(&data->serv_fd))
        EXIT_PRINT("Failed to create socket: %s", sockets_get_error());

    printf("binding server socket to port %d\n", (int) port);
    Address serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (!socket_bind(data->serv_fd,  &serv_addr))
        EXIT_PRINT("Failed to bind socket: %s", sockets_get_error());
    printf("server socket bound to port %d\n", (int) ntohs(serv_addr.sin_port));

    data->should_stop = false;

    if (!thread_spawn(&data->receiver, (void (*)(void *)) server_thread_receiver, data))
        EXIT_PRINT("Failed to create server receiver thread: %s", threads_get_error());

    data->sender = THREAD_NULL;

    return data;
}

void net_server_close(Server *const data) {
    data->should_stop = true;

    if (!thread_is_null(data->sender) && !thread_join(data->sender))
        EXIT_PRINT("Failed to send cancel request to server sender thread: %s", threads_get_error());

    if (!thread_is_null(data->receiver) && !thread_join(data->receiver))
        EXIT_PRINT("Failed to send cancel request to server receiver thread: %s", threads_get_error());

    if (!mutex_close(&data->len_mutex))
        EXIT_PRINT("Failed to destroy server mutex: %s", threads_get_error());

    if (!socket_close(data->serv_fd))
        EXIT_PRINT("Failed to close server socket: %s", threads_get_error());

    if (!socket_cleanup())
        EXIT_PRINT("Failed to clean up socket code: %s", sockets_get_error());

    free(data->clnt_states);
    free(data->clnt_last);
    free(data->clnt_addrs);
    free(data);
}

static void client_thread_sender(Client *const data) {
    printf("starting client sender thread\n");

    C2SPacket packet;

    while (true) {
        if (data->should_stop) break;

        fflush(stdout);

        thread_sleep_ms(SENDER_DELAY);

        short ev;
        if (!socket_poll(data->clnt_fd, POLLOUT, &ev, POLL_TIMEOUT))
            EXIT_PRINT("Failed to poll for write on client socket: %s", sockets_get_error());

        if (ev == 0) {
            printf("send loop timed out\n");
            continue;
        }

        size_t packet_size = sizeof (C2SPacket);

        switch (data->clnt_state) {
            case JOINING: {
                packet_size = sizeof (PacketTag);
                packet.tag = JOIN;

                DEBUG_PRINT("<<< Sending JOIN packet to %s:%d", inet_ntoa(data->serv_addr.sin_addr), ntohs(data->serv_addr.sin_port));
            } break;
            case REJOINING: {
                packet_size = sizeof (C2SPacket);
                packet.tag = REJOIN;
                packet.r_player = (Player) {
                    .id = htonl(data->player->id),
                    .pos.x = htonl(data->player->pos.x),
                    .pos.y = htonl(data->player->pos.y),
                };

                DEBUG_PRINT("<<< Sending REJOIN packet to %s:%d", inet_ntoa(data->serv_addr.sin_addr), ntohs(data->serv_addr.sin_port));
            } break;
            case PLAYING: {
                //packet_size = sizeof (C2SPacket);
                packet.tag = POSITION;
                packet.p_pos.x = htonl(data->player->pos.x);
                packet.p_pos.y = htonl(data->player->pos.y);

                DEBUG_PRINT("<<< Sending POSITION packet to %s:%d", inet_ntoa(data->serv_addr.sin_addr), ntohs(data->serv_addr.sin_port));
            } break;
        }

        if (!socket_sendto_inet(data->clnt_fd, &packet, packet_size, &data->serv_addr))
            EXIT_PRINT("Failed to send to server: %s", sockets_get_error());

        DEBUG_PRINT("< Send %zu bytes to %s:%d", packet_size, inet_ntoa(data->serv_addr.sin_addr), ntohs(data->serv_addr.sin_port));
    }

    printf("stopping client sender thread\n");
}

static void client_thread_receiver(Client *const data) {
    printf("starting client receiver thread\n");

    uint16_t players_max = 0;
    uint16_t players_len = 0;
    S2CPacket *packet = malloc(sizeof (S2CPacket));

next_loop:
    while (true) {
        if (data->should_stop) break;

        fflush(stdout);

        short ev;
        if (!socket_poll(data->clnt_fd, POLLIN, &ev, DISCONNECT_TIMEOUT))
            EXIT_PRINT("Failed to poll for read on client socket: %s", sockets_get_error());

        if (ev == 0) {
            printf("receive loop timed out\n");
            data->clnt_state = REJOINING;
            goto next_loop;
        }

        Address serv_addr;
        int nread;
        if (!socket_recvfrom_inet(data->clnt_fd, packet, sizeof (S2CPacket) + players_len * sizeof (Player), &nread, &serv_addr))
            EXIT_PRINT("Failed to receive from server: %s", sockets_get_error());

        DEBUG_PRINT("> Received %ld bytes from %s:%d", (long) nread, inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));

        switch (packet->tag) {
            case ACCEPT: {
                DEBUG_PRINT(">>> Received ACCEPT packet with id %u", data->player->id);

                if (data->clnt_state != JOINING) {
                    printf("Received ACCEPT packet but is not joining\n");
                    continue;
                }
                players_max = ntohs(packet->a_max);
                data->player->id = ntohl(packet->a_id);
                data->clnt_state = PLAYING;

                packet = realloc(packet, sizeof (S2CPacket) + players_max * sizeof (Player));
            } break;
            case POSITIONS: {
                DEBUG_PRINT(">>> Received POSITIONS packet with %u players", players_len);

                if (data->clnt_state != PLAYING && data->clnt_state != REJOINING)
                    EXIT_PRINT("Received POSITIONS packet but is not playing or rejoining");

                data->clnt_state = PLAYING;

                players_len = ntohs(packet->p_len);

                // TODO: Do something with the data
            } break;
        }
    }

    printf("stopping client receiver thread\n");

    free(packet);
}

Client *net_client_spawn(Player *const player, uint16_t const port) {
    Client *data = malloc(sizeof (Client));

    data->clnt_state = JOINING;
    data->player     = player;

    if (!socket_startup())
        EXIT_PRINT("Failed to start up socket code: %s", sockets_get_error());

    printf("creating client socket\n");
    if (!socket_init_udp(&data->clnt_fd))
        EXIT_PRINT("Failed to create socket: %s", sockets_get_error());

    printf("setting server address to port %d\n", (int) port);
    data->serv_addr = (Address ) {0};
    data->serv_addr.sin_family = AF_INET;
    data->serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &data->serv_addr.sin_addr);

    data->should_stop = false;

    if (!thread_spawn(&data->sender, (void (*)(void *)) client_thread_sender, data))
        EXIT_PRINT("Failed to create client sender thread: %s", threads_get_error());

    if (!thread_spawn(&data->receiver, (void (*)(void *)) client_thread_receiver, data))
        EXIT_PRINT("Failed to create client receiver thread: %s", threads_get_error());

    return data;
}

void net_client_close(Client *const data) {
    data->should_stop = true;

    if (!thread_is_null(data->sender) && !thread_join(data->sender))
        EXIT_PRINT("Failed to send cancel request to client sender thread: %s", threads_get_error());

    if (!thread_is_null(data->receiver) && !thread_join(data->receiver))
        EXIT_PRINT("Failed to send cancel request to client receiver thread: %s", threads_get_error());

    if (!socket_close(data->clnt_fd))
        EXIT_PRINT("Failed to close client socket: %s", sockets_get_error());

    if (!socket_cleanup())
        EXIT_PRINT("Failed to clean up socket code: %s", sockets_get_error());

    free(data);
}
#endif

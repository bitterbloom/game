#pragma once

#include "player.h"

typedef struct Server Server;

#if !defined(HOTRELOADING) || !defined(__linux__)
Server *net_server_spawn(Player *players, uint16_t *len_players, uint16_t max_players, uint16_t port);

void net_server_close(Server *data);
#else
typedef ServerData *net_server_spawn_t(Player *players, uint16_t *len_players, uint16_t max_players, uint16_t);

typedef void net_server_close_t(ServerData *data);
#endif

typedef struct Client Client;

#if !defined(HOTRELOADING) || !defined(__linux__)
Client *net_client_spawn(Player *player, uint16_t port);

void net_client_close(Client *data);
#else
typedef ClientData *net_client_spawn_t(Player *player, uint16_t port);

typedef void net_client_close_t(ClientData *data);
#endif


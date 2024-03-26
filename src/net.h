#pragma once

#include "./game.h"

typedef struct ServerData ServerData;

#if !defined(HOTRELOADING) || !defined(__linux__)
ServerData *net_server_data_new(Player *players, uint16_t *len_players, uint16_t max_players);

void net_server_data_delete(ServerData *data);

void net_server_create(uint16_t port, ServerData *data);
#else
typedef ServerData *net_server_data_new_t(Player *players, uint16_t *len_players, uint16_t max_players);

typedef void net_server_data_delete_t(ServerData *data);

typedef void net_server_create_t(uint16_t port, ServerData *data);
#endif

typedef struct ClientData ClientData;

#if !defined(HOTRELOADING) || !defined(__linux__)
ClientData *net_client_data_new(Player *player);

void net_client_data_delete(ClientData *data);

void net_client_create(uint16_t port, ClientData *data);
#else
typedef ClientData *net_client_data_new_t(Player *player);

typedef void net_client_data_delete_t(ClientData *data);

typedef void net_client_create_t(uint16_t port, ClientData *data);
#endif


#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "./game.h"

typedef struct ServerData ServerData;

ServerData *net_server_data_new(Player *players, uint16_t *len_players, uint16_t max_players);

void net_server_data_destroy(ServerData *data);

void net_server_create(uint16_t port, ServerData *data);

typedef struct ClientData ClientData;

ClientData *net_client_data_new(Player *player);

void net_client_data_destroy(ClientData *data);

void net_client_create(uint16_t port, ClientData *data);

// bool server();

// bool client(char const *ip, char const *port);


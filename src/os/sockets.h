#pragma once

#include <stdbool.h>

#ifdef __linux__
#include <sys/socket.h>
#include <arpa/inet.h>

typedef struct { int socket; } Socket;
typedef struct sockaddr_in Address;
#elif defined(_WIN64)

#endif

char *sockets_get_error(void);

bool socket_init_udp(Socket *socket);
bool socket_close(Socket socket);

bool socket_bind(Socket socket, Address *address);
bool socket_sendto_inet(Socket socket, void const *buffer, int length, Address const *destination);
bool socket_recvfrom_inet(Socket socket, void *buffer, int length, int *read, Address *source);

bool socket_poll(Socket socket, short events_requested, short *const events_returned, int timeout_millis);


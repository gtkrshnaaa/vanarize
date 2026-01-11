#ifndef VANARIZE_STDLIB_STDNETWORK_H
#define VANARIZE_STDLIB_STDNETWORK_H

#include "Core/VanarizeValue.h"

/**
 * StdNetwork - Event-driven I/O subsystem
 * MASTERPLAN Section 5.3: Non-blocking sockets managed by epoll/kqueue
 */

// Create a listening socket on specified port
// Returns socket file descriptor
Value StdNetwork_Listen(int port);

// Accept incoming connection on listening socket
// Returns client file descriptor
Value StdNetwork_Accept(int sockFd);

// HTTP GET request (async)
// Returns response body as string
Value StdNetwork_Get(Value url);

// HTTP POST request with body (async)
// Returns response body as string
Value StdNetwork_Post(Value url, Value body);

#endif // VANARIZE_STDLIB_STDNETWORK_H

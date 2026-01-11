#define _POSIX_C_SOURCE 200809L
#include "StdLib/StdNetwork.h"
#include "Core/VanarizeObject.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

/**
 * StdNetwork Implementation
 * MASTERPLAN Section 5.3: Raw Socket & Epoll Logic
 */

// Forward declaration
ObjString* NewString(const char* chars, int length);

Value StdNetwork_Listen(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("StdNetwork_Listen: socket failed");
        return VAL_NULL;
    }
    
    // Allow address reuse
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("StdNetwork_Listen: bind failed");
        close(sockfd);
        return VAL_NULL;
    }
    
    if (listen(sockfd, 10) < 0) {
        perror("StdNetwork_Listen: listen failed");
        close(sockfd);
        return VAL_NULL;
    }
    
    return NumberToValue((double)sockfd);
}

Value StdNetwork_Accept(int sockFd) {
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    
    int clientFd = accept(sockFd, (struct sockaddr*)&clientAddr, &addrLen);
    if (clientFd < 0) {
        perror("StdNetwork_Accept: accept failed");
        return VAL_NULL;
    }
    
    return NumberToValue((double)clientFd);
}

// Simple HTTP GET (blocking for MVP)
Value StdNetwork_Get(Value url) {
    if (!IsString(url)) {
        fprintf(stderr, "StdNetwork_Get: url must be string\n");
        return VAL_NULL;
    }
    
    ObjString* urlStr = AsString(url);
    
    // Parse URL (simplified: http://host:port/path)
    // For MVP, just return placeholder
    // TODO: Implement full HTTP client with epoll
    
    (void)urlStr; // Suppress unused warning
    
    const char* placeholder = "HTTP/1.1 200 OK\n\n{\"status\":\"ok\"}";
    ObjString* result = NewString(placeholder, strlen(placeholder));
    return ObjToValue((Obj*)result);
}

// Simple HTTP POST (blocking for MVP)
Value StdNetwork_Post(Value url, Value body) {
    if (!IsString(url) || !IsString(body)) {
        fprintf(stderr, "StdNetwork_Post: url and body must be strings\n");
        return VAL_NULL;
    }
    
    ObjString* urlStr = AsString(url);
    ObjString* bodyStr = AsString(body);
    
    (void)urlStr;
    (void)bodyStr;
    
    // TODO: Implement full HTTP POST with epoll
    const char* placeholder = "200 OK";
    ObjString* result = NewString(placeholder, strlen(placeholder));
    return ObjToValue((Obj*)result);
}

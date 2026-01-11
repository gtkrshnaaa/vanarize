#define _POSIX_C_SOURCE 200809L
#include "Core/EventLoop.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <time.h>
#include <errno.h>

#define MAX_EVENTS 64

static int epollFd;
static Task* readyQueueHead = NULL;
static Task* readyQueueTail = NULL;
static bool loopRunning = false;

void EventLoop_Init(void) {
    epollFd = epoll_create1(0);
    if (epollFd == -1) {
        perror("EventLoop_Init: epoll_create1 failed");
        exit(1);
    }
}

void EventLoop_ScheduleTask(TaskCallback callback, void* data) {
    Task* task = malloc(sizeof(Task));
    task->callback = callback;
    task->data = data;
    task->next = NULL;

    if (readyQueueTail) {
        readyQueueTail->next = task;
        readyQueueTail = task;
    } else {
        readyQueueHead = task;
        readyQueueTail = task;
    }
}

void EventLoop_ScheduleTimer(int ms, TaskCallback callback, void* data) {
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (tfd == -1) {
        perror("timerfd_create failed");
        return;
    }

    struct itimerspec ts;
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 0;
    ts.it_value.tv_sec = ms / 1000;
    ts.it_value.tv_nsec = (ms % 1000) * 1000000;

    if (timerfd_settime(tfd, 0, &ts, NULL) == -1) {
        perror("timerfd_settime failed");
        close(tfd);
        return;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    
    // We need to store callback and data. 
    // Simplified: We store a structure pointer in ev.data.ptr
    // But we need to cleanup this structure later.
    
    // For now, let's use a wrapper struct
    typedef struct {
        int fd;
        TaskCallback cb;
        void* arg;
    } TimerData;
    
    TimerData* td = malloc(sizeof(TimerData));
    td->fd = tfd;
    td->cb = callback;
    td->arg = data;
    
    ev.data.ptr = td;
    
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, tfd, &ev) == -1) {
        perror("epoll_ctl failed");
        free(td);
        close(tfd);
    }
}

void EventLoop_Run(void) {
    loopRunning = true;
    struct epoll_event events[MAX_EVENTS];

    while (loopRunning) {
        // 1. Run all ready tasks (Microtasks / Immediate)
        while (readyQueueHead) {
            Task* task = readyQueueHead;
            readyQueueHead = task->next;
            if (!readyQueueHead) readyQueueTail = NULL;
            
            task->callback(task->data);
            free(task);
        }

        // 2. Wait for IO/Timer events
        // If queue is empty, wait indefinitely (-1). 
        // If we want to be non-blocking for other embedding purposes, use 0.
        // Here we are the main loop, so we wait.
        int nfds = epoll_wait(epollFd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait failed");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            // Assume it's a timer for now or generic task
            // In a real system, we'd check event types.
            // Our TimerData struct is stored in ptr.
            
            // Safety check: In real impl, distinguish types.
            typedef struct {
                int fd;
                TaskCallback cb;
                void* arg;
            } TimerData;
            
            TimerData* td = (TimerData*)events[i].data.ptr;
            
            uint64_t exp;
            ssize_t ret = read(td->fd, &exp, sizeof(uint64_t));
            if (ret == -1) {
                perror("read timer failed");
            }
            
            td->cb(td->arg);
            
            // Cleanup
            epoll_ctl(epollFd, EPOLL_CTL_DEL, td->fd, NULL);
            close(td->fd);
            free(td);
        }
        
        // Break if no tasks and no pending timers? 
        // TODO: Track pending async operation count.
    }
}

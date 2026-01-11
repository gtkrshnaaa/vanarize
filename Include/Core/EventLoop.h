#ifndef VANARIZE_CORE_EVENTLOOP_H
#define VANARIZE_CORE_EVENTLOOP_H

#include <stdbool.h>
#include <stdint.h>
#include "Core/VanarizeValue.h"

// Task Callback Function Pointer
typedef void (*TaskCallback)(void* data);

// Task Structure
typedef struct Task {
    TaskCallback callback;
    void* data;
    struct Task* next;
} Task;

// Event Loop Interface
void EventLoop_Init(void);
void EventLoop_Run(void);
void EventLoop_ScheduleTask(TaskCallback callback, void* data);

// Timer Interface (Basic)
void EventLoop_ScheduleTimer(int ms, TaskCallback callback, void* data);

#endif // VANARIZE_CORE_EVENTLOOP_H

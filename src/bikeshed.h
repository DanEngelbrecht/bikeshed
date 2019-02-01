#pragma once

#include <stdint.h>

namespace bikeshed
{

typedef struct Shed* HShed;
struct SyncPrimitive
{
    bool (*AcquireLock)(SyncPrimitive* primitive);
    void (*ReleaseLock)(SyncPrimitive* primitive);
    void (*SignalReady)(SyncPrimitive* primitive, uint16_t ready_count);
};

// We probably want to extend TTaskID to 32-bit and use some bits for generation to avoid using stale ids
typedef uint16_t TTaskID;

enum TaskResult
{
    TASK_RESULT_COMPLETE,   // Call ResolveTask, schedule any resolve tasks, Call FreeTask
    TASK_RESULT_BLOCKED,    // Task is blocked, call ReadyTasks when ready to execute again
    TASK_RESULT_YIELD       // Call ReadyTasks to reschedule the task at en of queue
};

typedef TaskResult (*TaskFunc)(HShed shed, TTaskID task_id, void* context_data);

uint32_t GetShedSize(uint16_t max_task_count, uint16_t max_dependency_count);
HShed CreateShed(void* mem, uint16_t max_task_count, uint16_t max_dependency_count, SyncPrimitive* sync_primitive);

bool CreateTasks(HShed shed, uint16_t task_count, TaskFunc* task_functions, void** task_context_data, TTaskID* out_task_ids);
void FreeTasks(HShed shed, uint16_t task_count, const TTaskID* task_ids);
bool ReadyTasks(HShed shed, uint16_t task_count, const TTaskID* task_ids);
bool AddTaskDependencies(HShed shed, TTaskID task_id, uint16_t task_count, const TTaskID* dependency_task_ids);

struct ResolvedCallback
{
    bool (*ConsumeTask)(ResolvedCallback* callback, TTaskID task_id);
};

bool ExecuteOneTask(HShed shed, ResolvedCallback* resolved_callback, TTaskID* executed_task_id);
void ExecuteAndResolveTask(HShed shed, TTaskID task_id, ResolvedCallback* resolved_callback);

}

#pragma once

#include <stdint.h>

namespace bikeshed
{

typedef struct Shed* HShed;
struct SyncPrimitive
{
    bool (*AcquireLock)(SyncPrimitive* primitive);
    void (*ReleaseLock)(SyncPrimitive* primitive);
};

typedef uint16_t TTaskID;

enum TaskResult
{
    TASK_RESULT_COMPLETE,
    TASK_RESULT_BLOCKED,
    TASK_RESULT_YIELD
};

typedef TaskResult (*TaskFunc)(HShed shed, TTaskID task_id, void* context_data);

uint32_t GetShedSize(uint16_t max_task_count);
HShed CreateShed(void* mem, uint16_t max_task_count, SyncPrimitive* sync_primitive);

bool CreateTasks(HShed shed, uint16_t task_count, TaskFunc* task_functions, void** task_context_data, TTaskID* out_task_ids);
void FreeTasks(HShed shed, uint16_t task_count, const TTaskID* task_ids);
bool ReadyTasks(HShed shed, uint16_t task_count, const TTaskID* task_ids);
bool AddTaskDependencies(HShed shed, TTaskID task_id, uint16_t task_count, const TTaskID* dependency_task_ids);
void ResolveTask(HShed shed, TTaskID task_id, uint16_t* resolved_task_count, TTaskID* out_resolved_tasks);
bool GetFirstReadyTask(HShed shed, TTaskID* out_task_id);
TaskResult ExecuteTask(HShed shed, TTaskID task_id, uint16_t* resolved_task_count, TTaskID* out_resolved_tasks);

}

#pragma once

#include <stdint.h>

namespace bikeshed {

typedef void (*Assert)(const char* file, int line);
void SetAssert(Assert assert_func);

typedef struct Shed* HShed;
struct ReadyCallback
{
    void (*SignalReady)(ReadyCallback* ready_callback, uint32_t ready_count);
};

typedef uint32_t TTaskID;

enum TaskResult
{
    TASK_RESULT_COMPLETE, // Task is complete, dependecies will be resolved and the task is freed
    TASK_RESULT_BLOCKED   // Task is blocked, call ReadyTasks on the task id when ready to execute again
};

typedef TaskResult (*TaskFunc)(HShed shed, TTaskID task_id, void* context_data);

// Up to 8 388 607 tasks
uint32_t GetShedSize(uint32_t max_task_count, uint32_t max_dependency_count);
HShed    CreateShed(void* mem, uint32_t max_task_count, uint32_t max_dependency_count, ReadyCallback* ready_callback);

bool CreateTasks(HShed shed, uint32_t task_count, TaskFunc* task_functions, void** task_context_data, TTaskID* out_task_ids);
void ReadyTasks(HShed shed, uint32_t task_count, const TTaskID* task_ids);

// Dependencies can not be added to a ready task
// You can not add dependecies in the TaskFunc to the executing task
bool AddTaskDependencies(HShed shed, TTaskID task_id, uint32_t task_count, const TTaskID* dependency_task_ids);

bool ExecuteOneTask(HShed shed, TTaskID* out_next_ready_task_id);
void ExecuteAndResolveTask(HShed shed, TTaskID task_id, TTaskID* out_next_ready_task_id);

} // namespace bikeshed

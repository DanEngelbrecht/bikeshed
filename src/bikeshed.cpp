#include "bikeshed.h"

namespace bikeshed
{

typedef uint16_t TDependencyIndex;
typedef uint16_t TReadyIndex;

struct Dependency
{
    TTaskID m_TaskID;
    TDependencyIndex m_NextDependency;
};

struct Task
{
    uint32_t m_DependencyCount;
    TDependencyIndex m_FirstDependency;
    TaskFunc m_TaskFunc;
    void* m_TaskContextData;
};

struct ReadyTask
{
    TTaskID m_TaskID;
    TReadyIndex m_NextReadyTaskIndex;
};

struct Shed
{
    uint16_t            m_MaxTaskCount;
    uint16_t            m_FreeTaskCount;
    uint16_t            m_FreeDependencyCount;
    uint16_t            m_FreeReadyCount;
    TReadyIndex         m_FirstReadyIndex;
    TReadyIndex         m_LastReadyIndex;
    Task*               m_Tasks;
    Dependency*         m_Dependencies;
    ReadyTask*          m_ReadyTasks;
    TTaskID*            m_FreeTaskIndexes;
    TDependencyIndex*   m_FreeDependencyIndexes;
    TReadyIndex*        m_FreeReadyTaskIndexes;
    SyncPrimitive*      m_SyncPrimitive;
};

uint32_t GetShedSize(uint16_t max_task_count)
{
    uint32_t size = (uint32_t)sizeof(Shed) +
                    (uint32_t)(sizeof(Task) * max_task_count) +
                    (uint32_t)(sizeof(Dependency) * max_task_count) +
                    (uint32_t)(sizeof(ReadyTask) * max_task_count) +
                    (uint32_t)(sizeof(TDependencyIndex) * max_task_count) +
                    (uint32_t)(sizeof(TTaskID) * max_task_count) +
                    (uint32_t)(sizeof(TReadyIndex) * max_task_count);
    return size;
}

HShed CreateShed(void* mem, uint16_t max_task_count, SyncPrimitive* sync_primitive)
{
    HShed shed = (HShed)mem;
    shed->m_MaxTaskCount = max_task_count;
    shed->m_FreeTaskCount = max_task_count;
    shed->m_FreeDependencyCount = max_task_count;
    shed->m_FreeReadyCount = max_task_count;
    shed->m_FirstReadyIndex = max_task_count;
    shed->m_LastReadyIndex = max_task_count;
    uint8_t* p = (uint8_t*)mem;
    p += sizeof(Shed);
    shed->m_Tasks = (Task*)p;
    p += (sizeof(Task) * max_task_count);
    shed->m_Dependencies = (Dependency*)p;
    p += (sizeof(Dependency) * max_task_count);
    shed->m_ReadyTasks = (ReadyTask*)p;
    p += (sizeof(ReadyTask) * max_task_count);
    shed->m_FreeTaskIndexes = (TTaskID*)p;
    p += (sizeof(TTaskID) * max_task_count);
    shed->m_FreeDependencyIndexes = (TDependencyIndex*)p;
    p += (sizeof(TDependencyIndex) * max_task_count);
    shed->m_FreeReadyTaskIndexes = (TReadyIndex*)p;
    p += (sizeof(TReadyIndex) * max_task_count);
    shed->m_SyncPrimitive = sync_primitive;

    for (uint16_t i = 0; i < max_task_count; ++i)
    {
        uint16_t free_index = max_task_count - (1 + i);
        shed->m_FreeTaskIndexes[i] = free_index;
        shed->m_FreeDependencyIndexes[i] = free_index;
        shed->m_FreeReadyTaskIndexes[i] = free_index;
    }

    return shed;
}

bool CreateTasks(HShed shed, uint16_t task_count, TaskFunc* task_functions, void** task_context_data, TTaskID* out_task_ids)
{
    if (shed->m_SyncPrimitive && !shed->m_SyncPrimitive->AcquireLock(shed->m_SyncPrimitive))
    {
        return false;
    }
    if (shed->m_FreeTaskCount < task_count)
    {
        if (shed->m_SyncPrimitive)
        {
            shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
        }
        return false;
    }
    for (uint16_t i = 0; i < task_count; ++i)
    {
        TTaskID task_id = shed->m_FreeTaskIndexes[--shed->m_FreeTaskCount];
        out_task_ids[i] = task_id;
        Task* task = &shed->m_Tasks[task_id];
        task->m_DependencyCount = 0;
        task->m_FirstDependency = shed->m_MaxTaskCount;
        task->m_TaskFunc = task_functions[i];
        task->m_TaskContextData = task_context_data[i];
    }
    if (shed->m_SyncPrimitive)
    {
        shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
    }
    return true;
}

void FreeTasks(HShed shed, uint16_t task_count, const TTaskID* task_ids)
{
    if (shed->m_SyncPrimitive && !shed->m_SyncPrimitive->AcquireLock(shed->m_SyncPrimitive))
    {
        return;
    }
    for (uint16_t i = 0; i < task_count; ++i)
    {
        TTaskID task_id = task_ids[i];
        Task* task = &shed->m_Tasks[task_id];
        shed->m_FreeTaskIndexes[shed->m_FreeTaskCount++] = task_id;
        TDependencyIndex dependency_index = task->m_FirstDependency;
        while (dependency_index != shed->m_MaxTaskCount)
        {
            Dependency* dependency = &shed->m_Dependencies[dependency_index];
            shed->m_FreeDependencyIndexes[shed->m_FreeDependencyCount++] = dependency_index;
            dependency_index = dependency->m_NextDependency;
        }
    }
    if (shed->m_SyncPrimitive)
    {
        shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
    }
}

bool AddTaskDependencies(HShed shed, TTaskID task_id, uint16_t task_count, const TTaskID* dependency_task_ids)
{
    if (shed->m_SyncPrimitive && !shed->m_SyncPrimitive->AcquireLock(shed->m_SyncPrimitive))
    {
        return false;
    }
    if (task_count > shed->m_FreeDependencyCount)
    {
        if (shed->m_SyncPrimitive)
        {
            shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
        }
        return false;
    }

    for (uint16_t i = 0; i < task_count; ++i)
    {
        TTaskID dependency_task_id = dependency_task_ids[i];
        Task* dependency_task = &shed->m_Tasks[dependency_task_id];
        TDependencyIndex dependency_index = shed->m_FreeDependencyIndexes[--shed->m_FreeDependencyCount];
        Dependency* dependency = &shed->m_Dependencies[dependency_index];
        dependency->m_TaskID = task_id;
        dependency->m_NextDependency = dependency_task->m_FirstDependency;
        dependency_task->m_FirstDependency = dependency_index;
    }
    Task* task = &shed->m_Tasks[task_id];
    task->m_DependencyCount += task_count;

    if (shed->m_SyncPrimitive)
    {
        shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
    }
    return true;
}

bool ReadyTasks(HShed shed, uint16_t task_count, const TTaskID* task_ids)
{
    if (shed->m_SyncPrimitive && !shed->m_SyncPrimitive->AcquireLock(shed->m_SyncPrimitive))
    {
        return false;
    }
    if (task_count > shed->m_FreeReadyCount)
    {
        if (shed->m_SyncPrimitive)
        {
            shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
        }
        return false;
    }

    for (uint16_t i = 0; i < task_count; ++i)
    {
        TTaskID task_id = task_ids[i];
        Task* task = &shed->m_Tasks[task_id];
        if (task->m_DependencyCount != 0)
        {
            if (shed->m_SyncPrimitive)
            {
                shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
            }
            return false;
        }
    }

    TReadyIndex* prev_ready_index_ptr = &shed->m_FirstReadyIndex;
    if (shed->m_LastReadyIndex != shed->m_MaxTaskCount)
    {
        ReadyTask* last_ready_task = &shed->m_ReadyTasks[shed->m_LastReadyIndex];
        prev_ready_index_ptr = &last_ready_task->m_NextReadyTaskIndex;
    }
    for (uint16_t i = 0; i < task_count; ++i)
    {
        TTaskID task_id = task_ids[i];
        TReadyIndex ready_index = shed->m_FreeReadyTaskIndexes[--shed->m_FreeReadyCount];
        ReadyTask* ready_task = &shed->m_ReadyTasks[ready_index];
        ready_task->m_TaskID = task_id;
        ready_task->m_NextReadyTaskIndex = shed->m_MaxTaskCount;
        *prev_ready_index_ptr = ready_index;
        prev_ready_index_ptr = &ready_task->m_NextReadyTaskIndex;
        shed->m_LastReadyIndex = ready_index;
    }

    if (shed->m_SyncPrimitive)
    {
        shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
        shed->m_SyncPrimitive->SignalReady(shed->m_SyncPrimitive, task_count);
    }
    return true;
}

void ResolveTask(HShed shed, TTaskID task_id, uint16_t* resolved_task_count, TTaskID* out_resolved_tasks)
{
    if (shed->m_SyncPrimitive && !shed->m_SyncPrimitive->AcquireLock(shed->m_SyncPrimitive))
    {
        return;
    }
    uint16_t resolved_count = 0;
    Task* task = &shed->m_Tasks[task_id];
    TDependencyIndex dependency_index = task->m_FirstDependency;

    while (dependency_index != shed->m_MaxTaskCount)
    {
        Dependency* dependency = &shed->m_Dependencies[dependency_index];
        Task* dependency_task = &shed->m_Tasks[dependency->m_TaskID];
        if (dependency_task->m_DependencyCount-- == 1)
        {
            out_resolved_tasks[resolved_count++] = dependency->m_TaskID;
        }
        dependency_index = dependency->m_NextDependency;
    }
    if (shed->m_SyncPrimitive)
    {
        shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
    }
    *resolved_task_count = resolved_count;
}

bool GetFirstReadyTask(HShed shed, TTaskID* out_task_id)
{
    if (shed->m_SyncPrimitive && !shed->m_SyncPrimitive->AcquireLock(shed->m_SyncPrimitive))
    {
        return false;
    }
    if (shed->m_FirstReadyIndex == shed->m_MaxTaskCount)
    {
        if (shed->m_SyncPrimitive)
        {
            shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
        }
        return false;
    }
    TReadyIndex ready_index = shed->m_FirstReadyIndex;
    ReadyTask* ready_task = &shed->m_ReadyTasks[ready_index];
    *out_task_id = ready_task->m_TaskID;
    shed->m_FirstReadyIndex = ready_task->m_NextReadyTaskIndex;
    if (shed->m_FirstReadyIndex == shed->m_MaxTaskCount)
    {
        shed->m_LastReadyIndex = shed->m_MaxTaskCount;
    }
    shed->m_FreeReadyTaskIndexes[shed->m_FreeReadyCount++] = ready_index;
    if (shed->m_SyncPrimitive)
    {
        shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
    }
    return true;
}

TaskResult ExecuteTask(HShed shed, TTaskID task_id)
{
    Task* task = &shed->m_Tasks[task_id];

    TaskResult result = task->m_TaskFunc(shed, task_id, task->m_TaskContextData);
    return result;
}

}

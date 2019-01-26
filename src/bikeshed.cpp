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
    Task*               m_Tasks;
    Dependency*         m_Dependencies;
    ReadyTask*          m_ReadyTasks;
    TTaskID*            m_FreeTaskIndexes;
    TDependencyIndex*   m_FreeDependencyIndexes;
    TReadyIndex*        m_FreeReadyTaskIndexes;
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

TShed CreateShed(void* mem, uint16_t max_task_count)
{
    TShed shed = (TShed)mem;
    shed->m_MaxTaskCount = max_task_count;
    shed->m_FreeTaskCount = max_task_count;
    shed->m_FreeDependencyCount = max_task_count;
    shed->m_FreeReadyCount = max_task_count;
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

    for (uint16_t i = 0; i < max_task_count; ++i)
    {
        uint16_t free_index = max_task_count - (1 + i);
        shed->m_FreeTaskIndexes[i] = free_index;
        shed->m_FreeDependencyIndexes[i] = free_index;
        shed->m_FreeReadyTaskIndexes[i] = free_index;
    }

    return shed;
}

bool CreateTasks(TShed shed, uint16_t task_count, TaskFunc* task_functions, void** task_context_data, TTaskID* out_task_ids)
{
    if (shed->m_FreeTaskCount < task_count)
    {
        return false;
    }
    for (uint16_t i = 0; i < task_count; ++task_count)
    {
        TTaskID task_id = shed->m_FreeTaskIndexes[--shed->m_FreeTaskCount];
        out_task_ids[i] = task_id;
        Task* task = &shed->m_Tasks[task_id];
        task->m_DependencyCount = 0;
        task->m_FirstDependency = shed->m_MaxTaskCount;
        task->m_TaskFunc = task_functions[i];
        task->m_TaskContextData = task_context_data[i];
    }
    return true;
}

void FreeTasks(TShed shed, uint16_t task_count, const TTaskID* task_ids)
{
    for (uint16_t i = 0; i < task_count; ++task_count)
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
}

bool AddTaskDependencies(TShed shed, TTaskID task_id, uint16_t task_count, const TTaskID* dependency_task_ids)
{
    if (task_count > shed->m_FreeDependencyCount)
    {
        return false;
    }

    Task* task = &shed->m_Tasks[task_id];
    TDependencyIndex* prev_dependency_index_ptr = &task->m_FirstDependency;
    for (uint16_t i = 0; i < task_count; ++task_count)
    {
        TTaskID dependency_task_id = dependency_task_ids[i];
        TDependencyIndex dependency_index = shed->m_FreeDependencyIndexes[--shed->m_FreeDependencyCount];
        Dependency* dependency = &shed->m_Dependencies[dependency_index];
        dependency->m_TaskID = dependency_task_id;
        dependency->m_NextDependency = *prev_dependency_index_ptr;
        *prev_dependency_index_ptr = dependency_index;
    }
    task->m_DependencyCount += task_count;

    return true;
}

bool ReadyTasks(TShed shed, uint16_t task_count, const TTaskID* task_ids)
{
    if (task_count > shed->m_FreeReadyCount)
    {
        return false;
    }

    TReadyIndex* prev_ready_index_ptr = &shed->m_FirstReadyIndex;
    for (uint16_t i = 0; i < task_count; ++task_count)
    {
        TTaskID task_id = task_ids[i];
        TReadyIndex ready_index = shed->m_FreeReadyTaskIndexes[--shed->m_FreeReadyCount];
        ReadyTask* ready_task = &shed->m_ReadyTasks[ready_index];
        ready_task->m_TaskID = task_id;
        ready_task->m_NextReadyTaskIndex = *prev_ready_index_ptr;
        *prev_ready_index_ptr = ready_index;
    }

    return true;
}

void ResolveTask(TShed shed, TTaskID task_id, uint16_t* resolved_task_count, TTaskID* out_resolved_tasks)
{
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
    *resolved_task_count = resolved_count;
}

bool GetFirstReadyTask(TShed shed, TTaskID* out_task_id)
{
    if (shed->m_FirstReadyIndex == shed->m_MaxTaskCount)
    {
        return false;
    }
    TReadyIndex ready_index = shed->m_FirstReadyIndex;
    ReadyTask* ready_task = &shed->m_ReadyTasks[ready_index];
    *out_task_id = ready_task->m_TaskID;
    shed->m_FirstReadyIndex = ready_task->m_NextReadyTaskIndex;
    shed->m_FreeReadyTaskIndexes[shed->m_FreeReadyCount++] = ready_index;
    return true;
}

void ExecuteOneTask(TShed shed, TTaskID task_id, uint16_t* resolved_task_count, TTaskID* out_resolved_tasks)
{
    Task* task = &shed->m_Tasks[task_id];

    task->m_TaskFunc(shed, task_id, task->m_TaskContextData);

    ResolveTask(shed, task_id, resolved_task_count, out_resolved_tasks);
}

}

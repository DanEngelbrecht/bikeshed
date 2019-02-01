#include "bikeshed.h"

#define ALIGN_SIZE(x, align)    (((x) + ((align) - 1)) & ~((align) - 1))

#if defined(ENABLE_VALIDATE_STATE)
    #include <assert.h>
    #ifdef _WIN32
        #include <malloc.h>
        #define alloca _alloca
    #else
        #include <alloca.h>
    #endif
    #define VALIDATE_STATE(shed) assert(ValidateState(shed));
#else
    #define VALIDATE_STATE(shed)
#endif

namespace bikeshed
{

typedef uint16_t TDependencyIndex;
typedef uint16_t TReadyIndex;

struct Dependency
{
    TTaskID m_ParentTaskID;
    TDependencyIndex m_NextParentDependencyIndex;
};

struct Task
{
    uint32_t m_ChildDependencyCount;
    TDependencyIndex m_FirstParentDependencyIndex;
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
    uint16_t            m_FreeTaskCount;
    uint16_t            m_FreeDependencyCount;
    uint16_t            m_FreeReadyCount;
    TReadyIndex         m_LastReadyIndex;
    Task*               m_Tasks;
    Dependency*         m_Dependencies;
    ReadyTask*          m_ReadyTasks;
    TTaskID*            m_FreeTaskIndexes;
    TDependencyIndex*   m_FreeDependencyIndexes;
    TReadyIndex*        m_FreeReadyTaskIndexes;
    SyncPrimitive*      m_SyncPrimitive;
};

uint32_t GetShedSize(uint16_t max_task_count, uint16_t max_dependency_count)
{
    uint32_t size = (uint32_t)ALIGN_SIZE(sizeof(Shed), 8) +
                    (uint32_t)ALIGN_SIZE((sizeof(Task) * max_task_count), 8) +
                    (uint32_t)ALIGN_SIZE((sizeof(Dependency) * max_dependency_count), 8) +
                    (uint32_t)ALIGN_SIZE((sizeof(ReadyTask) * (max_task_count + 1)), 8) +
                    (uint32_t)ALIGN_SIZE((sizeof(TTaskID) * max_task_count), 8) +
                    (uint32_t)ALIGN_SIZE((sizeof(TDependencyIndex) * max_dependency_count), 8) +
                    (uint32_t)ALIGN_SIZE((sizeof(TReadyIndex) * max_task_count), 8);
    return size;
}

HShed CreateShed(void* mem, uint16_t max_task_count, uint16_t max_dependency_count, SyncPrimitive* sync_primitive)
{
    HShed shed = (HShed)mem;
    shed->m_FreeTaskCount = max_task_count;
    shed->m_FreeDependencyCount = max_dependency_count;
    shed->m_FreeReadyCount = max_task_count;
    shed->m_LastReadyIndex = 0;
    uint8_t* p = (uint8_t*)mem;
    p += ALIGN_SIZE(sizeof(Shed), 8);
    shed->m_Tasks = (Task*)((void*)p);
    p += ALIGN_SIZE((sizeof(Task) * max_task_count), 8);
    shed->m_Dependencies = (Dependency*)((void*)p);
    p += ALIGN_SIZE((sizeof(Dependency) * max_dependency_count), 8);
    shed->m_ReadyTasks = (ReadyTask*)((void*)p);
    p += ALIGN_SIZE((sizeof(ReadyTask) * (max_task_count + 1)), 8);
    shed->m_FreeTaskIndexes = (TTaskID*)((void*)p);
    p += ALIGN_SIZE((sizeof(TTaskID) * max_task_count), 8);
    shed->m_FreeDependencyIndexes = (TDependencyIndex*)((void*)p);
    p += ALIGN_SIZE((sizeof(TDependencyIndex) * max_dependency_count), 8);
    shed->m_FreeReadyTaskIndexes = (TReadyIndex*)((void*)p);
    p += ALIGN_SIZE((sizeof(TReadyIndex) * max_task_count), 8);
    shed->m_SyncPrimitive = sync_primitive;

    for (uint16_t i = 0; i < max_task_count; ++i)
    {
        uint16_t free_index = max_task_count - i;
        shed->m_Tasks[i].m_TaskFunc = 0;
        shed->m_FreeTaskIndexes[i] = free_index;
        shed->m_FreeDependencyIndexes[i] = free_index;
        shed->m_FreeReadyTaskIndexes[i] = free_index;
    }

    for (uint16_t i = 0; i < max_dependency_count; ++i)
    {
        uint16_t free_index = max_dependency_count - i;
        shed->m_FreeDependencyIndexes[i] = free_index;
    }

    return shed;
}

#if defined(ENABLE_VALIDATE_STATE)
static bool ValidateState(HShed shed)
{
    if (shed->m_FreeTaskCount > shed->m_MaxTaskCount)
    {
        return false;
    }
    if (shed->m_FreeDependencyCount > shed->m_MaxDependencyCount)
    {
        return false;
    }
    if (shed->m_FreeReadyCount > shed->m_MaxTaskCount)
    {
        return false;
    }
    if (shed->m_FirstReadyIndex > shed->m_MaxTaskCount)
    {
        return false;
    }
    if (shed->m_LastReadyIndex > shed->m_MaxTaskCount)
    {
        return false;
    }
    uint16_t* validate_child_count = (uint16_t*)alloca(sizeof(uint16_t) * shed->m_MaxTaskCount);
    for (uint16_t i = 1; i < shed->m_MaxTaskCount; ++i)
    {
        validate_child_count[i] = 0;
    }

    uint16_t allocated_task_count = shed->m_MaxTaskCount - shed->m_FreeTaskCount;
    uint16_t ready_task_count = 0;
    uint16_t ready_index = shed->m_FirstReadyIndex;
    while (ready_index != shed->m_MaxTaskCount)
    {
        ReadyTask* ready_task = &shed->m_ReadyTasks[ready_index];
        if (ready_task->m_TaskID > shed->m_MaxTaskCount)
        {
            return false;
        }
        if (ready_task->m_NextReadyTaskIndex > shed->m_MaxTaskCount)
        {
            return false;
        }
        Task* task = &shed->m_Tasks[ready_task->m_TaskID];
        if (task->m_TaskFunc == 0)
        {
            return false;
        }
        ready_index = ready_task->m_NextReadyTaskIndex;
        ++ready_task_count;
        if (ready_task_count > allocated_task_count)
        {
            return false;
        }
    }

    uint16_t live_task_count = 0;
    for (uint16_t i = 0; i < shed->m_MaxTaskCount; ++i)
    {
        Task* task = &shed->m_Tasks[i];
        if (task->m_TaskFunc == 0)
        {
            continue;
        }
        ++live_task_count;
        if (live_task_count > allocated_task_count)
        {
            return false;
        }
        uint16_t dependency_index = task->m_FirstParentDependencyIndex;
        while (dependency_index != shed->m_MaxDependencyCount)
        {
            Dependency* dependency = &shed->m_Dependencies[dependency_index - 1];
            if (dependency->m_NextParentDependencyIndex > shed->m_MaxDependencyCount)
            {
                return false;
            }
            TTaskID parent_task_id = dependency->m_ParentTaskID;
            if (parent_task_id > shed->m_MaxTaskCount)
            {
                return false;
            }
            Task* parent_task = &shed->m_Tasks[parent_task_id];
            if (parent_task->m_TaskFunc == 0)
            {
                return false;
            }
            validate_child_count[parent_task_id]++;
            dependency_index = dependency->m_NextParentDependencyIndex;
        }
    }
    for (uint16_t i = 0; i < shed->m_MaxTaskCount; ++i)
    {
        Task* task = &shed->m_Tasks[i];
        if (task->m_TaskFunc == 0)
        {
            continue;
        }
        if (task->m_ChildDependencyCount != validate_child_count[i])
        {
            return false;
        }
    }
    return true;
}
#endif

bool CreateTasks(HShed shed, uint16_t task_count, TaskFunc* task_functions, void** task_context_data, TTaskID* out_task_ids)
{
    if (shed->m_SyncPrimitive && !shed->m_SyncPrimitive->AcquireLock(shed->m_SyncPrimitive))
    {
        return false;
    }
    VALIDATE_STATE(shed)
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
        Task* task = &shed->m_Tasks[task_id - 1];
        task->m_ChildDependencyCount = 0;
        task->m_FirstParentDependencyIndex = 0;
        task->m_TaskFunc = task_functions[i];
        task->m_TaskContextData = task_context_data[i];
    }
    VALIDATE_STATE(shed)
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
    VALIDATE_STATE(shed)
    for (uint16_t i = 0; i < task_count; ++i)
    {
        TTaskID task_id = task_ids[i];
        Task* task = &shed->m_Tasks[task_id - 1];
        task->m_TaskFunc = 0;
        shed->m_FreeTaskIndexes[shed->m_FreeTaskCount++] = task_id;
        TDependencyIndex dependency_index = task->m_FirstParentDependencyIndex;
        while (dependency_index != 0)
        {
            Dependency* dependency = &shed->m_Dependencies[dependency_index - 1];
            shed->m_FreeDependencyIndexes[shed->m_FreeDependencyCount++] = dependency_index;
            dependency_index = dependency->m_NextParentDependencyIndex;
        }
    }
    VALIDATE_STATE(shed)
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
    VALIDATE_STATE(shed)
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
        Task* dependency_task = &shed->m_Tasks[dependency_task_id - 1];
        TDependencyIndex dependency_index = shed->m_FreeDependencyIndexes[--shed->m_FreeDependencyCount];
        Dependency* dependency = &shed->m_Dependencies[dependency_index - 1];
        dependency->m_ParentTaskID = task_id;
        dependency->m_NextParentDependencyIndex = dependency_task->m_FirstParentDependencyIndex;
        dependency_task->m_FirstParentDependencyIndex = dependency_index;
    }
    Task* task = &shed->m_Tasks[task_id - 1];
    task->m_ChildDependencyCount += task_count;

    VALIDATE_STATE(shed)
    if (shed->m_SyncPrimitive)
    {
        shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
    }
    return true;
}

bool ReadyTasks(HShed shed, uint16_t task_count, const TTaskID* task_ids)
{
    if (task_count == 0)
    {
        return true;
    }
    if (shed->m_SyncPrimitive && !shed->m_SyncPrimitive->AcquireLock(shed->m_SyncPrimitive))
    {
        return false;
    }
    VALIDATE_STATE(shed)
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
        const Task* task = &shed->m_Tasks[task_id - 1];
        if (task->m_ChildDependencyCount != 0)
        {
            if (shed->m_SyncPrimitive)
            {
                shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
            }
            return false;
        }
    }

    for (uint16_t i = 0; i < task_count; ++i)
    {
        TTaskID task_id = task_ids[i];
        TReadyIndex ready_index = shed->m_FreeReadyTaskIndexes[--shed->m_FreeReadyCount];
        ReadyTask* ready_task = &shed->m_ReadyTasks[ready_index];
        ready_task->m_TaskID = task_id;
        ready_task->m_NextReadyTaskIndex = 0;
        shed->m_ReadyTasks[shed->m_LastReadyIndex].m_NextReadyTaskIndex = ready_index;
        shed->m_LastReadyIndex = ready_index;
    }

    VALIDATE_STATE(shed)
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
    VALIDATE_STATE(shed)
    uint16_t resolved_count = 0;
    Task* task = &shed->m_Tasks[task_id - 1];
    task->m_TaskFunc = 0;
    TDependencyIndex dependency_index = task->m_FirstParentDependencyIndex;

    while (dependency_index != 0)
    {
        Dependency* dependency = &shed->m_Dependencies[dependency_index - 1];
        Task* parent_task = &shed->m_Tasks[dependency->m_ParentTaskID - 1];
        if (parent_task->m_ChildDependencyCount-- == 1)
        {
            out_resolved_tasks[resolved_count++] = dependency->m_ParentTaskID;
        }
        dependency_index = dependency->m_NextParentDependencyIndex;
    }
    *resolved_task_count = resolved_count;

    VALIDATE_STATE(shed)
    if (shed->m_SyncPrimitive)
    {
        shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
    }
}

bool GetFirstReadyTask(HShed shed, TTaskID* out_task_id)
{
    if (shed->m_SyncPrimitive && !shed->m_SyncPrimitive->AcquireLock(shed->m_SyncPrimitive))
    {
        return false;
    }
    VALIDATE_STATE(shed)
    TReadyIndex* first_ready_index_ptr = &shed->m_ReadyTasks[0].m_NextReadyTaskIndex;
    if (*first_ready_index_ptr == 0)
    {
        if (shed->m_SyncPrimitive)
        {
            shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
        }
        return false;
    }

    ReadyTask* ready_task = &shed->m_ReadyTasks[*first_ready_index_ptr];
    shed->m_FreeReadyTaskIndexes[shed->m_FreeReadyCount++] = *first_ready_index_ptr;

    *first_ready_index_ptr = ready_task->m_NextReadyTaskIndex;
    *out_task_id = ready_task->m_TaskID;
    if (ready_task->m_NextReadyTaskIndex == 0)
    {
        shed->m_LastReadyIndex = 0;
    }

    VALIDATE_STATE(shed)
    if (shed->m_SyncPrimitive)
    {
        shed->m_SyncPrimitive->ReleaseLock(shed->m_SyncPrimitive);
    }
    return true;
}

TaskResult ExecuteTask(HShed shed, TTaskID task_id)
{
    Task* task = &shed->m_Tasks[task_id - 1];

    TaskResult result = task->m_TaskFunc(shed, task_id, task->m_TaskContextData);
    return result;
}

}

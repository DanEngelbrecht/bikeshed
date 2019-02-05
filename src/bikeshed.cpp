#include "bikeshed.h"

#define ALIGN_SIZE(x, align)    (((x) + ((align) - 1)) & ~((align) - 1))

#if defined(BIKESHED_ASSERTS)
    #define BIKESHED_FATAL_ASSERT(x, y) if (gAssert && !(x)) {gAssert(__FILE__, __LINE__); goto y;}
    #define BIKESHED_ASSERT_EXIT(l) l:
#else // defined(BIKESHED_ASSERTS)
    #define BIKESHED_FATAL_ASSERT(x, y)
    #define BIKESHED_ASSERT_EXIT(l)
#endif // defined(BIKESHED_ASSERTS)

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

#if defined(BIKESHED_ASSERTS)
static Assert gAssert = 0;
#endif // defined(BIKESHED_ASSERTS)

void SetAssert(Assert assert_func)
{
#if defined(BIKESHED_ASSERTS)
    gAssert = assert_func;
#else // defined(BIKESHED_ASSERTS)
    assert_func = 0;
#endif // defined(BIKESHED_ASSERTS)
}

typedef uint16_t TTaskIndex;
typedef uint16_t TDependencyIndex;
typedef uint16_t TReadyIndex;

struct Dependency
{
    TTaskIndex m_ParentTaskIndex;
    TDependencyIndex m_NextParentDependencyIndex;
};

struct Task
{
    uint16_t m_Generation;
    uint16_t m_ChildDependencyCount;
    TDependencyIndex m_FirstParentDependencyIndex;
    TaskFunc m_TaskFunc;
    void* m_TaskContextData;
};

struct ReadyTask
{
    TTaskIndex m_TaskIndex;
    TReadyIndex m_NextReadyTaskIndex;
};

struct Shed
{
    uint16_t            m_Generation;
    uint16_t            m_FreeTaskCount;
    uint16_t            m_FreeDependencyCount;
    uint16_t            m_FreeReadyCount;
    TReadyIndex         m_FirstReadyIndex;
    TReadyIndex         m_LastReadyIndex;
    Task*               m_Tasks;
    Dependency*         m_Dependencies;
    ReadyTask*          m_ReadyTasks;
    TTaskIndex*         m_FreeTaskIndexes;
    TDependencyIndex*   m_FreeDependencyIndexes;
    TReadyIndex*        m_FreeReadyTaskIndexes;
    SyncPrimitive*      m_SyncPrimitive;
};

struct SyncPrimitiveScopedLock
{
    SyncPrimitiveScopedLock(SyncPrimitive* sync_primitive)
        : m_SyncPrimitive(sync_primitive)
    {
        if (m_SyncPrimitive)
        {
            m_SyncPrimitive->AcquireLock(m_SyncPrimitive);
        }
    }
    ~SyncPrimitiveScopedLock()
    {
        if (m_SyncPrimitive)
        {
            m_SyncPrimitive->ReleaseLock(m_SyncPrimitive);
        }
    }

    SyncPrimitive* m_SyncPrimitive;
};

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

#define TASK_ID(index, generation) (((TTaskID)(generation) << 16) + index)
#define TASK_GENERATION(task_id) ((uint16_t)(task_id >> 16))
#define TASK_INDEX(task_id) ((TTaskIndex)(task_id & 0xffff))

static void SyncedFreeTask(HShed shed, TTaskID task_id)
{
    TTaskIndex task_index = TASK_INDEX(task_id);
    Task* task = &shed->m_Tasks[task_index - 1];
    BIKESHED_FATAL_ASSERT(TASK_GENERATION(task_id) == task->m_Generation, bail);
    task->m_Generation = 0;
    shed->m_FreeTaskIndexes[shed->m_FreeTaskCount++] = task_index;
    TDependencyIndex dependency_index = task->m_FirstParentDependencyIndex;
    while (dependency_index != 0)
    {
        Dependency* dependency = &shed->m_Dependencies[dependency_index - 1];
        shed->m_FreeDependencyIndexes[shed->m_FreeDependencyCount++] = dependency_index;
        dependency_index = dependency->m_NextParentDependencyIndex;
    }
BIKESHED_ASSERT_EXIT(bail)
    return;
}

static void SyncedReadyTask(HShed shed, TTaskID task_id)
{
    TTaskIndex task_index = TASK_INDEX(task_id);

    BIKESHED_FATAL_ASSERT(TASK_GENERATION(task_id) == shed->m_Tasks[task_index - 1].m_Generation, bail);
    TReadyIndex ready_index = shed->m_FreeReadyTaskIndexes[--shed->m_FreeReadyCount];
    ReadyTask* ready_task = &shed->m_ReadyTasks[ready_index - 1];
    ready_task->m_TaskIndex = task_index;
    ready_task->m_NextReadyTaskIndex = 0;
    if (shed->m_LastReadyIndex == 0)
    {
        shed->m_FirstReadyIndex = ready_index;
    }
    else
    {
        shed->m_ReadyTasks[shed->m_LastReadyIndex - 1].m_NextReadyTaskIndex = ready_index;
    }

    shed->m_LastReadyIndex = ready_index;

BIKESHED_ASSERT_EXIT(bail)
    return;
}

static void SyncedResolveTask(HShed shed, TTaskID task_id, ResolvedCallback* resolves_callback)
{
    TTaskIndex task_index = TASK_INDEX(task_id);
    Task* task = &shed->m_Tasks[task_index - 1];
    BIKESHED_FATAL_ASSERT(TASK_GENERATION(task_id) == task->m_Generation, bail);
    TDependencyIndex dependency_index = task->m_FirstParentDependencyIndex;

    while (dependency_index != 0)
    {
        Dependency* dependency = &shed->m_Dependencies[dependency_index - 1];
        TTaskIndex parent_task_index = dependency->m_ParentTaskIndex;
        Task* parent_task = &shed->m_Tasks[parent_task_index - 1];
		TTaskID parent_task_id = TASK_ID(parent_task_index, parent_task->m_Generation);
        BIKESHED_FATAL_ASSERT(parent_task->m_ChildDependencyCount > 0, bail);
        if (parent_task->m_ChildDependencyCount-- == 1)
        {
            if (resolves_callback && resolves_callback->ConsumeTask(resolves_callback, parent_task_id))
            {
                // Consumed by caller
            }
            else
            {
               SyncedReadyTask(shed, parent_task_id);
            }           
        }
        dependency_index = dependency->m_NextParentDependencyIndex;
    }
BIKESHED_ASSERT_EXIT(bail)
    return;
}

static bool SyncGetFirstReadyTask(HShed shed, TTaskID* out_task_id)
{
    TReadyIndex first_ready_index = shed->m_FirstReadyIndex;
    if (first_ready_index == 0)
    {
        return false;
    }
    ReadyTask* ready_task = &shed->m_ReadyTasks[first_ready_index - 1];
    Task* task = &shed->m_Tasks[ready_task->m_TaskIndex - 1];
    shed->m_FreeReadyTaskIndexes[shed->m_FreeReadyCount++] = first_ready_index;

    shed->m_FirstReadyIndex = ready_task->m_NextReadyTaskIndex;
    if (shed->m_LastReadyIndex == first_ready_index)
    {
        shed->m_LastReadyIndex = 0;
    }
	*out_task_id = TASK_ID(ready_task->m_TaskIndex, task->m_Generation);

    return true;
}

uint32_t GetShedSize(uint16_t max_task_count, uint16_t max_dependency_count)
{
    uint32_t size = (uint32_t)ALIGN_SIZE(sizeof(Shed), 8) +
        (uint32_t)ALIGN_SIZE((sizeof(Task) * max_task_count), 8) +
        (uint32_t)ALIGN_SIZE((sizeof(Dependency) * max_dependency_count), 8) +
        (uint32_t)ALIGN_SIZE((sizeof(ReadyTask) * (max_task_count)), 8) +
        (uint32_t)ALIGN_SIZE((sizeof(TTaskIndex) * max_task_count), 8) +
        (uint32_t)ALIGN_SIZE((sizeof(TDependencyIndex) * max_dependency_count), 8) +
        (uint32_t)ALIGN_SIZE((sizeof(TReadyIndex) * max_task_count), 8);
    return size;
}

HShed CreateShed(void* mem, uint16_t max_task_count, uint16_t max_dependency_count, SyncPrimitive* sync_primitive)
{
    HShed shed = (HShed)mem;
    shed->m_Generation = 1;
    shed->m_FreeTaskCount = max_task_count;
    shed->m_FreeDependencyCount = max_dependency_count;
    shed->m_FreeReadyCount = max_task_count;
    shed->m_FirstReadyIndex = 0;
    shed->m_LastReadyIndex = 0;
    uint8_t* p = (uint8_t*)mem;
    p += ALIGN_SIZE(sizeof(Shed), 8);
    shed->m_Tasks = (Task*)((void*)p);
    p += ALIGN_SIZE((sizeof(Task) * max_task_count), 8);
    shed->m_Dependencies = (Dependency*)((void*)p);
    p += ALIGN_SIZE((sizeof(Dependency) * max_dependency_count), 8);
    shed->m_ReadyTasks = (ReadyTask*)((void*)p);
    p += ALIGN_SIZE((sizeof(ReadyTask) * (max_task_count)), 8);
    shed->m_FreeTaskIndexes = (TTaskIndex*)((void*)p);
    p += ALIGN_SIZE((sizeof(TTaskIndex) * max_task_count), 8);
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
        shed->m_FreeReadyTaskIndexes[i] = free_index;
    }

    for (uint16_t i = 0; i < max_dependency_count; ++i)
    {
        uint16_t free_index = max_dependency_count - i;
        shed->m_FreeDependencyIndexes[i] = free_index;
    }

    return shed;
}

bool CreateTasks(HShed shed, uint16_t task_count, TaskFunc* task_functions, void** task_context_data, TTaskID* out_task_ids)
{
    SyncPrimitiveScopedLock lock(shed->m_SyncPrimitive);

    VALIDATE_STATE(shed)
    if (task_count > shed->m_FreeTaskCount)
    {
        return false;
    }
    for (uint16_t i = 0; i < task_count; ++i)
    {
        BIKESHED_FATAL_ASSERT(task_functions[i] != 0, bail);
        TTaskIndex task_index = shed->m_FreeTaskIndexes[--shed->m_FreeTaskCount];
        out_task_ids[i] = TASK_ID(task_index, shed->m_Generation);
        Task* task = &shed->m_Tasks[task_index - 1];
        task->m_Generation = shed->m_Generation;
        task->m_ChildDependencyCount = 0;
        task->m_FirstParentDependencyIndex = 0;
        task->m_TaskFunc = task_functions[i];
        task->m_TaskContextData = task_context_data[i];
    }
    ++shed->m_Generation;
    VALIDATE_STATE(shed)
    return true;
BIKESHED_ASSERT_EXIT(bail)
    return false;
}

bool AddTaskDependencies(HShed shed, TTaskID task_id, uint16_t task_count, const TTaskID* dependency_task_ids)
{
    SyncPrimitiveScopedLock lock(shed->m_SyncPrimitive);
    VALIDATE_STATE(shed)
    if (task_count > shed->m_FreeDependencyCount)
    {
        return false;
    }

    TTaskIndex task_index = TASK_INDEX(task_id);
    Task* task = &shed->m_Tasks[task_index - 1];
    BIKESHED_FATAL_ASSERT(TASK_GENERATION(task_id) == task->m_Generation, bail);

    for (uint16_t i = 0; i < task_count; ++i)
    {
        TTaskID dependency_task_id = dependency_task_ids[i];
        TTaskIndex dependency_task_index = TASK_INDEX(dependency_task_id);
        Task* dependency_task = &shed->m_Tasks[dependency_task_index - 1];
        BIKESHED_FATAL_ASSERT(TASK_GENERATION(dependency_task_id) == dependency_task->m_Generation, bail);
        TDependencyIndex dependency_index = shed->m_FreeDependencyIndexes[--shed->m_FreeDependencyCount];
        Dependency* dependency = &shed->m_Dependencies[dependency_index - 1];
        dependency->m_ParentTaskIndex = task_index;
        dependency->m_NextParentDependencyIndex = dependency_task->m_FirstParentDependencyIndex;
        dependency_task->m_FirstParentDependencyIndex = dependency_index;
    }
    task->m_ChildDependencyCount += task_count;

    VALIDATE_STATE(shed)
    return true;
BIKESHED_ASSERT_EXIT(bail)
    return false;
}

void ReadyTasks(HShed shed, uint16_t task_count, const TTaskID* task_ids)
{
    if (task_count == 0)
    {
        return;
    }
    {
        SyncPrimitiveScopedLock lock(shed->m_SyncPrimitive);
        VALIDATE_STATE(shed)
        BIKESHED_FATAL_ASSERT(task_count <= shed->m_FreeReadyCount, bail);

        for (uint16_t i = 0; i < task_count; ++i)
        {
            TTaskID task_id = task_ids[i];
			BIKESHED_FATAL_ASSERT(TASK_GENERATION(task_id) == shed->m_Tasks[TASK_INDEX(task_id) - 1].m_Generation, bail);
			BIKESHED_FATAL_ASSERT(shed->m_Tasks[TASK_INDEX(task_id) - 1].m_ChildDependencyCount == 0, bail);
            SyncedReadyTask(shed, task_id);
        }

        VALIDATE_STATE(shed)
    }
    if (shed->m_SyncPrimitive)
    {
        shed->m_SyncPrimitive->SignalReady(shed->m_SyncPrimitive, task_count);
    }
BIKESHED_ASSERT_EXIT(bail)
    return;
}

void ExecuteAndResolveTask(HShed shed, TTaskID task_id, ResolvedCallback* resolves_callback)
{
    TTaskIndex task_index = TASK_INDEX(task_id);
    Task* task = &shed->m_Tasks[task_index - 1];
    BIKESHED_FATAL_ASSERT(TASK_GENERATION(task_id) == task->m_Generation, bail);

    TaskResult task_result = task->m_TaskFunc(shed, task_id, task->m_TaskContextData);

    if (task_result == TASK_RESULT_BLOCKED)
    {
        return;
    }

    {
        SyncPrimitiveScopedLock lock(shed->m_SyncPrimitive);
        if (task_result == TASK_RESULT_COMPLETE)
        {
            SyncedResolveTask(shed, task_id, resolves_callback);
            SyncedFreeTask(shed, task_id);
        }
        else if (task_result == TASK_RESULT_YIELD)
        {
            SyncedReadyTask(shed, task_id);
        }
        VALIDATE_STATE(shed)
    }
BIKESHED_ASSERT_EXIT(bail)
    return;
}

bool ExecuteOneTask(HShed shed, ResolvedCallback* resolves_callback, TTaskID* executed_task_id)
{
    TTaskID task_id;
    {
        SyncPrimitiveScopedLock lock(shed->m_SyncPrimitive);
        VALIDATE_STATE(shed)
        if (!SyncGetFirstReadyTask(shed, &task_id))
        {
            return false;
        }
    }

    ExecuteAndResolveTask(shed, task_id, resolves_callback);
    if (executed_task_id != 0)
    {
        *executed_task_id = task_id;
    }
    return true;
}

}

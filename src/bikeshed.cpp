#include "bikeshed.h"

#if !defined(BIKESHED_ATOMICADD)
    #if defined(_MSC_VER)
        #if !defined(_WINDOWS_)
            #define WIN32_LEAN_AND_MEAN
            #include <Windows.h>
            #undef WIN32_LEAN_AND_MEAN
        #endif

        #define BIKESHED_ATOMICADD(value, amount) (_InterlockedExchangeAdd(value, amount) + amount)
    #endif
    #if defined(__clang__) || defined(__GNUC__)
        #define BIKESHED_ATOMICADD(value, amount) (__sync_fetch_and_add(value, amount) + amount)
    #endif
#endif

#if !defined(BIKESHED_ATOMICCAS)
    #if defined(_MSC_VER)
        #if !defined(_WINDOWS_)
            #define WIN32_LEAN_AND_MEAN
            #include <Windows.h>
            #undef WIN32_LEAN_AND_MEAN
        #endif

        #define BIKESHED_ATOMICCAS(store, compare, value) _InterlockedCompareExchange(store, value, compare)
    #endif
    #if defined(__clang__) || defined(__GNUC__)
        #define BIKESHED_ATOMICCAS(store, compare, value) __sync_val_compare_and_swap(store, compare, value)
    #endif
#endif

#define ALIGN_SIZE(x, align) (((x) + ((align)-1)) & ~((align)-1))

#if defined(BIKESHED_ASSERTS)
#    define BIKESHED_FATAL_ASSERT(x, bail) \
        if (gAssert && !(x)) \
        { \
            gAssert(__FILE__, __LINE__); \
            bail; \
        }
#else // defined(BIKESHED_ASSERTS)
#    define BIKESHED_FATAL_ASSERT(x, y)
#endif // defined(BIKESHED_ASSERTS)

namespace bikeshed {

#if defined(BIKESHED_ASSERTS)
static Assert gAssert = 0;
#endif // defined(BIKESHED_ASSERTS)

void SetAssert(Assert assert_func)
{
#if defined(BIKESHED_ASSERTS)
    gAssert = assert_func;
#else  // defined(BIKESHED_ASSERTS)
    assert_func = 0;
#endif // defined(BIKESHED_ASSERTS)
}

typedef uint32_t TTaskIndex;
typedef uint32_t TDependencyIndex;
typedef uint32_t TReadyIndex;

static const uint32_t BIKSHED_GENERATION_SHIFT = 23u;
static const uint32_t BIKSHED_INDEX_MASK       = 0x007fffffu;
static const uint32_t BIKSHED_GENERATION_MASK  = 0xff800000u;

#define TASK_ID(index, generation) (((TTaskID)(generation) << BIKSHED_GENERATION_SHIFT) + index)
#define TASK_GENERATION(task_id) ((long)(task_id >> BIKSHED_GENERATION_SHIFT))
#define TASK_INDEX(task_id) ((TTaskIndex)(task_id & BIKSHED_INDEX_MASK))

struct Pool
{
    long volatile  m_Generation;
    long volatile* m_Head;
};

inline void PoolPush(long volatile* head, long volatile* generation, uint32_t index)
{
    uint32_t gen = (((uint32_t)BIKESHED_ATOMICADD(generation, 1)) << BIKSHED_GENERATION_SHIFT) & BIKSHED_GENERATION_MASK;
    uint32_t new_head = gen | index;

    uint32_t current_head = (uint32_t)head[0];
    head[index] = (long)(current_head & BIKSHED_INDEX_MASK);

    while (BIKESHED_ATOMICCAS(&head[0], (long)current_head, (long)new_head) != (long)current_head)
    {
        current_head = (uint32_t)head[0];
        head[index] = (long)(current_head & BIKSHED_INDEX_MASK);
    }
}

inline uint32_t PoolPop(long volatile* head)
{
    do
    {
        uint32_t current_head = (uint32_t)head[0];
        uint32_t head_index = current_head & BIKSHED_INDEX_MASK;
        if (head_index == 0)
        {
            return 0;
        }

        uint32_t next = (uint32_t)head[head_index];
        uint32_t new_head = (current_head & BIKSHED_GENERATION_MASK) | next;

        if (BIKESHED_ATOMICCAS(&head[0], (long)current_head, (long)new_head) == (long)current_head)
        {
            return head_index;
        }
    } while(1);
}

static void PoolInitialize(long volatile* head, uint32_t fill_count)
{
    if (fill_count > 0)
    {
        head[0] = 1;
        for (uint32_t i = 1; i < fill_count; ++i)
        {
            head[i] = i + 1;
        }
        head[fill_count] = 0;
    }
    else
    {
        head[0] = 0;
    }
}

struct Dependency
{
    TTaskIndex       m_ParentTaskIndex;
    TDependencyIndex m_NextParentDependencyIndex;
};

struct Task
{
    long volatile    m_ChildDependencyCount;
    TTaskID          m_TaskID;
    TDependencyIndex m_FirstParentDependencyIndex;
    TaskFunc         m_TaskFunc;
    void*            m_TaskContextData;
};

struct Shed
{
    Task*                     m_Tasks;
    Dependency*               m_Dependencies;
    Pool                      m_TaskIndexPool;
    Pool                      m_DependencyIndexPool;
    Pool                      m_ReadyQueue;
    long volatile             m_TaskGeneration;
    ReadyCallback*            m_ReadyCallback;
};

static void AsyncFreeTask(HShed shed, TTaskID task_id)
{
    TTaskIndex task_index = TASK_INDEX(task_id);
    Task*      task       = &shed->m_Tasks[task_index - 1];
    BIKESHED_FATAL_ASSERT(task_id == task->m_TaskID, return );
    BIKESHED_FATAL_ASSERT(0 == task->m_ChildDependencyCount, return );

    task->m_TaskID                                   = 0;
    TDependencyIndex dependency_index                = task->m_FirstParentDependencyIndex;
    while (dependency_index != 0)
    {
        Dependency* dependency                  = &shed->m_Dependencies[dependency_index - 1];
        TDependencyIndex next_dependency_index  = dependency->m_NextParentDependencyIndex;
        PoolPush(shed->m_DependencyIndexPool.m_Head, &shed->m_DependencyIndexPool.m_Generation, dependency_index);
        dependency_index                        = next_dependency_index;
    }
    PoolPush(shed->m_TaskIndexPool.m_Head, &shed->m_TaskIndexPool.m_Generation, task_index);
}

static void AsyncReadyTask(HShed shed, TTaskID task_id)
{
    TTaskIndex task_index = TASK_INDEX(task_id);
    BIKESHED_FATAL_ASSERT(task_id == shed->m_Tasks[task_index - 1].m_TaskID, return);
    BIKESHED_FATAL_ASSERT(0x20000000 == BIKESHED_ATOMICADD(&shed->m_Tasks[task_index - 1].m_ChildDependencyCount, 0x20000000), return);

    PoolPush(shed->m_ReadyQueue.m_Head, &shed->m_ReadyQueue.m_Generation, task_index);
}

static void AsyncResolveTask(HShed shed, TTaskID task_id, TTaskID* out_next_ready_task_id)
{
    TTaskIndex task_index = TASK_INDEX(task_id);
    Task*      task       = &shed->m_Tasks[task_index - 1];
    BIKESHED_FATAL_ASSERT(task_id == task->m_TaskID, return );
    TDependencyIndex dependency_index = task->m_FirstParentDependencyIndex;

    while (dependency_index != 0)
    {
        Dependency* dependency        = &shed->m_Dependencies[dependency_index - 1];
        TTaskIndex  parent_task_index = dependency->m_ParentTaskIndex;
        Task*       parent_task       = &shed->m_Tasks[parent_task_index - 1];
        TTaskID     parent_task_id    = TASK_ID(parent_task_index, TASK_GENERATION(parent_task->m_TaskID));
        long child_dependency_count   = BIKESHED_ATOMICADD(&parent_task->m_ChildDependencyCount, -1);
        if (child_dependency_count == 0)
        {
            if (out_next_ready_task_id && *out_next_ready_task_id == 0)
            {
                BIKESHED_FATAL_ASSERT(0x20000000 == BIKESHED_ATOMICADD(&parent_task->m_ChildDependencyCount, 0x20000000), return);
                *out_next_ready_task_id = parent_task_id;
            }
            else
            {
                AsyncReadyTask(shed, parent_task_id);
            }
        }
        dependency_index = dependency->m_NextParentDependencyIndex;
    }
}

static bool AsyncGetFirstReadyTask(HShed shed, TTaskID* out_task_id)
{
    uint32_t ready_index = PoolPop(shed->m_ReadyQueue.m_Head);
    if (ready_index == 0)
    {
        return false;
    }
    Task* task = &shed->m_Tasks[ready_index - 1];
    *out_task_id = task->m_TaskID;
    return true;
}

uint32_t GetShedSize(uint32_t max_task_count, uint32_t max_dependency_count)
{
    uint32_t size = (uint32_t)ALIGN_SIZE(sizeof(Shed), 8) +
        (uint32_t)ALIGN_SIZE((sizeof(Task) * max_task_count), 8) +
        (uint32_t)ALIGN_SIZE((sizeof(Dependency) * max_dependency_count), 8) +
        (uint32_t)ALIGN_SIZE((sizeof(long volatile) * (1 + max_task_count)), 4) +
        (uint32_t)ALIGN_SIZE((sizeof(long volatile) * (1 + max_dependency_count)), 4) +
        (uint32_t)ALIGN_SIZE((sizeof(long volatile) * (1 + max_task_count)), 4);
    return size;
}

HShed CreateShed(void* mem, uint32_t max_task_count, uint32_t max_dependency_count, ReadyCallback* sync_primitive)
{
    if (max_task_count !=  TASK_INDEX(max_task_count))
    {
        return 0;
    }
    HShed shed                  = (HShed)mem;
    shed->m_TaskGeneration      = 1;
    uint8_t* p                  = (uint8_t*)mem;
    p += ALIGN_SIZE(sizeof(Shed), 8);
    shed->m_Tasks = (Task*)((void*)p);
    p += ALIGN_SIZE((sizeof(Task) * max_task_count), 8);
    shed->m_Dependencies = (Dependency*)((void*)p);
    p += ALIGN_SIZE((sizeof(Dependency) * max_dependency_count), 8);
    shed->m_TaskIndexPool.m_Head = (long volatile*)(void*)p;
    p += ALIGN_SIZE((sizeof(long volatile) * (1 +  max_task_count)), 4);
    shed->m_DependencyIndexPool.m_Head = (long volatile*)(void*)p;
    p += ALIGN_SIZE((sizeof(long volatile) * (1 + max_dependency_count)), 4);
    shed->m_ReadyQueue.m_Head = (long volatile*)(void*)p;
    p += ALIGN_SIZE((sizeof(long volatile) * (1 + max_task_count)), 4);

    shed->m_ReadyCallback = sync_primitive;

    PoolInitialize(shed->m_TaskIndexPool.m_Head, max_task_count);
    shed->m_TaskIndexPool.m_Generation = 0;

    PoolInitialize(shed->m_DependencyIndexPool.m_Head, max_dependency_count);
    shed->m_DependencyIndexPool.m_Generation = 0;

    PoolInitialize(shed->m_ReadyQueue.m_Head, 0);
    shed->m_ReadyQueue.m_Generation = 0;

    return shed;
}

bool CreateTasks(HShed shed, uint32_t task_count, TaskFunc* task_functions, void** task_context_data, TTaskID* out_task_ids)
{
    long generation = BIKESHED_ATOMICADD(&shed->m_TaskGeneration, 1);
    for (uint32_t i = 0; i < task_count; ++i)
    {
        BIKESHED_FATAL_ASSERT(task_functions[i] != 0, return false);
        TTaskIndex task_index = (TTaskIndex)PoolPop(shed->m_TaskIndexPool.m_Head);
        if (task_index == 0)
        {
            while (i > 0)
            {
                --i;
                PoolPush(shed->m_TaskIndexPool.m_Head, &shed->m_TaskIndexPool.m_Generation, out_task_ids[i]);
            }
            return false;
        }
        TTaskID task_id                    = TASK_ID(task_index, generation);
        out_task_ids[i]                    = task_id;
        Task* task                         = &shed->m_Tasks[task_index - 1];
        task->m_TaskID                     = task_id;
        task->m_ChildDependencyCount       = 0;
        task->m_FirstParentDependencyIndex = 0;
        task->m_TaskFunc                   = task_functions[i];
        task->m_TaskContextData            = task_context_data[i];
    }
    return true;
}

bool AddTaskDependencies(HShed shed, TTaskID task_id, uint32_t task_count, const TTaskID* dependency_task_ids)
{
    TTaskIndex task_index = TASK_INDEX(task_id);
    Task*      task       = &shed->m_Tasks[task_index - 1];
    BIKESHED_FATAL_ASSERT(task_id == task->m_TaskID, return false);
    BIKESHED_FATAL_ASSERT(0 == task->m_ChildDependencyCount, return false);

    for (uint32_t i = 0; i < task_count; ++i)
    {
        TTaskID    dependency_task_id    = dependency_task_ids[i];
        TTaskIndex dependency_task_index = TASK_INDEX(dependency_task_id);
        Task*      dependency_task       = &shed->m_Tasks[dependency_task_index - 1];
        BIKESHED_FATAL_ASSERT(dependency_task_id == dependency_task->m_TaskID, return false);
        TDependencyIndex dependency_index   = (TDependencyIndex)PoolPop(shed->m_DependencyIndexPool.m_Head);
        if (dependency_index == 0)
        {
            dependency_index = dependency_task->m_FirstParentDependencyIndex;

            while (dependency_index != 0)
            {
                Dependency* dependency                  = &shed->m_Dependencies[dependency_index - 1];
                TDependencyIndex next_dependency_index  = dependency->m_NextParentDependencyIndex;
                PoolPush(shed->m_DependencyIndexPool.m_Head, &shed->m_DependencyIndexPool.m_Generation, dependency_index);
                dependency_index                        = next_dependency_index;
            }
            return false;
        }
        Dependency*      dependency                   = &shed->m_Dependencies[dependency_index - 1];
        dependency->m_ParentTaskIndex                 = task_index;
        dependency->m_NextParentDependencyIndex       = dependency_task->m_FirstParentDependencyIndex;
        dependency_task->m_FirstParentDependencyIndex = dependency_index;
    }
    BIKESHED_ATOMICADD(&task->m_ChildDependencyCount, task_count);
    BIKESHED_FATAL_ASSERT(task->m_ChildDependencyCount >= (long)task_count, return false);

    return true;
}

void ReadyTasks(HShed shed, uint32_t task_count, const TTaskID* task_ids)
{
    if (task_count == 0)
    {
        return;
    }
    {
        uint32_t i = task_count;
        do
        {
            TTaskID task_id = task_ids[--i];
            BIKESHED_FATAL_ASSERT(0 != shed->m_Tasks[TASK_INDEX(task_id) - 1].m_TaskFunc, return );
            BIKESHED_FATAL_ASSERT(task_id == shed->m_Tasks[TASK_INDEX(task_id) - 1].m_TaskID, return );
            // TODO, we could be more efficient if we built the ready chain and just
            // inserted the head of the chain and pointed to the tail on the last readied task
            AsyncReadyTask(shed, task_id);
        } while(i > 0);
    }
    if (shed->m_ReadyCallback)
    {
        shed->m_ReadyCallback->SignalReady(shed->m_ReadyCallback, task_count);
    }
}

void ExecuteAndResolveTask(HShed shed, TTaskID task_id, TTaskID* out_next_ready_task_id)
{
    if (out_next_ready_task_id)
    {
        *out_next_ready_task_id = 0;
    }
    TTaskIndex task_index = TASK_INDEX(task_id);
    Task*      task       = &shed->m_Tasks[task_index - 1];
    BIKESHED_FATAL_ASSERT(task_id == task->m_TaskID, return );

    TaskResult task_result = task->m_TaskFunc(shed, task_id, task->m_TaskContextData);

    if (task_result == TASK_RESULT_COMPLETE)
    {
        AsyncResolveTask(shed, task_id, out_next_ready_task_id);
        BIKESHED_FATAL_ASSERT(0 == BIKESHED_ATOMICADD(&task->m_ChildDependencyCount, -0x20000000), return);
        AsyncFreeTask(shed, task_id);
    }
    else if (task_result == TASK_RESULT_BLOCKED)
    {
        BIKESHED_FATAL_ASSERT(0 == BIKESHED_ATOMICADD(&task->m_ChildDependencyCount, -0x20000000), return);
    }

    if (out_next_ready_task_id && *out_next_ready_task_id == 0)
    {
        AsyncGetFirstReadyTask(shed, out_next_ready_task_id);
    }
}

bool ExecuteOneTask(HShed shed, TTaskID* out_next_ready_task_id)
{
    TTaskID task_id;
    {
        if (!AsyncGetFirstReadyTask(shed, &task_id))
        {
            return false;
        }
    }

    ExecuteAndResolveTask(shed, task_id, out_next_ready_task_id);
    return true;
}

} // namespace bikeshed

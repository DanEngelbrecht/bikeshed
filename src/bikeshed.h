#pragma once

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef void (*Bikeshed_Assert)(const char* file, int line);
void Bikeshed_SetAssert(Bikeshed_Assert assert_func);

typedef struct Bikeshed_Shed_private* Bikeshed;
struct Bikeshed_ReadyCallback
{
    void (*SignalReady)(struct Bikeshed_ReadyCallback* ready_callback, uint32_t ready_count);
};

typedef uint32_t Bikeshed_TaskID;

enum Bikeshed_TaskResult
{
    BIKESHED_TASK_RESULT_COMPLETE, // Task is complete, dependecies will be resolved and the task is freed
    BIKESHED_TASK_RESULT_BLOCKED   // Task is blocked, call ReadyTasks on the task id when ready to execute again
};

typedef enum Bikeshed_TaskResult (*BikeShed_TaskFunc)(Bikeshed shed, Bikeshed_TaskID task_id, void* context);

// Up to 8 388 607 tasks
uint32_t Bikeshed_GetSize(uint32_t max_task_count, uint32_t max_dependency_count);
Bikeshed Bikeshed_Create(void* mem, uint32_t max_task_count, uint32_t max_dependency_count, struct Bikeshed_ReadyCallback* ready_callback);
Bikeshed Bikeshed_CloneState(void* mem, Bikeshed original, uint32_t shed_size);

int Bikeshed_CreateTasks(Bikeshed shed, uint32_t task_count, BikeShed_TaskFunc* task_functions, void** contexts, Bikeshed_TaskID* out_task_ids);
void Bikeshed_ReadyTasks(Bikeshed shed, uint32_t task_count, const Bikeshed_TaskID* task_ids);

// Dependencies can not be added to a ready task
// You can not add dependecies in the BikeShed_TaskFunc to the executing task
int Bikeshed_AddDependencies(Bikeshed shed, Bikeshed_TaskID task_id, uint32_t task_count, const Bikeshed_TaskID* dependency_task_ids);

int Bikeshed_ExecuteOne(Bikeshed shed, Bikeshed_TaskID* out_next_ready_task_id);
void Bikeshed_ExecuteAndResolve(Bikeshed shed, Bikeshed_TaskID task_id, Bikeshed_TaskID* out_next_ready_task_id);

#if defined(BIKESHED_IMPLEMENTATION)

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

#define BIKESHED_ALIGN_SIZE_PRIVATE(x, align) (((x) + ((align)-1)) & ~((align)-1))

#if defined(BIKESHED_ASSERTS)
#    define BIKESHED_FATAL_ASSERT_PRIVATE(x, bail) \
        if (Bikeshed_Assert_private && !(x)) \
        { \
            Bikeshed_Assert_private(__FILE__, __LINE__); \
            bail; \
        }
#else // defined(BIKESHED_ASSERTS)
#    define BIKESHED_FATAL_ASSERT_PRIVATE(x, y)
#endif // defined(BIKESHED_ASSERTS)

#if defined(BIKESHED_ASSERTS)
static Bikeshed_Assert Bikeshed_Assert_private = 0;
#endif // defined(BIKESHED_ASSERTS)

void Bikeshed_SetAssert(Bikeshed_Assert assert_func)
{
#if defined(BIKESHED_ASSERTS)
    Bikeshed_Assert_private = assert_func;
#else  // defined(BIKESHED_ASSERTS)
    assert_func = 0;
#endif // defined(BIKESHED_ASSERTS)
}

typedef uint32_t Bikeshed_TaskIndex_private;
typedef uint32_t Bikeshed_DependencyIndex_private;
typedef uint32_t Bikeshed_ReadyIndex_private;

static const uint32_t BIKSHED_GENERATION_SHIFT_PRIVATE = 23u;
static const uint32_t BIKSHED_INDEX_MASK_PRIVATE       = 0x007fffffu;
static const uint32_t BIKSHED_GENERATION_MASK_PRIVATE  = 0xff800000u;

#define BIKESHED_TASK_ID_PRIVATE(index, generation) (((Bikeshed_TaskID)(generation) << BIKSHED_GENERATION_SHIFT_PRIVATE) + index)
#define BIKESHED_TASK_GENERATION_PRIVATE(task_id) ((long)(task_id >> BIKSHED_GENERATION_SHIFT_PRIVATE))
#define BIKESHED_TASK_INDEX_PRIVATE(task_id) ((Bikeshed_TaskIndex_private)(task_id & BIKSHED_INDEX_MASK_PRIVATE))

struct Bikeshed_Pool_private
{
    long volatile  m_Generation;
    long volatile* m_Head;
};

inline void Bikeshed_PoolPush_private(struct Bikeshed_Pool_private* pool, uint32_t index)
{
    uint32_t gen = (((uint32_t)BIKESHED_ATOMICADD(&pool->m_Generation, 1)) << BIKSHED_GENERATION_SHIFT_PRIVATE) & BIKSHED_GENERATION_MASK_PRIVATE;
    uint32_t new_head = gen | index;

    uint32_t current_head   = (uint32_t)pool->m_Head[0];
    pool->m_Head[index]     = (long)(current_head & BIKSHED_INDEX_MASK_PRIVATE);

    while (BIKESHED_ATOMICCAS(&pool->m_Head[0], (long)current_head, (long)new_head) != (long)current_head)
    {
        current_head        = (uint32_t)pool->m_Head[0];
        pool->m_Head[index] = (long)(current_head & BIKSHED_INDEX_MASK_PRIVATE);
    }
}

inline uint32_t Bikeshed_PoolPop_private(struct Bikeshed_Pool_private* pool)
{
    do
    {
        uint32_t current_head   = (uint32_t)pool->m_Head[0];
        uint32_t head_index     = current_head & BIKSHED_INDEX_MASK_PRIVATE;
        if (head_index == 0)
        {
            return 0;
        }

        uint32_t next       = (uint32_t)pool->m_Head[head_index];
        uint32_t new_head   = (current_head & BIKSHED_GENERATION_MASK_PRIVATE) | next;

        if (BIKESHED_ATOMICCAS(&pool->m_Head[0], (long)current_head, (long)new_head) == (long)current_head)
        {
            return head_index;
        }
    } while(1);
}

static void Bikeshed_PoolInitialize_private(struct Bikeshed_Pool_private* pool, uint32_t fill_count)
{
    pool->m_Generation = 0;
    if (fill_count == 0)
    {
        pool->m_Head[0] = 0;
        return;
    }
    for (uint32_t i = 0; i < fill_count; ++i)
    {
        pool->m_Head[i] = i + 1;
    }
    pool->m_Head[fill_count] = 0;
}

struct Bikeshed_Dependency_private
{
    Bikeshed_TaskIndex_private       m_ParentTaskIndex;
    Bikeshed_DependencyIndex_private m_NextParentDependencyIndex;
};

struct Bikeshed_Task_private
{
    long volatile                       m_ChildDependencyCount;
    Bikeshed_TaskID                     m_TaskID;
    Bikeshed_DependencyIndex_private    m_FirstParentDependencyIndex;
    BikeShed_TaskFunc                   m_TaskFunc;
    void*                               m_TaskContext;
};

struct Bikeshed_Shed_private
{
    struct Bikeshed_Task_private*       m_Tasks;
    struct Bikeshed_Dependency_private* m_Dependencies;
    struct Bikeshed_Pool_private        m_TaskIndexPool;
    struct Bikeshed_Pool_private        m_DependencyIndexPool;
    struct Bikeshed_Pool_private        m_ReadyQueue;
    long volatile                       m_TaskGeneration;
    struct Bikeshed_ReadyCallback*      m_ReadyCallback;
};

static void Bikeshed_AsyncFreeTask_private(Bikeshed shed, Bikeshed_TaskID task_id)
{
    Bikeshed_TaskIndex_private task_index  = BIKESHED_TASK_INDEX_PRIVATE(task_id);
    struct Bikeshed_Task_private* task     = &shed->m_Tasks[task_index - 1];
    BIKESHED_FATAL_ASSERT_PRIVATE(task_id == task->m_TaskID, return );
    BIKESHED_FATAL_ASSERT_PRIVATE(0 == task->m_ChildDependencyCount, return );

    task->m_TaskID                                    = 0;
    Bikeshed_DependencyIndex_private dependency_index = task->m_FirstParentDependencyIndex;
    while (dependency_index != 0)
    {
        struct Bikeshed_Dependency_private* dependency         = &shed->m_Dependencies[dependency_index - 1];
        Bikeshed_DependencyIndex_private next_dependency_index = dependency->m_NextParentDependencyIndex;
        Bikeshed_PoolPush_private(&shed->m_DependencyIndexPool, dependency_index);
        dependency_index    = next_dependency_index;
    }
    Bikeshed_PoolPush_private(&shed->m_TaskIndexPool, task_index);
}

inline void Bikeshed_AsyncReadyTask_private(Bikeshed shed, Bikeshed_TaskID task_id)
{
    Bikeshed_TaskIndex_private task_index = BIKESHED_TASK_INDEX_PRIVATE(task_id);
    BIKESHED_FATAL_ASSERT_PRIVATE(task_id == shed->m_Tasks[task_index - 1].m_TaskID, return);
    BIKESHED_FATAL_ASSERT_PRIVATE(0x20000000 == BIKESHED_ATOMICADD(&shed->m_Tasks[task_index - 1].m_ChildDependencyCount, 0x20000000), return);

    Bikeshed_PoolPush_private(&shed->m_ReadyQueue, task_index);
}

static void Bikeshed_AsyncResolveTask_private(Bikeshed shed, Bikeshed_TaskID task_id, Bikeshed_TaskID* out_next_ready_task_id)
{
    Bikeshed_TaskIndex_private task_index               = BIKESHED_TASK_INDEX_PRIVATE(task_id);
    struct Bikeshed_Task_private* task                  = &shed->m_Tasks[task_index - 1];
    BIKESHED_FATAL_ASSERT_PRIVATE(task_id == task->m_TaskID, return );
    Bikeshed_DependencyIndex_private dependency_index   = task->m_FirstParentDependencyIndex;

    while (dependency_index != 0)
    {
        struct Bikeshed_Dependency_private* dependency  = &shed->m_Dependencies[dependency_index - 1];
        Bikeshed_TaskIndex_private  parent_task_index   = dependency->m_ParentTaskIndex;
        struct Bikeshed_Task_private* parent_task       = &shed->m_Tasks[parent_task_index - 1];
        Bikeshed_TaskID parent_task_id                  = BIKESHED_TASK_ID_PRIVATE(parent_task_index, BIKESHED_TASK_GENERATION_PRIVATE(parent_task->m_TaskID));
        long child_dependency_count                     = BIKESHED_ATOMICADD(&parent_task->m_ChildDependencyCount, -1);
        if (child_dependency_count == 0)
        {
            if (out_next_ready_task_id && *out_next_ready_task_id == 0)
            {
                BIKESHED_FATAL_ASSERT_PRIVATE(0x20000000 == BIKESHED_ATOMICADD(&parent_task->m_ChildDependencyCount, 0x20000000), return);
                *out_next_ready_task_id = parent_task_id;
            }
            else
            {
                Bikeshed_AsyncReadyTask_private(shed, parent_task_id);
            }
        }
        dependency_index = dependency->m_NextParentDependencyIndex;
    }
}

inline int Bikeshed_AsyncGetFirstReadyTask_private(Bikeshed shed, Bikeshed_TaskID* out_task_id)
{
    uint32_t ready_index = Bikeshed_PoolPop_private(&shed->m_ReadyQueue);
    if (ready_index == 0)
    {
        return 0;
    }
    struct Bikeshed_Task_private* task = &shed->m_Tasks[ready_index - 1];
    *out_task_id = task->m_TaskID;
    return 1;
}

uint32_t Bikeshed_GetSize(uint32_t max_task_count, uint32_t max_dependency_count)
{
    uint32_t size =
        (uint32_t)BIKESHED_ALIGN_SIZE_PRIVATE(sizeof(struct Bikeshed_Shed_private), 8) +
        (uint32_t)BIKESHED_ALIGN_SIZE_PRIVATE((sizeof(struct Bikeshed_Task_private) * max_task_count), 8) +
        (uint32_t)BIKESHED_ALIGN_SIZE_PRIVATE((sizeof(struct Bikeshed_Dependency_private) * max_dependency_count), 8) +
        (uint32_t)BIKESHED_ALIGN_SIZE_PRIVATE((sizeof(long volatile) * (1 + max_task_count)), 4) +
        (uint32_t)BIKESHED_ALIGN_SIZE_PRIVATE((sizeof(long volatile) * (1 + max_dependency_count)), 4) +
        (uint32_t)BIKESHED_ALIGN_SIZE_PRIVATE((sizeof(long volatile) * (1 + max_task_count)), 4);
    return size;
}

Bikeshed Bikeshed_Create(void* mem, uint32_t max_task_count, uint32_t max_dependency_count, struct Bikeshed_ReadyCallback* sync_primitive)
{
    if (max_task_count != BIKESHED_TASK_INDEX_PRIVATE(max_task_count))
    {
        return 0;
    }

    Bikeshed shed                       = (Bikeshed)mem;
    shed->m_TaskGeneration              = 1;
    uint8_t* p                          = (uint8_t*)mem;
    p += BIKESHED_ALIGN_SIZE_PRIVATE(sizeof(struct Bikeshed_Shed_private), 8);
    shed->m_Tasks                       = (struct Bikeshed_Task_private*)((void*)p);
    p += BIKESHED_ALIGN_SIZE_PRIVATE((sizeof(struct Bikeshed_Task_private) * max_task_count), 8);
    shed->m_Dependencies                = (struct Bikeshed_Dependency_private*)((void*)p);
    p += BIKESHED_ALIGN_SIZE_PRIVATE((sizeof(struct Bikeshed_Dependency_private) * max_dependency_count), 8);
    shed->m_TaskIndexPool.m_Head        = (long volatile*)(void*)p;
    p += BIKESHED_ALIGN_SIZE_PRIVATE((sizeof(long volatile) * (1 +  max_task_count)), 4);
    shed->m_DependencyIndexPool.m_Head  = (long volatile*)(void*)p;
    p += BIKESHED_ALIGN_SIZE_PRIVATE((sizeof(long volatile) * (1 + max_dependency_count)), 4);
    shed->m_ReadyQueue.m_Head           = (long volatile*)(void*)p;
    p += BIKESHED_ALIGN_SIZE_PRIVATE((sizeof(long volatile) * (1 + max_task_count)), 4);

    shed->m_ReadyCallback               = sync_primitive;

    Bikeshed_PoolInitialize_private(&shed->m_TaskIndexPool, max_task_count);
    Bikeshed_PoolInitialize_private(&shed->m_DependencyIndexPool, max_dependency_count);
    Bikeshed_PoolInitialize_private(&shed->m_ReadyQueue, 0);

    return shed;
}

Bikeshed Bikeshed_CloneState(void* mem, Bikeshed original, uint32_t shed_size)
{
    memcpy(mem, original, shed_size);

    Bikeshed shed                       = (Bikeshed)mem;
    uint8_t* p                          = (uint8_t*)mem;
    shed->m_Tasks                       = (struct Bikeshed_Task_private*)(void*)(&p[(uintptr_t)original->m_Tasks - (uintptr_t)original]);
    shed->m_Dependencies                = (struct Bikeshed_Dependency_private*)(void*)(&p[(uintptr_t)original->m_Dependencies - (uintptr_t)original]);
    shed->m_TaskIndexPool.m_Head        = (long volatile*)(void*)(&p[(uintptr_t)original->m_TaskIndexPool.m_Head - (uintptr_t)original]);
    shed->m_DependencyIndexPool.m_Head  = (long volatile*)(void*)(&p[(uintptr_t)original->m_DependencyIndexPool.m_Head - (uintptr_t)original]);
    shed->m_ReadyQueue.m_Head           = (long volatile*)(void*)(&p[(uintptr_t)original->m_ReadyQueue.m_Head - (uintptr_t)original]);

    return shed;
}

int Bikeshed_CreateTasks(Bikeshed shed, uint32_t task_count, BikeShed_TaskFunc* task_functions, void** contexts, Bikeshed_TaskID* out_task_ids)
{
    long generation = BIKESHED_ATOMICADD(&shed->m_TaskGeneration, 1);
    for (uint32_t i = 0; i < task_count; ++i)
    {
        BIKESHED_FATAL_ASSERT_PRIVATE(task_functions[i] != 0, return 0);
        Bikeshed_TaskIndex_private task_index = (Bikeshed_TaskIndex_private)Bikeshed_PoolPop_private(&shed->m_TaskIndexPool);
        if (task_index == 0)
        {
            while (i > 0)
            {
                --i;
                Bikeshed_PoolPush_private(&shed->m_TaskIndexPool, out_task_ids[i]);
            }
            return 0;
        }
        Bikeshed_TaskID task_id            = BIKESHED_TASK_ID_PRIVATE(task_index, generation);
        out_task_ids[i]                    = task_id;
        struct Bikeshed_Task_private* task = &shed->m_Tasks[task_index - 1];
        task->m_TaskID                     = task_id;
        task->m_ChildDependencyCount       = 0;
        task->m_FirstParentDependencyIndex = 0;
        task->m_TaskFunc                   = task_functions[i];
        task->m_TaskContext                = contexts[i];
    }
    return 1;
}

int Bikeshed_AddDependencies(Bikeshed shed, Bikeshed_TaskID task_id, uint32_t task_count, const Bikeshed_TaskID* dependency_task_ids)
{
    Bikeshed_TaskIndex_private task_index   = BIKESHED_TASK_INDEX_PRIVATE(task_id);
    struct Bikeshed_Task_private* task      = &shed->m_Tasks[task_index - 1];
    BIKESHED_FATAL_ASSERT_PRIVATE(task_id == task->m_TaskID, return 0);
    BIKESHED_FATAL_ASSERT_PRIVATE(task->m_ChildDependencyCount < 0x20000000, return 0);

    for (uint32_t i = 0; i < task_count; ++i)
    {
        Bikeshed_TaskID dependency_task_id                  = dependency_task_ids[i];
        Bikeshed_TaskIndex_private dependency_task_index    = BIKESHED_TASK_INDEX_PRIVATE(dependency_task_id);
        struct Bikeshed_Task_private* dependency_task       = &shed->m_Tasks[dependency_task_index - 1];
        BIKESHED_FATAL_ASSERT_PRIVATE(dependency_task_id == dependency_task->m_TaskID, return 0);
        Bikeshed_DependencyIndex_private dependency_index   = (Bikeshed_DependencyIndex_private)Bikeshed_PoolPop_private(&shed->m_DependencyIndexPool);
        if (dependency_index == 0)
        {
            dependency_index = dependency_task->m_FirstParentDependencyIndex;

            while (dependency_index != 0)
            {
                struct Bikeshed_Dependency_private* dependency          = &shed->m_Dependencies[dependency_index - 1];
                Bikeshed_DependencyIndex_private next_dependency_index  = dependency->m_NextParentDependencyIndex;
                Bikeshed_PoolPush_private(&shed->m_DependencyIndexPool, dependency_index);
                dependency_index    = next_dependency_index;
            }
            return 0;
        }
        struct Bikeshed_Dependency_private* dependency  = &shed->m_Dependencies[dependency_index - 1];
        dependency->m_ParentTaskIndex                   = task_index;
        dependency->m_NextParentDependencyIndex         = dependency_task->m_FirstParentDependencyIndex;
        dependency_task->m_FirstParentDependencyIndex   = dependency_index;
    }
    BIKESHED_ATOMICADD(&task->m_ChildDependencyCount, task_count);

    return 1;
}

void Bikeshed_ReadyTasks(Bikeshed shed, uint32_t task_count, const Bikeshed_TaskID* task_ids)
{
    if (task_count == 0)
    {
        return;
    }
    {
        uint32_t i = task_count;
        do
        {
            Bikeshed_TaskID task_id = task_ids[--i];
            BIKESHED_FATAL_ASSERT_PRIVATE(0 != shed->m_Tasks[BIKESHED_TASK_INDEX_PRIVATE(task_id) - 1].m_TaskFunc, return );
            BIKESHED_FATAL_ASSERT_PRIVATE(task_id == shed->m_Tasks[BIKESHED_TASK_INDEX_PRIVATE(task_id) - 1].m_TaskID, return );
            // TODO, we could be more efficient if we built the ready chain and just
            // inserted the head of the chain and pointed to the tail on the last readied task
            Bikeshed_AsyncReadyTask_private(shed, task_id);
        } while(i > 0);
    }
    if (shed->m_ReadyCallback)
    {
        shed->m_ReadyCallback->SignalReady(shed->m_ReadyCallback, task_count);
    }
}

void Bikeshed_ExecuteAndResolve(Bikeshed shed, Bikeshed_TaskID task_id, Bikeshed_TaskID* out_next_ready_task_id)
{
    if (out_next_ready_task_id)
    {
        *out_next_ready_task_id = 0;
    }
    Bikeshed_TaskIndex_private task_index   = BIKESHED_TASK_INDEX_PRIVATE(task_id);
    struct Bikeshed_Task_private* task      = &shed->m_Tasks[task_index - 1];
    BIKESHED_FATAL_ASSERT_PRIVATE(task_id == task->m_TaskID, return );

    enum Bikeshed_TaskResult task_result    = task->m_TaskFunc(shed, task_id, task->m_TaskContext);

    if (task_result == BIKESHED_TASK_RESULT_COMPLETE)
    {
        Bikeshed_AsyncResolveTask_private(shed, task_id, out_next_ready_task_id);
        BIKESHED_FATAL_ASSERT_PRIVATE(0 == BIKESHED_ATOMICADD(&task->m_ChildDependencyCount, -0x20000000), return);
        Bikeshed_AsyncFreeTask_private(shed, task_id);
    }
    else if (task_result == BIKESHED_TASK_RESULT_BLOCKED)
    {
        BIKESHED_FATAL_ASSERT_PRIVATE(0 == BIKESHED_ATOMICADD(&task->m_ChildDependencyCount, -0x20000000), return);
    }

    if (out_next_ready_task_id && *out_next_ready_task_id == 0)
    {
        Bikeshed_AsyncGetFirstReadyTask_private(shed, out_next_ready_task_id);
    }
}

int Bikeshed_ExecuteOne(Bikeshed shed, Bikeshed_TaskID* out_next_ready_task_id)
{
    Bikeshed_TaskID task_id;
    {
        if (!Bikeshed_AsyncGetFirstReadyTask_private(shed, &task_id))
        {
            return 0;
        }
    }

    Bikeshed_ExecuteAndResolve(shed, task_id, out_next_ready_task_id);
    return 1;
}

#endif // !defined(BIKESHED_IMPLEMENTATION)

#ifdef __cplusplus
}
#endif // __cplusplus

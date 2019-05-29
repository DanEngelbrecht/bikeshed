#ifndef BIKESHED_INCLUDEGUARD_PRIVATE_H
#define BIKESHED_INCLUDEGUARD_PRIVATE_H

/*
bikeshed.h - public domain - Dan Engelbrecht @DanEngelbrecht, 2019

# BIKESHED

Lock free hierarchical work scheduler, builds with MSVC, Clang and GCC, header only, C99 compliant, MIT license.

See github for latest version: https://github.com/DanEngelbrecht/bikeshed

See design blogs at: https://danengelbrecht.github.io

## Version history

### Version v0.4 18/5 2019

**Pre-release 4**

### API changes
  - Bikeshed_AddDependencies to take array of parent task task_ids
  - Bikeshed_ReadyCallback now gets called per channel range

### API additions
  - Bikeshed_FreeTasks
  - BIKESHED_L1CACHE_SIZE to control ready head alignement - no padding/alignement added by default
  - BIKESHED_CPU_YIELD to control yielding in high-contention scenarios

### Fixes
  - Added default (non-atomic) implementations for helper for unsupported platforms/compilers
  - Codacy analisys and badge
  - More input validation on public apis when BIKESHED_ASSERTS is enabled
  - Batch allocation of task indexes
  - Batch allocation of dependency indexes
  - Batch free of task indexes
  - Batch free of dependency indexes
  - Fixed broken channel range detection when resolving dependencies

### Version v0.3 1/5 2019

**Pre-release 3**

#### Fixes

- Ready callback is now called when a task is readied via dependency resolve
- Tasks are readied in batch when possible

### Version v0.2 29/4 2019

**Pre-release 2**

#### Fixes

- Internal cleanups
- Fixed warnings and removed clang warning suppressions
  - `-Wno-sign-conversion`
  - `-Wno-unused-macros`
  - `-Wno-c++98-compat`
  - `-Wno-implicit-fallthrough`
- Made it compile cleanly with clang++ on Windows

### Version v0.1 26/4 2019

**Pre-release 1**

## Usage
In *ONE* source file, put:

```C
#define BIKESHED_IMPLEMENTATION
// Define any overrides of platform specific implementations before including bikeshed.h.
#include "bikeshed.h"
```

Other source files should just #include "bikeshed.h"

## Macros

BIKESHED_IMPLEMENTATION
Define in one compilation unit to include implementation.

BIKESHED_ASSERTS
Define if you want Bikeshed to validate API usage, make sure to set the assert callback using
Bikeshed_SetAssert and take appropriate action if triggered.

BIKESHED_ATOMICADD
Macro for platform specific implementation of an atomic addition. Returns the result of the operation.
#define BIKESHED_ATOMICADD(value, amount) return MyAtomicAdd(value, amount)

BIKESHED_ATOMICCAS
Macro for platform specific implementation of an atomic compare and swap. Returns the previous value of "store".
#define BIKESHED_ATOMICCAS(store, compare, value) return MyAtomicCAS(store, compare, value);

BIKESHED_CPU_YIELD
Macro to yield the CPU when there is contention.

BIKESHED_L1CACHE_SIZE
Macro to specify the L1 cache line size - used to align data and ready heads to cache line boundaries. If not
defined the data will no be padded. If the CPU cacheline size is 64, set BIKESHED_L1CACHE_SIZE to reduce cache thrashing.

## Notes
Macros, typedefs, structs and functions suffixed with "_private" are internal and subject to change.

## Dependencies
Standard C library headers:
#include <stdint.h>
#include <string.h>

If either BIKESHED_ATOMICADD, BIKESHED_ATOMICCAS or BIKESHED_CPU_YIELD is not defined the MSVC/Windows implementation will
include <Windows.h> to get access to `_InterlockedExchangeAdd`, `_InterlockedCompareExchange` and `YieldProcessor` respectively.

The GCC/clang implementation relies on the compiler intrisics `__sync_fetch_and_add` and `__sync_val_compare_and_swap`.
If BIKESHED_CPU_YIELD is not defined <emmintrin.h> will be included to get access to `_mm_pause`

## License

MIT License

Copyright (c) 2019 Dan Engelbrecht

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

//  ------------- Public API Begin

// Custom assert hook
// If BIKESHED_ASSERTS is defined, Bikeshed will validate input parameters to make sure
// correct API usage.
// The callback will be called if an API error is detected and the library function will bail.
//
// Warning: If an API function asserts the state of the Bikeshed might be compromised.
// To reset the assert callback call Bikeshed_SetAssert(0);
typedef void (*Bikeshed_Assert)(const char* expression, const char* file, int line);
void Bikeshed_SetAssert(Bikeshed_Assert assert_func);

// Bikeshed handle
typedef struct Bikeshed_Shed_private* Bikeshed;

// Callback instance and hook for the library user to implement.
// Called when one or more tasks are "readied" - either by a call to Bikeshed_ReadyTasks or if
// a task has been resolved due to all dependencies have finished.
//
// struct MyReadyCallback {
//      // Add the Bikeshed_ReadyCallback struct *first* in your struct
//      struct Bikeshed_ReadyCallback cb;
//      // Add any custom data here
//      static void Ready(struct Bikeshed_ReadyCallback* ready_callback, uint32_t ready_count)
//      {
//          MyReadyCallback* _this = (MyReadyCallback*)ready_callback;
//          // Your code goes here
//      }
// };
// MyReadyCallback myCallback;
// myCallback.cb.SignalReady = MyReadyCallback::Ready;
// bikeshed = CreateBikeshed(mem, max_task_count, max_dependency_count, channel_count, &myCallback.cb);
//
// The Bikeshed_ReadyCallback instance must have a lifetime that starts before and ends after the Bikeshed instance.
struct Bikeshed_ReadyCallback
{
    void (*SignalReady)(struct Bikeshed_ReadyCallback* ready_callback, uint8_t channel, uint32_t ready_count);
};

// Task identifier
typedef uint32_t Bikeshed_TaskID;

// Result codes for task execution
enum Bikeshed_TaskResult
{
    BIKESHED_TASK_RESULT_COMPLETE, // Task is complete, dependecies will be resolved and the task is automatically freed
    BIKESHED_TASK_RESULT_BLOCKED   // Task is blocked, call ReadyTasks on the task id when ready to execute again
};

// Task execution callback
// A task execution function may:
//  - Create new tasks
//  - Add new dependencies to tasks that are not "Ready" or "Executing"
//  - Ready other tasks that are not "Ready" or "Executing"
//  - Deallocate any memory associated with the context
//
// A task execution function may not:
//  - Add dependencies to the executing task
//  - Ready the executing task
//  - Destroy the Bikeshed instance
//
// A task execution function should not:
//  - Block, if the function blocks the thread that called Bikeshed_ExecuteOne will block, return BIKESHED_TASK_RESULT_BLOCKED instead
//  - Call Bikeshed_ExecuteOne, this works but makes little sense
typedef enum Bikeshed_TaskResult (*BikeShed_TaskFunc)(Bikeshed shed, Bikeshed_TaskID task_id, uint8_t channel, void* context);

// Calculates the memory needed for a Bikeshed instance
// BIKESHED_SIZE is a macro which allows the Bikeshed to be allocated on the stack without heap allocations
//
// max_task_count: 1 to 8 388 607 tasks
// max_dependency_count: 0 to 8 388 607 dependencies
// channel_count: 1 to 255 channels
#define BIKESHED_SIZE(max_task_count, max_dependency_count, channel_count)

// Create a Bikeshed at the provided memory location
// Use BIKESHED_SIZE to get the required size of the memory block
// max_task_count, max_dependency_count and channel_count must match call to BIKESHED_SIZE
//
// ready_callback is optional and may be 0
//
// The returned Bikeshed is a typed pointer that points to same address as `mem`
Bikeshed Bikeshed_Create(void* mem, uint32_t max_task_count, uint32_t max_dependency_count, uint8_t channel_count, struct Bikeshed_ReadyCallback* ready_callback);

// Clones the state of a Bikeshed
// Use BIKESHED_SIZE to get the required size of the memory block
// max_task_count, max_dependency_count and channel_count must match call to BIKESHED_SIZE
//
// Makes a complete copy of the current state of a Bikeshed, executing on the clone copy will not affect the
// bikeshed state of the original.
//
// The returned Bikeshed is a typed pointer that points to same address as mem
Bikeshed Bikeshed_CloneState(void* mem, Bikeshed original, uint32_t shed_size);

// Creates one or more tasks
// Reserves task_count number of tasks from the internal Bikeshed state
// Caller is responsible for making sure the context pointers are valid until the corresponding task is executed
// Tasks will not be executed until they are readied with Bikeshed_ReadyTasks or if it has dependencies that have all been resolved
// Returns:
//  1 - Success
//  0 - Not enough free task instances in the bikeshed
int Bikeshed_CreateTasks(Bikeshed shed, uint32_t task_count, BikeShed_TaskFunc* task_functions, void** contexts, Bikeshed_TaskID* out_task_ids);

// Frees one or more tasks
// Explicitly freeing tasks should only be done when you need to revert a Bikeshed_CreateTasks without
// actually executing the created tasks. The task may not be ready or have any ready task dependencies.
// Make sure to free tasks that are dependencies of this task first.
// Any tasks that has dependencies on a freed task will have the dependencies removed.
// Trying to free a tasks that has unresolved (or un-freed) dependencies is not allowed.
void Bikeshed_FreeTasks(Bikeshed shed, uint32_t task_count, const Bikeshed_TaskID* task_ids);

// Set the channel of one or more tasks
// The default channel for a task is 0.
//
// The channel is used when calling Bikeshed_ExecuteOne
void Bikeshed_SetTasksChannel(Bikeshed shed, uint32_t task_count, Bikeshed_TaskID* task_ids, uint8_t channel);

// Readies one or more tasks for execution
// Tasks must not have any unresolved dependencies
// Task execution order is not guarranteed - use dependecies to eforce task execution order
// The Bikeshed_ReadyCallback of the shed (if any is set) will be called with task_count as number of readied tasks
void Bikeshed_ReadyTasks(Bikeshed shed, uint32_t task_count, const Bikeshed_TaskID* task_ids);

// Add dependencies to a task
// A task can have zero or more dependencies
// If a task has been made ready with Bikeshed_ReadyTasks you may not add dependencies to the task.
// A task that has been made ready may not be added as a dependency to another task.
// A task may have multiple "parents" - it is valid to add the same task as child to more than one task.
// Creating a task hierarchy that has circular dependencies makes it impossible to resolve.
//
// Returns:
//  1 - Success
//  0 - Not enough free dependency instances in the bikeshed
int Bikeshed_AddDependencies(Bikeshed shed, uint32_t task_count, const Bikeshed_TaskID* task_id, uint32_t dependency_task_count, const Bikeshed_TaskID* dependency_task_ids);

// Execute one task
// Checks the ready queue of channel and executes the task callback
// Any parent dependencies are resolved and if any parent gets all its dependencies resolved they will be made ready for execution.
//
// Returns:
//  1 - Executed one task
//  2 - No task are ready for execution
int Bikeshed_ExecuteOne(Bikeshed shed, uint8_t channel);

//  ------------- Public API End


typedef uint32_t Bikeshed_TaskIndex_private;
typedef uint32_t Bikeshed_DependencyIndex_private;
typedef uint32_t Bikeshed_ReadyIndex_private;

struct Bikeshed_Dependency_private
{
    Bikeshed_TaskIndex_private       m_ParentTaskIndex;
    Bikeshed_DependencyIndex_private m_NextDependencyIndex;
};

struct Bikeshed_Task_private
{
    int32_t volatile                    m_ChildDependencyCount;
    Bikeshed_TaskID                     m_TaskID;
    uint32_t                            m_Channel  : 8;
    uint32_t                            m_FirstDependencyIndex : 24;
    BikeShed_TaskFunc                   m_TaskFunc;
    void*                               m_TaskContext;
};

struct AtomicIndex
{
    int32_t volatile m_Index;
#if defined(BIKESHED_L1CACHE_SIZE)
    uint8_t padding[(BIKESHED_L1CACHE_SIZE) - sizeof(int32_t volatile)];
#endif
};

struct Bikeshed_Shed_private
{
    struct AtomicIndex                  m_ReadyGeneration;
    struct AtomicIndex                  m_TaskIndexHead;
    struct AtomicIndex                  m_DependencyIndexHead;
    struct AtomicIndex                  m_DependencyIndexGeneration;
    struct AtomicIndex                  m_TaskIndexGeneration;
    struct AtomicIndex                  m_TaskGeneration;
    struct AtomicIndex*                 m_ReadyHeads;
    int32_t*                            m_ReadyIndexes;
    int32_t*                            m_TaskIndexes;
    int32_t*                            m_DependencyIndexes;
    struct Bikeshed_Task_private*       m_Tasks;
    struct Bikeshed_Dependency_private* m_Dependencies;
    struct Bikeshed_ReadyCallback*      m_ReadyCallback;
};

#define BIKESHED_ALIGN_SIZE_PRIVATE(x, align) (((x) + ((align)-1)) & ~((align)-1))

#if defined(BIKESHED_L1CACHE_SIZE)
    #define BIKESHED_SHED_ALIGNEMENT_PRIVATE    (BIKESHED_L1CACHE_SIZE)
#else
    #define BIKESHED_SHED_ALIGNEMENT_PRIVATE    4u
#endif

#undef BIKESHED_SIZE
#define BIKESHED_SIZE(max_task_count, max_dependency_count, channel_count) \
        ((uint32_t)BIKESHED_ALIGN_SIZE_PRIVATE((uint32_t)sizeof(struct Bikeshed_Shed_private), BIKESHED_SHED_ALIGNEMENT_PRIVATE) + \
         (uint32_t)BIKESHED_ALIGN_SIZE_PRIVATE((uint32_t)(sizeof(struct AtomicIndex) * ((channel_count) - 1) + sizeof(int32_t volatile)), 4u) + \
         (uint32_t)BIKESHED_ALIGN_SIZE_PRIVATE((uint32_t)(sizeof(int32_t volatile) * (max_task_count)), 4u) + \
         (uint32_t)BIKESHED_ALIGN_SIZE_PRIVATE((uint32_t)(sizeof(int32_t volatile) * (max_task_count)), 4u) + \
         (uint32_t)BIKESHED_ALIGN_SIZE_PRIVATE((uint32_t)(sizeof(int32_t volatile) * (max_dependency_count)), 4u) + \
         (uint32_t)BIKESHED_ALIGN_SIZE_PRIVATE((uint32_t)(sizeof(struct Bikeshed_Task_private) * (max_task_count)), 8u) + \
         (uint32_t)BIKESHED_ALIGN_SIZE_PRIVATE((uint32_t)(sizeof(struct Bikeshed_Dependency_private) * (max_dependency_count)), 4u))

#if defined(BIKESHED_IMPLEMENTATION)

#if !defined(BIKESHED_ATOMICADD)
    #if defined(__clang__) || defined(__GNUC__)
        #define BIKESHED_ATOMICADD_PRIVATE(value, amount) (__sync_add_and_fetch (value, amount))
    #elif defined(_MSC_VER)
        #if !defined(_WINDOWS_)
            #define WIN32_LEAN_AND_MEAN
            #include <Windows.h>
            #undef WIN32_LEAN_AND_MEAN
        #endif

        #define BIKESHED_ATOMICADD_PRIVATE(value, amount) (_InterlockedExchangeAdd((volatile LONG *)value, amount) + amount)
    #else
        inline int32_t Bikeshed_NonAtomicAdd(volatile int32_t* store, int32_t value) { *store += value; return *store; }
        #define BIKESHED_ATOMICADD_PRIVATE(value, amount) (Bikeshed_NonAtomicAdd(value, amount))
    #endif
#else
    #define BIKESHED_ATOMICADD_PRIVATE BIKESHED_ATOMICADD
#endif

#if !defined(BIKESHED_ATOMICCAS)
    #if defined(__clang__) || defined(__GNUC__)
        #define BIKESHED_ATOMICCAS_PRIVATE(store, compare, value) __sync_val_compare_and_swap(store, compare, value)
    #elif defined(_MSC_VER)
        #if !defined(_WINDOWS_)
            #define WIN32_LEAN_AND_MEAN
            #include <Windows.h>
            #undef WIN32_LEAN_AND_MEAN
        #endif

        #define BIKESHED_ATOMICCAS_PRIVATE(store, compare, value) _InterlockedCompareExchange((volatile LONG *)store, value, compare)
    #else
        inline int32_t Bikeshed_NonAtomicCAS(volatile int32_t* store, int32_t compare, int32_t value) { int32_t old = *store; if (old == compare) { *store = value; } return old; }
        #define BIKESHED_ATOMICCAS_PRIVATE(store, compare, value) Bikeshed_NonAtomicCAS(store, value, compare)
    #endif
#else
    #define BIKESHED_ATOMICCAS_PRIVATE BIKESHED_ATOMICCAS
#endif

#if !defined(BIKESHED_CPU_YIELD)
    #if defined(__clang__) || defined(__GNUC__)
        #include <emmintrin.h>
        #define BIKESHED_CPU_YIELD_PRIVATE   _mm_pause();
    #elif defined(_MSC_VER)
        #if !defined(_WINDOWS_)
            #define WIN32_LEAN_AND_MEAN
            #include <Windows.h>
            #undef WIN32_LEAN_AND_MEAN
        #endif
        #define BIKESHED_CPU_YIELD_PRIVATE   YieldProcessor();
    #else
        #define BIKESHED_CPU_YIELD_PRIVATE   void();
    #endif
#else
    #define BIKESHED_CPU_YIELD_PRIVATE   BIKESHED_CPU_YIELD
#endif

#if defined(BIKESHED_ASSERTS)
#    define BIKESHED_FATAL_ASSERT_PRIVATE(x, bail) \
        if (!(x)) \
        { \
            if (Bikeshed_Assert_private) \
            { \
                Bikeshed_Assert_private(#x, __FILE__, __LINE__); \
            } \
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
    (void)assert_func;
#endif // defined(BIKESHED_ASSERTS)
}

#define BIKSHED_GENERATION_SHIFT_PRIVATE 23u
#define BIKSHED_INDEX_MASK_PRIVATE       0x007fffffu
#define BIKSHED_GENERATION_MASK_PRIVATE  0xff800000u

#define BIKESHED_TASK_ID_PRIVATE(index, generation) (((Bikeshed_TaskID)(generation) << BIKSHED_GENERATION_SHIFT_PRIVATE) + index)
#define BIKESHED_TASK_GENERATION_PRIVATE(task_id) ((int32_t)(task_id >> BIKSHED_GENERATION_SHIFT_PRIVATE))
#define BIKESHED_TASK_INDEX_PRIVATE(task_id) ((Bikeshed_TaskIndex_private)(task_id & BIKSHED_INDEX_MASK_PRIVATE))

static void Bikeshed_PoolInitialize_private(int32_t volatile* generation, int32_t volatile* head, int32_t volatile* items, uint32_t fill_count)
{
    *generation = 0;
    if (fill_count == 0)
    {
        *head = 0;
        return;
    }
    *head = 1;
    for (uint32_t i = 0; i < fill_count - 1; ++i)
    {
        items[i] = (int32_t)(i + 2);
    }
    items[fill_count - 1] = 0;
}

static void Bikeshed_PushRange_private(int32_t volatile* head, uint32_t gen, uint32_t head_index, int32_t* tail_index)
{
    uint32_t new_head                           = gen | head_index;
    uint32_t current_head                       = (uint32_t)*head;
    *tail_index                                 = (int32_t)(BIKESHED_TASK_INDEX_PRIVATE(current_head));

    while (BIKESHED_ATOMICCAS_PRIVATE(head, (int32_t)current_head, (int32_t)new_head) != (int32_t)current_head)
    {
        BIKESHED_CPU_YIELD_PRIVATE
        current_head    = (uint32_t)*head;
        *tail_index     = (int32_t)(BIKESHED_TASK_INDEX_PRIVATE(current_head));
    }
}

static void Bikeshed_FreeDependencies_private(Bikeshed shed, Bikeshed_DependencyIndex_private* head_resolved_dependency_index, Bikeshed_DependencyIndex_private *tail_resolved_dependency_index, Bikeshed_DependencyIndex_private dependency_index)
{
    if (*head_resolved_dependency_index)
    {
        shed->m_DependencyIndexes[*tail_resolved_dependency_index - 1]   = (int32_t)dependency_index;
        *tail_resolved_dependency_index                                  = dependency_index;
    }
    else
    {
        *head_resolved_dependency_index = dependency_index;
        *tail_resolved_dependency_index = dependency_index;
    }

    do
    {
        struct Bikeshed_Dependency_private* dependency  = &shed->m_Dependencies[dependency_index - 1];
        Bikeshed_TaskIndex_private  parent_task_index   = dependency->m_ParentTaskIndex;
        struct Bikeshed_Task_private* parent_task       = &shed->m_Tasks[parent_task_index - 1];
        BIKESHED_ATOMICADD_PRIVATE(&parent_task->m_ChildDependencyCount, -1);
        dependency_index = dependency->m_NextDependencyIndex;
        if (dependency_index == 0)
        {
            break;
        }
        shed->m_DependencyIndexes[*tail_resolved_dependency_index - 1] = (int32_t)dependency_index;
        *tail_resolved_dependency_index = dependency_index;
    } while (1);
}

static void Bikeshed_ResolveTask_private(Bikeshed shed, Bikeshed_DependencyIndex_private dependency_index)
{
    Bikeshed_TaskIndex_private  head_resolved_task_index = 0;
    Bikeshed_TaskIndex_private  tail_resolved_task_index = 0;
    uint32_t                    resolved_task_count = 0;
    uint8_t                     channel = 0;

    Bikeshed_DependencyIndex_private head_resolved_dependency_index = dependency_index;
    Bikeshed_DependencyIndex_private tail_resolved_dependency_index = dependency_index;

    do
    {
        struct Bikeshed_Dependency_private* dependency  = &shed->m_Dependencies[dependency_index - 1];
        Bikeshed_TaskIndex_private  parent_task_index   = dependency->m_ParentTaskIndex;
        struct Bikeshed_Task_private* parent_task       = &shed->m_Tasks[parent_task_index - 1];
        int32_t child_dependency_count                  = BIKESHED_ATOMICADD_PRIVATE(&parent_task->m_ChildDependencyCount, -1);
        if (child_dependency_count == 0)
        {
            BIKESHED_FATAL_ASSERT_PRIVATE(0x20000000 == BIKESHED_ATOMICADD_PRIVATE(&shed->m_Tasks[parent_task_index - 1].m_ChildDependencyCount, 0x20000000), return)
            if (resolved_task_count++ == 0)
            {
                head_resolved_task_index    = parent_task_index;
                channel                     = (uint8_t)parent_task->m_Channel;
            }
            else if (channel == parent_task->m_Channel)
            {
                shed->m_ReadyIndexes[tail_resolved_task_index - 1]   = (int32_t)parent_task_index;
            }
            else
            {
                uint32_t gen = (((uint32_t)BIKESHED_ATOMICADD_PRIVATE(&shed->m_ReadyGeneration.m_Index, 1)) << BIKSHED_GENERATION_SHIFT_PRIVATE) & BIKSHED_GENERATION_MASK_PRIVATE;
                Bikeshed_PushRange_private(&shed->m_ReadyHeads[channel].m_Index, gen, head_resolved_task_index, &shed->m_ReadyIndexes[tail_resolved_task_index-1]);
                if (shed->m_ReadyCallback)
                {
                    shed->m_ReadyCallback->SignalReady(shed->m_ReadyCallback, channel, resolved_task_count - 1);
                }
                head_resolved_task_index    = parent_task_index;
                channel                     = (uint8_t)parent_task->m_Channel;
                resolved_task_count         = 1;
            }
            tail_resolved_task_index    = parent_task_index;
        }
        dependency_index = dependency->m_NextDependencyIndex;
        shed->m_DependencyIndexes[tail_resolved_dependency_index - 1]   = (int32_t)dependency_index;
        if (dependency_index == 0)
        {
            break;
        }
        tail_resolved_dependency_index = dependency_index;
    } while (1);

    {
        uint32_t gen = (((uint32_t)BIKESHED_ATOMICADD_PRIVATE(&shed->m_DependencyIndexGeneration.m_Index, 1)) << BIKSHED_GENERATION_SHIFT_PRIVATE) & BIKSHED_GENERATION_MASK_PRIVATE;
        Bikeshed_PushRange_private(&shed->m_DependencyIndexHead.m_Index, gen, head_resolved_dependency_index, &shed->m_DependencyIndexes[tail_resolved_dependency_index-1]);
    }

    if (resolved_task_count > 0)
    {
        uint32_t gen = (((uint32_t)BIKESHED_ATOMICADD_PRIVATE(&shed->m_ReadyGeneration.m_Index, 1)) << BIKSHED_GENERATION_SHIFT_PRIVATE) & BIKSHED_GENERATION_MASK_PRIVATE;
        Bikeshed_PushRange_private(&shed->m_ReadyHeads[channel].m_Index, gen, head_resolved_task_index, &shed->m_ReadyIndexes[tail_resolved_task_index-1]);
        if (shed->m_ReadyCallback)
        {
            shed->m_ReadyCallback->SignalReady(shed->m_ReadyCallback, channel, resolved_task_count);
        }
    }
}

Bikeshed Bikeshed_Create(void* mem, uint32_t max_task_count, uint32_t max_dependency_count, uint8_t channel_count, struct Bikeshed_ReadyCallback* sync_primitive)
{
    BIKESHED_FATAL_ASSERT_PRIVATE(mem != 0, return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(max_task_count > 0, return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(max_task_count == BIKESHED_TASK_INDEX_PRIVATE(max_task_count), return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(max_dependency_count == BIKESHED_TASK_INDEX_PRIVATE(max_dependency_count), return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(channel_count >= 1, return 0)

    Bikeshed shed                   = (Bikeshed)mem;
    shed->m_TaskGeneration.m_Index  = 1;
    shed->m_ReadyCallback           = sync_primitive;

    uint8_t* p                  = (uint8_t*)mem;
    p                          += BIKESHED_ALIGN_SIZE_PRIVATE((uint32_t)sizeof(struct Bikeshed_Shed_private), BIKESHED_SHED_ALIGNEMENT_PRIVATE);
    shed->m_ReadyHeads          = (struct AtomicIndex*)(void*)p;
    p                          += BIKESHED_ALIGN_SIZE_PRIVATE((uint32_t)(sizeof(struct AtomicIndex) * (channel_count - 1) + sizeof(int32_t volatile)), 4u);
    shed->m_ReadyIndexes        = (int32_t*)(void*)p;
    p                          += BIKESHED_ALIGN_SIZE_PRIVATE((uint32_t)(sizeof(int32_t) * max_task_count), 4u);
    shed->m_TaskIndexes         = (int32_t*)(void*)p;
    p                          += BIKESHED_ALIGN_SIZE_PRIVATE((uint32_t)(sizeof(int32_t) * max_task_count), 4u);
    shed->m_DependencyIndexes   = (int32_t*)(void*)p;
    p                          += BIKESHED_ALIGN_SIZE_PRIVATE((uint32_t)(sizeof(int32_t) * max_dependency_count), 4u);
    shed->m_Tasks               = (struct Bikeshed_Task_private*)((void*)p);
    p                          += BIKESHED_ALIGN_SIZE_PRIVATE((uint32_t)(sizeof(struct Bikeshed_Task_private) * max_task_count), 8u);
    shed->m_Dependencies        = (struct Bikeshed_Dependency_private*)((void*)p);

    Bikeshed_PoolInitialize_private(&shed->m_TaskIndexGeneration.m_Index, &shed->m_TaskIndexHead.m_Index, shed->m_TaskIndexes, max_task_count);
    Bikeshed_PoolInitialize_private(&shed->m_DependencyIndexGeneration.m_Index, &shed->m_DependencyIndexHead.m_Index, shed->m_DependencyIndexes, max_dependency_count);
    for (uint8_t channel = 0; channel < channel_count; ++channel)
    {
        shed->m_ReadyHeads[channel].m_Index = 0;
    }

    return shed;
}

Bikeshed Bikeshed_CloneState(void* mem, Bikeshed original, uint32_t shed_size)
{
    BIKESHED_FATAL_ASSERT_PRIVATE(mem != 0, return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(original != 0, return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(shed_size >= sizeof(struct Bikeshed_Shed_private), return 0)

    memcpy(mem, original, shed_size);

    Bikeshed shed               = (Bikeshed)mem;
    uint8_t* p                  = (uint8_t*)mem;
    shed->m_ReadyHeads          = (struct AtomicIndex*)(void*)(&p[(uintptr_t)original->m_ReadyHeads - (uintptr_t)original]);
    shed->m_ReadyIndexes        = (int32_t*)(void*)(&p[(uintptr_t)original->m_ReadyIndexes - (uintptr_t)original]);
    shed->m_TaskIndexes         = (int32_t*)(void*)(&p[(uintptr_t)original->m_TaskIndexes - (uintptr_t)original]);
    shed->m_DependencyIndexes   = (int32_t*)(void*)(&p[(uintptr_t)original->m_DependencyIndexes - (uintptr_t)original]);
    shed->m_Tasks               = (struct Bikeshed_Task_private*)(void*)(&p[(uintptr_t)original->m_Tasks - (uintptr_t)original]);
    shed->m_Dependencies        = (struct Bikeshed_Dependency_private*)(void*)(&p[(uintptr_t)original->m_Dependencies - (uintptr_t)original]);

    return shed;
}

int Bikeshed_CreateTasks(Bikeshed shed, uint32_t task_count, BikeShed_TaskFunc* task_functions, void** contexts, Bikeshed_TaskID* out_task_ids)
{
    BIKESHED_FATAL_ASSERT_PRIVATE(shed != 0, return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(task_count > 0, return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(task_functions != 0, return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(contexts != 0, return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(out_task_ids != 0, return 0)

    do
    {
        uint32_t head = (uint32_t)shed->m_TaskIndexHead.m_Index;
        Bikeshed_TaskIndex_private task_index = BIKESHED_TASK_INDEX_PRIVATE(head);
        if (task_index == 0)
        {
            return 0;
        }
        out_task_ids[0] = task_index;
        for (uint32_t t = 1; t < task_count; ++t)
        {
            task_index = (uint32_t)shed->m_TaskIndexes[task_index - 1];
            if (task_index == 0)
            {
                return 0;
            }
            out_task_ids[t] = task_index;
        }
        uint32_t new_head   = (head & BIKSHED_GENERATION_MASK_PRIVATE) | (uint32_t)shed->m_TaskIndexes[task_index - 1];
        if (BIKESHED_ATOMICCAS_PRIVATE(&shed->m_TaskIndexHead.m_Index, (int32_t)head, (int32_t)new_head) == (int32_t)head)
        {
            break;
        }
    } while (1);

    int32_t generation = BIKESHED_ATOMICADD_PRIVATE(&shed->m_TaskGeneration.m_Index, 1);
    for (uint32_t i = 0; i < task_count; ++i)
    {
        Bikeshed_TaskIndex_private task_index   = out_task_ids[i];
        Bikeshed_TaskID task_id                 = BIKESHED_TASK_ID_PRIVATE(task_index, generation);
        out_task_ids[i]                         = task_id;
        struct Bikeshed_Task_private* task      = &shed->m_Tasks[task_index - 1];
        task->m_TaskID                          = task_id;
        task->m_ChildDependencyCount            = 0;
        task->m_FirstDependencyIndex            = 0;
        task->m_Channel                         = 0;
        task->m_TaskFunc                        = task_functions[i];
        task->m_TaskContext                     = contexts[i];
    }
    return 1;
}

void Bikeshed_FreeTasks(Bikeshed shed, uint32_t task_count, const Bikeshed_TaskID* task_ids)
{
    BIKESHED_FATAL_ASSERT_PRIVATE(shed != 0, return)
    BIKESHED_FATAL_ASSERT_PRIVATE(task_count > 0, return)
    BIKESHED_FATAL_ASSERT_PRIVATE(task_ids != 0, return)

    Bikeshed_TaskIndex_private head_free_task_index = BIKESHED_TASK_INDEX_PRIVATE(task_ids[0]);
    Bikeshed_TaskIndex_private tail_free_task_index = head_free_task_index;

    Bikeshed_DependencyIndex_private head_resolved_dependency_index = 0;
    Bikeshed_DependencyIndex_private tail_resolved_dependency_index = 0;

    struct Bikeshed_Task_private* task  = &shed->m_Tasks[tail_free_task_index - 1];
    BIKESHED_FATAL_ASSERT_PRIVATE(task->m_ChildDependencyCount == 0, return)
    if (task->m_FirstDependencyIndex)
    {
        Bikeshed_FreeDependencies_private(shed, &head_resolved_dependency_index, &tail_resolved_dependency_index, task->m_FirstDependencyIndex);
        task->m_FirstDependencyIndex = 0;
    }

    for (uint32_t i = 1; i < task_count; ++i)
    {
        Bikeshed_TaskIndex_private next_free_task_index = BIKESHED_TASK_INDEX_PRIVATE(task_ids[i]);

        task  = &shed->m_Tasks[next_free_task_index - 1];
        BIKESHED_FATAL_ASSERT_PRIVATE(task->m_ChildDependencyCount == 0, return)
        if (task->m_FirstDependencyIndex)
        {
            Bikeshed_FreeDependencies_private(shed, &head_resolved_dependency_index, &tail_resolved_dependency_index, task->m_FirstDependencyIndex);
            task->m_FirstDependencyIndex = 0;
        }

        shed->m_TaskIndexes[tail_free_task_index - 1] = (int32_t)next_free_task_index;
        tail_free_task_index = next_free_task_index;
    }

    if (head_resolved_dependency_index)
    {
        uint32_t gen = (((uint32_t)BIKESHED_ATOMICADD_PRIVATE(&shed->m_DependencyIndexGeneration.m_Index, 1)) << BIKSHED_GENERATION_SHIFT_PRIVATE) & BIKSHED_GENERATION_MASK_PRIVATE;
        Bikeshed_PushRange_private(&shed->m_DependencyIndexHead.m_Index, gen, head_resolved_dependency_index, &shed->m_DependencyIndexes[tail_resolved_dependency_index-1]);
    }

    uint32_t gen = (((uint32_t)BIKESHED_ATOMICADD_PRIVATE(&shed->m_TaskIndexGeneration.m_Index, 1)) << BIKSHED_GENERATION_SHIFT_PRIVATE) & BIKSHED_GENERATION_MASK_PRIVATE;
    Bikeshed_PushRange_private(&shed->m_TaskIndexHead.m_Index, gen, head_free_task_index, &shed->m_TaskIndexes[tail_free_task_index-1]);
}

void Bikeshed_SetTasksChannel(Bikeshed shed, uint32_t task_count, Bikeshed_TaskID* task_ids, uint8_t channel)
{
    BIKESHED_FATAL_ASSERT_PRIVATE(shed != 0, return)
    BIKESHED_FATAL_ASSERT_PRIVATE(task_count > 0, return)
    BIKESHED_FATAL_ASSERT_PRIVATE(task_ids != 0, return)
    BIKESHED_FATAL_ASSERT_PRIVATE((uintptr_t)&shed->m_ReadyHeads[channel] < (uintptr_t)shed->m_ReadyIndexes, return)

    for (uint32_t t = 0; t < task_count; ++t)
    {
        Bikeshed_TaskIndex_private task_index   = BIKESHED_TASK_INDEX_PRIVATE(task_ids[t]);
        struct Bikeshed_Task_private* task      = &shed->m_Tasks[task_index - 1];
        BIKESHED_FATAL_ASSERT_PRIVATE(task_ids[t] == task->m_TaskID, return)
        BIKESHED_FATAL_ASSERT_PRIVATE(task->m_ChildDependencyCount < 0x20000000, return)
        task->m_Channel  = channel;
    }
}

int Bikeshed_AddDependencies(Bikeshed shed, uint32_t task_count, const Bikeshed_TaskID* task_ids, uint32_t dependency_task_count, const Bikeshed_TaskID* dependency_task_ids)
{
    BIKESHED_FATAL_ASSERT_PRIVATE(shed != 0, return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(task_count > 0, return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(task_ids != 0, return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(dependency_task_count > 0, return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE(dependency_task_ids != 0, return 0)

    uint32_t total_dependency_count = task_count * dependency_task_count;

    uint32_t dependency_index_head;
    do
    {
        dependency_index_head = (uint32_t)shed->m_DependencyIndexHead.m_Index;
        Bikeshed_TaskIndex_private dependency_index = BIKESHED_TASK_INDEX_PRIVATE(dependency_index_head);
        if (dependency_index == 0)
        {
            return 0;
        }
        for (uint32_t d = 1; d < total_dependency_count; ++d)
        {
            dependency_index = (uint32_t)shed->m_DependencyIndexes[dependency_index - 1];
            if (dependency_index == 0)
            {
                return 0;
            }
        }
        uint32_t new_head   = (dependency_index_head & BIKSHED_GENERATION_MASK_PRIVATE) | (uint32_t)shed->m_DependencyIndexes[dependency_index - 1];
        if (BIKESHED_ATOMICCAS_PRIVATE(&shed->m_DependencyIndexHead.m_Index, (int32_t)dependency_index_head, (int32_t)new_head) == (int32_t)dependency_index_head)
        {
            break;
        }
    } while (1);

    Bikeshed_DependencyIndex_private dependency_index = BIKESHED_TASK_INDEX_PRIVATE(dependency_index_head);
    for (uint32_t t = 0; t < task_count; ++t)
    {
        Bikeshed_TaskID task_id                 = task_ids[t];
        Bikeshed_TaskIndex_private task_index   = BIKESHED_TASK_INDEX_PRIVATE(task_id);
        struct Bikeshed_Task_private* task      = &shed->m_Tasks[task_index - 1];
        BIKESHED_FATAL_ASSERT_PRIVATE(task_id == task->m_TaskID, return 0)
        BIKESHED_FATAL_ASSERT_PRIVATE(task->m_ChildDependencyCount < 0x20000000, return 0)

        for (uint32_t i = 0; i < dependency_task_count; ++i)
        {
            Bikeshed_TaskID dependency_task_id                  = dependency_task_ids[i];
            Bikeshed_TaskIndex_private dependency_task_index    = BIKESHED_TASK_INDEX_PRIVATE(dependency_task_id);
            struct Bikeshed_Task_private* dependency_task       = &shed->m_Tasks[dependency_task_index - 1];
            BIKESHED_FATAL_ASSERT_PRIVATE(dependency_task_id == dependency_task->m_TaskID, return 0)
            BIKESHED_FATAL_ASSERT_PRIVATE(dependency_task_id != task_id, return 0)

            struct Bikeshed_Dependency_private* dependency  = &shed->m_Dependencies[dependency_index - 1];
            dependency->m_ParentTaskIndex                   = task_index;
            dependency->m_NextDependencyIndex               = dependency_task->m_FirstDependencyIndex;
            dependency_task->m_FirstDependencyIndex         = dependency_index;
            dependency_index                                = (uint32_t)shed->m_DependencyIndexes[dependency_index - 1];
        }
        BIKESHED_ATOMICADD_PRIVATE(&task->m_ChildDependencyCount, (int32_t)dependency_task_count);
    }

    return 1;
}

void Bikeshed_ReadyTasks(Bikeshed shed, uint32_t task_count, const Bikeshed_TaskID* task_ids)
{
    BIKESHED_FATAL_ASSERT_PRIVATE(shed != 0, return)
    BIKESHED_FATAL_ASSERT_PRIVATE(task_count > 0, return)
    BIKESHED_FATAL_ASSERT_PRIVATE(task_ids != 0, return)
    Bikeshed_TaskID head_task_id                = task_ids[0];
    Bikeshed_TaskIndex_private head_task_index  = BIKESHED_TASK_INDEX_PRIVATE(head_task_id);
    Bikeshed_TaskIndex_private tail_task_index  = head_task_index;
    struct Bikeshed_Task_private* head_task     = &shed->m_Tasks[head_task_index - 1];
    BIKESHED_FATAL_ASSERT_PRIVATE(head_task_id == head_task->m_TaskID, return )
    BIKESHED_FATAL_ASSERT_PRIVATE(0x20000000 == BIKESHED_ATOMICADD_PRIVATE(&head_task->m_ChildDependencyCount, 0x20000000), return)

    uint8_t channel = (uint8_t)head_task->m_Channel;
    uint32_t gen = (((uint32_t)BIKESHED_ATOMICADD_PRIVATE(&shed->m_ReadyGeneration.m_Index, 1)) << BIKSHED_GENERATION_SHIFT_PRIVATE) & BIKSHED_GENERATION_MASK_PRIVATE;
    uint32_t ready_task_count = 1;
    uint32_t i = 1;
    while (i < task_count)
    {
        Bikeshed_TaskID next_task_id                = task_ids[i];
        Bikeshed_TaskIndex_private next_task_index  = BIKESHED_TASK_INDEX_PRIVATE(next_task_id);
        struct Bikeshed_Task_private* next_task     = &shed->m_Tasks[next_task_index - 1];
        BIKESHED_FATAL_ASSERT_PRIVATE(next_task_id == next_task->m_TaskID, return )
        BIKESHED_FATAL_ASSERT_PRIVATE(0x20000000 == BIKESHED_ATOMICADD_PRIVATE(&shed->m_Tasks[next_task_index - 1].m_ChildDependencyCount, 0x20000000), return)

        if (next_task->m_Channel == channel)
        {
            shed->m_ReadyIndexes[tail_task_index - 1] = (int32_t)next_task_index;
            tail_task_index                           = next_task_index;
            ++ready_task_count;
            ++i;
            continue;
        }
        Bikeshed_PushRange_private(&shed->m_ReadyHeads[channel].m_Index, gen, head_task_index, &shed->m_ReadyIndexes[tail_task_index-1]);
        if (shed->m_ReadyCallback)
        {
            shed->m_ReadyCallback->SignalReady(shed->m_ReadyCallback, channel, ready_task_count);
        }

        ready_task_count    = 1;
        channel             = (uint8_t)next_task->m_Channel;
        head_task_index     = next_task_index;
        tail_task_index     = next_task_index;
        ++i;
    }

    if (ready_task_count > 0)
    {
        Bikeshed_PushRange_private(&shed->m_ReadyHeads[channel].m_Index, gen, head_task_index, &shed->m_ReadyIndexes[tail_task_index-1]);

        if (shed->m_ReadyCallback)
        {
            shed->m_ReadyCallback->SignalReady(shed->m_ReadyCallback, channel, ready_task_count);
        }
    }
}

int Bikeshed_ExecuteOne(Bikeshed shed, uint8_t channel)
{
    BIKESHED_FATAL_ASSERT_PRIVATE(shed != 0, return 0)
    BIKESHED_FATAL_ASSERT_PRIVATE((uintptr_t)&shed->m_ReadyHeads[channel] < (uintptr_t)shed->m_ReadyIndexes, return 0)

    int32_t volatile* head = &shed->m_ReadyHeads[channel].m_Index;
    int32_t* items = shed->m_ReadyIndexes;
    uint32_t task_index = 0;
    do
    {
        uint32_t current_head   = (uint32_t)*head;
        task_index              = BIKESHED_TASK_INDEX_PRIVATE(current_head);
        if (task_index == 0)
        {
            return 0;
        }

        uint32_t next       = (uint32_t)items[task_index - 1];
        uint32_t new_head   = (current_head & BIKSHED_GENERATION_MASK_PRIVATE) | next;

        if (BIKESHED_ATOMICCAS_PRIVATE(head, (int32_t)current_head, (int32_t)new_head) == (int32_t)current_head)
        {
            break;
        }
        BIKESHED_CPU_YIELD_PRIVATE
    } while (1);

    struct Bikeshed_Task_private* task      = &shed->m_Tasks[task_index - 1];
    Bikeshed_TaskID task_id                 = task->m_TaskID;

    enum Bikeshed_TaskResult task_result    = task->m_TaskFunc(shed, task_id, channel, task->m_TaskContext);

    BIKESHED_FATAL_ASSERT_PRIVATE(0 == BIKESHED_ATOMICADD_PRIVATE(&task->m_ChildDependencyCount, -0x20000000), return 0)

    if (task_result == BIKESHED_TASK_RESULT_COMPLETE)
    {
        if (task->m_FirstDependencyIndex)
        {
            Bikeshed_ResolveTask_private(shed, task->m_FirstDependencyIndex);
            task->m_FirstDependencyIndex = 0;
        }
        uint32_t gen = (((uint32_t)BIKESHED_ATOMICADD_PRIVATE(&shed->m_TaskIndexGeneration.m_Index, 1)) << BIKSHED_GENERATION_SHIFT_PRIVATE) & BIKSHED_GENERATION_MASK_PRIVATE;
        Bikeshed_PushRange_private(&shed->m_TaskIndexHead.m_Index, gen, task_index, &shed->m_TaskIndexes[task_index-1]);
    }
    else
    {
        BIKESHED_FATAL_ASSERT_PRIVATE(BIKESHED_TASK_RESULT_BLOCKED == task_result, return 0)
    }

    return 1;
}


#undef BIKESHED_ATOMICADD_PRIVATE
#undef BIKESHED_ATOMICCAS_PRIVATE
#undef BIKSHED_GENERATION_SHIFT_PRIVATE
#undef BIKSHED_INDEX_MASK_PRIVATE
#undef BIKSHED_GENERATION_MASK_PRIVATE
#undef BIKESHED_TASK_ID_PRIVATE
#undef BIKESHED_TASK_GENERATION_PRIVATE
#undef BIKESHED_TASK_INDEX_PRIVATE

#endif // !defined(BIKESHED_IMPLEMENTATION)

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // BIKESHED_INCLUDEGUARD_PRIVATE_H

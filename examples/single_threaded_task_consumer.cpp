#define BIKESHED_IMPLEMENTATION
#define BIKESHED_L1CACHE_SIZE 64

#include "../src/bikeshed.h"
#include "../third-party/nadir/src/nadir.h"

#include "helpers/perf_timer.h"
#include "helpers/counting_task.h"

#include <stdio.h>

int main(int , char** )
{
    SetupTime();

    long volatile executed_count = 0;
    long dispatched_count = 0;

    const uint32_t COUNT = 4000000;
    const uint32_t BATCH_COUNT = 10;

    CountingTask counting_context;
    counting_context.execute_count = &executed_count;

    Bikeshed shed = Bikeshed_Create(malloc(BIKESHED_SIZE(BATCH_COUNT, 0, 1)), BATCH_COUNT, 0, 1, 0);

    uint64_t start_time = Tick();

    for (uint32_t i = 0; i < COUNT; i += BATCH_COUNT)
    {
        BikeShed_TaskFunc task_funcs[BATCH_COUNT];
        void* task_contexts[BATCH_COUNT];
        for (uint32_t j = 0; j < BATCH_COUNT; ++j)
        {
            task_funcs[j] = CountingTask::Compute;
            task_contexts[j] = &counting_context;
        }
        Bikeshed_TaskID task_ids[BATCH_COUNT];
        Bikeshed_CreateTasks(shed, BATCH_COUNT, task_funcs, task_contexts, task_ids);
        Bikeshed_ReadyTasks(shed, BATCH_COUNT, task_ids);
        dispatched_count += BATCH_COUNT;
        for (uint32_t j = 0; j < BATCH_COUNT; ++j)
        {
            Bikeshed_ExecuteOne(shed, 0);
        }
    }

    uint64_t end_time = Tick();

    uint64_t elapsed_ticks = end_time - start_time;
    double elapsed_seconds = TicksToSeconds(elapsed_ticks);
    double task_per_second = COUNT / elapsed_seconds;
    double seconds_per_task = elapsed_seconds / COUNT;

    double ns_per_task = 1000000000.0 * seconds_per_task;

    printf("SingleThreadedTaskConsumer:                   tasks per second: %.3lf, task time: %.3lf ns\n", task_per_second, ns_per_task);

    free(shed);
    return 0;
}

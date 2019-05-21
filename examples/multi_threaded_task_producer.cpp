#define BIKESHED_IMPLEMENTATION
#define BIKESHED_L1CACHE_SIZE 64

#include "../src/bikeshed.h"
#include "../third-party/nadir/src/nadir.h"

#include "helpers/perf_timer.h"
#include "helpers/counting_task.h"
#include "helpers/nadir_sync.h"
#include "helpers/task_producer.h"

#include <stdio.h>

int main(int , char** )
{
    SetupTime();

    long volatile executed_count = 0;

    const uint32_t COUNT = 4000000;

    nadir::TAtomic32 stop = 0;

    CountingTask counting_context;
    counting_context.execute_count = &executed_count;

    NadirSync sync;
    Bikeshed shed = Bikeshed_Create(malloc(BIKESHED_SIZE(COUNT, 0, 1)), COUNT, 0, 1, &sync.m_ReadyCallback);

    static const uint32_t PRODUCER_WORKER_COUNT = 7;
    TaskProducer workers[PRODUCER_WORKER_COUNT];
    for (uint32_t w = 0; w < PRODUCER_WORKER_COUNT; ++w)
    {
        workers[w].CreateThread(shed, sync.m_ConditionVariable, w * COUNT / PRODUCER_WORKER_COUNT, (w + 1) * COUNT / PRODUCER_WORKER_COUNT, 0, &executed_count);
    }

    nadir::Sleep(100000);
    sync.WakeAll();

    uint64_t start_time = Tick();

    while (executed_count != COUNT)
    {
        Bikeshed_ExecuteOne(shed, 0);
    }

    uint64_t end_time = Tick();

    nadir::AtomicAdd32(&stop, 1);
    sync.WakeAll();

    for (uint32_t w = 0; w < PRODUCER_WORKER_COUNT; ++w)
    {
        workers[w].Join();
    }

    for (uint32_t w = 0; w < PRODUCER_WORKER_COUNT; ++w)
    {
        workers[w].Dispose();
    }

    uint64_t elapsed_ticks = end_time - start_time;
    double elapsed_seconds = TicksToSeconds(elapsed_ticks);
    double task_per_second = COUNT / elapsed_seconds;
    double seconds_per_task = elapsed_seconds / COUNT;

    double ns_per_task = 1000000000.0 * seconds_per_task;

    printf("MultiThreadedTaskProducer:                    tasks per second:  %.3lf, task time: %.3lf ns\n", task_per_second, ns_per_task);

    free(shed);
    return 0;
}

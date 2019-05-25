#define BIKESHED_IMPLEMENTATION
#define BIKESHED_L1CACHE_SIZE 64
//#define BIKESHED_CPU_YIELD

#include "../src/bikeshed.h"
#include "../third-party/nadir/src/nadir.h"

#define PROCESSOR_CORE_COUNT        8
#define PRODUCER_WORKER_COUNT       (PROCESSOR_CORE_COUNT / 2)
#define CONSUMER_WORKER_COUNT       ((PROCESSOR_CORE_COUNT / 2) - 1)
#define TASK_PRODUCER_BATCH_SIZE    8
#define CHANNEL_COUNT               PRODUCER_WORKER_COUNT
#define STEALING_CHANNEL_COUNT      CHANNEL_COUNT

#include "helpers/perf_timer.h"
#include "helpers/counting_task.h"
#include "helpers/nadir_sync.h"
#include "helpers/stealing_worker.h"
#include "helpers/task_producer.h"

#include <stdio.h>

int main(int , char** )
{
    SetupTime();

    long volatile executed_count = 0;

    const uint32_t COUNT = 0x1ffffffu;

    nadir::TAtomic32 stop = 0;

    NadirSync sync;
    Bikeshed shed = Bikeshed_Create(malloc(BIKESHED_SIZE(0x7fffffu, 0, CHANNEL_COUNT)), 0x7fffffu, 0, CHANNEL_COUNT, &sync.m_ReadyCallback);

    TaskProducer producer[PRODUCER_WORKER_COUNT];
    for (uint32_t w = 0; w < PRODUCER_WORKER_COUNT; ++w)
    {
        producer[w].CreateThread(shed, sync.m_ConditionVariable, w * COUNT / PRODUCER_WORKER_COUNT, (w + 1) * COUNT / PRODUCER_WORKER_COUNT, (uint8_t)(w * CHANNEL_COUNT / PRODUCER_WORKER_COUNT), &executed_count);
    }

    StealingNodeWorker consumers[CONSUMER_WORKER_COUNT];
    for (uint32_t w = 0; w < CONSUMER_WORKER_COUNT; ++w)
    {
        consumers[w].CreateThread(shed, sync.m_ConditionVariable, (uint8_t)((w + 1) * CHANNEL_COUNT / (CONSUMER_WORKER_COUNT + 1)), STEALING_CHANNEL_COUNT, &stop);
    }

    nadir::Sleep(100000);
    sync.WakeAll();

    uint64_t start_time = Tick();

    while (executed_count != COUNT)
    {
        StealingNodeWorker::ExecuteOne(shed, 0, STEALING_CHANNEL_COUNT);
    }

    uint64_t end_time = Tick();

    nadir::AtomicAdd32(&stop, 1);
    sync.WakeAll();

    for (uint32_t w = 0; w < PRODUCER_WORKER_COUNT; ++w)
    {
        producer[w].Join();
    }

    for (uint32_t w = 0; w < PRODUCER_WORKER_COUNT; ++w)
    {
        producer[w].Dispose();
    }

    for (uint32_t w = 0; w < CONSUMER_WORKER_COUNT; ++w)
    {
        consumers[w].Join();
    }

    for (uint32_t w = 0; w < CONSUMER_WORKER_COUNT; ++w)
    {
        consumers[w].Dispose();
    }

    uint64_t elapsed_ticks = end_time - start_time;
    double elapsed_seconds = TicksToSeconds(elapsed_ticks);
    double task_per_second = COUNT / elapsed_seconds;
    double seconds_per_task = elapsed_seconds / COUNT;

    double ns_per_task = 1000000000.0 * seconds_per_task;

    printf("StealingMultiThreadedTaskProducerAndConsumer: tasks per second:  %.3lf, task time: %.3lf ns\n", task_per_second, ns_per_task);

    free(shed);

    return 0;
}

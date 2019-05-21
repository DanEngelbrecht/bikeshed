#pragma once

#include "../third-party/nadir/src/nadir.h"
#include "../src/bikeshed.h"

struct CountingTask
{
    CountingTask()
        : execute_count(0)
    {
    }
    static Bikeshed_TaskResult Compute(Bikeshed , Bikeshed_TaskID , uint8_t , void* context)
    {
        CountingTask* _this = reinterpret_cast<CountingTask*>(context);
        nadir::AtomicAdd32(_this->execute_count, 1);
        return BIKESHED_TASK_RESULT_COMPLETE;
    }
    long volatile*          execute_count;
};


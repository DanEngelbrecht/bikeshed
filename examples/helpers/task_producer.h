#pragma once

#include "counting_task.h"

struct TaskProducer
{

    TaskProducer()
        : executed_count(0)
        , start(0)
        , end(0)
        , channel(0)
        , shed(0)
        , condition_variable(0)
        , thread(0)
    {
    }

    ~TaskProducer()
    {
    }

    bool CreateThread(Bikeshed in_shed, nadir::HConditionVariable in_condition_variable, uint32_t in_start, uint32_t in_end, uint8_t in_channel, nadir::TAtomic32* in_executed)
    {
        shed                = in_shed;
        start               = in_start;
        end                 = in_end;
        channel             = in_channel;
        executed_count      = in_executed;
        condition_variable  = in_condition_variable;
        thread              = nadir::CreateThread(malloc(nadir::GetThreadSize()), TaskProducer::Execute, 0, this);
        return thread != 0;
    }

    void Join()
    {
        nadir::JoinThread(thread, nadir::TIMEOUT_INFINITE);
    }

    void Dispose()
    {
        nadir::DeleteThread(thread);
        free(thread);
    }

    static int32_t Execute(void* context)
    {
        TaskProducer* _this = reinterpret_cast<TaskProducer*>(context);

        for (uint32_t c = 0; c < TASK_PRODUCER_BATCH_SIZE; ++c)
        {
            _this->counting_context[c].execute_count = _this->executed_count;
        }

        nadir::SleepConditionVariable(_this->condition_variable, nadir::TIMEOUT_INFINITE);

        uint32_t i = _this->start;

        do
        {
            uint32_t batch_count = (i + TASK_PRODUCER_BATCH_SIZE) < _this->end ? TASK_PRODUCER_BATCH_SIZE : (_this->end - i);
            BikeShed_TaskFunc task_funcs[TASK_PRODUCER_BATCH_SIZE];
            void* task_contexts[TASK_PRODUCER_BATCH_SIZE];
            for (uint32_t j = 0; j < TASK_PRODUCER_BATCH_SIZE; ++j)
            {
                task_funcs[j] = CountingTask::Compute;
                task_contexts[j] = &_this->counting_context[j];
            }
            Bikeshed_TaskID task_ids[TASK_PRODUCER_BATCH_SIZE];
            while(0 == Bikeshed_CreateTasks(_this->shed, batch_count, task_funcs, task_contexts, task_ids))
            {
                nadir::Sleep(1000);
            }
            Bikeshed_SetTasksChannel(_this->shed, batch_count, task_ids, _this->channel);
            Bikeshed_ReadyTasks(_this->shed, batch_count, task_ids);

            i += batch_count;
        } while (i != _this->end);
        return 0;
    }

    CountingTask        counting_context[TASK_PRODUCER_BATCH_SIZE];
    nadir::TAtomic32*   executed_count;
    uint32_t            start;
    uint32_t            end;
    uint8_t             channel;
    Bikeshed            shed;
    nadir::HConditionVariable   condition_variable;
    nadir::HThread      thread;
};


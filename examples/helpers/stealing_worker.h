#pragma once

struct StealingNodeWorker
{
    StealingNodeWorker()
        : stop(0)
        , shed(0)
        , condition_variable(0)
        , thread(0)
        , index(0)
        , count(1)
    {
    }

    ~StealingNodeWorker()
    {
    }

    bool CreateThread(Bikeshed in_shed, nadir::HConditionVariable in_condition_variable, uint32_t worker_index, uint32_t worker_count, nadir::TAtomic32* in_stop)
    {
        shed               = in_shed;
        stop               = in_stop;
        condition_variable = in_condition_variable;
        index              = worker_index;
        count              = worker_count;
        thread             = nadir::CreateThread(malloc(nadir::GetThreadSize()), StealingNodeWorker::Execute, 0, this);
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

    static int32_t ExecuteOne(Bikeshed shed, uint32_t start_channel, uint32_t channel_count)
    {
        uint32_t channel = start_channel;
        for (uint32_t c = 0; c < channel_count; ++c)
        {
            if (Bikeshed_ExecuteOne(shed, (uint8_t)channel))
            {
                if (start_channel != channel)
                {
                    return 2;
                }
                return 1;
            }
            channel = (channel + 1) % channel_count;
        }
        return 0;
    }

    static int32_t Execute(void* context)
    {
        StealingNodeWorker* _this = reinterpret_cast<StealingNodeWorker*>(context);

        while (*_this->stop == 0)
        {
            ExecuteOne(_this->shed, _this->index, _this->count);
        }
        return 0;
    }

    nadir::TAtomic32*           stop;
    Bikeshed                    shed;
    nadir::HConditionVariable   condition_variable;
    nadir::HThread              thread;
    uint32_t                    index;
    uint32_t                    count;
};


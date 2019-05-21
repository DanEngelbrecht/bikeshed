#pragma once

struct NadirWorker
{
    NadirWorker()
        : stop(0)
        , shed(0)
        , condition_variable(0)
        , thread(0)
    {
    }

    ~NadirWorker()
    {
    }

    bool CreateThread(Bikeshed in_shed, nadir::HConditionVariable in_condition_variable, nadir::TAtomic32* in_stop)
    {
        shed               = in_shed;
        stop               = in_stop;
        condition_variable = in_condition_variable;
        thread             = nadir::CreateThread(malloc(nadir::GetThreadSize()), NadirWorker::Execute, 0, this);
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
        NadirWorker* _this = reinterpret_cast<NadirWorker*>(context);

        nadir::SleepConditionVariable(_this->condition_variable, nadir::TIMEOUT_INFINITE);

        while (*_this->stop == 0)
        {
            Bikeshed_ExecuteOne(_this->shed, 0);
        }
        return 0;
    }

    nadir::TAtomic32*           stop;
    Bikeshed                    shed;
    nadir::HConditionVariable   condition_variable;
    nadir::HThread              thread;
};

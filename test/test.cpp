#include "../third-party/jctest/src/jc_test.h"
#include "../third-party/nadir/src/nadir.h"

#include "../src/bikeshed.h"

#include <memory>

struct AssertAbort
{
    AssertAbort()
    {
        Bikeshed_SetAssert(AssertAbort::Assert);
    }
    ~AssertAbort()
    {
        Bikeshed_SetAssert(0);
    }
    static void Assert(const char* expression, const char* file, int line)
    {
        printf("'%s' failed at %s(%d)", expression, file, line);
        ASSERT_TRUE(false);
    }
};

struct AssertExpect
{
    static uint32_t gAssertCount;
    AssertExpect()
    {
        gAssertCount = 0;
        Bikeshed_SetAssert(AssertExpect::Assert);
    }
    ~AssertExpect()
    {
        Bikeshed_SetAssert(0);
    }
    static void Assert(const char* , const char* , int )
    {
        ++gAssertCount;
    }
};

uint32_t AssertExpect::gAssertCount = 0;

TEST(Bikeshed, Assert)
{
    AssertExpect assert_expect;
    char mem[BIKESHED_SIZE(1, 0, 1)];
    Bikeshed shed = Bikeshed_Create(mem, 1, 0, 1, 0);
    ASSERT_NE((Bikeshed)0, shed);
#if defined(BIKESHED_ASSERTS)
    Bikeshed_TaskID invalid_task_id = 1;
    Bikeshed_ReadyTasks(shed, 1, &invalid_task_id);
    ASSERT_EQ(1u, AssertExpect::gAssertCount);
#endif
}

struct TaskData
{
    TaskData()
        : shed(0)
        , task_id(0)
        , executed(0)
        , channel(255)
    {
    }
    static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, uint8_t channel, void* context)
    {
        TaskData* task_data = reinterpret_cast<TaskData*>(context);
        task_data->shed = shed;
        ++task_data->executed;
        task_data->task_id = task_id;
        task_data->channel = channel;
        return BIKESHED_TASK_RESULT_COMPLETE;
    }
    Bikeshed        shed;
    Bikeshed_TaskID task_id;
    uint32_t        executed;
    uint32_t        channel;
};


TEST(Bikeshed, SingleTask)
{
    AssertAbort fatal;

    char mem[BIKESHED_SIZE(1, 0, 1)];
    Bikeshed shed = Bikeshed_Create(mem, 1, 0, 1, 0);

    TaskData  task;

    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));

    BikeShed_TaskFunc task_functions[1] = {TaskData::Compute};
    void* task_contexts[1] = {&task};

    Bikeshed_TaskID task_id;
    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 1, task_functions, task_contexts, &task_id));

    ASSERT_FALSE(Bikeshed_CreateTasks(shed, 1, task_functions, task_contexts, &task_id));

    Bikeshed_ReadyTasks(shed, 1, &task_id);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));

    ASSERT_EQ(shed, task.shed);
    ASSERT_EQ(task.task_id, task_id);
    ASSERT_EQ(1u, task.executed);

    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));
}

TEST(Bikeshed, FreeTasks)
{
    AssertAbort fatal;

    char mem[BIKESHED_SIZE(5, 3, 1)];
    Bikeshed shed = Bikeshed_Create(mem, 5, 3, 1, 0);

    TaskData  tasks[5];
    BikeShed_TaskFunc task_functions[5] = {
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute
        };
    void* task_contexts[5] = {
        &tasks[0],
        &tasks[1],
        &tasks[2],
        &tasks[3],
        &tasks[4]
        };


    Bikeshed_TaskID task_ids[5];
    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 5, task_functions, task_contexts, task_ids));
    ASSERT_FALSE(Bikeshed_CreateTasks(shed, 1, task_functions, task_contexts, task_ids));
    Bikeshed_FreeTasks(shed, 5, task_ids);
    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 5, task_functions, task_contexts, task_ids));
    ASSERT_FALSE(Bikeshed_CreateTasks(shed, 1, task_functions, task_contexts, task_ids));
	ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[0], 2, &task_ids[2]));
	ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[1], 1, &task_ids[4]));
	Bikeshed_FreeTasks(shed, 3, &task_ids[2]);
    Bikeshed_FreeTasks(shed, 2, &task_ids[0]);
    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 5, task_functions, task_contexts, task_ids));
	ASSERT_FALSE(Bikeshed_AddDependencies(shed, 1, &task_ids[0], 4, &task_ids[1]));
	ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[0], 3, &task_ids[1]));
	ASSERT_FALSE(Bikeshed_AddDependencies(shed, 1, &task_ids[1], 1, &task_ids[4]));
}

TEST(Bikeshed, Blocked)
{
    AssertAbort fatal;

    char mem[BIKESHED_SIZE(2, 0, 1)];
    Bikeshed shed = Bikeshed_Create(mem, 2, 0, 1, 0x0);

    struct TaskData
    {
        TaskData()
            : shed(0)
            , task_id(0)
            , blocked_count(0)
            , executed(0)
        {
        }
        static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, uint8_t , void* task_context)
        {
            TaskData* task_data = reinterpret_cast<TaskData*>(task_context);
            ++task_data->executed;
            if (task_data->blocked_count > 0)
            {
                --task_data->blocked_count;
                return BIKESHED_TASK_RESULT_BLOCKED;
            }
            task_data->shed    = shed;
            task_data->task_id = task_id;
            return BIKESHED_TASK_RESULT_COMPLETE;
        }
        Bikeshed   shed;
        Bikeshed_TaskID task_id;
        uint8_t           blocked_count;
        uint32_t          executed;
    };

    TaskData tasks[2];
    tasks[0].blocked_count = 1;

    BikeShed_TaskFunc task_functions[2] = {
        TaskData::Compute,
        TaskData::Compute
    };
    void* task_contexts[2] = {
        &tasks[0],
        &tasks[1]
    };

    Bikeshed_TaskID task_ids[2];
    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 2, task_functions, task_contexts, task_ids));

    Bikeshed_ReadyTasks(shed, 2, task_ids);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));
    Bikeshed_ReadyTasks(shed, 1, &task_ids[0]);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_EQ(0, tasks[0].blocked_count);
    ASSERT_EQ(shed, tasks[0].shed);
    ASSERT_EQ(task_ids[0], tasks[0].task_id);
    ASSERT_EQ(2u, tasks[0].executed);

    ASSERT_EQ(0, tasks[0].blocked_count);
    ASSERT_EQ(shed, tasks[1].shed);
    ASSERT_EQ(task_ids[1], tasks[1].task_id);
    ASSERT_EQ(1u, tasks[1].executed);

    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));
}

TEST(Bikeshed, Sync)
{
    AssertAbort fatal;

    struct FakeLock
    {
        Bikeshed_ReadyCallback m_ReadyCallback;
        FakeLock()
            : ready_count(0)
        {
            m_ReadyCallback.SignalReady = signal;
        }
        static void signal(Bikeshed_ReadyCallback* primitive, uint32_t ready_count)
        {
            (reinterpret_cast<FakeLock*>(primitive))->ready_count += ready_count;
        }
        uint32_t ready_count;
    } lock;
    char mem[BIKESHED_SIZE(1, 0, 1)];
    Bikeshed shed = Bikeshed_Create(mem, 1, 0, 1, &lock.m_ReadyCallback);

    TaskData task;

    BikeShed_TaskFunc funcs[1]    = { TaskData::Compute };
    void*              contexts[1] = { &task };

    Bikeshed_TaskID task_id;
    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 1, funcs, contexts, &task_id));

    ASSERT_FALSE(Bikeshed_CreateTasks(shed, 1, funcs, contexts, &task_id));

    Bikeshed_ReadyTasks(shed, 1, &task_id);
    ASSERT_EQ(1u, lock.ready_count);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));

    ASSERT_EQ(shed, task.shed);
    ASSERT_EQ(task.task_id, task_id);
    ASSERT_EQ(1u, task.executed);

    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_EQ(1u, lock.ready_count);
}

TEST(Bikeshed, ReadyOrder)
{
    AssertAbort fatal;

    char mem[BIKESHED_SIZE(5, 4, 1)];
    Bikeshed shed = Bikeshed_Create(mem, 5, 4, 1, 0);

    TaskData           tasks[5];
    BikeShed_TaskFunc funcs[5] = {
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute
    };
    void* contexts[5] = {
        &tasks[0],
        &tasks[1],
        &tasks[2],
        &tasks[3],
        &tasks[4]
    };
    Bikeshed_TaskID task_ids[5];

    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 5, funcs, contexts, task_ids));
    Bikeshed_ReadyTasks(shed, 5, &task_ids[0]);

    for (uint32_t i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
        ASSERT_EQ(task_ids[i], tasks[i].task_id);
        ASSERT_EQ(shed, tasks[i].shed);
        ASSERT_EQ(1u, tasks[i].executed);
    }
    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));
}

struct ReadyCounter {
    ReadyCounter()
        : ready_count(0)
    {
        cb.SignalReady = ReadyCounter::Ready;
    }
    struct Bikeshed_ReadyCallback cb;
    nadir::TAtomic32 ready_count;
    static void Ready(struct Bikeshed_ReadyCallback* ready_callback, uint32_t ready_count)
    {
        ReadyCounter* _this = reinterpret_cast<ReadyCounter*>(ready_callback);
        nadir::AtomicAdd32(&_this->ready_count, (long)ready_count);
    }
};

TEST(Bikeshed, ReadyCallback)
{
    AssertAbort fatal;

    ReadyCounter myCallback;
    char mem[BIKESHED_SIZE(3, 2, 3)];
    Bikeshed shed = Bikeshed_Create(mem, 3, 2, 3, &myCallback.cb);
    TaskData           tasks[3];
    BikeShed_TaskFunc funcs[3] = {
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute
    };
    void* contexts[3] = {
        &tasks[0],
        &tasks[1],
        &tasks[2]
    };
    Bikeshed_TaskID task_ids[3];
    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 3, funcs, contexts, task_ids));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[0], 2, &task_ids[1]));
    Bikeshed_ReadyTasks(shed, 2, &task_ids[1]);
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));
    ASSERT_EQ(3, myCallback.ready_count);

    myCallback.ready_count = 0;
    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 3, funcs, contexts, task_ids));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[0], 1, &task_ids[2]));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[1], 1, &task_ids[2]));
    Bikeshed_ReadyTasks(shed, 1, &task_ids[2]);
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));
    ASSERT_EQ(3, myCallback.ready_count);

    myCallback.ready_count = 0;
    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 3, funcs, contexts, task_ids));
    Bikeshed_SetTasksChannel(shed, 1, &task_ids[0], 2);
    Bikeshed_SetTasksChannel(shed, 1, &task_ids[1], 1);
    Bikeshed_SetTasksChannel(shed, 1, &task_ids[2], 0);
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, 2, &task_ids[0], 1, &task_ids[2]));
    Bikeshed_ReadyTasks(shed, 1, &task_ids[2]);
    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 1));
    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 2));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 1));
    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 1));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 2));
    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 2));
    ASSERT_EQ(3, myCallback.ready_count);
}

TEST(Bikeshed, Dependency)
{
    AssertAbort fatal;

    char mem[BIKESHED_SIZE(5, 5, 1)];
    Bikeshed shed = Bikeshed_Create(mem, 5, 5, 1, 0);

    TaskData           tasks[5];
    BikeShed_TaskFunc funcs[5] = {
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute
    };
    void* contexts[5] = {
        &tasks[0],
        &tasks[1],
        &tasks[2],
        &tasks[3],
        &tasks[4]
    };
    Bikeshed_TaskID task_ids[5];

    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 5, funcs, contexts, task_ids));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[0], 3, &task_ids[1]));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[3], 1, &task_ids[4]));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[1], 1, &task_ids[4]));
    Bikeshed_ReadyTasks(shed, 1, &task_ids[2]);
    Bikeshed_ReadyTasks(shed, 1, &task_ids[4]);

    for (uint32_t i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    }
    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));

    for (uint32_t i = 0; i < 5; ++i)
    {
        ASSERT_EQ(shed, tasks[i].shed);
        ASSERT_EQ(task_ids[i], tasks[i].task_id);
        ASSERT_EQ(1u, tasks[i].executed);
    }

    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));
}

struct NodeWorker
{
    NodeWorker()
        : stop(0)
        , shed(0)
        , condition_variable(0)
        , thread(0)
    {
    }

    ~NodeWorker()
    {
    }

    bool CreateThread(Bikeshed in_shed, nadir::HConditionVariable in_condition_variable, nadir::TAtomic32* in_stop)
    {
        shed               = in_shed;
        stop               = in_stop;
        condition_variable = in_condition_variable;
        thread             = nadir::CreateThread(malloc(nadir::GetThreadSize()), NodeWorker::Execute, 0, this);
        return thread != 0;
    }

    void DisposeThread()
    {
        nadir::DeleteThread(thread);
        free(thread);
    }

    static int32_t Execute(void* context)
    {
        NodeWorker* _this = reinterpret_cast<NodeWorker*>(context);

        while (*_this->stop == 0)
        {
            if (!Bikeshed_ExecuteOne(_this->shed, 0))
            {
                nadir::SleepConditionVariable(_this->condition_variable, 1000);
            }
        }
        return 0;
    }

    nadir::TAtomic32*            stop;
    Bikeshed                    shed;
    nadir::HConditionVariable    condition_variable;
    nadir::HThread                thread;
};

struct NadirLock
{
    Bikeshed_ReadyCallback m_ReadyCallback;
    NadirLock()
        : m_Lock(nadir::CreateLock(malloc(nadir::GetNonReentrantLockSize())))
        , m_ConditionVariable(nadir::CreateConditionVariable(malloc(nadir::GetConditionVariableSize()), m_Lock))
        , m_ReadyCount(0)
    {
        m_ReadyCallback.SignalReady = signal;
    }
    ~NadirLock()
    {
        nadir::DeleteConditionVariable(m_ConditionVariable);
        free(m_ConditionVariable);
        nadir::DeleteNonReentrantLock(m_Lock);
        free(m_Lock);
    }
    static void signal(Bikeshed_ReadyCallback* primitive, uint32_t ready_count)
    {
        NadirLock* _this = reinterpret_cast<NadirLock*>(primitive);
        nadir::AtomicAdd32(&_this->m_ReadyCount, (long)ready_count);
        if (ready_count > 1)
        {
            nadir::WakeAll(_this->m_ConditionVariable);
        }
        else if (ready_count > 0)
        {
            nadir::WakeOne(_this->m_ConditionVariable);
        }
    }
    nadir::HNonReentrantLock  m_Lock;
    nadir::HConditionVariable m_ConditionVariable;
    nadir::TAtomic32          m_ReadyCount;
};

struct TaskDataWorker
{
    TaskDataWorker()
        : done(0)
        , shed(0)
        , task_id(0)
        , channel(255)
        , executed(0)
    {
    }
    static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, uint8_t channel, void* context_data)
    {
        TaskDataWorker* _this = reinterpret_cast<TaskDataWorker*>(context_data);
        if (nadir::AtomicAdd32(&_this->executed, 1) != 1)
        {
            exit(-1);
        }
        _this->shed    = shed;
        _this->task_id = task_id;
        _this->channel = channel;
        if (_this->done != 0)
        {
            nadir::AtomicAdd32(_this->done, 1);
        }
        return BIKESHED_TASK_RESULT_COMPLETE;
    }
    nadir::TAtomic32* done;
    Bikeshed          shed;
    Bikeshed_TaskID   task_id;
    uint8_t           channel;
    nadir::TAtomic32  executed;
};

TEST(Bikeshed, WorkerThread)
{
    AssertAbort fatal;

    NadirLock sync_primitive;

    nadir::TAtomic32 stop = 0;
    TaskDataWorker         task;
    task.done = &stop;

    char mem[BIKESHED_SIZE(1, 0, 1)];
    Bikeshed shed = Bikeshed_Create(mem, 1, 0, 1, &sync_primitive.m_ReadyCallback);

    BikeShed_TaskFunc funcs[1]    = { TaskDataWorker::Compute };
    void*              contexts[1] = { &task };

    NodeWorker thread_context;
    thread_context.CreateThread(shed, sync_primitive.m_ConditionVariable, &stop);

    Bikeshed_TaskID task_id;
    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 1, funcs, contexts, &task_id));
    Bikeshed_ReadyTasks(shed, 1, &task_id);

    nadir::JoinThread(thread_context.thread, nadir::TIMEOUT_INFINITE);
    thread_context.DisposeThread();

    ASSERT_EQ(shed, task.shed);
    ASSERT_EQ(task_id, task.task_id);
    ASSERT_EQ(1, task.executed);

    ASSERT_EQ(1, sync_primitive.m_ReadyCount);

    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));
}

struct TaskData2
{
    TaskData2()
        : done(0)
        , shed(0)
        , task_id(0)
        , executed(0)
    {
    }
    static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, uint8_t , void* context_data)
    {
        TaskData2* _this = reinterpret_cast<TaskData2*>(context_data);
        _this->shed    = shed;
        _this->task_id = task_id;
        nadir::AtomicAdd32(_this->executed, 1);
        return BIKESHED_TASK_RESULT_COMPLETE;
    }
    nadir::TAtomic32* done;
    Bikeshed          shed;
    Bikeshed_TaskID   task_id;
    nadir::TAtomic32* executed;
};

TEST(Bikeshed, WorkerThreads)
{
    AssertAbort fatal;

    NadirLock sync_primitive;

    nadir::TAtomic32 stop = 0;
    nadir::TAtomic32 executed = 0;
    TaskData2         task;
    task.executed = &executed;

    Bikeshed shed = Bikeshed_Create(malloc(BIKESHED_SIZE(65535, 0, 1)), 65535, 0, 1, &sync_primitive.m_ReadyCallback);

    BikeShed_TaskFunc funcs[1]    = { TaskData2::Compute };
    void*              contexts[1] = { &task };

    NodeWorker thread_context[5];
    thread_context[0].CreateThread(shed, sync_primitive.m_ConditionVariable, &stop);
    thread_context[1].CreateThread(shed, sync_primitive.m_ConditionVariable, &stop);
    thread_context[2].CreateThread(shed, sync_primitive.m_ConditionVariable, &stop);
    thread_context[3].CreateThread(shed, sync_primitive.m_ConditionVariable, &stop);
    thread_context[4].CreateThread(shed, sync_primitive.m_ConditionVariable, &stop);

    for (uint32_t i = 0;  i < 65535; ++i)
    {
        Bikeshed_TaskID task_id;
        if (!Bikeshed_CreateTasks(shed, 1, funcs, contexts, &task_id)) {
            break;
        }
        Bikeshed_ReadyTasks(shed, 1, &task_id);
    }

    while (executed != 65535)
    {
        nadir::Sleep(1000);
    }

    ASSERT_EQ(65535, sync_primitive.m_ReadyCount);

    sync_primitive.signal(&sync_primitive.m_ReadyCallback, 5);
    nadir::AtomicAdd32(&stop, 1);

    nadir::JoinThread(thread_context[0].thread, nadir::TIMEOUT_INFINITE);
    nadir::JoinThread(thread_context[1].thread, nadir::TIMEOUT_INFINITE);
    nadir::JoinThread(thread_context[2].thread, nadir::TIMEOUT_INFINITE);
    nadir::JoinThread(thread_context[3].thread, nadir::TIMEOUT_INFINITE);
    nadir::JoinThread(thread_context[4].thread, nadir::TIMEOUT_INFINITE);

    thread_context[0].DisposeThread();
    thread_context[1].DisposeThread();
    thread_context[2].DisposeThread();
    thread_context[3].DisposeThread();
    thread_context[4].DisposeThread();

    ASSERT_EQ(shed, task.shed);
    ASSERT_EQ(65535, executed);

    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));

    free(shed);
}

TEST(Bikeshed, DependenciesThread)
{
    AssertAbort fatal;

    NadirLock sync_primitive;

    nadir::TAtomic32 stop = 0;

    TaskDataWorker tasks[5];
    tasks[0].done               = &stop;
    BikeShed_TaskFunc funcs[5] = {
        TaskDataWorker::Compute,
        TaskDataWorker::Compute,
        TaskDataWorker::Compute,
        TaskDataWorker::Compute,
        TaskDataWorker::Compute
    };
    void* contexts[5] = {
        &tasks[0],
        &tasks[1],
        &tasks[2],
        &tasks[3],
        &tasks[4]
    };
    Bikeshed_TaskID task_ids[5];

    char mem[BIKESHED_SIZE(5, 5, 1)];
    Bikeshed shed = Bikeshed_Create(mem, 5, 5, 1, &sync_primitive.m_ReadyCallback);

    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 5, funcs, contexts, task_ids));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[0], 3, &task_ids[1]));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[3], 1, &task_ids[4]));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[1], 1, &task_ids[4]));

    NodeWorker thread_context;
    thread_context.CreateThread(shed, sync_primitive.m_ConditionVariable, &stop);
    Bikeshed_ReadyTasks(shed, 1, &task_ids[2]);
    Bikeshed_ReadyTasks(shed, 1, &task_ids[4]);

    nadir::JoinThread(thread_context.thread, nadir::TIMEOUT_INFINITE);
    thread_context.DisposeThread();

    for (uint32_t i = 0; i < 5; ++i)
    {
        ASSERT_EQ(shed, tasks[i].shed);
        ASSERT_EQ(task_ids[i], tasks[i].task_id);
        ASSERT_EQ(1, tasks[i].executed);
    }
    ASSERT_EQ(5, sync_primitive.m_ReadyCount);

    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));
}

TEST(Bikeshed, DependenciesThreads)
{
    AssertAbort fatal;

    NadirLock sync_primitive;

    static const uint32_t LAYER_COUNT                   = 4;
    static const uint32_t LAYER_0_TASK_COUNT            = 1;
    static const uint32_t LAYER_1_TASK_COUNT            = 1024;
    static const uint32_t LAYER_2_TASK_COUNT            = 796;
    static const uint32_t LAYER_3_TASK_COUNT            = 640;
    static const uint32_t LAYER_TASK_COUNT[LAYER_COUNT] = { LAYER_0_TASK_COUNT, LAYER_1_TASK_COUNT, LAYER_2_TASK_COUNT, LAYER_3_TASK_COUNT };
    static const uint32_t TASK_COUNT                    = (uint32_t)(LAYER_0_TASK_COUNT + LAYER_1_TASK_COUNT + LAYER_2_TASK_COUNT + LAYER_3_TASK_COUNT);
    static const uint32_t DEPENDENCY_COUNT              = LAYER_1_TASK_COUNT + LAYER_2_TASK_COUNT + LAYER_3_TASK_COUNT;

    static const uint32_t LAYER_TASK_OFFSET[LAYER_COUNT] = {
        0,
        LAYER_0_TASK_COUNT,
        (uint32_t)(LAYER_0_TASK_COUNT + LAYER_1_TASK_COUNT),
        (uint32_t)(LAYER_0_TASK_COUNT + LAYER_1_TASK_COUNT + LAYER_2_TASK_COUNT)
    };

    nadir::TAtomic32 stop = 0;
    nadir::TAtomic32 done = 0;

    Bikeshed_TaskID task_ids[TASK_COUNT];
    TaskDataWorker          tasks[TASK_COUNT];
    tasks[0].done = &done;

    BikeShed_TaskFunc funcs[TASK_COUNT];
    void*              contexts[TASK_COUNT];
    for (uint32_t task_index = 0; task_index < TASK_COUNT; ++task_index)
    {
        funcs[task_index]    = TaskDataWorker::Compute;
        contexts[task_index] = &tasks[task_index];
    }

    Bikeshed shed = Bikeshed_Create(malloc(BIKESHED_SIZE(TASK_COUNT, DEPENDENCY_COUNT, 1)), TASK_COUNT, DEPENDENCY_COUNT, 1, &sync_primitive.m_ReadyCallback);

    for (uint32_t layer_index = 0; layer_index < LAYER_COUNT; ++layer_index)
    {
        uint32_t task_offset = LAYER_TASK_OFFSET[layer_index];
        ASSERT_TRUE(Bikeshed_CreateTasks(shed, LAYER_TASK_COUNT[layer_index], &funcs[task_offset], &contexts[task_offset], &task_ids[task_offset]));
    }

    ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[0], LAYER_TASK_COUNT[1], &task_ids[LAYER_TASK_OFFSET[1]]));
    for (uint32_t i = 0; i < LAYER_TASK_COUNT[2]; ++i)
    {
        uint32_t parent_index = LAYER_TASK_OFFSET[1] + i;
        uint32_t child_index  = LAYER_TASK_OFFSET[2] + i;
        ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[parent_index], 1, &task_ids[child_index]));
    }
    for (uint32_t i = 0; i < LAYER_TASK_COUNT[3]; ++i)
    {
        uint32_t parent_index = LAYER_TASK_OFFSET[2] + i;
        uint32_t child_index  = LAYER_TASK_OFFSET[3] + i;
        ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[parent_index], 1, &task_ids[child_index]));
    }

    static const uint32_t WORKER_COUNT = 7;
    NodeWorker            workers[WORKER_COUNT];
    for (uint32_t worker_index = 0; worker_index < WORKER_COUNT; ++worker_index)
    {
        ASSERT_TRUE(workers[worker_index].CreateThread(shed, sync_primitive.m_ConditionVariable, &stop));
    }
    Bikeshed_ReadyTasks(shed, LAYER_TASK_COUNT[3], &task_ids[LAYER_TASK_OFFSET[3]]);
    Bikeshed_ReadyTasks(shed, LAYER_TASK_COUNT[2] - LAYER_TASK_COUNT[3], &task_ids[LAYER_TASK_OFFSET[2] + LAYER_TASK_COUNT[3]]);
    Bikeshed_ReadyTasks(shed, LAYER_TASK_COUNT[1] - LAYER_TASK_COUNT[2], &task_ids[LAYER_TASK_OFFSET[1] + LAYER_TASK_COUNT[2]]);

    while (!done)
    {
        if (!Bikeshed_ExecuteOne(shed, 0))
        {
            // We can't wait for the signal here since it only signals if there is work to be done
            // Ie, if another thread executes the last work item that sets done to true we will
            // not get a signal to wake up since no new work will be set to ready.
            // So we just go like crazy until top level task sets the 'done' flag
            nadir::Sleep(1000);
        }
    }
    ASSERT_EQ((long)TASK_COUNT, sync_primitive.m_ReadyCount);
    nadir::AtomicAdd32(&stop, WORKER_COUNT);
    nadir::WakeAll(sync_primitive.m_ConditionVariable);

    for (uint32_t worker_index = 0; worker_index < WORKER_COUNT; ++worker_index)
    {
        while (!nadir::JoinThread(workers[worker_index].thread, 1000))
        {
            // Need to look into logic for breaking workers, right now we can get in a state where
            // a thread is not woken up
            nadir::WakeAll(sync_primitive.m_ConditionVariable);
        }
    }

    for (uint32_t worker_index = 0; worker_index < WORKER_COUNT; ++worker_index)
    {
        workers[worker_index].DisposeThread();
    }

    for (uint32_t i = 0; i < TASK_COUNT; ++i)
    {
        ASSERT_EQ(shed, tasks[i].shed);
        ASSERT_EQ(task_ids[i], tasks[i].task_id);
        ASSERT_EQ(1, tasks[i].executed);
    }

    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));

    free(shed);
}

struct MotherData
{
    MotherData()
        : shed(0)
        , task_id(0)
        , executed(0)
        , sub_task_spawned(0)
        , sub_task_executed(0)
    {
        for (uint32_t i = 0; i < 5; ++i)
        {
            funcs[i] = 0;
            contexts[i] = 0;
            task_ids[i] = 0u;
        }
    }

    struct ChildData
    {
        ChildData()
            : mother_data(0)
        { }

        static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID , uint8_t , void* context_data)
        {
            ChildData* _this = reinterpret_cast<ChildData*>(context_data);
            _this->mother_data->sub_task_executed++;
            --_this->mother_data->sub_task_spawned;
            if (_this->mother_data->sub_task_spawned == 0)
            {
                Bikeshed_ReadyTasks(shed, 1, &_this->mother_data->task_id);
            }
            return BIKESHED_TASK_RESULT_COMPLETE;
        }
        MotherData*       mother_data;
    };

    static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, uint8_t , void* context_data)
    {
        MotherData* _this = reinterpret_cast<MotherData*>(context_data);

        _this->shed    = shed;
        _this->task_id = task_id;

        if (_this->sub_task_executed == 0)
        {
            _this->funcs[0]    = ChildData::Compute;
            _this->funcs[1]    = ChildData::Compute;
            _this->funcs[2]    = ChildData::Compute;
            _this->sub_tasks[0].mother_data = _this;
            _this->sub_tasks[1].mother_data = _this;
            _this->sub_tasks[2].mother_data = _this;
            _this->contexts[0] = &_this->sub_tasks[0];
            _this->contexts[1] = &_this->sub_tasks[1];
            _this->contexts[2] = &_this->sub_tasks[2];
            Bikeshed_CreateTasks(shed, 3, &_this->funcs[0], &_this->contexts[0], &_this->task_ids[0]);

            _this->sub_task_spawned += 3;
            Bikeshed_ReadyTasks(shed, 3, &_this->task_ids[0]);
            return BIKESHED_TASK_RESULT_BLOCKED;
        }
        else if (_this->sub_task_executed == 3)
        {
            _this->funcs[3]    = ChildData::Compute;
            _this->funcs[4]    = ChildData::Compute;
            _this->sub_tasks[3].mother_data = _this;
            _this->sub_tasks[4].mother_data = _this;
            _this->contexts[3] = &_this->sub_tasks[3];
            _this->contexts[4] = &_this->sub_tasks[4];
            Bikeshed_CreateTasks(shed, 2, &_this->funcs[3], &_this->contexts[3], &_this->task_ids[3]);

            _this->sub_task_spawned += 2;
            Bikeshed_ReadyTasks(shed, 2, &_this->task_ids[3]);
            return BIKESHED_TASK_RESULT_BLOCKED;
        }
        else if (_this->sub_task_executed == 5)
        {
            return BIKESHED_TASK_RESULT_COMPLETE;
        }
        else
        {
            exit(-1);
        }
    }
    Bikeshed            shed;
    Bikeshed_TaskID     task_id;
    nadir::TAtomic32    executed;
    uint32_t            sub_task_spawned;
    uint32_t            sub_task_executed;

    ChildData           sub_tasks[5];
    BikeShed_TaskFunc   funcs[5];
    void*               contexts[5];
    Bikeshed_TaskID     task_ids[5];
};

TEST(Bikeshed, InExecutionSpawnTasks)
{
    AssertAbort fatal;

    NadirLock sync_primitive;

    nadir::TAtomic32 stop = 0;

    Bikeshed_TaskID  mother_task_id;
    MotherData         mother_task;
    void*              mother_context = &mother_task;
    BikeShed_TaskFunc mother_func[1] = { MotherData::Compute };

    char mem[BIKESHED_SIZE(4, 3, 1)];
    Bikeshed shed = Bikeshed_Create(mem, 4, 3, 1, &sync_primitive.m_ReadyCallback);

    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 1, mother_func, &mother_context, &mother_task_id));
    Bikeshed_ReadyTasks(shed, 1, &mother_task_id);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0)); // Mother
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0)); // Child[0]
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0)); // Child[1]
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0)); // Child[2]
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0)); // Mother
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0)); // Child[3]
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0)); // Child[4]
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0)); // Mother

    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));

    ASSERT_EQ(0, stop);
}

TEST(Bikeshed, ShedCopyState)
{
    AssertAbort fatal;

    struct CounterTask
    {
        CounterTask()
            : m_Counter(0)
        {
        }

        static Bikeshed_TaskResult Execute(Bikeshed , Bikeshed_TaskID , uint8_t , void* context_data)
        {
            CounterTask* counter_task = reinterpret_cast<CounterTask*>(context_data);
            nadir::AtomicAdd32(&counter_task->m_Counter, 1);
            return BIKESHED_TASK_RESULT_COMPLETE;
        }
        nadir::TAtomic32 m_Counter;
    };

    // Mother0
    //  Mother0Child0
    //    Mother0Child0Child0
    //    Mother0Child0Child1
    //  Mother0Child1
    //    Mother0Child1Child0
    //    Mother0Child1Child1
    //    Mother0Child1Child2
    // Mother1
    // Mother2
    // Mother3
    //
    //   Mother123Child0
    //   Mother123Child1

    // 13 tasks
    // 13 dependencies

    uint32_t shed_size = BIKESHED_SIZE(13, 13, 1);

    void* shed_master_mem = malloc(shed_size);
    ASSERT_NE(reinterpret_cast<void*>(0), shed_master_mem);
    void* shed_execute_mem = malloc(shed_size);
    ASSERT_NE(reinterpret_cast<void*>(0), shed_execute_mem);

    NadirLock sync_primitive;

    Bikeshed shed_master = Bikeshed_Create(shed_master_mem, 13, 13, 1, &sync_primitive.m_ReadyCallback);

    // Build graph

    CounterTask mother[4];
    Bikeshed_TaskID motherTaskId[4];
    BikeShed_TaskFunc motherFunc[4] = {CounterTask::Execute, CounterTask::Execute, CounterTask::Execute, CounterTask::Execute};
    void* motherContext[4] = {&mother[0], &mother[1], &mother[2], &mother[3]};

    ASSERT_NE(0, Bikeshed_CreateTasks(shed_master, 4, motherFunc, motherContext, motherTaskId));

    CounterTask mother0Child[2];
    Bikeshed_TaskID mother0ChildTaskId[2];
    BikeShed_TaskFunc mother0ChildFunc[2] = {CounterTask::Execute, CounterTask::Execute};
    void* mother0ChildContext[2] = {&mother0Child[0], &mother0Child[1]};
    ASSERT_NE(0, Bikeshed_CreateTasks(shed_master, 2, mother0ChildFunc, mother0ChildContext, mother0ChildTaskId));
    ASSERT_NE(0, Bikeshed_AddDependencies(shed_master, 1, &motherTaskId[0], 2, mother0ChildTaskId));

    CounterTask mother0Child0Child[2];
    Bikeshed_TaskID mother0Child0ChildTaskId[2];
    BikeShed_TaskFunc mother0Child0ChildFunc[2] = {CounterTask::Execute, CounterTask::Execute};
    void* mother0Child0ChildContext[2] = {&mother0Child0Child[0], &mother0Child0Child[1]};
    ASSERT_NE(0, Bikeshed_CreateTasks(shed_master, 2, mother0Child0ChildFunc, mother0Child0ChildContext, mother0Child0ChildTaskId));
    ASSERT_NE(0, Bikeshed_AddDependencies(shed_master, 1, &mother0ChildTaskId[0], 2, mother0Child0ChildTaskId));

    CounterTask mother0Child1Child[3];
    Bikeshed_TaskID mother0Child1ChildTaskId[3];
    BikeShed_TaskFunc mother0Child1ChildFunc[3] = {CounterTask::Execute, CounterTask::Execute, CounterTask::Execute};
    void* mother0Child1ChildContext[3] = {&mother0Child1Child[0], &mother0Child1Child[1], &mother0Child1Child[2]};
    ASSERT_NE(0, Bikeshed_CreateTasks(shed_master, 3, mother0Child1ChildFunc, mother0Child1ChildContext, mother0Child1ChildTaskId));
    ASSERT_NE(0, Bikeshed_AddDependencies(shed_master, 1, &mother0ChildTaskId[1], 3, mother0Child1ChildTaskId));

    CounterTask mother123Child[2];
    Bikeshed_TaskID mother123ChildTaskId[2];
    BikeShed_TaskFunc mother123ChildFunc[2] = {CounterTask::Execute, CounterTask::Execute};
    void* mother123ChildContext[2] = {&mother123Child[0], &mother123Child[1]};
    ASSERT_NE(0, Bikeshed_CreateTasks(shed_master, 2, mother123ChildFunc, mother123ChildContext, mother123ChildTaskId));

    ASSERT_NE(0, Bikeshed_AddDependencies(shed_master, 1, &motherTaskId[1], 2, mother123ChildTaskId));
    ASSERT_NE(0, Bikeshed_AddDependencies(shed_master, 1, &motherTaskId[2], 2, mother123ChildTaskId));
    ASSERT_NE(0, Bikeshed_AddDependencies(shed_master, 1, &motherTaskId[3], 2, mother123ChildTaskId));

    Bikeshed_ReadyTasks(shed_master, 2, mother0Child0ChildTaskId);
    Bikeshed_ReadyTasks(shed_master, 3, mother0Child1ChildTaskId);
    Bikeshed_ReadyTasks(shed_master, 2, mother123ChildTaskId);

    // Copy state for execution
    Bikeshed shed_execute = Bikeshed_CloneState(shed_execute_mem, shed_master, shed_size);
    while (Bikeshed_ExecuteOne(shed_execute, 0));

    // Do it twice
    shed_execute = Bikeshed_CloneState(shed_execute_mem, shed_master, shed_size);
    while (Bikeshed_ExecuteOne(shed_execute, 0));

    // Do it three times
    shed_execute = Bikeshed_CloneState(shed_execute_mem, shed_master, shed_size);
    while (Bikeshed_ExecuteOne(shed_execute, 0));

    ASSERT_EQ(3, mother[0].m_Counter);
    ASSERT_EQ(3, mother[1].m_Counter);
    ASSERT_EQ(3, mother[2].m_Counter);
    ASSERT_EQ(3, mother[3].m_Counter);

    free(shed_master_mem);
    free(shed_execute_mem);
}

TEST(Bikeshed, Channels)
{
    AssertAbort fatal;

    char mem[BIKESHED_SIZE(5, 0, 5)];
    Bikeshed shed = Bikeshed_Create(mem, 5, 0, 5, 0);

    TaskData           tasks[5];
    BikeShed_TaskFunc funcs[5] = {
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute
    };
    void* contexts[5] = {
        &tasks[0],
        &tasks[1],
        &tasks[2],
        &tasks[3],
        &tasks[4]
    };
    Bikeshed_TaskID task_ids[5];

    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 5, funcs, contexts, task_ids));

    Bikeshed_SetTasksChannel(shed, 1, &task_ids[0], 3);
    Bikeshed_SetTasksChannel(shed, 1, &task_ids[1], 1);
    Bikeshed_SetTasksChannel(shed, 1, &task_ids[2], 4);
    Bikeshed_SetTasksChannel(shed, 1, &task_ids[3], 0);
    Bikeshed_SetTasksChannel(shed, 1, &task_ids[4], 2);

    Bikeshed_ReadyTasks(shed, 5, &task_ids[0]);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_EQ(task_ids[3], tasks[3].task_id);
    ASSERT_EQ(shed, tasks[3].shed);
    ASSERT_EQ(1u, tasks[3].executed);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 1));
    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 1));
    ASSERT_EQ(task_ids[1], tasks[1].task_id);
    ASSERT_EQ(shed, tasks[1].shed);
    ASSERT_EQ(1u, tasks[1].executed);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 2));
    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 2));
    ASSERT_EQ(task_ids[4], tasks[4].task_id);
    ASSERT_EQ(shed, tasks[4].shed);
    ASSERT_EQ(1u, tasks[4].executed);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 3));
    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 3));
    ASSERT_EQ(task_ids[0], tasks[0].task_id);
    ASSERT_EQ(shed, tasks[0].shed);
    ASSERT_EQ(1u, tasks[0].executed);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 4));
    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 4));
    ASSERT_EQ(task_ids[2], tasks[2].task_id);
    ASSERT_EQ(shed, tasks[2].shed);
    ASSERT_EQ(1u, tasks[2].executed);
}

TEST(Bikeshed, ChannelRanges)
{
    AssertAbort fatal;

    char mem[BIKESHED_SIZE(16, 0, 5)];
    Bikeshed shed = Bikeshed_Create(mem, 16, 0, 5, 0);

    TaskData           tasks[16];
    BikeShed_TaskFunc funcs[16] = {
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute
    };
    void* contexts[16] = {
        &tasks[0],
        &tasks[1],
        &tasks[2],
        &tasks[3],
        &tasks[4],
        &tasks[5],
        &tasks[6],
        &tasks[7],
        &tasks[8],
        &tasks[9],
        &tasks[10],
        &tasks[11],
        &tasks[12],
        &tasks[13],
        &tasks[14],
        &tasks[15]
    };
    Bikeshed_TaskID task_ids[16];

    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 16, funcs, contexts, task_ids));

    Bikeshed_SetTasksChannel(shed, 5, &task_ids[0], 2);
    Bikeshed_SetTasksChannel(shed, 5, &task_ids[5], 0);
    Bikeshed_SetTasksChannel(shed, 5, &task_ids[10], 1);
    Bikeshed_SetTasksChannel(shed, 1, &task_ids[15], 0);

    Bikeshed_ReadyTasks(shed, 16, &task_ids[0]);

    for (uint32_t i = 0; i < 6; ++i)
    {
        ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    }
    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));

    for (uint32_t i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 1));
    }
    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 1));

    for (uint32_t i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 2));
    }
    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 2));

    for (uint32_t i = 0; i < 5; ++i)
    {
        ASSERT_EQ(task_ids[i + 0], tasks[i + 0].task_id);
        ASSERT_EQ(shed, tasks[i + 0].shed);
        ASSERT_EQ(1u, tasks[i + 0].executed);
        ASSERT_EQ(2u, tasks[i + 0].channel);
    }

    for (uint32_t i = 0; i < 5; ++i)
    {
        ASSERT_EQ(task_ids[i + 5], tasks[i + 5].task_id);
        ASSERT_EQ(shed, tasks[i + 5].shed);
        ASSERT_EQ(1u, tasks[i + 5].executed);
        ASSERT_EQ(0u, tasks[i + 5].channel);
    }
    ASSERT_EQ(task_ids[15], tasks[15].task_id);
    ASSERT_EQ(shed, tasks[15].shed);
    ASSERT_EQ(1u, tasks[15].executed);
    ASSERT_EQ(0u, tasks[15].channel);

    for (uint32_t i = 0; i < 5; ++i)
    {
        ASSERT_EQ(task_ids[i + 10], tasks[i + 10].task_id);
        ASSERT_EQ(shed, tasks[i + 10].shed);
        ASSERT_EQ(1u, tasks[i + 10].executed);
        ASSERT_EQ(1u, tasks[i + 10].channel);
    }

}

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

    void DisposeThread()
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
            if (ExecuteOne(_this->shed, _this->index, _this->count))
            {
                continue;
            }
            nadir::SleepConditionVariable(_this->condition_variable, nadir::TIMEOUT_INFINITE);
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

struct TaskDataStealing
{
    static const uint32_t   WORKER_COUNT = 8;
    static const uint32_t   TASK_COUNT = WORKER_COUNT + WORKER_COUNT * WORKER_COUNT;
    TaskDataStealing()
        : executed_count(0)
        , complete_wakeup(0)
        , child_task_data(0)
        , executed_channel(WORKER_COUNT)
    {
    }
    static Bikeshed_TaskResult NoSpawn(Bikeshed , Bikeshed_TaskID , uint8_t channel, void* context)
    {
        TaskDataStealing* _this = reinterpret_cast<TaskDataStealing*>(context);
        _this->executed_channel = channel;
        NadirLock* complete_wakeup = _this->complete_wakeup;
        if (TASK_COUNT == nadir::AtomicAdd32(_this->executed_count, 1))
        {
            complete_wakeup->signal(&complete_wakeup->m_ReadyCallback, 1);
        }
        return BIKESHED_TASK_RESULT_COMPLETE;
    }
    static Bikeshed_TaskResult SpawnLocal(Bikeshed shed, Bikeshed_TaskID , uint8_t channel, void* context)
    {
        TaskDataStealing* _this = (TaskDataStealing*)context;
        _this->executed_channel = channel;

        TaskDataStealing*        tasks = _this->child_task_data;

        BikeShed_TaskFunc funcs[WORKER_COUNT];
        void* contexts[WORKER_COUNT];
        for (uint32_t i = 0; i < WORKER_COUNT; ++i)
        {
            tasks[i].executed_count = _this->executed_count;
            tasks[i].complete_wakeup = _this->complete_wakeup;
            tasks[i].child_task_data = 0;
            funcs[i] = TaskDataStealing::NoSpawn;
            contexts[i] = &tasks[i];
        }
        Bikeshed_TaskID task_ids[WORKER_COUNT];
        if (Bikeshed_CreateTasks(shed, WORKER_COUNT, funcs, contexts, task_ids))
        {
            Bikeshed_SetTasksChannel(shed, WORKER_COUNT, &task_ids[0], channel);
            Bikeshed_ReadyTasks(shed, WORKER_COUNT, &task_ids[0]);
        }

        nadir::AtomicAdd32(_this->executed_count, 1);
        return BIKESHED_TASK_RESULT_COMPLETE;
    }
    nadir::TAtomic32*   executed_count;
    NadirLock*          complete_wakeup;
    TaskDataStealing*   child_task_data;
    uint8_t             executed_channel;
};

TEST(Bikeshed, TaskStealing)
{
    AssertAbort fatal;

    static const uint32_t   SHED_SIZE BIKESHED_SIZE(TaskDataStealing::TASK_COUNT, 0, TaskDataStealing::WORKER_COUNT);
    char mem[SHED_SIZE];

    NadirLock complete_wakeup;
    NadirLock sync_primitive;
    
    Bikeshed shed = Bikeshed_Create(mem, TaskDataStealing::TASK_COUNT, 0, TaskDataStealing::WORKER_COUNT, &sync_primitive.m_ReadyCallback);

    nadir::TAtomic32 stop = 0;

    StealingNodeWorker    workers[TaskDataStealing::WORKER_COUNT];
    for (uint32_t worker_index = 0; worker_index < TaskDataStealing::WORKER_COUNT; ++worker_index)
    {
        ASSERT_TRUE(workers[worker_index].CreateThread(shed, sync_primitive.m_ConditionVariable, worker_index, TaskDataStealing::WORKER_COUNT, &stop));
    }

    nadir::TAtomic32 executed_count = 0;

    TaskDataStealing    tasks[TaskDataStealing::TASK_COUNT];
    BikeShed_TaskFunc   funcs[TaskDataStealing::WORKER_COUNT];
    void*               contexts[TaskDataStealing::WORKER_COUNT];
    for (uint32_t i = 0; i < TaskDataStealing::WORKER_COUNT; ++i)
    {
        tasks[i].executed_count = &executed_count;
        tasks[i].complete_wakeup = &complete_wakeup;
        tasks[i].child_task_data = &tasks[(i + 1) * TaskDataStealing::WORKER_COUNT];
        funcs[i] = TaskDataStealing::SpawnLocal;
        contexts[i] = &tasks[i];
    }

    Bikeshed_TaskID        task_ids[TaskDataStealing::WORKER_COUNT];

    ASSERT_TRUE(Bikeshed_CreateTasks(shed, TaskDataStealing::WORKER_COUNT, funcs, contexts, task_ids));

    for (uint8_t c = 0; c < (uint8_t)TaskDataStealing::WORKER_COUNT; ++c)
    {
        Bikeshed_SetTasksChannel(shed, 1, &task_ids[c], c);
    }

    Bikeshed_ReadyTasks(shed, TaskDataStealing::WORKER_COUNT, &task_ids[0]);

    while(executed_count != TaskDataStealing::TASK_COUNT)
    {
        nadir::SleepConditionVariable(complete_wakeup.m_ConditionVariable, 1000);
    }

    nadir::AtomicAdd32(&stop, TaskDataStealing::WORKER_COUNT);
    nadir::WakeAll(sync_primitive.m_ConditionVariable);

    for (uint32_t worker_index = 0; worker_index < TaskDataStealing::WORKER_COUNT; ++worker_index)
    {
        while (!nadir::JoinThread(workers[worker_index].thread, 1000))
        {
            // Need to look into logic for breaking workers, right now we can get in a state where
            // a thread is not woken up
            nadir::WakeAll(sync_primitive.m_ConditionVariable);
        }
    }

    for (uint32_t worker_index = 0; worker_index < TaskDataStealing::WORKER_COUNT; ++worker_index)
    {
        workers[worker_index].DisposeThread();
    }
}

TEST(Bikeshed, TaskCreateHalfwayOverflow)
{
    AssertAbort fatal;

    static const uint32_t   MAX_TASK_COUNT = 4;
    static const uint32_t   SHED_SIZE BIKESHED_SIZE(MAX_TASK_COUNT, 0, 1);
    char mem[SHED_SIZE];

    Bikeshed shed = Bikeshed_Create(mem, MAX_TASK_COUNT, 0, 1, 0);
    ASSERT_NE((Bikeshed)0, shed);

    TaskData           tasks[5];
    BikeShed_TaskFunc funcs[5] = {
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute
    };
    void* contexts[5] = {
        &tasks[0],
        &tasks[1],
        &tasks[2],
        &tasks[3],
        &tasks[4]
    };
    Bikeshed_TaskID task_ids[5];

    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 2, funcs, contexts, task_ids));

    ASSERT_FALSE(Bikeshed_CreateTasks(shed, 3, &funcs[2], &contexts[2], &task_ids[2]));

    Bikeshed_ReadyTasks(shed, 2, &task_ids[0]);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));

    ASSERT_EQ(task_ids[0], tasks[0].task_id);
    ASSERT_EQ(shed, tasks[0].shed);
    ASSERT_EQ(1u, tasks[0].executed);

    ASSERT_EQ(task_ids[1], tasks[1].task_id);
    ASSERT_EQ(shed, tasks[1].shed);
    ASSERT_EQ(1u, tasks[1].executed);

    ASSERT_EQ(0u, tasks[2].task_id);
    ASSERT_EQ(0, tasks[2].shed);
    ASSERT_EQ(0u, tasks[2].executed);

    ASSERT_EQ(0u, tasks[3].task_id);
    ASSERT_EQ(0, tasks[3].shed);
    ASSERT_EQ(0u, tasks[3].executed);

    ASSERT_EQ(0u, tasks[4].task_id);
    ASSERT_EQ(0, tasks[4].shed);
    ASSERT_EQ(0u, tasks[4].executed);
}

TEST(Bikeshed, DependencyCreateHalfwayOverflow)
{
    AssertAbort fatal;

    static const uint32_t   MAX_TASK_COUNT = 6;
    static const uint32_t   MAX_DEPENDENCY_COUNT = 4;
    static const uint32_t   SHED_SIZE BIKESHED_SIZE(MAX_TASK_COUNT, MAX_DEPENDENCY_COUNT, 1);
    char mem[SHED_SIZE];

    Bikeshed shed = Bikeshed_Create(mem, MAX_TASK_COUNT, MAX_DEPENDENCY_COUNT, 1, 0);
    ASSERT_NE((Bikeshed)0, shed);

    TaskData           tasks[6];
    BikeShed_TaskFunc funcs[6] = {
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute,
        TaskData::Compute
    };
    void* contexts[6] = {
        &tasks[0],
        &tasks[1],
        &tasks[2],
        &tasks[3],
        &tasks[4],
        &tasks[5]
    };
    Bikeshed_TaskID task_ids[6];

    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 6, funcs, contexts, task_ids));

    Bikeshed_TaskID dependency_ids[5] = {
        task_ids[1],
        task_ids[2],
        task_ids[3],
        task_ids[4],
        task_ids[5]
    };

    ASSERT_FALSE(Bikeshed_AddDependencies(shed, 1, &task_ids[0], 5, dependency_ids));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, 1, &task_ids[0], 4, dependency_ids));

    Bikeshed_ReadyTasks(shed, 4, &task_ids[1]);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));

    ASSERT_EQ(task_ids[0], tasks[0].task_id);
    ASSERT_EQ(shed, tasks[0].shed);
    ASSERT_EQ(1u, tasks[0].executed);

    ASSERT_EQ(task_ids[1], tasks[1].task_id);
    ASSERT_EQ(shed, tasks[1].shed);
    ASSERT_EQ(1u, tasks[1].executed);

    ASSERT_EQ(task_ids[2], tasks[2].task_id);
    ASSERT_EQ(shed, tasks[1].shed);
    ASSERT_EQ(1u, tasks[2].executed);

    ASSERT_EQ(task_ids[3], tasks[3].task_id);
    ASSERT_EQ(shed, tasks[3].shed);
    ASSERT_EQ(1u, tasks[3].executed);

    ASSERT_EQ(task_ids[4], tasks[4].task_id);
    ASSERT_EQ(shed, tasks[4].shed);
    ASSERT_EQ(1u, tasks[4].executed);

    ASSERT_EQ(0u, tasks[5].task_id);
    ASSERT_EQ(0, tasks[5].shed);
    ASSERT_EQ(0u, tasks[5].executed);
}

TEST(Bikeshed, MaxTaskCount)
{
    AssertAbort fatal;

    static const uint32_t   MAX_TASK_COUNT = 8388607;
    static const uint32_t   SHED_SIZE BIKESHED_SIZE(MAX_TASK_COUNT, 0, 1);
    void* mem = malloc(SHED_SIZE);
    ASSERT_NE(reinterpret_cast<void*>(0), mem);

    Bikeshed shed = Bikeshed_Create(mem, MAX_TASK_COUNT, 0, 1, 0);
    ASSERT_NE((Bikeshed)0, shed);
    free(mem);
}

TEST(Bikeshed, MaxOverdraftTaskCount)
{
    AssertExpect expect;

    static const uint32_t   MAX_TASK_COUNT = 8388608;
    static const uint32_t   SHED_SIZE BIKESHED_SIZE(MAX_TASK_COUNT, 0, 1);
    void* mem = malloc(SHED_SIZE);
    ASSERT_NE(reinterpret_cast<void*>(0), mem);

    Bikeshed_Create(mem, MAX_TASK_COUNT, 0, 1, 0);
#if defined(BIKESHED_ASSERTS)
    ASSERT_EQ(1u, AssertExpect::gAssertCount);
#endif
    free(mem);
}

TEST(Bikeshed, MaxDependencyCount)
{
    AssertAbort fatal;

    static const uint32_t   MAX_DEPENDENCY_COUNT = 8388607;
    static const uint32_t   SHED_SIZE BIKESHED_SIZE(2, MAX_DEPENDENCY_COUNT, 1);
    void* mem = malloc(SHED_SIZE);
    ASSERT_NE(reinterpret_cast<void*>(0), mem);

    Bikeshed_Create(mem, 2, MAX_DEPENDENCY_COUNT, 1, 0);
    free(mem);
}

TEST(Bikeshed, MaxOverdraftDependencyCount)
{
    AssertExpect expect;

    static const uint32_t   MAX_DEPENDENCY_COUNT = 8388608;
    static const uint32_t   SHED_SIZE BIKESHED_SIZE(2, MAX_DEPENDENCY_COUNT, 1);
    void* mem = malloc(SHED_SIZE);

    Bikeshed_Create(mem, 2, MAX_DEPENDENCY_COUNT, 1, 0);
#if defined(BIKESHED_ASSERTS)
    ASSERT_EQ(1u, AssertExpect::gAssertCount);
#endif
    free(mem);
}

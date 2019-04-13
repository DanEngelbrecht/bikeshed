#include "../third-party/jctest/src/jc_test.h"
#include "../third-party/nadir/src/nadir.h"

#define BIKESHED_IMPLEMENTATION
#include "../src/bikeshed.h"

#include <memory>

static uint32_t gAssertCount = 0;

static void Assert(const char*, int)
{
    ++gAssertCount;
}

TEST(Bikeshed, Assert)
{
    Bikeshed_SetAssert(Assert);
    Bikeshed shed = Bikeshed_Create(malloc(Bikeshed_GetSize(1, 0)), 1, 0, 0);
    ASSERT_NE((Bikeshed)0, shed);
#if defined(BIKESHED_ASSERTS)
    Bikeshed_TaskID invalid_task_id = 1;
    Bikeshed_ReadyTasks(shed, 1, &invalid_task_id);
    ASSERT_EQ(1u, gAssertCount);
#endif
    free(shed);
    Bikeshed_SetAssert(0);
}

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
    static void Assert(const char* file, int line)
    {
        printf("Assert at %s(%d)", file, line);
        ASSERT_TRUE(false);
    }
};

TEST(Bikeshed, SingleTask)
{
    AssertAbort fatal;

    Bikeshed shed = Bikeshed_Create(malloc(Bikeshed_GetSize(1, 0)), 1, 0, 0);
    ASSERT_NE((Bikeshed)0, shed);

    struct TaskData
    {
        TaskData()
            : shed(0)
            , task_id((Bikeshed_TaskID)-1)
            , executed(0)
        {
        }
        static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, void* task_context)
        {
            TaskData* task_data = (TaskData*)task_context;
            task_data->shed = shed;
            ++task_data->executed;
            task_data->task_id = task_id;
            return BIKESHED_TASK_RESULT_COMPLETE;
        }
        Bikeshed       shed;
        Bikeshed_TaskID     task_id;
        uint32_t    executed;
    } task;

    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));

    BikeShed_TaskFunc task_functions[1] = {TaskData::Compute};
    void* task_contexts[1] = {&task};

    Bikeshed_TaskID task_id;
    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 1, task_functions, task_contexts, &task_id));

    ASSERT_TRUE(!Bikeshed_CreateTasks(shed, 1, task_functions, task_contexts, &task_id));

    Bikeshed_ReadyTasks(shed, 1, &task_id);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));

    ASSERT_EQ(shed, task.shed);
    ASSERT_EQ(task.task_id, task_id);
    ASSERT_EQ(1u, task.executed);

    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));

    free(shed);
}

#if 0
TEST(Bikeshed, Yield)
{
    AssertAbort fatal;

    Bikeshed shed = Bikeshed_Create(malloc(Bikeshed_GetSize(2, 0)), 2, 0, 0x0);
    ASSERT_NE((Bikeshed)0, shed);

    struct TaskData
    {
        TaskData()
            : shed(0)
            , task_id((Bikeshed_TaskID)-1)
            , yield_count(0)
            , executed(0)
        {
        }
        static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, TaskData* task_data)
        {
            ++task_data->executed;
            if (task_data->yield_count > 0)
            {
                --task_data->yield_count;
                return BIKESHED_TASK_RESULT_BLOCKED;
            }
            task_data->shed    = shed;
            task_data->task_id = task_id;
            return BIKESHED_TASK_RESULT_COMPLETE;
        }
        Bikeshed   shed;
        Bikeshed_TaskID task_id;
        uint8_t           yield_count;
        uint32_t          executed;
    };

    TaskData tasks[2];
    tasks[0].yield_count = 1;

    BikeShed_TaskFunc funcs[2] = {
        (BikeShed_TaskFunc)TaskData::Compute,
        (BikeShed_TaskFunc)TaskData::Compute
    };
    void* contexts[2] = { &tasks[0], &tasks[1] };

    Bikeshed_TaskID task_ids[2];
    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 2, funcs, contexts, task_ids));

    Bikeshed_ReadyTasks(shed, 2, task_ids);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_EQ(0, tasks[0].yield_count);
    ASSERT_EQ(0, tasks[0].shed);
    ASSERT_EQ((Bikeshed_TaskID)-1, tasks[0].task_id);
    ASSERT_EQ(1u, tasks[0].executed);

    ASSERT_EQ(0, tasks[1].yield_count);
    ASSERT_EQ(0, tasks[1].shed);
    ASSERT_EQ((Bikeshed_TaskID)-1, tasks[1].task_id);
    ASSERT_EQ(0u, tasks[1].executed);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));
    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));

    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));

    ASSERT_EQ(0, tasks[0].yield_count);
    ASSERT_EQ(shed, tasks[0].shed);
    ASSERT_EQ(task_ids[0], tasks[0].task_id);
    ASSERT_EQ(2u, tasks[0].executed);

    ASSERT_EQ(0, tasks[1].yield_count);
    ASSERT_EQ(shed, tasks[1].shed);
    ASSERT_EQ(task_ids[1], tasks[1].task_id);
    ASSERT_EQ(1u, tasks[1].executed);

    free(shed);
}
#endif // 0

TEST(Bikeshed, Blocked)
{
    AssertAbort fatal;

    Bikeshed shed = Bikeshed_Create(malloc(Bikeshed_GetSize(2, 0)), 2, 0, 0x0);
    ASSERT_NE((Bikeshed)0, shed);

    struct TaskData
    {
        TaskData()
            : shed(0)
            , task_id((Bikeshed_TaskID)-1)
            , blocked_count(0)
            , executed(0)
        {
        }
        static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, void* task_context)
        {
            TaskData* task_data = (TaskData*)task_context;
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
    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));
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

    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));

    free(shed);
}

TEST(Bikeshed, Sync)
{
    AssertAbort fatal;

    struct FakeLock
    {
        Bikeshed_ReadyCallback m_ReadyCallback;
        FakeLock()
            : m_ReadyCallback { signal }
            , ready_count(0)
        {
        }
        static void signal(Bikeshed_ReadyCallback* primitive, uint32_t ready_count)
        {
            ((FakeLock*)primitive)->ready_count += ready_count;
        }
        uint32_t ready_count;
    } lock;
    Bikeshed shed = Bikeshed_Create(malloc(Bikeshed_GetSize(1, 0)), 1, 0, &lock.m_ReadyCallback);
    ASSERT_NE((Bikeshed)0, shed);

    struct TaskData
    {
        TaskData()
            : shed(0)
            , task_id((Bikeshed_TaskID)-1)
            , executed(0)
        {
        }
        static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, TaskData* task_data)
        {
            task_data->shed = shed;
            ++task_data->executed;
            task_data->task_id = task_id;
            return BIKESHED_TASK_RESULT_COMPLETE;
        }
        Bikeshed   shed;
        Bikeshed_TaskID task_id;
        uint32_t          executed;
    } task;

    BikeShed_TaskFunc funcs[1]    = { (BikeShed_TaskFunc)TaskData::Compute };
    void*              contexts[1] = { &task };

    Bikeshed_TaskID task_id;
    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 1, funcs, contexts, &task_id));

    ASSERT_TRUE(!Bikeshed_CreateTasks(shed, 1, funcs, contexts, &task_id));

    Bikeshed_ReadyTasks(shed, 1, &task_id);
    ASSERT_EQ(1u, lock.ready_count);

    ASSERT_TRUE(Bikeshed_ExecuteOne(shed, 0));

    ASSERT_EQ(shed, task.shed);
    ASSERT_EQ(task.task_id, task_id);
    ASSERT_EQ(1u, task.executed);

    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));
    ASSERT_EQ(1u, lock.ready_count);

    free(shed);
}

TEST(Bikeshed, ReadyOrder)
{
    AssertAbort fatal;

    Bikeshed shed = Bikeshed_Create(malloc(Bikeshed_GetSize(5, 4)), 5, 4, 0);
    ASSERT_NE((Bikeshed)0, shed);

    struct TaskData
    {
        TaskData()
            : shed(0)
            , task_id((Bikeshed_TaskID)-1)
            , executed(0)
        {
        }
        static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, TaskData* task_data)
        {
            task_data->shed = shed;
            ++task_data->executed;
            task_data->task_id = task_id;
            return BIKESHED_TASK_RESULT_COMPLETE;
        }
        Bikeshed   shed;
        Bikeshed_TaskID task_id;
        uint32_t          executed;
    };

    TaskData           tasks[5];
    BikeShed_TaskFunc funcs[5] = {
        (BikeShed_TaskFunc)TaskData::Compute,
        (BikeShed_TaskFunc)TaskData::Compute,
        (BikeShed_TaskFunc)TaskData::Compute,
        (BikeShed_TaskFunc)TaskData::Compute,
        (BikeShed_TaskFunc)TaskData::Compute
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
    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));
    free(shed);
}

TEST(Bikeshed, Dependency)
{
    AssertAbort fatal;

    Bikeshed shed = Bikeshed_Create(malloc(Bikeshed_GetSize(5, 5)), 5, 5, 0);
    ASSERT_NE((Bikeshed)0, shed);

    struct TaskData
    {
        TaskData()
            : shed(0)
            , task_id(0)
            , executed(0)
        {
        }
        static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, TaskData* task_data)
        {
            task_data->shed = shed;
            ++task_data->executed;
            task_data->task_id = task_id;
            return BIKESHED_TASK_RESULT_COMPLETE;
        }
        Bikeshed   shed;
        Bikeshed_TaskID task_id;
        uint32_t          executed;
    };

    TaskData           tasks[5];
    BikeShed_TaskFunc funcs[5] = {
        (BikeShed_TaskFunc)TaskData::Compute,
        (BikeShed_TaskFunc)TaskData::Compute,
        (BikeShed_TaskFunc)TaskData::Compute,
        (BikeShed_TaskFunc)TaskData::Compute,
        (BikeShed_TaskFunc)TaskData::Compute
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
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, task_ids[0], 3, &task_ids[1]));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, task_ids[3], 1, &task_ids[4]));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, task_ids[1], 1, &task_ids[4]));
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

    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));
    free(shed);
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
        NodeWorker* _this = (NodeWorker*)context;

        Bikeshed_TaskID next_ready_task = 0;
        while (*_this->stop == 0)
        {
            if (next_ready_task != 0)
            {
                Bikeshed_ExecuteAndResolve(_this->shed, next_ready_task, &next_ready_task);
                continue;
            }
            if (!Bikeshed_ExecuteOne(_this->shed, &next_ready_task))
            {
                nadir::SleepConditionVariable(_this->condition_variable, 1000);
            }
        }
        return 0;
    }

    nadir::TAtomic32*         stop;
    Bikeshed           shed;
    nadir::HConditionVariable condition_variable;
    nadir::HThread            thread;
};

struct NadirLock
{
    Bikeshed_ReadyCallback m_ReadyCallback;
    NadirLock()
        : m_ReadyCallback { signal }
        , m_Lock(nadir::CreateLock(malloc(nadir::GetNonReentrantLockSize())))
        , m_ConditionVariable(nadir::CreateConditionVariable(malloc(nadir::GetConditionVariableSize()), m_Lock))
    {
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
        NadirLock* _this = (NadirLock*)primitive;
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
};

struct TaskData
{
    TaskData()
        : done(0)
        , shed(0)
        , task_id((Bikeshed_TaskID)-1)
        , executed(0)
    {
    }
    static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, void* context_data)
    {
        TaskData* _this = (TaskData*)context_data;
        if (nadir::AtomicAdd32(&_this->executed, 1) != 1)
        {
            exit(-1);
        }
        _this->shed    = shed;
        _this->task_id = task_id;
        if (_this->done != 0)
        {
            nadir::AtomicAdd32(_this->done, 1);
        }
        return BIKESHED_TASK_RESULT_COMPLETE;
    }
    nadir::TAtomic32* done;
    Bikeshed          shed;
    Bikeshed_TaskID   task_id;
    nadir::TAtomic32  executed;
};

TEST(Bikeshed, WorkerThread)
{
    AssertAbort fatal;

    NadirLock sync_primitive;

    nadir::TAtomic32 stop = 0;
    TaskData         task;
    task.done = &stop;

    Bikeshed shed = Bikeshed_Create(malloc(Bikeshed_GetSize(1, 0)), 1, 0, &sync_primitive.m_ReadyCallback);
    ASSERT_NE((Bikeshed)0, shed);

    BikeShed_TaskFunc funcs[1]    = { TaskData::Compute };
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

    ASSERT_FALSE(Bikeshed_ExecuteOne(shed, 0));

    free(shed);
}

struct TaskData2
{
    TaskData2()
        : done(0)
        , shed(0)
        , task_id((Bikeshed_TaskID)-1)
    {
    }
    static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, void* context_data)
    {
        TaskData2* _this = (TaskData2*)context_data;
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

    Bikeshed shed = Bikeshed_Create(malloc(Bikeshed_GetSize(65535, 0)), 65535, 0, &sync_primitive.m_ReadyCallback);
    ASSERT_NE((Bikeshed)0, shed);

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

    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));

    free(shed);
}

TEST(Bikeshed, DependenciesThread)
{
    AssertAbort fatal;

    NadirLock sync_primitive;

    nadir::TAtomic32 stop = 0;

    TaskData tasks[5];
    tasks[0].done               = &stop;
    BikeShed_TaskFunc funcs[5] = {
        (BikeShed_TaskFunc)TaskData::Compute,
        (BikeShed_TaskFunc)TaskData::Compute,
        (BikeShed_TaskFunc)TaskData::Compute,
        (BikeShed_TaskFunc)TaskData::Compute,
        (BikeShed_TaskFunc)TaskData::Compute
    };
    void* contexts[5] = {
        &tasks[0],
        &tasks[1],
        &tasks[2],
        &tasks[3],
        &tasks[4]
    };
    Bikeshed_TaskID task_ids[5];

    Bikeshed shed = Bikeshed_Create(malloc(Bikeshed_GetSize(5, 5)), 5, 5, &sync_primitive.m_ReadyCallback);
    ASSERT_NE((Bikeshed)0, shed);

    ASSERT_TRUE(Bikeshed_CreateTasks(shed, 5, funcs, contexts, task_ids));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, task_ids[0], 3, &task_ids[1]));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, task_ids[3], 1, &task_ids[4]));
    ASSERT_TRUE(Bikeshed_AddDependencies(shed, task_ids[1], 1, &task_ids[4]));

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

    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));

    free(shed);
}

TEST(Bikeshed, DependenciesThreads)
{
    AssertAbort fatal;

    NadirLock sync_primitive;

    static const uint16_t LAYER_COUNT                   = 4;
    static const uint16_t LAYER_0_TASK_COUNT            = 1;
    static const uint16_t LAYER_1_TASK_COUNT            = 1024;
    static const uint16_t LAYER_2_TASK_COUNT            = 796;
    static const uint16_t LAYER_3_TASK_COUNT            = 640;
    static const uint16_t LAYER_TASK_COUNT[LAYER_COUNT] = { LAYER_0_TASK_COUNT, LAYER_1_TASK_COUNT, LAYER_2_TASK_COUNT, LAYER_3_TASK_COUNT };
    static const uint16_t TASK_COUNT                    = (uint16_t)(LAYER_0_TASK_COUNT + LAYER_1_TASK_COUNT + LAYER_2_TASK_COUNT + LAYER_3_TASK_COUNT);
    static const uint16_t DEPENDENCY_COUNT              = LAYER_1_TASK_COUNT + LAYER_2_TASK_COUNT + LAYER_3_TASK_COUNT;

    static const uint16_t LAYER_TASK_OFFSET[LAYER_COUNT] = {
        0,
        LAYER_0_TASK_COUNT,
        (uint16_t)(LAYER_0_TASK_COUNT + LAYER_1_TASK_COUNT),
        (uint16_t)(LAYER_0_TASK_COUNT + LAYER_1_TASK_COUNT + LAYER_2_TASK_COUNT)
    };

    nadir::TAtomic32 stop = 0;
    nadir::TAtomic32 done = 0;

    Bikeshed_TaskID task_ids[TASK_COUNT];
    TaskData          tasks[TASK_COUNT];
    tasks[0].done = &done;

    BikeShed_TaskFunc funcs[TASK_COUNT];
    void*              contexts[TASK_COUNT];
    for (uint16_t task_index = 0; task_index < TASK_COUNT; ++task_index)
    {
        funcs[task_index]    = TaskData::Compute;
        contexts[task_index] = &tasks[task_index];
    }

    Bikeshed shed = Bikeshed_Create(malloc(Bikeshed_GetSize(TASK_COUNT, DEPENDENCY_COUNT)), TASK_COUNT, DEPENDENCY_COUNT, &sync_primitive.m_ReadyCallback);
    ASSERT_NE((Bikeshed)0, shed);

    for (uint16_t layer_index = 0; layer_index < LAYER_COUNT; ++layer_index)
    {
        uint16_t task_offset = LAYER_TASK_OFFSET[layer_index];
        ASSERT_TRUE(Bikeshed_CreateTasks(shed, LAYER_TASK_COUNT[layer_index], &funcs[task_offset], &contexts[task_offset], &task_ids[task_offset]));
    }

    ASSERT_TRUE(Bikeshed_AddDependencies(shed, task_ids[0], LAYER_TASK_COUNT[1], &task_ids[LAYER_TASK_OFFSET[1]]));
    for (uint16_t i = 0; i < LAYER_TASK_COUNT[2]; ++i)
    {
        uint16_t parent_index = LAYER_TASK_OFFSET[1] + i;
        uint16_t child_index  = LAYER_TASK_OFFSET[2] + i;
        ASSERT_TRUE(Bikeshed_AddDependencies(shed, task_ids[parent_index], 1, &task_ids[child_index]));
    }
    for (uint16_t i = 0; i < LAYER_TASK_COUNT[3]; ++i)
    {
        uint16_t parent_index = LAYER_TASK_OFFSET[2] + i;
        uint16_t child_index  = LAYER_TASK_OFFSET[3] + i;
        ASSERT_TRUE(Bikeshed_AddDependencies(shed, task_ids[parent_index], 1, &task_ids[child_index]));
    }

    static const uint16_t WORKER_COUNT = 7;
    NodeWorker            workers[WORKER_COUNT];
    for (uint16_t worker_index = 0; worker_index < WORKER_COUNT; ++worker_index)
    {
        ASSERT_TRUE(workers[worker_index].CreateThread(shed, sync_primitive.m_ConditionVariable, &stop));
    }
    Bikeshed_ReadyTasks(shed, LAYER_TASK_COUNT[3], &task_ids[LAYER_TASK_OFFSET[3]]);
    Bikeshed_ReadyTasks(shed, LAYER_TASK_COUNT[2] - LAYER_TASK_COUNT[3], &task_ids[LAYER_TASK_OFFSET[2] + LAYER_TASK_COUNT[3]]);
    Bikeshed_ReadyTasks(shed, LAYER_TASK_COUNT[1] - LAYER_TASK_COUNT[2], &task_ids[LAYER_TASK_OFFSET[1] + LAYER_TASK_COUNT[2]]);

    Bikeshed_TaskID next_ready_task = 0;
    while (!done)
    {
        if (next_ready_task != 0)
        {
            Bikeshed_ExecuteAndResolve(shed, next_ready_task, &next_ready_task);
            continue;
        }
        if (!Bikeshed_ExecuteOne(shed, &next_ready_task))
        {
            // We can't wait for the signal here since it only signals if there is work to be done
            // Ie, if another thread executes the last work item that sets done to true we will
            // not get a signal to wake up since no new work will be set to ready.
            // So we just go like crazy until top level task sets the 'done' flag
            nadir::Sleep(1000);
        }
    }
    nadir::AtomicAdd32(&stop, WORKER_COUNT);
    nadir::WakeAll(sync_primitive.m_ConditionVariable);

    for (uint16_t worker_index = 0; worker_index < WORKER_COUNT; ++worker_index)
    {
        while (!nadir::JoinThread(workers[worker_index].thread, 1000))
        {
            // Need to look into logic for breaking workers, right now we can get in a state where
            // a thread is not woken up
            nadir::WakeAll(sync_primitive.m_ConditionVariable);
        }
    }

    for (uint16_t worker_index = 0; worker_index < WORKER_COUNT; ++worker_index)
    {
        workers[worker_index].DisposeThread();
    }

    for (uint32_t i = 0; i < TASK_COUNT; ++i)
    {
        ASSERT_EQ(shed, tasks[i].shed);
        ASSERT_EQ(task_ids[i], tasks[i].task_id);
        ASSERT_EQ(1, tasks[i].executed);
    }

    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));

    free(shed);
}

struct MotherData
{
    MotherData()
        : shed(0)
        , task_id((Bikeshed_TaskID)-1)
        , executed(0)
        , sub_task_spawned(0)
		, sub_task_executed(0)
    { }

    struct ChildData
    {
        ChildData()
            : mother_data(0)
        { }

        static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID , void* context_data)
        {
            ChildData* _this = (ChildData*)context_data;
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

    static Bikeshed_TaskResult Compute(Bikeshed shed, Bikeshed_TaskID task_id, void* context_data)
    {
        MotherData* _this = (MotherData*)context_data;

        _this->shed    = shed;
        _this->task_id = task_id;

        if (_this->sub_task_executed == 0)
        {
            _this->funcs[0]    = (BikeShed_TaskFunc)ChildData::Compute;
            _this->funcs[1]    = (BikeShed_TaskFunc)ChildData::Compute;
            _this->funcs[2]    = (BikeShed_TaskFunc)ChildData::Compute;
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
            _this->funcs[3]    = (BikeShed_TaskFunc)ChildData::Compute;
            _this->funcs[4]    = (BikeShed_TaskFunc)ChildData::Compute;
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
    Bikeshed   shed;
    Bikeshed_TaskID task_id;
    nadir::TAtomic32  executed;
    uint32_t          sub_task_spawned;
    uint32_t          sub_task_executed;

    ChildData          sub_tasks[5];
    BikeShed_TaskFunc funcs[5];
    void*              contexts[5];
    Bikeshed_TaskID  task_ids[5];
};

TEST(Bikeshed, InExecutionSpawnTasks)
{
    AssertAbort fatal;

    NadirLock sync_primitive;

    nadir::TAtomic32 stop = 0;

    Bikeshed_TaskID  mother_task_id;
    MotherData         mother_task;
    void*              mother_context = &mother_task;
    BikeShed_TaskFunc mother_func[1] = { (BikeShed_TaskFunc)MotherData::Compute };

    Bikeshed shed = Bikeshed_Create(malloc(Bikeshed_GetSize(4, 3)), 4, 3, &sync_primitive.m_ReadyCallback);
    ASSERT_NE((Bikeshed)0, shed);

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

    ASSERT_TRUE(!Bikeshed_ExecuteOne(shed, 0));

    ASSERT_EQ(0, stop);

    free(shed);
}


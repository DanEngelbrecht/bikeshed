#include "../src/bikeshed.h"

#include "../third-party/nadir/src/nadir.h"

#include <memory>

#define ALIGN_SIZE(x, align)    (((x) + ((align) - 1)) & ~((align) - 1))

typedef struct SCtx
{
} SCtx;

static SCtx* main_setup()
{
    return reinterpret_cast<SCtx*>( malloc( sizeof(SCtx) ) );
}

static void main_teardown(SCtx* ctx)
{
    free(ctx);
}

static void test_setup(SCtx* )
{
}

static void test_teardown(SCtx* )
{
}

static void create(SCtx* )
{
    uint32_t size = bikeshed::GetShedSize(16, 32);
    void* p = malloc(size);
    bikeshed::HShed shed = bikeshed::CreateShed(p, 16, 32, 0);
    ASSERT_NE(0, shed);
    free(shed);
}

static uint32_t gAssertCount = 0;

static void Assert(const char* , int )
{
    ++gAssertCount;
}

static void test_assert(SCtx* )
{
    bikeshed::SetAssert(Assert);
    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(1, 1)), 1, 1, 0);
    ASSERT_NE(0, shed);
#if defined(BIKESHED_ASSERTS)
    bikeshed::TTaskID invalid_task_id = 1;
    bikeshed::ReadyTasks(shed, 1, &invalid_task_id);
    ASSERT_EQ(1, gAssertCount);
#endif
    free(shed);
    bikeshed::SetAssert(0);
}

struct AssertAbort
{
    AssertAbort()
    {
        bikeshed::SetAssert(AssertAbort::Assert);
    }
    ~AssertAbort()
    {
        bikeshed::SetAssert(0);
    }
    static void Assert(const char* file, int line)
    {
        printf("Assert at %s(%d)", file, line);
        ASSERT_TRUE(false);
    }
};

static void single_task(SCtx* )
{
    AssertAbort fatal;

    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(1, 1)), 1, 1, 0);
    ASSERT_NE(0, shed);

    struct TaskData {
        TaskData()
            : shed(0)
            , task_id((bikeshed::TTaskID)-1)
            , executed(0)
        { }
        static bikeshed::TaskResult Compute(bikeshed::HShed shed, bikeshed::TTaskID task_id, TaskData* task_data)
        {
            task_data->shed = shed;
            ++task_data->executed;
            task_data->task_id = task_id;
            return bikeshed::TASK_RESULT_COMPLETE;
        }
        bikeshed::HShed shed;
        bikeshed::TTaskID task_id;
        uint32_t executed;
    } task;

    ASSERT_TRUE(!bikeshed::ExecuteOneTask(shed, 0, 0));

    bikeshed::TaskFunc funcs[1] = {(bikeshed::TaskFunc)TaskData::Compute};
    void* contexts[1] = {&task};

    bikeshed::TTaskID task_id;
    ASSERT_TRUE(bikeshed::CreateTasks(shed, 1, funcs, contexts, &task_id));

    ASSERT_TRUE(!bikeshed::CreateTasks(shed, 1, funcs, contexts, &task_id));

    bikeshed::ReadyTasks(shed, 1, &task_id);

    bikeshed::TTaskID executed_task;
    ASSERT_TRUE(bikeshed::ExecuteOneTask(shed, 0, &executed_task));
    ASSERT_EQ(task_id, executed_task);

    ASSERT_EQ(shed, task.shed);
    ASSERT_EQ(task.task_id, executed_task);
    ASSERT_EQ(1, task.executed);

    ASSERT_TRUE(!bikeshed::ExecuteOneTask(shed, 0, 0));

    free(shed);
}

static void test_sync(SCtx* )
{
    AssertAbort fatal;

    struct FakeLock
    {
        bikeshed::SyncPrimitive m_SyncPrimitive;
        FakeLock()
            : m_SyncPrimitive{lock, unlock, signal}
            , lock_count(0)
            , unlock_count(0)
            , ready_count(0)
        {

        }
        static void lock(bikeshed::SyncPrimitive* primitive){
            ((FakeLock*)primitive)->lock_count++;
        }
        static void unlock(bikeshed::SyncPrimitive* primitive){
            ((FakeLock*)primitive)->unlock_count++;
        }
        static void signal(bikeshed::SyncPrimitive* primitive, uint16_t ready_count){
            ((FakeLock*)primitive)->ready_count += ready_count;
        }
        uint32_t lock_count;
        uint32_t unlock_count;
        uint32_t ready_count;
    }lock;
    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(1, 1)), 1, 1, &lock.m_SyncPrimitive);
    ASSERT_NE(0, shed);

    struct TaskData {
        TaskData()
            : shed(0)
            , task_id((bikeshed::TTaskID)-1)
            , executed(0)
        { }
        static bikeshed::TaskResult Compute(bikeshed::HShed shed, bikeshed::TTaskID task_id, TaskData* task_data)
        {
            task_data->shed = shed;
            ++task_data->executed;
            task_data->task_id = task_id;
            return bikeshed::TASK_RESULT_COMPLETE;
        }
        bikeshed::HShed shed;
        bikeshed::TTaskID task_id;
        uint32_t executed;
    } task;

    bikeshed::TaskFunc funcs[1] = {(bikeshed::TaskFunc)TaskData::Compute};
    void* contexts[1] = {&task};

    bikeshed::TTaskID task_id;
    ASSERT_TRUE(bikeshed::CreateTasks(shed, 1, funcs, contexts, &task_id));

    ASSERT_TRUE(!bikeshed::CreateTasks(shed, 1, funcs, contexts, &task_id));

    bikeshed::ReadyTasks(shed, 1, &task_id);
    ASSERT_EQ(1, lock.ready_count);

    bikeshed::TTaskID executed_task;
    ASSERT_TRUE(bikeshed::ExecuteOneTask(shed, 0, &executed_task));
    ASSERT_EQ(task_id, executed_task);

    ASSERT_EQ(shed, task.shed);
    ASSERT_EQ(task.task_id, task_id);
    ASSERT_EQ(1, task.executed);

    ASSERT_TRUE(!bikeshed::ExecuteOneTask(shed, 0, 0));

    ASSERT_EQ(6, lock.lock_count);
    ASSERT_EQ(6, lock.unlock_count);

    free(shed);
}

static void test_ready_order(SCtx* )
{
    AssertAbort fatal;

    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(5, 5)), 5, 5, 0);
    ASSERT_NE(0, shed);

    struct TaskData {
        TaskData()
            : shed(0)
            , task_id((bikeshed::TTaskID)-1)
            , executed(0)
        { }
        static bikeshed::TaskResult Compute(bikeshed::HShed shed, bikeshed::TTaskID task_id, TaskData* task_data)
        {
            task_data->shed = shed;
            ++task_data->executed;
            task_data->task_id = task_id;
            return bikeshed::TASK_RESULT_COMPLETE;
        }
        bikeshed::HShed shed;
        bikeshed::TTaskID task_id;
        uint32_t executed;
    };
    
    TaskData tasks[5];
    bikeshed::TaskFunc funcs[5] = {
        (bikeshed::TaskFunc)TaskData::Compute,
        (bikeshed::TaskFunc)TaskData::Compute,
        (bikeshed::TaskFunc)TaskData::Compute,
        (bikeshed::TaskFunc)TaskData::Compute,
        (bikeshed::TaskFunc)TaskData::Compute};
    void* contexts[5] = {
        &tasks[0],
        &tasks[1],
        &tasks[2],
        &tasks[3],
        &tasks[4]};
    bikeshed::TTaskID task_ids[5];

    ASSERT_TRUE(bikeshed::CreateTasks(shed, 5, funcs, contexts, task_ids));
    bikeshed::ReadyTasks(shed, 2, &task_ids[0]);
    bikeshed::ReadyTasks(shed, 1, &task_ids[2]);
    bikeshed::ReadyTasks(shed, 2, &task_ids[3]);

    for (uint32_t i = 0; i < 5; ++i)
    {
        bikeshed::TTaskID executed_task;
        ASSERT_TRUE(bikeshed::ExecuteOneTask(shed, 0, &executed_task));
        ASSERT_EQ(task_ids[i], executed_task);
        ASSERT_EQ(shed, tasks[i].shed);
        ASSERT_EQ(1, tasks[i].executed);
    }
    ASSERT_TRUE(!bikeshed::ExecuteOneTask(shed, 0, 0));
    free(shed);
}

static void test_dependency(SCtx* )
{
    AssertAbort fatal;

    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(5, 5)), 5, 5, 0);
    ASSERT_NE(0, shed);

    struct TaskData {
        TaskData()
            : shed(0)
            , task_id((bikeshed::TTaskID)-1)
            , executed(0)
        { }
        static bikeshed::TaskResult Compute(bikeshed::HShed shed, bikeshed::TTaskID task_id, TaskData* task_data)
        {
            task_data->shed = shed;
            ++task_data->executed;
            task_data->task_id = task_id;
            return bikeshed::TASK_RESULT_COMPLETE;
        }
        bikeshed::HShed shed;
        bikeshed::TTaskID task_id;
        uint32_t executed;
    };
    
    TaskData tasks[5];
    bikeshed::TaskFunc funcs[5] = {
        (bikeshed::TaskFunc)TaskData::Compute,
        (bikeshed::TaskFunc)TaskData::Compute,
        (bikeshed::TaskFunc)TaskData::Compute,
        (bikeshed::TaskFunc)TaskData::Compute,
        (bikeshed::TaskFunc)TaskData::Compute};
    void* contexts[5] = {
        &tasks[0],
        &tasks[1],
        &tasks[2],
        &tasks[3],
        &tasks[4]};
    bikeshed::TTaskID task_ids[5];

    ASSERT_TRUE(bikeshed::CreateTasks(shed, 5, funcs, contexts, task_ids));
    ASSERT_TRUE(bikeshed::AddTaskDependencies(shed, task_ids[0], 3, &task_ids[1]));
    ASSERT_TRUE(bikeshed::AddTaskDependencies(shed, task_ids[3], 1, &task_ids[4]));
    ASSERT_TRUE(bikeshed::AddTaskDependencies(shed, task_ids[1], 1, &task_ids[4]));
    bikeshed::ReadyTasks(shed, 1, &task_ids[2]);
    bikeshed::ReadyTasks(shed, 1, &task_ids[4]);

    struct ResolvedCallback
    {
        bikeshed::ResolvedCallback m_ResolvedCallback;
        uint32_t resolve_count;
        ResolvedCallback()
            : m_ResolvedCallback{ConsumeTask}
            , resolve_count(0)
        { }
        static bool ConsumeTask(bikeshed::ResolvedCallback* callback, bikeshed::TTaskID )
        {
            ResolvedCallback* resolved_callback = (ResolvedCallback*)callback;
            ++resolved_callback->resolve_count;
            return false;
        }
    } resolved_callback;

    auto execute_one = [&](bikeshed::TTaskID expected_ready)
    {
        resolved_callback.resolve_count = 0;
        bikeshed::TTaskID executed_task;
        bool executed =  ExecuteOneTask(shed, &resolved_callback.m_ResolvedCallback, &executed_task);
        if (!executed)
        {
            return false;
        }
        if (executed_task != expected_ready)
        {
            return false;
        }
        return true;
    };

    ASSERT_TRUE(execute_one(task_ids[2]));
    ASSERT_EQ(0, resolved_callback.resolve_count);
    ASSERT_TRUE(execute_one(task_ids[4]));
    ASSERT_EQ(2, resolved_callback.resolve_count);
    ASSERT_TRUE(execute_one(task_ids[1]));
    ASSERT_EQ(0, resolved_callback.resolve_count);
    ASSERT_TRUE(execute_one(task_ids[3]));
    ASSERT_EQ(1, resolved_callback.resolve_count);
    ASSERT_TRUE(execute_one(task_ids[0]));
    ASSERT_EQ(0, resolved_callback.resolve_count);

    for (uint32_t i = 0; i < 5; ++i)
    {
        ASSERT_EQ(shed, tasks[i].shed);
        ASSERT_EQ(task_ids[i], tasks[i].task_id);
        ASSERT_EQ(1, tasks[i].executed);
    }

    ASSERT_TRUE(!bikeshed::ExecuteOneTask(shed, 0, 0));
    free(shed);
}

struct NodeWorker
{
    NodeWorker()
        : stop(0)
        , shed(0)
        , condition_variable(0)
        , thread(0)
    {}

    ~NodeWorker()
    {
    }

    bool CreateThread(bikeshed::HShed in_shed, nadir::HConditionVariable in_condition_variable, nadir::TAtomic32* in_stop)
    {
        shed = in_shed;
        stop = in_stop;
        condition_variable = in_condition_variable;
        thread = nadir::CreateThread(malloc(nadir::GetThreadSize()), NodeWorker::Execute, 0, this);
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

        struct ResolvedCallback
        {
            bikeshed::ResolvedCallback m_ResolvedCallback;
            bikeshed::TTaskID m_ResolvedTask;
            ResolvedCallback()
                : m_ResolvedCallback{ConsumeTask}
                , m_ResolvedTask(0)
            { }
            static bool ConsumeTask(bikeshed::ResolvedCallback* callback, bikeshed::TTaskID task_id)
            {
                ResolvedCallback* resolved_callback = (ResolvedCallback*)callback;
                if (resolved_callback->m_ResolvedTask == 0)
                {
                    resolved_callback->m_ResolvedTask = task_id;
                    return true;                    
                }
                return false;
            }
        } resolved_callback;

        while(*_this->stop == 0)
        {
            if (resolved_callback.m_ResolvedTask != 0)
            {
                bikeshed::TTaskID task_id = resolved_callback.m_ResolvedTask;
                resolved_callback.m_ResolvedTask = 0;
                bikeshed::ExecuteAndResolveTask(_this->shed, task_id, &resolved_callback.m_ResolvedCallback);
                continue;
            }
            if (!bikeshed::ExecuteOneTask(_this->shed, &resolved_callback.m_ResolvedCallback, 0))
            {
                nadir::SleepConditionVariable(_this->condition_variable, nadir::TIMEOUT_INFINITE);
            }
        }
        return 0;
    }

    nadir::TAtomic32* stop;
    bikeshed::HShed shed;
    nadir::HConditionVariable condition_variable;
    nadir::HThread thread;
};

struct NadirLock
{
    bikeshed::SyncPrimitive m_SyncPrimitive;
    NadirLock()
        : m_SyncPrimitive{lock, unlock, signal}
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
    static void lock(bikeshed::SyncPrimitive* primitive){
        NadirLock* _this = (NadirLock*)primitive;
        nadir::LockNonReentrantLock(_this->m_Lock);
    }
    static void unlock(bikeshed::SyncPrimitive* primitive){
        NadirLock* _this = (NadirLock*)primitive;
        nadir::UnlockNonReentrantLock(_this->m_Lock);
    }
    static void signal(bikeshed::SyncPrimitive* primitive, uint16_t ready_count){
        NadirLock* _this = (NadirLock*)primitive;
        if (ready_count > 1)
        {
            nadir::LockNonReentrantLock(_this->m_Lock);
            nadir::WakeAll(_this->m_ConditionVariable);
            nadir::UnlockNonReentrantLock(_this->m_Lock);
        }
        else if (ready_count > 0)
        {
            nadir::WakeOne(_this->m_ConditionVariable);
        }
    }
    nadir::HNonReentrantLock m_Lock;
    nadir::HConditionVariable m_ConditionVariable;
};

struct TaskData {
    TaskData()
        : done(0)
        , shed(0)
        , task_id((bikeshed::TTaskID)-1)
        , executed(0)
    { }
    static bikeshed::TaskResult Compute(bikeshed::HShed shed, bikeshed::TTaskID task_id, void* context_data)
    {
        TaskData* _this = (TaskData*)context_data;
        if (nadir::AtomicAdd32(&_this->executed, 1) != 1)
        {
            exit(-1);
        }
        _this->shed = shed;
        _this->task_id = task_id;
        if (_this->done != 0)
        {
            nadir::AtomicAdd32(_this->done, 1);
        }
        return bikeshed::TASK_RESULT_COMPLETE;
    }
    nadir::TAtomic32* done;
    bikeshed::HShed shed;
    bikeshed::TTaskID task_id;
    nadir::TAtomic32 executed;
};

static void test_worker_thread(SCtx* )
{
    AssertAbort fatal;

    NadirLock sync_primitive;

    nadir::TAtomic32 stop = 0;
    TaskData task;
    task.done = &stop;

    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(1, 1)), 1, 1, &sync_primitive.m_SyncPrimitive);
    ASSERT_NE(0, shed);

    bikeshed::TaskFunc funcs[1] = {TaskData::Compute};
    void* contexts[1] = {&task};

    NodeWorker thread_context;
    thread_context.CreateThread(shed, sync_primitive.m_ConditionVariable, &stop);

    bikeshed::TTaskID task_id;
    ASSERT_TRUE(bikeshed::CreateTasks(shed, 1, funcs, contexts, &task_id));
    bikeshed::ReadyTasks(shed, 1, &task_id);

    nadir::JoinThread(thread_context.thread, nadir::TIMEOUT_INFINITE);
    thread_context.DisposeThread();

    ASSERT_EQ(shed, task.shed);
    ASSERT_EQ(task_id, task.task_id);
    ASSERT_EQ(1, task.executed);

    ASSERT_TRUE(!bikeshed::ExecuteOneTask(shed, 0, 0));

    free(shed);
}

static void test_dependencies_thread(SCtx* )
{
    AssertAbort fatal;

    NadirLock sync_primitive;

    nadir::TAtomic32 stop = 0;

    TaskData tasks[5];
    tasks[0].done = &stop;
    bikeshed::TaskFunc funcs[5] = {
        (bikeshed::TaskFunc)TaskData::Compute,
        (bikeshed::TaskFunc)TaskData::Compute,
        (bikeshed::TaskFunc)TaskData::Compute,
        (bikeshed::TaskFunc)TaskData::Compute,
        (bikeshed::TaskFunc)TaskData::Compute};
    void* contexts[5] = {
        &tasks[0],
        &tasks[1],
        &tasks[2],
        &tasks[3],
        &tasks[4]};
    bikeshed::TTaskID task_ids[5];

    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(5, 5)), 5, 5, &sync_primitive.m_SyncPrimitive);
    ASSERT_NE(0, shed);

    ASSERT_TRUE(bikeshed::CreateTasks(shed, 5, funcs, contexts, task_ids));
    ASSERT_TRUE(bikeshed::AddTaskDependencies(shed, task_ids[0], 3, &task_ids[1]));
    ASSERT_TRUE(bikeshed::AddTaskDependencies(shed, task_ids[3], 1, &task_ids[4]));
    ASSERT_TRUE(bikeshed::AddTaskDependencies(shed, task_ids[1], 1, &task_ids[4]));

    NodeWorker thread_context;
    thread_context.CreateThread(shed, sync_primitive.m_ConditionVariable, &stop);
    bikeshed::ReadyTasks(shed, 1, &task_ids[2]);
	bikeshed::ReadyTasks(shed, 1, &task_ids[4]);

    nadir::JoinThread(thread_context.thread, nadir::TIMEOUT_INFINITE);
    thread_context.DisposeThread();

    for (uint32_t i = 0; i < 5; ++i)
    {
        ASSERT_EQ(shed, tasks[i].shed);
        ASSERT_EQ(task_ids[i], tasks[i].task_id);
        ASSERT_EQ(1, tasks[i].executed);
    }

    ASSERT_TRUE(!bikeshed::ExecuteOneTask(shed, 0, 0));

    free(shed);
}

static void test_dependencies_threads(SCtx* )
{
    AssertAbort fatal;

    NadirLock sync_primitive;

    static const uint16_t LAYER_COUNT = 4;
    static const uint16_t LAYER_0_TASK_COUNT = 1;
    static const uint16_t LAYER_1_TASK_COUNT = 1024;
    static const uint16_t LAYER_2_TASK_COUNT = 796;
    static const uint16_t LAYER_3_TASK_COUNT = 640;
    static const uint16_t LAYER_TASK_COUNT[LAYER_COUNT] = {LAYER_0_TASK_COUNT, LAYER_1_TASK_COUNT, LAYER_2_TASK_COUNT, LAYER_3_TASK_COUNT};
    static const uint16_t TASK_COUNT = (uint16_t)(LAYER_0_TASK_COUNT + LAYER_1_TASK_COUNT + LAYER_2_TASK_COUNT + LAYER_3_TASK_COUNT);
    static const uint16_t DEPENDENCY_COUNT = LAYER_1_TASK_COUNT + LAYER_2_TASK_COUNT + LAYER_3_TASK_COUNT;

    static const uint16_t LAYER_TASK_OFFSET[LAYER_COUNT] = {
        0,
        LAYER_0_TASK_COUNT,
        (uint16_t)(LAYER_0_TASK_COUNT + LAYER_1_TASK_COUNT),
        (uint16_t)(LAYER_0_TASK_COUNT + LAYER_1_TASK_COUNT + LAYER_2_TASK_COUNT)
        };

    nadir::TAtomic32 stop = 0;
    nadir::TAtomic32 done = 0;

    bikeshed::TTaskID task_ids[TASK_COUNT];
    TaskData tasks[TASK_COUNT];
    tasks[0].done = &done;

    bikeshed::TaskFunc funcs[TASK_COUNT];
    void* contexts[TASK_COUNT];
    for (uint16_t task_index = 0; task_index < TASK_COUNT; ++task_index)
    {
        funcs[task_index] = TaskData::Compute;
        contexts[task_index] = &tasks[task_index];
    }

    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(TASK_COUNT, DEPENDENCY_COUNT)), TASK_COUNT, DEPENDENCY_COUNT, &sync_primitive.m_SyncPrimitive);
    ASSERT_NE(0, shed);

    for (uint16_t layer_index = 0; layer_index < LAYER_COUNT; ++layer_index)
    {
        uint16_t task_offset = LAYER_TASK_OFFSET[layer_index];
        ASSERT_TRUE(bikeshed::CreateTasks(shed, LAYER_TASK_COUNT[layer_index], &funcs[task_offset], &contexts[task_offset], &task_ids[task_offset]));
    }

    ASSERT_TRUE(bikeshed::AddTaskDependencies(shed, task_ids[0], LAYER_TASK_COUNT[1], &task_ids[LAYER_TASK_OFFSET[1]]));
    for (uint16_t i = 0; i < LAYER_TASK_COUNT[2]; ++i)
    {
        uint16_t parent_index = LAYER_TASK_OFFSET[1] + i;
        uint16_t child_index = LAYER_TASK_OFFSET[2] + i;
        ASSERT_TRUE(bikeshed::AddTaskDependencies(shed, task_ids[parent_index], 1, &task_ids[child_index]));
    }
    for (uint16_t i = 0; i < LAYER_TASK_COUNT[3]; ++i)
    {
        uint16_t parent_index = LAYER_TASK_OFFSET[2] + i;
        uint16_t child_index = LAYER_TASK_OFFSET[3] + i;
        ASSERT_TRUE(bikeshed::AddTaskDependencies(shed, task_ids[parent_index], 1, &task_ids[child_index]));
    }

    static const uint16_t WORKER_COUNT = 7;
    NodeWorker workers[WORKER_COUNT];
    for (uint16_t worker_index = 0; worker_index < WORKER_COUNT; ++worker_index)
    {
        ASSERT_TRUE(workers[worker_index].CreateThread(shed, sync_primitive.m_ConditionVariable, &stop));
    }
    bikeshed::ReadyTasks(shed, LAYER_TASK_COUNT[3], &task_ids[LAYER_TASK_OFFSET[3]]);
    bikeshed::ReadyTasks(shed, LAYER_TASK_COUNT[2] - LAYER_TASK_COUNT[3], &task_ids[LAYER_TASK_OFFSET[2] + LAYER_TASK_COUNT[3]]);
    bikeshed::ReadyTasks(shed, LAYER_TASK_COUNT[1] - LAYER_TASK_COUNT[2], &task_ids[LAYER_TASK_OFFSET[1] + LAYER_TASK_COUNT[2]]);

    while(!done)
    {
        bikeshed::ExecuteOneTask(shed, 0, 0);
        // We can't wait for the signal here since it only signals if there is work to be done
        // Ie, if another thread executes the last work item that sets done to true we will
        // not get a signal to wake up since no new work will be set to ready.
        // So we just go like crazy until top level task sets the 'done' flag
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

    ASSERT_TRUE(!bikeshed::ExecuteOneTask(shed, 0, 0));

    free(shed);
}

TEST_BEGIN(test, main_setup, main_teardown, test_setup, test_teardown)
    TEST(create)
    TEST(test_assert)
    TEST(single_task)
    TEST(test_sync)
    TEST(test_ready_order)
    TEST(test_dependency)
    TEST(test_worker_thread)
    TEST(test_dependencies_thread)
    TEST(test_dependencies_threads)
TEST_END(test)

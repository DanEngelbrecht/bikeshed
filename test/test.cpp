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
    uint32_t size = bikeshed::GetShedSize(16);
    void* p = malloc(size);
    bikeshed::HShed shed = bikeshed::CreateShed(p, 16, 0);
    ASSERT_NE(0, shed);
    free(shed);
}

static void single_task(SCtx* )
{
    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(1)), 1, 0);
    ASSERT_NE(0, shed);

    struct TaskData {
        TaskData()
            : task_id((bikeshed::TTaskID)-1)
            , shed(0)
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

    ASSERT_TRUE(bikeshed::ReadyTasks(shed, 1, &task_id));

    bikeshed::TTaskID ready_task;
    ASSERT_TRUE(bikeshed::GetFirstReadyTask(shed, &ready_task));
    ASSERT_EQ(task_id, ready_task);
    bikeshed::TaskResult result = bikeshed::ExecuteTask(shed, ready_task);
    ASSERT_EQ(bikeshed::TASK_RESULT_COMPLETE, result);

    ASSERT_EQ(shed, task.shed);
    ASSERT_EQ(task.task_id, ready_task);
    ASSERT_EQ(1, task.executed);

    bikeshed::FreeTasks(shed, 1, &ready_task);
    ASSERT_TRUE(!bikeshed::GetFirstReadyTask(shed, &ready_task));

    free(shed);
}

static void test_sync(SCtx* )
{
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
        static bool lock(bikeshed::SyncPrimitive* primitive){
            ((FakeLock*)primitive)->lock_count++;
            return true;
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
    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(1)), 1, &lock.m_SyncPrimitive);
    ASSERT_NE(0, shed);

    struct TaskData {
        TaskData()
            : task_id((bikeshed::TTaskID)-1)
            , shed(0)
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

    ASSERT_TRUE(bikeshed::ReadyTasks(shed, 1, &task_id));
    ASSERT_EQ(1, lock.ready_count);

    bikeshed::TTaskID ready_task;
    ASSERT_TRUE(bikeshed::GetFirstReadyTask(shed, &ready_task));
    ASSERT_EQ(task_id, ready_task);
    bikeshed::TaskResult result = bikeshed::ExecuteTask(shed, ready_task);
    ASSERT_EQ(bikeshed::TASK_RESULT_COMPLETE, result);

    ASSERT_EQ(shed, task.shed);
    ASSERT_EQ(task.task_id, ready_task);
    ASSERT_EQ(1, task.executed);

    bikeshed::FreeTasks(shed, 1, &ready_task);
    ASSERT_TRUE(!bikeshed::GetFirstReadyTask(shed, &ready_task));

    ASSERT_EQ(6, lock.lock_count);
    ASSERT_EQ(6, lock.unlock_count);

    free(shed);
}

static void test_ready_order(SCtx* )
{
    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(5)), 5, 0);
    ASSERT_NE(0, shed);

    struct TaskData {
        TaskData()
            : task_id((bikeshed::TTaskID)-1)
            , shed(0)
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
    ASSERT_TRUE(bikeshed::ReadyTasks(shed, 2, &task_ids[0]));
    ASSERT_TRUE(bikeshed::ReadyTasks(shed, 1, &task_ids[2]));
    ASSERT_TRUE(bikeshed::ReadyTasks(shed, 2, &task_ids[3]));

    for (uint32_t i = 0; i < 5; ++i)
    {
        bikeshed::TTaskID ready_task;
        ASSERT_TRUE(bikeshed::GetFirstReadyTask(shed, &ready_task));
        ASSERT_EQ(task_ids[i], ready_task);
        bikeshed::TaskResult result = bikeshed::ExecuteTask(shed, ready_task);
        ASSERT_EQ(bikeshed::TASK_RESULT_COMPLETE, result);
        bikeshed::FreeTasks(shed, 1, &ready_task);

        ASSERT_EQ(shed, tasks[i].shed);
        ASSERT_EQ(tasks[i].task_id, ready_task);
        ASSERT_EQ(1, tasks[i].executed);
    }
    bikeshed::TTaskID ready_task;
    ASSERT_TRUE(!bikeshed::GetFirstReadyTask(shed, &ready_task));
    free(shed);
}

static void test_dependency(SCtx* )
{
    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(5)), 5, 0);
    ASSERT_NE(0, shed);

    struct TaskData {
        TaskData()
            : task_id((bikeshed::TTaskID)-1)
            , shed(0)
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
    ASSERT_TRUE(bikeshed::ReadyTasks(shed, 1, &task_ids[2]));
    ASSERT_TRUE(bikeshed::ReadyTasks(shed, 1, &task_ids[4]));

    bikeshed::TTaskID resolved_task_ids[5];
    uint16_t resolved_task_count;

    auto execute_one = [&](bikeshed::TTaskID expected_ready)
    {
        bikeshed::TTaskID ready_task;
        ASSERT_TRUE(bikeshed::GetFirstReadyTask(shed, &ready_task));
        ASSERT_EQ(expected_ready, ready_task);
        bikeshed::TaskResult result = bikeshed::ExecuteTask(shed, ready_task);
        ASSERT_EQ(bikeshed::TASK_RESULT_COMPLETE, result);
        bikeshed::ResolveTask(shed, ready_task, &resolved_task_count, resolved_task_ids);
        bikeshed::FreeTasks(shed, 1, &ready_task);
        bikeshed::ReadyTasks(shed, resolved_task_count, resolved_task_ids);
    };

    execute_one(task_ids[2]);
    ASSERT_EQ(0, resolved_task_count);
    execute_one(task_ids[4]);
    ASSERT_EQ(2, resolved_task_count);
    ASSERT_EQ(task_ids[1], resolved_task_ids[0]);
    ASSERT_EQ(task_ids[3], resolved_task_ids[1]);
    execute_one(task_ids[1]);
    ASSERT_EQ(0, resolved_task_count);
    execute_one(task_ids[3]);
    ASSERT_EQ(1, resolved_task_count);
    ASSERT_EQ(task_ids[0], resolved_task_ids[0]);
    execute_one(task_ids[0]);

    for (uint32_t i = 0; i < 5; ++i)
    {
        ASSERT_EQ(shed, tasks[i].shed);
        ASSERT_EQ(task_ids[i], tasks[i].task_id);
        ASSERT_EQ(1, tasks[i].executed);
    }

    bikeshed::TTaskID ready_task;
    ASSERT_TRUE(!bikeshed::GetFirstReadyTask(shed, &ready_task));
    free(shed);
}

static void test_worker_thread(SCtx* )
{
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
        static bool lock(bikeshed::SyncPrimitive* primitive){
            NadirLock* _this = (NadirLock*)primitive;
            nadir::LockNonReentrantLock(_this->m_Lock);
            return true;
        }
        static void unlock(bikeshed::SyncPrimitive* primitive){
            NadirLock* _this = (NadirLock*)primitive;
            nadir::UnlockNonReentrantLock(_this->m_Lock);
        }
        static void signal(bikeshed::SyncPrimitive* primitive, uint16_t ready_count){
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
        nadir::HNonReentrantLock m_Lock;
        nadir::HConditionVariable m_ConditionVariable;
    }sync_primitive;

    struct ThreadContext
    {
        ThreadContext()
            : shed(0)
            , stop(0)
            , condition_variable(0)
            , thread(0)
        {}

        ~ThreadContext()
        {
        }

        bool CreateThread(bikeshed::HShed in_shed, nadir::HConditionVariable in_condition_variable, nadir::TAtomic32* in_stop)
        {
            shed = in_shed;
            stop = in_stop;
            condition_variable = in_condition_variable;
            thread = nadir::CreateThread(malloc(nadir::GetThreadSize()), ThreadContext::Execute, 0, this);
            return thread != 0;
        }

        void DisposeThread()
        {
            nadir::DeleteThread(thread);
            free(thread);
        }

        static int32_t Execute(void* context)
        {
            ThreadContext* _this = (ThreadContext*)context;
            while(true)
            {
				if (nadir::AtomicAdd32(_this->stop, -1) >= 0)
				{
					break;
				}
				nadir::AtomicAdd32(_this->stop, 1);

				bikeshed::TTaskID task_id;
				if (bikeshed::GetFirstReadyTask(_this->shed, &task_id))
				{
					bikeshed::TaskResult result = bikeshed::ExecuteTask(_this->shed, task_id);
					switch (result)
					{
					case bikeshed::TASK_RESULT_COMPLETE:
						{
							uint16_t resolved_task_count;
							bikeshed::TTaskID resolved_task_ids[8];
							bikeshed::ResolveTask(_this->shed, task_id, &resolved_task_count, resolved_task_ids);
							bikeshed::ReadyTasks(_this->shed, resolved_task_count, resolved_task_ids);
						}
						break;
					case bikeshed::TASK_RESULT_BLOCKED:
						break;
					case bikeshed::TASK_RESULT_YIELD:
						bikeshed::ReadyTasks(_this->shed, 1, &task_id);
						break;
					}
				}
				else
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

    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(1)), 1, &sync_primitive.m_SyncPrimitive);
    ASSERT_NE(0, shed);

    nadir::TAtomic32 stop = 0;

    struct TaskData {
        TaskData(nadir::TAtomic32* stop)
            : stop(stop)
            , task_id((bikeshed::TTaskID)-1)
            , shed(0)
            , executed(0)
        { }
        static bikeshed::TaskResult Compute(bikeshed::HShed shed, bikeshed::TTaskID task_id, void* context_data)
        {
            TaskData* _this = (TaskData*)context_data;
            _this->shed = shed;
            ++_this->executed;
            _this->task_id = task_id;
            nadir::AtomicAdd32(_this->stop, 1);
            return bikeshed::TASK_RESULT_COMPLETE;
        }
        nadir::TAtomic32* stop;
        bikeshed::HShed shed;
        bikeshed::TTaskID task_id;
        uint32_t executed;
    } task(&stop);

    bikeshed::TaskFunc funcs[1] = {TaskData::Compute};
    void* contexts[1] = {&task};

    ThreadContext thread_context;
    thread_context.CreateThread(shed, sync_primitive.m_ConditionVariable, &stop);

    bikeshed::TTaskID task_id;
    ASSERT_TRUE(bikeshed::CreateTasks(shed, 1, funcs, contexts, &task_id));
    ASSERT_TRUE(bikeshed::ReadyTasks(shed, 1, &task_id));

    nadir::JoinThread(thread_context.thread, nadir::TIMEOUT_INFINITE);

    ASSERT_EQ(shed, task.shed);
    ASSERT_EQ(task_id, task.task_id);
    ASSERT_EQ(1, task.executed);

    bikeshed::FreeTasks(shed, 1, &task_id);
    bikeshed::TTaskID ready_task;
    ASSERT_TRUE(!bikeshed::GetFirstReadyTask(shed, &ready_task));

    free(shed);
}

TEST_BEGIN(test, main_setup, main_teardown, test_setup, test_teardown)
    TEST(create)
    TEST(single_task)
    TEST(test_sync)
    TEST(test_ready_order)
    TEST(test_dependency)
    TEST(test_worker_thread)
TEST_END(test)

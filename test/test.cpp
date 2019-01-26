#include "../src/bikeshed.h"

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
            , executed(false)
        { }
        static bikeshed::TaskResult Compute(bikeshed::HShed shed, bikeshed::TTaskID task_id, TaskData* task_data)
        {
            task_data->shed = shed;
            task_data->executed = true;
            task_data->task_id = task_id;
            return bikeshed::TASK_RESULT_COMPLETE;
        }
        bikeshed::HShed shed;
        bikeshed::TTaskID task_id;
        bool executed;
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

    ASSERT_EQ(task.shed, shed);
    ASSERT_EQ(task.task_id, ready_task);
    ASSERT_TRUE(task.executed);

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
			: m_SyncPrimitive{lock, unlock}
			, lock_count(0)
			, unlock_count(0)
		{

		}
		static bool lock(bikeshed::SyncPrimitive* primitive){
			((FakeLock*)primitive)->lock_count++;
			return true;
		}
		static void unlock(bikeshed::SyncPrimitive* primitive){
			((FakeLock*)primitive)->unlock_count++;
		}
		uint32_t lock_count;
		uint32_t unlock_count;
	}lock;
    bikeshed::HShed shed = bikeshed::CreateShed(malloc(bikeshed::GetShedSize(1)), 1, &lock.m_SyncPrimitive);
    ASSERT_NE(0, shed);

    struct TaskData {
        TaskData()
            : task_id((bikeshed::TTaskID)-1)
            , shed(0)
            , executed(false)
        { }
        static bikeshed::TaskResult Compute(bikeshed::HShed shed, bikeshed::TTaskID task_id, TaskData* task_data)
        {
            task_data->shed = shed;
            task_data->executed = true;
            task_data->task_id = task_id;
            return bikeshed::TASK_RESULT_COMPLETE;
        }
        bikeshed::HShed shed;
        bikeshed::TTaskID task_id;
        bool executed;
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

    ASSERT_EQ(task.shed, shed);
    ASSERT_EQ(task.task_id, ready_task);
    ASSERT_TRUE(task.executed);

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
            , executed(false)
        { }
        static bikeshed::TaskResult Compute(bikeshed::HShed shed, bikeshed::TTaskID task_id, TaskData* task_data)
        {
            task_data->shed = shed;
            task_data->executed = true;
            task_data->task_id = task_id;
            return bikeshed::TASK_RESULT_COMPLETE;
        }
        bikeshed::HShed shed;
        bikeshed::TTaskID task_id;
        bool executed;
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
    }
    bikeshed::TTaskID ready_task;
    ASSERT_TRUE(!bikeshed::GetFirstReadyTask(shed, &ready_task));
    free(shed);
}

TEST_BEGIN(test, main_setup, main_teardown, test_setup, test_teardown)
    TEST(create)
    TEST(single_task)
    TEST(test_sync)
    TEST(test_ready_order)
TEST_END(test)

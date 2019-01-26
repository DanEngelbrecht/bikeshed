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
    uint16_t resolved_task_count = 79;
    bikeshed::TaskResult result = bikeshed::ExecuteTask(shed, ready_task, &resolved_task_count, 0);
    ASSERT_EQ(bikeshed::TASK_RESULT_COMPLETE, result);
    ASSERT_EQ(0, resolved_task_count);

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
    uint16_t resolved_task_count = 79;
    bikeshed::TaskResult result = bikeshed::ExecuteTask(shed, ready_task, &resolved_task_count, 0);
    ASSERT_EQ(bikeshed::TASK_RESULT_COMPLETE, result);
    ASSERT_EQ(0, resolved_task_count);

    ASSERT_EQ(task.shed, shed);
    ASSERT_EQ(task.task_id, ready_task);
    ASSERT_TRUE(task.executed);

    bikeshed::FreeTasks(shed, 1, &ready_task);
    ASSERT_TRUE(!bikeshed::GetFirstReadyTask(shed, &ready_task));

	ASSERT_EQ(7, lock.lock_count);
	ASSERT_EQ(7, lock.unlock_count);

    free(shed);
}

TEST_BEGIN(test, main_setup, main_teardown, test_setup, test_teardown)
    TEST(create)
    TEST(single_task)
    TEST(test_sync)
TEST_END(test)

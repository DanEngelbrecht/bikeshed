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
}

TEST_BEGIN(test, main_setup, main_teardown, test_setup, test_teardown)
    TEST(create)
TEST_END(test)

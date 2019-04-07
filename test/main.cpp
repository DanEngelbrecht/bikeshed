#define JC_TEST_IMPLEMENTATION
#include "../third-party/jctest/src/jc_test.h"

int main(int argc, char** argv)
{
    jc_test_init(&argc, argv);
    return jc_test_run_all();
}

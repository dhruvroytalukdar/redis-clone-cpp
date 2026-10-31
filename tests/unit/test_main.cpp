// Only job: generate doctest's main(). Kept separate because the doctest
// implementation is slow to compile and should not be rebuilt with every test.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <stdlib.h>
#include <stdint.h>

#define true 1
#define false 0

#define USECS_IN_SECOND 1000000

static __inline unsigned int max(unsigned int a, unsigned int b) { return (a > b ? a : b); }
static __inline unsigned int min(unsigned int a, unsigned int b) { return (a < b ? a : b); }

static unsigned int
random_int_between(unsigned int min, unsigned int max)
{
    return (rand() % (max - min + 1)) + min;
};

static float
random_float_between_0_and_1()
{
    return (float)rand() / (float)RAND_MAX;
};
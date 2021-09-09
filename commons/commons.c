#include "commons.h"

unsigned long to_usec(struct timespec64 *t) {
	return (t->tv_sec * 1000000) + (t->tv_nsec / 1000);
}

unsigned long diff_usec(struct timespec64 *t1, struct timespec64 *t2) {
	struct timespec64 diff;
	diff = timespec64_sub(*t2, *t1);
	return to_usec(&diff);
}


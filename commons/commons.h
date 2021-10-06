#ifndef _SL_COMMONS_H
#define _SL_COMMONS_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>

struct SharedGpio {
	int gpio;
	bool busy;
};

unsigned long long to_usec(struct timespec64 *t);

unsigned long long diff_usec(struct timespec64 *t1, struct timespec64 *t2);

#endif

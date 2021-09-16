#ifndef _SL_CLOCKDATA_H
#define _SL_CLOCKDATA_H

#include "../commons/commons.h"

struct ClockDataBean {
	char id;
	struct SharedGpio *clockLine;
	struct SharedGpio *dataLine;
	unsigned int irq;
	bool irqRequested;
	unsigned long clockPeriodMin_usec;
	unsigned long clockPeriodMax_usec;
	bool enabled;
	uint64_t data;
	int bitCount;
	int noise;
	struct timespec64 lastClockTs;
};

void clockDataAdd(struct ClockDataBean* cd);

void clockDataDisable(struct ClockDataBean* cd);

ssize_t devAttrClockDataEnabled_show(struct device* dev,
		struct device_attribute* attr, char *buf);

ssize_t devAttrClockDataEnabled_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

ssize_t devAttrClockDataData_show(struct device* dev,
		struct device_attribute* attr, char *buf);

ssize_t devAttrClockDataNoise_show(struct device* dev,
		struct device_attribute* attr, char *buf);
ssize_t devAttrClockDataClockPeriodMin_show(struct device* dev,
		struct device_attribute* attr, char *buf);
ssize_t devAttrClockDataClockPeriodMin_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);
ssize_t devAttrClockDataClockPeriodMax_show(struct device* dev,
		struct device_attribute* attr, char *buf);

ssize_t devAttrClockDataClockPeriodMax_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

#endif

#include "clockdata.h"

#include <linux/interrupt.h>

#define CLOCKDATA_MAX_BITS 64
#define CLOCKDATA_INTERFACES 2

struct ClockDataBean *cds[CLOCKDATA_INTERFACES];
int cdCount = 0;

void clockDataAdd(struct ClockDataBean* cd) {
	cds[cdCount++] = cd;
	cd->irqRequested = false;
	cd->enabled = false;
	cd->clockPeriodMin_usec = 400;
	cd->clockPeriodMax_usec = 1600;
	cd->noise = 0;
	cd->id = '0' + cdCount;
}

static void clockDataReset(struct ClockDataBean* cd) {
	cd->enabled = true;
	cd->data = 0;
	cd->bitCount = 0;
}

void clockDataDisable(struct ClockDataBean* cd) {
	if (cd->enabled) {
		gpio_free(cd->clockLine->gpio);
		gpio_free(cd->dataLine->gpio);

		if (cd->irqRequested) {
			free_irq(cd->irq, NULL);
			cd->irqRequested = false;
		}

		cd->clockLine->busy = false;
		cd->dataLine->busy = false;
		cd->enabled = false;
	}
}

static irq_handler_t clockDataIrqHandler(unsigned int irq, void *dev_id,
		struct pt_regs *regs) {
	struct timespec64 now;
	unsigned long diff;
	struct ClockDataBean* cd = NULL;
	int i;

	for (i = 0; i < cdCount; i++) {
		if (cds[i]->enabled && irq == cds[i]->irq) {
			cd = cds[i];
			break;
		}
	}

	if (cd == NULL) {
		return (irq_handler_t) IRQ_HANDLED;
	}

	ktime_get_raw_ts64(&now);

	if (cd->bitCount != 0) {
		diff = diff_usec((struct timespec64 *) &(cd->lastClockTs), &now);

		if (diff < cd->clockPeriodMin_usec) {
			// clock too fast
			cd->noise = 10;
			goto noise;
		}

		if (diff > cd->clockPeriodMax_usec) {
			cd->data = 0;
			cd->bitCount = 0;
		}
	}

	cd->lastClockTs.tv_sec = now.tv_sec;
	cd->lastClockTs.tv_nsec = now.tv_nsec;

	if (cd->bitCount >= CLOCKDATA_MAX_BITS) {
		return (irq_handler_t) IRQ_HANDLED;
	}

	cd->data <<= 1;
	if (gpio_get_value(cd->dataLine->gpio) == 0) {
		cd->data |= 1;
	}
	cd->bitCount++;

	return (irq_handler_t) IRQ_HANDLED;

	noise:
	clockDataReset(cd);
	return (irq_handler_t) IRQ_HANDLED;
}

static struct ClockDataBean* getClockDataBean(struct device_attribute* attr) {
	int i;
	for (i = 0; i < cdCount; i++) {
		if (attr->attr.name[2] == cds[i]->id) {
			return cds[i];
		}
	}
	return NULL;
}

ssize_t devAttrClockDataEnabled_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct ClockDataBean* w;
	w = getClockDataBean(attr);
	return sprintf(buf, w->enabled ? "1\n" : "0\n");
}

ssize_t devAttrClockDataEnabled_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	struct ClockDataBean* w;
	bool enable;
	int result = 0;
	char reqName[] = "clockData_cdN_X";

	w = getClockDataBean(attr);

	if (buf[0] == '0') {
		enable = false;
	} else if (buf[0] == '1') {
		enable = true;
	} else {
		return -EINVAL;
	}

	if (enable && !w->enabled) {
		if (w->clockLine->busy || w->dataLine->busy) {
			return -EBUSY;
		}
		w->clockLine->busy = true;
		w->dataLine->busy = true;
		reqName[11] = w->id;

		reqName[14] = 'C';
		gpio_request(w->clockLine->gpio, reqName);
		reqName[14] = 'D';
		gpio_request(w->dataLine->gpio, reqName);

		result = gpio_direction_input(w->clockLine->gpio);
		if (!result) {
			result = gpio_direction_input(w->dataLine->gpio);
		}

		if (result) {
			printk(KERN_ALERT "error setting up clockData GPIOs\n");
			enable = false;
		} else {
			gpio_set_debounce(w->clockLine->gpio, 0);
			gpio_set_debounce(w->dataLine->gpio, 0);

			w->irq = gpio_to_irq(w->clockLine->gpio);

			reqName[14] = 'C';
			result = request_irq(w->irq, (irq_handler_t) clockDataIrqHandler,
					IRQF_TRIGGER_RISING, reqName, NULL);

			if (result) {
				printk(
				KERN_ALERT "error registering clockData irq handler\n");
				enable = false;
			} else {
				w->irqRequested = true;
			}
		}
	}

	if (enable) {
		w->noise = 0;
		clockDataReset(w);
	} else {
		clockDataDisable(w);
	}

	if (result) {
		return result;
	}
	return count;
}

ssize_t devAttrClockDataData_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct timespec64 now;
	unsigned long diff;
	struct ClockDataBean* w;
	w = getClockDataBean(attr);

	if (!w->enabled) {
		return -ENODEV;
	}

	ktime_get_raw_ts64(&now);
	diff = diff_usec((struct timespec64 *) &(w->lastClockTs), &now);
	if (diff <= w->clockPeriodMax_usec) {
		return -EBUSY;
	}

	return sprintf(buf, "%lu %d %llu\n", to_usec(&w->lastClockTs), w->bitCount,
			w->data);
}

ssize_t devAttrClockDataNoise_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct ClockDataBean* w;
	int noise;
	w = getClockDataBean(attr);
	noise = w->noise;

	w->noise = 0;

	return sprintf(buf, "%d\n", noise);
}

ssize_t devAttrClockDataClockPeriodMin_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct ClockDataBean* w;
	w = getClockDataBean(attr);

	return sprintf(buf, "%lu\n", w->clockPeriodMin_usec);
}

ssize_t devAttrClockDataClockPeriodMin_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct ClockDataBean* w;
	w = getClockDataBean(attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->clockPeriodMin_usec = val;

	return count;
}

ssize_t devAttrClockDataClockPeriodMax_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct ClockDataBean* w;
	w = getClockDataBean(attr);

	return sprintf(buf, "%lu\n", w->clockPeriodMax_usec);
}

ssize_t devAttrClockDataClockPeriodMax_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct ClockDataBean* w;
	w = getClockDataBean(attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->clockPeriodMax_usec = val;

	return count;
}

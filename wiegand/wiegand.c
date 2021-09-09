#include "wiegand.h"
#include <linux/interrupt.h>

#define WIEGAND_MAX_BITS 64
#define WIEGAND_INTERFACES 2

struct WiegandBean *ws[WIEGAND_INTERFACES];
int wCount = 0;

void wiegandAdd(struct WiegandBean* w) {
	ws[wCount++] = w;
	w->d0.irqRequested = false;
	w->d1.irqRequested = false;
	w->enabled = false;
	w->pulseWidthMin_usec = 10;
	w->pulseWidthMax_usec = 150;
	w->pulseIntervalMin_usec = 1200;
	w->pulseIntervalMax_usec = 2700;
	w->noise = 0;
	w->id = '0' + wCount;
}

static void wiegandReset(struct WiegandBean* w) {
	w->enabled = true;
	w->data = 0;
	w->bitCount = 0;
	w->activeLine = NULL;
	w->d0.wasLow = false;
	w->d1.wasLow = false;
}

void wiegandDisable(struct WiegandBean* w) {
	if (w->enabled) {
		gpio_free(w->d0.gpio->gpio);
		gpio_free(w->d1.gpio->gpio);

		if (w->d0.irqRequested) {
			free_irq(w->d0.irq, NULL);
			w->d0.irqRequested = false;
		}

		if (w->d1.irqRequested) {
			free_irq(w->d1.irq, NULL);
			w->d1.irqRequested = false;
		}

		w->enabled = false;
	}
}

static irq_handler_t wiegandDataIrqHandler(unsigned int irq, void *dev_id,
		struct pt_regs *regs) {
	bool isLow;
	struct timespec64 now;
	unsigned long diff;
	struct WiegandBean* w;
	struct WiegandLine* l = NULL;
	int i;

	for (i = 0; i < wCount; i++) {
		if (ws[i]->enabled) {
			if (irq == ws[i]->d0.irq) {
				w = ws[i];
				l = &ws[i]->d0;
				break;
			} else if (irq == ws[i]->d1.irq) {
				w = ws[i];
				l = &ws[i]->d1;
				break;
			}
		}
	}

	if (l == NULL) {
		return (irq_handler_t) IRQ_HANDLED;
	}

	isLow = gpio_get_value(l->gpio->gpio) == 0;

	ktime_get_raw_ts64(&now);

	if (l->wasLow == isLow) {
		// got the interrupt but didn't change state. Maybe a fast pulse
		w->noise = 10;
		return (irq_handler_t) IRQ_HANDLED;
	}

	l->wasLow = isLow;

	if (isLow) {
		if (w->bitCount != 0) {
			diff = diff_usec((struct timespec64 *) &(w->lastBitTs), &now);

			if (diff < w->pulseIntervalMin_usec) {
				// pulse too early
				w->noise = 11;
				goto noise;
			}

			if (diff > w->pulseIntervalMax_usec) {
				w->data = 0;
				w->bitCount = 0;
			}
		}

		if (w->activeLine != NULL) {
			// there's movement on both lines
			w->noise = 12;
			goto noise;
		}

		w->activeLine = l;

		w->lastBitTs.tv_sec = now.tv_sec;
		w->lastBitTs.tv_nsec = now.tv_nsec;

	} else {
		if (w->activeLine != l) {
			// there's movement on both lines or previous noise
			w->noise = 13;
			goto noise;
		}

		w->activeLine = NULL;

		if (w->bitCount >= WIEGAND_MAX_BITS) {
			return (irq_handler_t) IRQ_HANDLED;
		}

		diff = diff_usec((struct timespec64 *) &(w->lastBitTs), &now);
		if (diff < w->pulseWidthMin_usec) {
			// pulse too short
			w->noise = 14;
			goto noise;
		}
		if (diff > w->pulseWidthMax_usec) {
			// pulse too long
			w->noise = 15;
			goto noise;
		}

		w->data <<= 1;
		if (l == &w->d1) {
			w->data |= 1;
		}
		w->bitCount++;
	}

	return (irq_handler_t) IRQ_HANDLED;

	noise:
	wiegandReset(w);
	return (irq_handler_t) IRQ_HANDLED;
}

static struct WiegandBean* getWiegandBean(struct device_attribute* attr) {
	int i;
	for (i = 0; i < wCount; i++) {
		if (attr->attr.name[1] == ws[i]->id) {
			return ws[i];
		}
	}
	return NULL;
}

ssize_t devAttrWiegandEnabled_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(attr);
	return sprintf(buf, w->enabled ? "1\n" : "0\n");
}

ssize_t devAttrWiegandEnabled_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	struct WiegandBean* w;
	bool enable;
	int result = 0;
	char reqName[] = "wiegand_wN_dN";

	w = getWiegandBean(attr);

	if (buf[0] == '0') {
		enable = false;
	} else if (buf[0] == '1') {
		enable = true;
	} else {
		return -EINVAL;
	}

	if (enable && !w->enabled) {
		if (w->d0.gpio->busy || w->d1.gpio->busy) {
			return -EBUSY;
		}
		w->d0.gpio->busy = true;
		w->d1.gpio->busy = true;
		reqName[9] = w->id;

		reqName[12] = '0';
		gpio_request(w->d0.gpio->gpio, reqName);
		reqName[12] = '1';
		gpio_request(w->d1.gpio->gpio, reqName);

		result = gpio_direction_input(w->d0.gpio->gpio);
		if (!result) {
			result = gpio_direction_input(w->d1.gpio->gpio);
		}

		if (result) {
			printk(KERN_ALERT "error setting up wiegand GPIOs\n");
			enable = false;
		} else {
			gpio_set_debounce(w->d0.gpio->gpio, 0);
			gpio_set_debounce(w->d1.gpio->gpio, 0);

			w->d0.irq = gpio_to_irq(w->d0.gpio->gpio);
			w->d1.irq = gpio_to_irq(w->d1.gpio->gpio);

			reqName[12] = '0';
			result = request_irq(w->d0.irq,
					(irq_handler_t) wiegandDataIrqHandler,
					IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
					reqName, NULL);

			if (result) {
				printk(KERN_ALERT "error registering wiegand D0 irq handler\n");
				enable = false;
			} else {
				w->d0.irqRequested = true;

				reqName[12] = '1';
				result = request_irq(w->d1.irq,
						(irq_handler_t) wiegandDataIrqHandler,
						IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
						reqName, NULL);

				if (result) {
					printk(
					KERN_ALERT "error registering wiegand D1 irq handler\n");
					enable = false;
				} else {
					w->d1.irqRequested = true;
				}
			}
		}
	}

	if (enable) {
		w->noise = 0;
		wiegandReset(w);
	} else {
		wiegandDisable(w);
	}

	if (result) {
		return result;
	}
	return count;
}

ssize_t devAttrWiegandData_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct timespec64 now;
	unsigned long diff;
	struct WiegandBean* w;
	w = getWiegandBean(attr);

	if (!w->enabled) {
		return -ENODEV;
	}

	ktime_get_raw_ts64(&now);
	diff = diff_usec((struct timespec64 *) &(w->lastBitTs), &now);
	if (diff <= w->pulseIntervalMax_usec) {
		return -EBUSY;
	}

	return sprintf(buf, "%lu %d %llu\n", to_usec(&w->lastBitTs), w->bitCount,
			w->data);
}

ssize_t devAttrWiegandNoise_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	int noise;
	w = getWiegandBean(attr);
	noise = w->noise;

	w->noise = 0;

	return sprintf(buf, "%d\n", noise);
}

ssize_t devAttrWiegandPulseIntervalMin_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(attr);

	return sprintf(buf, "%lu\n", w->pulseIntervalMin_usec);
}

ssize_t devAttrWiegandPulseIntervalMin_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct WiegandBean* w;
	w = getWiegandBean(attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->pulseIntervalMin_usec = val;

	return count;
}

ssize_t devAttrWiegandPulseIntervalMax_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(attr);

	return sprintf(buf, "%lu\n", w->pulseIntervalMax_usec);
}

ssize_t devAttrWiegandPulseIntervalMax_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct WiegandBean* w;
	w = getWiegandBean(attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->pulseIntervalMax_usec = val;

	return count;
}


ssize_t devAttrWiegandPulseWidthMin_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(attr);

	return sprintf(buf, "%lu\n", w->pulseWidthMin_usec);
}

ssize_t devAttrWiegandPulseWidthMin_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct WiegandBean* w;
	w = getWiegandBean(attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->pulseWidthMin_usec = val;

	return count;
}

ssize_t devAttrWiegandPulseWidthMax_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(attr);

	return sprintf(buf, "%lu\n", w->pulseWidthMax_usec);
}

ssize_t devAttrWiegandPulseWidthMax_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct WiegandBean* w;
	w = getWiegandBean(attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->pulseWidthMax_usec = val;

	return count;
}

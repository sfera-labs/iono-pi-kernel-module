/*
 * Iono Pi kernel module
 *
 *     Copyright (C) 2020 Sfera Labs S.r.l.
 *
 *     For information, visit https://www.sferalabs.cc
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * LICENSE.txt file for more details.
 *
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/time.h>

#define WIEGAND_MAX_BITS 	64

#define GPIO_MODE_IN 		1
#define GPIO_MODE_OUT		2

#define GPIO_TTL1			4
#define GPIO_TTL2			26
#define GPIO_TTL3			20
#define GPIO_TTL4			21
#define GPIO_DI1			16
#define GPIO_DI2			19
#define GPIO_DI3			13
#define GPIO_DI4			12
#define GPIO_DI5			6
#define GPIO_DI6			5
#define GPIO_OC1			18
#define GPIO_OC2			25
#define GPIO_OC3			24
#define GPIO_O1				17
#define GPIO_O2				27
#define GPIO_O3				22
#define GPIO_O4				23
#define GPIO_LED			7

#define AI1_AI2_FACTOR 		7319
#define AI3_AI4_FACTOR 		725

#define AI1_MCP_CHANNEL 	1
#define AI2_MCP_CHANNEL 	0
#define AI3_MCP_CHANNEL 	2
#define AI4_MCP_CHANNEL 	3

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sfera Labs - http://sferalabs.cc");
MODULE_DESCRIPTION("Iono Pi driver module");
MODULE_VERSION("1.1");

struct DeviceAttrBean {
	struct device_attribute devAttr;
	int gpioMode;
	int gpio;
};

struct DeviceBean {
	char *name;
	struct device *pDevice;
	struct DeviceAttrBean *devAttrBeans;
};

struct WiegandLine {
	int gpio;
	unsigned int irq;
	bool irqRequested;
	bool wasLow;
};

struct WiegandBean {
	struct WiegandLine d0;
	struct WiegandLine d1;
	struct WiegandLine *activeLine;
	unsigned long pulseIntervalMin_usec;
	unsigned long pulseIntervalMax_usec;
	unsigned long pulseWidthMin_usec;
	unsigned long pulseWidthMax_usec;
	bool enabled;
	uint64_t data;
	int bitCount;
	struct timespec lastBitTs;
};

static struct class *pDeviceClass;

static ssize_t devAttrGpio_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrGpio_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrGpioBlink_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrAi1Mv_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrAi2Mv_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrAi3Mv_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrAi4Mv_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrAi1Raw_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrAi2Raw_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrAi3Raw_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrAi4Raw_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandEnabled_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandEnabled_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrWiegandData_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandPulseIntervalMin_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandPulseIntervalMin_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrWiegandPulseIntervalMax_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandPulseIntervalMax_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrWiegandPulseWidthMin_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandPulseWidthMin_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrWiegandPulseWidthMax_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandPulseWidthMax_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static struct WiegandBean w1 = {
	.d0 = {
		.gpio = GPIO_TTL1,
		.irqRequested = false,
	},
	.d1 = {
		.gpio = GPIO_TTL2,
		.irqRequested = false,
	},
	.enabled = false,
	.pulseWidthMin_usec = 10,
	.pulseWidthMax_usec = 150,
	.pulseIntervalMin_usec = 1200,
	.pulseIntervalMax_usec = 2700,
};

static struct WiegandBean w2 = {
	.d0 = {
		.gpio = GPIO_TTL3,
		.irqRequested = false,
	},
	.d1 = {
		.gpio = GPIO_TTL4,
		.irqRequested = false,
	},
	.enabled = false,
	.pulseWidthMin_usec = 10,
	.pulseWidthMax_usec = 150,
	.pulseIntervalMin_usec = 1200,
	.pulseIntervalMax_usec = 2700,
};

static struct DeviceAttrBean devAttrBeansLed[] = {
	{
		.devAttr = {
			.attr = {
				.name = "status",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpioMode = GPIO_MODE_OUT,
		.gpio = GPIO_LED,
	},

	{
		.devAttr = {
			.attr = {
				.name = "blink",
				.mode = 0220,
			},
			.store = devAttrGpioBlink_store,
		},
		.gpioMode = GPIO_MODE_OUT,
		.gpio = GPIO_LED,
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansDigitalIn[] = {
	{
		.devAttr = {
			.attr = {
				.name = "di1",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
		},
		.gpioMode = GPIO_MODE_IN,
		.gpio = GPIO_DI1,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di2",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
		},
		.gpioMode = GPIO_MODE_IN,
		.gpio = GPIO_DI2,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di3",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
		},
		.gpioMode = GPIO_MODE_IN,
		.gpio = GPIO_DI3,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di4",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
		},
		.gpioMode = GPIO_MODE_IN,
		.gpio = GPIO_DI4,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di5",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
		},
		.gpioMode = GPIO_MODE_IN,
		.gpio = GPIO_DI5,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di6",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
		},
		.gpioMode = GPIO_MODE_IN,
		.gpio = GPIO_DI6,
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansAnalogIn[] = {
	{
		.devAttr = {
			.attr = {
				.name = "ai1_mv",
				.mode = 0440,
			},
			.show = devAttrAi1Mv_show,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai2_mv",
				.mode = 0440,
			},
			.show = devAttrAi2Mv_show,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai3_mv",
				.mode = 0440,
			},
			.show = devAttrAi3Mv_show,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai4_mv",
				.mode = 0440,
			},
			.show = devAttrAi4Mv_show,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai1_raw",
				.mode = 0440,
			},
			.show = devAttrAi1Raw_show,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai2_raw",
				.mode = 0440,
			},
			.show = devAttrAi2Raw_show,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai3_raw",
				.mode = 0440,
			},
			.show = devAttrAi3Raw_show,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai4_raw",
				.mode = 0440,
			},
			.show = devAttrAi4Raw_show,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansRelay[] = {
	{
		.devAttr = {
			.attr = {
				.name = "o1",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpioMode = GPIO_MODE_OUT,
		.gpio = GPIO_O1,
	},

	{
		.devAttr = {
			.attr = {
				.name = "o2",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpioMode = GPIO_MODE_OUT,
		.gpio = GPIO_O2,
	},

	{
		.devAttr = {
			.attr = {
				.name = "o3",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpioMode = GPIO_MODE_OUT,
		.gpio = GPIO_O3,
	},

	{
		.devAttr = {
			.attr = {
				.name = "o4",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpioMode = GPIO_MODE_OUT,
		.gpio = GPIO_O4,
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansOpenCollector[] = {
	{
		.devAttr = {
			.attr = {
				.name = "oc1",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpioMode = GPIO_MODE_OUT,
		.gpio = GPIO_OC1,
	},

	{
		.devAttr = {
			.attr = {
				.name = "oc2",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpioMode = GPIO_MODE_OUT,
		.gpio = GPIO_OC2,
	},

	{
		.devAttr = {
			.attr = {
				.name = "oc3",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpioMode = GPIO_MODE_OUT,
		.gpio = GPIO_OC3,
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansWiegand[] = {
	{
		.devAttr = {
			.attr = {
				.name = "w1_enabled",
				.mode = 0660,
			},
			.show = devAttrWiegandEnabled_show,
			.store = devAttrWiegandEnabled_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w1_data",
				.mode = 0440,
			},
			.show = devAttrWiegandData_show,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w1_pulse_itvl_min",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseIntervalMin_show,
			.store = devAttrWiegandPulseIntervalMin_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w1_pulse_itvl_max",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseIntervalMax_show,
			.store = devAttrWiegandPulseIntervalMax_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w1_pulse_width_min",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseWidthMin_show,
			.store = devAttrWiegandPulseWidthMin_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w1_pulse_width_max",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseWidthMax_show,
			.store = devAttrWiegandPulseWidthMax_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w2_enabled",
				.mode = 0660,
			},
			.show = devAttrWiegandEnabled_show,
			.store = devAttrWiegandEnabled_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w2_data",
				.mode = 0440,
			},
			.show = devAttrWiegandData_show,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w2_pulse_itvl_min",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseIntervalMin_show,
			.store = devAttrWiegandPulseIntervalMin_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w2_pulse_itvl_max",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseIntervalMax_show,
			.store = devAttrWiegandPulseIntervalMax_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w2_pulse_width_min",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseWidthMin_show,
			.store = devAttrWiegandPulseWidthMin_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w2_pulse_width_max",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseWidthMax_show,
			.store = devAttrWiegandPulseWidthMax_store,
		}
	},

	{ }
};

static struct DeviceBean devices[] = {
	{
		.name = "led",
		.devAttrBeans = devAttrBeansLed,
	},

	{
		.name = "digital_in",
		.devAttrBeans = devAttrBeansDigitalIn,
	},

	{
		.name = "analog_in",
		.devAttrBeans = devAttrBeansAnalogIn,
	},

	{
		.name = "relay",
		.devAttrBeans = devAttrBeansRelay,
	},

	{
		.name = "open_coll",
		.devAttrBeans = devAttrBeansOpenCollector,
	},

	{
		.name = "wiegand",
		.devAttrBeans = devAttrBeansWiegand,
	},

	{ }
};

struct mcp3204_data {
	struct spi_device *spi;
	struct spi_message msg;
	struct spi_transfer transfer[2];

	struct regulator *reg;
	struct mutex lock;

	u8 tx_buf ____cacheline_aligned;
	u8 rx_buf[2];
};

static struct mcp3204_data *mcp3204_spi_data;

static char toUpper(char c) {
	if (c >= 97 && c <= 122) {
		return c - 32;
	}
	return c;
}

static struct DeviceAttrBean* devAttrGetBean(struct device* dev,
		struct device_attribute* attr) {
	int di, ai;
	di = 0;
	while (devices[di].name != NULL) {
		if (dev == devices[di].pDevice) {
			ai = 0;
			while (devices[di].devAttrBeans[ai].devAttr.attr.name != NULL) {
				if (attr == &devices[di].devAttrBeans[ai].devAttr) {
					return &devices[di].devAttrBeans[ai];
					break;
				}
				ai++;
			}
			break;
		}
		di++;
	}
	return NULL;
}

static int getGpio(struct device* dev, struct device_attribute* attr) {
	struct DeviceAttrBean* dab;
	dab = devAttrGetBean(dev, attr);
	if (dab == NULL || dab->gpioMode == 0) {
		return -1;
	}
	return dab->gpio;
}

static ssize_t devAttrGpio_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	int gpio;
	gpio = getGpio(dev, attr);
	if (gpio < 0) {
		return -EFAULT;
	}
	return sprintf(buf, "%d\n", gpio_get_value(gpio));
}

static ssize_t devAttrGpio_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	bool val;
	int gpio = getGpio(dev, attr);
	if (gpio < 0) {
		return -EFAULT;
	}
	if (kstrtobool(buf, &val) < 0) {
		if (toUpper(buf[0]) == 'E') { // Enable
			val = true;
		} else if (toUpper(buf[0]) == 'D') { // Disable
			val = false;
		} else if (toUpper(buf[0]) == 'F' || toUpper(buf[0]) == 'T') { // Flip/Toggle
			val = gpio_get_value(gpio) == 1 ? false : true;
		} else {
			return -EINVAL;
		}
	}
	gpio_set_value(gpio, val ? 1 : 0);
	return count;
}

static ssize_t devAttrGpioBlink_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int i;
	long on = 0;
	long off = 0;
	long rep = 1;
	char *end = NULL;
	int gpio = getGpio(dev, attr);
	if (gpio < 0) {
		return -EFAULT;
	}
	on = simple_strtol(buf, &end, 10);
	if (++end < buf + count) {
		off = simple_strtol(end, &end, 10);
		if (++end < buf + count) {
			rep = simple_strtol(end, NULL, 10);
		}
	}
	if (rep < 1) {
		rep = 1;
	}
	printk(KERN_INFO "ionopi: - | gpio blink %ld %ld %ld\n", on, off, rep);
	if (on > 0) {
		for (i = 0; i < rep; i++) {
			gpio_set_value(gpio, 1);
			msleep(on);
			gpio_set_value(gpio, 0);
			if (i < rep - 1) {
				msleep(off);
			}
		}
	}
	return count;
}

static ssize_t devAttrMcp3204_show(char *buf, unsigned int channel, int mult) {
	int ret;

	memset(&mcp3204_spi_data->rx_buf, 0, sizeof(mcp3204_spi_data->rx_buf));
	mcp3204_spi_data->tx_buf = 0b1100000 | (channel << 2);

	ret = spi_sync(mcp3204_spi_data->spi, &mcp3204_spi_data->msg);
	if (ret < 0) {
		return ret;
	}

	ret = mcp3204_spi_data->rx_buf[0] << 4 | mcp3204_spi_data->rx_buf[1] >> 4;
	if (mult > 0) {
		ret = ret * mult / 1000;
	}

	return sprintf(buf, "%d\n", ret);
}

static ssize_t devAttrAi1Mv_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	return devAttrMcp3204_show(buf, AI1_MCP_CHANNEL, AI1_AI2_FACTOR);
}

static ssize_t devAttrAi2Mv_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	return devAttrMcp3204_show(buf, AI2_MCP_CHANNEL, AI1_AI2_FACTOR);
}

static ssize_t devAttrAi3Mv_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	return devAttrMcp3204_show(buf, AI3_MCP_CHANNEL, AI3_AI4_FACTOR);
}

static ssize_t devAttrAi4Mv_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	return devAttrMcp3204_show(buf, AI4_MCP_CHANNEL, AI3_AI4_FACTOR);
}

static ssize_t devAttrAi1Raw_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	return devAttrMcp3204_show(buf, AI1_MCP_CHANNEL, 0);
}

static ssize_t devAttrAi2Raw_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	return devAttrMcp3204_show(buf, AI2_MCP_CHANNEL, 0);
}

static ssize_t devAttrAi3Raw_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	return devAttrMcp3204_show(buf, AI3_MCP_CHANNEL, 0);
}

static ssize_t devAttrAi4Raw_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	return devAttrMcp3204_show(buf, AI4_MCP_CHANNEL, 0);
}

static struct WiegandBean* getWiegandBean(struct device* dev,
		struct device_attribute* attr) {
	struct DeviceAttrBean* dab;
	dab = devAttrGetBean(dev, attr);
	if (dab->devAttr.attr.name[1] == '1') {
		return &w1;
	} else {
		return &w2;
	}
}

static ssize_t devAttrWiegandEnabled_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);
	return sprintf(buf, w->enabled ? "1\n" : "0\n");
}

static void wiegandReset(struct WiegandBean* w) {
	w->enabled = true;
	w->data = 0;
	w->bitCount = 0;
	w->activeLine = NULL;
	w->d0.wasLow = false;
	w->d1.wasLow = false;
}

static unsigned long to_usec(struct timespec *t) {
	return (t->tv_sec * 1000000) + (t->tv_nsec / 1000);
}

static unsigned long diff_usec(struct timespec *t1, struct timespec *t2) {
	struct timespec diff;
	diff = timespec_sub(*t2, *t1);
	return to_usec(&diff);
}

static irq_handler_t wiegandDataIrqHandler(unsigned int irq, void *dev_id,
		struct pt_regs *regs) {
	bool isLow;
	struct timespec now;
	unsigned long diff;
	struct WiegandBean* w;
	struct WiegandLine* l = NULL;

	if (w1.enabled) {
		if (irq == w1.d0.irq) {
			w = &w1;
			l = &w1.d0;
		} else if (irq == w1.d1.irq) {
			w = &w1;
			l = &w1.d1;
		}
	}

	if (w2.enabled) {
		if (irq == w2.d0.irq) {
			w = &w2;
			l = &w2.d0;
		} else if (irq == w2.d1.irq) {
			w = &w2;
			l = &w2.d1;
		}
	}

	if (l == NULL) {
		return (irq_handler_t) IRQ_HANDLED;
	}

	isLow = gpio_get_value(l->gpio) == 0;

	getrawmonotonic(&now);

	if (l->wasLow == isLow) {
		// got the interrupt but didn't change state. Maybe a fast pulse
		printk(KERN_ALERT "ionopi: * | repeated interrupt on GPIO %d\n", l->gpio);
		return (irq_handler_t) IRQ_HANDLED;
	}

	l->wasLow = isLow;

	if (isLow) {
		if (w->bitCount != 0) {
			diff = diff_usec((struct timespec *) &(w->lastBitTs), &now);

			if (diff < w->pulseIntervalMin_usec) {
				// pulse too early
				goto noise;
			}

			if (diff > w->pulseIntervalMax_usec) {
				w->data = 0;
				w->bitCount = 0;
			}
		}

		if (w->activeLine != NULL) {
			// there's movement on both lines
			goto noise;
		}

		w->activeLine = l;

		w->lastBitTs.tv_sec = now.tv_sec;
		w->lastBitTs.tv_nsec = now.tv_nsec;

	} else {
		if (w->activeLine != l) {
			// there's movement on both lines or previous noise
			goto noise;
		}

		w->activeLine = NULL;

		if (w->bitCount >= WIEGAND_MAX_BITS) {
			return (irq_handler_t) IRQ_HANDLED;
		}

		diff = diff_usec((struct timespec *) &(w->lastBitTs), &now);
		if (diff < w->pulseWidthMin_usec || diff > w->pulseWidthMax_usec) {
			// pulse too short or too long
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

static void wiegandDisable(struct WiegandBean* w) {
	w->enabled = false;

	gpio_unexport(w->d0.gpio);
	gpio_unexport(w->d1.gpio);

	gpio_free(w->d0.gpio);
	gpio_free(w->d1.gpio);

	if (w->d0.irqRequested) {
		free_irq(w->d0.irq, NULL);
		w->d0.irqRequested = false;
	}

	if (w->d1.irqRequested) {
		free_irq(w->d1.irq, NULL);
		w->d1.irqRequested = false;
	}
}

static ssize_t devAttrWiegandEnabled_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	struct WiegandBean* w;
	bool enable;
	bool isW1;
	int result = 0;
	char reqName[] = "ionopi_wN_dN";

	w = getWiegandBean(dev, attr);

	if (buf[0] == '0') {
		enable = false;
	} else if (buf[0] == '1') {
		enable = true;
	} else {
		return -EINVAL;
	}

	if (enable) {
		isW1 = w == &w1;
		if (isW1) {
			reqName[8] = '1';
		} else {
			reqName[8] = '2';
		}

		reqName[11] = '0';
		gpio_request(w->d0.gpio, reqName);
		reqName[11] = '1';
		gpio_request(w->d1.gpio, reqName);

		result = gpio_direction_input(w->d0.gpio);
		if (!result) {
			result = gpio_direction_input(w->d1.gpio);
		}

		if (result) {
			printk(KERN_ALERT "ionopi: * | error setting up wiegand GPIOs\n");
			enable = false;
		} else {
			gpio_set_debounce(w->d0.gpio, 0);
			gpio_set_debounce(w->d1.gpio, 0);
			gpio_export(w->d0.gpio, false);
			gpio_export(w->d1.gpio, false);

			w->d0.irq = gpio_to_irq(w->d0.gpio);
			w->d1.irq = gpio_to_irq(w->d1.gpio);

			reqName[11] = '0';
			result = request_irq(w->d0.irq,
					(irq_handler_t) wiegandDataIrqHandler,
					IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
					reqName, NULL);

			if (result) {
				printk(KERN_ALERT "ionopi: * | error registering wiegand D0 irq handler\n");
				enable = false;
			} else {
				w->d0.irqRequested = true;

				reqName[11] = '1';
				result = request_irq(w->d1.irq,
						(irq_handler_t) wiegandDataIrqHandler,
						IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
						reqName, NULL);

				if (result) {
					printk(KERN_ALERT "ionopi: * | error registering wiegand D1 irq handler\n");
					enable = false;
				} else {
					w->d1.irqRequested = true;
				}
			}
		}
	}

	if (enable) {
		wiegandReset(w);
	} else {
		wiegandDisable(w);
	}

	if (result) {
		return result;
	}
	return count;
}

static ssize_t devAttrWiegandData_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct timespec now;
	unsigned long diff;
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	if (!w->enabled) {
		return -ENODEV;
	}

	getrawmonotonic(&now);
	diff = diff_usec((struct timespec *) &(w->lastBitTs), &now);
	if (diff <= w->pulseIntervalMax_usec) {
		return -EBUSY;
	}

	return sprintf(buf, "%lu %d %llu\n", to_usec(&w->lastBitTs), w->bitCount,
			w->data);
}

static ssize_t devAttrWiegandPulseIntervalMin_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	return sprintf(buf, "%lu\n", w->pulseIntervalMin_usec);
}

static ssize_t devAttrWiegandPulseIntervalMin_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->pulseIntervalMin_usec = val;

	return count;
}

static ssize_t devAttrWiegandPulseIntervalMax_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	return sprintf(buf, "%lu\n", w->pulseIntervalMax_usec);
}

static ssize_t devAttrWiegandPulseIntervalMax_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->pulseIntervalMax_usec = val;

	return count;
}

static ssize_t devAttrWiegandPulseWidthMin_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	return sprintf(buf, "%lu\n", w->pulseWidthMin_usec);
}

static ssize_t devAttrWiegandPulseWidthMin_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->pulseWidthMin_usec = val;

	return count;
}

static ssize_t devAttrWiegandPulseWidthMax_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	return sprintf(buf, "%lu\n", w->pulseWidthMax_usec);
}

static ssize_t devAttrWiegandPulseWidthMax_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->pulseWidthMax_usec = val;

	return count;
}

static int mcp3204_spi_probe(struct spi_device *spi) {
	int ret;

	mcp3204_spi_data = devm_kzalloc(&spi->dev, sizeof(struct mcp3204_data),
			GFP_KERNEL);
	if (!mcp3204_spi_data) {
		return -ENOMEM;
	}

	mcp3204_spi_data->spi = spi;
	spi_set_drvdata(spi, mcp3204_spi_data);

	mcp3204_spi_data->transfer[0].tx_buf = &mcp3204_spi_data->tx_buf;
	mcp3204_spi_data->transfer[0].len = sizeof(mcp3204_spi_data->tx_buf);
	mcp3204_spi_data->transfer[1].rx_buf = mcp3204_spi_data->rx_buf;
	mcp3204_spi_data->transfer[1].len = 2;

	spi_message_init_with_transfers(&mcp3204_spi_data->msg,
			mcp3204_spi_data->transfer,
			ARRAY_SIZE(mcp3204_spi_data->transfer));

	mcp3204_spi_data->reg = devm_regulator_get(&spi->dev, "vref");
	if (IS_ERR(mcp3204_spi_data->reg)) {
		return PTR_ERR(mcp3204_spi_data->reg);
	}

	ret = regulator_enable(mcp3204_spi_data->reg);
	if (ret < 0) {
		return ret;
	}

	mutex_init(&mcp3204_spi_data->lock);

	printk(KERN_INFO "ionopi: - | mcp3204 probed\n");

	return 0;
}

static int mcp3204_spi_remove(struct spi_device *spi) {
	struct mcp3204_data *data = spi_get_drvdata(spi);

	regulator_disable(data->reg);
	mutex_destroy(&data->lock);

	printk(KERN_INFO "ionopi: - | mcp3204 removed\n");

	return 0;
}

const struct of_device_id ionopi_of_match[] = {
	{ .compatible = "sferalabs,ionopi", },
	{ },
};
MODULE_DEVICE_TABLE( of, ionopi_of_match);

static const struct spi_device_id ionopi_spi_ids[] = {
	{ "ionopi", 0 },
	{ },
};
MODULE_DEVICE_TABLE( spi, ionopi_spi_ids);

static struct spi_driver mcp3204_spi_driver = {
	.driver = {
		.name = "ionopi",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ionopi_of_match),
	},
	.probe = mcp3204_spi_probe,
	.remove = mcp3204_spi_remove,
	.id_table = ionopi_spi_ids,
};

static void cleanup(void) {
	int di, ai;

	spi_unregister_driver(&mcp3204_spi_driver);

	di = 0;
	while (devices[di].name != NULL) {
		if (devices[di].pDevice && !IS_ERR(devices[di].pDevice)) {
			ai = 0;
			while (devices[di].devAttrBeans[ai].devAttr.attr.name != NULL) {
				device_remove_file(devices[di].pDevice,
						&devices[di].devAttrBeans[ai].devAttr);
				if (devices[di].devAttrBeans[ai].gpioMode != 0) {
					gpio_unexport(devices[di].devAttrBeans[ai].gpio);
					gpio_free(devices[di].devAttrBeans[ai].gpio);
				}
				ai++;
			}
		}
		device_destroy(pDeviceClass, 0);
		di++;
	}

	if (!IS_ERR(pDeviceClass)) {
		class_destroy(pDeviceClass);
	}

	wiegandDisable(&w1);
	wiegandDisable(&w2);
}

static int __init ionopi_init(void) {
	char gpioReqName[256];
	char *gpioReqNamePart;
	int result = 0;
	int di, ai;

	printk(KERN_INFO "ionopi: - | init\n");

	strcpy(gpioReqName, "ionopi_");
	gpioReqNamePart = gpioReqName + strlen("ionopi_");

	if (spi_register_driver(&mcp3204_spi_driver)) {
		printk(KERN_ALERT "ionopi: * | failed to register mcp3204 driver\n");
		goto fail;
	}

	pDeviceClass = class_create(THIS_MODULE, "ionopi");
	if (IS_ERR(pDeviceClass)) {
		printk(KERN_ALERT "ionopi: * | failed to create device class\n");
		goto fail;
	}

	di = 0;
	while (devices[di].name != NULL) {
		devices[di].pDevice = device_create(pDeviceClass, NULL, 0, NULL,
				devices[di].name);
		if (IS_ERR(devices[di].pDevice)) {
			printk(KERN_ALERT "ionopi: * | failed to create device '%s'\n", devices[di].name);
			goto fail;
		}

		ai = 0;
		while (devices[di].devAttrBeans[ai].devAttr.attr.name != NULL) {
			result = device_create_file(devices[di].pDevice,
					&devices[di].devAttrBeans[ai].devAttr);
			if (result) {
				printk(KERN_ALERT "ionopi: * | failed to create device file '%s/%s'\n",
						devices[di].name,
						devices[di].devAttrBeans[ai].devAttr.attr.name);
				goto fail;
			}
			if (devices[di].devAttrBeans[ai].gpioMode != 0) {
				strcpy(gpioReqNamePart, devices[di].name);
				gpioReqNamePart[strlen(devices[di].name)] = '_';

				strcpy(gpioReqNamePart + strlen(devices[di].name) + 1,
						devices[di].devAttrBeans[ai].devAttr.attr.name);

				gpio_request(devices[di].devAttrBeans[ai].gpio, gpioReqName);
				if (devices[di].devAttrBeans[ai].gpioMode == GPIO_MODE_OUT) {
					result = gpio_direction_output(devices[di].devAttrBeans[ai].gpio, false);
				} else {
					result = gpio_direction_input(devices[di].devAttrBeans[ai].gpio);
				}
				if (result) {
					printk(KERN_ALERT "ionopi: * | error setting up GPIO %d\n",
							devices[di].devAttrBeans[ai].gpio);
					goto fail;
				}
				gpio_export(devices[di].devAttrBeans[ai].gpio, false);
			}
			ai++;
		}
		di++;
	}

	printk(KERN_INFO "ionopi: - | ready\n");
	return 0;

	fail:
	printk(KERN_ALERT "ionopi: * | init failed\n");
	cleanup();
	return -1;
}

static void __exit ionopi_exit(void) {
	cleanup();
	printk(KERN_INFO "ionopi: - | exit\n");
}

module_init( ionopi_init);
module_exit( ionopi_exit);

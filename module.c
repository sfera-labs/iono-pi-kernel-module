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

#include "commons/commons.h"
#include "wiegand/wiegand.h"
#include "atecc/atecc.h"
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

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

#define DEBOUNCE_DEFAULT_TIME_USEC 50000ul
#define DEBOUNCE_STATE_NOT_DEFINED -1

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sfera Labs - http://sferalabs.cc");
MODULE_DESCRIPTION("Iono Pi driver module");
MODULE_VERSION("1.6");

struct DebounceBean {
	int gpio;
	const char* debIrqDevName;
	int debValue;
	int debPastValue;
	int debIrqNum;
	struct timespec64 lastDebIrqTs;
	unsigned long debOnMinTime_usec;
	unsigned long debOffMinTime_usec;
	unsigned long debOnStateCnt;
	unsigned long debOffStateCnt;
};

struct DeviceAttrBean {
	struct device_attribute devAttr;
	int gpioMode;
	int gpio;
	struct DebounceBean *debBean;
};

struct DeviceBean {
	char *name;
	struct device *pDevice;
	struct DeviceAttrBean *devAttrBeans;
};

static struct class *pDeviceClass;

static ssize_t devAttrGpio_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrGpio_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrGpioDeb_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrGpioDebMsOn_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrGpioDebMsOn_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrGpioDebMsOff_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrGpioDebMsOff_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrGpioDebOnCnt_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrGpioDebOffCnt_show(struct device* dev,
		struct device_attribute* attr, char *buf);

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

static struct SharedGpio ttl1 = {
	.gpio = GPIO_TTL1,
	.busy = false,
};

static struct SharedGpio ttl2 = {
	.gpio = GPIO_TTL2,
	.busy = false,
};

static struct SharedGpio ttl3 = {
	.gpio = GPIO_TTL3,
	.busy = false,
};

static struct SharedGpio ttl4 = {
	.gpio = GPIO_TTL4,
	.busy = false,
};

static struct WiegandBean w1 = {
	.d0 = {
		.gpio = &ttl1,
	},
	.d1 = {
		.gpio = &ttl2,
	},
};

static struct WiegandBean w2 = {
	.d0 = {
		.gpio = &ttl3,
	},
	.d1 = {
		.gpio = &ttl4,
	},
};

enum digital_in {
	DI1 = 0,
	DI2,
	DI3,
	DI4,
	DI5,
	DI6,
};

static struct DebounceBean debounceBeans[] = {
	[DI1] = {
		.gpio = GPIO_DI1,
		.debIrqDevName = "ionopi_di1_deb",
		.debOnMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOffMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOnStateCnt = 0,
		.debOffStateCnt = 0,
	},

	[DI2] = {
		.gpio = GPIO_DI2,
		.debIrqDevName = "ionopi_di2_deb",
		.debOnMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOffMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOnStateCnt = 0,
		.debOffStateCnt = 0,
	},

	[DI3] = {
		.gpio = GPIO_DI3,
		.debIrqDevName = "ionopi_di3_deb",
		.debOnMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOffMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOnStateCnt = 0,
		.debOffStateCnt = 0,
	},

	[DI4] = {
		.gpio = GPIO_DI4,
		.debIrqDevName = "ionopi_di4_deb",
		.debOnMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOffMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOnStateCnt = 0,
		.debOffStateCnt = 0,
	},

	[DI5] = {
		.gpio = GPIO_DI5,
		.debIrqDevName = "ionopi_di5_deb",
		.debOnMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOffMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOnStateCnt = 0,
		.debOffStateCnt = 0,
	},

	[DI6] = {
		.gpio = GPIO_DI6,
		.debIrqDevName = "ionopi_di6_deb",
		.debOnMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOffMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOnStateCnt = 0,
		.debOffStateCnt = 0,
	},

	{ }
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

	{
		.devAttr = {
			.attr = {
				.name = "di1_deb",
				.mode = 0440,
			},
			.show = devAttrGpioDeb_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI1],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di2_deb",
				.mode = 0440,
			},
			.show = devAttrGpioDeb_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI2],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di3_deb",
				.mode = 0440,
			},
			.show = devAttrGpioDeb_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI3],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di4_deb",
				.mode = 0440,
			},
			.show = devAttrGpioDeb_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI4],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di5_deb",
				.mode = 0440,
			},
			.show = devAttrGpioDeb_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI5],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di6_deb",
				.mode = 0440,
			},
			.show = devAttrGpioDeb_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI6],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di1_deb_on_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOn_show,
			.store = devAttrGpioDebMsOn_store,
		},
		.debBean = &debounceBeans[DI1],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di1_deb_off_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOff_show,
			.store = devAttrGpioDebMsOff_store,
		},
		.debBean = &debounceBeans[DI1],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di2_deb_on_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOn_show,
			.store = devAttrGpioDebMsOn_store,
		},
		.debBean = &debounceBeans[DI2],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di2_deb_off_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOff_show,
			.store = devAttrGpioDebMsOff_store,
		},
		.debBean = &debounceBeans[DI2],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di3_deb_on_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOn_show,
			.store = devAttrGpioDebMsOn_store,
		},
		.debBean = &debounceBeans[DI3],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di3_deb_off_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOff_show,
			.store = devAttrGpioDebMsOff_store,
		},
		.debBean = &debounceBeans[DI3],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di4_deb_on_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOn_show,
			.store = devAttrGpioDebMsOn_store,
		},
		.debBean = &debounceBeans[DI4],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di4_deb_off_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOff_show,
			.store = devAttrGpioDebMsOff_store,
		},
		.debBean = &debounceBeans[DI4],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di5_deb_on_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOn_show,
			.store = devAttrGpioDebMsOn_store,
		},
		.debBean = &debounceBeans[DI5],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di5_deb_off_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOff_show,
			.store = devAttrGpioDebMsOff_store,
		},
		.debBean = &debounceBeans[DI5],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di6_deb_on_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOn_show,
			.store = devAttrGpioDebMsOn_store,
		},
		.debBean = &debounceBeans[DI6],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di6_deb_off_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOff_show,
			.store = devAttrGpioDebMsOff_store,
		},
		.debBean = &debounceBeans[DI6],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di1_deb_on_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOnCnt_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI1],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di1_deb_off_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOffCnt_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI1],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di2_deb_on_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOnCnt_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI2],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di2_deb_off_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOffCnt_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI2],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di3_deb_on_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOnCnt_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI3],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di3_deb_off_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOffCnt_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI3],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di4_deb_on_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOnCnt_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI4],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di4_deb_off_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOffCnt_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI4],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di5_deb_on_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOnCnt_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI5],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di5_deb_off_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOffCnt_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI5],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di6_deb_on_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOnCnt_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI6],
	},

	{
		.devAttr = {
			.attr = {
				.name = "di6_deb_off_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOffCnt_show,
			.store = NULL,
		},
		.debBean = &debounceBeans[DI6],
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
				.name = "w1_noise",
				.mode = 0440,
			},
			.show = devAttrWiegandNoise_show,
			.store = NULL,
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
				.name = "w2_noise",
				.mode = 0440,
			},
			.show = devAttrWiegandNoise_show,
			.store = NULL,
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

static struct DeviceAttrBean devAttrBeansAtecc[] = {
	{
		.devAttr = {
			.attr = {
				.name = "serial_num",
				.mode = 0440,
			},
			.show = devAttrAteccSerial_show,
			.store = NULL,
		},
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

	{
		.name = "sec_elem",
		.devAttrBeans = devAttrBeansAtecc,
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

static ssize_t devAttrGpioDeb_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct timespec64 now;
	unsigned long long diff;
	int actualGPIOStatus;
	struct DeviceAttrBean *dab;
	int res;

	ktime_get_raw_ts64(&now);
	dab = container_of(attr, struct DeviceAttrBean, devAttr);
	diff = diff_usec((struct timespec64*) &dab->debBean->lastDebIrqTs,
			&now);
	actualGPIOStatus = gpio_get_value(dab->debBean->gpio);
	if (actualGPIOStatus) {
		if (diff >= dab->debBean->debOnMinTime_usec) {
			res = actualGPIOStatus;
		} else {
			res = dab->debBean->debValue;
		}
	} else {
		if (diff >= dab->debBean->debOffMinTime_usec) {
			res = actualGPIOStatus;
		} else {
			res = dab->debBean->debValue;
		}
	}
	return sprintf(buf, "%d\n", res);
}

static ssize_t devAttrGpioDebMsOn_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct DeviceAttrBean
	*dab = container_of(attr, struct DeviceAttrBean,
			devAttr);

	return sprintf(buf, "%lu\n",
			dab->debBean->debOnMinTime_usec / 1000);
}

static ssize_t devAttrGpioDebMsOff_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct DeviceAttrBean
	*dab = container_of(attr, struct DeviceAttrBean, devAttr);

	return sprintf(buf, "%lu\n",
			dab->debBean->debOffMinTime_usec / 1000);
}

static ssize_t devAttrGpioDebMsOn_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	struct DeviceAttrBean
	* dab = container_of(attr, struct DeviceAttrBean, devAttr);
	unsigned int val;

	int ret = kstrtouint(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}
	dab->debBean->debOnMinTime_usec = val * 1000;
	dab->debBean->debOnStateCnt = 0;
	dab->debBean->debOffStateCnt = 0;
	dab->debBean->debValue = DEBOUNCE_STATE_NOT_DEFINED;
	return count;
}

static ssize_t devAttrGpioDebMsOff_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	struct DeviceAttrBean
	* dab = container_of(attr, struct DeviceAttrBean, devAttr);
	unsigned int val;

	int ret = kstrtouint(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}
	dab->debBean->debOffMinTime_usec = val * 1000;
	dab->debBean->debOnStateCnt = 0;
	dab->debBean->debOffStateCnt = 0;
	dab->debBean->debValue = DEBOUNCE_STATE_NOT_DEFINED;
	return count;
}

static ssize_t devAttrGpioDebOnCnt_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct DeviceAttrBean
	* dab = container_of(attr, struct DeviceAttrBean, devAttr);
	struct timespec64 now;
	unsigned long long diff;
	int actualGPIOStatus;
	unsigned long res;

	ktime_get_raw_ts64(&now);
	diff = diff_usec(
			(struct timespec64*) &dab->debBean->lastDebIrqTs, &now);

	actualGPIOStatus = gpio_get_value(dab->debBean->gpio);
	if (dab->debBean->debPastValue == actualGPIOStatus
			&& actualGPIOStatus
			&& diff >= dab->debBean->debOnMinTime_usec
			&& actualGPIOStatus != dab->debBean->debValue) {
		res = dab->debBean->debOnStateCnt + 1;
	} else {
		res = dab->debBean->debOnStateCnt;
	}

	return sprintf(buf, "%lu\n", res);
}

static ssize_t devAttrGpioDebOffCnt_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct DeviceAttrBean
	*dab = container_of(attr, struct DeviceAttrBean, devAttr);
	struct timespec64 now;
	unsigned long long diff;
	int actualGPIOStatus;
	unsigned long res;

	ktime_get_raw_ts64(&now);
	diff = diff_usec(
			(struct timespec64*) &dab->debBean->lastDebIrqTs, &now);

	actualGPIOStatus = gpio_get_value(dab->debBean->gpio);
	if (dab->debBean->debPastValue == actualGPIOStatus
			&& !actualGPIOStatus
			&& diff >= dab->debBean->debOffMinTime_usec
			&& actualGPIOStatus != dab->debBean->debValue) {
		res = dab->debBean->debOffStateCnt + 1;
	} else {
		res = dab->debBean->debOffStateCnt;
	}

	return sprintf(buf, "%lu\n", res);
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
	int i, ret;

	ret = -EBUSY;
	for (i = 0; i < 40; i++) {
		if (mutex_trylock(&mcp3204_spi_data->lock)) {
			ret = 1;
			break;
		}
		msleep(1);
	}

	if (ret < 0) {
		return ret;
	}

	memset(&mcp3204_spi_data->rx_buf, 0, sizeof(mcp3204_spi_data->rx_buf));
	mcp3204_spi_data->tx_buf = 0b1100000 | (channel << 2);

	ret = spi_sync(mcp3204_spi_data->spi, &mcp3204_spi_data->msg);
	if (ret >= 0) {
		ret = mcp3204_spi_data->rx_buf[0] << 4
				| mcp3204_spi_data->rx_buf[1] >> 4;
		if (mult > 0) {
			ret = ret * mult / 1000;
		}
	}

	mutex_unlock(&mcp3204_spi_data->lock);

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
MODULE_DEVICE_TABLE(of, ionopi_of_match);

static const struct spi_device_id ionopi_spi_ids[] = {
	{ "ionopi", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, ionopi_spi_ids);

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

static irqreturn_t gpio_deb_irq_handler(int irq, void *dev_id) {
	struct timespec64 now;
	int db = 0;
	unsigned long long diff;
	int actualGPIOStatus;

	ktime_get_raw_ts64(&now);

	while (debounceBeans[db].debIrqDevName != NULL) {
		if (debounceBeans[db].debIrqNum == irq && debounceBeans[db].gpio != 0) {
			actualGPIOStatus = gpio_get_value(debounceBeans[db].gpio);

			diff = diff_usec(
					(struct timespec64*) &debounceBeans[db].lastDebIrqTs, &now);

			if (debounceBeans[db].debPastValue == actualGPIOStatus) {
				return IRQ_HANDLED;
			}

			debounceBeans[db].debPastValue = actualGPIOStatus;

			if (actualGPIOStatus == debounceBeans[db].debValue
					|| debounceBeans[db].debValue == DEBOUNCE_STATE_NOT_DEFINED) {
				if (actualGPIOStatus) {
					if (diff >= debounceBeans[db].debOffMinTime_usec) {
						debounceBeans[db].debValue = 0;
						debounceBeans[db].debOffStateCnt++;
					}
				} else {
					if (diff >= debounceBeans[db].debOnMinTime_usec) {
						debounceBeans[db].debValue = 1;
						debounceBeans[db].debOnStateCnt++;
					}
				}
			}

			debounceBeans[db].lastDebIrqTs = now;
			break;
		}
		db++;
	}

	return IRQ_HANDLED;
}

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
				if (devices[di].devAttrBeans[ai].debBean != NULL) {
					free_irq(devices[di].devAttrBeans[ai].debBean->debIrqNum,
					NULL);
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

	ateccAddDriver();

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
			printk(KERN_ALERT "ionopi: * | failed to create device '%s'\n",
					devices[di].name);
			goto fail;
		}

		ai = 0;
		while (devices[di].devAttrBeans[ai].devAttr.attr.name != NULL) {
			result = device_create_file(devices[di].pDevice,
					&devices[di].devAttrBeans[ai].devAttr);
			if (result) {
				printk(
				KERN_ALERT "ionopi: * | failed to create device file '%s/%s'\n",
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
					result = gpio_direction_output(
							devices[di].devAttrBeans[ai].gpio, false);
				} else {
					result = gpio_direction_input(
							devices[di].devAttrBeans[ai].gpio);
				}
				if (result) {
					printk(KERN_ALERT "ionopi: * | error setting up GPIO %d\n",
							devices[di].devAttrBeans[ai].gpio);
					goto fail;
				}
				gpio_export(devices[di].devAttrBeans[ai].gpio, false);
			}
			if (devices[di].devAttrBeans[ai].debBean != NULL) {
				if (!devices[di].devAttrBeans[ai].debBean->debIrqNum) {
					devices[di].devAttrBeans[ai].debBean->debIrqNum =
							gpio_to_irq(
									devices[di].devAttrBeans[ai].debBean->gpio);
					if (request_irq(
							devices[di].devAttrBeans[ai].debBean->debIrqNum,
							(void *) gpio_deb_irq_handler,
							IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
							devices[di].devAttrBeans[ai].debBean->debIrqDevName,
							NULL)) {
						printk(
								KERN_ALERT "ionopi: * | cannot register IRQ of %s in device %s\n",
								devices[di].devAttrBeans[ai].devAttr.attr.name,
								devices[di].name);
						goto fail;
					}
					ktime_get_raw_ts64(
							&devices[di].devAttrBeans[ai].debBean->lastDebIrqTs);
					devices[di].devAttrBeans[ai].debBean->debValue =
					DEBOUNCE_STATE_NOT_DEFINED;
					devices[di].devAttrBeans[ai].debBean->debPastValue =
							gpio_get_value(
									devices[di].devAttrBeans[ai].debBean->gpio);
				}
			}
			ai++;
		}
		di++;
	}

	wiegandAdd(&w1);
	wiegandAdd(&w2);

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

module_init(ionopi_init);
module_exit(ionopi_exit);

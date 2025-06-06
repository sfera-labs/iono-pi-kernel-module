/*
 * Iono Pi kernel module
 *
 *     Copyright (C) 2020-2025 Sfera Labs S.r.l.
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
#include "gpio/gpio.h"
#include "wiegand/wiegand.h"
#include "atecc/atecc.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/version.h>

#define AI1_AI2_FACTOR 		7319
#define AI3_AI4_FACTOR 		725

#define AI1_MCP_CHANNEL 	1
#define AI2_MCP_CHANNEL 	0
#define AI3_MCP_CHANNEL 	2
#define AI4_MCP_CHANNEL 	3

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sfera Labs - http://sferalabs.cc");
MODULE_DESCRIPTION("Iono Pi driver module");
MODULE_VERSION("1.24");

#define LOG_TAG "ionopi: "

struct DeviceAttrBean {
	struct device_attribute devAttr;
	struct GpioBean *gpio;
};

struct DeviceBean {
	char *name;
	struct device *pDevice;
	struct DeviceAttrBean *devAttrBeans;
};

static struct class *pDeviceClass;

static ssize_t devAttrAi1Mv_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t devAttrAi2Mv_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t devAttrAi3Mv_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t devAttrAi4Mv_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t devAttrAi1Raw_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t devAttrAi2Raw_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t devAttrAi3Raw_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t devAttrAi4Raw_show(struct device *dev,
		struct device_attribute *attr, char *buf);

enum digInEnum {
	DI1 = 0,
	DI2,
	DI3,
	DI4,
	DI5,
	DI6,
	DI_SIZE,
};

enum relayEnum {
	O1 = 0,
	O2,
	O3,
	O4,
	O_SIZE,
};

enum ocEnum {
	OC1 = 0,
	OC2,
	OC3,
	OC_SIZE,
};

enum ttlEnum {
	TTL1 = 0,
	TTL2,
	TTL3,
	TTL4,
	TTL_SIZE,
};

static struct GpioBean gpioLed = {
	.name = "ionopi_led",
	.flags = GPIOD_OUT_LOW,
};

static struct DebouncedGpioBean gpioDI[] = {
	[DI1] = {
		.gpio = {
			.name = "ionopi_di1",
			.flags = GPIOD_IN,
		},
	},
	[DI2] = {
		.gpio = {
			.name = "ionopi_di2",
			.flags = GPIOD_IN,
		},
	},
	[DI3] = {
		.gpio = {
			.name = "ionopi_di3",
			.flags = GPIOD_IN,
		},
	},
	[DI4] = {
		.gpio = {
			.name = "ionopi_di4",
			.flags = GPIOD_IN,
		},
	},
	[DI5] = {
		.gpio = {
			.name = "ionopi_di5",
			.flags = GPIOD_IN,
		},
	},
	[DI6] = {
		.gpio = {
			.name = "ionopi_di6",
			.flags = GPIOD_IN,
		},
	},
};

static struct GpioBean gpioO[] = {
	[O1] = {
		.name = "ionopi_o1",
		.flags = GPIOD_OUT_LOW,
	},
	[O2] = {
		.name = "ionopi_o2",
		.flags = GPIOD_OUT_LOW,
	},
	[O3] = {
		.name = "ionopi_o3",
		.flags = GPIOD_OUT_LOW,
	},
	[O4] = {
		.name = "ionopi_o4",
		.flags = GPIOD_OUT_LOW,
	},
};

static struct GpioBean gpioOC[] = {
	[OC1] = {
		.name = "ionopi_oc1",
		.flags = GPIOD_OUT_LOW,
	},
	[OC2] = {
		.name = "ionopi_oc2",
		.flags = GPIOD_OUT_LOW,
	},
	[OC3] = {
		.name = "ionopi_oc3",
		.flags = GPIOD_OUT_LOW,
	},
};

static struct GpioBean gpioTtl[] = {
	[TTL1] = {
		.name = "ionopi_ttl1",
	},
	[TTL2] = {
		.name = "ionopi_ttl2",
	},
	[TTL3] = {
		.name = "ionopi_ttl3",
	},
	[TTL4] = {
		.name = "ionopi_ttl4",
	},
};

static struct WiegandBean w1 = {
	.d0 = {
		.gpio = &gpioTtl[TTL1],
	},
	.d1 = {
		.gpio = &gpioTtl[TTL2],
	},
};

static struct WiegandBean w2 = {
	.d0 = {
		.gpio = &gpioTtl[TTL3],
	},
	.d1 = {
		.gpio = &gpioTtl[TTL4],
	},
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
		.gpio = &gpioLed,
	},

	{
		.devAttr = {
			.attr = {
				.name = "blink",
				.mode = 0220,
			},
			.store = devAttrGpioBlink_store,
		},
		.gpio = &gpioLed,
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
		.gpio = &gpioDI[DI1].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di2",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
		},
		.gpio = &gpioDI[DI2].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di3",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
		},
		.gpio = &gpioDI[DI3].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di4",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
		},
		.gpio = &gpioDI[DI4].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di5",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
		},
		.gpio = &gpioDI[DI5].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di6",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
		},
		.gpio = &gpioDI[DI6].gpio,
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
		.gpio = &gpioDI[DI1].gpio,
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
		.gpio = &gpioDI[DI2].gpio,
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
		.gpio = &gpioDI[DI3].gpio,
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
		.gpio = &gpioDI[DI4].gpio,
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
		.gpio = &gpioDI[DI5].gpio,
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
		.gpio = &gpioDI[DI6].gpio,
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
		.gpio = &gpioDI[DI1].gpio,
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
		.gpio = &gpioDI[DI1].gpio,
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
		.gpio = &gpioDI[DI2].gpio,
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
		.gpio = &gpioDI[DI2].gpio,
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
		.gpio = &gpioDI[DI3].gpio,
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
		.gpio = &gpioDI[DI3].gpio,
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
		.gpio = &gpioDI[DI4].gpio,
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
		.gpio = &gpioDI[DI4].gpio,
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
		.gpio = &gpioDI[DI5].gpio,
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
		.gpio = &gpioDI[DI5].gpio,
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
		.gpio = &gpioDI[DI6].gpio,
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
		.gpio = &gpioDI[DI6].gpio,
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
		.gpio = &gpioDI[DI1].gpio,
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
		.gpio = &gpioDI[DI1].gpio,
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
		.gpio = &gpioDI[DI2].gpio,
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
		.gpio = &gpioDI[DI2].gpio,
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
		.gpio = &gpioDI[DI3].gpio,
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
		.gpio = &gpioDI[DI3].gpio,
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
		.gpio = &gpioDI[DI4].gpio,
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
		.gpio = &gpioDI[DI4].gpio,
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
		.gpio = &gpioDI[DI5].gpio,
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
		.gpio = &gpioDI[DI5].gpio,
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
		.gpio = &gpioDI[DI6].gpio,
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
		.gpio = &gpioDI[DI6].gpio,
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
		.gpio = &gpioO[O1],
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
		.gpio = &gpioO[O2],
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
		.gpio = &gpioO[O3],
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
		.gpio = &gpioO[O4],
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
		.gpio = &gpioOC[OC1],
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
		.gpio = &gpioOC[OC2],
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
		.gpio = &gpioOC[OC3],
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

struct GpioBean* gpioGetBean(struct device *dev, struct device_attribute *attr) {
	struct DeviceAttrBean *dab;
	dab = container_of(attr, struct DeviceAttrBean, devAttr);
	if (dab == NULL) {
		return NULL;
	}
	return dab->gpio;
}

struct WiegandBean* wiegandGetBean(struct device *dev,
		struct device_attribute *attr) {
	if (attr->attr.name[1] == '1') {
		return &w1;
	} else {
		return &w2;
	}
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

	if (ret < 0) {
		return ret;
	}

	return sprintf(buf, "%d\n", ret);
}

static ssize_t devAttrAi1Mv_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	return devAttrMcp3204_show(buf, AI1_MCP_CHANNEL, AI1_AI2_FACTOR);
}

static ssize_t devAttrAi2Mv_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	return devAttrMcp3204_show(buf, AI2_MCP_CHANNEL, AI1_AI2_FACTOR);
}

static ssize_t devAttrAi3Mv_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	return devAttrMcp3204_show(buf, AI3_MCP_CHANNEL, AI3_AI4_FACTOR);
}

static ssize_t devAttrAi4Mv_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	return devAttrMcp3204_show(buf, AI4_MCP_CHANNEL, AI3_AI4_FACTOR);
}

static ssize_t devAttrAi1Raw_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	return devAttrMcp3204_show(buf, AI1_MCP_CHANNEL, 0);
}

static ssize_t devAttrAi2Raw_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	return devAttrMcp3204_show(buf, AI2_MCP_CHANNEL, 0);
}

static ssize_t devAttrAi3Raw_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	return devAttrMcp3204_show(buf, AI3_MCP_CHANNEL, 0);
}

static ssize_t devAttrAi4Raw_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	return devAttrMcp3204_show(buf, AI4_MCP_CHANNEL, 0);
}

static int mcp3204_spi_probe(struct spi_device *spi) {
	int ret;

	mcp3204_spi_data =
			devm_kzalloc(&spi->dev, sizeof(struct mcp3204_data), GFP_KERNEL);
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

	pr_info(LOG_TAG "mcp3204 probed\n");

	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,0,0)
static int mcp3204_spi_remove(struct spi_device *spi) {
#else
static void mcp3204_spi_remove(struct spi_device *spi) {
#endif
	struct mcp3204_data *data = spi_get_drvdata(spi);

	regulator_disable(data->reg);
	mutex_destroy(&data->lock);

	pr_info(LOG_TAG "mcp3204 removed\n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,0,0)
	return 0;
#endif
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
	struct DeviceBean *db;
	struct DeviceAttrBean *dab;
	int i, di, ai;

	spi_unregister_driver(&mcp3204_spi_driver);

	di = 0;
	while (devices[di].name != NULL) {
		if (devices[di].pDevice && !IS_ERR(devices[di].pDevice)) {
			db = &devices[di];
			ai = 0;
			while (db->devAttrBeans[ai].devAttr.attr.name != NULL) {
				dab = &db->devAttrBeans[ai];
				device_remove_file(db->pDevice, &dab->devAttr);
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

	gpioFree(&gpioLed);
	for (i = 0; i < DI_SIZE; i++) {
		gpioFreeDebounce(&gpioDI[i]);
	}
	for (i = 0; i < O_SIZE; i++) {
		gpioFree(&gpioO[i]);
	}
	for (i = 0; i < OC_SIZE; i++) {
		gpioFree(&gpioOC[i]);
	}
}

static int ionopi_init(struct platform_device *pdev) {
	struct DeviceBean *db;
	struct DeviceAttrBean *dab;
	int i, di, ai;

	pr_info(LOG_TAG "init\n");

	gpioSetPlatformDev(pdev);

	if (spi_register_driver(&mcp3204_spi_driver)) {
		pr_err(LOG_TAG "failed to register mcp3204 driver\n");
		goto fail;
	}

	if (gpioInit(&gpioLed)) {
		pr_err(LOG_TAG "error setting up GPIO %s\n", gpioLed.name);
		goto fail;
	}
	for (i = 0; i < DI_SIZE; i++) {
		if (gpioInitDebounce(&gpioDI[i])) {
			pr_err(LOG_TAG "error setting up GPIO %s\n", gpioDI[i].gpio.name);
			goto fail;
		}
	}
	for (i = 0; i < O_SIZE; i++) {
		if (gpioInit(&gpioO[i])) {
			pr_err(LOG_TAG "error setting up GPIO %s\n", gpioO[i].name);
			goto fail;
		}
	}
	for (i = 0; i < OC_SIZE; i++) {
		if (gpioInit(&gpioOC[i])) {
			pr_err(LOG_TAG "error setting up GPIO %s\n", gpioOC[i].name);
			goto fail;
		}
	}

	wiegandInit(&w1);
	wiegandInit(&w2);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,5,0)
	pDeviceClass = class_create("ionopi");
#else
	pDeviceClass = class_create(THIS_MODULE, "ionopi");
#endif

	if (IS_ERR(pDeviceClass)) {
		pr_err(LOG_TAG "failed to create device class\n");
		goto fail;
	}

	di = 0;
	while (devices[di].name != NULL) {
		db = &devices[di];
		db->pDevice = device_create(pDeviceClass, NULL, 0, NULL, db->name);
		if (IS_ERR(db->pDevice)) {
			pr_err(LOG_TAG "failed to create device '%s'\n", db->name);
			goto fail;
		}

		ai = 0;
		while (db->devAttrBeans[ai].devAttr.attr.name != NULL) {
			dab = &db->devAttrBeans[ai];
			if (device_create_file(db->pDevice, &dab->devAttr)) {
				pr_err(LOG_TAG "failed to create device file '%s/%s'\n",
						db->name, dab->devAttr.attr.name);
				goto fail;
			}
			ai++;
		}
		di++;
	}

	pr_info(LOG_TAG "ready\n");
	return 0;

	fail:
	pr_err(LOG_TAG "init failed\n");
	cleanup();
	return -1;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
static void ionopi_exit(struct platform_device *pdev) {
#else
static int ionopi_exit(struct platform_device *pdev) {
#endif
  cleanup();
  pr_info(LOG_TAG "exit\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
  return 0;
#endif
}

static struct platform_driver ionopi_driver = {
	.probe = ionopi_init,
	.remove = ionopi_exit,
	.driver = {
		.name = "ionopi_driver",
		.owner = THIS_MODULE,
		.of_match_table = ionopi_of_match,
	}
};

module_platform_driver(ionopi_driver);

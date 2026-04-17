obj-m += ionopi.o

ionopi-objs := module.o
ionopi-objs += commons/utils/utils.o
ionopi-objs += commons/gpio/gpio.o
ionopi-objs += commons/wiegand/wiegand.o
ionopi-objs += commons/atecc/atecc.o

MODULE_NAME := ionopi
MODULE_VERSION_DEFINE := IONOPI_MODULE_VERSION
DTS_NAME := ionopi
UDEV_RULES := 99-ionopi.rules

include commons/scripts/kmod-common.mk

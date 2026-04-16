obj-m += ionopi.o

ionopi-objs := module.o
ionopi-objs += commons/commons.o
ionopi-objs += gpio/gpio.o
ionopi-objs += wiegand/wiegand.o
ionopi-objs += atecc/atecc.o

SOURCE_DIR := $(if $(src),$(src),$(CURDIR))
IONOPI_VERSION := $(strip $(shell cat $(SOURCE_DIR)/VERSION))
ccflags-y += -DIONOPI_MODULE_VERSION=\"$(IONOPI_VERSION)\"

KVER ?= $(if $(KERNELRELEASE),$(KERNELRELEASE),$(shell uname -r))
KDIR ?= /lib/modules/$(KVER)/build

all: dtbo
	make -C $(KDIR) M=$(PWD) modules

dtbo: ionopi.dts
	dtc -@ -Hepapr -I dts -O dtb -o ionopi.dtbo ionopi.dts

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f ionopi.dtbo

install:
	sudo install -D -m 644 -c ionopi.ko /lib/modules/$(KVER)/updates/dkms/ionopi.ko
	sudo depmod
	sudo $(MAKE) install-extra

install-extra: dtbo
	install -D -m 644 -c ionopi.dtbo /boot/overlays/ionopi.dtbo
	install -D -m 644 -c 99-ionopi.rules /etc/udev/rules.d/99-ionopi.rules
	udevadm control --reload-rules || true
	udevadm trigger || true

uninstall-extra:
	rm -f /boot/overlays/ionopi.dtbo
	rm -f /etc/udev/rules.d/99-ionopi.rules
	udevadm control --reload-rules || true
	udevadm trigger || true

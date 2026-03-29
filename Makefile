# Module name
obj-m += ionopi.o

# Object files
ionopi-objs := module.o
ionopi-objs += commons/commons.o
ionopi-objs += gpio/gpio.o
ionopi-objs += wiegand/wiegand.o
ionopi-objs += atecc/atecc.o

# Kernel build directory
KDIR := /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

install:
	@echo "Installing ionopi.ko into /lib/modules/$(shell uname -r)/extra/"
	sudo install -d /lib/modules/$(shell uname -r)/extra
	sudo install -m 644 -c ionopi.ko /lib/modules/$(shell uname -r)/extra/
	sudo depmod
	@echo "Module installed. Load with: sudo modprobe ionopi"

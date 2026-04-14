obj-m += ionopi.o

ionopi-objs := module.o
ionopi-objs += commons/commons.o
ionopi-objs += gpio/gpio.o
ionopi-objs += wiegand/wiegand.o
ionopi-objs += atecc/atecc.o

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules
	dtc -@ -Hepapr -I dts -O dtb -o ionopi.dtbo ionopi.dts

clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean
	rm -f ionopi.dtbo

install:
	sudo install -m 644 -c ionopi.ko /lib/modules/$(shell uname -r)
	sudo depmod
	sudo cp ionopi.dtbo /boot/overlays/

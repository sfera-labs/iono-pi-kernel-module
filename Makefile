obj-m += ionopi.o

ionopi-objs := module.o
ionopi-objs += atecc/atecc.o

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean

install:
	sudo install -m 644 -c ionopi.ko /lib/modules/$(shell uname -r)
	sudo depmod

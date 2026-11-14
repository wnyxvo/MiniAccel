# M1-D0: external module build; requires headers for the RUNNING kernel.
obj-m += mini_accel.o
mini_accel-y := miniaccel_main.o miniaccel_ioctl.o
ccflags-y += -I$(src)/../uapi

.PHONY: all clean
all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) modules
clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean

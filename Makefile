ifeq ($(KBUILD_MODULES), 1)
obj-m += bhfs.o
bhfs-objs += super.o
else
knl_src := /lib/modules/$(shell uname -r)/build
cur_src := $(shell pwd)

.PHONE : all clean install
all :
	make -C $(knl_src) M=$(cur_src) modules
clean :
	make -C $(knl_src) M=$(cur_src) clean
install :
	make -C $(knl_src) M=$(cur_src) modules_install
endif

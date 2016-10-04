echo $(KBUILD_EXTMOD)
echo $(KBUILD_MODULES)

ifeq ($(KBUILD_MODULES), 1)
obj-y += bhfs.o
bhfs-objs += super.o
else
knl_src=$(HOME)/code/linux
cur_src=$(shell pwd)
.PHONE : all clean install
all :
	make -C $(knl_src) M=$(cur_src) modules
clean :
	make -C $(knl_src) M=$(cur_src) clean
install :
	make -C $(knl_src) M=$(cur_src) modules_install
endif

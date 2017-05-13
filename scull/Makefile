
#LINUX2.6
obj-m = scull.o
obj-dir = /opt/_install/_file/scull
KERNEL_SRC = /opt/kernel/kernel

linux:
	make -C $(KERNEL_SRC) ARCH=arm M=`pwd` modules
	mv scull.ko $(obj-dir)

clean:
	make -C $(KERNEL_SRC) ARCH=arm M=`pwd` clean
	rm -f *.ko *.o *.mod.o *.mod.c *.symvers modul* *.bin


#LINUX2.6
#obj-m = scull_pipe.o
#obj-dir = /opt/_install/_file/scull_pipe
#KERNEL_SRC = /opt/kernel/kernel

#Linux3.6
obj-m=scull_pipe.o
obj-dir = /opt/_install/_file/scull_pipe
KERNEL_SRC=~/software-new/software/linux


linux:
	make -C $(KERNEL_SRC) ARCH=arm M=`pwd` modules
	mv scull_pipe.ko $(obj-dir)

clean:
	make -C $(KERNEL_SRC) ARCH=arm M=`pwd` clean
	rm -f *.ko *.o *.mod.o *.mod.c *.symvers modul* *.bin

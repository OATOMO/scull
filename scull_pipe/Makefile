#cross compiler makefile for FIFO DMA example
KERN_SRC=/home/atom/software-new/software/linux
obj-m += scull_pipe.o

linux_3.6:
	make -C $(KERN_SRC) ARCH=arm M=`pwd` modules
	mv scull_pipe.ko /opt/_file/dri/scull
clean:
	make -C $(KERN_SRC) ARCH=arm M=`pwd` clean
	rm -f *.ko *.o *.mod.o *.mod.c *.symvers  modul* *.bin

linux_2.6:
	make -C /opt/kernel ARCH=arm M=`pwd` modules
	

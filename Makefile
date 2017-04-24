#cross compiler makefile for FIFO DMA example
KERN_SRC=/home/atom/software-new/software/linux
obj-m += scull.o

linux_3.6:
	make -C $(KERN_SRC) ARCH=arm M=`pwd` modules
clean:
	make -C $(KERN_SRC) ARCH=arm M=`pwd` clean
	rm -f *.ko *.o *.mod.o *.mod.c *.symvers  modul*

linux_2.6:
	make -C /opt/kernel ARCH=arm M=`pwd` modules
	

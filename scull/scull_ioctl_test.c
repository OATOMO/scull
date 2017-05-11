#include <stdio.h>
#include <fcntl.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#define SCULL_IOC_MAGIC 'Q' //幻数 ‘Q’


#define SCULL_IOCSQUANTUM 	_IOW(SCULL_IOC_MAGIC,1,int)
#define SCULL_IOCSQSET 		_IOW(SCULL_IOC_MAGIC,2,int)
#define SCULL_IOCTQUANTUM	_IO (SCULL_IOC_MAGIC,3)
#define SCULL_IOCTQSET		_IO (SCULL_IOC_MAGIC,4)
#define SCULL_IOCGQUANTUM  	_IOR(SCULL_IOC_MAGIC,5,int)
#define SCULL_IOCGQSET		_IOR(SCULL_IOC_MAGIC,6,int)
#define SCULL_IOCQQUANTUM	_IO (SCULL_IOC_MAGIC,7)
#define SCULL_IOCQQSET 		_IO (SCULL_IOC_MAGIC,8)
#define SCULL_IOCHQUANTUM 	_IO (SCULL_IOC_MAGIC,11)
#define SCULL_IOCHQSET  	_IO (SCULL_IOC_MAGIC,12)
#define SCULL_IOCXQUANTUM 	_IOWR(SCULL_IOC_MAGIC,9,int)
#define SCULL_IOCXQSET		_IOWR(SCULL_IOC_MAGIC,10,int)


void main(){
	int fd = open("/dev/scull",O_RDWR);
	int retval;
	char s[10];

	if(fd < 0){
		printf("****open faile**********\n");
	}
	int arg = 1024*1;
	ioctl(fd,SCULL_IOCSQUANTUM,&arg);
	arg = 1000;
	ioctl(fd,SCULL_IOCSQSET,&arg);

close(fd);
}

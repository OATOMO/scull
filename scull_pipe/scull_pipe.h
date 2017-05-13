/*
 *file : scull_pipe.h
 *Atom
 * */
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>		//注册卸载proc设备的函数
#include <linux/seq_file.h>
#include <linux/ioctl.h>
#include <linux/capability.h>  		//提供权能操作
#include <linux/kernel.h>
#include <linux/sched.h>

#define SCULL_NR_DEVS  1 //设备数
#define SCULL_MAJOR  0   //dynamic alloc dev_t
#define SCULL_MINOR  0   
#define SCULL_BUFFERSIZE (1024 * 2) //设备缓存大小  

#define PDEBUG(fmt,args...) \
		printk("<7>"fmt,## args)

struct scull_pipe{
	wait_queue_head_t inq,outq;			/*读取和写入队列*/
	char * buffer,* end;				/*缓存区的起始和结尾*/
	int buffersize;						/*用于指针计算*/	
	char *rp,*wp;						/*读取和写入的位置*/
	int nreaders,nwriters;				/*用于读写打开的数量*/
	struct fasync_struct *async_queue;	/*异步读取者*/	
	struct semaphore sem;				/*互斥信号量*/
	struct cdev cdev;					/*字符设备*/
};



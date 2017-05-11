/* 
 * Atom
 * 实现一个类似管道的缓存区
 */
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

#define SCULL_NR_DEVS  1 //设备数
#define SCULL_MAJOR  0   //dynamic alloc dev_t
#define SCULL_MINOR  0   //dynamic alloc dev_t


static dev_t scull_major = SCULL_MAJOR;
static dev_t scull_minor = SCULL_MINOR;
static int scull_nr_devs = SCULL_NR_DEVS;

static struct scull_pipe * scull_pipe;

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

//read
static int scull_p_read(struct file * filp,struct __user *buf,size_t count,loff_t * f_ops){
	struct scull_pipe *dev = filp->private_data;
}


//open
static int scull_p_open(struct inode * inode,struct file * filp){
	struct scull_pipe * dev;
	dev = container_of(inode->i_cdev,struct scull_pipe,cdev);
	filp->private_data = dev;

	printk("<6>""\n****************** open scull_pipe ***********************\n");
	return 0;
}

//clean
static int scull_p_clean(struct inode * inode,struct file * filp){
	filp->private_data = NULL;
	return 0;
}

//f_ops
static struct file_operations scull_fops = {
	.open 	= scull_p_open,
	.release= scull_p_clean,
	.write	= scull_p_write,
	.read  	= scull_p_read
};

static int __init scull_p_init(void){
return 0;
}

static void __exit scull_p_exit(void){
}

module_init(scull_p_init);
module_exit(scull_p_exit);
MODULE_LICENSE("GPL");


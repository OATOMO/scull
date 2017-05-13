/* 
 * Atom
 * 实现一个类似管道的缓存区
 */
#include "scull_pipe.h"


static dev_t scull_major = SCULL_MAJOR;
static dev_t scull_minor = SCULL_MINOR;
static int scull_nr_devs = SCULL_NR_DEVS;

static struct scull_pipe * scull_pipe;

//spacefree
static int spacefree(struct scull_pipe * dev){
	if(dev->rp == dev->wp)
		return dev->buffersize - 1;
	return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize) - 1;
}

//scull_p_getwritespace
static int scull_p_getwritespace(struct scull_pipe * dev,struct file * filp){
	while(spacefree(dev) == 0){/*内存满了*/
		DEFINE_WAIT(wait);

		up(&dev->sem);	/*要休眠的话先释放信号量*/	
		if(filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		PDEBUG("\"%s\" writing:going to sleep\n",current->comm);
		prepare_to_wait(&dev->outq,&wait,TASK_INTERRUPTIBLE);
		if(spacefree(dev) == 0)
			schedule();		//真正的休眠
		/*唤醒之后*/
		finish_wait(&dev->outq,&wait);
		if(signal_pending(current))
			return -ERESTARTSYS;
		if(down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}	
return 0;
}

//write
static int scull_p_write(struct file * filp,const char __user *buf,size_t count,loff_t * f_pos){
	struct scull_pipe * dev = filp->private_data;
	int result;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	/*确保有空间可写入*/
	result = scull_p_getwritespace(dev,filp);
	if(result)
		return result;
	/*有空间可用,写入数据*/
	count = min(count,(size_t)spacefree(dev));
	if(dev->wp >= dev->rp)
		count = min(count,(size_t)(dev->end - dev->wp));
	else
		count = min(count,(size_t)(dev->rp - dev->wp - 1));
	PDEBUG("Going to accept %li bytes to 0x%p from 0x%p\n",(long)count,dev->wp,buf);
	if(copy_from_user(dev->wp,buf,count)){
		up(&dev->sem);
		return -EFAULT;
	}
	dev->wp += count;
	if(dev->wp == dev->end)
		dev->wp = dev->buffer;
	up(&dev->sem);
	/*唤醒读取者*/
	wake_up_interruptible(&dev->inq); /*阻塞在read()和select()上*/

	/*通知异步读取者*/
#if 0
	if(dev->async_queue)
		kill_fasync(&dev->async_queue,SIGIO,POLL_IN);
#endif
PDEBUG("\"%s\" did write %li butes\n",current->comm,(long)count);

return count;
}

//read
static int scull_p_read(struct file * filp,char __user *buf,size_t count,loff_t * f_pos){
	struct scull_pipe * dev = filp->private_data;
	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	while(dev->rp == dev->wp){ /*没有数据可以读取*/
		/*没有数据可以读取时休眠,休眠时不能持有信号量*/
		up(&dev->sem);
		if(filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		PDEBUG("\"%s\" reading :going to sleep\n",current->comm);
		if(wait_event_interruptible(dev->inq,dev->rp != dev->wp))
			return -ERESTARTSYS;
		/*唤醒后先获取锁*/
		if(down_interruptible(&dev->sem))
			return -ERESTARTSYS;	
	}
	/*数据已就绪返回*/
	if(dev->wp > dev->rp)
		count = min(count,(size_t)(dev->wp - dev->rp));
	else	/*写入指针回卷,返回数据直到end*/ 
		count = min(count,(size_t)(dev->end - dev->rp));
	if(copy_to_user(buf,dev->rp,count)){
		up(&dev->sem);		/*指针无效就释放信号量并返回*/
		return -EFAULT;
	}
	dev->rp += count;
	if(dev->rp == dev->end)
		dev->rp = dev->buffer; /*回卷*/
	up(&dev->sem); //主要操作完成后释放信号量
		
	/*唤醒所有写入者*/
	wake_up_interruptible(&dev->outq);
	PDEBUG("\"%s\" did read %li bytes\n",current->comm,(long)count);
return count;
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
	printk("<6>""\n***************** clean scull_pipe ***********************\n");
	return 0;
}

//f_ops
static struct file_operations scull_pipe_fops = {
	.open 	= scull_p_open,
	.release= scull_p_clean,
	.write	= scull_p_write,
	.read  	= scull_p_read
};


/*主要负责初始化scull_pipe结构体中的cdev字符设备*/
static void scull_pipe_setup_cdev(struct scull_pipe * device,int index){
	int err;
	dev_t devno = MKDEV(scull_major,scull_minor + index);
	struct scull_pipe * dev = device + index;

	cdev_init(&(dev->cdev),&scull_pipe_fops);
	dev->cdev.owner = THIS_MODULE;
	sema_init(&dev->sem,1);
	init_waitqueue_head(&dev->inq);
	init_waitqueue_head(&dev->outq);
	dev->buffer = kmalloc(SCULL_BUFFERSIZE,GFP_KERNEL);
	dev->buffersize = SCULL_BUFFERSIZE;
	dev->end = dev->buffer + dev->buffersize;	
	dev->wp = dev->buffer;
	dev->rp = dev->buffer;
	dev->nreaders = 0;
	dev->nwriters = 0;
#if 0
	dev->async_queue = fasync_alloc();
#endif 

	err = cdev_add(&dev->cdev,devno,1);
	if(err){
		printk("<3>""Error <%d>,adding scull_pipe[%d]",err,index);
		/*...待调用清除函数*/
	}
}

static void scull_pipe_uninit(struct scull_pipe * device,int index){
	dev_t devno = MKDEV(scull_major,scull_minor + index);
	struct scull_pipe * dev = device + index;
	cdev_del(&dev->cdev);		//卸载字符设备
}

static int __init scull_p_init(void){
	int i,retval;
	dev_t devno;
	if (scull_major){
		devno = MKDEV(scull_major,scull_minor);
		register_chrdev_region(devno,scull_nr_devs,"scull_pipe");	
	}
	else{
		retval = alloc_chrdev_region(&devno,scull_minor,scull_nr_devs,"scull_pipe");	
		scull_major = MAJOR(devno);
		scull_minor = MINOR(devno);
	}
	scull_pipe = kmalloc(sizeof(struct scull_pipe) * scull_nr_devs,GFP_KERNEL);

	for (i = 0;i < scull_nr_devs;i++){
		scull_pipe_setup_cdev(scull_pipe,i);
	}
return 0;
}

static void __exit scull_p_exit(void){
	int i;
	dev_t devno = MKDEV(scull_major,scull_minor);
	for (i = 0;i < scull_nr_devs;i ++){
		scull_pipe_uninit(scull_pipe,i);
	}
	unregister_chrdev_region(devno,scull_nr_devs);  //释放设备号
}

module_init(scull_p_init);
module_exit(scull_p_exit);
MODULE_LICENSE("GPL");


#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/slab.h>

#define SCULL_MAJOR  0   //dynamic alloc dev_t
#define SCULL_MINOR  0   //dynamic alloc dev_t
#define SCULL_NR_DEVS  3 //设备数
#define SCULL_QUANTUM   (4*1024)	//量子大小
#define SCULL_QSET  	(1000*4)		//量子集大小

static dev_t scull_major = SCULL_MAJOR;
static dev_t scull_minor = SCULL_MINOR;
static int scull_nr_devs = SCULL_NR_DEVS;

static struct scull_dev * scull_dev;

struct scull_qset{
	void ** data;
	struct scull_qset * next;
};//scull node

struct scull_dev{  
	struct scull_qset * data;
	int quantum; 		//量子大小
	int qset;			//数组大小
	unsigned long size;	//数据总量
	unsigned int access_key; //由sculluid和scullpriv使用
	struct semaphore sem;//互斥信号量
	struct cdev cdev;
	loff_t * pwf_pos;
	loff_t * prf_pos;
};//scull device

//trim
static void scull_trim(struct scull_dev * dev){
	struct scull_qset * dptr,* next;
	int qset = dev->qset,i;
	for(dptr = dev->data; dptr; dptr = next){
		if(dptr->data){
			for(i = 0;i < qset ;i++){
				kfree(dptr->data[i]);
			}
			kfree(dptr->data);
			dptr->data = NULL;
		}
		next = dptr->next;
		kfree(dptr);
	}
}

static struct scull_qset * scull_follow(struct scull_dev * dev,int item){
	struct scull_qset * dptr = dev->data;
	int i;
	for(i = 0;i < item;i++){
		dptr = dptr->next;
	}

return dptr;
}

//write
static ssize_t scull_write(struct file * filp,const char __user * buf,size_t count ,loff_t * f_pos){
	struct scull_dev * dev = filp->private_data;
	struct scull_qset * dptr;
	int quantum = dev->quantum,qset = dev->qset;
	int itemsize = quantum * qset;
	int item,rest,s_pos,q_pos;
	int retval = 0;
	if(down_interruptible(&dev->sem))
		{	
			printk("semaphore down fualt\n");
			return -ERESTARTSYS;
		}

	*f_pos = *dev->pwf_pos;
	printk("write *f_pos  = %lld\n",*f_pos);
	item  = (long )*f_pos/itemsize;
	rest  = (long )*f_pos%itemsize;
	s_pos = rest/quantum;
	q_pos = rest%quantum;
	
	dptr  = scull_follow(dev,item);
	if(!dptr){
		goto out;
	}

	if(!dptr->data){
		printk("try malloc qset pointer\n");
		dptr->data = kmalloc((qset * sizeof(char *)),GFP_KERNEL);
		if(!dptr->data){
			printk("malloc qset pointer fault T_T\n");
			goto out;
		}
		memset(dptr->data,0,qset*sizeof(char *));
	}
	
	if(!dptr->data[s_pos]){
		printk("try malloc quantum pointer\n");
		dptr->data[s_pos] = kmalloc(quantum,GFP_KERNEL);
		if(!dptr->data[s_pos]){
			printk("malloc quantum pointer fault T_T\n");
			goto out;
		}
	}

	if(count > quantum - q_pos){
		count = quantum - q_pos;
	}

	if(copy_from_user(dptr->data[s_pos] + q_pos,buf,count)){
		retval = -EFAULT;
		goto out;
	}

	*dev->pwf_pos += count;
	*f_pos = *dev->pwf_pos;
	retval = count;


	if(dev->size < *f_pos){
		dev->size = *f_pos;
	}
out:
	up(&dev->sem);
	return retval;
}

//read
static ssize_t scull_read(struct file * filp,char __user * buf,size_t count,loff_t * f_pos){
	struct scull_dev * dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = dev->quantum,qset = dev->qset;
	int itemsize = quantum * qset;
	int item,rest,s_pos,q_pos;
	size_t retval = 0;

	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	

	*f_pos = *dev->prf_pos;
	printk("read *f_pos  = %lld\n",*f_pos);
	if(*f_pos > dev->size)
		goto out;
	if(count > (dev->size - *f_pos))
		count = dev->size - *f_pos;
	
	item = (long)*f_pos/itemsize;
	rest = (long)*f_pos%itemsize;
	s_pos = rest/quantum;
	q_pos = rest%quantum;

	dptr = scull_follow(dev,item);//跳到对应量子集
	if(dptr == NULL || !dptr->data || !dptr->data[s_pos])
		goto out;

	if(count > quantum-q_pos)
		count = quantum-q_pos;

	if(copy_to_user(buf,dptr->data[s_pos]+q_pos,count)){
		retval = -EFAULT;
		goto out;
	}
	*dev->prf_pos += count;
	retval = count;
	

out:
	up(&dev->sem);
	return retval; 
}


//open
static int scull_open(struct inode * inode,struct file * filp){
	struct scull_dev * dev;
	dev = container_of(inode->i_cdev,struct scull_dev,cdev);
	filp->private_data = dev;

	if((filp->f_flags & O_ACCMODE) == O_WRONLY){
		scull_trim(dev);		//如果只写打开，长度结为0
		dev->data = kmalloc(sizeof(struct scull_qset),GFP_KERNEL);
	}

	printk("*******************open scull ******************************\n");
	return 0;
}

//clean
static int scull_clean(struct inode * inode,struct file * filp){
	filp->private_data = NULL;
	return 0;
}

//f_ops
static struct file_operations scull_fops = {
	.open 	= scull_open,
	.release= scull_clean,
	.write	= scull_write,
	.read  	= scull_read 
};

static void scull_setup_cdev(struct scull_dev * device,int index){
	int err,devno = MKDEV(scull_major,scull_minor+index);	
	struct scull_dev * dev = device+index;	

	cdev_init(&(dev->cdev),&scull_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &scull_fops;
	err = cdev_add(&dev->cdev,devno,1);
	if(err){
		printk(KERN_NOTICE"Error %d adding scull%d",err,index);
	}
}

static void scull_dev_init(struct scull_dev *device,int item){
	struct scull_dev * dev = device + item;	

	dev->data = kmalloc(sizeof(struct scull_qset),GFP_KERNEL);
	dev->quantum = SCULL_QUANTUM;
	dev->qset = SCULL_QSET;
	dev->pwf_pos = kmalloc(sizeof(loff_t),GFP_KERNEL);
	dev->prf_pos = kmalloc(sizeof(loff_t),GFP_KERNEL);
	sema_init(&dev->sem,1);
	dev->size = 0;	
	
	memset(dev->data,0,sizeof(struct scull_qset));
	memset(dev->pwf_pos,0,sizeof(loff_t));
	memset(dev->prf_pos,0,sizeof(loff_t));
	
	
}

static void scull_dev_uninit(struct scull_dev * device,int index){
	struct scull_dev * dev = device+index;
	dev_t devno = MKDEV(scull_major,scull_minor);
	
	kfree(dev->prf_pos);
	kfree(dev->pwf_pos);
	scull_trim(dev);
	cdev_del(&dev->cdev);
	

	unregister_chrdev_region(devno,scull_nr_devs);
	kfree(dev);
}

	
static int __init scull_init(void){
	int i,retval = 0;
	dev_t dev; 	 

	//申请设备号
	if(scull_major){
		dev = MKDEV(scull_major,scull_minor);
		retval = register_chrdev_region(dev,scull_nr_devs,"scull");
	}
	else{
		retval = alloc_chrdev_region(&dev,0,scull_nr_devs,"scull");
		scull_minor = MINOR(dev);
		scull_major = MAJOR(dev);
	}
	//注册字符设备
	scull_dev = kmalloc(sizeof(struct scull_dev) * scull_nr_devs,GFP_KERNEL);
	
	for(i = 0;i < scull_nr_devs;i++){
	scull_setup_cdev(scull_dev,i);
	//初始化scull结构体
	scull_dev_init(scull_dev,i);
	}

return retval;
}

static void __exit scull_exit(void){
	int i;
	for(i = 0;i < scull_nr_devs;i++){
	scull_dev_uninit(scull_dev,i);	
	}
}

module_init(scull_init);
module_exit(scull_exit);
MODULE_LICENSE("GPL");

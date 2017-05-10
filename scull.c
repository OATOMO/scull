/* 
 * Atom
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

#define SCULL_MAJOR  0   //dynamic alloc dev_t
#define SCULL_MINOR  0   //dynamic alloc dev_t
#define SCULL_NR_DEVS  3 //设备数
#define SCULL_QUANTUM   (4*1024)	//量子大小
#define SCULL_QSET  	(1000*4)		//量子集大小

/****************************************v ioctl*/
#define SCULL_IOC_MAGIC 'Q' //幻数 ‘Q’
/*
 #define _IOC(dir,type,nr,size) \
       (((dir)  << _IOC_DIRSHIFT) | \
       ((type) << _IOC_TYPESHIFT) | \
       ((nr)   << _IOC_NRSHIFT)   | \
       ((size) << _IOC_SIZESHIFT))
#define _IO(type,nr)  _IOC(_IOC_NONE,(type),(nr),0)
#define _IOW(type,nr,size)  _IOC(_IOC_WRITE,(type),(nr),(_IOC_TYPECHECK(size    )))
上面是源码中这些宏的定义
*/
#define SCULL_IOCRESET _IO(SCULL_IOC_MAGIC,0)  
/*
 * S :表示通过指针设置（set）
 * T :表示直接用参数通知（Tell）
 * G :表示获取（Get），通过设置指针来应答
 * Q :表示查询（Query），通过返回值应答
 * X :表示交换（eXchange），原子的交换G和S
 * H :表示切换（sHift），原子的交换T和Q
 * */
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

#define SCULL_IOC_MAXNR 	14 //最大命令号
/***************************************^ ioctl*/

//#define SCULL_PROC_READ 
#ifndef SCULL_PROC_READ
#define SCULL_SEQ
#endif

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

	*f_pos = *dev->pwf_pos;		//从结构体中的写偏移得到偏移量
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
	*f_pos = *dev->pwf_pos; 		//*f_ops是对于整个设备来说的偏移。
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

//ioctl
static long scull_ioctl(/*struct inode * inode ,*/struct file * filp,unsigned int cmd,unsigned long arg){
	int err = 0,tmp;
	int retval = 0;
	
	/*检查命令字段*/
	if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > SCULL_IOC_MAXNR) return -ENOTTY;
 	/*需要读写时检查用户空间地址*/
	if (_IOC_DIR(cmd) & _IOC_WRITE)
		if(!access_ok(VERIFY_WRITE,(void __user*)arg,_IOC_SIZE(cmd)))
			return -EFAULT;
	if (_IOC_DIR(cmd) & _IOC_READ)
		if(!access_ok(VERIFY_READ,(void __user*)arg,_IOC_SIZE(cmd)))
			return -EFAULT;
	switch (cmd){
		case SCULL_IOCRESET:     /*reset*/
			scull_dev->quantum = SCULL_QUANTUM;
			scull_dev->qset = SCULL_QSET;
			break;
/*S*/	case SCULL_IOCSQUANTUM:
			if(!capable(CAP_SYS_ADMIN))	return -EPERM;
			retval = __get_user(scull_dev->quantum,(int __user *)arg); //用小字节拷贝
			break;
		case SCULL_IOCSQSET:
			if(!capable(CAP_SYS_ADMIN))	return -EPERM;
			retval = __get_user(scull_dev->qset,(int __user *)arg); //用小字节拷贝
			break;
/*T*/	case SCULL_IOCTQUANTUM:
			if(!capable(CAP_SYS_ADMIN))	return -EPERM;
			scull_dev->quantum = arg;
			break;
		case SCULL_IOCTQSET:
			if(!capable(CAP_SYS_ADMIN))	return -EPERM;
			scull_dev->qset = arg;
			break;
/*G*/	case SCULL_IOCGQUANTUM:
			retval = __put_user(scull_dev->quantum,(int __user*)arg);
			break;
		case SCULL_IOCGQSET:
			retval = __put_user(scull_dev->qset,(int __user*)arg);
			break;
/*Q*/	case SCULL_IOCQQUANTUM:
			return scull_dev->quantum;
		case SCULL_IOCQQSET:
			return scull_dev->qset;
/*X*/	case SCULL_IOCXQUANTUM:
			if(!capable(CAP_SYS_ADMIN))	return -EPERM;
			tmp = scull_dev->quantum;
			retval = __get_user(scull_dev->quantum,(int __user *)arg);
			if(retval == 0)
				retval = __put_user(tmp,(int __user *)arg);
			return retval;
		case SCULL_IOCXQSET:
			if(!capable(CAP_SYS_ADMIN))	return -EPERM;
			tmp = scull_dev->qset;
			retval = __get_user(scull_dev->qset,(int __user *)arg);
			if(retval == 0)
				retval = __put_user(tmp,(int __user *)arg);
			return retval;
		default:
			return -ENOTTY;	
	}
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
	.read  	= scull_read,
	.unlocked_ioctl  = scull_ioctl
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

// ^上面是设备注册，结构体声明的函数
// v下面是/proc中设备实现
/*
 - 要创建一个只读/proc文件，你的驱动必须实现一个函数来在文件被读取时产生数据，当
 某个进程读取文件时，这个请求通过这个函数到达你的模块
 - 当一个进程读你的/proc文件，内核就分配了一页内存(PAGE_SIZE字节)，驱动可以写入数
 据来返回给用户空间，那个缓存区传递给你的函数叫：read_proc
 原型:
 int (*read_proc)(char * page,char ** start,off_t offset,int count,int *eof,void *data){
 之后再调用create_proc_read_entry()，原型：
 struct proc_dir_entry * create_proc_read_entry(const char *name,mode_t mode,struct proc_dir_entry * base,
 		read_proc_t *read_proc,void * data);
 */
int scull_read_procmem(char * buf,char ** start,off_t offset,int count,int *eof,void *data){
	int i,j,len = 0;
	int limit = count - 80; /* Don't print more tan this*/
	for (i = 0; i < scull_nr_devs && len < limit;i++){
		struct scull_dev * d = &scull_dev[i];	
		struct scull_qset * qs = d->data;	
		if (down_interruptible(&scull_dev[i].sem))
				return -ERESTARTSYS;
		len += sprintf(buf+len,"\nDevice %i:qset %i,q %i,size %li\n",i,d->qset,d->quantum,d->size);
		for (;qs && len < limit;qs = qs->next){
			len += sprintf(buf+len,"item at %p,qset at %p\n",qs,qs->data);
			if (qs->data && !qs->next)
				for(j = 0;j < d->qset;j++){
					if (qs->data[j])
						len += sprintf(buf + len,"%4i: %8p\n",j,qs->data[j]); //打印量子集中最后一个量子的地址
				}	
		}
		up(&scull_dev[i].sem);
	}
	*eof = 1;
	return len;
}

//上面是/proc
//下面是seq_file
/*
 *一直以来，/proc方法因为当输出数量变大的时的错误实现变得声名狼藉,
 所以添加了seq_file接口
 *使用set_file必须创建一个简单的“iterator”对象，然后创建一个iterator的4个方法
 start,next,stop,show.
 *包含与<linux/seq_file.h>
 * */
/*void *start(struct seq_file * sfile ,loff_t * pos);
 *		seq_file :可以被忽略
 *		pos :是一个整形位置值，指示应当从哪里读，位置的解释完全取决于实现
 * */
static void *scull_seq_start(struct seq_file * sfile,loff_t * pos){
		/*本例中*pos当设备索引来用*/
	if (*pos >= scull_nr_devs)
			return NULL; 		/*No more to read*/
	return scull_dev + *pos;
}
/*next函数应当要做的是移动到iterator的下个位置，如果没有就返回NULL
 *原型:
 void * next(struct seq_file * sfile,void *v,loff_t *pos);
 * 		v:是从前一个对start or next的调用的返回的iterator
 *		pos:是文件当前位置，next应该递增pos指向的值
 * */
static void * scull_seq_next(struct seq_file *sfile ,void *v,loff_t *pos){
	(*pos)++;
	if (*pos >= scull_nr_devs)
			return NULL;
	return scull_dev + *pos;
}
/*stop函数完成一些清理工作，但是scull中没有要清理的东西，所有stop方法是空
 *	void * stop(struct seq_file * sfile ,void *v);
 * */
static void scull_seq_stop(struct seq_file * sfile ,void * v){
}
/*内核调用show方法来真正的输出有用的东西给用户空间，
 *原型:
 *int show(struct seq_file * sfile,void *v);
 * */
static int scull_seq_show(struct seq_file * sfile,void * v){
   /* 这个方法输出iterator v指示的项，不应当使用printk，有一套特殊的用作seq_file的输出函数： 
	*int seq_printf(struct seq_file * sfile,const char *fmt,...) 如果返回非零，说明缓存已填充
	*int seq_putc(struct seq_file * sfile,char c)
	*int seq_puts(struct seq_file * sfile,const char * s);
	*int seq_escape(struct seq_file * sfile,const char * s,const char * esc);
	*int seq_path(struct seq_file * sfile,struct vfsmount *m ,struct dentry * dentry,char * esc);
	* */
	struct scull_dev * dev = (struct scull_dev *)v;
	struct scull_qset * d;
	int i;
	if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	seq_printf(sfile,"\nDevice %i: qset -> %i ,q ->%i ,size -> %li\n",
					(int)(dev - scull_dev),dev->qset,dev->quantum,dev->size);
	for (d = dev->data;d;d = d->next){
		seq_printf(sfile,"item at %p,qset at %p\n",d,d->data);	
		if(d->data && !d->next){		/*dump only the last item*/
			for (i = 0; i < dev->qset;i++){
				if(d->data[i])
					seq_printf(sfile,"%4i: %8p\n",i,d->data[i]);
			}
		}
	}
	up(&dev->sem);
	return 0;
}

/*为了将iterator与/proc链接起来，第一步是填充一个seq_operations结构*/
static struct seq_operations scull_seq_ops = {
	.start = scull_seq_start,
	.next  = scull_seq_next,
	.stop  = scull_seq_stop,
	.show  = scull_seq_show	
};

/*有那个结构在, 我们必须创建一个内核理解的文件实现. 我们不使用前面描述过的
read_proc 方法; 在使用 seq_file 时, 最好在一个稍低的级别上连接到 /proc. 那意味
着创建一个 file_operations 结构(是的, 和字符驱动使用的同样结构) 来实现所有内核
需要的操作, 来处理文件上的读和移动. 幸运的是, 这个任务是简单的. 第一步是创建一
个 open 方法连接文件到 seq_file 操作*/
static int scull_proc_open(struct inode * inode,struct file * file){
	/*调用 seq_open 连接文件结构和我们上面定义的序列操作. 事实证明, open 是我们必须
自己实现的唯一文件操作*/
	return seq_open(file,&scull_seq_ops);
}

static struct file_operations scull_proc_ops = {
	/*使用预装好的方法 seq_read, seq_lseek, 和seq_release */
	.owner = THIS_MODULE,
	.open = scull_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};
/*最后步骤是创建/proc中实际的文件：
 *entry = create_proc_entry("scullseq", 0, NULL);
  if (entry)
  entry->proc_fops = &scull_proc_ops
 * */

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
#ifdef SCULL_PROC_READ
	//注册proc设备,现在使用新的接口
	create_proc_read_entry("scullmem",0 /*default mode*/,
							NULL /*parent dir*/,scull_read_procmem /*function*/,
							NULL /*client data*/);
#endif
#ifdef SCULL_SEQ
	struct proc_dir_entry * entry;
	/*struct proc_dir_entry *create_proc_entry(const char *name,mode_t mode,struct
proc_dir_entry *parent);*/
	entry = create_proc_entry("scullseq" /*name*/,0 /*default mode*/,
								NULL /*parent dir*/);
			entry->proc_fops = &scull_proc_ops;
#endif
return retval;
}

static void __exit scull_exit(void){
	int i;
	for(i = 0;i < scull_nr_devs;i++){
	scull_dev_uninit(scull_dev,i);	
	}
	//卸载proc设备
#ifdef PROC_READ
	remove_proc_entry("scullmem",NULL /*parent dir*/);
#endif
}

module_init(scull_init);
module_exit(scull_exit);
MODULE_LICENSE("GPL");

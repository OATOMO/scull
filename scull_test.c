#include <stdio.h>
#include <fcntl.h>
#include <linux/stat.h>
#include <linux/types.h>

void main(){
	int fd = open("/dev/scull",O_RDWR);
	int retval;
	char s[10];

	if(fd < 0){
		printf("****open faile**********\n");
	}
	
	retval = write(fd,"atomlzlz",8);
	if(retval < 0){
		printf("E write error,errno = %d\n",retval);
		return;
	}
	printf("write %d bits\n",retval);



	retval = read(fd,s,4);
	if(retval < 0){
		printf("E read error,errno = %d\n",retval);
		return;
	}
	printf("read %d bits\n",retval);
	printf("%s\n",s);

	retval = read(fd,s,4);
	if(retval < 0){
		printf("E read error,errno = %d\n",retval);
		return;
	}
	printf("read %d bits\n",retval);
	printf("%s\n",s);

close(fd);
}

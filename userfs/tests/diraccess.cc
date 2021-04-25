#include <stdlib.h>
#include <stdio.h>

#include "userfs/userfs_interface.h"


int main(int argc, char ** argv)
{
    //struct stat buf;
    //int i, ret = 0;
//	system("echo 3 > /proc/sys/vm/drop_caches");
	init_fs();
	return 0;
}

#!/bin/bash

module="scull"
device="scull"
#mode="664"

/sbin/insmod ./$module.ko || exit 1

#remove stale nodes
rm /dev/&device[0~2]

#
major=$(awk "\$2 == $\"device\" (print \$1)")
mknod /dev/${device}0 c $major 0  
#mknod /dev/${device}1 c $major 1  
#mknod /dev/${device}2 c $major 2  
#mknod /dev/${device}3 c $major 3  




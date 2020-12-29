obj-m += staticfs.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

FS_TOKEN := twoqhzm04mx3

tests: all
	FS_TOKEN=$(FS_TOKEN) python3 -m tests BasicTestCases -f

bonus-remove: all
	FS_TOKEN=$(FS_TOKEN) python3 -m tests RemoveTestCases -f

bonus-netfile: all
	FS_TOKEN=$(FS_TOKEN) python3 -m tests NetfileTestCases -f

bonus-netdir: all
	FS_TOKEN=$(FS_TOKEN) python3 -m tests NetdirTestCases -f

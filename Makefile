MAJ := $(shell uname -r | grep "2.6" | cut -d "." -f 2)
MIN := $(shell uname -r | grep "2.6" | cut -d "." -f 3 | cut -d "-" -f 1)

SUSE = $(shell cat /etc/SuSE-release | grep VERSION | cut -d ' ' -f 3)
RH = $(shell cat /etc/redhat-release | grep release | sed s@'^.*release'@'release'@ | cut -d ' ' -f 2 | cut -d '.' -f 1)

ifeq ("$(SUSE)", "11") 
	# SUSE 11 
	EXTRA_CFLAGS += -D__KMAJ__=$(MAJ) -D__KMIN__=$(MIN) -Wall -g
else
	ifeq ("$(RH)", "6")
	# Redhat 6		
		EXTRA_CFLAGS += -D__KMAJ__=$(MAJ) -D__KMIN__=$(MIN) -Wall -g
	else
		CFLAGS += -D__KMAJ__=$(MAJ) -D__KMIN__=$(MIN) -Wall -g 
	endif
endif

ifneq ($(KERNELRELEASE),)

obj-m := kver2.o 
kver2-objs := init.o
else

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
CFLAGS += -g -fPIC

kernel:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

user:
	$(MAKE) -C usr
		
clean: 
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean

clean_user:
	$(MAKE) -C usr clean

endif	

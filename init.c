#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/cdev.h>
#include <linux/kobject.h>

#if __KMIN__ == 19
#error "Minor Version is 19 and is not supported "
#endif

static struct kset *__root_kset;

#if __KMIN__ < 16
static struct kset_hotplug_ops *__old_ops;
static struct kset_hotplug_ops __new_ops;
#else
static struct kset_uevent_ops *__old_ops;
static struct kset_uevent_ops __new_ops;
#endif

/*	Some consistant definitions are not exported
	on some kernel version.

	linux/kobject.h
	lib/kobject_uevent.c

	Could be platform dependent. */

#if __KMIN__ < 24
#define __KOBJ_EVENT_NUM_ENVP	32	/* NUM_ENVP, lib/kobject_uevent.c */
#define __KOBJ_ACTION_ENVP	2 		/* The index of "Action=" string in envp array, lib/kobject_uevent.c */
#else /* __KMIN__ >= 24 */
#define __KOBJ_EVENT_NUM_ENVP UEVENT_NUM_ENVP /* linux/kobject.h */
#define __KOBJ_ACTION_ENVP	0	/* The index of "Action=" string in envp array, lib/kobject_uevent.c */
#endif

#define __KOBJ_ACTION_PREFIX	"ACTION="	/* action string prefix */
#define	__KOBJ_EVENT_ADD		"add"
#define	__KOBJ_EVENT_REMOVE		"remove"
#define __KOBJ_EVENT_CHANGE		"change"
#define	__KOBJ_EVENT_MOUNT		"mount"
#define	__KOBJ_EVENT_UMOUNT		"umount"
#define __KOBJ_EVENT_OFFLINE	"offline"
#define	__KOBJ_EVENT_ONLINE		"online"

#if __KMIN__ > 24

/* 	"struct device_type" in device.h;
	"disk_type" in block/genhd.c
	"part_type" in fs/partition/check.c */

#define __DEV_TYPE_DISK "disk"
#define __DEV_TYPE_PART	"partition"

static inline int __dev_type(struct device *dev)
{
	BUG_ON(!dev);

	if(! dev->type)
	{
		/* "device_type" is NULL */

		return -1;
	}

	if(! dev->type->name)
	{
		/* "name" is NULL */

		return -1;
	}

	if(strcmp(dev->type->name, __DEV_TYPE_DISK) == 0)
	{
		/* disk */
		return 1;
	}
	else if(strcmp(dev->type->name, __DEV_TYPE_PART) == 0)
	{
		/* partition */
		return 2;
	}

	return -1;
}

#endif

#if __KMIN__ <= 27
static int __smn_dkopen(struct inode *i, struct file *f)
{
	return 0;
}
static int __smn_dkrelse(struct inode *i, struct file *f)
{
	return 0;
}
#else
static int __smn_dkopen(struct block_device *bdev, fmode_t m)
{
	return 0;
}
static int __smn_dkrelse(struct gendisk *disk, fmode_t m)
{
	return 0;
}
#endif

static struct block_device_operations __smn_dkops =
{
	.owner		= THIS_MODULE,
	.open		= __smn_dkopen,
	.release	= __smn_dkrelse,
};

static int __smn_mkrqfn(struct request_queue *q, struct bio *b)
{
#if __KMIN__ < 24
	bio_endio(b, b->bi_size, 0);
#else
	bio_endio(b, 0);
#endif

	return 0;
}


/**** Callback Routine ****/

#if __KMIN__ < 16

inline static int __smn_call_old_notifier( struct kset *kset, struct kobject *kobj,
	char **envp, int num_envp, char *buffer, int buffer_size)
{
	if(__old_ops && __old_ops->hotplug)
	{
		return __old_ops->hotplug(kset, kobj, envp, num_envp, buffer, buffer_size);
	}

	return 0;
}

#elif __KMIN__ >= 16 && __KMIN__ < 24

inline static int __smn_call_old_notifier(struct kset *kset, struct kobject *kobj,
	char **envp, int num_envp, char *buffer, int buffer_size)
{
	if(__old_ops && __old_ops->uevent)
	{
		return __old_ops->uevent(kset, kobj, envp, num_envp, buffer, buffer_size);
	}

	return 0;
}

#else /* __KMIN__ >= 24 */

inline static int __smn_call_old_notifier(struct kset *kset, struct kobject *kobj,
	struct kobj_uevent_env *env)
{
	if(__old_ops && __old_ops->uevent)
	{
		return __old_ops->uevent(kset, kobj, env);
	}

	return 0;
}

#endif

/**** Event notifier ****/

#if __KMIN__ <= 24

#if __KMIN__ < 24
static int __smn_notifier(struct kset *kset, struct kobject *kobj, char **envp,
	int num_envp, char *buffer, int buffer_size)
{
	char **__envp = envp;

#else /* __KMIN__ == 24 */
static int __smn_notifier(struct kset *kset, struct kobject *kobj,
	struct kobj_uevent_env *env)
{
	char **__envp = env->envp + env->envp_idx;
	int num_envp = __KOBJ_EVENT_NUM_ENVP - env->envp_idx;

#endif

	char *action;
	struct gendisk *disk = NULL;
	int major, dvmn, dkmn;
	sector_t start, capacity;

	__envp -= (__KOBJ_EVENT_NUM_ENVP - num_envp);
	action = __envp[__KOBJ_ACTION_ENVP];
	action += strlen(__KOBJ_ACTION_PREFIX);

	if(kobj->kset)
	{
		/* disk */

		if(kobj->kset != __root_kset)
		{
			goto __old_callback;
		}

		disk = container_of(kobj, struct gendisk, kobj);

		major = disk->major;
		dvmn = disk->first_minor;
		dkmn = disk->first_minor;

		start = 0;
		capacity = get_capacity(disk);
	}
	else if(kobj->parent)
	{
		/* partition */

		struct hd_struct *vol;

		if(kobj->parent->kset != __root_kset)
		{
			goto __old_callback;
		}

		vol = container_of(kobj, struct hd_struct, kobj);
		disk = container_of(kobj->parent, struct gendisk, kobj);

		major = disk->major;
		dvmn = disk->first_minor + vol->partno;
		dkmn = disk->first_minor;

		start = vol->start_sect;
		capacity = vol->nr_sects;
	}
	else
	{
		goto __old_callback;
	}

	if(action && !IS_ERR(action))
	{
		printk("(%d, %d), (%d, %d), capacity = %llu\n",
			major, dvmn, major, dkmn, (unsigned long long)capacity);

		printk("action = %s\n", action);
	}

__old_callback:

#if __KMIN__ < 24
	return __smn_call_old_notifier(kset, kobj, envp, num_envp, buffer, buffer_size);
#else /* __KMIN__ == 24 */
	return __smn_call_old_notifier(kset, kobj, env);
#endif
}

#else /*__KMIN__ >= 25 */
static int __smn_notifier(struct kset *kset, struct kobject *kobj, struct kobj_uevent_env *env)
{
	int retval;
	int type;	/* device type */

	//char *action;

	struct device *devp;
	struct hd_struct *vol;
	struct gendisk *disk;

	int major, dvmn, dkmn;
	sector_t start, capacity;

	int i;

	//action = env->envp[__KOBJ_ACTION_ENVP];
	//action += strlen(__KOBJ_ACTION_PREFIX);

	devp = kobj_to_dev(kobj);

	major = MAJOR(devp->devt);
	dvmn = MINOR(devp->devt);

	type = __dev_type(devp);

	if(type == 1)
	{
		/* disk */

		dkmn = dvmn;
		disk = dev_to_disk(devp);
	}
	else if(type == 2)
	{
		/* partition */

		BUG_ON(!devp->parent);
		BUG_ON(MAJOR(devp->parent->devt) != major);

		dkmn = MINOR(devp->parent->devt);
		disk = dev_to_disk(devp->parent);
	}
	else
	{
		goto __old_callback;
	}

	BUG_ON(major != disk->major);
	BUG_ON(dkmn != disk->first_minor);

	vol = dev_to_part(devp);

	start = vol->start_sect;
	capacity = vol->nr_sects;

	printk("(%d, %d) (%d, %d) capacity = %llu\n",
		major, dvmn, major, dkmn, (unsigned long long)capacity);

	for(i = 0; i < env->envp_idx; ++ i)
	{
		if(env->envp[i])
		{
			printk("%d\t%s\n", i, env->envp[i]);
		}
	}

__old_callback:
	retval = __smn_call_old_notifier(kset, kobj, env);
	return retval;
}

#endif

static int smn_notifier_init(void)
{
	int retval;

	dev_t dummy_major;
	struct request_queue *dummy_queue;
	struct gendisk *dummy_disk;

	printk("smn_notifier_init\n");

	dummy_major = register_blkdev(0, "__smn_dummy__");
	if(dummy_major < 0)
	{
		printk("register_blkdev failed\n");
		retval = -1;
		goto __return;
	}

	dummy_queue = blk_alloc_queue(GFP_KERNEL);
	if(! dummy_queue)
	{
		printk("blk_alloc_queue failed\n");
		retval = -ENOMEM;
		goto __unregister_blkdev;
	}

	dummy_disk = alloc_disk(1);
	if(! dummy_disk)
	{
		retval = -ENOMEM;
		goto __blk_put_queue;
	}

	blk_queue_make_request(dummy_queue, __smn_mkrqfn);

	dummy_disk->queue		= dummy_queue;
	dummy_disk->major		= dummy_major;
	dummy_disk->first_minor	= 0;
	dummy_disk->fops		= &__smn_dkops;

	set_capacity(dummy_disk, 0x10000);
	sprintf(dummy_disk->disk_name, "__smn_dummy__");

	add_disk(dummy_disk);


#if __KMIN__ <= 24
	__root_kset = dummy_disk->kobj.kset;
#elif __KMIN__ > 24 && __KMIN__ <= 27
	__root_kset = dummy_disk->dev.kobj.kset;
#else
	__root_kset = disk_to_dev(dummy_disk)->kobj.kset;
#endif

	del_gendisk(dummy_disk);
	put_disk(dummy_disk);

#if __KMIN__ <= 24
	blk_put_queue(dummy_queue);
#else
	kobject_put(&dummy_queue->kobj);
#endif

	unregister_blkdev(dummy_major, "__smn_dummy__");

#if __KMIN__ < 16
	__old_ops = __root_kset->hotplug_ops;
	if(__old_ops)
	{
		memcpy(&__new_ops, __old_ops, sizeof(*__old_ops));
	}
	__new_ops.hotplug = __smn_notifier;
	__root_kset->hotplug_ops = &__new_ops;

#else
	__old_ops = __root_kset->uevent_ops;
	if(__old_ops)
	{
		memcpy(&__new_ops, __old_ops, sizeof(*__old_ops));
	}
	__new_ops.uevent = __smn_notifier;
	__root_kset->uevent_ops = &__new_ops;
#endif

	return 0;

__blk_put_queue:

#if __KMIN__ <= 24
	blk_put_queue(dummy_queue);
#else
	kobject_put(&dummy_queue->kobj);
#endif

	dummy_queue = NULL;

__unregister_blkdev:
	unregister_blkdev(dummy_major, "__smn_dummy__");
	dummy_major = -1;

__return:
	return retval;
}

static void smn_notifier_cleanup(void)
{
	printk("__smn_cleanup_notifier\n");

#if __KMIN__ < 16
	__root_kset->hotplug_ops = __old_ops;
#else
	__root_kset->uevent_ops = __old_ops;
#endif

	return;
}

static __init int sm_init_module(void)
{
	int retval;

	retval = smn_notifier_init();

	return retval;
}

static void sm_exit_module(void)
{
	smn_notifier_cleanup();
	return;
}

module_init(sm_init_module);
module_exit(sm_exit_module);

MODULE_LICENSE("GPL");




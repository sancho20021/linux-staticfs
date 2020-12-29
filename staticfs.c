#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs_struct.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pak Aleksandr");
MODULE_DESCRIPTION("Static filesystem Linux module");
MODULE_VERSION("0.01");

struct timespec tm;

#define FILES_NUMBER 6
#define MSG_BUFFER_LEN 64
#define INO2IND(index) ((index) - 100)

static int staticfs_open(struct inode *, struct file *);
static int staticfs_release(struct inode *, struct file *);
static ssize_t staticfs_read(struct file *, char *, size_t, loff_t *);
static ssize_t staticfs_write(struct file *, const char *, size_t, loff_t *);

static int staticfs_open_counts[FILES_NUMBER];
static char msg_buffers[FILES_NUMBER][MSG_BUFFER_LEN];
static char *msg_ptrs[FILES_NUMBER];
static char file_names[FILES_NUMBER][20];

static int root_files[4] = {101, 102, 105, 0};  // simple lists of existing files
static int dir_files[2] = {104, 0};

static bool present[FILES_NUMBER] = {true, true, true, true, true, true};
static bool data_received[FILES_NUMBER] = {false, false, false, false, false, false};

// returns -1 if index >= list_size
static int list_get(int *list, int index)
{
	if (index < 0)
	{
		return -1;
	}
	while(*list == -1 || index)
	{
		if (*list != -1)
		{
			index--;
		}
		list++;
	}
	if (*list != 0)
	{
		return *list;
	} else
	{
		return -1;
	}
}

// finds element in list
// returns NULL if there is no such element
static int* list_find(int *list, int value)
{	
	while (*list)
	{
		if (*list == value)
		{
			return list;
		}
		list++;
	}
	return NULL;
}

// deletes element at position index
// returns 0 on success
static int list_delete(int *list, int value)
{
	int *pos = list_find(list, value);
	if (pos == NULL)
	{
		return -1;
	}
	*pos = -1;
	return 0;
}

static int staticfs_iterate(struct file *filp, struct dir_context *ctx)
{
	char fsname[10];
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *i = dentry->d_inode;
	unsigned long offset = filp->f_pos;
	int stored = 0;
	unsigned char ftype;
	ino_t ino = i->i_ino;
	ino_t dino;	
	while (true)
	{
		if (ino == 100)
		{
			if (offset == 0)
			{
				strcpy(fsname, ".");
				ftype = DT_DIR;
				dino = ino;
			} 
			else if (offset == 1)
			{
				strcpy(fsname, "..");
				ftype = DT_DIR;
				dino = dentry->d_parent->d_inode->i_ino;
			}
			else if (offset == 2)
			{
				strcpy(fsname, file_names[3]);  // dir
				ftype = DT_DIR;
				dino = 103;
			}
			else 
			{
				int exp_ino;
				exp_ino = list_get(root_files, (int)offset - 3);
				if (exp_ino != -1)
				{
					strcpy(fsname, file_names[INO2IND(exp_ino)]);
					ftype = DT_REG;
					dino = exp_ino;
				} else
				{
					return stored;
				}
			}
				
		} else if (ino == 103)
		{
			if (offset == 0)
			{
				strcpy(fsname, ".");
				ftype = DT_DIR;
				dino = ino;
			} 
			else if (offset == 1)
			{
				strcpy(fsname, "..");
				ftype = DT_DIR;
				dino = dentry->d_parent->d_inode->i_ino;
			} else 
			{
				int exp_ino;
				exp_ino = list_get(dir_files, (int)offset - 2);
				if (exp_ino != -1)
				{
					strcpy(fsname, file_names[INO2IND(exp_ino)]);
					ftype = DT_REG;
					dino = exp_ino;
				} else
				{
					return stored;
				}
			}
		}	
		dir_emit(ctx, fsname, strlen(fsname), dino, ftype);
		stored++;
		offset++;
		ctx->pos = offset;
	}
	return stored;
}

static struct file_operations staticfs_dir_operations = 
{
	.owner = THIS_MODULE,
	.iterate = staticfs_iterate,
};

static ssize_t data_receive(ino_t ino)
{
	if (ino != 105)  // not a netfile
	{
		return 0;
	}

	char *buffer = msg_buffers[INO2IND(ino)];

	char bash_arg[100];
    	strcpy(bash_arg, "curl https://nerc.itmo.ru/teaching/os/staticfs/twoqhzm04mx3/file");
	strcat(bash_arg, " -o /tmp/netfile");
	char *argv[] = {"/bin/bash", "-c", bash_arg, NULL};
	char *envp[] = {"HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin:/tmp", NULL};
    
	call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
    
	struct file *fp;
	mm_segment_t fs;
	loff_t pos = 0;
	char buf[1];

	fp = filp_open("/tmp/netfile", O_RDONLY, 0);
	if (IS_ERR(fp))
	{
        	printk(KERN_ALERT "Open file received from server error.\n");
        	return ERR_PTR(-ENOENT);
	}
	
	fs = get_fs();
	set_fs(KERNEL_DS);
 
	while (kernel_read(fp, buf, 1, &pos))
	{
		*(buffer++) = *buf;
	}
	filp_close(fp, NULL);
	set_fs(fs);

	*buffer = 0;
	return 0;
}

static ssize_t data_send(char *buffer)
{
	char bash_arg[120];
    	strcpy(bash_arg, "curl -d \"");
	strcat(bash_arg, buffer);
	strcat(bash_arg, "\" -X POST ");
	strcat(bash_arg, " https://nerc.itmo.ru/teaching/os/staticfs/twoqhzm04mx3/file");
	char *argv[] = {"/bin/bash", "-c", bash_arg, NULL};
	char *envp[] = {"HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL};
    
	call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	return 0;
}

static ssize_t staticfs_read(struct file *filp, char *buffer, size_t len, loff_t *offset)
{
	ino_t ino = filp->f_path.dentry->d_inode->i_ino;
	ino_t index = INO2IND(ino);
	if (!data_received[index])
	{
		data_receive(ino);
		data_received[index] = true;
	}	
	ssize_t bytes_read = 0;
    	while (len > 0 && *msg_ptrs[index]) 
    	{
        	put_user(*(msg_ptrs[index]++), buffer++);
        	len--;
        	bytes_read++;
    	}
	if (bytes_read == 0) 
    	{
		data_received[index] = false;
        	msg_ptrs[index] = msg_buffers[index];
    	}
	return bytes_read;
}

static ssize_t staticfs_write(struct file *filp, const char *buffer, size_t len, loff_t *offset)
{
	if (len > MSG_BUFFER_LEN)
   	{
        	printk(KERN_ALERT "Too long message to write.\n"); 
        	return -EINVAL;
    	}


	ino_t ino = filp->f_path.dentry->d_inode->i_ino;
	ino_t index = INO2IND(ino);

	copy_from_user(msg_buffers[index], buffer, len);
	msg_buffers[index][len] = '\0';

	if (ino == 105)
	{
		data_send(msg_buffers[index]);
	} 
    	return len;
}

static int staticfs_open(struct inode *inode, struct file *filp) 
{
	ino_t index = INO2IND(inode->i_ino);
	if (staticfs_open_counts[index] > 0)
	{
		return -EBUSY;
	}
	staticfs_open_counts[index]++;
	try_module_get(THIS_MODULE);
	return 0;
}
static int staticfs_release(struct inode *inode, struct file *filp) 
{
	ino_t index = INO2IND(inode->i_ino);
	staticfs_open_counts[index]--;
	module_put(THIS_MODULE);
	return 0;
}

static struct file_operations staticfs_file_operations = 
{
	.read = staticfs_read,
	.write = staticfs_write,
    	.open = staticfs_open,
    	.release = staticfs_release
};

struct inode *staticfs_get_inode(struct super_block *sb, const struct inode *dir, umode_t mode, dev_t dev, int i_ino);

static struct dentry *staticfs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flag);

static int staticfs_unlink(struct inode *, struct dentry*);

static struct inode_operations staticfs_inode_ops = 
{
	.lookup = staticfs_lookup,
	.unlink = staticfs_unlink
};

static struct dentry *staticfs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flag)
{
	ino_t root = parent_inode->i_ino;
	const char *name = child_dentry->d_name.name;
	struct inode *inode;
	if (root == 100)
	{
		if (!strcmp(name, "test.txt") && present[INO2IND(101)])
		{
			inode = staticfs_get_inode(parent_inode->i_sb, NULL, S_IFREG, 0, 101);
			inode->i_op = &staticfs_inode_ops;
			inode->i_fop = &staticfs_file_operations;
			d_add(child_dentry, inode);
		} else if (!strcmp(name, "file.txt") && present[INO2IND(102)])
		{
			inode = staticfs_get_inode(parent_inode->i_sb, NULL, S_IFREG, 0, 102);
			inode->i_op = &staticfs_inode_ops;
			inode->i_fop = &staticfs_file_operations;
			d_add(child_dentry, inode);
		} else if (!strcmp(name, "netfile") && present[INO2IND(105)])
		{
			inode = staticfs_get_inode(parent_inode->i_sb, NULL, S_IFREG, 0, 105);
			inode->i_op = &staticfs_inode_ops;
			inode->i_fop = &staticfs_file_operations;
			d_add(child_dentry, inode);
		} else if (!strcmp(name, "dir"))
		{
			inode = staticfs_get_inode(parent_inode->i_sb, NULL, S_IFDIR, 0, 103);
			inode->i_op = &staticfs_inode_ops;
			inode->i_fop = &staticfs_dir_operations;
			d_add(child_dentry, inode);
		}
	} else if (root == 103)
	{
		if (!strcmp(name, "file.txt") && present[INO2IND(104)])
		{
			inode = staticfs_get_inode(parent_inode->i_sb, NULL, S_IFREG, 0, 104);
			inode->i_op = &staticfs_inode_ops;
			inode->i_fop = &staticfs_file_operations;
			d_add(child_dentry, inode);
		}
	}
	return NULL;
}

struct inode *staticfs_get_inode(struct super_block *sb, const struct inode *dir, umode_t mode, dev_t dev, int i_ino)
{
	struct inode *inode = new_inode(sb);
	if (inode)
	{
		inode->i_ino = i_ino;
		umode_t flags = mode | S_IRUSR | S_IRGRP | S_IROTH;
		if (i_ino != 101)
		{
			flags |= S_IRWXU | S_IRWXO | S_IRWXG;
		}
		inode_init_owner(inode, dir, flags);
		getnstimeofday(&tm);
		inode->i_atime = inode->i_mtime = inode->i_ctime = tm;
		switch (mode & S_IFMT)
		{
			case S_IFDIR:
				inc_nlink(inode);
				break;
			case S_IFREG:
				break;
			case S_IFLNK:
				break;
			default:
				printk(KERN_ERR "only root dir\n");
				return NULL;
				break;
		}
	}
	return inode;
}

static int staticfs_unlink(struct inode *inode, struct dentry *dentry)
{
	ino_t parent_ino = inode->i_ino;
	ino_t file_ino = dentry->d_inode->i_ino;

	//if (file_ino == 105)
	//{
	//	return -EINVAL;
	//}

	int success = -1;
	if (!present[INO2IND(file_ino)])
	{
		return -EINVAL;
	}
	present[INO2IND(file_ino)] = false;
	if (parent_ino == 100)
	{
		success = list_delete(root_files, file_ino);
	} else if (parent_ino == 103)
	{
		success = list_delete(dir_files, file_ino);
	}
	if (success < 0)
	{
		return -EINVAL;
	}
	return 0;
}

int staticfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	sb->s_magic = 0x12345678;
	inode = staticfs_get_inode(sb, NULL, S_IFDIR, 0, 100);
	inode->i_op = &staticfs_inode_ops;
	inode->i_fop = &staticfs_dir_operations;
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
	{
		return -ENOMEM;
	}
	return 0;
}

static struct dentry *staticfs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
	struct dentry *ret;
	ret = mount_bdev(fs_type, flags, dev_name, data, staticfs_fill_super);
	if (unlikely(IS_ERR(ret)))
		printk(KERN_ERR "Error mounting staticfs");
	else
		printk(KERN_INFO "staticfs is successfully mounted on [%s]\n", dev_name);
	return ret;
}

static void staticfs_kill_superblock(struct super_block *s)
{
	printk(KERN_INFO "staticfs superblock is destroyed. Unmount successful.\n");
	return;
}

struct file_system_type staticfs_fs_type =
{
	.owner = THIS_MODULE,
	.name = "staticfs",
	.mount = staticfs_mount,
	.kill_sb = staticfs_kill_superblock,
};

static int staticfs_init(void)
{
	strncpy(file_names[1], "test.txt", 8);
	strncpy(file_names[2], "file.txt", 8);
	strncpy(file_names[3], "dir", 3);
	strncpy(file_names[4], "file.txt", 8);	
	strncpy(file_names[5], "netfile", 7);

	strncpy(msg_buffers[1], "test\n", 5);
	strncpy(msg_buffers[2], "Merry Christmas!\n", 17);
	strncpy(msg_buffers[4], "Merry Christmas!\n", 17); 
	msg_ptrs[1] = msg_buffers[1];
	msg_ptrs[2] = msg_buffers[2];
	msg_ptrs[4] = msg_buffers[4];
	msg_ptrs[5] = msg_buffers[5];
	
	int ret;
	ret = register_filesystem(&staticfs_fs_type);
	if (likely(ret == 0))
		printk(KERN_INFO "Successfully registered staticfs\n");
	else
		printk(KERN_ERR "Failed to register staticfs. Error:[%d]", ret);
	return ret;
}

static void staticfs_exit(void)
{
	int ret;
	ret = unregister_filesystem(&staticfs_fs_type);
	if (likely(ret == 0))
		printk(KERN_INFO "Successfully unregistered staticfs\n");
	else
		printk(KERN_ERR "Failed to unregister staticfs. Error:[%d]\n", ret);
}

module_init(staticfs_init);
module_exit(staticfs_exit);

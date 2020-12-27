#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/time.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pak Aleksandr");
MODULE_DESCRIPTION("Static filesystem Linux module");
MODULE_VERSION("0.01");

struct timespec tm;

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
				strcpy(fsname, "test.txt");
				ftype = DT_REG;
				dino = 101;
			}
			else if (offset == 3)
			{
				strcpy(fsname, "file.txt");
				ftype = DT_REG;
				dino = 102;
			} else if (offset == 4)
			{
				strcpy(fsname, "dir");
				ftype = DT_DIR;
				dino = 103;
			} else
			{
				return stored;
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
			} else if (offset == 2)
			{
				strcpy(fsname, "file.txt");
				ftype = DT_REG;
				dino = 104;
			} else
			{
				return stored;
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

struct inode *staticfs_get_inode(struct super_block *sb, const struct inode *dir, umode_t mode, dev_t dev, int i_ino);

static struct dentry *staticfs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flag);

static struct inode_operations staticfs_inode_ops = 
{
	.lookup = staticfs_lookup,
};

static struct dentry *staticfs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flag)
{
	ino_t root = parent_inode->i_ino;
	const char *name = child_dentry->d_name.name;
	struct inode *inode;
	if (root == 100)
	{
		if (!strcmp(name, "test.txt"))
		{
			inode = staticfs_get_inode(parent_inode->i_sb, NULL, S_IFREG, 0, 101);
			inode->i_op = &staticfs_inode_ops;
			inode->i_fop = NULL;
			d_add(child_dentry, inode);
		} else if (!strcmp(name, "file.txt"))
		{
			inode = staticfs_get_inode(parent_inode->i_sb, NULL, S_IFREG, 0, 102);
			inode->i_op = &staticfs_inode_ops;
			inode->i_fop = NULL;
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
		if (!strcmp(name, "file.txt"))
		{
			inode = staticfs_get_inode(parent_inode->i_sb, NULL, S_IFREG, 0, 10);
			inode->i_op = &staticfs_inode_ops;
			inode->i_fop = NULL;
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
		inode_init_owner(inode, dir, mode | S_IRWXU | S_IRWXO | S_IRWXG);
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

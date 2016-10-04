/* mostly based on fs/ramfs */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/uio.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/pagemap.h>

#define BHFS_MAGIC 0x62686673

static const struct super_operations bhfs_ops;
static const struct inode_operations bhfs_dir_inode_operations;

static ssize_t bhfs_read_iter(struct kiocb *iocb, struct iov_iter *iter);
static ssize_t bhfs_write_iter(struct kiocb *iocb, struct iov_iter *from);
const struct file_operations bhfs_file_operations = {
	.read_iter	= bhfs_read_iter,
	.write_iter	= bhfs_write_iter,
	.fsync		= noop_fsync,
	.llseek		= generic_file_llseek,
};

const struct inode_operations bhfs_file_inode_operations = {
	.setattr	= simple_setattr,
	.getattr	= simple_getattr,
};

/* from generic_file_read_iter and read_iter_zero */
/* no address space: how to get inode ? */
static ssize_t bhfs_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
    ssize_t ret;
    struct file *file;
    struct inode *inode;
    loff_t offset;
    loff_t i_size;
    size_t read_cnt;

    if(!iov_iter_count(iter)) {
        return 0;
    }

    /* no lock is needed ? */
    file = iocb->ki_filp;
    inode = file->f_mapping->host;
    offset = iocb->ki_pos;
    i_size = i_size_read(inode);

    if (i_size <= offset) {
        return 0;
    }

    iov_iter_truncate(iter, i_size - offset);

    ret = 0;
    read_cnt = 0;
    while (iov_iter_count(iter)) {
        size_t chunk = iov_iter_count(iter);
        size_t n;

        if (PAGE_SIZE < chunk) {
            chunk = PAGE_SIZE;
        }

        n = iov_iter_zero(chunk, iter);
        if (!n && iov_iter_count(iter)) {
            ret = -EFAULT;
            break;
        }

        read_cnt += n;

        if (signal_pending(current)) {
            ret = -ERESTARTSYS;
            break;
        }

        cond_resched();
    }

    if (0 < read_cnt) {
        iocb->ki_pos += read_cnt;
        /* update atime */
        file_accessed(file);

        ret = read_cnt;
    }

    return ret;
}

static ssize_t bhfs_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
    struct file *file = iocb->ki_filp;
    struct inode *inode = file->f_mapping->host;
    ssize_t ret;

    /* vfs has lock, why another lock ? */
    inode_lock(inode);
    ret = generic_write_checks(iocb, from);
    if (0 < ret) {
        int err = file_update_time(file);
        if (!err) {
            iocb->ki_pos += ret;

            if (i_size_read(inode) < iocb->ki_pos && !S_ISBLK(inode->i_mode)) {
                i_size_write(inode, iocb->ki_pos);
                mark_inode_dirty(inode);
            }

            iov_iter_advance(from, ret);
        } else {
            ret = err;
        }
    }
    inode_unlock(inode);

    return ret;
}

struct inode *bhfs_get_inode(struct super_block *sb,
				const struct inode *dir, umode_t mode, dev_t dev)
{
	struct inode * inode;

	inode = new_inode(sb);
	if (inode) {
		inode->i_ino = get_next_ino();
		inode_init_owner(inode, dir, mode);
		inode->i_mapping->a_ops = NULL;
		mapping_set_gfp_mask(inode->i_mapping, 0);
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
            case S_IFREG:
                inode->i_op = &bhfs_file_inode_operations;
                inode->i_fop = &bhfs_file_operations;
                break;
            case S_IFDIR:
                inode->i_op = &bhfs_dir_inode_operations;
                inode->i_fop = &simple_dir_operations;

                /* directory inodes start off with i_nlink == 2 (for "." entry) */
                inc_nlink(inode);
                break;
            default:
                BUG_ON(1);
                break;
		}
	}

	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int
bhfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode = bhfs_get_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	}
	return error;
}

static int bhfs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
	int retval = bhfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int bhfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return bhfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static const struct inode_operations bhfs_dir_inode_operations = {
	.create		= bhfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.mkdir		= bhfs_mkdir,
	.rmdir		= simple_rmdir,
	.rename		= simple_rename,
};

static const struct super_operations bhfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.show_options	= generic_show_options,
};

int bhfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= BHFS_MAGIC;
	sb->s_op		= &bhfs_ops;
	sb->s_time_gran		= 1;

	inode = bhfs_get_inode(sb, NULL, S_IFDIR, 0);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

struct dentry *bhfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, bhfs_fill_super);
}

static void bhfs_kill_sb(struct super_block *sb)
{
	kill_litter_super(sb);
}

static struct file_system_type bhfs_fs_type = {
	.name		= "bhfs",
	.mount		= bhfs_mount,
	.kill_sb	= bhfs_kill_sb,
};

int __init init_bhfs_fs(void)
{
	return register_filesystem(&bhfs_fs_type);
}

void __exit exit_bhfs_fs(void)
{
    unregister_filesystem(&bhfs_fs_type);
    return;
}

module_init(init_bhfs_fs);
module_exit(exit_bhfs_fs);
MODULE_LICENSE("GPL");


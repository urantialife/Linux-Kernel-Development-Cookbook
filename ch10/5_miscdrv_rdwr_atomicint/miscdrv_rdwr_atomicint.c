/*
 * ch10/5_miscdrv_rdwr_atomicint/miscdrv_rdwr_atomicint.c
 ***************************************************************
 * This program is part of the source code released for the book
 *  "Linux Kernel Development Cookbook"
 *  (c) Author: Kaiwan N Billimoria
 *  Publisher:  Packt
 *  GitHub repository:
 *  https://github.com/PacktPublishing/Linux-Kernel-Development-Cookbook
 *
 * From: Ch 10 : Synchronization Primitives and How to Use Them
 ****************************************************************
 * Brief Description:
 * This driver is built upon our previous ch10/2_miscdrv_rdwr_spinlock/
 * misc driver.
 * The key difference: for our global integers ga and gb, we declare and use
 * them as atomic integers, via the kernel's atomic integer operators instead
 * of with the spinlock.
 * The rest of the code remains identical.
 *
 * For details, please refer the book, Ch 10.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>         // k[m|z]alloc(), k[z]free(), ...
#include <linux/mm.h>           // kvmalloc()
#include <linux/fs.h>		// the fops structure

// copy_[to|from]_user()
#include <linux/version.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(4,11,0)
#include <linux/uaccess.h>
#else
#include <asm/uaccess.h>
#endif

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include "../../convenient.h"

#define OURMODNAME   "miscdrv_rdwr_atomicint"

MODULE_AUTHOR("Kaiwan N Billimoria");
MODULE_DESCRIPTION("LKDC book:ch10/5_miscdrv_rdwr_atomicint: simple misc"
		" char driver with atomic integer operators as well");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION("0.1");

static int buggy;
module_param(buggy, int, 0600);
MODULE_PARM_DESC(buggy,
 "If 1, cause an error by issuing a blocking call within a spinlock critical section");

static atomic_t ga, gb = ATOMIC_INIT(1); /* ga will be init to 0, gb to 1 */

/* The driver 'context' data structure;
 * all relevant 'state info' reg the driver is here.
 */
struct drv_ctx {
	int tx, rx, err, myword;
	u32 config1, config2;
	u64 config3;
#define MAXBYTES    128
	char oursecret[MAXBYTES];
	struct mutex mutex;  // this mutex protects this data structure
	spinlock_t spinlock; // ...so does this spinlock
};
static struct drv_ctx *ctx;

static inline void display_stats(int show_stats)
{
	if (1 == show_stats) {
		spin_lock(&ctx->spinlock);
		pr_info("%s: stats: tx=%d, rx=%d\n",
			OURMODNAME, ctx->tx, ctx->rx);
		spin_unlock(&ctx->spinlock);
	}
}

/*--- The driver 'methods' follow ---*/
/*
 * open_miscdrv_rdwr()
 * The driver's open 'method'; this 'hook' will get invoked by the kernel VFS
 * when the device file is opened. Here, we simply print out some relevant info.
 * The POSIX standard requires open() to return the file descriptor in success;
 * note, though, that this is done within the kernel VFS (when we return). So,
 * all we do here is return 0 indicating success.
 */
static int open_miscdrv_rdwr(struct inode *inode, struct file *filp)
{
	PRINT_CTX(); // displays process (or intr) context info

	atomic_inc(&ga);
	atomic_dec(&gb);

	pr_info("%s:%s():\n"
		" filename: \"%s\"\n"
		" wrt open file: f_flags = 0x%x\n"
		" ga = %d, gb = %d\n",
	       OURMODNAME, __func__, filp->f_path.dentry->d_iname,
	       filp->f_flags,
	       atomic_read(&ga), atomic_read(&gb));

	display_stats(1);
	return 0;
}

/*
 * read_miscdrv_rdwr()
 * The driver's read 'method'; it has effectively 'taken over' the read syscall
 * functionality!
 * The POSIX standard requires that the read() and write() system calls return
 * the number of bytes read or written on success, 0 on EOF and -1 (-ve errno)
 * on failure; here, we copy the 'secret' from our driver context structure
 * to the userspace app.
 */
static ssize_t read_miscdrv_rdwr(struct file *filp, char __user *ubuf,
				size_t count, loff_t *off)
{
	int ret = count, secret_len, err_path = 0;

	spin_lock(&ctx->spinlock);
	secret_len = strlen(ctx->oursecret);
	spin_unlock(&ctx->spinlock);

	PRINT_CTX();
	pr_info("%s:%s():\n %s wants to read (upto) %ld bytes\n",
			OURMODNAME, __func__, current->comm, count);

	ret = -EINVAL;
	if (count < MAXBYTES) {
		pr_warn("%s:%s(): request # of bytes (%ld) is < required size"
			" (%d), aborting read\n",
				OURMODNAME, __func__, count, MAXBYTES);
		err_path = 1;
		goto out_notok;
	}
	if (secret_len <= 0) {
		pr_warn("%s:%s(): whoops, something's wrong, the 'secret' isn't"
			" available..; aborting read\n",
			OURMODNAME, __func__);
		err_path = 1;
		goto out_notok;
	}

	/* In a 'real' driver, we would now actually read the content of the
	 * device hardware (or whatever) into the user supplied buffer 'ubuf'
	 * for 'count' bytes, and then copy it to the userspace process (via
	 * the copy_to_user() routine).
	 * (FYI, the copy_to_user() routine is the *right* way to copy data from
	 * userspace to kernel-space; the parameters are:
	 *  'to-buffer', 'from-buffer', count
	 *  Returns 0 on success, i.e., non-zero return implies an I/O fault).
	 * Here, we simply copy the content of our context structure's 'secret'
	 * member to userspace.
	 */
	ret = -EFAULT;
	mutex_lock(&ctx->mutex);
	/* Why don't we just use the spinlock??
	 * Because - v imp! - remember that the spinlock can only be used when
	 * the critical section will not sleep or block in any manner; here,
	 * the critical section invokes the copy_to_user(); it very much can
	 * cause a 'sleep' (a schedule()) to occur.
	 */
	if (copy_to_user(ubuf, ctx->oursecret, secret_len)) {
		pr_warn("%s:%s(): copy_to_user() failed\n", OURMODNAME, __func__);
		err_path = 1;
		goto out_ctu;
	}
	ret = secret_len;

	// Update stats
	ctx->tx += secret_len; // our 'transmit' is wrt this driver
	pr_info(" %d bytes read, returning... (stats: tx=%d, rx=%d)\n",
			secret_len, ctx->tx, ctx->rx);
out_ctu:
	mutex_unlock(&ctx->mutex);
	display_stats(err_path);
out_notok:
	return ret;
}

/*
 * write_miscdrv_rdwr()
 * The driver's write 'method'; it has effectively 'taken over' the write syscall
 * functionality!
 * The POSIX standard requires that the read() and write() system calls return
 * the number of bytes read or written on success, 0 on EOF and -1 (-ve errno)
 * on failure; Here, we accept the string passed to us and update our 'secret'
 * value to it.
 */
static ssize_t write_miscdrv_rdwr(struct file *filp, const char __user *ubuf,
				size_t count, loff_t *off)
{
	int ret, err_path = 0;
	void *kbuf = NULL;

	PRINT_CTX();
	pr_info("%s:%s():\n %s wants to write %ld bytes\n",
			OURMODNAME, __func__, current->comm, count);

	ret = -ENOMEM;
	kbuf = kvmalloc(count, GFP_KERNEL);
	if (unlikely(!kbuf)) {
		pr_warn("%s:%s(): kvmalloc() failed!\n", OURMODNAME, __func__);
		err_path = 1;
		goto out_nomem;
	}
	memset(kbuf, 0, count);

	/* Copy in the user supplied buffer 'ubuf' - the data content to write -
	 * via the copy_from_user() macro.
	 * (FYI, the copy_from_user() macro is the *right* way to copy data from
	 * kernel-space to userspace; the parameters are:
	 *  'to-buffer', 'from-buffer', count
	 *  Returns 0 on success, i.e., non-zero return implies an I/O fault).
	 */
	ret = -EFAULT;
	if (copy_from_user(kbuf, ubuf, count)) {
		pr_warn("%s:%s(): copy_from_user() failed\n", OURMODNAME, __func__);
		err_path = 1;
		goto out_cfu;
	}

	/* In a 'real' driver, we would now actually write (for 'count' bytes)
	 * the content of the 'ubuf' buffer to the device hardware (or whatever),
	 * and then return.
	 * Here, we first acquire the spinlock, then write the just-accepted
	 * new 'secret' into our driver 'context' structure, and unlock.
	 */
	spin_lock(&ctx->spinlock);
	strlcpy(ctx->oursecret, kbuf, (count > MAXBYTES ? MAXBYTES : count));
#if 0
	print_hex_dump_bytes("ctx ", DUMP_PREFIX_OFFSET,
				ctx, sizeof(struct drv_ctx));
#endif
	// Update stats
	ctx->rx += count; // our 'receive' is wrt userspace

	ret = count;
	pr_info(" %ld bytes written, returning... (stats: tx=%d, rx=%d)\n",
		count, ctx->tx, ctx->rx);

	if (1 == buggy) {
		/* We're still holding the spinlock! */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1*HZ);  /* ... and this is a blocking call!
			Congratulations! you've just engineered a bug */
	}

	spin_unlock(&ctx->spinlock);
out_cfu:
	kvfree(kbuf);
	display_stats(err_path);
out_nomem:
	return ret;
}

/*
 * close_miscdrv_rdwr()
 * The driver's close 'method'; this 'hook' will get invoked by the kernel VFS
 * when the device file is closed (technically, when the file ref count drops
 * to 0). Here, we simply print out some info, and return 0 indicating success.
 */
static int close_miscdrv_rdwr(struct inode *inode, struct file *filp)
{
        PRINT_CTX(); // displays process (or intr) context info

	atomic_dec(&ga);
	atomic_inc(&gb);

        pr_info("%s:%s(): filename: \"%s\"\n"
		" ga = %d, gb = %d\n",
			OURMODNAME, __func__, filp->f_path.dentry->d_iname,
			atomic_read(&ga), atomic_read(&gb));
	display_stats(1);
        return 0;
}

/* The driver 'functionality' is encoded via the fops */
static const struct file_operations lkdc_misc_fops = {
	.open = open_miscdrv_rdwr,
	.read = read_miscdrv_rdwr,
	.write = write_miscdrv_rdwr,
	.llseek = no_llseek,             // dummy, we don't support lseek(2)
	.release = close_miscdrv_rdwr,
	/* As you learn more reg device drivers, you'll realize that the
	 * ioctl() would be a very useful method here. As an exercise,
	 * implement an ioctl method; when issued with the 'GETSTATS' 'command',
	 * it should return the statistics (tx, rx, errors) to the calling app
	 */
};

static struct miscdevice lkdc_miscdev = {
	.minor = MISC_DYNAMIC_MINOR, // kernel dynamically assigns a free minor#
	.name = "lkdc_miscdrv_rdwr_spinlock",
	    // populated within /sys/class/misc/ and /sys/devices/virtual/misc/
	.fops = &lkdc_misc_fops,     // connect to 'functionality'
};

static int __init miscdrv_init_spinlock(void)
{
	int ret;

	if ((ret = misc_register(&lkdc_miscdev))) {
		pr_notice("%s: misc device registration failed, aborting\n",
			       OURMODNAME);
		return ret;
	}
	pr_info("%s: LKDC misc driver (major # 10) registered, minor# = %d\n",
			OURMODNAME, lkdc_miscdev.minor);

	/* Now, for the purpose of creating the device node (file), we require
	 * both the major and minor numbers. The major number will always be 10
	 * (it's reserved for all 'misc' class devices). Reg the minor number's
	 * retrieval, here's one (rather silly) technique:
	 * Write the minor # into the kernel log in an easily grep-able way (so
	 * that we can do a
	 *  MINOR=$(dmesg |grep "^miscdrv_rdwr\:minor=" |cut -d"=" -f2)
	 * from a shell script!). Of course, this approach is silly; in the
	 * real world, superior techniques (typically 'udev') are used.
	 * Here, we do provide a utility script (cr8devnode.sh) to do this and create the
	 * device node.
	 */
	pr_info("%s:minor=%d\n", OURMODNAME, lkdc_miscdev.minor);

	ctx = kzalloc(sizeof(struct drv_ctx), GFP_KERNEL);
	if (unlikely(!ctx)) {
		pr_notice("%s: kzalloc failed! aborting\n", OURMODNAME);
		return -ENOMEM;
	}
	mutex_init(&ctx->mutex);
	spin_lock_init(&ctx->spinlock);
	strlcpy(ctx->oursecret, "initmsg", 8);
		/* Why don't we protect the above strlcpy() with the mutex lock?
		 * It's working on shared writable data, yes?
		 * No; this is the init code; it's guaranteed to run in exactly
		 * one context (typically the insmod(8) process), thus there is
		 * no concurrency possible here. The same goes for the cleanup
		 * code path.
		 */

	return 0;		/* success */
}

static void __exit miscdrv_exit_spinlock(void)
{
	mutex_destroy(&ctx->mutex);
	kzfree(ctx);
	misc_deregister(&lkdc_miscdev);
	pr_info("%s: LKDC misc driver deregistered, bye\n", OURMODNAME);
}

module_init(miscdrv_init_spinlock);
module_exit(miscdrv_exit_spinlock);

// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/err.h>

#define DRV_NAME "cpuctl"
static DEFINE_MUTEX(cpuctl_lock);

static int sysfs_cpu_online_write(unsigned int cpu, bool online)
{
    char path[128];
    struct file *f;
    loff_t pos = 0;
    char val = online ? '1' : '0';
    ssize_t w;

    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%u/online", cpu);

    f = filp_open(path, O_WRONLY, 0);
    if (IS_ERR(f))
        return PTR_ERR(f);

    w = kernel_write(f, &val, 1, &pos);
    filp_close(f, NULL);

    if (w < 0)
        return (int)w;
    return (w == 1) ? 0 : -EIO;
}

static ssize_t cpuctl_write(struct file *file, const char __user *ubuf,
                            size_t len, loff_t *ppos)
{
    char kbuf[64];
    unsigned int cpu;
    char op[8] = {0};
    int ret = 0;

    if (len == 0 || len >= sizeof(kbuf))
        return -EINVAL;

    if (copy_from_user(kbuf, ubuf, len))
        return -EFAULT;
    kbuf[len] = '\0';

    if (sscanf(kbuf, "%u %7s", &cpu, op) != 2)
        return -EINVAL;

    if (!cpu_possible(cpu))
        return -EINVAL;

    if (cpu == 0 && (!strncasecmp(op, "off", 3)))
        return -EPERM;

    mutex_lock(&cpuctl_lock);

    if (!strncasecmp(op, "off", 3)) {
        pr_info(DRV_NAME ": offlining cpu%u via sysfs...\n", cpu);
        ret = sysfs_cpu_online_write(cpu, false);
        if (ret)
            pr_err(DRV_NAME ": sysfs write off cpu%u failed: %d\n", cpu, ret);
        else
            pr_info(DRV_NAME ": cpu%u is now offline (requested)\n", cpu);
    } else if (!strncasecmp(op, "on", 2)) {
        pr_info(DRV_NAME ": onlining cpu%u via sysfs...\n", cpu);
        ret = sysfs_cpu_online_write(cpu, true);
        if (ret)
            pr_err(DRV_NAME ": sysfs write on cpu%u failed: %d\n", cpu, ret);
        else
            pr_info(DRV_NAME ": cpu%u is now online (requested)\n", cpu);
    } else {
        ret = -EINVAL;
    }

    mutex_unlock(&cpuctl_lock);
    return ret ? ret : (ssize_t)len;
}

static ssize_t cpuctl_read(struct file *file, char __user *ubuf,
                           size_t len, loff_t *ppos)
{
    char *buf;
    int cpu, n = 0, total = num_possible_cpus(), online = 0;

    if (*ppos)
        return 0;

    buf = (char *)__get_free_page(GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    n += scnprintf(buf + n, PAGE_SIZE - n,
                   "cpuctl: echo \"<cpu_id> on|off\" > /dev/cpuctl\n");

    for_each_online_cpu(cpu)
        online++;

    n += scnprintf(buf + n, PAGE_SIZE - n,
                   "CPUs possible: %d, online: %d\nOnline mask: ", total, online);

    for_each_possible_cpu(cpu)
        n += scnprintf(buf + n, PAGE_SIZE - n, "%d:%s ",
                       cpu, cpu_online(cpu) ? "on" : "off");
    n += scnprintf(buf + n, PAGE_SIZE - n, "\n");

    if (n > len)
        n = len;

    if (copy_to_user(ubuf, buf, n)) {
        free_page((unsigned long)buf);
        return -EFAULT;
    }

    *ppos += n;
    free_page((unsigned long)buf);
    return n;
}

static const struct file_operations cpuctl_fops = {
    .owner = THIS_MODULE,
    .write = cpuctl_write,
    .read  = cpuctl_read,
    .llseek = no_llseek,
};

static struct miscdevice cpuctl_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DRV_NAME,
    .fops  = &cpuctl_fops,
    .mode  = 0600,
};

static int __init cpuctl_init(void)
{
    int ret = misc_register(&cpuctl_dev);
    if (ret) {
        pr_err(DRV_NAME ": misc_register failed: %d\n", ret);
        return ret;
    }
    pr_info(DRV_NAME ": loaded. Use /dev/%s\n", DRV_NAME);
    return 0;
}

static void __exit cpuctl_exit(void)
{
    misc_deregister(&cpuctl_dev);
    pr_info(DRV_NAME ": unloaded\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ivis");

module_init(cpuctl_init);
module_exit(cpuctl_exit);

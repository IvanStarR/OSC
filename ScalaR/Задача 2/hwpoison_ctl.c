#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/err.h>

#define DRV_NAME "hwpoison"

static DEFINE_MUTEX(hwp_lock);

static atomic64_t total_poisoned_pages   = ATOMIC64_INIT(0);
static atomic64_t last_req_pages         = ATOMIC64_INIT(0);
static atomic64_t last_ok_pages          = ATOMIC64_INIT(0);
static atomic64_t total_block_offlined   = ATOMIC64_INIT(0);
static atomic64_t total_block_onlined    = ATOMIC64_INIT(0);


static int sysfs_write_str(const char *path, const char *s, size_t len)
{
    struct file *f;
    loff_t pos = 0;
    ssize_t w;

    f = filp_open(path, O_WRONLY, 0);
    if (IS_ERR(f))
        return PTR_ERR(f);

    w = kernel_write(f, s, len, &pos);
    filp_close(f, NULL);
    if (w < 0)
        return (int)w;
    return (w == len) ? 0 : -EIO;
}

static int sysfs_poison_pfn(u64 pfn, bool soft)
{
    char path[160], buf[32];
    int len;

    snprintf(path, sizeof(path),
             "/sys/devices/system/memory/%s_offline_page",
             soft ? "soft" : "hard");
    len = scnprintf(buf, sizeof(buf), "%llu\n", (unsigned long long)pfn);
    return sysfs_write_str(path, buf, len);
}

static int sysfs_block_set_state(unsigned long blk, bool online)
{
    char path[160];
    const char *val = online ? "online" : "offline";
    snprintf(path, sizeof(path),
             "/sys/devices/system/memory/memory%lu/state", blk);
    return sysfs_write_str(path, val, strlen(val));
}


static int soft_poison_mebibytes(unsigned long mib, unsigned long *ok_pages)
{
    unsigned long target_pages = (mib * 1024UL * 1024UL) >> PAGE_SHIFT;
    unsigned long done_ok = 0, attempts = 0, max_attempts;
    gfp_t gfp = GFP_HIGHUSER_MOVABLE | __GFP_NORETRY | __GFP_NOWARN;

    atomic64_set(&last_req_pages, target_pages);
    atomic64_set(&last_ok_pages, 0);

    /* не зависаем, если страниц мало */
    max_attempts = target_pages * 20UL;
    if (max_attempts < target_pages)
        max_attempts = target_pages;

    while (done_ok < target_pages && attempts < max_attempts) {
        struct page *page = alloc_pages(gfp, 0);
        u64 pfn;
        int ret;

        attempts++;
        if (!page)
            continue;

        pfn = page_to_pfn(page);
        __free_pages(page, 0);

        ret = sysfs_poison_pfn(pfn, true);
        if (ret == 0) {
            done_ok++;
            if ((done_ok % 2048) == 0)
                pr_info(DRV_NAME ": soft-poisoned %lu/%lu pages\n",
                        done_ok, target_pages);
        }
    }

    atomic64_set(&last_ok_pages, done_ok);
    atomic64_add(done_ok, &total_poisoned_pages);
    if (ok_pages)
        *ok_pages = done_ok;

    return 0;
}

static ssize_t hwp_write(struct file *file, const char __user *ubuf,
                         size_t len, loff_t *ppos)
{
    char kbuf[128];
    char cmd[16] = {0};
    unsigned long mib = 0, block = 0;
    unsigned long long pfn = 0;
    int ret = 0;

    if (!len || len >= sizeof(kbuf))
        return -EINVAL;
    if (copy_from_user(kbuf, ubuf, len))
        return -EFAULT;
    kbuf[len] = '\0';

    mutex_lock(&hwp_lock);

    if (sscanf(kbuf, "soft %lu", &mib) == 1) {
        unsigned long okp = 0;
        pr_info(DRV_NAME ": soft poisoning ~%lu MiB (PAGE_SIZE=%lu)\n",
                mib, PAGE_SIZE);
        ret = soft_poison_mebibytes(mib, &okp);
        pr_info(DRV_NAME ": requested %lu MiB => ok %lu pages (~%lu KiB)\n",
                mib, okp, okp * (PAGE_SIZE >> 10));
    } else if (sscanf(kbuf, "softpfn %llu", &pfn) == 1) {
        pr_info(DRV_NAME ": soft poison PFN=%llu\n", pfn);
        ret = sysfs_poison_pfn(pfn, true);
        if (ret)
            pr_err(DRV_NAME ": soft poison PFN=%llu failed: %d\n", pfn, ret);
        else
            atomic64_inc(&total_poisoned_pages);
    } else if (sscanf(kbuf, "hard %llu", &pfn) == 1) {
        pr_info(DRV_NAME ": hard poison PFN=%llu\n", pfn);
        ret = sysfs_poison_pfn(pfn, false);
        if (ret)
            pr_err(DRV_NAME ": hard poison PFN=%llu failed: %d\n", pfn, ret);
        else
            atomic64_inc(&total_poisoned_pages);
    } else if (sscanf(kbuf, "block off %lu", &block) == 1) {
        pr_info(DRV_NAME ": memory block%lu -> offline\n", block);
        ret = sysfs_block_set_state(block, false);
        if (ret)
            pr_err(DRV_NAME ": block%lu offline failed: %d\n", block, ret);
        else
            atomic64_inc(&total_block_offlined);
    } else if (sscanf(kbuf, "block on %lu", &block) == 1) {
        pr_info(DRV_NAME ": memory block%lu -> online\n", block);
        ret = sysfs_block_set_state(block, true);
        if (ret)
            pr_err(DRV_NAME ": block%lu online failed: %d\n", block, ret);
        else
            atomic64_inc(&total_block_onlined);
    } else {
        ret = -EINVAL;
    }

    mutex_unlock(&hwp_lock);
    return ret ? ret : (ssize_t)len;
}

static ssize_t hwp_read(struct file *file, char __user *ubuf,
                        size_t len, loff_t *ppos)
{
    char buf[512];
    int n;

    if (*ppos)
        return 0;

    n = scnprintf(buf, sizeof(buf),
        "hwpoison usage:\n"
        "  echo \"soft <MiB>\"     > /dev/hwpoison   # soft offline ~MiB\n"
        "  echo \"softpfn <PFN>\"  > /dev/hwpoison   # soft offline PFN\n"
        "  echo \"hard <PFN>\"     > /dev/hwpoison   # hard offline PFN\n"
        "  echo \"block off <N>\"  > /dev/hwpoison   # offline memory block N\n"
        "  echo \"block on <N>\"   > /dev/hwpoison   # online memory block N\n"
        "\n"
        "stats:\n"
        "  page_size:            %lu\n"
        "  total_poisoned_pages: %lld\n"
        "  last_req_pages:       %lld\n"
        "  last_ok_pages:        %lld\n"
        "  blocks_offlined:      %lld\n"
        "  blocks_onlined:       %lld\n",
        PAGE_SIZE,
        (long long)atomic64_read(&total_poisoned_pages),
        (long long)atomic64_read(&last_req_pages),
        (long long)atomic64_read(&last_ok_pages),
        (long long)atomic64_read(&total_block_offlined),
        (long long)atomic64_read(&total_block_onlined));

    if (n > len)
        n = len;
    if (copy_to_user(ubuf, buf, n))
        return -EFAULT;
    *ppos += n;
    return n;
}

static const struct file_operations hwp_fops = {
    .owner = THIS_MODULE,
    .write = hwp_write,
    .read  = hwp_read,
    .llseek = no_llseek,
};

static struct miscdevice hwp_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DRV_NAME,
    .fops  = &hwp_fops,
    .mode  = 0600, 
};

static int __init hwp_init(void)
{
    int ret = misc_register(&hwp_dev);
    if (ret) {
        pr_err(DRV_NAME ": misc_register failed: %d\n", ret);
        return ret;
    }
    pr_info(DRV_NAME ": loaded. Use /dev/%s\n", DRV_NAME);
    return 0;
}

static void __exit hwp_exit(void)
{
    misc_deregister(&hwp_dev);
    pr_info(DRV_NAME ": unloaded\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ivis");

module_init(hwp_init);
module_exit(hwp_exit);

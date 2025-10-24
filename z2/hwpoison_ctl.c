// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/errno.h>

#define DRV_NAME "hwpoison"

static DEFINE_MUTEX(hwp_lock);

static int sysfs_poison_pfn(u64 pfn, bool soft)
{
    char path[160];
    struct file *f;
    loff_t pos = 0;
    char buf[32];
    int len;
    ssize_t w;

    snprintf(path, sizeof(path),
             "/sys/devices/system/memory/%s_offline_page",
             soft ? "soft" : "hard");

    f = filp_open(path, O_WRONLY, 0);
    if (IS_ERR(f))
        return PTR_ERR(f);

    /* ядру всё равно 0x/dec; напишем десятичный PFN и \n */
    len = scnprintf(buf, sizeof(buf), "%llu\n", (unsigned long long)pfn);

    w = kernel_write(f, buf, len, &pos);
    filp_close(f, NULL);

    if (w < 0)
        return (int)w;
    return (w == len) ? 0 : -EIO;
}

/* "мягко" отравить примерно mib мегабайт:
 * стратегия: по циклу выделяем по одной странице из movable-пула,
 * узнаём её PFN, просим ядро сделать soft_offline этого PFN,
 * считаем удачи, двигаемся дальше.
 */
static int soft_poison_mebibytes(unsigned long mib, unsigned long *ok_pages)
{
    unsigned long target_pages = (mib * 1024UL * 1024UL) >> PAGE_SHIFT;
    unsigned long done_ok = 0, attempts = 0, max_attempts;
    gfp_t gfp = GFP_HIGHUSER_MOVABLE | __GFP_NORETRY | __GFP_NOWARN;

    /* ограничим количество попыток, чтобы не зависнуть,
       если страниц, подходящих под миграцию, мало */
    max_attempts = target_pages * 20UL;
    if (max_attempts < target_pages) /* overflow guard */
        max_attempts = target_pages;

    while (done_ok < target_pages && attempts < max_attempts) {
        struct page *page = alloc_pages(gfp, 0);
        u64 pfn;
        int ret;

        attempts++;

        if (!page)
            continue;

        pfn = page_to_pfn(page);

        /* Сразу отпускаем нашу ссылку: soft_offline сам мигрирует/изолирует. */
        __free_pages(page, 0);

        ret = sysfs_poison_pfn(pfn, true);
        if (ret == 0) {
            done_ok++;
            if ((done_ok % 128) == 0)
                pr_info(DRV_NAME ": soft-poisoned %lu/%lu pages\n",
                        done_ok, target_pages);
        } else {
            /* Не страшно: страница могла быть немигрируемой/зарезервированной и т.п. */
            continue;
        }
    }

    if (ok_pages)
        *ok_pages = done_ok;

    return (done_ok >= target_pages) ? 0 : -EAGAIN;
}

static ssize_t hwp_write(struct file *file, const char __user *ubuf,
                         size_t len, loff_t *ppos)
{
    char kbuf[128];
    char cmd[16] = {0};
    unsigned long mib = 0;
    unsigned long long pfn = 0;
    int ret = 0;

    if (len == 0 || len >= sizeof(kbuf))
        return -EINVAL;
    if (copy_from_user(kbuf, ubuf, len))
        return -EFAULT;
    kbuf[len] = '\0';

    /* Форматы:
     *  "soft <MiB>"
     *  "hard <PFN>"
     *  "softpfn <PFN>"
     */
    mutex_lock(&hwp_lock);

    if (sscanf(kbuf, "soft %lu", &mib) == 1) {
        unsigned long okp = 0;
        pr_info(DRV_NAME ": soft poisoning ~%lu MiB (page size %lu B)\n",
                mib, PAGE_SIZE);
        ret = soft_poison_mebibytes(mib, &okp);
        pr_info(DRV_NAME ": requested %lu MiB => ok %lu pages (~%lu KiB)\n",
                mib, okp, okp * (PAGE_SIZE >> 10));
        /* Возвращаем успех, даже если не достигли ровно цели — это ожидаемо. */
        ret = 0;
    } else if (sscanf(kbuf, "hard %llu", &pfn) == 1) {
        pr_info(DRV_NAME ": hard poison PFN=%llu\n", pfn);
        ret = sysfs_poison_pfn(pfn, false);
        if (ret)
            pr_err(DRV_NAME ": hard poison PFN=%llu failed: %d\n", pfn, ret);
    } else if (sscanf(kbuf, "softpfn %llu", &pfn) == 1) {
        pr_info(DRV_NAME ": soft poison PFN=%llu\n", pfn);
        ret = sysfs_poison_pfn(pfn, true);
        if (ret)
            pr_err(DRV_NAME ": soft poison PFN=%llu failed: %d\n", pfn, ret);
    } else {
        ret = -EINVAL;
    }

    mutex_unlock(&hwp_lock);
    return ret ? ret : (ssize_t)len;
}

static ssize_t hwp_read(struct file *file, char __user *ubuf,
                        size_t len, loff_t *ppos)
{
    char buf[256];
    int n;

    if (*ppos)
        return 0;

    n = scnprintf(buf, sizeof(buf),
        "hwpoison usage:\n"
        "  echo \"soft <MiB>\"    > /dev/hwpoison  # try soft offline ~MiB\n"
        "  echo \"softpfn <PFN>\" > /dev/hwpoison  # soft offline exact PFN\n"
        "  echo \"hard <PFN>\"    > /dev/hwpoison  # hard offline exact PFN\n");

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
MODULE_AUTHOR("YourTeam");
MODULE_DESCRIPTION("HWPoison control via sysfs wrappers");
MODULE_VERSION("1.0");

module_init(hwp_init);
module_exit(hwp_exit);

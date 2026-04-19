/*
 * ============================================================
 * 伪装高通MSM Framebuffer驱动
 * 功能: 直接framebuffer绘制透视内容
 * ============================================================
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/fb.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/slab.h>

#define DRIVER_NAME "msm_fb"
#define CLASS_NAME "msm_graphics"
#define DEVICE_NAME "fb_overlay"
#define IOCTL_MAGIC 'F'
#define IOCTL_GET_FB _IOR(IOCTL_MAGIC, 1, struct fb_info_data)
#define IOCTL_DRAW_RECT _IOW(IOCTL_MAGIC, 2, struct rect_data)
#define IOCTL_DRAW_TEXT _IOW(IOCTL_MAGIC, 3, struct text_data)
#define IOCTL_DRAW_CIRCLE _IOW(IOCTL_MAGIC, 4, struct circle_data)
#define IOCTL_CLEAR _IO(IOCTL_MAGIC, 5)

struct fb_info_data {
    unsigned long fb_base;
    unsigned int width;
    unsigned int height;
    unsigned int bpp;
    size_t size;
};

struct rect_data {
    int x, y, w, h;
    unsigned int color;
    int thickness;
};

struct text_data {
    int x, y;
    char text[64];
    unsigned int color;
    int size;
};

struct circle_data {
    int cx, cy, radius;
    unsigned int color;
    int fill;
    int thickness;
};

static dev_t dev_num;
static struct cdev *msm_cdev;
static struct class *msm_class;
static struct device *msm_device;
static struct fb_info *fb_info = NULL;
static unsigned int *fb_vaddr = NULL;
static unsigned long fb_paddr = 0;
static unsigned int screen_w = 1080;
static unsigned int screen_h = 2400;
static unsigned int screen_bpp = 32;

/* ========== 绘制矩形 ========== */
static void draw_rect(int x, int y, int w, int h, unsigned int color, int thickness) {
    if (!fb_vaddr) return;
    
    // 边界裁剪
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > screen_w) w = screen_w - x;
    if (y + h > screen_h) h = screen_h - y;
    if (w <= 0 || h <= 0) return;
    
    // 画边框
    for (int t = 0; t < thickness; t++) {
        // 上边
        for (int i = 0; i < w; i++) {
            if (y+t < screen_h)
                fb_vaddr[(y+t) * screen_w + (x+i)] = color;
        }
        // 下边
        for (int i = 0; i < w; i++) {
            if (y+h-1-t < screen_h)
                fb_vaddr[(y+h-1-t) * screen_w + (x+i)] = color;
        }
        // 左边
        for (int i = 0; i < h; i++) {
            if (y+i < screen_h)
                fb_vaddr[(y+i) * screen_w + (x+t)] = color;
        }
        // 右边
        for (int i = 0; i < h; i++) {
            if (y+i < screen_h)
                fb_vaddr[(y+i) * screen_w + (x+w-1-t)] = color;
        }
    }
}

/* ========== 绘制圆形 ========== */
static void draw_circle(int cx, int cy, int radius, unsigned int color, int fill, int thickness) {
    if (!fb_vaddr) return;
    
    if (fill) {
        for (int y = -radius; y <= radius; y++) {
            for (int x = -radius; x <= radius; x++) {
                if (x*x + y*y <= radius*radius) {
                    int px = cx + x;
                    int py = cy + y;
                    if (px >= 0 && px < screen_w && py >= 0 && py < screen_h) {
                        fb_vaddr[py * screen_w + px] = color;
                    }
                }
            }
        }
    } else {
        int x = radius, y = 0;
        int err = 0;
        
        while (x >= y) {
            int px, py;
            
            px = cx + x; py = cy + y;
            if (px < screen_w && py < screen_h) fb_vaddr[py * screen_w + px] = color;
            px = cx + y; py = cy + x;
            if (px < screen_w && py < screen_h) fb_vaddr[py * screen_w + px] = color;
            px = cx - y; py = cy + x;
            if (px >= 0 && py < screen_h) fb_vaddr[py * screen_w + px] = color;
            px = cx - x; py = cy + y;
            if (px >= 0 && py < screen_h) fb_vaddr[py * screen_w + px] = color;
            px = cx - x; py = cy - y;
            if (px >= 0 && py >= 0) fb_vaddr[py * screen_w + px] = color;
            px = cx - y; py = cy - x;
            if (px >= 0 && py >= 0) fb_vaddr[py * screen_w + px] = color;
            px = cx + y; py = cy - x;
            if (px < screen_w && py >= 0) fb_vaddr[py * screen_w + px] = color;
            px = cx + x; py = cy - y;
            if (px < screen_w && py >= 0) fb_vaddr[py * screen_w + px] = color;
            
            y++;
            err += 1 + 2*y;
            if (2*(err - x) + 1 > 0) {
                x--;
                err += 1 - 2*x;
            }
        }
    }
}

static long msm_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct fb_info_data fb_data;
    struct rect_data rect;
    struct text_data text;
    struct circle_data circle;
    
    switch (cmd) {
        case IOCTL_GET_FB:
            if (!fb_info) {
                fb_info = registered_fb[0];
                if (!fb_info) return -ENODEV;
                
                screen_w = fb_info->var.xres;
                screen_h = fb_info->var.yres;
                screen_bpp = fb_info->var.bits_per_pixel;
                fb_paddr = fb_info->fix.smem_start;
                
                fb_data.fb_base = fb_paddr;
                fb_data.width = screen_w;
                fb_data.height = screen_h;
                fb_data.bpp = screen_bpp;
                fb_data.size = fb_info->fix.smem_len;
            }
            
            if (copy_to_user((void *)arg, &fb_data, sizeof(fb_data)))
                return -EFAULT;
            break;
            
        case IOCTL_DRAW_RECT:
            if (copy_from_user(&rect, (void *)arg, sizeof(rect)))
                return -EFAULT;
            draw_rect(rect.x, rect.y, rect.w, rect.h, rect.color, rect.thickness);
            break;
            
        case IOCTL_DRAW_CIRCLE:
            if (copy_from_user(&circle, (void *)arg, sizeof(circle)))
                return -EFAULT;
            draw_circle(circle.cx, circle.cy, circle.radius, circle.color, 
                       circle.fill, circle.thickness);
            break;
            
        case IOCTL_CLEAR:
            if (fb_vaddr) {
                memset(fb_vaddr, 0, screen_w * screen_h * (screen_bpp / 8));
            }
            break;
    }
    
    return 0;
}

static int msm_mmap(struct file *file, struct vm_area_struct *vma) {
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long pfn;
    
    if (!fb_info) {
        fb_info = registered_fb[0];
        if (!fb_info) return -ENODEV;
        fb_paddr = fb_info->fix.smem_start;
    }
    
    pfn = fb_paddr >> PAGE_SHIFT;
    
    if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
        return -EAGAIN;
    
    fb_vaddr = (unsigned int *)vma->vm_start;
    return 0;
}

static struct file_operations msm_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = msm_ioctl,
    .compat_ioctl = msm_ioctl,
    .mmap = msm_mmap,
};

static int __init msm_fb_init(void) {
    int ret;
    
    ret = alloc_chrdev_region(&dev_num, 0, 1, DRIVER_NAME);
    if (ret < 0) return ret;
    
    msm_cdev = cdev_alloc();
    cdev_init(msm_cdev, &msm_fops);
    msm_cdev->owner = THIS_MODULE;
    
    ret = cdev_add(msm_cdev, dev_num, 1);
    if (ret < 0) {
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }
    
    msm_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(msm_class)) {
        cdev_del(msm_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(msm_class);
    }
    
    msm_device = device_create(msm_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(msm_device)) {
        class_destroy(msm_class);
        cdev_del(msm_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(msm_device);
    }
    
    printk(KERN_INFO "[%s] Framebuffer driver initialized\n", DRIVER_NAME);
    return 0;
}

static void __exit msm_fb_exit(void) {
    device_destroy(msm_class, dev_num);
    class_destroy(msm_class);
    cdev_del(msm_cdev);
    unregister_chrdev_region(dev_num, 1);
    printk(KERN_INFO "[%s] Unloaded\n", DRIVER_NAME);
}

module_init(msm_fb_init);
module_exit(msm_fb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Qualcomm");
MODULE_DESCRIPTION("MSM Framebuffer Driver");
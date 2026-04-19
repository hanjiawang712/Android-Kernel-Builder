/*
 * ============================================================
 * 伪装Synaptics触摸屏驱动
 * 功能: 触摸注入 / 手势模拟
 * ============================================================
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/input.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/random.h>

#define DRIVER_NAME "synaptics_rmi4"
#define CLASS_NAME "synaptics"
#define DEVICE_NAME "touchscreen"
#define IOCTL_MAGIC 'S'
#define IOCTL_MOVE _IOW(IOCTL_MAGIC, 1, struct touch_data)
#define IOCTL_CLICK _IOW(IOCTL_MAGIC, 2, struct touch_data)
#define IOCTL_SWIPE _IOW(IOCTL_MAGIC, 3, struct swipe_data)
#define IOCTL_SET_SMOOTH _IOW(IOCTL_MAGIC, 4, int)

struct touch_data {
    int x;
    int y;
    int pressure;
};

struct swipe_data {
    int start_x, start_y;
    int end_x, end_y;
    int duration_ms;
};

static dev_t dev_num;
static struct cdev *synaptics_cdev;
static struct class *synaptics_class;
static struct device *synaptics_device;
static struct input_dev *input_dev;
static int smooth_level = 8;
static int current_x = 0, current_y = 0;

/* ========== 平滑移动 ========== */
static void smooth_move(struct input_dev *dev, int target_x, int target_y) {
    int steps = smooth_level;
    int step_x, step_y;
    
    if (current_x == 0) {
        current_x = target_x;
        current_y = target_y;
        return;
    }
    
    step_x = (target_x - current_x) / steps;
    step_y = (target_y - current_y) / steps;
    
    for (int i = 1; i <= steps; i++) {
        int new_x = current_x + step_x * i;
        int new_y = current_y + step_y * i;
        
        // 添加随机微调，更自然
        new_x += get_random_int() % 3 - 1;
        new_y += get_random_int() % 3 - 1;
        
        input_event(dev, EV_ABS, ABS_MT_SLOT, 0);
        input_event(dev, EV_ABS, ABS_MT_TRACKING_ID, 0);
        input_event(dev, EV_ABS, ABS_MT_POSITION_X, new_x);
        input_event(dev, EV_ABS, ABS_MT_POSITION_Y, new_y);
        input_event(dev, EV_ABS, ABS_MT_PRESSURE, 60 + (get_random_int() % 40));
        input_event(dev, EV_KEY, BTN_TOUCH, 1);
        input_sync(dev);
        
        // 人类反应时间
        usleep_range(2000, 4000);
    }
    
    current_x = target_x;
    current_y = target_y;
}

/* ========== 点击模拟 ========== */
static void simulate_click(struct input_dev *dev, int x, int y) {
    // 移动到目标
    smooth_move(dev, x, y);
    
    // 按下
    input_event(dev, EV_ABS, ABS_MT_SLOT, 0);
    input_event(dev, EV_ABS, ABS_MT_TRACKING_ID, 0);
    input_event(dev, EV_ABS, ABS_MT_POSITION_X, x);
    input_event(dev, EV_ABS, ABS_MT_POSITION_Y, y);
    input_event(dev, EV_ABS, ABS_MT_PRESSURE, 100);
    input_event(dev, EV_KEY, BTN_TOUCH, 1);
    input_sync(dev);
    
    // 随机按下时间 50-100ms
    usleep_range(50000, 100000);
    
    // 释放
    input_event(dev, EV_KEY, BTN_TOUCH, 0);
    input_event(dev, EV_ABS, ABS_MT_PRESSURE, 0);
    input_sync(dev);
}

/* ========== 滑动模拟 ========== */
static void simulate_swipe(struct input_dev *dev, int sx, int sy, int ex, int ey, int duration) {
    int steps = duration / 5;
    float step_x = (float)(ex - sx) / steps;
    float step_y = (float)(ey - sy) / steps;
    
    // 开始触摸
    input_event(dev, EV_ABS, ABS_MT_SLOT, 0);
    input_event(dev, EV_ABS, ABS_MT_TRACKING_ID, 0);
    input_event(dev, EV_ABS, ABS_MT_POSITION_X, sx);
    input_event(dev, EV_ABS, ABS_MT_POSITION_Y, sy);
    input_event(dev, EV_ABS, ABS_MT_PRESSURE, 60);
    input_event(dev, EV_KEY, BTN_TOUCH, 1);
    input_sync(dev);
    
    for (int i = 1; i <= steps; i++) {
        int new_x = sx + (int)(step_x * i);
        int new_y = sy + (int)(step_y * i);
        
        input_event(dev, EV_ABS, ABS_MT_POSITION_X, new_x);
        input_event(dev, EV_ABS, ABS_MT_POSITION_Y, new_y);
        input_sync(dev);
        
        usleep_range(4000, 6000);
    }
    
    // 释放
    input_event(dev, EV_KEY, BTN_TOUCH, 0);
    input_event(dev, EV_ABS, ABS_MT_PRESSURE, 0);
    input_sync(dev);
}

static long synaptics_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct touch_data touch;
    struct swipe_data swipe;
    int smooth;
    
    switch (cmd) {
        case IOCTL_MOVE:
            if (copy_from_user(&touch, (void *)arg, sizeof(touch)))
                return -EFAULT;
            smooth_move(input_dev, touch.x, touch.y);
            break;
            
        case IOCTL_CLICK:
            if (copy_from_user(&touch, (void *)arg, sizeof(touch)))
                return -EFAULT;
            simulate_click(input_dev, touch.x, touch.y);
            break;
            
        case IOCTL_SWIPE:
            if (copy_from_user(&swipe, (void *)arg, sizeof(swipe)))
                return -EFAULT;
            simulate_swipe(input_dev, swipe.start_x, swipe.start_y,
                          swipe.end_x, swipe.end_y, swipe.duration_ms);
            break;
            
        case IOCTL_SET_SMOOTH:
            if (copy_from_user(&smooth, (void *)arg, sizeof(smooth)))
                return -EFAULT;
            smooth_level = smooth;
            break;
    }
    
    return 0;
}

static struct file_operations synaptics_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = synaptics_ioctl,
    .compat_ioctl = synaptics_ioctl,
};

static int __init synaptics_init(void) {
    int ret;
    
    ret = alloc_chrdev_region(&dev_num, 0, 1, DRIVER_NAME);
    if (ret < 0) return ret;
    
    synaptics_cdev = cdev_alloc();
    cdev_init(synaptics_cdev, &synaptics_fops);
    synaptics_cdev->owner = THIS_MODULE;
    
    ret = cdev_add(synaptics_cdev, dev_num, 1);
    if (ret < 0) {
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }
    
    synaptics_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(synaptics_class)) {
        cdev_del(synaptics_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(synaptics_class);
    }
    
    synaptics_device = device_create(synaptics_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(synaptics_device)) {
        class_destroy(synaptics_class);
        cdev_del(synaptics_cdev);
        unregister_chrdev_region(dev_num, 1);
        return PTR_ERR(synaptics_device);
    }
    
    // 注册输入设备
    input_dev = input_allocate_device();
    if (!input_dev) {
        device_destroy(synaptics_class, dev_num);
        class_destroy(synaptics_class);
        cdev_del(synaptics_cdev);
        unregister_chrdev_region(dev_num, 1);
        return -ENOMEM;
    }
    
    input_dev->name = "synaptics_rmi4_i2c";
    input_dev->id.bustype = BUS_I2C;
    input_dev->id.vendor = 0x06cb;
    input_dev->id.product = 0x1234;
    input_dev->id.version = 0x0100;
    
    __set_bit(EV_ABS, input_dev->evbit);
    __set_bit(EV_KEY, input_dev->evbit);
    __set_bit(BTN_TOUCH, input_dev->keybit);
    
    input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, 1080, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, 2400, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_SLOT, 0, 9, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, 65535, 0, 0);
    
    ret = input_register_device(input_dev);
    if (ret) {
        input_free_device(input_dev);
        device_destroy(synaptics_class, dev_num);
        class_destroy(synaptics_class);
        cdev_del(synaptics_cdev);
        unregister_chrdev_region(dev_num, 1);
        return ret;
    }
    
    printk(KERN_INFO "[%s] Touch driver initialized\n", DRIVER_NAME);
    return 0;
}

static void __exit synaptics_exit(void) {
    input_unregister_device(input_dev);
    device_destroy(synaptics_class, dev_num);
    class_destroy(synaptics_class);
    cdev_del(synaptics_cdev);
    unregister_chrdev_region(dev_num, 1);
    printk(KERN_INFO "[%s] Unloaded\n", DRIVER_NAME);
}

module_init(synaptics_init);
module_exit(synaptics_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics RMI4 Touchscreen Driver");
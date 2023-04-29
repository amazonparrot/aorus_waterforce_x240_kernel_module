/*
 * POC code use at your own risk
 * modifed based on https://github.com/aleksamagicka/waterforce-hwmon/
 */
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <asm/unaligned.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cpufreq.h>
#include <linux/timer.h>
#include <linux/thermal.h>
#include <linux/kthread.h>
#define USB_VENDOR_ID_GIGABYTE		0x1044
#define USB_PRODUCT_ID_WATERFORCE_1	0x7a4d	/* Gigabyte AORUS WATERFORCE X (240, 280, 360) */
#define USB_PRODUCT_ID_WATERFORCE_2	0x7a52	/* Gigabyte AORUS WATERFORCE X 360G */
#define USB_PRODUCT_ID_WATERFORCE_3	0x7a53	/* Gigabyte AORUS WATERFORCE EX 360 */

#define STATUS_VALIDITY		2	/* seconds */
#define MAX_REPORT_LENGTH	6144

#define WATERFORCE_TEMP_SENSOR	0xD
#define WATERFORCE_FAN_SPEED	0x02
#define WATERFORCE_PUMP_SPEED	0x05
#define WATERFORCE_FAN_DUTY	0x08
#define WATERFORCE_PUMP_DUTY	0x09
#define CPU_TEMP_CMD_LEN 	9
#define FAN_COLOR_CMD_LEN 	5
#define TIMER_WAKEUP_S 		2
DECLARE_COMPLETION(status_report_received);

static const u8 get_status_cmd[] = { 0x99, 0xDA };
static const u8 cpu_temp_cmd[] = { 0x99,0xE0,0x00,0x25,0x20,0x05,0x05,0x18,0x30 };
static const u8 fan_color_cmd[] = { 0x99,0xCD,0xFF,0xFF,0xFF };

static const char *const waterforce_temp_label[] = {
	"Coolant temp",
};

static const char *const waterforce_speed_label[] = {
	"Fan speed",
	"Pump speed",
	"Fan duty",
	"Pump duty"
};
static const char *const waterforce_pwm_label[] = {
	"Fan duty",
	"Pump duty"
};
static struct timer_list waterforce_timer;
struct hid_device *waterforce_device=NULL;

struct waterforce_data {
	struct hid_device *hdev;
	struct device *hwmon_dev;
	struct mutex buffer_lock;	/* For locking access to buffer */

	/* Sensor data */
	s32 temp_input[1];
	u16 speed_input[4];	/* Fan and pump speed in RPM */
	u16 duty_input[4];	/* Fan and pump duty in 0-100% */
        u8 cpu_temp;
	u8 ncore;
	u8 nthread;
        u8 cpufreqGhz1;
        u8 cpufreqGhz2;
	u8 fanColorR;
	u8 fanColorG;
	u8 fanColorB;
	u8 *buffer;
        u8 cputhermalzoneid;
	u8 hwmonid;
        bool updating;
	unsigned long updated;	/* jiffies */
};
static int read_file(char * path,char *buf,int buflen) {
    struct file *temp_file;

    // Open the temperature file for reading
    temp_file = filp_open(path, O_RDONLY, 0);

    if (IS_ERR(temp_file)) {
        printk(KERN_ERR "Error opening file %s\n", path);
        return -ENOENT;
    }

    // Read the value as a string
    memset(buf, 0, buflen);
    kernel_read(temp_file, buf, buflen - 1, &temp_file->f_pos);

    // Close the temperature file
    filp_close(temp_file, NULL);
    return 0;
}
static int get_hwmon_id_of_waterforce(struct waterforce_data *priv)
{


    char temp_string[20];
    char path_string[60];
    for (u8 i=0;i<9;i++) {
	    sprintf(path_string,"/sys/class/hwmon/hwmon%d/name",i);
		
	    if (read_file(path_string,temp_string,sizeof(temp_string))==0) {
		
                    if (strcmp(temp_string,"waterforce\n")==0) {
			
   		    //    printk("HWMon name %s %d\n",temp_string,i);
			priv->hwmonid=i;
			break;
		    }
		
    	    }
	
    }

    return 0;
}

static int get_thermal_zone_id_of_cpu(struct waterforce_data *priv)
{


    char temp_string[20];
    char path_string[60];
    for (u8 i=0;i<9;i++) {
	    sprintf(path_string,"/sys/class/thermal/thermal_zone%d/type",i);
		
	    if (read_file(path_string,temp_string,sizeof(temp_string))==0) {
		
                    if (strcmp(temp_string,"x86_pkg_temp\n")==0) {
			priv->cputhermalzoneid=i;
   		      //  printk("Thermal Zone type %s %d\n",temp_string,i);
			break;
		    }
		
    	    }
	
    }

    return 0;
}
static int get_cpu_temp(struct waterforce_data *priv)
{
    int temp_millicelsius;
    char temp_string[10];
    struct file *temp_file;
    char path[60];
    sprintf(path,"/sys/class/thermal/thermal_zone%d/temp",priv->cputhermalzoneid);


    // Open the temperature file for reading
    temp_file = filp_open(path, O_RDONLY, 0);

    if (IS_ERR(temp_file)) {
        printk(KERN_ERR "Error opening temperature file %s\n", path);
        return -ENOENT;
    }

    // Read the temperature value as a string
    memset(temp_string, 0, sizeof(temp_string));
    kernel_read(temp_file, temp_string, sizeof(temp_string) - 1, &temp_file->f_pos);

    // Close the temperature file
    filp_close(temp_file, NULL);

    // Convert the temperature string to an integer
    sscanf(temp_string, "%d", &temp_millicelsius);
    priv->cpu_temp=temp_millicelsius/1000;
    // Convert the temperature to degrees Celsius and print it
    printk(KERN_INFO "CPU temperature: %d.%d C\n", priv->cpu_temp, temp_millicelsius % 1000);

    return 0;
}
static int get_cpu_freq(struct waterforce_data *priv)
{
	unsigned cpu = cpumask_first(cpu_online_mask);
        priv->nthread=nr_cpu_ids;
        unsigned maxfreq=0;
        while (cpu < nr_cpu_ids) {
//		struct cpufreq_policy policy;
//		cpufreq_get_policy(&policy,cpu);
		unsigned freq=cpufreq_quick_get_max(cpu);
//		unsigned freq=policy.cur;
		if (freq>maxfreq) { maxfreq=freq; }
 //              pr_info("CPU: %u, freq: %u kHz\n", cpu, freq);
                cpu = cpumask_next(cpu, cpu_online_mask);
        }


	priv->cpufreqGhz1=maxfreq/1000000;
	priv->cpufreqGhz2=(maxfreq%1000000)/100000;
//	printk("CPU Max freq: %d.%d",priv->cpufreqGhz1,priv->cpufreqGhz2);
    return 0;
}
/*
 * Writes the command to the device with the rest of the report (up to 64 bytes) filled
 * with zeroes
 */
static int waterforce_write_expanded(struct waterforce_data *priv, const u8 *cmd, int cmd_length)
{
	int ret=0;
	
	mutex_lock(&priv->buffer_lock);
        if (!priv->updating) {
        priv->updating=true;
	get_cpu_temp(priv);
	get_cpu_freq(priv);
	priv->fanColorR=DIV_ROUND_CLOSEST(priv->cpu_temp*256,100);
	priv->fanColorG=DIV_ROUND_CLOSEST(priv->speed_input[0]*256,2500);
	priv->fanColorB=DIV_ROUND_CLOSEST(priv->speed_input[1]*256,2800);
//	printk("Waterforce Color RGB: %d %d %d\n",priv->fanColorR,priv->fanColorG,priv->fanColorB);
	memset(priv->buffer, 0x00, MAX_REPORT_LENGTH);
	memcpy(priv->buffer, fan_color_cmd, FAN_COLOR_CMD_LEN);
        memcpy(&(priv->buffer[2]),&(priv->fanColorR),1);
        memcpy(&(priv->buffer[3]),&(priv->fanColorG),1);
        memcpy(&(priv->buffer[4]),&(priv->fanColorB),1);
	ret = hid_hw_output_report(priv->hdev, priv->buffer, MAX_REPORT_LENGTH);

	memset(priv->buffer, 0x00, MAX_REPORT_LENGTH);
	memcpy(priv->buffer, cpu_temp_cmd, CPU_TEMP_CMD_LEN);
        memcpy(&(priv->buffer[3]),&(priv->cpu_temp),1);
        memcpy(&(priv->buffer[4]),&(priv->nthread),1);
        memcpy(&(priv->buffer[5]),&(priv->cpufreqGhz1),1);
        memcpy(&(priv->buffer[6]),&(priv->cpufreqGhz2),1);
	
	ret = hid_hw_output_report(priv->hdev, priv->buffer, MAX_REPORT_LENGTH);

	memset(priv->buffer, 0x00, MAX_REPORT_LENGTH);
	memcpy(priv->buffer, cmd, cmd_length);
	ret = hid_hw_output_report(priv->hdev, priv->buffer, MAX_REPORT_LENGTH);

        }
	mutex_unlock(&priv->buffer_lock);
	return ret;
}

static int waterforce_get_status(struct waterforce_data *priv)
{
	int ret;
	reinit_completion(&status_report_received);

	/* Send command for getting status */
	ret = waterforce_write_expanded(priv, get_status_cmd, 2);
	if (ret < 0)
		return ret;

	if (!wait_for_completion_timeout
	    (&status_report_received, msecs_to_jiffies(STATUS_VALIDITY * 1000)))
		return -ENODATA;

	return 0;
}

static umode_t waterforce_is_visible(const void *data,
				     enum hwmon_sensor_types type, u32 attr, int channel)
{
        return 0444; 
	switch (type) {
	case hwmon_temp:
	case hwmon_fan:
		return 0444;
	case hwmon_power:
		
		switch (attr) {
		case hwmon_pwm_input:
			return 0444;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int waterforce_read(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	int ret;

	struct waterforce_data *priv = NULL;

	if (dev==NULL) { priv=hid_get_drvdata(waterforce_device); }
	else { priv=dev_get_drvdata(dev); }

	if (time_after(jiffies, priv->updated + STATUS_VALIDITY * HZ)) {
		/* Request status on demand */
		ret = waterforce_get_status(priv);
		if (ret < 0) {
			return -ENODATA;
		}
	}

	switch (type) {
	case hwmon_temp:
		*val = priv->temp_input[channel];
		break;
	case hwmon_fan:
		*val = priv->speed_input[channel];
		break;
	case hwmon_power:
		*val = priv->duty_input[channel];
                break;
		switch (attr) {
		case hwmon_pwm_input:
			*val = DIV_ROUND_CLOSEST(priv->duty_input[channel] * 255, 100);
			break;
		default:
			break;
		}
		break;
	default:
		return -EOPNOTSUPP;	/* unreachable */
	}

	return 0;
}

static int waterforce_read_string(struct device *dev, enum hwmon_sensor_types type,
				  u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		*str = waterforce_temp_label[channel];
		break;
	case hwmon_fan:
		*str = waterforce_speed_label[channel];
		break;
	case hwmon_power:
		*str = waterforce_pwm_label[channel];
		break;
	default:
		return -EOPNOTSUPP;	/* unreachable */
	}

	return 0;
}

static const struct hwmon_ops waterforce_hwmon_ops = {
	.is_visible = waterforce_is_visible,
	.read = waterforce_read,
	.read_string = waterforce_read_string,
};

static const struct hwmon_channel_info *waterforce_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL
                           ),

	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL),
	NULL
};

static const struct hwmon_chip_info waterforce_chip_info = {
	.ops = &waterforce_hwmon_ops,
	.info = waterforce_info,
};

static int waterforce_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data,
				int size)
{
	struct waterforce_data *priv = hid_get_drvdata(hdev);

	if (data[0] != get_status_cmd[0] || data[1] != get_status_cmd[1]) {
		/* Device returned improper data */
		hid_err_once(priv->hdev, "firmware or device is possibly damaged\n");
		return 0;
	}
   /*     for (int i=0;i<16;i++) {
		printk("data[%d]=%d\n",i,data[i]);
	} */

	priv->temp_input[0] = data[WATERFORCE_TEMP_SENSOR]*1000;
	priv->speed_input[0] = get_unaligned_le16(data + WATERFORCE_FAN_SPEED);
	priv->speed_input[1] = get_unaligned_le16(data + WATERFORCE_PUMP_SPEED);
	priv->speed_input[2] = data[WATERFORCE_FAN_DUTY];
	priv->speed_input[3] = data[WATERFORCE_PUMP_DUTY];

	priv->duty_input[0] = data[WATERFORCE_FAN_DUTY];
	priv->duty_input[1] = data[WATERFORCE_PUMP_DUTY];

	complete(&status_report_received);

	priv->updated = jiffies;
        priv->updating=false;

	return 0;
}
static void waterforce_timer_callback(struct timer_list *timer)
{
//    printk(KERN_INFO "Timer expired\n"); 
    if (waterforce_device!=NULL) {
	struct waterforce_data *priv = hid_get_drvdata(waterforce_device);
	if (priv) {
        	get_thermal_zone_id_of_cpu(priv);
	        get_hwmon_id_of_waterforce(priv);

		char buf[100];
		char path[100];
		sprintf(path,"/sys/class/hwmon/hwmon%d/fan1_input",priv->hwmonid);
		read_file(path,buf,sizeof(buf));
	}
    }
	
}

static int waterforce_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct waterforce_data *priv;
	int ret;

	priv = devm_kzalloc(&hdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->hdev = hdev;
	hid_set_drvdata(hdev, priv);
        priv->updating=false;
        waterforce_device=hdev;
	/*
	 * Initialize ->updated to STATUS_VALIDITY seconds in the past, making
	 * the initial empty data invalid for waterforce_read without the need for
	 * a special case there.
	 */
	priv->updated = jiffies - STATUS_VALIDITY * HZ;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "hid parse failed with %d\n", ret);
		return ret;
	}

	/*
	 * Enable hidraw so existing user-space tools can continue to work.
	 */
	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "hid hw start failed with %d\n", ret);
		goto fail_and_stop;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "hid hw open failed with %d\n", ret);
		goto fail_and_close;
	}

	priv->buffer = devm_kzalloc(&hdev->dev, MAX_REPORT_LENGTH, GFP_KERNEL);
	if (!priv->buffer) {
		ret = -ENOMEM;
		goto fail_and_close;
	}

	mutex_init(&priv->buffer_lock);

	priv->hwmon_dev = hwmon_device_register_with_info(&hdev->dev, "waterforce",
							  priv, &waterforce_chip_info, NULL);
	if (IS_ERR(priv->hwmon_dev)) {
		ret = PTR_ERR(priv->hwmon_dev);
		hid_err(hdev, "hwmon registration failed with %d\n", ret);
		goto fail_and_close;
	}
	get_cpu_freq(priv);
	return 0;

fail_and_close:
	hid_hw_close(hdev);
fail_and_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void waterforce_remove(struct hid_device *hdev)
{
	struct waterforce_data *priv = hid_get_drvdata(hdev);

	hwmon_device_unregister(priv->hwmon_dev);

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id waterforce_table[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_GIGABYTE, USB_PRODUCT_ID_WATERFORCE_1) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_GIGABYTE, USB_PRODUCT_ID_WATERFORCE_2) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_GIGABYTE, USB_PRODUCT_ID_WATERFORCE_3) },
	{ }
};

MODULE_DEVICE_TABLE(hid, waterforce_table);

static struct hid_driver waterforce_driver = {
	.name = "waterforce",
	.id_table = waterforce_table,
	.probe = waterforce_probe,
	.remove = waterforce_remove,
	.raw_event = waterforce_raw_event,
};






static struct task_struct *waterforce_timer_thread;

static int waterforce_thread_fn(void *data)
{
//	timer_setup(&waterforce_timer,waterforce_timer_callback,0);
//        mod_timer(&waterforce_timer, jiffies + msecs_to_jiffies(TIMER_WAKEUP_MS));

    while (!kthread_should_stop()) {
	set_current_state(TASK_INTERRUPTIBLE);
//	printk("waterforce_thread_fn\n");

	waterforce_timer_callback(NULL);
	schedule_timeout(HZ*TIMER_WAKEUP_S);
    }
    return 0;
}
static int __init waterforce_init(void)
{
        waterforce_timer_thread = kthread_run(waterforce_thread_fn,NULL,"waterforce_timer_thread");
	if (IS_ERR(waterforce_timer_thread)) {
	        printk(KERN_ERR "Waterforce: Failed to create thread\n");
//        	del_timer(&waterforce_timer);
	        return PTR_ERR(waterforce_timer_thread);
   	}
	return hid_register_driver(&waterforce_driver);
}

static void __exit waterforce_exit(void)
{
//	del_timer_sync(&waterforce_timer);
//	wake_up_process(waterforce_timer_thread);
	kthread_stop(waterforce_timer_thread);
	hid_unregister_driver(&waterforce_driver);
}


/*
 * When compiled into the kernel, initialize after the hid bus.
 */
late_initcall(waterforce_init);
module_exit(waterforce_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hwmon driver for Gigabyte AORUS Waterforce AIO coolers");

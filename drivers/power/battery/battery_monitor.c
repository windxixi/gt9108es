/*
 * module/samsung_battery/battery_monitor.c
 *
 * SAMSUNG battery driver for Linux
 *
 * Copyright (C) 2009 SAMSUNG ELECTRONICS.
 * Author: EUNGON KIM (egstyle.kim@samsung.com)
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/i2c/twl6030-gpadc.h>
#include <linux/i2c/twl.h>
#include <linux/power_supply.h>
#include <linux/wakelock.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/android_alarm.h>
#include "common.h"

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#define DG_TEST 0

#if DG_TEST
#include <plat/microusbic.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#endif


#include <linux/gpio.h>
#include <plat/mux.h>

#define DRIVER_NAME "secBattMonitor"

#define TEMP_DEG    0
#define TEMP_ADC    1

#define FAST_POLL  30
#define SLOW_POLL  60

//#define _OMS_FEATURES_ // CHINA BORQS CONCEPTS

#if defined(DG_TEST) || defined(CONFIG_MACH_CHN_OPEN_ALARM)
extern int get_real_usbic_state(void);
#endif


static DEFINE_MUTEX(battery_lock);

#if 0
static int NCP15WB473J_batt_table[] = {

	/* -10C ~ 70C */

	/* 0[-10] ~ 9[-1] */
	727, 714, 701, 688, 675,
	662, 649, 635, 625, 615,

	/* 10[0] ~ 49[39] */
	604, 587, 569, 552, 534,
	517, 499, 482, 464, 447,
	429, 418, 407, 396, 385,
	375, 364, 353, 342, 331,
	320, 311, 302, 293, 284,
	276, 267, 258, 249, 240,
	231, 224, 217, 210, 203,
	196, 188, 181, 174, 167,

	/* 50[40] ~ 64[54] */
	160, 155, 151, 146, 141,
	137, 132, 127, 123, 118,
	113, 110, 106, 103, 100,

	/* 65[55] ~ 80[70] */
	97, 93, 90, 87, 83,
	80, 77, 74, 72, 69,
	66, 64, 62, 60, 58,
	56,
};
#endif

static int NCP15WB473J_batt_table[] = {

	/* -10C ~ 70C */

	/* 0[-10] ~ 9[-1] */
	725, 706, 686, 667, 648,
	629, 609, 590, 576, 562,

	/* 10[0] ~ 49[39] */
	548, 535, 523, 510, 498,
	485, 472, 460, 447, 435,
	422, 411, 400, 389, 378,
	367, 355, 344, 333, 322,
	311, 302, 293, 284, 275,
	266, 257, 248, 239, 230,
	221, 215, 210, 204, 199,
	193, 187, 182, 176, 171,

	/* 50[40] ~ 64[54] */
	165, 159, 152, 146, 142,
	138, 134, 129, 125, 121,
	117, 113, 109, 104, 100,

	/* 65[55] ~ 80[70] */
	96, 92, 88, 83, 79,
	75, 72, 69, 67, 64,
	61, 60, 59, 58, 57,
	56,
};

struct battery_device_config
// THIS CONFIG IS SET IN BOARD_FILE.(platform_data)
{
	/* SUPPORT MONITORING CHARGE CURRENT FOR CHECKING FULL */
	int MONITORING_CHG_CURRENT;
	int CHG_CURRENT_ADC_PORT;

	/* SUPPORT MONITORING TEMPERATURE OF THE SYSTEM FOR BLOCKING CHARGE */
	int MONITORING_SYSTEM_TEMP;
	int TEMP_ADC_PORT;

	/* Check Battery state for LPM */
	int boot_mode;
	unsigned int (*pm_poweroff) (void);

};

struct battery_device_info {
	struct device *dev;
	struct delayed_work battery_monitor_work;
	struct alarm        alarm;

	struct power_supply sec_battery;
	struct power_supply sec_ac;
	struct power_supply sec_usb;

	struct early_suspend early_suspend;

	ktime_t             last_poll;

	bool                is_sleep;
	
};

static struct device *this_dev;
static struct wake_lock sec_bc_wakelock;

static SEC_battery_charger_info sec_bci;
static struct battery_device_config *device_config;


/* [TODO] should check */
/* static struct gptimer12_timer batt_gptimer_12; */

static char *samsung_bci_supplied_to[] = {
	"battery",
};


SEC_battery_charger_info *get_sec_bci(void)
{
	return &sec_bci;
}


static enum full_charge_mode {
	NOT_FULL = 0,
	DISPLAY_FULL,
	END_FULL,
};


// Prototype
int _charger_state_change_(int, int);
int _low_battery_alarm_(void);
int _get_average_value_(int *, int);
int _get_t2adc_data_(int);
static int get_elapsed_time_secs(unsigned long long *);
static int t2adc_to_temperature(int, int);
static int get_battery_level_adc(void);
static int get_battery_level_ptg(void);
static int get_system_temperature(bool);
static int get_charging_current_adc_val(void);
static enum full_charge_mode check_full_charge_using_chg_current(int);
static int battery_monitor_core(void);
static void battery_monitor_work_handler(struct work_struct *);
static void battery_program_alarm(struct battery_device_info *, int);
static void battery_monitor_alarm(struct alarm *);
static int samsung_battery_get_property(struct power_supply *,
					enum power_supply_property,
					union power_supply_propval *);
static int samsung_ac_get_property(struct power_supply *,
				   enum power_supply_property,
				   union power_supply_propval *);
static int samsung_usb_get_property(struct power_supply *,
				    enum power_supply_property,
				    union power_supply_propval *);
static void samsung_pwr_external_power_changed(struct power_supply *);
static ssize_t store_event(struct kobject *kobj,
			   struct kobj_attribute *attr, const char *buf,
			   size_t size);

static int __devinit battery_probe(struct platform_device *);
static int __devexit battery_remove(struct platform_device *);
static int battery_suspend(struct platform_device *, pm_message_t);
static int battery_resume(struct platform_device *);
static void battery_early_suspend(struct early_suspend *);
static void battery_early_resume(struct early_suspend *);
static int __init battery_init(void);
static void __exit battery_exit(void);

// Charger
extern int charger_init(void);
extern void charger_exit(void);
extern int _battery_state_change_(int category, int value, bool is_sleep);
extern int _cable_status_now_(void);

// Fuel Guage
extern int fuelgauge_init(void);
extern void fuelgauge_exit(void);
extern int fuelgauge_quickstart(void);
extern int get_fuelgauge_adc_value(int count);
extern int get_fuelgauge_ptg_value(void);

#if 0
extern int update_rcomp_by_temperature(int temp);
#endif

extern unsigned long long sched_clock(void);

extern u32 sec_bootmode;

static bool boot_complete = false;
static int boot_monitor_count = 0;

int stop_temperature_overheat = CHARGE_STOP_TEMPERATURE_MAX;
int recover_temperature_overheat = CHARGE_RECOVER_TEMPERATURE_MAX;

#ifdef CONFIG_SEC_BATTERY_USE_RECOVERY_MODE
static int recovery_mode = 0;
module_param(recovery_mode, bool, 0);
#endif				/* CONFIG_SEC_BATTER_USE_RECOVERY_MODE */


// ------------------------------------------------------------------------- //
//                           sysfs interface                                 //
// ------------------------------------------------------------------------- //
#define  __ATTR_SHOW_CALLBACK( _name, _ret_val ) \
static ssize_t _name( struct kobject *kobj, \
		      struct kobj_attribute *attr,	\
		      char *buf )			\
{ \
    return sprintf ( buf, "%d\n", _ret_val ); \
}

static ssize_t store_fuelgauge_reset(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t size)
{
	fuelgauge_quickstart();
	return size;
}

static int get_batt_monitor_temp(void)
{
	return sec_bci.battery.support_monitor_temp;
}

static ssize_t store_batt_monitor_temp(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       const char *buf, size_t size)
{
	int flag;

	sscanf(buf, "%d", &flag);

	printk("[BM] change value %d\n", flag);

	sec_bci.battery.support_monitor_temp = flag;
	sec_bci.battery.support_monitor_timeout = flag;
	sec_bci.battery.support_monitor_full = flag;

	return size;
}

static ssize_t store_batt_boot_complete(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t size)
{
	int flag;

	sscanf(buf, "%d", &flag);
	printk("[BM] boot complete flag:%d, buf:%s, size:%d\n", flag, buf,
	       size);

	boot_complete = true;

	return size;
}

static ssize_t show_check_lp_charging( struct kobject *kobj,
		      struct kobj_attribute *attr, char *buf )
{
	int ret;

	if( device_config->boot_mode == 5 ) {
		ret = 1;
		if( sec_bci.charger.charge_status == POWER_SUPPLY_STATUS_DISCHARGING ) {
			if( device_config->pm_poweroff ) {
				device_config->pm_poweroff();
			} else {
				pr_info("pm_poweroff is NULL !!\n");
			}
		}
	} else {
		ret = 0;
	}
	sprintf ( buf, "%d\n", ret );
	printk("%s, boot_mode = %d, charge_status = %d\n", __func__,
		device_config->boot_mode, sec_bci.charger.charge_status);

    return ret;
}

static ssize_t show_battery_type( struct kobject *kobj,
		      struct kobj_attribute *attr, char *buf )
{
	return sprintf ( buf, "%s\n", "SDI_SDI" ); \
}

__ATTR_SHOW_CALLBACK(show_batt_vol, get_battery_level_adc() * 1000)
__ATTR_SHOW_CALLBACK(show_batt_vol_adc, 0)
__ATTR_SHOW_CALLBACK(show_batt_temp, get_system_temperature(TEMP_DEG) * 10)
__ATTR_SHOW_CALLBACK(show_batt_temp_adc, get_system_temperature(TEMP_ADC))
__ATTR_SHOW_CALLBACK(show_batt_v_f_adc, 0)
__ATTR_SHOW_CALLBACK(show_batt_capacity, get_battery_level_ptg())
__ATTR_SHOW_CALLBACK(show_batt_monitor_temp, get_batt_monitor_temp())
__ATTR_SHOW_CALLBACK(show_batt_temp_check, sec_bci.battery.battery_health)
__ATTR_SHOW_CALLBACK(show_batt_full_check,
		     (sec_bci.charger.charge_status ==
		  POWER_SUPPLY_STATUS_FULL) ? 1 : 0)
__ATTR_SHOW_CALLBACK(show_charging_source, sec_bci.charger.cable_status)
#ifdef _OMS_FEATURES_
__ATTR_SHOW_CALLBACK(show_batt_vol_toolow,
		     sec_bci.battery.battery_vol_toolow)
static struct kobj_attribute batt_vol_toolow =
__ATTR(batt_vol_toolow, 0644, show_batt_vol_toolow, NULL);
#endif


static struct kobj_attribute batt_sysfs_testmode[] = {
	/* Event logging - Put these attributes at first position of this array
	   For using the call back function 'store_event'
	*/
	__ATTR(mp3, 0664, NULL, store_event),
	__ATTR(talk_wcdma, 0664, NULL, store_event),
	__ATTR(talk_gsm, 0664, NULL, store_event),
	__ATTR(data_call, 0664, NULL, store_event),
	__ATTR(vt_call, 0664, NULL, store_event),
	__ATTR(camera_preview, 0664, NULL, store_event),
	__ATTR(camera_recording, 0664, NULL, store_event),
	__ATTR(video, 0664, NULL, store_event),
	__ATTR(g_map, 0664, NULL, store_event),
	__ATTR(e_book, 0664, NULL, store_event),
	__ATTR(bt_call, 0664, NULL, store_event),
	__ATTR(wap_browsing, 0664, NULL, store_event),
	__ATTR(wifi_browsing, 0664, NULL, store_event),
	__ATTR(browser, 0664, NULL, store_event),
	__ATTR(game, 0664, NULL, store_event),
	/* END of Event logging */

	__ATTR(batt_vol, 0644, show_batt_vol, NULL),
	__ATTR(batt_vol_adc, 0644, show_batt_vol_adc, NULL),
	__ATTR(batt_temp, 0644, show_batt_temp, NULL),
	__ATTR(batt_temp_adc, 0644, show_batt_temp_adc, NULL),
	__ATTR(batt_v_f_adc, 0644, show_batt_v_f_adc, NULL),
	__ATTR(batt_capacity, 0644, show_batt_capacity, NULL),
	__ATTR(fg_reset_soc, 0644, NULL, store_fuelgauge_reset),
	__ATTR(batt_monitor_temp, 0644, show_batt_monitor_temp,
	       store_batt_monitor_temp),
	__ATTR(batt_boot_complete, 0644, NULL, store_batt_boot_complete),
	__ATTR(fg_read_soc, 0644, show_batt_capacity, NULL),
	__ATTR(batt_temp_check, 0644, show_batt_temp_check, NULL),
	__ATTR(batt_full_check, 0644, show_batt_full_check, NULL),
	__ATTR(charging_source, 0644, show_charging_source, NULL),
	__ATTR(batt_lp_charging, 0644, show_check_lp_charging, NULL),
	__ATTR(batt_type, 0644, show_battery_type, NULL),

};

/* Event logging */
u32 event_logging = 0;

enum {
	MP3 =0, TALK_WCDMA, TALK_GSM, DATA_CALL, VT_CALL, CAMERA_PREVIEW,
	CAMERA_RECORDING,
	VIDEO, G_MAP, E_BOOK, BT_CALL, WAP_BROWSING, WIFI_BROWSING, BROWSER,
	GAME
};
static ssize_t store_event(struct kobject *kobj,
			   struct kobj_attribute *attr,
			   const char *buf, size_t size)
{
	int flag;

	const ptrdiff_t off = attr - batt_sysfs_testmode;

	sscanf(buf, "%d", &flag);

	if (flag == 1)
		event_logging |= (0x1 << off);
	else if (flag == 0)
		event_logging &= ~(0x1 << off);

	printk("[BM] store_event offset=%d, value=%d\n", off, flag);
	return size;
}

/* END of Event logging */

int _charger_state_change_(int category, int value)
{

	struct battery_device_info *di;
	struct platform_device *pdev;

	pdev = to_platform_device(this_dev);
	di = platform_get_drvdata(pdev);


	printk("[BM] cate: %d, value: %d\n", category, value);

	if (category == STATUS_CATEGORY_CABLE) {
		switch (value) {
		case POWER_SUPPLY_TYPE_BATTERY:
			/*Stop monitoring the batt. level for Re-charging */
			sec_bci.battery.monitor_field_rechg_vol = false;

			/*Stop monitoring the temperature */
			sec_bci.battery.monitor_field_temp = false;

			sec_bci.battery.confirm_full_by_current = 0;
			sec_bci.battery.confirm_recharge = 0;

			sec_bci.charger.charging_timeout = DEFAULT_CHARGING_TIMEOUT;

			break;

		case POWER_SUPPLY_TYPE_MAINS:
			sec_bci.charger.charging_timeout = DEFAULT_CHARGING_TIMEOUT;
			wake_lock_timeout(&sec_bc_wakelock, HZ);
			break;

		case POWER_SUPPLY_TYPE_USB:
			break;

		default:
			break;
		}

		goto Out_Charger_State_Change;
	} else if (category == STATUS_CATEGORY_CHARGING) {
		switch (value) {
		case POWER_SUPPLY_STATUS_UNKNOWN:
		case POWER_SUPPLY_STATUS_DISCHARGING:
			//sec_bci.charger.full_charge = false;

			/*Stop monitoring the batt. level for Re-charging */
			sec_bci.battery.monitor_field_rechg_vol = false;

			if (sec_bci.battery.battery_health !=
			    POWER_SUPPLY_HEALTH_OVERHEAT
			    && sec_bci.battery.battery_health !=
			    POWER_SUPPLY_HEALTH_COLD) {
				/*Stop monitoring the temperature */
				sec_bci.battery.monitor_field_temp = false;
			}

			break;

		case POWER_SUPPLY_STATUS_NOT_CHARGING:
			break;

		case POWER_SUPPLY_STATUS_FULL:
			/*Start monitoring the batt. level for Re-charging */
			sec_bci.battery.monitor_field_rechg_vol = true;

			/*Start monitoring the temperature */
			sec_bci.battery.monitor_field_temp = true;
			break;

		case POWER_SUPPLY_STATUS_FULL_END:
			/*Start monitoring the batt. level for Re-charging */
			sec_bci.battery.monitor_field_rechg_vol = true;

			/*Start monitoring the temperature */
			sec_bci.battery.monitor_field_temp = true;

			break;

		case POWER_SUPPLY_STATUS_CHARGING:
			/*Start monitoring the temperature */
			sec_bci.battery.monitor_field_temp = true;

			/*Stop monitoring the batt. level for Re-charging */
			sec_bci.battery.monitor_field_rechg_vol = false;

			break;

		case POWER_SUPPLY_STATUS_RECHARGING_FOR_FULL:
			/*Start monitoring the temperature */
			sec_bci.battery.monitor_field_temp = true;

			/*Stop monitoring the batt. level for Re-charging */
			sec_bci.battery.monitor_field_rechg_vol = false;

			/*Not change the battery bar - keep battery full screen */
			//goto Out_Charger_State_Change;
			break;

		case POWER_SUPPLY_STATUS_RECHARGING_FOR_TEMP:
			/*Start monitoring the temperature */
			sec_bci.battery.monitor_field_temp = true;

			/*Stop monitoring the batt. level for Re-charging */
			sec_bci.battery.monitor_field_rechg_vol = false;

			break;

		default:
			break;
		}

	}


	power_supply_changed(&di->sec_battery);
	power_supply_changed(&di->sec_ac);
	power_supply_changed(&di->sec_usb);

	if (di->is_sleep) {
		/* [TODO] */
		/* [TODO] should insert something instead of gptimer12 */

		/* release_gptimer12( &batt_gptimer_12 ); */
		/* request_gptimer12( &batt_gptimer_12 ); */
			

	} else {
		cancel_delayed_work(&di->battery_monitor_work);
		queue_delayed_work(sec_bci.sec_battery_workq,
				   &di->battery_monitor_work, 5 * HZ);

	} 

Out_Charger_State_Change:
	return 0;
}

int _low_battery_alarm_()
{
	struct battery_device_info *di;
	struct platform_device *pdev;
	int level;

	pdev = to_platform_device(this_dev);
	di = platform_get_drvdata(pdev);

	level = get_battery_level_ptg();
	if (level == 1)
		sec_bci.battery.battery_level_ptg = 0;
	else
		sec_bci.battery.battery_level_ptg = level;

	wake_lock_timeout(&sec_bc_wakelock, HZ);
	power_supply_changed(&di->sec_battery);

	return 0;
}

int _get_average_value_(int *data, int count)
{
	int average;
	int min = 0;
	int max = 0;
	int sum = 0;
	int i;

	if (count >= 5) {
		min = max = data[0];
		for (i = 0; i < count; i++) {
			if (data[i] < min)
				min = data[i];

			if (data[i] > max)
				max = data[i];

			sum += data[i];
		}
		average = (sum - max - min) / (count - 2);
	} else {
		for (i = 0; i < count; i++)
			sum += data[i];

		average = sum / count;
	}

	return average;
}


int _get_t2adc_data_(int ch)
{
	int ret = 0;
	int val[5];
	int i;
	struct twl6030_gpadc_request req;


	req.channels = (1 << ch);
	req.method = TWL6030_GPADC_SW2;
	req.active = 0;
	req.func_cb = NULL;

	for (i = 0; i < 5; i++) {
		twl6030_gpadc_conversion(&req);
		val[i] = req.rbuf[ch];
	}

	ret = _get_average_value_(val, 5);

	return ret;
}

static int get_elapsed_time_secs(unsigned long long *start)
{
	unsigned long long now;
	unsigned long long diff;
	unsigned long long max = 0xFFFFFFFF;

	max = (max << 32) | 0xFFFFFFFF;

	now = sched_clock();

	if (now >= *start) {
		diff = now - *start;
	} else {
		sec_bci.charger.charge_start_time = now;
		diff = 0;
		//diff = max - *start + now;
	}

	do_div(diff, 1000000000L);
	/*
	  printk( KERN_DEBUG "[BM] now: %llu, start: %llu, diff:%d\n",
	  now, *start, (int)diff );
	*/
	return (int) diff;
}

static int t2adc_to_temperature(int value, int channel)
{
	int temp;

	/*calculating temperature */
	for (temp = 80; temp >= 0; temp--) {
		if ((NCP15WB473J_batt_table[temp] - value) >= 0)
			break;
	}

	temp = temp - 10;

	return temp;
}

static int get_battery_level_adc(void)
{
	int value;
	value = get_fuelgauge_adc_value(5);
	if (value < 0)
		value = sec_bci.battery.battery_level_vol;

#ifdef CONFIG_SAMSUNG_BATTERY_TESTMODE
	return 4100;
#else
	return value;

#endif

}

static int get_adjusted_battery_ptg(int value)
{
	if (value == 100)
		value = 100;
	else if (value < 30)
		value = ((value * 100 * 4 / 3) + 50) / 115;
	else if (value < 76)
		value = value + 5;
	else
		value = ((value * 100 - 7600) * 8 / 10 + 50) / 80 + 81;

	if (value > 100)
		value = 100;
	return value;
}

static int get_battery_level_ptg(void)
{
	int value;

	value = get_fuelgauge_ptg_value();

	/* adjust percentage value for Latona */
	//if(!sec_bci.charger.is_charging)
	/* value = get_adjusted_battery_ptg(value); */
/*
  if ( sec_bci.charger.is_charging && value >= 100)
  value = 99;
*/
	if (sec_bci.charger.charge_status == POWER_SUPPLY_STATUS_FULL)
		value = 100;

	if (!boot_complete && value <= 0)
		value = 1;

#ifdef CONFIG_SAMSUNG_BATTERY_TESTMODE
	return 60;
#else
	return value;

#endif
}

static int get_system_temperature(bool flag)
{
	int adc;
	int temp;

	adc = _get_t2adc_data_(device_config->TEMP_ADC_PORT);

	if (flag)
		return adc;

	temp = t2adc_to_temperature(adc, device_config->TEMP_ADC_PORT);

//      return 70;
	return temp;
}

static int get_charging_current_adc_val(void)
{
	int adc;

	adc = _get_t2adc_data_(device_config->CHG_CURRENT_ADC_PORT);

	return adc;
}


static enum full_charge_mode check_full_charge_using_chg_current(int charge_current_adc)
{

	if (sec_bci.battery.battery_level_vol < 4000) {
		sec_bci.battery.confirm_full_by_current = 0;
		return NOT_FULL;
	}

	if (sec_bci.battery.support_monitor_full) {
		if (charge_current_adc <= CHARGE_FULL_CURRENT_DISPLAY_ADC
		    && charge_current_adc > CHARGE_FULL_CURRENT_END_ADC) {
			sec_bci.battery.confirm_full_by_current++;

			// changing freq. of monitoring adc to Burst.
			sec_bci.battery.monitor_duration = 5;
		} else if (charge_current_adc <= CHARGE_FULL_CURRENT_END_ADC) {
			sec_bci.battery.monitor_duration = MONITOR_DEFAULT_DURATION;
			sec_bci.battery.confirm_full_by_current = 0;

			return END_FULL;
		} else {
			sec_bci.battery.confirm_full_by_current = 0;
			// changing freq. of monitoring adc to Default.
			/* batt_gptimer_12.expire_time = MONITOR_DURATION_DUR_SLEEP; */
			sec_bci.battery.monitor_duration = MONITOR_DEFAULT_DURATION;
		}

		if (sec_bci.battery.confirm_full_by_current >= 4) {
			/* batt_gptimer_12.expire_time = MONITOR_DURATION_DUR_SLEEP; */
			sec_bci.battery.monitor_duration = MONITOR_DEFAULT_DURATION;
			sec_bci.battery.confirm_full_by_current = 0;

			return DISPLAY_FULL;
		}
	}
	return NOT_FULL;
}


static int battery_monitor_core(void)
{

	int charging_time;
	int rechg_voltage;

	struct battery_device_info *di;
	struct platform_device *pdev;

	pdev = to_platform_device(this_dev);
	di = platform_get_drvdata(pdev);


	if (event_logging) {
		stop_temperature_overheat = CHARGE_STOP_TEMPERATURE_EVENT;
		recover_temperature_overheat = CHARGE_RECOVER_TEMPERATURE_EVENT;
	} else {
		stop_temperature_overheat = CHARGE_STOP_TEMPERATURE_MAX;
		recover_temperature_overheat = CHARGE_RECOVER_TEMPERATURE_MAX;
	}

    /*Monitoring the system temperature */
	if (sec_bci.battery.monitor_field_temp) {
		if (sec_bci.battery.support_monitor_timeout) {
			/*Check charging time */
			charging_time =
				get_elapsed_time_secs(&sec_bci.charger.charge_start_time);
			if (charging_time >= sec_bci.charger.charging_timeout) {
				_battery_state_change_(STATUS_CATEGORY_CHARGING,
									   POWER_SUPPLY_STATUS_CHARGING_OVERTIME, di->is_sleep);
				return -1;
			}
		}

		if (sec_bci.battery.support_monitor_temp) {
			if (sec_bci.battery.battery_health ==
			    POWER_SUPPLY_HEALTH_OVERHEAT
			    || sec_bci.battery.battery_health ==
			    POWER_SUPPLY_HEALTH_COLD) {
				if (sec_bci.battery.battery_temp <= recover_temperature_overheat//CHARGE_RECOVER_TEMPERATURE_MAX
				    && sec_bci.battery.battery_temp >=
				    CHARGE_RECOVER_TEMPERATURE_MIN) {
					sec_bci.battery.battery_health =
						POWER_SUPPLY_HEALTH_GOOD;
					_battery_state_change_(STATUS_CATEGORY_TEMP,
							       BATTERY_TEMPERATURE_NORMAL, di->is_sleep);
				}

			} else {
				if (sec_bci.battery.monitor_duration >  MONITOR_TEMP_DURATION)
					sec_bci.battery.monitor_duration =	MONITOR_TEMP_DURATION;

				if (sec_bci.battery.battery_temp >= stop_temperature_overheat)	//CHARGE_STOP_TEMPERATURE_MAX )
				{
					printk("[TA] Temperature is high (%d*)\n",
					       sec_bci.battery.battery_temp);
					if (sec_bci.battery.battery_health !=
					    POWER_SUPPLY_HEALTH_OVERHEAT) {
						sec_bci.battery.battery_health =
							POWER_SUPPLY_HEALTH_OVERHEAT;

						_battery_state_change_(STATUS_CATEGORY_TEMP,
								       BATTERY_TEMPERATURE_HIGH, di->is_sleep);
					}
				} else if (sec_bci.battery.battery_temp <=
					   CHARGE_STOP_TEMPERATURE_MIN) {
					printk("[TA] Temperature is low (%d*)\n",
					       sec_bci.battery.battery_temp);
					if (sec_bci.battery.battery_health !=
					    POWER_SUPPLY_HEALTH_COLD) {
						sec_bci.battery.battery_health =
							POWER_SUPPLY_HEALTH_COLD;

						_battery_state_change_(STATUS_CATEGORY_TEMP,
								       BATTERY_TEMPERATURE_LOW, di->is_sleep);
					}
				} else {
					if (sec_bci.battery.battery_health !=
					    POWER_SUPPLY_HEALTH_GOOD) {
						sec_bci.battery.battery_health =
							POWER_SUPPLY_HEALTH_GOOD;
						_battery_state_change_(STATUS_CATEGORY_TEMP,
								       BATTERY_TEMPERATURE_NORMAL, di->is_sleep);
					}
				}
			}
		}
	}

	/*Monitoring the battery level for Re-charging */
	if (sec_bci.battery.monitor_field_rechg_vol
	    && (sec_bci.charger.rechg_count <= 0 || di->is_sleep)) {

		if (sec_bci.battery.monitor_duration > MONITOR_RECHG_VOL_DURATION)
			sec_bci.battery.monitor_duration = MONITOR_RECHG_VOL_DURATION;

		if (sec_bootmode == 5)	// offmode charging
			rechg_voltage = CHARGE_RECHG_VOLTAGE_OFFMODE;
		else
			rechg_voltage = CHARGE_RECHG_VOLTAGE;

		if (sec_bci.battery.battery_level_vol <= rechg_voltage) {
			sec_bci.battery.confirm_recharge++;
			if (sec_bci.battery.confirm_recharge >= 2) {
				printk("[BM] RE-charging vol rechg_voltage = %d\n",
				       rechg_voltage);
				sec_bci.battery.confirm_recharge = 0;
				_battery_state_change_(STATUS_CATEGORY_CHARGING,
						       POWER_SUPPLY_STATUS_RECHARGING_FOR_FULL, di->is_sleep);
			}
		} else {
			sec_bci.battery.confirm_recharge = 0;
		}
	}

	return 0;
}

#if defined(CONFIG_MACH_CHN_OPEN_ALARM)
static u8 t_alarm_time[6];
extern int twl_i2c_read_u8(u8 mod_no, u8 *value, u8 reg);
extern unsigned bcd2bin(unsigned char val);

static int check_alarm_boot_kernel()
{
	int ret;
	u8 i;
	unsigned time_sec, alarm_sec;
	u8 t_time[6];

	// current time read
	for(i = 0x00 ; i < 0x06 ; i++){
		ret = twl_i2c_read_u8(0x16,&t_time[i], i);
		if (ret < 0)
		pr_err("twl_rtc: Could not read TWL"
		       "register %X - error %d\n", i, ret);
	}
	printk("%s conversion : time(%04d.%02d.%02d %02d:%02d:%02d)\n", __func__,
        t_time[5], t_time[4], t_time[3], t_time[2], t_time[1], t_time[0]);

	printk("%s conversion : alarm(%04d.%02d.%02d %02d:%02d:%02d)\n", __func__,
        t_alarm_time[5], t_alarm_time[4], t_alarm_time[3], t_alarm_time[2], t_alarm_time[1], t_alarm_time[0]);

	if((t_time[5]==t_alarm_time[5]) && (t_time[4]==t_alarm_time[4]) 
		&& (t_time[3]==t_alarm_time[3]) && (t_time[2]==t_alarm_time[2])){

		time_sec = bcd2bin(t_time[1])*60 + bcd2bin(t_time[0]);
		alarm_sec = bcd2bin(t_alarm_time[1])*60 + bcd2bin(t_alarm_time[0]);

		if((time_sec > alarm_sec - 25) && (time_sec < alarm_sec + 5))
			return 1;
	}

	return 0;
}
#endif /* auto alarm boot for CHN - bsystar */

static void battery_monitor_work_handler(struct work_struct *work)
{
	int charge_current_adc;
	unsigned long flags;
	enum full_charge_mode is_full;
#if defined(CONFIG_MACH_CHN_OPEN_ALARM)
	int ret = 0;
	u8 alm_reg = 0;
	u8 alm_int = 0;
	extern u32 sec_bootmode;
	extern void machine_restart(char *cmd);
#endif

	struct battery_device_info *di = container_of(work,
						  struct  battery_device_info,
						  battery_monitor_work.work);

#if 0
	printk
		("[BM] battery monitor [Level:%d, ADC:%d, TEMP.:%d, cable: %d] \n",
		 get_battery_level_ptg(),
		 get_battery_level_adc(),
		 get_system_temperature(),
		 sec_bci.charger.cable_status);
#endif

	boot_monitor_count++;
	if (!boot_complete && boot_monitor_count >= 2) {
		printk("[BM] boot complete \n");
		boot_complete = true;
	}
	if (sec_bci.charger.rechg_count > 0)
		sec_bci.charger.rechg_count--;


//      printk("[BM] MMC2_DAT0 : %x\n", omap_readw(0x4800215c));

	if (device_config->MONITORING_SYSTEM_TEMP)
		sec_bci.battery.battery_temp = get_system_temperature(TEMP_DEG);
	else
		sec_bci.battery.battery_temp = 0;

#if 0
	update_rcomp_by_temperature(sec_bci.battery.battery_temp);
#endif

	/* Monitoring the battery info. */
	sec_bci.battery.battery_level_ptg = get_battery_level_ptg();
	msleep(10);
	sec_bci.battery.battery_level_vol = get_battery_level_adc();

	if (!(sec_bci.battery.monitor_field_temp)
	    && !(sec_bci.battery.monitor_field_rechg_vol)) {
		sec_bci.battery.monitor_duration = MONITOR_DEFAULT_DURATION;
	} else {
#if defined(CONFIG_MACH_CHN_OPEN_ALARM)
		if((sec_bootmode == 5) && check_alarm_boot_kernel()){
			printk( "IT_ALARM is 1\n");
			machine_restart(NULL);
		}
#endif /* auto alarm boot for CHN - bsystar */

		// Workaround : check status of cabel at this point.
		if (!_cable_status_now_()) {
			_battery_state_change_(STATUS_CATEGORY_ETC,
					       ETC_CABLE_IS_DISCONNECTED, di->is_sleep);
		}


		if (sec_bci.charger.is_charging
		    && device_config->MONITORING_CHG_CURRENT) {
			// in charging && enable monitor_chg_current
			charge_current_adc = get_charging_current_adc_val();
			is_full =
				check_full_charge_using_chg_current(charge_current_adc);

			switch(is_full) {
			case DISPLAY_FULL:
				_battery_state_change_(STATUS_CATEGORY_CHARGING,
						       POWER_SUPPLY_STATUS_FULL, di->is_sleep);
				break;

			case END_FULL:
				_battery_state_change_(STATUS_CATEGORY_CHARGING,
						       POWER_SUPPLY_STATUS_FULL_END, di->is_sleep);
				break;
			default:
				battery_monitor_core();
				break;
			}
		} else {
			battery_monitor_core();
		}
	}

#if 1
	printk
		("[BM] monitor BATT.(%d%%, %dmV, %d*, count=%d, charging=%d, Charging Current ADC : %d)\n",
		 sec_bci.battery.battery_level_ptg,
		 sec_bci.battery.battery_level_vol,
		 sec_bci.battery.battery_temp,
		 boot_monitor_count,
		 sec_bci.charger.is_charging,
		 get_charging_current_adc_val()
			);
#endif
    //printk("[BM] adc 167 -> %d^, adc 198 -> %d^\n", t2adc_to_temperature(927, 0), t2adc_to_temperature(884, 0));

#if DG_TEST
	printk("[BM] Temp ADC : %d\n", get_system_temperature(TEMP_ADC));

    if (1) {

	struct file *filp;
	char buf[1024];
	int count;
	int retval = 0;
	mm_segment_t fs;

	fs = get_fs();
	set_fs(KERNEL_DS);
	filp = filp_open("/tmp/b", 00000100 /*O_CREAT */  | 00000002,
			 0666);
	memset(buf, 0, 1024);
	sprintf(buf,
		"Voltage : %d,\nSOC : %d,\nCHG CURRENT MONITORING : %d,\nCABLE STATUS : %d,\nUSBIC STATUS : %d,\n",
		sec_bci.battery.battery_level_vol,
		sec_bci.battery.battery_level_ptg,
		get_charging_current_adc_val(),
		sec_bci.charger.cable_status, get_real_usbic_state()
	    );
	count = filp->f_op->write(filp, &buf, 1024, &filp->f_pos);
	retval = filp_close(filp, NULL);
	set_fs(fs);

    }
#endif

    power_supply_changed(&di->sec_battery);
    power_supply_changed(&di->sec_ac);
    power_supply_changed(&di->sec_usb);


    if(di->is_sleep) {

	    printk("====== battery_monitor_work_handler in sleep  ====\n");
	    local_irq_save(flags);
	    wake_unlock(&sec_bc_wakelock);
	    battery_program_alarm(di, sec_bci.battery.monitor_duration);
	    local_irq_restore(flags);

    } else {
	    queue_delayed_work(sec_bci.sec_battery_workq,
			       &di->battery_monitor_work,
			       sec_bci.battery.monitor_duration * HZ);
    }
    

}

static void battery_program_alarm(struct battery_device_info *di, int seconds)
{
	ktime_t low_interval = ktime_set(seconds, 0);
	ktime_t slack = ktime_set(20, 0);
	ktime_t next;

	next = ktime_add(di->last_poll, low_interval);

	printk("[BM] Wake-up alarm set : now->%d, next->%d \n",
	       di->last_poll.tv.sec, next.tv.sec);

	alarm_start_range(&di->alarm, next, ktime_add(next, slack));
}


static void battery_monitor_alarm(struct alarm *alarm)
{
	struct battery_device_info *di = container_of(alarm,
						  struct  battery_device_info,
						  alarm);

	/* di->last_poll = alarm_get_elapsed_realtime(); */
}


// ------------------------------------------------------------------------- //
//                            Power supply monitor                           //
// ------------------------------------------------------------------------- //

static enum power_supply_property samsung_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
#ifdef _OMS_FEATURES_
	POWER_SUPPLY_PROP_TEMP,
#endif
	POWER_SUPPLY_PROP_CAPACITY,	// in percents
};

static enum power_supply_property samsung_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static enum power_supply_property samsung_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static int samsung_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (sec_bci.charger.charge_status ==
			POWER_SUPPLY_STATUS_FULL_END
			|| sec_bci.charger.charge_status ==
			POWER_SUPPLY_STATUS_RECHARGING_FOR_FULL)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else if (sec_bci.charger.charge_status ==
			 POWER_SUPPLY_STATUS_RECHARGING_FOR_TEMP)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = sec_bci.charger.charge_status;

		break;

	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = sec_bci.battery.battery_health;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = sec_bci.battery.battery_level_vol * 1000;
		val->intval = val->intval <= 0 ? 0 : 1;
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = sec_bci.charger.cable_status;
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = sec_bci.battery.battery_technology;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = sec_bci.battery.battery_level_vol * 1000;
		break;
#ifdef _OMS_FEATURES_
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = sec_bci.battery.battery_temp * 10;
		break;
#endif
	case POWER_SUPPLY_PROP_CAPACITY:	/* in percents! */
		val->intval = sec_bci.battery.battery_level_ptg;
		break;


	default:
		return -EINVAL;
	}

    //printk("[BM] GET %d, %d  !!! \n", psp, val->intval );
	return 0;
}

static int samsung_ac_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (sec_bci.charger.cable_status == POWER_SUPPLY_TYPE_MAINS)
			val->intval = 1;
		else
			val->intval = 0;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = sec_bci.battery.battery_level_vol * 1000;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int samsung_usb_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (sec_bci.charger.cable_status == POWER_SUPPLY_TYPE_USB)
			val->intval = 1;
		else
			val->intval = 0;

		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = sec_bci.battery.battery_level_vol * 1000;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static void samsung_pwr_external_power_changed(struct power_supply *psy)
{
	//cancel_delayed_work(&di->twl4030_bci_monitor_work);
	//schedule_delayed_work(&di->twl4030_bci_monitor_work, 0);
}


// ------------------------------------------------------------------------- //
//                           Driver interface                                //
// ------------------------------------------------------------------------- //
static int __devinit battery_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i = 0;

	struct battery_device_info *di;

	/* [TODO] should check */
	/* printk( "[BM] Battery Probe... bootmode = %d\n",sec_bootmode); */
	pr_info("[BM] Battery Probe..\n");

	this_dev = &pdev->dev;

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	platform_set_drvdata(pdev, di);

	di->dev = &pdev->dev;
	device_config = pdev->dev.platform_data;

	INIT_DELAYED_WORK(&di->battery_monitor_work,
			  battery_monitor_work_handler);

	
	di->is_sleep = false;
	
	/*Create power supplies */
	di->sec_battery.name = "battery";
	di->sec_battery.type = POWER_SUPPLY_TYPE_BATTERY;
	di->sec_battery.properties = samsung_battery_props;
	di->sec_battery.num_properties = ARRAY_SIZE(samsung_battery_props);
	di->sec_battery.get_property = samsung_battery_get_property;
	di->sec_battery.external_power_changed =
		samsung_pwr_external_power_changed;

	di->sec_ac.name = "ac";
	di->sec_ac.type = POWER_SUPPLY_TYPE_MAINS;
	di->sec_ac.supplied_to = samsung_bci_supplied_to;
	di->sec_ac.num_supplicants = ARRAY_SIZE(samsung_bci_supplied_to);
	di->sec_ac.properties = samsung_ac_props;
	di->sec_ac.num_properties = ARRAY_SIZE(samsung_ac_props);
	di->sec_ac.get_property = samsung_ac_get_property;
	di->sec_ac.external_power_changed = samsung_pwr_external_power_changed;

	di->sec_usb.name = "usb";
	di->sec_usb.type = POWER_SUPPLY_TYPE_USB;
	di->sec_usb.supplied_to = samsung_bci_supplied_to;
	di->sec_usb.num_supplicants = ARRAY_SIZE(samsung_bci_supplied_to);
	di->sec_usb.properties = samsung_usb_props;
	di->sec_usb.num_properties = ARRAY_SIZE(samsung_usb_props);
	di->sec_usb.get_property = samsung_usb_get_property;
	di->sec_usb.external_power_changed =
		samsung_pwr_external_power_changed;



#ifdef CONFIG_HAS_EARLYSUSPEND
	di->early_suspend.suspend = battery_early_suspend;
	di->early_suspend.resume = battery_early_resume;
	register_early_suspend(&di->early_suspend);
#endif

	di->last_poll = alarm_get_elapsed_realtime();
	alarm_init(&di->alarm, ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP,
			   battery_monitor_alarm);

	ret = power_supply_register(&pdev->dev, &di->sec_battery);
	if (ret) {
		printk("[BM] Failed to register main battery, charger\n");
		goto batt_regi_fail1;
	}

	ret = power_supply_register(&pdev->dev, &di->sec_ac);
	if (ret) {
		printk("[BM] Failed to register ac\n");
		goto batt_regi_fail2;
	}

	ret = power_supply_register(&pdev->dev, &di->sec_usb);
	if (ret) {
		printk("[BM] Failed to register usb\n");
		goto batt_regi_fail3;
	}

#ifdef _OMS_FEATURES_
	// Create battery sysfs files for sharing battery information with platform.
	ret =
		sysfs_create_file(&di->sec_battery.dev->kobj,
			  &batt_vol_toolow.attr);
	if (ret) {
		printk("[BM] sysfs create fail - %s\n", batt_vol_toolow.attr.name);
	}
#endif

	for (i = 0; i < ARRAY_SIZE(batt_sysfs_testmode); i++) {
		ret = sysfs_create_file(&di->sec_battery.dev->kobj,
					&batt_sysfs_testmode[i].attr);
		if (ret) {
			printk("[BM] sysfs create fail - %s\n",
			       batt_sysfs_testmode[i].attr.name);
		}
	}

	/* Enable GPADC for monitoring temperature  */
	get_system_temperature(TEMP_DEG);


#ifdef CONFIG_SEC_BATTERY_USE_RECOVERY_MODE
	if (likely(recovery_mode == 0))
		queue_delayed_work(sec_bci.sec_battery_workq,
				   &di->battery_monitor_work, HZ / 2);
	else
		queue_delayed_work(sec_bci.sec_battery_workq,
				   &di->battery_monitor_work, 0);
#else
	queue_delayed_work(sec_bci.sec_battery_workq,
			   &di->battery_monitor_work, HZ / 2);
#endif
	sec_bci.ready = true;

#if defined(CONFIG_MACH_CHN_OPEN_ALARM)
	// alarm time read
	for(i = 0x00 ; i < 0x06 ; i++){
		ret = twl_i2c_read_u8(0x16,&t_alarm_time[i], i+0x08);
		if (ret < 0)
		pr_err("twl_rtc: Could not read TWL"
		       "register %X - error %d\n", i+0x08, ret);
	}
	printk("%s conversion : alarm(%04d.%02d.%02d %02d:%02d:%02d)\n", __func__,
        t_alarm_time[5], t_alarm_time[4], t_alarm_time[3], t_alarm_time[2], t_alarm_time[1], t_alarm_time[0]);
#endif /* auto alarm boot for CHN - bsystar */

	return 0;

batt_regi_fail3:
	power_supply_unregister(&di->sec_ac);

batt_regi_fail2:
	power_supply_unregister(&di->sec_battery);

batt_regi_fail1:
	alarm_cancel(&di->alarm);


	kfree(di);

	return ret;
}

static int __devexit battery_remove(struct platform_device *pdev)
{
	struct battery_device_info *di = platform_get_drvdata(pdev);

	alarm_cancel(&di->alarm);

	flush_scheduled_work();
	cancel_delayed_work(&di->battery_monitor_work);

	power_supply_unregister(&di->sec_ac);
	power_supply_unregister(&di->sec_battery);

	platform_set_drvdata(pdev, NULL);
	kfree(di);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void battery_early_suspend(struct early_suspend *handler)
{
	struct battery_device_info *di = container_of(handler,
						      struct battery_device_info,
						      early_suspend);

	printk("=======%s=======\n",__func__);

	if( sec_bci.charger.cable_status == POWER_SUPPLY_TYPE_BATTERY)
		cancel_delayed_work(&di->battery_monitor_work);

}

static void battery_early_resume(struct early_suspend *handler)
{
	struct battery_device_info *di = container_of(handler,
						      struct battery_device_info,
						      early_suspend);

	printk("=======%s=======\n",__func__);

	cancel_delayed_work(&di->battery_monitor_work);
	queue_delayed_work(sec_bci.sec_battery_workq,
			   &di->battery_monitor_work, HZ * 1);
}
#endif


static int battery_suspend_alarm(struct device *dev)
{
	struct battery_device_info *di = dev_get_drvdata(dev);

	cancel_delayed_work(&di->battery_monitor_work);

//	di->is_sleep = true;

	sec_bci.charger.rechg_count = 0;

	printk("=== battery_suspend_alarm ===\n");
	printk("=== CABLE_TYPE :   %d     ===\n",sec_bci.charger.cable_status);

	di->last_poll = alarm_get_elapsed_realtime();

	if( sec_bci.charger.cable_status == POWER_SUPPLY_TYPE_MAINS )
	{
		battery_program_alarm(di, SLOW_POLL);
	}

	return 0;
}


static int battery_resume_alarm(struct device *dev)
{
	struct battery_device_info *di = dev_get_drvdata(dev);

	printk("=== battery_resume_alarm ===\n");

	di->is_sleep = false;

	get_system_temperature(TEMP_DEG);
	
	wake_lock_timeout(&sec_bc_wakelock, HZ * 2);

	power_supply_changed(&di->sec_battery);
	power_supply_changed(&di->sec_ac);
	power_supply_changed(&di->sec_usb);

	queue_delayed_work(sec_bci.sec_battery_workq,
	 		   &di->battery_monitor_work, HZ * 1);

	return 0;
}


static const struct dev_pm_ops charger_pm_ops = {
	.prepare = battery_suspend_alarm,
	.complete = battery_resume_alarm,
};


struct platform_driver battery_platform_driver = {
	.probe = battery_probe,
	.remove = __devexit_p(battery_remove),
	.driver = {
		.name = DRIVER_NAME,
		.pm = &charger_pm_ops,
	},
};


static int __init battery_init(void)
{
	int ret;

	pr_alert("\n[BM] Battery Init\n");

	sec_bci.ready = false;

	sec_bci.battery.battery_health = POWER_SUPPLY_HEALTH_GOOD;
	sec_bci.battery.battery_technology = POWER_SUPPLY_TECHNOLOGY_LION;
	sec_bci.battery.battery_level_ptg = 0;
	sec_bci.battery.battery_level_vol = 0;
	sec_bci.battery.monitor_duration = MONITOR_DEFAULT_DURATION;
	sec_bci.battery.monitor_field_temp = false;
	sec_bci.battery.monitor_field_rechg_vol = false;
	sec_bci.battery.confirm_full_by_current = 0;
	sec_bci.battery.support_monitor_temp = 1;
	sec_bci.battery.support_monitor_timeout = 1;
	sec_bci.battery.support_monitor_full = 1;
	sec_bci.battery.confirm_recharge = 0;

	sec_bci.charger.prev_cable_status = -1;
	sec_bci.charger.cable_status = -1;
	sec_bci.charger.prev_charge_status = 0;
	sec_bci.charger.charge_status = 0;
	sec_bci.charger.is_charging = false;
	sec_bci.charger.charge_start_time = 0;
	sec_bci.charger.charged_time = 0;
	sec_bci.charger.charging_timeout = DEFAULT_CHARGING_TIMEOUT;
	sec_bci.charger.use_ta_nconnected_irq = false;
	sec_bci.charger.rechg_count = 0;
	sec_bci.sec_battery_workq =
		create_singlethread_workqueue("sec_battery_workq");


	/* Get the charger driver */
	if ((ret = charger_init() < 0)) {
		printk("[BM] Fail to get charger driver.\n");
		return ret;
	}

	/* Get the fuelgauge driver */
	if ((ret = fuelgauge_init() < 0)) {
		printk("[BM] Fail to get fuelgauge driver.\n");
		return ret;
	}

	wake_lock_init(&sec_bc_wakelock, WAKE_LOCK_SUSPEND, "samsung-battery");

	ret = platform_driver_register(&battery_platform_driver);

	return ret;
}

module_init(battery_init);

static void __exit battery_exit(void)
{
	/*Remove the charger driver */
	charger_exit();
	/*Remove the fuelgauge driver */
	fuelgauge_exit();

	/* [TODO] should check */
	/* finish_gptimer12(); */

	platform_driver_unregister(&battery_platform_driver);
	pr_alert("[BM] Battery Driver Exit\n");
}

module_exit(battery_exit);

MODULE_AUTHOR("EUNGON KIM <egstyle.kim@samsung.com>");
MODULE_DESCRIPTION("Samsung Battery monitor driver");
MODULE_LICENSE("GPL");


/**
 * header for modem control
 *
 * Copyright (C) 2010 Samsung Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#define DEBUG

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/irq.h>
//#include <linux/ioport.h>

#include <linux/phone_svn/modemctl.h>

#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#define MODEM_CTL_DEFAULT_WAKLOCK_HZ	(2*HZ)
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#ifdef CONFIG_SVNET_WHITELIST
#include "portlist.h"
#endif /* CONFIG_SVNET_WHITELIST */

/* request by HW */
#include <mach/sec_param.h>
#include <plat/mux.h>
#include <mach/sec_mux.h>
#include "../../../arch/arm/mach-omap2/mux.h"

#define DRVNAME "modemctl"

//#define USE_EARLYSUSPEND_TO_CTRL_PDAACTIVE_LOW
//#define USE_LATERESUME_TO_CTRL_PDAACTIVE_HIGH
#define USE_EARLYSUSPEND_TO_CTRL_WHITELIST

//#define SIM_DEBOUNCE_TIME_HZ	(HZ)

struct modemctl;

struct modemctl_ops {
	void (*modem_on)(struct modemctl *);
	void (*modem_off)(struct modemctl *);
	void (*modem_reset)(struct modemctl *);
	void (*modem_boot_on)(struct modemctl *);
	void (*modem_boot_off)(struct modemctl *);
};

struct modemctl_info {
	const char *name;
	struct modemctl_ops ops;
};

struct modemctl {
	int irq_phone_active;
#if defined ( CONFIG_CHN_CMCC_SPI_SPRD )
	int irq_cp_dump_int;
#endif
	//	int irq_sim_ndetect;

	unsigned gpio_phone_on;
	unsigned gpio_phone_active;
	unsigned gpio_pda_active;
	unsigned gpio_cp_reset;
	unsigned gpio_reset_req_n;
	unsigned gpio_cp_dump_int;

//	unsigned gpio_flm_sel;
//	unsigned gpio_con_cp_sel;
//	unsigned gpio_phone_on;
//	unsigned gpio_usim_boot;
//	unsigned gpio_sim_ndetect;
//	unsigned sim_reference_level;
//	unsigned sim_change_reset;
//	struct timer_list sim_irq_debounce_timer;

#ifdef CONFIG_HAS_WAKELOCK
	struct wake_lock mc_wlock;
	long	 waketime;
#endif	

	struct modemctl_ops *ops;

	struct class *class;
	struct device *dev;
	const struct attribute_group *group;

	struct work_struct work;

#if defined( CONFIG_HAS_EARLYSUSPEND ) && \
	( defined( USE_EARLYSUSPEND_TO_CTRL_PDAACTIVE_LOW ) || \
		defined( USE_LATERESUME_TO_CTRL_PDAACTIVE_HIGH ) )
	struct early_suspend pda_active_early_suspend;
#endif

#if defined( CONFIG_SVNET_WHITELIST ) && \
	defined( CONFIG_HAS_EARLYSUSPEND ) && \
	defined( USE_EARLYSUSPEND_TO_CTRL_WHITELIST )
	struct early_suspend white_list_early_suspend;
#endif
	
};
struct modemctl *g_mc;

#if 0
enum {
	SIM_LEVEL_NONE = -1,
	SIM_LEVEL_STABLE,
	SIM_LEVEL_CHANGED
};
#endif

static int check_phone_restart = 0;
#if defined ( CONFIG_CHN_CMCC_SPI_SPRD )
extern int ipc_spi_thread_restart( void );
#else
extern void ipc_hsi_restart_hsi( void );
#endif
extern void ipc_hsi_dump_cmd( void );


#ifdef CONFIG_HAS_WAKELOCK
static inline void _wake_lock_init(struct modemctl *mc)
{
	wake_lock_init(&mc->mc_wlock, WAKE_LOCK_SUSPEND, "modemctl");
	mc->waketime = MODEM_CTL_DEFAULT_WAKLOCK_HZ;
}

static inline void _wake_lock_destroy(struct modemctl *mc)
{
	wake_lock_destroy(&mc->mc_wlock);
}

static inline void _wake_lock_timeout(struct modemctl *mc)
{
	wake_lock_timeout(&mc->mc_wlock, mc->waketime);
}

static inline void _wake_lock_settime(struct modemctl *mc, long time)
{
	if (mc)
		mc->waketime = time;
}

static inline long _wake_lock_gettime(struct modemctl *mc)
{
	return mc?mc->waketime:MODEM_CTL_DEFAULT_WAKLOCK_HZ;
}
#else
#  define _wake_lock_init(mc) do { } while(0)
#  define _wake_lock_destroy(mc) do { } while(0)
#  define _wake_lock_timeout(mc) do { } while(0)
#  define _wake_lock_settime(mc, time) do { } while(0)
#  define _wake_lock_gettime(mc) (0)
#endif

//static int sim_check_status(struct modemctl *);
//static int sim_get_reference_status(struct modemctl *);
//static void sim_irq_debounce_timer_func(unsigned);

#if defined (CONFIG_CHN_CMCC_SPI_SPRD )
static void sprd_on(struct modemctl *);
static void sprd_off(struct modemctl *);
static void sprd_reset(struct modemctl *);
static void sprd_boot_on(struct modemctl *);
static void sprd_boot_off(struct modemctl *);

static struct modemctl_info mdmctl_info[] = {
	{
		.name = "sprd",
		.ops = {
			.modem_on = sprd_on,
			.modem_off = sprd_off,
			.modem_reset = sprd_reset,
			.modem_boot_on = sprd_boot_on,
			.modem_boot_off = sprd_boot_off,
		},
	},
};
#else
static void infinion_on(struct modemctl *);
static void infinion_off(struct modemctl *);
static void infinion_reset(struct modemctl *);
static void infinion_boot_on(struct modemctl *);
static void infinion_boot_off(struct modemctl *);

static struct modemctl_info mdmctl_info[] = {
	{
		.name = "xmm",
		.ops = {
			.modem_on = infinion_on,
			.modem_off = infinion_off,
			.modem_reset = infinion_reset,
			.modem_boot_on = infinion_boot_on,
			.modem_boot_off = infinion_boot_off,
		},
	},
};
#endif

static ssize_t show_control(struct device *d,
		struct device_attribute *attr, char *buf);
static ssize_t store_control(struct device *d,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t show_status(struct device *d,
		struct device_attribute *attr, char *buf);
static ssize_t show_debug(struct device *d,
		struct device_attribute *attr, char *buf);

static DEVICE_ATTR(control, S_IRUGO | S_IWUSR | S_IWGRP,
			show_control, store_control);
static DEVICE_ATTR(status, S_IRUGO, show_status, NULL);
static DEVICE_ATTR(debug, S_IRUGO, show_debug, NULL);

static struct attribute *modemctl_attributes[] = {
	&dev_attr_control.attr,
	&dev_attr_status.attr,
	&dev_attr_debug.attr,
	NULL
};

static const struct attribute_group modemctl_group = {
	.attrs = modemctl_attributes,
};

#if defined ( CONFIG_CHN_CMCC_SPI_SPRD )

static void sprd_on(struct modemctl *mc)
{
	dev_err(mc->dev, "%s\n", __func__);
	if (!mc->gpio_cp_reset || !mc->gpio_phone_on || !mc->gpio_reset_req_n){
		printk( "[%s]: gpio_cp_reset = %d\n",__func__, gpio_get_value(mc->gpio_cp_reset) );
		printk( "[%s]: gpio_phone_on = %d\n",__func__, gpio_get_value(mc->gpio_phone_on) );
		printk( "[%s]: gpio_reset_req_n = %d\n",__func__, gpio_get_value(mc->gpio_reset_req_n) );
		printk( "return\n" );
		return;
	}
	
	gpio_set_value(mc->gpio_pda_active, 0);
	gpio_set_value(mc->gpio_phone_on, 0);
	//gpio_set_value(mc->gpio_cp_reset, 0);
	msleep(50);
	gpio_set_value(mc->gpio_phone_on, 1);
	//gpio_set_value(mc->gpio_cp_reset, 1);
	//udelay(160);
	gpio_set_value(mc->gpio_pda_active, 1);
}

static void sprd_off(struct modemctl *mc)
{
	dev_err(mc->dev, "%s\n", __func__);
	if (!mc->gpio_cp_reset || !mc->gpio_phone_on)
		return;

	gpio_set_value(mc->gpio_phone_on, 0);
	gpio_set_value(mc->gpio_cp_reset, 0);
}

// sprd_reset means that ap requests cp forced reset
static void sprd_reset(struct modemctl *mc)
{
	dev_err(mc->dev, "%s\n", __func__);
    
	if(mc->gpio_reset_req_n)
		gpio_set_value(mc->gpio_reset_req_n, 1);
}

static void sprd_boot_on(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);

#if 0
	if(mc->gpio_flm_sel)
		gpio_set_value(mc->gpio_flm_sel, 0);

	if(mc->gpio_con_cp_sel)
		gpio_set_value(mc->gpio_con_cp_sel, 0);
#endif
}

static void sprd_boot_off(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);

#if 0
	if(mc->gpio_flm_sel)
		gpio_set_value(mc->gpio_flm_sel, 1);

	if(mc->gpio_con_cp_sel)
		gpio_set_value(mc->gpio_con_cp_sel, 1);
#endif
}

#else
static void infinion_on(struct modemctl *mc)
{
	int switch_sel = 0;
	
	dev_err(mc->dev, "%s\n", __func__);
	if (!mc->gpio_cp_reset || !mc->gpio_phone_on || !mc->gpio_reset_req_n){
		printk( "[%s]: gpio_cp_reset = %d\n",__func__, gpio_get_value(mc->gpio_cp_reset) );
		printk( "[%s]: gpio_phone_on = %d\n",__func__, gpio_get_value(mc->gpio_phone_on) );
		printk( "[%s]: gpio_reset_req_n = %d\n",__func__, gpio_get_value(mc->gpio_reset_req_n) );
		printk( "return\n" );
		return;
	}

	gpio_set_value(mc->gpio_phone_on, 0);
	gpio_set_value(mc->gpio_cp_reset, 0);
	udelay(160);
	
	gpio_set_value(mc->gpio_pda_active, 0);
	msleep(100);

	gpio_set_value(mc->gpio_cp_reset, 1);
	udelay(160);
	gpio_set_value(mc->gpio_reset_req_n, 1);
	udelay(160);

	gpio_set_value(mc->gpio_phone_on, 1);
	udelay(60);
	gpio_set_value(mc->gpio_phone_on, 0);
	
	omap_writel(0x0B000B00, 0x4A100144);
	dev_dbg(mc->dev, "[HW_REQ] AP FLM TX/RX SET MUX HIGH\n");

	if (sec_get_param_value) {
		sec_get_param_value(__SWITCH_SEL, &switch_sel);
		dev_dbg(mc->dev, "[HW_REQ] GOT Swtich_Sel : 0x%x\n", switch_sel);

		if (switch_sel & UART_SEL_MASK) { /* Uart Sel state is PDA */
			gpio_set_value(OMAP_GPIO_UART_SEL, 1);
			omap_mux_init_signal("uart4_rx", OMAP_MUX_MODE0 | OMAP_PIN_INPUT);
			dev_dbg(mc->dev, "[HW_REQ] uart sel state is already PDA. uart_sel : %d\n", 
						gpio_get_value(OMAP_GPIO_UART_SEL));
		}
		else { /* Uart Sel state is MODEM */
			printk("[HW_REQ] UART Set To CP ------------\n");
			dev_dbg(mc->dev, "[HW_REQ] SWITCH_SEL : MODEM UART\n");
			
			omap_mux_init_signal("uart4_rx", OMAP_MUX_MODE7 | OMAP_PIN_INPUT_PULLDOWN);
			gpio_set_value(OMAP_GPIO_UART_SEL, 0);
			dev_dbg(mc->dev, "[HW_REQ] uart sel state change to MODEM. uart_sel : %d\n", 
						gpio_get_value(OMAP_GPIO_UART_SEL));
		}
	}
	else {	
		printk( "[HW_REQ] failed to set switch because there are no param\n" );
	}

	msleep(20);
	gpio_set_value(mc->gpio_pda_active, 1);
}

static void infinion_off(struct modemctl *mc)
{
	dev_err(mc->dev, "%s\n", __func__);
	if (!mc->gpio_cp_reset || !mc->gpio_phone_on)
		return;

	gpio_set_value(mc->gpio_phone_on, 0);
	gpio_set_value(mc->gpio_cp_reset, 0);
}

static void infinion_reset(struct modemctl *mc)
{
	dev_err(mc->dev, "%s\n", __func__);
	if (!mc->gpio_cp_reset || !mc->gpio_reset_req_n)
		return;

	gpio_set_value(mc->gpio_cp_reset, 0);
	gpio_set_value(mc->gpio_reset_req_n, 0);

	msleep(100);

	gpio_set_value(mc->gpio_cp_reset, 1);
	udelay(160);
	gpio_set_value(mc->gpio_reset_req_n, 1);
}

static void infinion_boot_on(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);

#if 0
	if(mc->gpio_flm_sel)
		gpio_set_value(mc->gpio_flm_sel, 0);

	if(mc->gpio_con_cp_sel)
		gpio_set_value(mc->gpio_con_cp_sel, 0);
#endif
}

static void infinion_boot_off(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);

	/* Request by HW */
	omap_writel( 0x010B010B, 0x4A100144 );
	printk( "[HW_REQ] AP FLM TX/RX SET MUX LOW\n" );

#if 0
	if(mc->gpio_flm_sel)
		gpio_set_value(mc->gpio_flm_sel, 1);

	if(mc->gpio_con_cp_sel)
		gpio_set_value(mc->gpio_con_cp_sel, 1);
#endif
}
#endif

static int modem_on(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if(!mc->ops || !mc->ops->modem_on) {
		return -ENXIO;
	}

	if( check_phone_restart ) {
#if defined ( CONFIG_CHN_CMCC_SPI_SPRD )
		dev_dbg( mc->dev, "%s: Phone Restart SPI Init.\n", __func__ );
		//ipc_spi_thread_restart();
#else
		dev_dbg( mc->dev, "%s: Phone Restart HSI Init.\n", __func__ );
		ipc_hsi_restart_hsi();
#endif
	}
	else {
		check_phone_restart = 1;
	}

	mc->ops->modem_on(mc);

	return 0;
}

static int modem_off(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if(!mc->ops || !mc->ops->modem_off) {
		return -ENXIO;
	}

	mc->ops->modem_off(mc);

	return 0;
}

static int modem_reset(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if(!mc->ops || !mc->ops->modem_reset) {
		return -ENXIO;
	}

	mc->ops->modem_reset(mc);

	return 0;
}

static int modem_boot_on(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if(!mc->ops || !mc->ops->modem_boot_on) {
		return -ENXIO;
	}

	mc->ops->modem_boot_on(mc);

	return 0;
}

static int modem_boot_off(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if(!mc->ops || !mc->ops->modem_boot_off) {
		return -ENXIO;
	}

	mc->ops->modem_boot_off(mc);

	return 0;
}

static int pda_on(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if(!mc->gpio_pda_active) {

		return -ENXIO;
	}

	gpio_set_value(mc->gpio_pda_active, 1);
	printk( "PDA ACTIVE HIGH : %d(%d)\n", gpio_get_value( mc->gpio_pda_active ), mc->gpio_pda_active );

	return 0;
}

static int pda_off(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if(!mc->gpio_pda_active) {

		return -ENXIO;
	}

	gpio_set_value(mc->gpio_pda_active, 0);
	printk( "PDA ACTIVE LOW  : %d(%d)\n", gpio_get_value( mc->gpio_pda_active ), mc->gpio_pda_active );

	return 0;
}

static int modem_get_active(struct modemctl *mc)
{
	dev_err(mc->dev, "%s\n", __func__);
	if(!mc->gpio_phone_active || !mc->gpio_cp_reset)
		return -ENXIO;

	dev_err(mc->dev, "cp %d phone %d\n",
			gpio_get_value(mc->gpio_cp_reset),
			gpio_get_value(mc->gpio_phone_active));
#if defined ( CONFIG_CHN_CMCC_SPI_SPRD )
	if(gpio_get_value(mc->gpio_phone_active) == 1){
        //            gpio_set_value(OMAP_GPIO_AP_CP_INT1, jig_flag); 
            return 1;
               }
	else
		return 0;
#endif
	if(gpio_get_value(mc->gpio_cp_reset))
		return !!gpio_get_value(mc->gpio_phone_active);

	return 0;
}

static ssize_t show_control(struct device *d,
		struct device_attribute *attr, char *buf)
{
	char *p = buf;
	struct modemctl *mc = dev_get_drvdata(d);
	struct modemctl_ops *ops = mc->ops;

	if(ops) {
		if(ops->modem_on)
			p += sprintf(p, "on ");
		if(ops->modem_off)
			p += sprintf(p, "off ");
		if(ops->modem_reset)
			p += sprintf(p, "reset ");
		if(ops->modem_boot_on)
			p += sprintf(p, "boot_on ");

		if(ops->modem_boot_off)
			p += sprintf(p, "boot_off ");
	} else {
		p += sprintf(p, "(No ops)");
	}

	p += sprintf(p, "\n");
	return p - buf;
}

static ssize_t store_control(struct device *d,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct modemctl *mc = dev_get_drvdata(d);

	dev_dbg( mc->dev, "store_control : %s\n", buf );

	if(!strncmp(buf, "on", 2)) {
		modem_on(mc);
		return count;
	}

	if(!strncmp(buf, "off", 3)) {
		modem_off(mc);
		return count;
	}

	if(!strncmp(buf, "reset", 5)) {
		modem_reset(mc);
		return count;
	}

	if(!strncmp(buf, "boot_on", 7)) {
		modem_boot_on(mc);
		return count;
	}

	if(!strncmp(buf, "boot_off", 8)) {
		modem_boot_off(mc);
		return count;
	}
	// for compatibility
	if(!strncmp(buf, "boot", 4)) {
		modem_boot_on(mc);
		return count;
	}

	return count;
}

static ssize_t show_status(struct device *d,
		struct device_attribute *attr, char *buf)
{
	char *p = buf;
	struct modemctl *mc = dev_get_drvdata(d);

	p += sprintf(p, "%d\n", modem_get_active(mc));

	return p - buf;
}

static ssize_t show_debug(struct device *d,
		struct device_attribute *attr, char *buf)
{
	char *p = buf;
	int i;
	struct modemctl *mc = dev_get_drvdata(d);

	if(mc->irq_phone_active)
		p += sprintf(p, "Irq Phone Active: %d\n", mc->irq_phone_active);
#if defined ( CONFIG_CHN_CMCC_SPI_SPRD )
	if(mc->irq_cp_dump_int)
		p += sprintf(p, "Irq Phone Active: %d\n", mc->irq_cp_dump_int);
#endif
//	if(mc->irq_sim_ndetect)
//		p += sprintf(p, "Irq Sim nDetect: %d\n", mc->irq_sim_ndetect);

	p += sprintf(p, "GPIO ---- \n");

	if(mc->gpio_phone_on)
		p += sprintf(p, "\t%3d %d : phone on\n", mc->gpio_phone_on,
				gpio_get_value(mc->gpio_phone_on));
	if(mc->gpio_reset_req_n)
		p += sprintf(p, "\t%3d %d : reset_req_n\n", mc->gpio_reset_req_n,
				gpio_get_value(mc->gpio_reset_req_n));
	if(mc->gpio_cp_reset)
		p += sprintf(p, "\t%3d %d : CP reset\n", mc->gpio_cp_reset,
				gpio_get_value(mc->gpio_cp_reset));
	if(mc->gpio_pda_active)
		p += sprintf(p, "\t%3d %d : pda active\n", mc->gpio_pda_active,
				gpio_get_value(mc->gpio_pda_active));
	if(mc->gpio_phone_active)
		p += sprintf(p, "\t%3d %d : phone active\n", mc->gpio_phone_active,
				gpio_get_value(mc->gpio_phone_active));
	if(mc->gpio_cp_dump_int)
		p += sprintf(p, "\t%3d %d : CP ramdump active\n", mc->gpio_cp_dump_int,
				gpio_get_value(mc->gpio_cp_dump_int));
//	if(mc->gpio_flm_sel)
//		p += sprintf(p, "\t%3d %d : FLM sel\n", mc->gpio_flm_sel,
//				gpio_get_value(mc->gpio_flm_sel));
//	if(mc->gpio_con_cp_sel)
//		p += sprintf(p, "\t%3d %d : CON_CP sel\n", mc->gpio_con_cp_sel,
//				gpio_get_value(mc->gpio_con_cp_sel));

//	if(mc->gpio_usim_boot)
//		p += sprintf(p, "\t%3d %d : USIM boot\n", mc->gpio_usim_boot,
//				gpio_get_value(mc->gpio_usim_boot));
//	if(mc->gpio_sim_ndetect)
//		p += sprintf(p, "\t%3d %d : Sim n Detect\n", mc->gpio_sim_ndetect,
//				gpio_get_value(mc->gpio_sim_ndetect));

	p += sprintf(p, "Support types --- \n");
	for(i=0;i<ARRAY_SIZE(mdmctl_info);i++) {
		if(mc->ops == &mdmctl_info[i].ops) {
			p += sprintf(p, "\t * ");
		} else {
			p += sprintf(p, "\t   ");
		}
		p += sprintf(p, "%s\n", mdmctl_info[i].name);
	}

	return p - buf;
}

int cp_force_reset_flag = 1;
int cp_crash = 0;
extern int cp_flashless_boot_done;
extern void modem_state_changed(int state);
static void mc_work(struct work_struct *work)
{
#if defined ( CONFIG_CHN_CMCC_SPI_SPRD )
	struct modemctl *mc = container_of(work, struct modemctl, work);
	int r, rc;

	r = modem_get_active(mc);
	rc = gpio_get_value( mc->gpio_cp_dump_int );
	
	if ((r < 0) ||(rc <0)) {
		dev_err(mc->dev, "Not initialized\n");
		return;
	}

	dev_err(mc->dev, "PHONE ACTIVE: %d\n", r);
	dev_err(mc->dev, "CP_DUMP_INT : %d\n", rc);
	
	if (r) {
//		if (mc->sim_change_reset == SIM_LEVEL_CHANGED) {
//			kobject_uevent(&mc->dev->kobj, KOBJ_CHANGE);	
//		} else {
//			if (mc->sim_reference_level == SIM_LEVEL_NONE) {
//				sim_get_reference_status(mc);
//			}
//			kobject_uevent(&mc->dev->kobj, KOBJ_ONLINE);
//		}

		kobject_uevent(&mc->dev->kobj, KOBJ_ONLINE);
	}
	else
		;//kobject_uevent(&mc->dev->kobj, KOBJ_OFFLINE);

	if ( rc && (!r) ){
            gpio_set_value(mc->gpio_reset_req_n, 0);    //set to low
            kobject_uevent(&mc->dev->kobj, KOBJ_OFFLINE);
	}
#else
	struct modemctl *mc = container_of(work, struct modemctl, work);
	int r = 1;
	int cpdump_int = 1;
	char *envs[ 2 ] = { NULL, NULL };

	int p_active = gpio_get_value( mc->gpio_phone_active );

	if( p_active ) {
		set_irq_type( mc->irq_phone_active, IRQ_TYPE_LEVEL_LOW );
		dev_err( mc->dev, "PA : %d, set_irq_type : IRQ_TYPE_LEVEL_LOW\n", p_active );
	}
	else {
		set_irq_type( mc->irq_phone_active, IRQ_TYPE_LEVEL_HIGH );
		dev_err( mc->dev, "PA : %d, set_irq_type : IRQ_TYPE_LEVEL_HIGH\n", p_active );
	}

	if (!cp_flashless_boot_done) {
		printk("flashless boot not done.\n");
		enable_irq(mc->irq_phone_active);
		return;
	}

	r = modem_get_active(mc);
	if (r < 0) {
		dev_err(mc->dev, "Not initialized\n");

		enable_irq( mc->irq_phone_active );
		return;
	}

	cpdump_int = gpio_get_value( mc->gpio_cp_dump_int );
	dev_err( mc->dev, "PHONE ACTIVE : %d, CP_DUMP_INT : %d\n", r, cpdump_int );
	
	/* temp for cp reset = 0, cp ramdump = 1 */
//	cpdump_int = 0;
	
	envs[ 0 ] = cpdump_int ? "MAILBOX=cp_exit" : "MAILBOX=cp_reset";

	if (r) {
//		if (mc->sim_change_reset == SIM_LEVEL_CHANGED) {
//			kobject_uevent(&mc->dev->kobj, KOBJ_CHANGE);	
//		} else {
//			if (mc->sim_reference_level == SIM_LEVEL_NONE) {
//				sim_get_reference_status(mc);
//			}
//			kobject_uevent(&mc->dev->kobj, KOBJ_ONLINE);
//		}

		cp_force_reset_flag = 0;

		if (!cp_crash) {
			modem_state_changed(1);
			kobject_uevent(&mc->dev->kobj, KOBJ_ONLINE);
		}
		else {
			printk("Don send online uevent to ril.\n");
		}
	}
	else {
		if( !cp_force_reset_flag && cp_flashless_boot_done ) {
			cp_force_reset_flag = 1;

			modem_state_changed(0);

			ipc_hsi_dump_cmd();

			if (cpdump_int)
				cp_crash = 1;
			
			dev_err( mc->dev, "send uevent to RIL... ( CP_DUMP_INT: %d )\n", cpdump_int );
			kobject_uevent_env( &mc->dev->kobj, KOBJ_OFFLINE, envs );
		}
	}

	enable_irq( mc->irq_phone_active );
#endif
}

void modemctl_force_silent_reset( void )
{
	int cpdump_int = 0;
	char *envs[ 2 ] = { NULL, NULL };

	if( cp_force_reset_flag ) return;

	printk( "modemctl_force_silent_reset\n" );

	cp_force_reset_flag = 1;

	envs[ 0 ] = cpdump_int ? "MAILBOX=cp_exit" : "MAILBOX=cp_reset";

	kobject_uevent_env( &g_mc->dev->kobj, KOBJ_OFFLINE, envs );

	printk( "modemctl_force_silent_reset : send event\n" );
}
EXPORT_SYMBOL( modemctl_force_silent_reset );

static irqreturn_t modemctl_irq_handler(int irq, void *dev_id)
{
	struct modemctl *mc = (struct modemctl *)dev_id;

	if (!work_pending(&mc->work))
		schedule_work(&mc->work);
#if defined ( CONFIG_CHN_CMCC_SPI_SPRD )
#else
	disable_irq_nosync( mc->irq_phone_active );
#endif
	return IRQ_HANDLED;
}

#if 0
static int sim_get_reference_status(struct modemctl* mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if(!mc->gpio_sim_ndetect)
		return -ENXIO;

	mc->sim_reference_level = gpio_get_value(mc->gpio_sim_ndetect);	

	return 0;
}

static int sim_check_status(struct modemctl* mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if(!mc->gpio_sim_ndetect || mc->sim_reference_level == SIM_LEVEL_NONE) {
		return -ENXIO;
	}

	if (mc->sim_reference_level != gpio_get_value(mc->gpio_sim_ndetect)) {
		mc->sim_change_reset = SIM_LEVEL_CHANGED;
		}		
	else
		{
		mc->sim_change_reset = SIM_LEVEL_STABLE;
		}

	return 0;
}

static void sim_irq_debounce_timer_func(unsigned aulong)
{
	struct modemctl *mc = (struct modemctl *)aulong;
	int r;

	r = sim_check_status(mc);
	if (r < 0) {
		dev_err(mc->dev, "Not initialized\n");
		return;
	}

	if (mc->sim_change_reset == SIM_LEVEL_CHANGED) {
		if (!work_pending(&mc->work))
			schedule_work(&mc->work);

		_wake_lock_timeout(mc);
	}
}

static irqreturn_t simctl_irq_handler(int irq, void *dev_id)
{
	struct modemctl *mc = (struct modemctl *)dev_id;
	int r;

	if ( mc->sim_reference_level == SIM_LEVEL_NONE) {
		return IRQ_HANDLED;
	}

	r = sim_check_status(mc);
	if (r < 0) {
		dev_err(mc->dev, "Not initialized\n");
		return IRQ_HANDLED;
	}

	if (mc->sim_change_reset == SIM_LEVEL_CHANGED) {
		mod_timer(&mc->sim_irq_debounce_timer, jiffies + SIM_DEBOUNCE_TIME_HZ);
		_wake_lock_timeout(mc);
	}

	return IRQ_HANDLED;
}
#endif

static struct modemctl_ops* _find_ops(const char *name)
{
	int i;
	struct modemctl_ops *ops = NULL;

	for(i=0;i<ARRAY_SIZE(mdmctl_info);i++) {
		if(mdmctl_info[i].name && !strcmp(name, mdmctl_info[i].name))
			ops = &mdmctl_info[i].ops;
	}

	return ops;
}

static void _free_all(struct modemctl *mc)
{
	if(mc) {
		if(mc->ops)
			mc->ops = NULL;

		if(mc->group)
			sysfs_remove_group(&mc->dev->kobj, mc->group);

		if(mc->irq_phone_active)
			free_irq(mc->irq_phone_active, mc);
#if defined ( CONFIG_CHN_CMCC_SPI_SPRD )
		if(mc->irq_cp_dump_int)
			free_irq(mc->irq_cp_dump_int, mc);
#endif		
//		if(mc->irq_sim_ndetect)
//			free_irq(mc->irq_sim_ndetect, mc);

		if(mc->dev)
			device_destroy(mc->class, mc->dev->devt);

		if(mc->class)
			class_destroy(mc->class);

		_wake_lock_destroy(mc);

		kfree(mc);
	}
}


#if defined( CONFIG_HAS_EARLYSUSPEND ) && \
	defined( USE_EARLYSUSPEND_TO_CTRL_PDAACTIVE_LOW )
static int modemctl_pda_active_early_suspend(struct early_suspend *h)
{
	struct modemctl *mc = container_of(h, struct modemctl, pda_active_early_suspend);

	pda_off(mc);

	return 0;
}
#endif

#if defined( CONFIG_HAS_EARLYSUSPEND ) && \
	defined(USE_LATERESUME_TO_CTRL_PDAACTIVE_HIGH)
static int modemctl_pda_active_late_resume(struct early_suspend *h)
{
	struct modemctl *mc = container_of(h, struct modemctl, pda_active_early_suspend);

	pda_on(mc);

	return 0;
}
#endif

#if defined( CONFIG_SVNET_WHITELIST ) && \
	defined( CONFIG_HAS_EARLYSUSPEND ) && \
	defined( USE_EARLYSUSPEND_TO_CTRL_WHITELIST )
static int modemctl_white_list_early_suspend(struct early_suspend *h)
{
	int ret;
	
	// call process white list
	ret = process_whilte_list();
	if (unlikely(ret !=0)) {
		printk("fail to send whitelist\n");
	} 
	
	return ret;
}
#endif

static int __devinit modemctl_probe(struct platform_device *pdev)
{
	struct modemctl *mc = NULL;
	struct modemctl_platform_data *pdata;
	struct resource *res;
	int r = 0;
	int irq_phone_active; //, irq_sim_ndetect;
#if defined ( CONFIG_CHN_CMCC_SPI_SPRD )
	int irq_cp_dump_int;
#endif
	printk("[%s]\n",__func__);

	pdata = pdev->dev.platform_data;
	if(!pdata || !pdata->cfg_gpio) {
		dev_err(&pdev->dev, "No platform data\n");
		r = -EINVAL;
		goto err;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if(!res)  {
		dev_err(&pdev->dev, "failed to get irq number\n");
		r = -EINVAL;
		goto err;
	}
	irq_phone_active = res->start;
#if defined ( CONFIG_CHN_CMCC_SPI_SPRD )
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if(!res)  {
		dev_err(&pdev->dev, "failed to get irq number\n");
		r = -EINVAL;
		goto err;
	}
	irq_cp_dump_int = res->start;
#endif	
#if 0
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if(!res)  {
		dev_err(&pdev->dev, "failed to get irq number\n");
		r = -EINVAL;
		goto err;
	}
	irq_sim_ndetect = res->start;
#endif

	mc = kzalloc(sizeof(struct modemctl), GFP_KERNEL);
	if(!mc) {
		dev_err(&pdev->dev, "failed to allocate device\n");
		r = -ENOMEM;
		goto err;
	}
	g_mc = mc;

	mc->gpio_reset_req_n = pdata->gpio_reset_req_n;
	mc->gpio_phone_on = pdata->gpio_phone_on;
	mc->gpio_cp_reset = pdata->gpio_cp_reset;
	mc->gpio_pda_active = pdata->gpio_pda_active;
	mc->gpio_phone_active = pdata->gpio_phone_active;
	mc->gpio_cp_dump_int = pdata->gpio_cp_dump_int;

	dev_dbg( &pdev->dev, "reset_req_n : %d\n", mc->gpio_reset_req_n );
	dev_dbg( &pdev->dev, "phone on : %d\n", mc->gpio_phone_on );
	dev_dbg( &pdev->dev, "cp_rst : %d\n", mc->gpio_cp_reset );
	dev_dbg( &pdev->dev, "pda_active : %d\n", mc->gpio_pda_active );
	dev_dbg( &pdev->dev, "phone_active : %d\n", mc->gpio_phone_active );
	dev_dbg( &pdev->dev, "cp_dump_int : %d\n", mc->gpio_cp_dump_int );
	
//	mc->gpio_flm_sel = pdata->gpio_flm_sel;
//	mc->gpio_con_cp_sel = pdata->gpio_con_cp_sel;
//	dev_dbg( &pdev->dev, "gpio_flm_sel : %d, gpio_con_cp_sel : %d\n", mc->gpio_flm_sel, mc->gpio_con_cp_sel );

	mc->ops = _find_ops(pdata->name);
	if(!mc->ops) {
		dev_err(&pdev->dev, "can't find operations: %s\n", pdata->name);
		goto err;
	}

	mc->class = class_create(THIS_MODULE, "modemctl");
	if(IS_ERR(mc->class)) {
		dev_err(&pdev->dev, "failed to create sysfs class\n");
		r = PTR_ERR(mc->class);
		mc->class = NULL;
		goto err;
	}

	pdata->cfg_gpio();

	mc->dev = device_create(mc->class, &pdev->dev, MKDEV(0, 0), NULL, "%s", pdata->name);
	if(IS_ERR(mc->dev)) {
		dev_err(&pdev->dev, "failed to create device\n");
		r = PTR_ERR(mc->dev);
		goto err;
	}
	dev_set_drvdata(mc->dev, mc);

	r = sysfs_create_group(&mc->dev->kobj, &modemctl_group);
	if(r) {
		dev_err(&pdev->dev, "failed to create sysfs files\n");
		goto err;
	}
	mc->group = &modemctl_group;

	INIT_WORK(&mc->work, mc_work);


	mc->irq_phone_active = irq_phone_active;
#if defined ( CONFIG_CHN_CMCC_SPI_SPRD )
	r = request_irq(irq_phone_active, modemctl_irq_handler,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"phone_active", mc);
#else
	r = request_irq(irq_phone_active, modemctl_irq_handler, IRQF_TRIGGER_HIGH,
			"phone_active", mc);
#endif
	if(r) {
		dev_err(&pdev->dev, "failed to allocate an interrupt(%d)\n",
				irq_phone_active);
		goto err;
	}
#if defined ( CONFIG_CHN_CMCC_SPI_SPRD )
	r = request_irq(irq_cp_dump_int, modemctl_irq_handler,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"cp_dump_int", mc);
	if(r) {
		dev_err(&pdev->dev, "failed to allocate an interrupt(%d)\n",
				irq_cp_dump_int);
		goto err;
	}
	mc->irq_cp_dump_int = irq_cp_dump_int;
#endif
#if 0
	setup_timer(&mc->sim_irq_debounce_timer, (void*)sim_irq_debounce_timer_func,(unsigned long)mc);

	r = request_irq(irq_sim_ndetect, simctl_irq_handler,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"sim_ndetect", mc);
	if(r) {
		dev_err(&pdev->dev, "failed to allocate an interrupt(%d)\n",
				irq_sim_ndetect);
		goto err;
	}
	mc->irq_sim_ndetect= irq_sim_ndetect;
#endif

#if defined( CONFIG_HAS_EARLYSUSPEND ) && \
	( defined( USE_EARLYSUSPEND_TO_CTRL_PDAACTIVE_LOW ) ||\
		defined( USE_LATERESUME_TO_CTRL_PDAACTIVE_HIGH ) )
	mc->pda_active_early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 200;

#if defined( USE_EARLYSUSPEND_TO_CTRL_PDAACTIVE_LOW )
	mc->pda_active_early_suspend.suspend = (void*) modemctl_pda_active_early_suspend;
#endif

#if defined( USE_LATERESUME_TO_CTRL_PDAACTIVE_HIGH )
	mc->pda_active_early_suspend.resume = (void*) modemctl_pda_active_late_resume;
#endif

	register_early_suspend(&mc->pda_active_early_suspend);
#endif

#if defined( CONFIG_SVNET_WHITELIST ) && \
	defined( CONFIG_HAS_EARLYSUSPEND ) && \
	defined( USE_EARLYSUSPEND_TO_CTRL_WHITELIST )
	mc->white_list_early_suspend.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING + 50;
	mc->white_list_early_suspend.suspend = (void*) modemctl_white_list_early_suspend;
	register_early_suspend(&mc->white_list_early_suspend);
#endif

	_wake_lock_init(mc);

	platform_set_drvdata(pdev, mc);

	dev_dbg( &pdev->dev, "modemctl_probe Done.\n" );

	return 0;

err:
	_free_all(mc);
	return r;
}

static int __devexit modemctl_remove(struct platform_device *pdev)
{
	struct modemctl *mc = platform_get_drvdata(pdev);

	flush_work(&mc->work);
	platform_set_drvdata(pdev, NULL);
	_free_all(mc);
	return 0;
}

#if defined( CONFIG_PM )
static int modemctl_suspend(struct platform_device *pdev, pm_message_t state)
{
	int retval;
	struct modemctl *mc = NULL;

	retval = 0;
	mc = platform_get_drvdata( pdev );

#if !defined( USE_EARLYSUSPEND_TO_CTRL_PDAACTIVE_LOW )
	pda_off(mc);
#endif

#if defined( CONFIG_SVNET_WHITELIST ) && \
	!defined( USE_EARLYSUSPEND_TO_CTRL_WHITELIST )
	// call process white list
	retval = process_whilte_list();
	if( unlikely( retval !=0 ) ) {
		printk( "fail to send whitelist : %d\n", retval );
	} 
#endif

	return 0;
}

static int modemctl_resume(struct platform_device *pdev)
{
	struct modemctl *mc = NULL;

	mc = platform_get_drvdata( pdev );

#if !defined( USE_LATERESUME_TO_CTRL_PDAACTIVE_HIGH )
	pda_on(mc);
#endif

	return 0;
}
#else
#  define modemctl_suspend NULL
#  define modemctl_resume NULL
#endif



static struct platform_driver modemctl_driver = {
	.probe = modemctl_probe,
	.remove = __devexit_p(modemctl_remove),
	.suspend = modemctl_suspend,
	.resume = modemctl_resume,
	.driver = {
		.name = DRVNAME,
	},
};

static int __init modemctl_init(void)
{
	printk("[%s]\n",__func__);
	return platform_driver_register(&modemctl_driver);
}

static void __exit modemctl_exit(void)
{
	platform_driver_unregister(&modemctl_driver);
}

module_init(modemctl_init);
module_exit(modemctl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Suchang Woo <suchang.woo@samsung.com>");
MODULE_DESCRIPTION("Modem control");

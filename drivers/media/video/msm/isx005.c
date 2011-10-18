/* Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 *
 * Sony 3M ISX005 camera sensor driver
 * Auther: Han Jun-Yeong[junyeong.han@lge.com], 2010-09-03
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <media/msm_camera.h>
#include <mach/gpio.h>
#include <linux/kthread.h>

//#define ISX005_TUN

#if defined(ISX005_TUN)
#include "isx005_tun.c"
#else
#include "isx005.h"
#include "isx005_reg.h"
#endif

/*
* AF Total steps parameters
*/
#define ISX005_TOTAL_STEPS_NEAR_TO_FAR	30

DEFINE_MUTEX(isx005_tuning_mutex);
static int tuning_thread_run;

#define CFG_WQ_SIZE		64

struct config_work_queue {
	int cfgtype;
	int mode;
};

struct config_work_queue *cfg_wq;
static int cfg_wq_num;

/* It is distinguish normal from macro focus */
static int prev_af_mode;
/* It is distinguish scene mode */
static int prev_scene_mode;
/* 2010-12-06 fixed run changing framerate mode repeatly */
//static int prev_fps_mode;
// 2010-12-31 reduce preview init time
static bool skipFstUICmd;
static bool chgUICmd;
// 2011-03-21 Samsung camera sensor porting
static int sensor_type;

static struct i2c_client *isx005_client;

struct isx005_ctrl {
	const struct msm_camera_sensor_info *sensordata;
};

static struct isx005_ctrl *isx005_ctrl;

DEFINE_MUTEX(isx005_mutex);

struct platform_device *isx005_pdev;

int pclk_rate;
static int always_on;

static int32_t isx005_i2c_txdata(unsigned short saddr,
	unsigned char *txdata, int length)
{
	struct i2c_msg msg[] = {
		{
			.addr = saddr,
			.flags = 0,
			.len = length,
			.buf = txdata,
		},
	};

	if (i2c_transfer(isx005_client->adapter, msg, 1) < 0) {
		CDBG("isx005_i2c_txdata failed\n");
		return -EIO;
	}

	return 0;
}

static int32_t isx005_i2c_write(unsigned short saddr,
	unsigned short waddr, unsigned short wdata, enum isx005_width width)
{
	int32_t rc = -EIO;
	unsigned char buf[4];

	memset(buf, 0, sizeof(buf));
	switch (width) {
	case BYTE_LEN:
		buf[0] = (waddr & 0xFF00) >> 8;
		buf[1] = (waddr & 0x00FF);
		buf[2] = wdata;
		rc = isx005_i2c_txdata(saddr, buf, 3);
		break;

	case WORD_LEN:
		buf[0] = (waddr & 0xFF00) >> 8;
		buf[1] = (waddr & 0x00FF);
		buf[2] = (wdata & 0xFF00) >> 8;
		buf[3] = (wdata & 0x00FF);

		rc = isx005_i2c_txdata(saddr, buf, 4);
		break;

	default:
		break;
	}

	if (rc < 0)
		printk(KERN_ERR "i2c_write failed, addr = 0x%x, val = 0x%x!\n",
			waddr, wdata);

	return rc;
}

static int isx005_i2c_rxdata(unsigned short saddr,
	unsigned char *rxdata, int length)
{
	struct i2c_msg msgs[] = {
		{
			.addr   = saddr,
			.flags = 0,
			.len   = 2,
			.buf   = rxdata,
		},
		{
			.addr   = saddr,
			.flags = I2C_M_RD,
			.len   = length,
			.buf   = rxdata,
		},
	};

	if (i2c_transfer(isx005_client->adapter, msgs, 2) < 0) {
		printk(KERN_ERR "isx005_i2c_rxdata failed!\n");
		return -EIO;
	}

	return 0;
}

static int32_t isx005_i2c_read(unsigned short   saddr,
	unsigned short raddr, unsigned short *rdata, enum isx005_width width)
{
	int32_t rc = 0;
	unsigned char buf[4];

	if (!rdata)
		return -EIO;

	memset(buf, 0, sizeof(buf));

	switch (width) {
	case BYTE_LEN:
		buf[0] = (raddr & 0xFF00) >> 8;
		buf[1] = (raddr & 0x00FF);
		rc = isx005_i2c_rxdata(saddr, buf, 2);
		if (rc < 0)
			return rc;
		*rdata = buf[0];
		break;

	case WORD_LEN:
		buf[0] = (raddr & 0xFF00) >> 8;
		buf[1] = (raddr & 0x00FF);
		rc = isx005_i2c_rxdata(saddr, buf, 2);
		if (rc < 0)
			return rc;
		*rdata = buf[0] << 8 | buf[1];
		break;

	default:
		break;
	}

	if (rc < 0)
		printk(KERN_ERR "isx005_i2c_read failed!\n");

	return rc;
}

static int32_t isx005_i2c_write_table(
	struct isx005_register_address_value_pair const *reg_conf_tbl,
	int num_of_items_in_table)
{
	int i;
	int32_t rc = -EIO;
	unsigned short temp;

	for (i = 0; i < num_of_items_in_table; ++i) {
	 if (reg_conf_tbl->register_address == 0xdddd) {  // sensor register refresh polling
	  do {
 	  isx005_i2c_write(isx005_client->addr, 0x098E, 0x8400, WORD_LEN);
 	  isx005_i2c_read(isx005_client->addr, 0x0990, &temp, WORD_LEN);

 	 } while( temp & 0x0006 );
  } else if ( reg_conf_tbl->register_address == 0xeeee ) {  // 2010-11-24 add delay logic
	   printk(KERN_ERR "delay time %d\n", reg_conf_tbl->register_value);
	   mdelay(reg_conf_tbl->register_value);
	 } else {
 		rc = isx005_i2c_write(isx005_client->addr,
 			reg_conf_tbl->register_address,
 			reg_conf_tbl->register_value,
 			reg_conf_tbl->register_length);
 		if (rc < 0)
 			break;
 	}

		reg_conf_tbl++;
	}

	return rc;
}

// 2011-03-21 Samsung camera sensor porting
void isx005_register2_setting() {
	isx005_regs.init_reg_settings = init_reg_settings_array2;
	isx005_regs.init_reg_settings_size = ARRAY_SIZE(init_reg_settings_array2);

	isx005_regs.tuning_reg_settings = tuning_reg_settings_array2;
	isx005_regs.tuning_reg_settings_size = ARRAY_SIZE(tuning_reg_settings_array2);

	isx005_regs.auto_framerate_reg_settings = auto_framerate_mode_reg_settings_array2;
	isx005_regs.auto_framerate_reg_settings_size = ARRAY_SIZE(auto_framerate_mode_reg_settings_array2);

	isx005_regs.fixed_framerate_reg_settings = fixed_framerate_mode_reg_settings_array2;
	isx005_regs.fixed_framerate_reg_settings_size = ARRAY_SIZE(fixed_framerate_mode_reg_settings_array2);

	isx005_regs.prev_reg_settings = preview_mode_reg_settings_array2;
	isx005_regs.prev_reg_settings_size = ARRAY_SIZE(preview_mode_reg_settings_array2);

	isx005_regs.snap_reg_settings = snapshot_mode_reg_settings_array2;
	isx005_regs.snap_reg_settings_size = ARRAY_SIZE(snapshot_mode_reg_settings_array2);

	isx005_regs.effect_off_reg_settings = effect_mode_off_reg_settings_array2;
	isx005_regs.effect_off_reg_settings_size = ARRAY_SIZE(effect_mode_off_reg_settings_array2);

	isx005_regs.effect_mono_reg_settings = effect_mode_mono_reg_settings_array2;
	isx005_regs.effect_mono_reg_settings_size = ARRAY_SIZE(effect_mode_mono_reg_settings_array2);

	isx005_regs.effect_negative_reg_settings = effect_mode_negative_reg_settings_array2;
	isx005_regs.effect_negative_reg_settings_size = ARRAY_SIZE(effect_mode_negative_reg_settings_array2);

	isx005_regs.effect_solarize_reg_settings = effect_mode_solarize_reg_settings_array2;
	isx005_regs.effect_solarize_reg_settings_size = ARRAY_SIZE(effect_mode_solarize_reg_settings_array2);

	isx005_regs.effect_sepia_reg_settings = effect_mode_sepia_reg_settings_array2;
	isx005_regs.effect_sepia_reg_settings_size = ARRAY_SIZE(effect_mode_sepia_reg_settings_array2);

	isx005_regs.effect_aqua_reg_settings = effect_mode_aqua_reg_settings_array2;
	isx005_regs.effect_aqua_reg_settings_size = ARRAY_SIZE(effect_mode_aqua_reg_settings_array2);

	isx005_regs.wb_auto_reg_settings = wb_mode_auto_reg_settings_array2;
	isx005_regs.wb_auto_reg_settings_size = ARRAY_SIZE(wb_mode_auto_reg_settings_array2);

	isx005_regs.wb_incandescent_reg_settings = wb_mode_incandescent_reg_settings_array2;
	isx005_regs.wb_incandescent_reg_settings_size = ARRAY_SIZE(wb_mode_incandescent_reg_settings_array2);

	isx005_regs.wb_fluorescent_reg_settings = wb_mode_fluorescent_reg_settings_array2;
	isx005_regs.wb_fluorescent_reg_settings_size = ARRAY_SIZE(wb_mode_fluorescent_reg_settings_array2);

	isx005_regs.wb_daylight_reg_settings = wb_mode_daylight_reg_settings_array2;
	isx005_regs.wb_daylight_reg_settings_size = ARRAY_SIZE(wb_mode_daylight_reg_settings_array2);

	isx005_regs.wb_cloudy_reg_settings = wb_mode_cloudy_reg_settings_array2;
	isx005_regs.wb_cloudy_reg_settings_size = ARRAY_SIZE(wb_mode_cloudy_reg_settings_array2);

	isx005_regs.iso_auto_reg_settings = iso_mode_auto_reg_settings_array2;
	isx005_regs.iso_auto_reg_settings_size = ARRAY_SIZE(iso_mode_auto_reg_settings_array2);

	isx005_regs.iso_100_reg_settings = iso_mode_100_reg_settings_array2;
	isx005_regs.iso_100_reg_settings_size = ARRAY_SIZE(iso_mode_100_reg_settings_array2);

	isx005_regs.iso_200_reg_settings = iso_mode_200_reg_settings_array2;
	isx005_regs.iso_200_reg_settings_size = ARRAY_SIZE(iso_mode_200_reg_settings_array2);

	isx005_regs.iso_400_reg_settings = iso_mode_400_reg_settings_array2;
	isx005_regs.iso_400_reg_settings_size = ARRAY_SIZE(iso_mode_400_reg_settings_array2);

	isx005_regs.brightness_reg_settings = brightness_reg_settings_array2;
	isx005_regs.brightness_reg_settings_size = ARRAY_SIZE(brightness_reg_settings_array2);
}

/* pll register setting */
static int isx005_reg_init(void)
{
	int rc = 0;
	int i;
	unsigned short temp;

	/* Configure sensor for Initial setting (PLL, Clock, etc) */
	for (i = 0; i < isx005_regs.init_reg_settings_size; ++i) {
	 // 2010-12-25 add pll delay=10ms
	 if ( isx005_regs.init_reg_settings[i].register_address == 0xeeee) {
	  printk(KERN_ERR "pll delay %d\n", isx005_regs.init_reg_settings[i].register_value);
	  mdelay(isx005_regs.init_reg_settings[i].register_value);
	 } else if ( isx005_regs.init_reg_settings[i].register_address == 0xdddd ) {
    do {
     isx005_i2c_read(isx005_client->addr, 0x0018, &temp, WORD_LEN);
     printk(KERN_ERR "pll polling 0x%x\n", temp);
    } while( temp & 0x4000 );
	 } else {
  		rc = isx005_i2c_write(isx005_client->addr,
  			isx005_regs.init_reg_settings[i].register_address,
  			isx005_regs.init_reg_settings[i].register_value,
  			isx005_regs.init_reg_settings[i].register_length);
  }

		if (rc < 0)
			return rc;
	}

	return rc;
}

static void enqueue_cfg_wq(int cfgtype, int mode)
{
	if (!cfg_wq) {
		cfg_wq_num = 0;
		return;
	}

	if (cfg_wq_num == CFG_WQ_SIZE)
		return;

	cfg_wq[cfg_wq_num].cfgtype = cfgtype;
	cfg_wq[cfg_wq_num].mode = mode;

	++cfg_wq_num;
}

/* init register setting (Aptina) */
int isx005_reg_tuning(void)
{
	int rc = 0;
	int i;
	unsigned short temp;

 printk(KERN_ERR "Aptina init_code\n");

	for (i = 0; i < isx005_regs.tuning_reg_settings_size; ++i) {
	 if ( isx005_regs.tuning_reg_settings[i].register_address == 0xdddd ) {
	  do {
 	  isx005_i2c_write(isx005_client->addr, 0x098E, 0x8400, WORD_LEN);
 	  isx005_i2c_read(isx005_client->addr, 0x0990, &temp, WORD_LEN);
 	 } while( temp & 0x0006 );
	 } else if ( isx005_regs.tuning_reg_settings[i].register_address == 0xddd1 ) {
	  do {
 	  isx005_i2c_write(isx005_client->addr, 0x098E, 0x8400, WORD_LEN);
 	  isx005_i2c_read(isx005_client->addr, 0x0990, &temp, WORD_LEN);
    printk(KERN_ERR "init extra polling %d\n", temp);
 	 } while( temp & 0x0005 );
	 } else if ( isx005_regs.tuning_reg_settings[i].register_address == 0xeeee ) { // 20101103 used by sensor tuning
	   printk(KERN_ERR "delay time %d\n", isx005_regs.tuning_reg_settings[i].register_value);
	   mdelay(isx005_regs.tuning_reg_settings[i].register_value);
	 } else if ( isx005_regs.tuning_reg_settings[i].register_address == 0xfff1 ) { // 20101111 check sensor patch ID
	  do {
 	  isx005_i2c_write(isx005_client->addr, 0x098E, 0x800C, WORD_LEN);
 	  isx005_i2c_read(isx005_client->addr, 0x0990, &temp, WORD_LEN);
 	  printk(KERN_ERR "patch id %d\n", isx005_regs.tuning_reg_settings[i].register_value);
 	 } while( temp != isx005_regs.tuning_reg_settings[i].register_value );
	 } else if ( isx005_regs.tuning_reg_settings[i].register_address == 0xfff2 ) { // 20101111 check sensor patch PASS
	  do {
 	  isx005_i2c_write(isx005_client->addr, 0x098E, 0x800D, WORD_LEN);
 	  isx005_i2c_read(isx005_client->addr, 0x0990, &temp, WORD_LEN);
 	  printk(KERN_ERR "patch pass %d\n", isx005_regs.tuning_reg_settings[i].register_value);
 	 } while( temp != isx005_regs.tuning_reg_settings[i].register_value );
	 } else {
   	rc = isx005_i2c_write(isx005_client->addr,
   		isx005_regs.tuning_reg_settings[i].register_address,
   		isx005_regs.tuning_reg_settings[i].register_value,
   		isx005_regs.tuning_reg_settings[i].register_length);
  }
 	if (rc < 0)
 		break;
	}

	return rc;
}

/* init register setting (Samsung)*/
int isx005_reg_tuning2(void)
{
	int rc = 0;
	int i;
	unsigned char buf[1500];
	int bufIndex = 2;

 printk(KERN_ERR "Samsung init_code\n");

 // for burst mode
 buf[0] = 0x0F;
 buf[1] = 0x12;

	for (i = 0; i < isx005_regs.tuning_reg_settings_size; ++i) {
  // burst mode
	 if ( isx005_regs.tuning_reg_settings[i].register_address == 0x0F12 ) {
	  buf[bufIndex] = (isx005_regs.tuning_reg_settings[i].register_value & 0xFF00) >> 8;
	  bufIndex++;
	  buf[bufIndex] = (isx005_regs.tuning_reg_settings[i].register_value & 0x00FF);
	  bufIndex++;
	 } else {
	   if ( bufIndex > 2 ) {
       rc = isx005_i2c_txdata(isx005_client->addr, buf, bufIndex+1);
	      bufIndex = 2;
	   }
   	rc = isx005_i2c_write(isx005_client->addr,
   		isx005_regs.tuning_reg_settings[i].register_address,
   		isx005_regs.tuning_reg_settings[i].register_value,
   		isx005_regs.tuning_reg_settings[i].register_length);
  }

 	if (rc < 0)
 		break;
	}

	return rc;
}

/* preview register setting */
static int isx005_reg_preview(void)
{
	int rc = 0;
	int i;
	unsigned short temp;

	/* Configure sensor for Preview mode */
	// 2010-12-31 for upgrading sensor stability
	for (i = 0; i < isx005_regs.prev_reg_settings_size; ++i) {
	 if ( isx005_regs.prev_reg_settings[i].register_address == 0xeeee ) {
   printk(KERN_ERR "preview delay time %d\n", isx005_regs.prev_reg_settings[i].register_value);
   mdelay(isx005_regs.prev_reg_settings[i].register_value);
	 } else if ( isx005_regs.prev_reg_settings[i].register_address == 0xdddd ) {
	  do {
 	  isx005_i2c_write(isx005_client->addr, 0x098E, 0x8400, WORD_LEN);
 	  isx005_i2c_read(isx005_client->addr, 0x0990, &temp, WORD_LEN);
 	 } while( temp & 0x0001 );
	 } else if ( isx005_regs.prev_reg_settings[i].register_address == 0xddd1 ) {
	  do {
 	  isx005_i2c_write(isx005_client->addr, 0x098E, 0x8400, WORD_LEN);
 	  isx005_i2c_read(isx005_client->addr, 0x0990, &temp, WORD_LEN);
    printk(KERN_ERR "preview extra polling %d\n", temp);
 	 } while( temp & 0x0005 );
	 } else {
  	rc = isx005_i2c_write(isx005_client->addr,
 	  isx005_regs.prev_reg_settings[i].register_address,
 	  isx005_regs.prev_reg_settings[i].register_value,
 	  isx005_regs.prev_reg_settings[i].register_length);
	 }

		if (rc < 0)
			return rc;
	}

	return rc;
}

/* capture register setting */
static int isx005_reg_snapshot(void)
{
	int rc = 0;
	int i;
	unsigned short temp;

	/* Configure sensor for Snapshot mode */
	for (i = 0; i < isx005_regs.snap_reg_settings_size; ++i) {
	 if ( isx005_regs.snap_reg_settings[i].register_address == 0xeeee ) {
   printk(KERN_ERR "capture delay time %d\n", isx005_regs.snap_reg_settings[i].register_value);
   mdelay(isx005_regs.snap_reg_settings[i].register_value);
	 } else if ( isx005_regs.snap_reg_settings[i].register_address == 0xdddd ) {
    do {
     isx005_i2c_write(isx005_client->addr, 0x098E, 0x8400, WORD_LEN);
     isx005_i2c_read(isx005_client->addr, 0x0990, &temp, WORD_LEN);
    } while( temp & 0x0002 );
	 } else {
 		rc = isx005_i2c_write(isx005_client->addr,
 			isx005_regs.snap_reg_settings[i].register_address,
 			isx005_regs.snap_reg_settings[i].register_value,
 			isx005_regs.snap_reg_settings[i].register_length);
 	}

		if (rc < 0)
			return rc;
	}

	return rc;
}

// 2010-11-24 change the framerate mode between capture and video
static int isx005_change_sensor_mode(int mode)
{
 int rc;

 switch (mode) {
	case FRAME_RATE_AUTO:
	 printk(KERN_ERR "FRAME_RATE_AUTO\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.auto_framerate_reg_settings,
		isx005_regs.auto_framerate_reg_settings_size);
  break;

 case FRAME_RATE_FIXED:
  printk(KERN_ERR "FRAME_RATE_FIXED\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.fixed_framerate_reg_settings,
		isx005_regs.fixed_framerate_reg_settings_size);
		break;

	default:
		return -EINVAL;
	}

 /* init_code */
 if ( sensor_type == APTINA_SENSOR ) {
  // 2010-12-31 impove stability when sensor mode changed between capture mode and video mode
  if ( chgUICmd ) {
   printk(KERN_ERR "change mode delay 350\n");
   mdelay(350);
  }
 }

	return rc;
}

static int isx005_set_sensor_mode(int mode)
{
	int rc = 0;
	int retry = 0;

	switch (mode) {
	case SENSOR_PREVIEW_MODE:
		for (retry = 0; retry < 3; ++retry) {
   printk(KERN_ERR "preview mode\n");
   //mdelay(60);  // 1 frame skip ==> total 2 frames skip
			rc = isx005_reg_preview();
			if (rc < 0)
				printk(KERN_ERR "[ERROR]%s:Sensor Preview Mode Fail\n",
					__func__);
			else {
			 //printk(KERN_ERR "preview delay 300\n");
			 //mdelay(300);  // 2 frames skip
				break;
			}
		}
		break;

	case SENSOR_SNAPSHOT_MODE:
	case SENSOR_RAW_SNAPSHOT_MODE:	/* Do not support */
		for (retry = 0; retry < 3; ++retry) {
   printk(KERN_ERR "snapshot mode\n");
   //mdelay(100);  // msm memory ready time (very important!!!)
			rc = isx005_reg_snapshot();
			if (rc < 0)
				printk(KERN_ERR "[ERROR]%s:Sensor Snapshot Mode Fail\n",
					__func__);
			else
				break;
		}
		break;

	default:
		return -EINVAL;
	}

	CDBG("Sensor Mode : %d, rc = %d\n", mode, rc);

	return rc;
}

/* effect register setting */
static int isx005_set_effect(int effect)
{
	int rc = 0;

	switch (effect) {
	case CAMERA_EFFECT_OFF:
	 printk(KERN_ERR "Effect Off\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.effect_off_reg_settings,
		isx005_regs.effect_off_reg_settings_size);
  break;

 case CAMERA_EFFECT_MONO:
  printk(KERN_ERR "Effect Mono\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.effect_mono_reg_settings,
		isx005_regs.effect_mono_reg_settings_size);
		break;

	case CAMERA_EFFECT_NEGATIVE:
	 printk(KERN_ERR "Effect Negative\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.effect_negative_reg_settings,
		isx005_regs.effect_negative_reg_settings_size);
		break;

	case CAMERA_EFFECT_SOLARIZE:
	 printk(KERN_ERR "Effect Solarize\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.effect_solarize_reg_settings,
		isx005_regs.effect_negative_reg_settings_size);
		break;

 case CAMERA_EFFECT_SEPIA:
  printk(KERN_ERR "Effect Sepia\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.effect_sepia_reg_settings,
		isx005_regs.effect_sepia_reg_settings_size);
		break;

 case CAMERA_EFFECT_AQUA:
  printk(KERN_ERR "Effect Aqua\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.effect_aqua_reg_settings,
		isx005_regs.effect_aqua_reg_settings_size);
		break;

	default:
		return -EINVAL;
 }

	return rc;
}

/* White Balance register setting */
static int isx005_set_wb(int mode)
{
	int rc = 0;

	switch (mode) {
	case CAMERA_WB_AUTO:
	 rc = isx005_i2c_write_table(
		isx005_regs.wb_auto_reg_settings,
		isx005_regs.wb_auto_reg_settings_size);
		break;

	case CAMERA_WB_CUSTOM:	/* Do not support */
		break;

	case CAMERA_WB_INCANDESCENT: {

 	 printk(KERN_ERR "INCANDESCENT!!!\n");

 	 rc = isx005_i2c_write_table(
 		isx005_regs.wb_incandescent_reg_settings,
 		isx005_regs.wb_incandescent_reg_settings_size);
 		break;
		}

	case CAMERA_WB_FLUORESCENT: {

	 printk(KERN_ERR "FLUORESCENT!!!\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.wb_fluorescent_reg_settings,
		isx005_regs.wb_fluorescent_reg_settings_size);
		break;
 }

	case CAMERA_WB_DAYLIGHT: {
	 printk(KERN_ERR "SUNNY!!!\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.wb_daylight_reg_settings,
		isx005_regs.wb_daylight_reg_settings_size);
		break;
	}

	case CAMERA_WB_CLOUDY_DAYLIGHT: {

	 printk(KERN_ERR "CLOUDY!!!\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.wb_cloudy_reg_settings,
		isx005_regs.wb_cloudy_reg_settings_size);
		break;
	}

	default:
		return -EINVAL;
	}

	return rc;
}

/* ISO register setting */
static int isx005_set_iso(int iso)
{
	int32_t rc;

	switch (iso) {
	case CAMERA_ISO_AUTO:
	 printk(KERN_ERR "ISO AUTO\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.iso_auto_reg_settings,
		isx005_regs.iso_auto_reg_settings_size);
		break;


	case CAMERA_ISO_100:
	 printk(KERN_ERR "ISO 100\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.iso_100_reg_settings,
		isx005_regs.iso_100_reg_settings_size);
		break;

	case CAMERA_ISO_200:
	 printk(KERN_ERR "ISO 200\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.iso_200_reg_settings,
		isx005_regs.iso_200_reg_settings_size);
		break;

	case CAMERA_ISO_400:
	 printk(KERN_ERR "ISO 400\n");

	 rc = isx005_i2c_write_table(
		isx005_regs.iso_400_reg_settings,
		isx005_regs.iso_400_reg_settings_size);
		break;

	default:
		rc = -EINVAL;
	}

	return rc;
}

/* brightness register setting */
static int32_t isx005_set_brightness(int8_t brightness)
{
 int rc = 0;

 if ( sensor_type == APTINA_SENSOR ) {
  rc = isx005_i2c_write(isx005_client->addr,
                        isx005_regs.brightness_reg_settings[0].register_address,
                        isx005_regs.brightness_reg_settings[0].register_value,
                        isx005_regs.brightness_reg_settings[0].register_length);
  rc = isx005_i2c_write(isx005_client->addr,
                        isx005_regs.brightness_reg_settings[brightness+1].register_address,
                        isx005_regs.brightness_reg_settings[brightness+1].register_value,
                        isx005_regs.brightness_reg_settings[brightness+1].register_length);  
 } else {  // Samsung sensor
  rc = isx005_i2c_write(isx005_client->addr,
                        isx005_regs.brightness_reg_settings[0].register_address,
                        isx005_regs.brightness_reg_settings[0].register_value,
                        isx005_regs.brightness_reg_settings[0].register_length);
  rc = isx005_i2c_write(isx005_client->addr,
                        isx005_regs.brightness_reg_settings[1].register_address,
                        isx005_regs.brightness_reg_settings[1].register_value,
                        isx005_regs.brightness_reg_settings[1].register_length);
  rc = isx005_i2c_write(isx005_client->addr,
                        isx005_regs.brightness_reg_settings[brightness+2].register_address,
                        isx005_regs.brightness_reg_settings[brightness+2].register_value,
                        isx005_regs.brightness_reg_settings[brightness+2].register_length);
 }
 
 return rc;
}

static int isx005_init_sensor(const struct msm_camera_sensor_info *data)
{

/*
 #if defined(ISX005_TUN)
  isx005_read_register_from_file();
 #endif
*/ 

	int rc;
	int num = 0;
	int i;
	struct task_struct *p;
	unsigned short temp;

	rc = data->pdata->camera_power_on();
	if (rc < 0) {
		printk(KERN_ERR "[ERROR]%s:failed to power on!\n", __func__);
		return rc;
	}

 // 2011-03-21 Samsung camera sensor porting
 isx005_i2c_read(isx005_client->addr, 0x3B80, &temp, WORD_LEN);
 printk("[dual] 0x3B80 = %x", temp);

 if ( temp != 0x0009 ) {
  sensor_type = SAMSUNG_SENSOR;
  data->pdata->camera_standy_high();
  isx005_register2_setting();
 } else {
  sensor_type = APTINA_SENSOR;
 }

 // for tuning mode
 #if defined(ISX005_TUN)
  isx005_read_register_from_file();
 #endif

 //pll
 printk(KERN_ERR "pll\n");
	rc = isx005_reg_init();

	if (rc < 0) {
		for (num = 0; num < 5; num++) {
			msleep(2);
			printk(KERN_ERR
				"[ERROR]%s:Set initial register error! retry~! \n", __func__);
			rc = isx005_reg_init();
			if (rc < 0)	{
				num++;
				printk(KERN_ERR
					"[ERROR]%s:Set initial register error!- loop no:%d \n",
					__func__, num);
			} else {
				printk(KERN_DEBUG "[%s]:Set initial register Success!\n",
					__func__);
				break;
			}
		}
	}

 /* init_code */
 if ( sensor_type == APTINA_SENSOR ) {
  rc = isx005_reg_tuning();  // Aptina
 } else {
  rc = isx005_reg_tuning2();  // Samsung
 }

 // 2010-12-31 reduce preview init time
 skipFstUICmd = true;
 chgUICmd = false;

	return rc;
}

static int isx005_sensor_init_probe(const struct msm_camera_sensor_info *data)
{
	int rc = 0;

	CDBG("init entry \n");

	if (data == 0) {
		printk(KERN_ERR "[ERROR]%s: data is null!\n", __func__);
		return -1;
	}

#if defined(CONFIG_MACH_MSM7X27_THUNDERG) || \
	defined(CONFIG_MACH_MSM7X27_THUNDERC)
	/* LGE_CHANGE_S. Change code to apply new LUT for display quality.
	 * 2010-08-13. minjong.gong@lge.com */
	mdp_load_thunder_lut(2);	/* Camera LUT */
#endif

	mutex_lock(&isx005_mutex);
	rc = isx005_init_sensor(data);
	mutex_unlock(&isx005_mutex);

	if (rc < 0) {
		printk(KERN_ERR "[ERROR]%s:failed to initialize sensor!\n", __func__);
		goto init_probe_fail;
	}

	prev_af_mode = -1;
	prev_scene_mode = -1;

	return rc;

init_probe_fail:
	return rc;
}

int isx005_sensor_init(const struct msm_camera_sensor_info *data)
{
	int rc = 0;

	isx005_ctrl = kzalloc(sizeof(struct isx005_ctrl), GFP_KERNEL);
	if (!isx005_ctrl) {
		printk(KERN_ERR "[ERROR]%s:isx005_init failed!\n", __func__);
		rc = -ENOMEM;
		goto init_done;
	}

	if (data)
		isx005_ctrl->sensordata = data;

	rc = isx005_sensor_init_probe(data);
	if (rc < 0) {
		printk(KERN_ERR "[ERROR]%s:isx005_sensor_init failed!\n", __func__);
		goto init_fail;
	}

init_done:
	return rc;

init_fail:
	kfree(isx005_ctrl);
	return rc;
}

int isx005_sensor_release(void)
{
	int rc = 0;

	if (always_on) {
		printk(KERN_INFO "always power-on camera.\n");
		return rc;
	}

	mutex_lock(&isx005_mutex);

	rc = isx005_ctrl->sensordata->pdata->camera_power_off();

	kfree(isx005_ctrl);

	mutex_unlock(&isx005_mutex);

#if defined(CONFIG_MACH_MSM7X27_THUNDERG) || \
	defined(CONFIG_MACH_MSM7X27_THUNDERC)
	/* LGE_CHANGE_S. Change code to apply new LUT for display quality.
	 * 2010-08-13. minjong.gong@lge.com */
	mdp_load_thunder_lut(1);	/* Normal LUT */
#endif

 /* 2010-12-06 fixed run changing framerate mode repeatly */
	//prev_fps_mode = -1;

	return rc;
}

int isx005_sensor_config(void __user *argp)
{
	struct sensor_cfg_data cfg_data;
	int rc;

	rc = copy_from_user(&cfg_data, (void *)argp,
		sizeof(struct sensor_cfg_data));

	if (rc < 0)
		return -EFAULT;

	CDBG("isx005_ioctl, cfgtype = %d, mode = %d\n",
		cfg_data.cfgtype, cfg_data.mode);

	mutex_lock(&isx005_tuning_mutex);
	if (tuning_thread_run) {
		if (cfg_data.cfgtype == CFG_MOVE_FOCUS)
			cfg_data.mode = cfg_data.cfg.focus.steps;

		enqueue_cfg_wq(cfg_data.cfgtype, cfg_data.mode);
		mutex_unlock(&isx005_tuning_mutex);
		return rc;
	}
	mutex_unlock(&isx005_tuning_mutex);

	mutex_lock(&isx005_mutex);

	// 2010-12-10 test
	printk(KERN_INFO "Config sensor info type %d", cfg_data.cfgtype);

	switch (cfg_data.cfgtype) {

 // 2010-11-24 change the framerate mode between capture and video
 case CFG_SET_FPS : {
  rc = isx005_change_sensor_mode(cfg_data.mode);
  break;
 }

	case CFG_SET_MODE: {
	 printk(KERN_INFO "Config Sensor Mode\n");
		rc = isx005_set_sensor_mode(cfg_data.mode);
		break;
	}

	case CFG_SET_EFFECT: {
	 printk(KERN_INFO "Config Effect\n");
		rc = isx005_set_effect(cfg_data.mode);

  // 2010-12-31 reduce preview init time
  chgUICmd = true;
		break;
 }

	case CFG_MOVE_FOCUS:
	 rc = 0;
		break;

	case CFG_SET_DEFAULT_FOCUS:
	 rc = 0;
		break;

	case CFG_GET_AF_MAX_STEPS:
		cfg_data.max_steps = ISX005_TOTAL_STEPS_NEAR_TO_FAR;
		if (copy_to_user((void *)argp,
				&cfg_data,
				sizeof(struct sensor_cfg_data)))
			rc = -EFAULT;
		break;

	case CFG_START_AF_FOCUS:
	 rc = 0;
		break;

 case CFG_CHECK_AF_DONE:
	 rc = 0;
		break;

	case CFG_CHECK_AF_CANCEL:
	 rc = 0;
		break;

	case CFG_SET_WB: {
	 // 2010-12-31 reduce preview init time
  if ( skipFstUICmd ) {
	   rc = 0;
	 } else {
  	 printk(KERN_INFO "Config white balance\n");
  		rc = isx005_set_wb(cfg_data.mode);
    // 2010-12-31 reduce preview init time
    chgUICmd = true;
  }
		break;
 }

	case CFG_SET_ANTIBANDING: /* not support */
	 rc = 0;
		break;

	case CFG_SET_ISO: {
	 printk(KERN_INFO "Config ISO\n");
		rc = isx005_set_iso(cfg_data.mode);

		break;
 }
	case CFG_SET_SCENE: /* not support */
	 rc = 0;
		break;

	case CFG_SET_BRIGHTNESS: {
  if ( skipFstUICmd ) {
	   rc = 0;
	   skipFstUICmd = false;
	 } else {
  		printk(KERN_INFO "Config Brightness\n");
  		rc = isx005_set_brightness(cfg_data.mode);
    // 2010-12-31 reduce preview init time
    chgUICmd = true;
  }

		break;
	}

	default:
		rc = -EINVAL;
		break;
	}

	mutex_unlock(&isx005_mutex);

	return rc;
}

static const struct i2c_device_id isx005_i2c_id[] = {
	{ "isx005", 0},
	{ },
};

static int isx005_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int rc = 0;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		rc = -ENOTSUPP;
		goto probe_failure;
	}

	isx005_client = client;

	CDBG("isx005_probe succeeded!\n");

	return rc;

probe_failure:
	printk(KERN_ERR "[ERROR]%s:isx005_probe failed!\n", __func__);
	return rc;
}

static struct i2c_driver isx005_i2c_driver = {
	.id_table = isx005_i2c_id,
	.probe  = isx005_i2c_probe,
	.remove = __exit_p(isx005_i2c_remove),
	.driver = {
		.name = "isx005",
	},
};

static ssize_t pclk_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	printk(KERN_INFO "mclk_rate = %d\n", pclk_rate);
	return 0;
}

static ssize_t pclk_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t size)
{
	int value;

	sscanf(buf, "%d", &value);
	pclk_rate = value;

	printk(KERN_INFO "pclk_rate = %d\n", pclk_rate);
	return size;
}

static DEVICE_ATTR(pclk, S_IRWXUGO, pclk_show, pclk_store);

static ssize_t mclk_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	printk(KERN_INFO "mclk_rate = %d\n", mclk_rate);
	return 0;
}

static ssize_t mclk_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t size)
{
	int value;

	sscanf(buf, "%d", &value);
	mclk_rate = value;

	printk(KERN_INFO "mclk_rate = %d\n", mclk_rate);
	return size;
}

static DEVICE_ATTR(mclk, S_IRWXUGO, mclk_show, mclk_store);

static ssize_t always_on_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	printk(KERN_INFO "always_on = %d\n", always_on);
	return 0;
}

static ssize_t always_on_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int value;

	sscanf(buf, "%d", &value);
	always_on = value;

	printk(KERN_INFO "always_on = %d\n", always_on);
	return size;
}

static DEVICE_ATTR(always_on, S_IRWXUGO, always_on_show, always_on_store);

static int isx005_sensor_probe(const struct msm_camera_sensor_info *info,
				struct msm_sensor_ctrl *s)
{
	int rc = i2c_add_driver(&isx005_i2c_driver);
	if (rc < 0 || isx005_client == NULL) {
		rc = -ENOTSUPP;
		goto probe_done;
	}

	s->s_init = isx005_sensor_init;
	s->s_release = isx005_sensor_release;
	s->s_config  = isx005_sensor_config;

	cfg_wq = 0;
	cfg_wq_num = 0;
	always_on = 0;

	rc = device_create_file(&isx005_pdev->dev, &dev_attr_pclk);
	if (rc < 0) {
		printk(KERN_INFO "device_create_file error!\n");
		return rc;
	}

	rc = device_create_file(&isx005_pdev->dev, &dev_attr_mclk);
	if (rc < 0) {
		printk(KERN_INFO "device_create_file error!\n");
		return rc;
	}

	rc = device_create_file(&isx005_pdev->dev, &dev_attr_always_on);
	if (rc < 0) {
		printk(KERN_INFO "device_create_file error!\n");
		return rc;
	}

probe_done:
	CDBG("%s %s:%d\n", __FILE__, __func__, __LINE__);
	return rc;
}

static int __isx005_probe(struct platform_device *pdev)
{
	isx005_pdev = pdev;
	return msm_camera_drv_start(pdev, isx005_sensor_probe);
}

static struct platform_driver msm_camera_driver = {
	.probe = __isx005_probe,
	.driver = {
		.name = "msm_camera_isx005",
		.owner = THIS_MODULE,
	},
};

static int __init isx005_init(void)
{
	return platform_driver_register(&msm_camera_driver);
}

late_initcall(isx005_init);


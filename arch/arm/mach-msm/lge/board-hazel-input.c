/* arch/arm/mach-msm/board-hazel-input.c
 * Copyright (C) 2009 LGE, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/types.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/gpio_event.h>
#include <linux/keyreset.h>
#include <mach/gpio.h>
#include <mach/vreg.h>
#include <mach/board.h>
#include <mach/board_lge.h>
#include <mach/rpc_server_handset.h>
#include <mach/pmic.h>
#include "proc_comm.h"

#include "board-hazel.h"

/* pp2106 qwerty keyboard device */
static unsigned short pp2106_keycode[PP2106_KEYPAD_ROW][PP2106_KEYPAD_COL] = {
	{KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y },
	{KEY_U, KEY_I, KEY_O, KEY_P, KEY_SEARCH, KEY_A },
	{KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J },
	{KEY_K, KEY_L, KEY_APOSTROPHE, KEY_BACKSPACE, KEY_LEFTSHIFT, KEY_Z },
	{KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M },
	{KEY_SEMICOLON, KEY_UP, KEY_ENTER, KEY_LEFTALT, KEY_BACK, KEY_MENU },
	{KEY_HOME, KEY_SPACE, KEY_COMMA, KEY_DOT, KEY_LEFT, KEY_DOWN },
	{KEY_RIGHT, KEY_SPACE, KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN },
};

static struct pp2106_platform_data pp2106_pdata = {
	.keypad_row = PP2106_KEYPAD_ROW,
	.keypad_col = PP2106_KEYPAD_COL,
	.keycode = (unsigned char *)pp2106_keycode,
	.reset_pin = GPIO_PP2106_RESET,
	.irq_pin = GPIO_PP2106_IRQ,
	.sda_pin = GPIO_PP2106_SDA,
	.scl_pin = GPIO_PP2106_SCL,
};

static struct platform_device qwerty_device = {
	.name = "kbd_pp2106",
	.id = -1,
	.dev = {
		.platform_data = &pp2106_pdata,
	},
};

/* hall ic device */
#if defined (CONFIG_LGE_SUPPORT_AT_CMD)  
struct input_dev* at_cmd_hall_ic;
EXPORT_SYMBOL(at_cmd_hall_ic);
#endif /* add val for C710 AT_CMD */

static struct gpio_event_direct_entry hazel_slide_switch_map[] = {
	{ GPIO_HALLIC_IRQ,       	SW_LID   	    },
};

static int hazel_gpio_slide_input_func(struct input_dev *input_dev,
			struct gpio_event_info *info, void **data, int func)
{
#if defined (CONFIG_LGE_SUPPORT_AT_CMD)  
	at_cmd_hall_ic = input_dev;
#endif /* add val for C710 AT_CMD */
	
	if (func == GPIO_EVENT_FUNC_INIT)
	{
		gpio_tlmm_config(GPIO_CFG(GPIO_HALLIC_IRQ, 0, GPIO_INPUT, GPIO_PULL_UP,
					GPIO_2MA), GPIO_ENABLE);
	}

	return gpio_event_input_func(input_dev, info, data, func);
}

static struct gpio_event_input_info hazel_slide_switch_info = {
	.info.func = hazel_gpio_slide_input_func,
	.debounce_time.tv64 = 0,
	.flags = 0,
	.type = EV_SW,
	.keymap = hazel_slide_switch_map,
	.keymap_size = ARRAY_SIZE(hazel_slide_switch_map)
};

static struct gpio_event_info *hazel_gpio_slide_info[] = {
	&hazel_slide_switch_info.info,
};

static struct gpio_event_platform_data hazel_gpio_slide_data = {
	.name = "gpio-slide-detect",
	.info = hazel_gpio_slide_info,
	.info_count = ARRAY_SIZE(hazel_gpio_slide_info)
};

static struct platform_device hazel_gpio_slide_device = {
	.name = GPIO_EVENT_DEV_NAME,
	.id = 0,
	.dev        = {
		.platform_data  = &hazel_gpio_slide_data,
	},
};

/* LGE_S [ynj.kim@lge.com] 2010-05-15 : atcmd virtual device */
static unsigned short atcmd_virtual_keycode[ATCMD_VIRTUAL_KEYPAD_ROW][ATCMD_VIRTUAL_KEYPAD_COL] = {
	{KEY_1, 		KEY_8, 				KEY_Q,  	 KEY_I,          KEY_D,      	KEY_HOME,	KEY_B,          KEY_UP},
	{KEY_2, 		KEY_9, 		  		KEY_W,		 KEY_O,       	 KEY_F,		 	KEY_RIGHTSHIFT, 	KEY_N,			KEY_DOWN},
	{KEY_3, 		KEY_0, 		  		KEY_E,		 KEY_P,          KEY_G,      	KEY_Z,        	KEY_M, 			KEY_UNKNOWN},
	{KEY_4, 		KEY_BACK,  			KEY_R,		 KEY_SEARCH,     KEY_H,			KEY_X,    		KEY_LEFTSHIFT,	KEY_UNKNOWN},
	{KEY_5, 		KEY_BACKSPACE, 		KEY_T,		 KEY_LEFTALT,    KEY_J,      	KEY_C,     		KEY_REPLY,    KEY_CAMERA},
	{KEY_6, 		KEY_ENTER,  		KEY_Y,  	 KEY_A,		     KEY_K,			KEY_V,  	    KEY_RIGHT,     	KEY_CAMERAFOCUS},
	{KEY_7, 		KEY_MENU,	KEY_U,  	 KEY_S,    		 KEY_L, 	    KEY_SPACE,      KEY_LEFT,     	KEY_SEND},
	{KEY_UNKNOWN, 	KEY_UNKNOWN,  		KEY_UNKNOWN, KEY_UNKNOWN, 	 KEY_UNKNOWN,	KEY_UNKNOWN,    KEY_FOLDER_MENU,      	KEY_FOLDER_HOME},

};

static struct atcmd_virtual_platform_data atcmd_virtual_pdata = {
	.keypad_row = ATCMD_VIRTUAL_KEYPAD_ROW,
	.keypad_col = ATCMD_VIRTUAL_KEYPAD_COL,
	.keycode = (unsigned char *)atcmd_virtual_keycode,
};

static struct platform_device atcmd_virtual_device = {
	.name = "atcmd_virtual_kbd",
	.id = -1,
	.dev = {
		.platform_data = &atcmd_virtual_pdata,
	},
};
/* LGE_E [ynj.kim@lge.com] 2010-05-15 : atcmd virtual device */

/* head set device */
static struct msm_handset_platform_data hs_platform_data = {
	.hs_name = "7k_handset",
	.pwr_key_delay_ms = 500, /* 0 will disable end key */
};

static struct platform_device hs_device = {
	.name   = "msm-handset",
	.id     = -1,
	.dev    = {
		.platform_data = &hs_platform_data,
	},
};

static unsigned int keypad_row_gpios[] = {
	38, 37
};

static unsigned int keypad_col_gpios[] = {32, 33};

#define KEYMAP_INDEX(col, row) ((col)*ARRAY_SIZE(keypad_row_gpios) + (row))

static const unsigned short keypad_keymap_hazel_revc[] = {
	[KEYMAP_INDEX(0, 0)] = KEY_SEND,
	[KEYMAP_INDEX(0, 1)] = KEY_VOLUMEUP,
	[KEYMAP_INDEX(1, 0)] = KEY_END,
	[KEYMAP_INDEX(1, 1)] = KEY_VOLUMEDOWN,
};

static const unsigned short keypad_keymap_hazel[] = {
	[KEYMAP_INDEX(0, 0)] = KEY_SEND,
	[KEYMAP_INDEX(0, 1)] = KEY_VOLUMEUP,
	[KEYMAP_INDEX(1, 0)] = KEY_SEND,
	[KEYMAP_INDEX(1, 1)] = KEY_VOLUMEDOWN,
};

int hazel_matrix_info_wrapper(struct input_dev *input_dev,struct gpio_event_info *info, void **data, int func)
{
        int ret ;
		if (func == GPIO_EVENT_FUNC_RESUME) {
			gpio_tlmm_config(GPIO_CFG(keypad_row_gpios[0], 0,
						GPIO_INPUT, GPIO_PULL_UP,GPIO_2MA), GPIO_ENABLE);
			gpio_tlmm_config(GPIO_CFG(keypad_row_gpios[1], 0,
						GPIO_INPUT, GPIO_PULL_UP,GPIO_2MA), GPIO_ENABLE);
		}

		ret = gpio_event_matrix_func(input_dev,info, data,func);
        return ret ;
}

static int hazel_gpio_matrix_power(
                const struct gpio_event_platform_data *pdata, bool on)
{
	/* this is dummy function to make gpio_event driver register suspend function
	 * 2010-01-29, cleaneye.kim@lge.com
	 * copy from ALOHA code
	 * 2010-04-22 younchan.kim@lge.com
	 */

	return 0;
}

static struct gpio_event_matrix_info hazel_keypad_matrix_info = {
	.info.func	= hazel_matrix_info_wrapper,
	.keymap		= NULL,
	.output_gpios	= keypad_col_gpios,
	.input_gpios	= keypad_row_gpios,
	.noutputs	= ARRAY_SIZE(keypad_col_gpios),
	.ninputs	= ARRAY_SIZE(keypad_row_gpios),
	.settle_time.tv.nsec = 40 * NSEC_PER_USEC,
	.poll_time.tv.nsec = 20 * NSEC_PER_MSEC,
	.flags		= GPIOKPF_LEVEL_TRIGGERED_IRQ | GPIOKPF_PRINT_UNMAPPED_KEYS | GPIOKPF_DRIVE_INACTIVE
};

static void __init hazel_select_keymap(void)
{
	if (lge_bd_rev == LGE_REV_C)
		hazel_keypad_matrix_info.keymap = keypad_keymap_hazel_revc;
	else
		hazel_keypad_matrix_info.keymap = keypad_keymap_hazel;

	return;
}

static struct gpio_event_info *hazel_keypad_info[] = {
	&hazel_keypad_matrix_info.info
};

static struct gpio_event_platform_data hazel_keypad_data = {
	.name		= "hazel_keypad",
	.info		= hazel_keypad_info,
	.info_count	= ARRAY_SIZE(hazel_keypad_info),
	.power          = hazel_gpio_matrix_power,
};

struct platform_device keypad_device_hazel= {
	.name	= GPIO_EVENT_DEV_NAME,
	.id	= -1,
	.dev	= {
		.platform_data	= &hazel_keypad_data,
	},
};

/* keyreset platform device */
static int hazel_reset_keys_up[] = {
	KEY_HOME,
	0
};

static struct keyreset_platform_data hazel_reset_keys_pdata = {
	.keys_up = hazel_reset_keys_up,
	.keys_down = {
		//KEY_BACK,
		KEY_VOLUMEDOWN,
		KEY_SEARCH,
		0
	},
};

struct platform_device hazel_reset_keys_device = {
	.name = KEYRESET_NAME,
	.dev.platform_data = &hazel_reset_keys_pdata,
};

/* input platform device */
static struct platform_device *hazel_input_devices[] __initdata = {
	&hs_device,
	&keypad_device_hazel,
	//&hazel_reset_keys_device,
	&atcmd_virtual_device,
	&qwerty_device,
	&hazel_gpio_slide_device,
};

/* MCS6000 Touch */
static struct gpio_i2c_pin ts_i2c_pin[] = {
	[0] = {
		.sda_pin	= TS_GPIO_I2C_SDA,
		.scl_pin	= TS_GPIO_I2C_SCL,
		.reset_pin	= 0,
		.irq_pin	= TS_GPIO_IRQ,
	},
};

static struct i2c_gpio_platform_data ts_i2c_pdata = {
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.udelay				= 2,
};

static struct platform_device ts_i2c_device = {
	.name	= "i2c-gpio",
	.dev.platform_data = &ts_i2c_pdata,
};

static int ts_set_vreg(unsigned char onoff)
{
	struct vreg *vreg_touch;
	int rc;
	unsigned on_off, id;

	printk("[Touch] %s() onoff:%d\n",__FUNCTION__, onoff);

	vreg_touch = vreg_get(0, "synt");

	if(IS_ERR(vreg_touch)) {
		printk("[Touch] vreg_get fail : touch\n");
		return -1;
	}

	if (onoff) {		
		on_off = 0;
		id = PM_VREG_PDOWN_SYNT_ID;
		msm_proc_comm(PCOM_VREG_PULLDOWN, &on_off, &id);		
		vreg_disable(vreg_touch);

		rc = vreg_set_level(vreg_touch, 3050);
		if (rc != 0) {
			printk("[Touch] vreg_set_level failed\n");
			return -1;
		}
		vreg_enable(vreg_touch);
	} else {		
		vreg_disable(vreg_touch);
		on_off = 1;
		id = PM_VREG_PDOWN_SYNT_ID;
		msm_proc_comm(PCOM_VREG_PULLDOWN, &on_off, &id);		
	}

	return 0;
}

static struct touch_platform_data ts_pdata = {
	.ts_x_min = TS_X_MIN,
	.ts_x_max = TS_X_MAX,
	.ts_y_min = TS_Y_MIN,
	.ts_y_max = TS_Y_MAX,
	.power 	  = ts_set_vreg,
	.irq 	  = TS_GPIO_IRQ,
	.scl      = TS_GPIO_I2C_SCL,
	.sda      = TS_GPIO_I2C_SDA,
};

static struct i2c_board_info ts_i2c_bdinfo[] = {
	[0] = {
		I2C_BOARD_INFO("touch_mcs6000", TS_I2C_SLAVE_ADDR),
		.type = "touch_mcs6000",
		.platform_data = &ts_pdata,
	},
};

static void __init hazel_init_i2c_touch(int bus_num)
{
	ts_i2c_device.id = bus_num;

	init_gpio_i2c_pin_touch(&ts_i2c_pdata, ts_i2c_pin[0], &ts_i2c_bdinfo[0]);
	i2c_register_board_info(bus_num, &ts_i2c_bdinfo[0], 1);
	platform_device_register(&ts_i2c_device);
}

/* acceleration */
static int kr3dh_config_gpio(int config)
{
	if (config) {	/* for wake state */
		gpio_tlmm_config(GPIO_CFG(ACCEL_GPIO_I2C_SCL, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), GPIO_ENABLE);
		gpio_tlmm_config(GPIO_CFG(ACCEL_GPIO_I2C_SDA, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), GPIO_ENABLE);
	} else {		/* for sleep state */
		gpio_tlmm_config(GPIO_CFG(ACCEL_GPIO_I2C_SCL, 0, GPIO_INPUT, GPIO_NO_PULL, GPIO_2MA), GPIO_ENABLE);
		gpio_tlmm_config(GPIO_CFG(ACCEL_GPIO_I2C_SDA, 0, GPIO_INPUT, GPIO_NO_PULL, GPIO_2MA), GPIO_ENABLE);
	}

	return 0;
}

static int kr_init(void)
{
	return 0;
}

static void kr_exit(void)
{
}

static int accel_power_on(void)
{
	int ret = 0;
	struct vreg *gp3_vreg = vreg_get(0, "gp3");

	printk("[Accelerometer] %s() : Power On\n",__FUNCTION__);

	vreg_set_level(gp3_vreg, 3000);
	vreg_enable(gp3_vreg);

	return ret;
}

static int accel_power_off(void)
{
	int ret = 0;
	struct vreg *gp3_vreg = vreg_get(0, "gp3");

	printk("[Accelerometer] %s() : Power Off\n",__FUNCTION__);

	vreg_disable(gp3_vreg);

	return ret;
}

struct kr3dh_platform_data kr3dh_data = {
	.poll_interval = 100,
	.min_interval = 0,
	.g_range = 0x00,
	.axis_map_x = 0,
	.axis_map_y = 1,
	.axis_map_z = 2,

	.negate_x = 0,
	.negate_y = 0, 
	.negate_z = 0,

	.irq_pin = ACCEL_GPIO_INT,
	
	.power_on = accel_power_on,
	.power_off = accel_power_off,
	.kr_init = kr_init,
	.kr_exit = kr_exit,
	.gpio_config = kr3dh_config_gpio,
};

static struct gpio_i2c_pin accel_i2c_pin[] = {
	[0] = {
		.sda_pin	= ACCEL_GPIO_I2C_SDA,
		.scl_pin	= ACCEL_GPIO_I2C_SCL,
		.reset_pin	= 0,
		.irq_pin	= ACCEL_GPIO_INT,
	},
};

static struct i2c_gpio_platform_data accel_i2c_pdata = {
	.sda_is_open_drain = 0,
	.scl_is_open_drain = 0,
	.udelay = 2,
};

static struct platform_device accel_i2c_device = {
	.name = "i2c-gpio",
	.dev.platform_data = &accel_i2c_pdata,
};

static struct i2c_board_info accel_i2c_bdinfo[] = {
	[1] = {
		I2C_BOARD_INFO("KR3DH", ACCEL_I2C_ADDRESS_H),
		.type = "KR3DH",
		.platform_data = &kr3dh_data,
	},
	[0] = {
		I2C_BOARD_INFO("KR3DM", ACCEL_I2C_ADDRESS),
		.type = "KR3DM",
		.platform_data = &kr3dh_data,
	},
};

static void __init hazel_init_i2c_acceleration(int bus_num)
{
	accel_i2c_device.id = bus_num;

	init_gpio_i2c_pin(&accel_i2c_pdata, accel_i2c_pin[0], &accel_i2c_bdinfo[0]);

//	if(lge_bd_rev >= LGE_REV_11)
		i2c_register_board_info(bus_num, &accel_i2c_bdinfo[1], 1);	/* KR3DH */
//	else
//		i2c_register_board_info(bus_num, &accel_i2c_bdinfo[0], 1);	/* KR3DM */

	platform_device_register(&accel_i2c_device);
}

/* proximity & ecompass */
static int ecom_power_set(unsigned char onoff)
{
	int ret = 0;
	struct vreg *gp3_vreg = vreg_get(0, "gp3");

	printk("[Ecompass] %s() : Power %s\n",__FUNCTION__, onoff ? "On" : "Off");

	if (onoff) {
		vreg_set_level(gp3_vreg, 3000);
		vreg_enable(gp3_vreg);
	} else {
		vreg_disable(gp3_vreg);
	}

	return ret;
}

static struct ecom_platform_data ecom_pdata = {
	.pin_int        	= ECOM_GPIO_INT,
	.pin_rst		= 0,
	.power          	= ecom_power_set,
};

static struct i2c_board_info prox_ecom_i2c_bdinfo[] = {
	[0] = {
		I2C_BOARD_INFO("ami304_sensor", ECOM_I2C_ADDRESS),
		.type = "ami304_sensor",
		.platform_data = &ecom_pdata,
	},
};

static struct gpio_i2c_pin proxi_ecom_i2c_pin[] = {
	[0] = {
		.sda_pin	= ECOM_GPIO_I2C_SDA,
		.scl_pin	= ECOM_GPIO_I2C_SCL,
		.reset_pin	= 0,
		.irq_pin	= ECOM_GPIO_INT,
	},
};

static struct i2c_gpio_platform_data proxi_ecom_i2c_pdata = {
	.sda_is_open_drain = 0,
	.scl_is_open_drain = 0,
	.udelay = 2,
};

static struct platform_device proxi_ecom_i2c_device = {
        .name = "i2c-gpio",
        .dev.platform_data = &proxi_ecom_i2c_pdata,
};


static void __init hazel_init_i2c_prox_ecom(int bus_num)
{
	proxi_ecom_i2c_device.id = bus_num;

	init_gpio_i2c_pin(&proxi_ecom_i2c_pdata, proxi_ecom_i2c_pin[0], &prox_ecom_i2c_bdinfo[0]);

	i2c_register_board_info(bus_num, &prox_ecom_i2c_bdinfo[0], 1);
	platform_device_register(&proxi_ecom_i2c_device);
}

/* common function */
void __init lge_add_input_devices(void)
{
	hazel_select_keymap();
	platform_add_devices(hazel_input_devices, ARRAY_SIZE(hazel_input_devices));

	lge_add_gpio_i2c_device(hazel_init_i2c_touch);
	lge_add_gpio_i2c_device(hazel_init_i2c_prox_ecom);
	lge_add_gpio_i2c_device(hazel_init_i2c_acceleration);
}


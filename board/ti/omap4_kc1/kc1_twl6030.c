/*
 * (C) Copyright 2009
 * Texas Instruments, <www.ti.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include <config.h>
#include <asm/omap_common.h>
#include <command.h>
#include <asm/io.h>

#include <kc1_board.h>
#include <twl6030.h>

#include "kc1_twl6030.h"
#include "fg_bq27541.h"


void twl6030_start_usb_charging(void)
{
	debug("[TWL6030] %s:: ENTER\n", __func__);
	twl6030_i2c_write_u8(TWL6030_CHIP_CHARGER, CHARGERUSB_VICHRG, CHARGERUSB_VICHRG_500);
	twl6030_i2c_write_u8(TWL6030_CHIP_CHARGER, CHARGERUSB_CINLIMIT, CHARGERUSB_CIN_LIMIT_500);
	twl6030_i2c_write_u8(TWL6030_CHIP_CHARGER, CONTROLLER_INT_MASK, MBAT_TEMP);
	twl6030_i2c_write_u8(TWL6030_CHIP_CHARGER, CHARGERUSB_INT_MASK, MASK_MCHARGERUSB_THMREG);
	twl6030_i2c_write_u8(TWL6030_CHIP_CHARGER, CHARGERUSB_VOREG, CHARGERUSB_VOREG_4P76);
	twl6030_i2c_write_u8(TWL6030_CHIP_CHARGER, CHARGERUSB_CTRL2, CHARGERUSB_CTRL2_VITERM_100);
	/* Enable USB charging */
	twl6030_i2c_write_u8(TWL6030_CHIP_CHARGER, CONTROLLER_CTRL1, CONTROLLER_CTRL1_EN_CHARGER);
}

void twl6030_shutdown(void)
{
	debug("[TWL6030] %s:: ENTER\n", __func__);
	twl6030_i2c_write_u8(TWL6030_CHIP_PM, TWL6030_PHONIX_DEV_ON, 7);
}

int twl6030_get_vbus_status(void)
{   
	u8   data;
	debug("[TWL6030] %s:: ENTER\n", __func__);
	twl6030_i2c_read_u8(TWL6030_CHIP_CHARGER, CONTROLLER_STAT1, &data);//49,
	return ((data & 0x4) >> 2);
}

void twl6030_init_battery_charging(void)
{
	debug("[TWL6030] %s:: ENTER\n", __func__);
	get_bat_voltage();
	get_bat_current();
	twl6030_start_usb_charging();
}

void twl6030_init_vusb(void)
{
	u8 data;
	debug("[TWL6030] %s:: ENTER\n", __func__);
	/* Select APP Group and set state to ON */
	twl6030_i2c_read_u8(TWL6030_CHIP_PM, MISC2, &data);
	data |= 0x10;
	/* Select the input supply for VBUS regulator */
	twl6030_i2c_write_u8(TWL6030_CHIP_PM, MISC2, data);

	twl6030_i2c_write_u8(TWL6030_CHIP_PM, VUSB_CFG_VOLTAGE, 0x18);
	twl6030_i2c_write_u8(TWL6030_CHIP_PM, VUSB_CFG_GRP, 0x00);
	twl6030_i2c_write_u8(TWL6030_CHIP_PM, VUSB_CFG_TRANS, 0x00);
	twl6030_i2c_write_u8(TWL6030_CHIP_PM, VUSB_CFG_STATE, 0x01);
}

void twl6030_disable_vusb(void)
{
	u8   data;
	debug("[TWL6030] %s:: ENTER\n", __func__);
	twl6030_i2c_write_u8(TWL6030_CHIP_PM, VUSB_CFG_VOLTAGE, 0x18);
	twl6030_i2c_write_u8(TWL6030_CHIP_PM, VUSB_CFG_GRP, 0x00);
	twl6030_i2c_write_u8(TWL6030_CHIP_PM, VUSB_CFG_TRANS, 0x00);
	twl6030_i2c_write_u8(TWL6030_CHIP_PM, VUSB_CFG_STATE, 0x00);

	/* Select APP Group and set state to ON */
	twl6030_i2c_read_u8(TWL6030_CHIP_PM, MISC2, &data);
	data &= ~0x18;
	/* Select the input supply for VBUS regulator */
	twl6030_i2c_write_u8(TWL6030_CHIP_PM, MISC2, data);
}

void twl6030_usb_device_settings(void)
{
	debug("[TWL6030] %s:: ENTER\n", __func__);
	twl6030_init_vusb();
}


int twl6030_get_power_button_status(void)
{   
	u8 data;
	twl6030_i2c_read_u8 (TWL6030_CHIP_PM, PHOENIX_STS_HW_CONDITIONS, &data);
	return (data & 0x1);
}

u32 twl6030_print_boot_reason(void)
{
	u8 data1 = 0, data2 = 0, data3 = 0;
	volatile u32 data4 = 0;

	printf ("PMIC TWL 6030 start conditions\n");
	twl6030_i2c_read_u8 (TWL6030_CHIP_PM, PHOENIX_START_CONDITION, &data1);
	twl6030_i2c_read_u8 (TWL6030_CHIP_PM, PHOENIX_LAST_TURNOFF_STS, &data2);
	twl6030_i2c_read_u8 (TWL6030_CHIP_PM, PHOENIX_STS_HW_CONDITIONS, &data3);
	printf ("PHOENIX_START_CONDITION  :0x%02x\n", data1);
	printf ("PHOENIX_LAST_TURNOFF_STS :0x%02x\n", data2);
	printf ("PHOENIX_STS_HW_CONDITIONS:0x%02x\n", data3);
	data4 = __raw_readl(PRM_RSTST);
	/* PRM_RSTST 10:31 are reserved. Mask them off */
	data4 = (0x3FF & data4);
	printf ("OMAP4 PRM_RSTST          :0x%08x\n", data4);

	if (data4 & (1<<1)) // warm reset
		data1 = 0x2;

	return data1; // power button press start
}

int do_powerdown(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	twl6030_shutdown();
	/*NOTREACHED*/
	return 0;
}

U_BOOT_CMD( powerdown, 1, 0, do_powerdown, "powerdown.", NULL );

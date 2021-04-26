/*
 * (C) Copyright 2010
 * Texas Instruments Incorporated, <www.ti.com>
 * Aneesh V       <aneesh@ti.com>
 * Steve Sakoman  <steve@sakoman.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include <config.h>
#include <common.h>
#include <asm/omap_common.h>
#include <twl6030.h>
#include <asm/arch/sys_proto.h>
#include <asm/arch/mmc_host_def.h>
#include <asm/mach-types.h>
#ifdef CONFIG_USB_MUSB_OMAP2PLUS
#include <asm/omap_musb.h>
#include <linux/usb/gadget.h>
#include <linux/usb/musb.h>
#endif
#include <usb/fastboot.h>
#ifdef CONFIG_LCD
#include <lcd.h>
#endif

#include "kc1_debug.h"
#include "kc1_mux_data.h"
#include "kc1_twl6030.h"
#include "pmic_smb347.h"

#define PRM_RSTCTRL_RESET_WARM_BIT      (1<<0)
#define PRM_RSTST_RESET_WARM_BIT        (1<<1)
#define REBOOT_REASON_SIZE              0xf

DECLARE_GLOBAL_DATA_PTR;

const struct omap_sysinfo sysinfo = {
	"Board: OMAP4430 SDP\n"
};

#ifdef CONFIG_USB_MUSB_OMAP2PLUS
static struct musb_hdrc_config musb_config = {
	.multipoint     = 1,
	.dyn_fifo       = 1,
	.num_eps        = 16,
	.ram_bits       = 12,
};

static struct omap_musb_board_data musb_board_data = {
	.interface_type	= MUSB_INTERFACE_ULPI,
};

static struct musb_hdrc_platform_data musb_plat = {
#if defined(CONFIG_MUSB_HOST)
	.mode           = MUSB_HOST,
#elif defined(CONFIG_MUSB_GADGET)
	.mode		= MUSB_PERIPHERAL,
#else
#error "Please define either CONFIG_MUSB_HOST or CONFIG_MUSB_GADGET"
#endif
	.config         = &musb_config,
	.power          = 200,
	.platform_ops	= &omap2430_ops,
	.board_data	= &musb_board_data,
};
#endif

static int fastboot_mode = 0;
static int recovery_mode = 0;
static int start_condition = 0; // 0x1 == powerbutton, 0x2 == warm_reset, 0x8 == charger

/**
 * @brief board_init
 *
 * @return 0
 */
int board_init(void)
{
	gpmc_init();

	gd->bd->bi_boot_params = CONFIG_ADDR_ATAGS; /* boot param addr */

        /*init gpio 54 as ouput and gpio 39 as input*/
        sr32((void *)0x48055134,22,1,0); //setting gpio 54 as output and give high
        sr32((void *)0x4805513c,22,1,0);
        sr32((void *)0x48055134,7,1,1); //setting gpio 39 as input for detect u-boot

	return 0;
}

int check_init_fastboot(void)
{
	if (__raw_readl(PRM_RSTST) & PRM_RSTST_RESET_WARM_BIT)
		if (!strncmp((const char *)PUBLIC_SAR_RAM_1_FREE, "bootloader", REBOOT_REASON_SIZE)) {
			strncpy((char *)PUBLIC_SAR_RAM_1_FREE, "", REBOOT_REASON_SIZE);
			printf("\n Case: 'reboot bootloader'\n");
			fastboot_mode = 1;
		}

	if (fastboot_mode == 0) {
		//gpio_52 set configuration to MUX3 and INPUT_ENABLE
		__raw_writew(0x10B,0x4A100078);
		udelay(100000); /* 1 sec */

		//gpio_52 read GPIO_DATAIN
		if (__raw_readl(0x48055138) & 0x00100000) {
			printf("\nSPECIAL USB CABLE DETECTED: enter FASTBOOT now\n");
			fastboot_mode = 1;
		}
	}

	printf("*** %s::fastboot_mode == %d\n", __func__, fastboot_mode);
	return fastboot_mode;
}

int check_fastboot(void)
{
	return fastboot_mode;
}

int check_init_recovery(void)
{
	if (__raw_readl(PRM_RSTST) & PRM_RSTST_RESET_WARM_BIT)
		if (!strncmp((const char *)PUBLIC_SAR_RAM_1_FREE, "recovery", REBOOT_REASON_SIZE)) {
			strncpy((char *)PUBLIC_SAR_RAM_1_FREE, "", REBOOT_REASON_SIZE);
			printf("\n Case: 'reboot recovery'\n");
			recovery_mode = 1;
		}
	printf("*** %s::recovery_mode == %d\n", __func__, recovery_mode);
	return recovery_mode;
}

int check_recovery(void)
{
	return recovery_mode;
}

#ifdef CONFIG_LONGPRESS_POWERON
int check_longpress_loop(int countdown_length)
{
	int button_press = 0;
	printf("ENTER LONGPRESS CHECK\n");
	while (countdown_length)
	{
		button_press = twl6030_get_power_button_status();
		if (button_press != 0) {
			printf("EXIT LONGPRESS CHECK: not complete! shutting down.\n");
			return 1;
		}
		udelay(1000000); /* 1 sec */
		countdown_length--;
	}
	button_press = twl6030_get_power_button_status();
	printf("EXIT LONGPRESS CHECK (%d)\n", button_press);
	return (button_press != 0);
}
#endif

int power_init_board(void)
{
#ifdef CONFIG_CMD_FASTBOOT
	int pre_boot = check_init_fastboot();
#else
	int pre_boot = 0;
#endif

	check_init_recovery();
	__raw_writel(0x30003,0x4A10015C); // gpio 155 & 156
	__raw_writel(__raw_readl(0x4805B134) & 0xf7ffffff, 0x4805B134); //GPIO 5 OE
	__raw_writel(__raw_readl(0x4805B13C) | 0x8000000, 0x4805B13C); //GPIO 5 DATAOUT

	__raw_writel(__raw_readl(0x4A100090) | 3,0x4A100090); // gpio 101
	__raw_writel(__raw_readl(0x48059134) & 0xffffffdf, 0x48059134);
	__raw_writel(__raw_readl(0x4805913C) | 0x00000000, 0x4805913C); //GPIO 4 DATAOUT 0x4805913C

	start_condition = twl6030_print_boot_reason();
#ifdef CONFIG_LONGPRESS_POWERON
	if ((pre_boot == 0) && (start_condition & 0x1)) { // device started via power button
		if (check_longpress_loop(CONFIG_LONGPRESS_POWERON)) {
		        twl6030_shutdown();
			hang();
		}
	}
#endif

//	i2c_init(CFG_I2C_SPEED, CFG_I2C_SLAVE);
	if (pre_boot == 0) {
		check_low_bat();
	}

	if (pre_boot == 1) {
		lcd_set_text_rotate(LCD_TEXT_ROTATE_NONE);
		run_command("kc1panel logo_set_index 2", 0);
	}
	else {
		lcd_set_text_rotate(LCD_TEXT_ROTATE_270);
	}

	run_command("setgreenled FF", 0);
	return 0;
}

/**
 * @brief misc_init_r - Configure SDP board specific configurations
 * such as power configurations, ethernet initialization as phase2 of
 * boot sequence
 *
 * @return 0
 */
int misc_init_r(void)
{
#ifdef CONFIG_CHARGERMODE_ENABLE
	if ((fastboot_get_setting_bit(0)) && (start_condition & STRT_ON_PLUG_DET)) {
		setenv("chargermode", "androidboot.mode=charger ");
	}
	else
		setenv("chargermode", "");
#else
	setenv("chargermode", "");
#endif
	if (!fastboot_get_setting_bit(1))
		setenv("console", "null");

#ifdef CONFIG_USB_MUSB_OMAP2PLUS
	// Setup USB
	musb_register(&musb_plat, &musb_board_data, (void *)MUSB_BASE);
#endif

	/*
	* Summit charger has finished all the detection at
	*  this point, so we are safe to turn on the LCD
	*/
#ifndef CONFIG_LCD
	run_command("kc1panel on", 0);
#endif

#ifdef CONFIG_FASTBOOT_COUNTDOWN
	if ((fastboot_mode == 0) && ((start_condition & STRT_ON_PLUG_DET) == 0)) {
		// DO MENU PREP
		lcd_position_cursor((lcd_get_screen_columns()/2)-(lcd_strlen(CONFIG_WELCOME_MSG)/2), lcd_get_screen_rows()-1);
		lcd_printf(CONFIG_WELCOME_MSG);
	}
#endif
	run_command("setbacklight 7f", 0);
	if ((start_condition & STRT_ON_PLUG_DET) == 0)
		run_command("fastboot", 0);
	return 0;
}

void set_muxconf_regs_essential(void)
{
	do_set_mux((*ctrl)->control_padconf_core_base,
		   core_padconf_array_essential,
		   sizeof(core_padconf_array_essential) /
		   sizeof(struct pad_conf_entry));

	do_set_mux((*ctrl)->control_padconf_wkup_base,
		   wkup_padconf_array_essential,
		   sizeof(wkup_padconf_array_essential) /
		   sizeof(struct pad_conf_entry));
}

void set_muxconf_regs_non_essential(void)
{
	do_set_mux((*ctrl)->control_padconf_core_base,
		   core_padconf_array_non_essential,
		   sizeof(core_padconf_array_non_essential) /
		   sizeof(struct pad_conf_entry));

	do_set_mux((*ctrl)->control_padconf_wkup_base,
		   wkup_padconf_array_non_essential,
		   sizeof(wkup_padconf_array_non_essential) /
		   sizeof(struct pad_conf_entry));

	if (omap_revision() < OMAP4460_ES1_0) {
		do_set_mux((*ctrl)->control_padconf_wkup_base,
			wkup_padconf_array_non_essential_4430,
			sizeof(wkup_padconf_array_non_essential_4430) /
			sizeof(struct pad_conf_entry));
	}
}

#if !defined(CONFIG_SPL_BUILD) && defined(CONFIG_GENERIC_MMC)
int board_mmc_init(bd_t *bis)
{
//	omap_mmc_init(0, 0, 0, -1, -1);
	omap_mmc_init(1, 0, 0, -1, -1);
	return 0;
}
#endif

/*
 * get_board_rev() - get board revision
 */
u32 get_board_rev(void)
{
	return 0x2;
}

int do_recoverymode(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	recovery_mode = 1;
	return 0;
}

U_BOOT_CMD( recoverymode,	1,	1,	do_recoverymode, NULL, NULL);

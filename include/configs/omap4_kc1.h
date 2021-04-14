/*
 * Based on omap4_sdp4430
 * Modified by Michael Scott
 *
 * ----------------------------------------------------
 *
 * (C) Copyright 2010
 * Texas Instruments Incorporated.
 * Aneesh V       <aneesh@ti.com>
 * Steve Sakoman  <steve@sakoman.com>
 *
 * Configuration settings for the TI SDP4430 board.
 * See omap4_common.h for OMAP4 common part
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __CONFIG_OMAP4KC1_H
#define __CONFIG_OMAP4KC1_H

/*
 * High Level Configuration Options
 */
#define CONFIG_OMAP4KC1			1
#define CONFIG_OMAP4KC1_VERSION		"l2.05"
#define CONFIG_WELCOME_MSG		"[ PRESS POWER TO OPEN BOOTMENU ]"

#include <configs/omap4_common.h>

#define CONFIG_ARCH_OMAP4               1

#undef CONFIG_SYS_PTV
#define CONFIG_SYS_PTV                  7

/* DEBUG OUTPUT */
#define DEBUG				1

/*
 * select serial console configuration
 */
#define CONFIG_CONS_INDEX		3

/* MEMORY ENV + 16M */
#undef CONFIG_SYS_MALLOC_LEN
#undef CONFIG_SYS_SPL_MALLOC_SIZE
#define CONFIG_SYS_MALLOC_LEN		(CONFIG_ENV_SIZE + 0x01000000)
#define CONFIG_SYS_SPL_MALLOC_SIZE	CONFIG_SYS_MALLOC_LEN

/* Not an SBL build */
#undef CONFIG_SPL

/* SMB347 */
#undef CONFIG_TWL6030_POWER
#define CONFIG_POWER_FG_BQ27541		1
#define CONFIG_POWER_SMB347		1

/* SPI */
/* TODO: Repurpose OMAP3 SPI driver for OMAP4 */
#undef CONFIG_CMD_SPI
#undef CONFIG_OMAP3_SPI
#define CONFIG_OMAP4_SPI		1

/* DSS */
#define CONFIG_VIDEO_OMAP4		1

/* SPLASH SCREEN */
#define CONFIG_LCD			1
#define CONFIG_CMD_BMP			1
#define CONFIG_BMP_16BPP		1
#define CONFIG_BMP_32BPP		1
#define CONFIG_VIDEO_BMP_GZIP		1
#define CONFIG_SPLASH_SCREEN		1
#define CONFIG_SYS_VIDEO_LOGO_MAX_SIZE  0x00200000
#define CONFIG_SYS_WHITE_ON_BLACK	1
#define LCD_BPP				LCD_COLOR16
//#define CONFIG_VIDEO_FONT_5X12	1

/* Enhance our eMMC support / experience. */
#define CONFIG_CMD_GPT
#define CONFIG_EFI_PARTITION
#define CONFIG_PARTITION_UUIDS
#define CONFIG_CMD_PART
#define CONFIG_SUPPORT_EMMC_BOOT

/* Defines for SDRAM init */
#undef CONFIG_SYS_EMIF_PRECALCULATED_TIMING_REGS
#define CONFIG_SYS_AUTOMATIC_SDRAM_DETECTION
#define CONFIG_SYS_DEFAULT_LPDDR2_TIMINGS

/* ENV related config options */
#define CONFIG_ENV_IS_NOWHERE            1

/* undef to save memory */
//#undef CONFIG_SYS_LONGHELP
//#undef CONFIG_SYS_HUSH_PARSER

/* USB */
#undef CONFIG_MUSB_UDC
#undef CONFIG_USB_OMAP3
#undef CONFIG_USB_TTY
#define CONFIG_USBD_HS                   1

/* fastboot */
#define CONFIG_CMD_FASTBOOT              1
#ifdef CONFIG_CMD_FASTBOOT
#define DEVICE_VENDOR_ID                 0x18d1
#define DEVICE_PRODUCT_ID                0x0100
#define CONFIG_DISABLE_FASTBOOT_DMA      1
//#undef CONFIG_USB_DEVICE
//#define CONFIG_MUSB_GADGET               1
//#define CONFIG_USB_MUSB_OMAP2PLUS        1
//#define CONFIG_MUSB_PIO_ONLY             1
//#define CONFIG_USB_GADGET_DUALSPEED      1
//#define CONFIG_TWL6030_USB               1
#endif

// 5 second fastboot / button press countdown
#define CONFIG_FASTBOOT_COUNTDOWN        5
// poll power_button # times per second
#define CONFIG_FASTBOOT_COUNTDOWN_POLL_POWER 5

/* long press power to turn on (in seconds) */
#define CONFIG_LONGPRESS_POWERON         1

// charger mode
#define CONFIG_CHARGERMODE_ENABLE        1

// check "splash" partition for boot logo
#define CONFIG_LOAD_SPLASH_FROM_EMMC     1

#define MEMORY_BASE                      0x80000000
#define CONFIG_ADDR_ATAGS                (MEMORY_BASE + 0x100)
#define CONFIG_ADDR_DOWNLOAD             (MEMORY_BASE + 0x03000000)
//#define CONFIG_MACH_TYPE                 MACH_TYPE_OMAP_4430SDP
#define CONFIG_MACH_TYPE                 2161
#define DEVICE_TREE                      0x82f80000
#define CONFIG_OF_LIBFDT
#define CONFIG_ANDROID_BOOT_IMAGE        1

/* env */
#undef CONFIG_EXTRA_ENV_SETTINGS
#define CONFIG_EXTRA_ENV_SETTINGS \
"console=ttyO2,115200n8\0" \
"fdt_high=0xffffffff\0" \
"fdtaddr=0x80f80000\0" \
"initrd_high=no\0" \
"idme_wifimac=FFEEDDCCBBAA\0" \
"idme_serialno=0123456789ABCDEF\0" \
"idme_settings=0x00000000\0" \
"mmcdev=1\0" \
\
"mmcargs_old=setenv bootargs console=${console} mem=456M@0x80000000 init=/init vram=5M omapfb.vram=0:5M androidboot.console=ttyO2\0" \
"mmcargs_new=setenv dtbootargs console=${console} ${chargermode}androidboot.wifimac=${idme_wifimac} androidboot.serialno=${idme_serialno} androidboot.hardware=otterx\0" \
\
"lcdmenu_fg_color=7\0" \
"lcdmenu_bg_color=0\0" \
"lcdmenu_width=0x28\0" \
"lcdmenu_0=MAIN MENU\0" \
"lcdmenuentry_00=<-- CONTINUE BOOT=lcdmenu hide; run mmcargs_old; run mmcargs_new; booti mmc1\0" \
"lcdmenuentry_01=RECOVERY BOOT=lcdmenu hide; recoverymode; run mmcargs_old; run mmcargs_new; booti mmc1\0" \
"lcdmenuentry_02=ADVANCED -->=lcdmenu show 1\0" \
"lcdmenu_1=ADVANCED MENU\0" \
"lcdmenuentry_10=<-- BACK=lcdmenu show 0\0" \
"lcdmenuentry_11=CHARGER MODE:    #charger_mode_flag# =lcdmenu show #charger_mode_menu_id#\0" \
"lcdmenuentry_12=SERIAL CONSOLE:  #serial_console_flag# =lcdmenu show #serial_console_menu_id#\0" \
"lcdmenuentry_13=SERIAL #:        #serial_no# =lcdmenu show 0\0" \
"lcdmenuentry_14=WIFI MAC ADDR:   #wifi_mac# =lcdmenu show 0\0" \
"lcdmenuentry_15=PARTITION MODE:  #partition_mode# =lcdmenu show 6\0" \
"lcdmenuentry_16=TRIGGER USBBOOT MODE =lcdmenu show 7\0" \
"lcdmenuentry_17=EMMC CORRUPTION FIX =lcdmenu show 8\0" \
"lcdmenu_2=CHARGER MODE MENU\0" \
"lcdmenuentry_20=<-- BACK=lcdmenu show 1\0" \
"lcdmenuentry_21=\e[33m[CONFIRM]\e[37m SET MODE: \e[32mOFF\e[37m =idme_settings 0 0; lcdmenu show 1\0" \
"lcdmenu_3=CHARGER MODE MENU\0" \
"lcdmenuentry_30=<-- BACK=lcdmenu show 1\0" \
"lcdmenuentry_31=\e[33m[CONFIRM]\e[37m SET MODE: \e[32mON\e[37m =idme_settings 0 1; lcdmenu show 1\0" \
"lcdmenu_4=BOOT CONSOLE MENU\0" \
"lcdmenuentry_40=<-- BACK=lcdmenu show 1\0" \
"lcdmenuentry_41=\e[33m[CONFIRM]\e[37m SET MODE: \e[36mOFF\e[37m =idme_settings 1 0; lcdmenu show 1\0" \
"lcdmenu_5=BOOT CONSOLE MENU\0" \
"lcdmenuentry_50=<-- BACK=lcdmenu show 1\0" \
"lcdmenuentry_51=\e[33m[CONFIRM]\e[37m SET MODE: \e[36mON\e[37m =idme_settings 1 1; lcdmenu show 1\0" \
"lcdmenu_6=PARTITION MODE MENU \0" \
"lcdmenuentry_60=<-- BACK=lcdmenu show 1\0" \
"lcdmenuentry_61=\e[31m** WARNING!!!!\e[37m =\0" \
"lcdmenuentry_62=\e[31m** ERASES SYSTEM/CACHE/USERDATA/SDCARD!\e[37m =\0" \
"lcdmenuentry_63=\e[31m** NEEDS COMPATIBLE RECOVERY AND ROMS!\e[37m =\0" \
"lcdmenuentry_64=\e[33m[CONFIRM]\e[37m SET MODE: \e[36m#partition_mode_other#\e[37m =kc1_swap_partition_mode; lcdmenu show 1\0" \
"lcdmenu_7=USB BOOT MODE\0" \
"lcdmenuentry_70=<-- BACK=lcdmenu show 1\0" \
"lcdmenuentry_71=\e[36m** USE TO TEST NEW BOOTLOADERS\e[37m =\0" \
"lcdmenuentry_72=\e[31m** LASTS TILL FULL POWER OFF\e[37m =\0" \
"lcdmenuentry_73=\e[33m[CONFIRM]\e[37m RESTART =kc1_usbboot\0" \
"lcdmenu_8=EMMC CORRUPTION FIX\0" \
"lcdmenuentry_80=<-- BACK=lcdmenu show 1\0" \
"lcdmenuentry_81=\e[31m** WARNING!!!!\e[37m =\0" \
"lcdmenuentry_82=\e[31m** THIS ERASES *EVERYTHING*!!\e[37m =\0" \
"lcdmenuentry_83=\e[33m[CONFIRM]\e[37m RUN EMMC FIX =kc1_emmcfix\0"

#undef CONFIG_BOOTCOMMAND
#define CONFIG_BOOTCOMMAND		"run mmcargs_old; run mmcargs_new; booti mmc1"

// legacy ATAGS config
#define CONFIG_REVISION_TAG		1

#endif /* __CONFIG_OMAP4KC1_H */

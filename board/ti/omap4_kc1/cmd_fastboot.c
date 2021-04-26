/*
 * (C) Copyright 2008 - 2009
 * Windriver, <www.windriver.com>
 * Tom Rix <Tom.Rix@windriver.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
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
 *
 * Part of the rx_handler were copied from the Android project. 
 * Specifically rx command parsing in the  usb_rx_data_complete 
 * function of the file bootable/bootloader/legacy/usbloader/usbloader.c
 *
 * The logical naming of flash comes from the Android project
 * Thse structures and functions that look like fastboot_flash_* 
 * They come from bootable/bootloader/legacy/libboot/flash.c
 *
 * This is their Copyright:
 * 
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the 
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED 
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <config.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <common.h>
#include <command.h>
#include <nand.h>
#include <usb/fastboot.h>
#include <environment.h>
#include <twl6030.h>
#include <lcd.h>
#include <mmc.h>

#include "kc1_twl6030.h"
#include "include/sparse.h"

#define PRM_RSTCTRL_RESET_WARM_BIT	(1<<0)

/* Forward decl */
static int rx_handler (const unsigned char *buffer, unsigned int buffer_size);
static void reset_handler (void);

static struct cmd_fastboot_interface interface = 
{
	.rx_handler            = rx_handler,
	.reset_handler         = reset_handler,
	.product_name          = NULL,
	.serial_no             = NULL,
	.storage_medium        = 0,
	.nand_block_size       = 0,
	.transfer_buffer       = (unsigned char *)0xffffffff,
	.transfer_buffer_size  = 0,
};

// Used to process sparse image
static int          mmcwrite_is_sparsed = 0;

static unsigned int fastboot_confirmed = 0;
       unsigned int download_size;
static unsigned int download_bytes;
static unsigned int download_bytes_unpadded;
static unsigned int download_error;
static unsigned int mmc_controller_no = 1;

void show_fastbootmode(void)
{
	fastboot_confirmed = 2;
	run_command("kc1panel logo_set_index 2", 0);
	run_command("kc1panel logo_reload", 0);
#ifdef CONFIG_LCD
	lcd_set_text_rotate(LCD_TEXT_ROTATE_NONE);
	lcd_position_cursor(0, lcd_get_screen_rows());
	lcd_setbgcolor(CONSOLE_COLOR_BLACK);
	lcd_setfgcolor(CONSOLE_COLOR_WHITE);
	lcd_printf("FASTBOOT MODE ENTERED\n");
#endif
}

static void reset_handler()
{
	/* If there was a download going on, bail */
	download_size = 0;
	download_bytes = 0;
	download_bytes_unpadded = 0;
	download_error = 0;
}

static int write_to_ptn_emmc(struct fastboot_ptentry *ptn, unsigned long offset, char* response)
{
	int status     = -1;
	struct mmc *mmc = find_mmc_device(mmc_controller_no);

	if (!mmc) {
		printf("no mmc device at slot %x\n", mmc_controller_no);
		return 1;
	}

	// First chunk
	if (offset == 0) {
		printf("fastboot: writing to partition '%s'\n", ptn->name);
		lcd_printf("writing %s", ptn->name);

		if (mmc_init(mmc)) {
			sprintf(response, "FAIL:Init of MMC card");
			return status;
		}

		// Sparse image detection
		if (((sparse_header_t *)interface.transfer_buffer)->magic == SPARSE_HEADER_MAGIC) {
			/* Compressed image: sparse header is detected */
			printf("fastboot: image is in sparse format\n");
			do_unsparse_start(ptn->start, mmc->block_dev.dev);
			mmcwrite_is_sparsed = 1;
		} else {
			mmcwrite_is_sparsed = 0;
		}
	}

	// Write the image
	if(mmcwrite_is_sparsed) {
		// Sparsed image: unspase it
		printf("fastboot: Writing sparse image into '%s'\n", ptn->name);
		if (!do_unsparse_process(interface.transfer_buffer, download_bytes)) {
			printf(" DONE!\n");
			lcd_printf("...done\n\n");
			status = 0;
		} else {
			printf(" FAILED!\n");
			sprintf(response, "FAIL: Sparsed Write");
		}
	}
	else {
		/* Normal image: no sparse */
		printf("fastboot: Writing normal image into '%s'\n", ptn->name);
		if (mmc->block_dev.block_write(
			mmc->block_dev.dev,
			ptn->start + (offset / mmc->write_bl_len),
			(download_bytes + (mmc->write_bl_len - 1)) / mmc->write_bl_len,
			interface.transfer_buffer) == 0)
		{
			printf(" FAILED!\n");
			lcd_printf("...failed\n\n");
			sprintf(response, "FAIL: Write partition");
		} else {
			printf(" DONE!\n");
			lcd_printf("...done\n\n");
			status = 0;
		}
	}

	if(status == 0) sprintf(response, "OKAY");
	return status;
}

static int rx_handler (const unsigned char *buffer, unsigned int buffer_size)
{
	int ret = 1;

	/* Use 65 instead of 64
	   null gets dropped  
	   strcpy's need the extra byte */
	char response[65];

	if (download_size) {
		if (fastboot_confirmed == 0)
			show_fastbootmode();

		/* Something to download */
		if (buffer_size) {

			/* Handle possible overflow */
			unsigned int transfer_size = download_size - download_bytes;

			if (buffer_size < transfer_size)
				transfer_size = buffer_size;
			
			/* Save the data to the transfer buffer */
			memcpy (interface.transfer_buffer + download_bytes, 
				buffer, transfer_size);

			download_bytes += transfer_size;
			
			/* Check if transfer is done */
			if (download_bytes >= download_size) {
				/* Reset global transfer variable,
				   Keep download_bytes because it will be
				   used in the next possible flashing command */
				download_size = 0;

				if (download_error) {
					/* There was an earlier error */
					sprintf(response, "ERROR");
				} else {
					/* Everything has transferred,
					   send the OK response */
					sprintf(response, "OKAY");
				}
				fastboot_tx_status(response, strlen(response));

				printf ("\ndownloading of %d bytes finished\n",
					download_bytes);

			}

			/* Provide some feedback */
			if (download_bytes && 0 == (download_bytes % (128 * interface.nand_block_size))) {
				/* Some feeback that the download is happening */
				if (download_error) {
					printf("X");
					lcd_printf("X");
				}
				else {
					printf(".");
					lcd_printf(".");
				}
				if (0 == (download_bytes % (80 * 128 * interface.nand_block_size)))
					printf("\n");
				
			}
		}
		else {
			/* Ignore empty buffers */
			printf ("Warning empty download buffer\n");
			printf ("Ignoring\n");
		}
		ret = 0;
	}
	else
	{
		/* A command */
		if (fastboot_confirmed == 0)
			show_fastbootmode();

		/* Cast to make compiler happy with string functions */
		const char *cmdbuf = (char *) buffer;

		/* Generic failed response */
		sprintf(response, "FAIL");

		/* continue
		   Continue booting as normal (if possible) */
		if(memcmp(cmdbuf, "continue", 8) == 0) {
			sprintf(response, "OKAY");
			fastboot_tx_status(response, strlen(response));

			lcd_printf("Continue with kernel boot ...\n");

			run_command(CONFIG_BOOTCOMMAND, 0);

			sprintf(response, "FAIL: invalid boot image");
			ret = 0;
		}

		/* reboot 
		   Reboot the board. */
		if(memcmp(cmdbuf, "reboot-bootloader", 17) == 0)
		{
			sprintf(response,"OKAY");
			fastboot_tx_status(response, strlen(response));

			/* Clear all reset reasons */
			__raw_writel(0xfff, PRM_RSTST);

			strcpy((char *)PUBLIC_SAR_RAM_1_FREE, "bootloader");

			/* now warm reset the silicon */
			__raw_writel(PRM_RSTCTRL_RESET_WARM_BIT,
					PRM_RSTCTRL);
			return 0;
		}

		if(memcmp(cmdbuf, "reboot", 6) == 0) 
		{
			sprintf(response,"OKAY");
			fastboot_tx_status(response, strlen(response));

			do_reset (NULL, 0, 0, NULL);
			
			/* This code is unreachable,
			   leave it to make the compiler happy */
			return 0;
		}
		
		/* getvar
		   Get common fastboot variables
		   Board has a chance to handle other variables */
		if(memcmp(cmdbuf, "getvar:", 7) == 0) 
		{
			int get_var_length = strlen("getvar:");

			sprintf(response,"OKAY");

			if (!strcmp(cmdbuf + get_var_length, "version")) {
				strcpy(response + 4, FASTBOOT_VERSION);
				lcd_printf("version: %s\n", FASTBOOT_VERSION);
			} else if (!strcmp(cmdbuf + get_var_length, "product")) {
				if (interface.product_name) {
					strcpy(response + 4, interface.product_name);
					lcd_printf("product: %s\n", interface.product_name);
				}
			} else if (!strcmp(cmdbuf + get_var_length, "serialno")) {
				if (interface.serial_no) {
					strcpy(response + 4, interface.serial_no);
					lcd_printf("serialno: %s\n", interface.serial_no);
				}
			} else if (!strcmp(cmdbuf + get_var_length, "downloadsize")) {
				if (interface.transfer_buffer_size) {
					sprintf(response + 4, "%08x", interface.transfer_buffer_size);
					lcd_printf("downloadsize: %lu\n", interface.transfer_buffer_size);
				}
			} else if (!strcmp(cmdbuf + get_var_length, "max-download-size")) {
				if (interface.transfer_buffer_size) {
					sprintf(response + 4, "%u", interface.transfer_buffer_size);
					lcd_printf("max-download-size: %lu\n", interface.transfer_buffer_size);
				}
			} else if (!strcmp(cmdbuf + get_var_length, "cpurev")) {
				if (interface.proc_rev) {
					strcpy(response + 4, interface.proc_rev);
					lcd_printf("cpurev: %lu\n", interface.proc_rev);
				}
			} else if (!strcmp(cmdbuf + get_var_length, "secure")) {
				if (interface.proc_type) {
					strcpy(response + 4, interface.proc_type);
					lcd_printf("secure: %lu\n", interface.proc_rev);
				}
			} else if (!strcmp(cmdbuf + get_var_length, "wifimac")) {
				char *wifimac = getenv("idme_wifimac");
				if (wifimac) {
					strcpy(response + 4, wifimac);
					lcd_printf("wifimac: %s\n", wifimac);
				}
			} else {
				fastboot_getvar(cmdbuf + 7, response + 4);
			}
			ret = 0;

		}

		/* %fastboot oem <cmd> */
		if (memcmp(cmdbuf, "oem ", 4) == 0) {

			ret = 0;
			cmdbuf += 4;

			if (memcmp(cmdbuf, "shutdown", 8) == 0) {
				sprintf(response, "OKAY");
				fastboot_tx_status(response, strlen(response));
				twl6030_shutdown();
				goto done;
			}

			/* fastboot oem recovery */
			if(memcmp(cmdbuf, "recovery", 8) == 0){
				sprintf(response,"OKAY");
				fastboot_tx_status(response, strlen(response));

				/* Clear all reset reasons */
				__raw_writel(0xfff, PRM_RSTST);
				strcpy((char *)PUBLIC_SAR_RAM_1_FREE, "recovery");
				/* now warm reset the silicon */
				__raw_writel(PRM_RSTCTRL_RESET_WARM_BIT, PRM_RSTCTRL);
				/* Never returns */
				while(1);
			}

			/* fastboot oem unlock */
			if(memcmp(cmdbuf, "unlock", 6) == 0){
				sprintf(response,"FAIL");
				lcd_printf("WARNING: oem unlock not implemented.\n");
				printf("\nfastboot: oem unlock not implemented yet!!\n");
				goto done;
			}

			ret = fastboot_oem(cmdbuf);
			if (ret == -1) {
				/* fastboot oem [xxx] */
				printf("\nfastboot: do not understand oem %s\n", cmdbuf);
				lcd_printf("ERROR: do not understand oem %s\n", cmdbuf);
				sprintf(response,"FAIL: do not understand oem %s", cmdbuf);
			} else if (ret != 0) {
				strcpy(response,"FAIL");
			} else {
				strcpy(response,"OKAY");
			}
			goto done;

		} /* end: %fastboot oem <cmd> */

		/* erase
		   Erase a register flash partition
		   Board has to set up flash partitions */

		if(memcmp(cmdbuf, "erase:", 6) == 0){

			if (interface.storage_medium == EMMC) {
				/* storage medium is EMMC */

				struct fastboot_ptentry *ptn;

				/* Find the partition and erase it */
				ptn = fastboot_flash_find_ptn(cmdbuf + 6);

				if (ptn == 0) {
					sprintf(response, "FAIL: partition doesn't exist");
					lcd_printf("ERROR: partition %s doesn't exist\n", cmdbuf + 6);
				} else {
					/* Call MMC erase function here */
					struct mmc *mmc = find_mmc_device(mmc_controller_no);
					if (!mmc) {
						printf("no mmc device at slot %x\n", mmc_controller_no);
						sprintf(response, "FAIL: No mmc device at slot %d", mmc_controller_no);
						lcd_printf("ERROR: No mmc device at slot %d\n", mmc_controller_no);
						return 1;
					}

					printf("Initializing '%s'\n", ptn->name);
					if (mmc_init(mmc))
						sprintf(response, "FAIL: Init of MMC card");
					else
						sprintf(response, "OKAY");

					printf("Erasing '%s'\n", ptn->name);
					lcd_printf("Erasing '%s' ", ptn->name);
					if (mmc->block_dev.block_erase(
						mmc->block_dev.dev,
						ptn->start,
						(ptn->length + (mmc->write_bl_len - 1)) / mmc->write_bl_len) == 0)
					{
						printf("Erasing '%s' FAILED!\n", ptn->name);
						lcd_printf("... FAILED!\n");
						sprintf(response, "FAIL: Erase partition");
					} else {
						printf("Erasing '%s' DONE!\n", ptn->name);
						lcd_printf("... DONE!\n");
						sprintf(response, "OKAY");
					}
				}
			}

			ret = 0;
		}


		/* EMMC Erase
		   Erase a register flash partition on MMC
		   Board has to set up flash partitions */
		if (memcmp(cmdbuf, "mmcerase:", 9) == 0) {
			struct fastboot_ptentry *ptn;

			/* Save the MMC controller number */
			unsigned int slot_no = simple_strtoul(cmdbuf + 9, NULL, 10);
			/* Find the partition and erase it */
			ptn = fastboot_flash_find_ptn(cmdbuf + 11);

			if (ptn == 0) {
				sprintf(response, "FAIL: partition doesn't exist");
				lcd_printf("ERROR: partition %s doesn't exist\n", cmdbuf + 11);
			} else {
				/* Call MMC erase function here */
				struct mmc *mmc = find_mmc_device(slot_no);
				if (!mmc) {
					printf("no mmc device at slot %x\n", slot_no);
					sprintf(response, "FAIL: No mmc device at slot %d\n", slot_no);
					lcd_printf("ERROR: No mmc device at slot %d\n", slot_no);
					return 1;
				}

				printf("Initializing '%s'\n", ptn->name);
				if (mmc_init(mmc)) {
					sprintf(response, "FAIL: Init of MMC card");
					lcd_printf("ERROR: Init of MMC card\n");
				}
				else
					sprintf(response, "OKAY");

				printf("Erasing '%s'\n", ptn->name);
				lcd_printf("Erasing '%s' ", ptn->name);
				if (mmc->block_dev.block_erase(
					mmc->block_dev.dev,
					ptn->start,
					(ptn->length + (mmc->write_bl_len - 1)) / mmc->write_bl_len) == 0)
				{
					printf("Erasing '%s' FAILED!\n", ptn->name);
					lcd_printf("... FAILED!\n");
					sprintf(response, "FAIL: Erase partition");
				} else {
					printf("Erasing '%s' DONE!\n", ptn->name);
					lcd_printf("... DONE!\n");
					sprintf(response, "OKAY");
				}
			}
		}

		/* download
		   download something .. 
		   What happens to it depends on the next command after data */
		if(memcmp(cmdbuf, "download:", 9) == 0) {

			/* save the size */
			download_size = simple_strtoul(cmdbuf + 9, NULL, 16);
			/* Reset the bytes count, now it is safe */
			download_bytes = 0;
			/* Reset error */
			download_error = 0;

			printf("Starting download of %d bytes\n", download_size);
			lcd_printf("Start downloading \n");

			if (0 == download_size)
			{
				/* bad user input */
				sprintf(response, "FAILdata invalid size");
			}
			else if (download_size > interface.transfer_buffer_size) {
				/* set download_size to 0
				 * because this is an error */
				download_size = 0;
				sprintf(response, "FAILdata too large");
			}
			else
			{
				/* The default case, the transfer fits
				   completely in the interface buffer */
				sprintf(response, "DATA%08x", download_size);
			}
			ret = 0;
		}

		/* boot
		   boot what was downloaded
		   **
		   ** +-----------------+
		   ** | boot header     | 1 page
		   ** +-----------------+
		   ** | kernel          | n pages
		   ** +-----------------+
		   ** | ramdisk         | m pages
		   ** +-----------------+
		   ** | second stage    | o pages
		   ** +-----------------+
		   **
		   Pagesize has default value of
		   CFG_FASTBOOT_MKBOOTIMAGE_PAGE_SIZE
		*/

		if(memcmp(cmdbuf, "boot", 4) == 0) {

			if ((download_bytes) && (download_bytes > CFG_FASTBOOT_MKBOOTIMAGE_PAGE_SIZE))
			{
				char booti_cmd[100];

				/* Execution should jump to kernel so send the response
				   now and wait a bit.  */
				sprintf(response, "OKAY");
				fastboot_tx_status(response, strlen(response));

				printf ("Booting kernel from RAM...\n");

				/* boot the boot.img */
				sprintf(booti_cmd, "run mmcargs_old; run mmcargs_new; booti ram 0x%08x", (unsigned int)interface.transfer_buffer);
				run_command(booti_cmd, 0);
			}
			sprintf(response, "FAILinvalid boot image");
			ret = 0;
		}

		/* mmcwrite
		   write what was downloaded on MMC*/
		if (memcmp(cmdbuf, "mmcwrite:", 9) == 0) {

			if (download_bytes) {

				struct fastboot_ptentry *ptn;

				/* Next is the partition name */
				ptn = fastboot_flash_find_ptn(cmdbuf + 11);

				if (ptn == 0) {
					sprintf(response, "FAILpartition does not exist");
				} else {
					write_to_ptn_emmc(ptn, 0, response);
				}

			} else {
				sprintf(response, "FAILno image downloaded");
			}
		}


		/* flash
		   Flash what was downloaded */

		if(memcmp(cmdbuf, "flash:", 6) == 0) {

			if (interface.storage_medium == EMMC) {
				/* storage medium is EMMC */

				if (download_bytes) {

					struct fastboot_ptentry *ptn;
					struct mmc *mmc = find_mmc_device(mmc_controller_no);
					if (!mmc) {
						printf("no mmc device at slot %x\n", mmc_controller_no);
						sprintf(response, "FAIL: No mmc device at slot %d\n", mmc_controller_no);
						lcd_printf("ERROR: No mmc device at slot %d\n", mmc_controller_no);
						return 1;
					}

					/* Next is the partition name */
					if (memcmp(cmdbuf+6, "bootpart:", 9) == 0) {
						struct fastboot_ptentry tmpptn;
						int bootpart = 0;
						printf("boot partition testing\n");
						ptn=&tmpptn;
						bootpart = simple_strtoul(cmdbuf + 15, NULL, 10);
						sprintf(ptn->name,"bootpart%lu", bootpart);
						ptn->length = (download_bytes + (mmc->write_bl_len - 1)) / mmc->write_bl_len;
						ptn->start  = 0;
						printf("name=%s length=%llu offset=%x\n",
							ptn->name, ptn->length, ptn->start);

						mmc_boot_part_access(mmc, 0, 0, bootpart + 1);
						write_to_ptn_emmc(ptn, 0, response);
						mmc_boot_part_access(mmc, 0, 0, 0);
					}
					else {
						ptn = fastboot_flash_find_ptn(cmdbuf + 6);

						if (ptn == 0) {
							printf("Partition:'%s' does not exist\n", ptn->name);
							sprintf(response, "FAILpartition does not exist");
							lcd_printf("ERROR: Partition:'%s' does not exist\n", ptn->name);
						}
						else if (download_bytes > ptn->length) {
							printf("Image too large for the partition\n");
							sprintf(response, "FAILimage too large for partition");
							lcd_printf("ERROR: image too large for partition\n");
						}
						else {
							write_to_ptn_emmc(ptn, 0, response);
						} /* Normal Case */
					}

				} else {
					sprintf(response, "FAILno image downloaded");
				}
			} /* EMMC */

			ret = 0;
		} /* fastboot flash ... */
done:
		fastboot_tx_status(response, strlen(response));

	} /* End of command */
	
	return ret;
}

//#define DEBUG_FASTBOOT_PULSE	1

int do_fastboot(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	int ret = 0;
#ifdef CONFIG_FASTBOOT_COUNTDOWN
	int bootmenu_open = 0;
	int keycode = 0;
/* how many loops per second */
#define LOOP_SPEED 1000000
	int countdown_length = CONFIG_FASTBOOT_COUNTDOWN * LOOP_SPEED;
	int val = check_fastboot();
#endif

	drv_twl6030_pwrbutton_init();

	if (argc >= 2)
		fastboot_confirmed = (unsigned int)*(argv[1])-48;

	if (fastboot_confirmed == 0)
		ret = fastboot_init(&interface);

	/* Initialize the board specific support */
	if (0 == ret)
	{
		printf ("Fastboot entered... (fastboot_mode = %u)\n", val);

		/* If we got this far, we are a success */
		ret = 0;

		// Set low green LED at bootmenu wait
		run_command("setgreenled 50", 0);

#ifdef CONFIG_FASTBOOT_COUNTDOWN
		while (countdown_length)
#else
		while (1)
#endif
		{
			if (fastboot_confirmed == 1)
				show_fastbootmode();

			if (fastboot_confirmed == 2) {
				while (1)
				{
					if (fastboot_poll())
						break;
				}
			}

			if (fastboot_poll())
				break;

#ifdef CONFIG_FASTBOOT_COUNTDOWN
			countdown_length--;
#ifdef DEBUG_FASTBOOT_PULSE
			if ((countdown_length % LOOP_SPEED) == 0)
				printf("*** %s::PULSE [%d]\n", __func__, countdown_length);
#endif

			keycode = pwrbutton_getc();
			if (keycode != 0) {
				// RESET FASTBOOT TO A LONG TIMER
				countdown_length = LOOP_SPEED * 60; /* 60 seconds */

				// HANDLE MENU OPEN
				if (bootmenu_open == 0) {
					bootmenu_open = 1;
					run_command("lcdmenu show 0", 0);
				}
				if (keycode == PWRBTN_KEY_LONG_PRESS)
					run_command("lcdmenu key select", 0);
				else
					run_command("lcdmenu key down", 0);
			}

			if ((!countdown_length) && ((val) || (fastboot_confirmed != 0))) {
				/* reset countdown to hold fastboot */
				countdown_length = CONFIG_FASTBOOT_COUNTDOWN * LOOP_SPEED;
			}
			udelay(1000000 / LOOP_SPEED);
#endif
		}
	}
	if (bootmenu_open) {
		run_command("lcdmenu hide", 0);
	}

	/* Reset the board specific support */
	fastboot_shutdown();
	printf ("Fastboot exited...\n");
	return ret;
}

U_BOOT_CMD(
	fastboot,	2,	1,	do_fastboot,
	"fastboot- use USB Fastboot protocol",
	"fastboot"
	"fastboot <fastboot_confirmed>"
);


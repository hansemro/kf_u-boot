// otter panel setup uses omap4_dss driver

#include <config.h>
#include <common.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/omap_common.h>
#include <asm/gpio.h>
#include <command.h>
#include <i2c.h>
#include <video_fb.h>
#include <asm/arch/dss.h>
#ifdef CONFIG_LCD
#include <lcd.h>
#endif
#include <version.h>
#ifdef CONFIG_CMD_FASTBOOT
#include <usb/fastboot.h>
#include <asm/sizes.h>
#include <usb/fastboot.h>
#include <mmc.h>
#include <malloc.h>
#include <asm/omap_mmc.h>
#endif

#include "kc1_debug.h"
#include "omap4_spi.h"

#define LCD_WIDTH		1024
#define LCD_HEIGHT		600
#define PANEL_DELAY_COUNT	100
#define PANEL_TIMEOUT		1000

enum logo_state {
	LOGO_SPLASH,
	LOGO_LOW_BATT,
	LOGO_FASTBOOT,
};

//#define PANEL_DEBUG		1

#ifdef PANEL_DEBUG
#define panel_writel(v,a)	debug("[WRITEL] addr=0x%08x, value=0x%08x\n", a, v);__raw_writel((unsigned int )(v),(unsigned int *)(a))
#define panel_writew(v,a)	debug("[WRITEW] addr=0x%08x, value=0x%04x\n", a, v);__raw_writew((unsigned int )(v),(unsigned int *)(a))
#else
#define panel_writel(v,a)	__raw_writel((unsigned int )(v),(unsigned int *)(a))
#define panel_writew(v,a)	__raw_writew((unsigned int )(v),(unsigned int *)(a))
#endif
#define panel_readl(a)		__raw_readl(a)
#define panel_readw(a)		__raw_readw(a)

#ifdef CONFIG_LCD
extern char const _binary_initlogo_bmp_gz_start[];
extern char const _binary_initlogo_bmp_gz_end[];
extern char const _binary_lowbattery_bmp_gz_start[];
extern char const _binary_lowbattery_bmp_gz_end[];
extern char const _binary_multi_download_bmp_gz_start[];
extern char const _binary_multi_download_bmp_gz_end[];
#else
extern char const _binary_initlogo_rle_start[];
extern char const _binary_initlogo_rle_end[];
extern char const _binary_lowbattery_rle_start[];
extern char const _binary_lowbattery_rle_end[];
extern char const _binary_multi_download_rle_start[];
extern char const _binary_multi_download_rle_end[];
#endif

static unsigned long logo_index = LOGO_SPLASH;
extern struct prcm_regs const **prcm;

void lcd_drawchars(ushort x, ushort y, uchar *str, int count);

static const struct panel_config dpi_panel_cfg = {
	.display_type	= OMAP_DISPLAY_TYPE_DPI,
	.channel	= OMAP_DSS_CHANNEL_LCD2,
	.panel_udelay	= 10000,

	.pixel_clock    = 51200, /* in kHz */
	.lck_div        = 1,
	.pck_div        = 4,

	.x_res          = LCD_WIDTH,
	.y_res          = LCD_HEIGHT,
	.hfp            = 160,   /* HFP fix 160 */
	.hsw            = 10,    /* HSW = 1~140 */
	.hbp            = 150,   /* HSW + HBP = 160 */
	.vfp            = 12,    /* VFP fix 12 */
	.vsw            = 3,     /* VSW = 1~20 */
	.vbp            = 20,    /* VSW + VBP = 23 */

	.pol_freq	= OMAP_DSS_LCD_IVS | OMAP_DSS_LCD_IHS, /* Pol Freq 0x3000 */

	.panel_type	= ACTIVE_DISPLAY, /* TFT */
	.data_lines	= LCD_INTERFACE_24_BIT, /* 24 Bit RGB */
	.load_mode	= 0x02, /* Frame Mode */
	.panel_color	= 0x00000000,
	.trans_color	= 0x00000000,
};

static const struct graphics_config gfx_cfg = {
	.x_pos			= 0,
	.y_pos			= 0,
	.gfx_format		= OMAP_DISP_GFX_RGB16,
	.fifo_threshold_low	= 560,
	.fifo_threshold_high	= 1279,
	.fifo_preload		= 1279,
	.row_inc		= 1,
	.pixel_inc		= 1,
	.window_skip		= 0,
	.video_data_format	= GDF_16BIT_565RGB,
};

#ifdef CONFIG_LCD
vidinfo_t panel_info = {
	.vl_col  = LCD_WIDTH,
	.vl_row  = LCD_HEIGHT,
	.vl_bpix = LCD_COLOR16,
	.cmap 	 = NULL,
};
#endif

void render_image(void);

void set_logoindex(unsigned long index)
{
	logo_index = index;
}

void turn_off_lcd(void)
{
	/* Turn off backlight */
	run_command("setbacklight 0", 0);

	/* GPIO_37 (OMAP_RGB_SHTDOWN, switch to LVDS mode) */
	panel_writew(0x1B,0x4a10005A); // 37 SELECT GPIO_MUX, PULLEN, PULLUP
	gpio_direction_output(37, 0);
	udelay(2000);

	/* GPIO_47 (OMAP_3V_ENABLE, 3.3 V rail) */
	panel_writel(0x001B0007,0x4a10006C); // 47 SELECT GPIO_MUX, PULLEN, PULLUP, 48 SAFE
	gpio_direction_output(47, 0);

	udelay(dpi_panel_cfg.panel_udelay);

	/* GPIO_47 (OMAP_3V_ENABLE, 3.3 V rail) */
	panel_writel(0x001B0007,0x4a10006C); // 47 SELECT GPIO_MUX, PULLEN, PULLUP, 48 SAFE
	gpio_direction_output(47, 0);

	/* GPIO_45 (LCD_PWR_ON) */
	panel_writew(0x1B,0x4a10006A); // 45 SELECT GPIO_MUX, PULLEN, PULLUP
	gpio_direction_output(45, 0);
	gpio_direction_output(119, 0);
	gpio_direction_output(120, 0);
}

#ifdef CONFIG_LCD

/************************************************************************/
/**  Small utility to check that you got the colours right		*/
/************************************************************************/
#define	N_BLK_VERT	2
#define	N_BLK_HOR	3

static int test_colors[N_BLK_HOR * N_BLK_VERT] = {
	CONSOLE_COLOR_RED,	CONSOLE_COLOR_GREEN,	CONSOLE_COLOR_YELLOW,
	CONSOLE_COLOR_BLUE,	CONSOLE_COLOR_MAGENTA,	CONSOLE_COLOR_CYAN,
};

static void kc1_test_pattern(void)
{
	ushort v_max  = panel_info.vl_row;
	ushort h_max  = panel_info.vl_col;
	ushort v_step = (v_max + N_BLK_VERT - 1) / N_BLK_VERT;
	ushort h_step = (h_max + N_BLK_HOR  - 1) / N_BLK_HOR;
	ushort v, h;
	uint16_t *pix = (uint16_t *)omap4_dss_get_frame_buffer();

	printf("[LCD] Test Pattern: %d x %d [%d x %d]\n",
		h_max, v_max, h_step, v_step);

	/* WARNING: Code silently assumes 8bit/pixel */
	for (v = 0; v < v_max; ++v) {
		uchar iy = v / v_step;
		for (h = 0; h < h_max; ++h) {
			uchar ix = N_BLK_HOR * iy + h / h_step;
			*pix++ = test_colors[ix];
		}
	}
}

static void kc1_text_grid(void)
{
	int i, j;
	char line1[255], line2[255];
	int cols = lcd_get_screen_columns();
	int rows = lcd_get_screen_rows();
	for (j = 1; j <= cols; j++)
		line1[j-1]=(48 + (j % 10));
	line1[cols+1] = '\0';
	for (j = 1; j <= cols; j++) {
		if ((j % 10)==0)
			line2[j-1]=(48 + (j / 10));
		else
			line2[j-1]=' ';
	}
	line2[cols+1] = '\0';
	for (i = 0; i < rows; i+=2) {
		if (((i % 2) == 0) && (i < rows)) {
			lcd_position_cursor(0, i);
			lcd_setfgcolor(test_colors[i % 6]);
			lcd_printf(line1);
		}
		if (((i % 2) != 0) && ((i+1) < rows)) {
			lcd_position_cursor(0, i + 1);
			lcd_setfgcolor(test_colors[(i+1) % 6]);
			lcd_printf(line2);
		}
	}
}
#endif

/**********************************************************
 * Routine: initialize_lcd
 * Description: Initializes the LCD and displays the
 *              splash logo
 **********************************************************/
void initialize_lcd(void)
{
	struct dispc_regs *dispc = (struct dispc_regs *) OMAP4_DISPC_BASE;
	struct dsi_regs *dsi2 = (struct dsi_regs *) OMAP4_DSI2_BASE;

	debug("*** %s\n", __func__);
	omap4_dss_register_configs(&dpi_panel_cfg, &gfx_cfg);
	panel_writel(0x001B001B, 0x4a100110); // 119/120 SELECT GPIO_MUX, PULLEN, PULLUP
	panel_writew(panel_readw(0x48059134) & 0xFEFF, 0x48059134);
	panel_writew(panel_readw(0x4805913C) | 0x0100, 0x4805913C);

	panel_writew(panel_readw(0x48059134) & 0xFF7F, 0x48059134);
	panel_writew(panel_readw(0x4805913C) | 0x0080, 0x4805913C);

	spi_init();

	panel_writew(0x1B, 0x4a10006A); // 45 SELECT GPIO_MUX, PULLEN, PULLUP, 48 SAFE
	panel_writew(panel_readw(0x48055134) & 0xDFFF, 0x48055134);//gpio_OE gpio45
	panel_writew(panel_readw(0x4805513C) | 0x2000, 0x4805513C);//gpio_dataout gpio45
	//gpio_direction_output(45, 1);

	/* Enable GPIO_47 (OMAP_3V_ENABLE, 3.3 V rail) */
	panel_writel(0x001B0007,0x4a10006C); // 47 SELECT GPIO_MUX, PULLEN, PULLUP, 48 SAFE
	panel_writew(panel_readw(0x48055134) & 0x7FFF, 0x48055134);//gpio_OE gpio47
	panel_writew(panel_readw(0x4805513C) | 0x8000, 0x4805513C);//gpio_dataout gpio47
	//gpio_direction_output(47, 1);

	udelay(dpi_panel_cfg.panel_udelay);
	spi_command();

	udelay(2000);

	/* Enable GPIO_37 (OMAP_RGB_SHTDOWN, switch to LVDS mode) */
	panel_writew(0x1B, 0x4a10005A); // 37 SELECT GPIO_MUX, PULLEN, PULLUP
	panel_writew(panel_readw(0x48055134) & 0xFFDF, 0x48055134);//gpio_OE gpio37
	panel_writew(panel_readw(0x4805513C) | 0x0020, 0x4805513C);//gpio_dataout gpio37
	//gpio_direction_output(37, 1);

	run_command("cls", 0);

	// render buffer
	render_image();

	/* cm_div_m5_dpll_per Set bit8 = 1, force HSDIVIDER_CLKOUT2 clock enabled*/
	panel_writew(panel_readw((*prcm)->cm_div_m5_dpll_per) | 0x100, (*prcm)->cm_div_m5_dpll_per);
	/* cm_ssc_deltamstep_dpll_per */
	panel_writew(0xCC , (*prcm)->cm_ssc_deltamstep_dpll_per);
	/* cm_ssc_modfreqdiv_dpll_per */
	panel_writew(0x264 , (*prcm)->cm_ssc_modfreqdiv_dpll_per);
	/* cm_clkmode_dpll_per Set bit12 = 1, force DPLL_SSC_EN enabled*/
	panel_writew(panel_readw((*prcm)->cm_clkmode_dpll_per) | 0x1000, (*prcm)->cm_clkmode_dpll_per);

	omap4_dss_reset_display_controller(PANEL_DELAY_COUNT, PANEL_TIMEOUT);

	// Enable DSS clocks
	panel_writel(0x00000502, (*prcm)->cm_dss_dss_clkctrl);

	// Configure the clock source
	// VP_CLK_POL, DE ACTIVE LOW, HSYNC ACTIVE LOW, NO LINE BUFFER, NO HSYNC START SHORT PACKET
	panel_writel((1<<8 | 0<<9 | 0<<10 | 0<<12 | 0<<17), &(dsi2->ctrl));
	udelay(dpi_panel_cfg.panel_udelay);

	// Configure interconnect parameters
	panel_writel((1<<0 | 1<<2 | 1<<4 | 1<<13), &(dispc->sysconfig));
	udelay(dpi_panel_cfg.panel_udelay);

	// Disable any interrupts
	panel_writel(0x0, &(dsi2->irqenable));
	udelay(dpi_panel_cfg.panel_udelay);

	omap4_dss_apply_graphics_config();

	// Setup the DSS1 clock divider - disable DSS1 clock, change divider, enable DSS clock
	// Enable LCD clocks

	panel_writel(panel_readl((*prcm)->cm_dss_dss_clkctrl) & ~(0x00000502), (*prcm)->cm_dss_dss_clkctrl);
	udelay(dpi_panel_cfg.panel_udelay);

	panel_writel(panel_readl((*prcm)->cm_div_m5_dpll_per) | (8 << 0), (*prcm)->cm_div_m5_dpll_per);
	udelay(dpi_panel_cfg.panel_udelay);

	// TURN ON: 1=DSS_CM_ON,8=DSS_OPTFuncClockEN,10=SYS_OPTFuncClockEN
	panel_writel(0x00000502, (*prcm)->cm_dss_dss_clkctrl);
	udelay(dpi_panel_cfg.panel_udelay);

	// TURN ON: 1=AUTO_IDLE,4=ENWAKEUP,10=SMARTIDLE
	panel_writel(0x00000015, &(dsi2->sysconfig));
	udelay(dpi_panel_cfg.panel_udelay);

	omap4_dss_apply_panel_config();
	udelay(dpi_panel_cfg.panel_udelay);

	omap4_dss_apply_disp_config(PANEL_DELAY_COUNT, PANEL_TIMEOUT);

	omap4_dss_enable();

	omap4_dss_video_init(1);
}

#ifdef CONFIG_LCD
void render_image(void)
{
	int displayed = 0;
	u_int16_t *start = 0;
	char buffer_cmd[100];
	if (logo_index == LOGO_FASTBOOT)
		start = (u_int16_t *)_binary_multi_download_bmp_gz_start;
	else if (logo_index == LOGO_LOW_BATT)
		start = (u_int16_t *)_binary_lowbattery_bmp_gz_start;
	else {
#ifdef CONFIG_LOAD_SPLASH_FROM_EMMC
		struct fastboot_ptentry *pte = NULL;
		struct mmc* mmc = NULL;
		void *dst = NULL;
		void *splash_bmp = NULL;
		bmp_image_t *bmp;

		mmc = find_mmc_device(1);
		if (mmc && (!mmc_init(mmc))) {
			pte = fastboot_flash_find_ptn("splash");
			if (pte) {
				dst = malloc(CONFIG_SYS_VIDEO_LOGO_MAX_SIZE + 3);
				if (dst) {
					if (mmc->block_dev.block_read(1, pte->start,
							CONFIG_SYS_VIDEO_LOGO_MAX_SIZE / mmc->read_bl_len, dst) > 0) {
						/* align to 32-bit-aligned-address + 2 */
						bmp = (bmp_image_t *)((((unsigned int)dst + 1) & ~3) + 2);
						memmove(bmp, dst, CONFIG_SYS_VIDEO_LOGO_MAX_SIZE); // shift this for reading
						start = (u_int16_t *)bmp;
						sprintf(buffer_cmd, "bmp display 0x%8x 0 0", (unsigned int)start);
						debug("*** %s::buffer_cmd #1 '%s'\n", __func__, buffer_cmd);
						if (run_command(buffer_cmd, 0)) {
							free(dst);
							start = 0; // bad BMP
						}
						else {
							displayed = 1;
						}
					}
					else {
						free(dst);
					}
				}
			}
		}

		if (!start)
#endif
		start = (u_int16_t *)_binary_initlogo_bmp_gz_start;
	}

	if (!displayed) {
		sprintf(buffer_cmd, "bmp info 0x%8x", (unsigned int)start);
		debug("*** %s::buffer_cmd #2 = '%s'\n", __func__, buffer_cmd);
		run_command(buffer_cmd, 0);

		sprintf(buffer_cmd, "bmp display 0x%8x 0 0", (unsigned int)start);
		debug("*** %s::buffer_cmd #3 = '%s'\n", __func__, buffer_cmd);
		run_command(buffer_cmd, 0);
	}
}
#else
void render_image(void)
{
	u_int16_t *start;
	u_int16_t *end;
	if (logo_index == LOGO_FASTBOOT) {
		start = (u_int16_t *)_binary_multi_download_rle_start;
		end = (u_int16_t *)_binary_multi_download_rle_end;
	}
	else if (logo_index == LOGO_LOW_BATT) {
		start = (u_int16_t *)_binary_lowbattery_rle_start;
		end = (u_int16_t *)_binary_lowbattery_rle_end;
	}
	else {
		start = (u_int16_t *)_binary_initlogo_rle_start;
		end = (u_int16_t *)_binary_initlogo_rle_end;
	}

	u_int16_t *target_addr = (uint16_t *)omap4_dss_get_frame_buffer();
	if (!target_addr) {
		printf("*** %s:: no framebuffer!\n", __func__);
		return;
	}

	/* Convert the RLE data into RGB565 */
	for (; start != end; start += 2) {
		u_int16_t count = start[0];
		while (count--) {
			*target_addr++ = start[1];
		}
	}
}
#endif

int board_video_init(void)
{
	debug("*** %s\n", __func__);
	lcd_set_flush_dcache(1);
#ifdef CONFIG_CMD_FASTBOOT
	board_mmc_ftbtptn_init(); // init fastboot partition table here
#endif
	initialize_lcd();
	return 0;
}

int do_kc1panel(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	if (argc < 2)
		return CMD_RET_USAGE;

	if (strcmp(argv[1], "on") == 0) {
		initialize_lcd();
	}
	else if (strcmp(argv[1], "off") == 0) {
		turn_off_lcd();
	}
	else if (strcmp(argv[1], "logo_reload") == 0) {
		render_image();
	}
	else if (strcmp(argv[1], "text_grid") == 0) {
		kc1_text_grid();
	}
	else if (strcmp(argv[1], "test_pattern") == 0) {
		kc1_test_pattern();
	}
	else if ((argc == 3) && (strcmp(argv[1], "logo_set_index") == 0)) {
		unsigned long index = simple_strtoul(argv[2], NULL, 16);
		debug("*** %s::logo_index == %lu\n", __func__, index);
		set_logoindex(index);
	}
	else
		return CMD_RET_USAGE;

	return 0;
}

U_BOOT_CMD( kc1panel,	3,	1,	do_kc1panel, NULL,
	"kc1panel [on|off|logo_set_index <index>|logo_reload]"
);


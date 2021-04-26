#include <asm/byteorder.h>
#include <common.h>
#include <command.h>
#include <asm/io.h>

int do_setbacklight(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	u8 brightness = (u8)simple_strtoul(argv[1], NULL, 16);

	/* Pull up GPIO 119 & 120 */
	__raw_writel(0x001B001B, 0x4a100110);
	//GPIO_OE
	__raw_writel(__raw_readl(0x48059134) & ~(0x11 << 23) , 0x48059134);
	//GPIO_DATAOUT
	__raw_writel(__raw_readl(0x4805913C) | (0x11 << 23), 0x4805913C);

	/* cm_l4per_gptimer10_clkctrl to select 32KHz (0) or CM_SYS_CLK=26Mhz (1) as clock source */
	*(int*)0x4A009428 &= ~(0x1 << 24);

	/* cm_l4per_gptimer10_clkctrl clock enable */
	*(int*)0x4A009428 |= 0x2;

	/* Set autoreload mode */
	*(int*)0x48086024 |= 0x2;

	/* Enable prescaler */
	*(int*)0x48086024 |= (0x1 << 5);

	/* GPT10 PWM configuration */
	*(int*)0x48086040 = 0x00000004;   /* TSICR */
	if (brightness == 0xFF) {
		//Set brightness
		*(int*)0x48086038 = 0xFFFFFF00;   /* TMAR */
		*(int*)0x4808602C = 0xFFFFFF00;   /* TLDR */
		*(int*)0x48086030 = 0x00000001;   /* TTGR */
		*(int*)0x48086028 = 0xFFFFFF00;   /* TCRR */
		*(int*)0x48086024 = 0x000018e3;   /* TCLR */
	}
	else {
		//Set brightness
		*(int*)0x48086038 = 0xFFFFFF00 | (brightness & 0xFF);   /* TMAR */
		*(int*)0x4808602C = 0xFFFFFF00;   /* TLDR */
		*(int*)0x48086030 = 0x00000001;   /* TTGR */
		*(int*)0x48086028 = 0xFFFFFF00;   /* TCRR */
		*(int*)0x48086024 = 0x00001863;   /* TCLR */
	}
	return 0;
}

U_BOOT_CMD( setbacklight, 2, 0, do_setbacklight, "set brightness.", NULL );

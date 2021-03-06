#include <common.h>
#include <asm/io.h>
#include <lcd.h>
#include <twl4030.h>
#include <asm/arch/dss.h>
#include <asm/arch/sys_proto.h>

#ifdef CONFIG_LCD
#include "logo.h"

#define TWL_INTBR_PMBR1	0x92
#define GPIODATADIR1	0x9b
#define SETGPIODATAOUT1	0xa4

static const struct panel_config panel_cfg = {
	.timing_h	= 0x0d504300, /* Horizantal timing */
	.timing_v	= 0x02202700, /* Vertical timing */
	.pol_freq	= 0x00007000, /* Pol Freq */
	.divisor	= 0x00010004, /* 36Mhz Pixel Clock */
	.lcd_size	= 0x01df031f, /* 800x480 */
	.panel_type	= 0x01, /* TFT */
	.data_lines	= 0x03, /* 24 Bit RGB */
	.load_mode	= 0x02, /* Frame Mode */
	.panel_color	= 0,
};

/*
 * Hacky DSS/LCD initialization code
 */

static void dss_lcd_init(uint base_addr)
{
	u32 l, div, dpll4;

	l = readl(0x48004d44);
	dpll4 = 26 * ((l >> 8) & 0xfff) / ((l & 0x7f) + 1);
	if (get_cpu_family() == CPU_OMAP36XX)
		dpll4 /= 2;

	/* find and write dpll4 m4 divisor */
	l = dpll4 / 4; /* / panel_cfg.divisor */
	for (div = 2; l / div > 36; div++)
		;

	/* program CM_CLKSEL_DSS m4 divisor */
	l = readl(0x48004e40);
	writel((l & ~0x3f) | div, 0x48004e40);

	omap3_dss_panel_config(&panel_cfg);

	writel(base_addr, 0x48050480);
	writel(base_addr, 0x48050484);
	writel(0x01df031f, 0x4805048c);	/* graphics window size */
	writel(0x0000008d, 0x480504a0);	/* graphics format */

	/* omap3_dss_enable(); */
	l = readl(0x48050440);
	l |= LCD_ENABLE | GO_LCD | GP_OUT0 | GP_OUT1;
	writel(l, 0x48050440);
}

/* SPI stuff to set correct clock polarity in the LCD */
static void lcd_spi_init(void)
{
	/* Enable clock for SPI1 */
	writel(readl(0x48004A00) | (1<<18), 0x48004A00);
	writel(readl(0x48004A10) | (1<<18), 0x48004A10);

	/* Reset module, wait for reset to complete */
	writel(0x00000002, 0x48098010);
	while ( !(readl(0x48098014) & 1) );

	/* SPI1 base address = 0x48098000 for CS0,
	 * for CS1 add 0x14 to the offset where applicable */
	*((volatile uint *) 0x48098034) = 0x00000000; /* CS0 +8 */
	*((volatile uint *) 0x48098048) = 0x00000000; /* CS1 +8 */
	*((volatile uint *) 0x4809801C) = 0x00000000;
	*((volatile uint *) 0x48098018) = 0xFFFFFFFF;
	*((volatile uint *) 0x48098024) = 0x00000000;
	*((volatile uint *) 0x48098028) = 0x00000000;
	*((volatile uint *) 0x48098010) = 0x00000308;
	*((volatile uint *) 0x48098040) = 0x020127DC;
	*((volatile uint *) 0x48098048) = 0x00000001; /* CS1 */
}

static void lcd_spi_write(uint addr, uint data)
{
	data &= 0xff;
	data |= (addr << 10) | (1 << 8);

	while ( !(readl(0x48098044) & (1<<1)) );	/* wait for TXS */

	writel(data, 0x4809804C);

	while ( !(readl(0x48098044) & (1<<1)) );	/* wait for TXS */
	while ( !(readl(0x48098044) & (1<<2)) );	/* wait for EOT */
}

static void lcd_init(void)
{
	DECLARE_GLOBAL_DATA_PTR;
	u8 d;

	/* make sure LCD nreset is driven low (GPIO157)
	 * (we are called before misc_init_r() which normally handles this stuff) */
	writel(0x20000000, 0x49056090);
	writel(readl(0x49056034) & ~0x20000000, 0x49056034);
	/* also GPIO164 (some audible noise otherwise) */
	writel(0x10, 0x49058094);
	writel(readl(0x49058034) & ~0x10, 0x49058034);

	lcd_spi_init();

	/* set VPLL2 to 1.8V */
	twl4030_i2c_write_u8(TWL4030_CHIP_PM_RECEIVER,
			     TWL4030_PM_RECEIVER_VPLL2_DEDICATED, 0x05);
	twl4030_i2c_write_u8(TWL4030_CHIP_PM_RECEIVER, 
			     TWL4030_PM_RECEIVER_VPLL2_DEV_GRP, 0x20);

	/* set VAUX1 to 3.0V (LCD) */
	twl4030_i2c_write_u8(TWL4030_CHIP_PM_RECEIVER, 
			     TWL4030_PM_RECEIVER_VAUX1_DEDICATED, 0x04);
	twl4030_i2c_write_u8(TWL4030_CHIP_PM_RECEIVER, 
			     TWL4030_PM_RECEIVER_VAUX1_DEV_GRP, 0x20);

	/* Clear frame buffer */
	memset((void *)gd->fb_base, 0, 800*480*2);

	writel(0x20000000, 0x49056094); /* Bring LCD out of reset (157) */
	udelay(2000); /* Need to wait at least 1ms after reset to start sending signals */

	dss_lcd_init((uint)gd->fb_base);

	lcd_spi_write(0x02, 0x0f);
	writel(0, 0x48098048); /* Disable SPI1, CS1 */

	/* Set GPIOs on T2 (Turn on LCD BL) */
	twl4030_i2c_read_u8(TWL4030_CHIP_INTBR, TWL_INTBR_PMBR1, &d);
	d &= ~0x0c; /* switch to GPIO function */
	twl4030_i2c_write_u8(TWL4030_CHIP_INTBR, TWL_INTBR_PMBR1, d);

	twl4030_i2c_read_u8(TWL4030_CHIP_GPIO, GPIODATADIR1, &d);
	d |= 0x40; /* GPIO6 */
	twl4030_i2c_write_u8(TWL4030_CHIP_GPIO, GPIODATADIR1, d);
	twl4030_i2c_write_u8(TWL4030_CHIP_GPIO, SETGPIODATAOUT1, 0x40);
}

static void draw_logo(void)
{
	DECLARE_GLOBAL_DATA_PTR;
	unsigned short *dest = (void *)gd->fb_base;
	unsigned short *logo = (unsigned short *)logo_data;
	int i;

	dest += 800 * 480/2 + 800/2;
	dest -= 800 * logo_height/2;
	dest -= logo_width/2;

	for (i = 0; i < logo_height; i++, dest += 800, logo += logo_width)
		memcpy(dest, logo, logo_width * 2);
}

/* u-boot LCD driver support */
vidinfo_t panel_info = {
	800, 480, LCD_BPP
};

void lcd_enable(void)
{
	draw_logo();
}

void lcd_ctrl_init(void *lcdbase)
{
	lcd_init();
}

/* Calculate fb size for VIDEOLFB_ATAG. */
ulong calc_fbsize(void)
{
	return (panel_info.vl_col * panel_info.vl_row *
		NBITS(panel_info.vl_bpix) / 8);
}

#endif /* CONFIG_LCD */

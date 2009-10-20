/*
 * (C) Copyright 2008
 * Grazvydas Ignotas <notasas@gmail.com>
 *
 * Derived from Beagle Board, 3430 SDP, and OMAP3EVM code by
 *	Richard Woodruff <r-woodruff2@ti.com>
 *	Syed Mohammed Khasim <khasim@ti.com>
 *	Sunil Kumar <sunilsaini05@gmail.com>
 *	Shashi Ranjan <shashiranjanmca05@gmail.com>
 *
 * (C) Copyright 2004-2008
 * Texas Instruments, <www.ti.com>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include <common.h>
#include <twl4030.h>
#include <asm/io.h>
#include <asm/arch/mux.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-types.h>
#include <i2c.h>
#include "pandora.h"

/*
 * Hacky DSS/LCD initialization code
 */

static void sendLcdSpiCommand(uint addr, uint data)
{
	data &= 0xff;
	data |= (addr << 10) | (1 << 8);

	while ( !(*((volatile uint *) 0x48098044) & (1<<1)) );	/* wait for TXS */

	*((volatile uint *) 0x4809804C) = data;

	while ( !(*((volatile uint *) 0x48098044) & (1<<1)) );	/* wait for TXS */
	while ( !(*((volatile uint *) 0x48098044) & (1<<2)) );	/* wait for EOT */
}

static void dss_lcd_init(void)
{
	*((volatile uint *) 0x48004D44) = 0x0001b00c; /* DPLL4 multiplier/divider (CM_CLKSEL2_PLL) */
	*((volatile uint *) 0x48004E40) = 0x00001006; /* DSS clock divisors */
	*((volatile uint *) 0x48004D00) = 0x00370037; /* control DPLL3/4 (CM_CLKEN_PLL) */

	*((volatile uint *) 0x48050010) = 0x00000001;
	*((volatile uint *) 0x48050410) = 0x00002015;
	*((volatile uint *) 0x48050444) = 0x00000004;
	*((volatile uint *) 0x48050464) = 0x0d504300; /* horizontal timing */
	*((volatile uint *) 0x48050468) = 0x02202700; /* vertical timing */
	*((volatile uint *) 0x4805046c) = 0x00007028; /* polarities */

	*((volatile uint *) 0x48050470) = 0x00010004;

	*((volatile uint *) 0x4805047c) = 0x01DF031F; /* display size */
	*((volatile uint *) 0x48050478) = 0x00ef027f;
	*((volatile uint *) 0x48050480) = 0x80500000;
	*((volatile uint *) 0x48050484) = 0x80500000;
	*((volatile uint *) 0x4805048c) = 0x01DF031F;
	*((volatile uint *) 0x480504a0) = 0x0000008d;
	*((volatile uint *) 0x480504a4) = 0x03c00200;
	*((volatile uint *) 0x480504b8) = 0x807ff000;
	*((volatile uint *) 0x48050440) = 0x3001836b;
	udelay(1000);
	*((volatile uint *) 0x48050440) = 0x3001836b;
	udelay(1000);
	*((volatile uint *) 0x48050440) = 0x3001836b;
}

// TPO LCD GAMMA VALUES
static const int lcd_gamma_table[12] = {
	106, 200, 289, 375, 460, 543, 625, 705, 785, 864, 942, 1020
};

static void lcd_init(void)
{
	const int *g = lcd_gamma_table;
	unsigned char byte;
	uint i, val;

	/* Enable clock for SPI1 */
	val = *((volatile uint *) 0x48004A00);
	val |= (1<<18);
	*((volatile uint *) 0x48004A00) = val;
	val = *((volatile uint *) 0x48004A10);
	val |= (1<<18);
	*((volatile uint *) 0x48004A10) = val;

	/* Reset module, wait for reset to complete */
	*((volatile uint *) 0x48098010) = 0x00000002;
	while ( !(*((volatile uint *) 0x48098014) & 1) );

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

	/* Set GPIOs on T2 (Turn on LCD BL) */
	byte = 0xC0;
	i2c_write(0x49, 0x9B, 1, &byte, 1);
	byte = 0xC0;
	i2c_write(0x49, 0x9E, 1, &byte, 1);

	/* VAUX1 = 3.0V    (LCD) */
	byte = 0x20;
	i2c_write(0x4B, 0x72, 1, &byte, 1);
	byte = 0x04;
	i2c_write(0x4B, 0x75, 1, &byte, 1);

	/* Clear frame buffer */
	memset((void *)0x80500000, 0, 800*480*2);

	udelay(11000);
	*((volatile uint *) 0x49056094) = 0x20000000; /* Bring LCD out of reset (157) */
	udelay(2000); /* Need to wait at least 1ms after reset to start sending signals */

	dss_lcd_init();

	/* setup gamma */
	val = 0;
	for (i = 0; i < 4; i++)
		val |= (g[i] & 0x300) >> ((i + 1) * 2);
	sendLcdSpiCommand(0x11, val);

	val = 0;
	for (i = 0; i < 4; i++)
		val |= (g[i+4] & 0x300) >> ((i + 1) * 2);
	sendLcdSpiCommand(0x12, val);

	val = 0;
	for (i = 0; i < 4; i++)
		val |= (g[i+8] & 0x300) >> ((i + 1) * 2);
	sendLcdSpiCommand(0x13, val);

	for (i = 0; i < 12; i++)
		sendLcdSpiCommand(0x14 + i, g[i] & 0xff);

	/* other stuff */
	sendLcdSpiCommand(0x02, 0x0f);
	sendLcdSpiCommand(0x03, 0xdf);
	sendLcdSpiCommand(0x04, 0x17);
	sendLcdSpiCommand(0x20, 0xf0);
	sendLcdSpiCommand(0x21, 0xf0);

	*((volatile uint *) 0x48098048) = 0x00000000; /* Disable SPI1, CS1 */
}

/*
 * Routine: board_init
 * Description: Early hardware init.
 */
int board_init(void)
{
	DECLARE_GLOBAL_DATA_PTR;

	gpmc_init(); /* in SRAM or SDRAM, finish GPMC */
	/* board id for Linux */
	gd->bd->bi_arch_number = MACH_TYPE_OMAP3_PANDORA;
	/* boot param addr */
	gd->bd->bi_boot_params = (OMAP34XX_SDRC_CS0 + 0x100);

	return 0;
}

/*
 * Routine: misc_init_r
 * Description: Configure board specific parts
 */
int misc_init_r(void)
{
	struct gpio *gpio1_base = (struct gpio *)OMAP34XX_GPIO1_BASE;
	struct gpio *gpio4_base = (struct gpio *)OMAP34XX_GPIO4_BASE;
	struct gpio *gpio5_base = (struct gpio *)OMAP34XX_GPIO5_BASE;
	struct gpio *gpio6_base = (struct gpio *)OMAP34XX_GPIO6_BASE;
	unsigned char byte;

	twl4030_power_init();
	twl4030_led_init();

	/* Configure GPIOs to output */
	writel(~(GPIO14 | GPIO15 | GPIO16 | GPIO23), &gpio1_base->oe);
	writel(~GPIO22, &gpio4_base->oe);	/* 118 */
	writel(~(GPIO0 | GPIO1 | GPIO28 | GPIO29 | GPIO30 | GPIO31),
		&gpio5_base->oe);	/* 128, 129, 156-159 */
	writel(~GPIO4, &gpio6_base->oe);	/* 164 */

	/* Set GPIOs */
	writel(GPIO28, &gpio5_base->setdataout);
	writel(GPIO4, &gpio6_base->setdataout);

	/* Enable battery backup capacitor */
	byte = 0x1E;	/* 3.2V, 0.5mA charge current */
	i2c_write(0x4B, 0x6D, 1, &byte, 1);

	dieid_num_r();
	lcd_init();

	/* this stuff should move to kernel. */
	/* set vaux4 to 2.8V (TOUCH, NUBS) */
	byte = 0x0A;
	i2c_write(0x4B, 0x81, 1, &byte, 1);
	byte = 0x20;
	i2c_write(0x4B, 0x7E, 1, &byte, 1);

	/* set vsim to 2.8V (AUDIO DAC external) */
	byte = 0x04;
	i2c_write(0x4B, 0x95, 1, &byte, 1);
	byte = 0x20;
	i2c_write(0x4B, 0x92, 1, &byte, 1);

	/* set vaux2 to 1.8V (USB HOST PHY power) */
	byte = 0x05;
	i2c_write(0x4B, 0x79, 1, &byte, 1);
	byte = 0x20;
	i2c_write(0x4B, 0x76, 1, &byte, 1);

	return 0;
}

/*
 * Routine: set_muxconf_regs
 * Description: Setting up the configuration Mux registers specific to the
 *		hardware. Many pins need to be moved from protect to primary
 *		mode.
 */
void set_muxconf_regs(void)
{
	MUX_PANDORA();
}

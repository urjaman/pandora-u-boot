#include <asm/arch/cpu.h>
#include <asm/arch/clock.h>


#include <common.h>
#include <asm/io.h>
#include <asm/arch/gpio.h>
#include <lcd.h>
#include <hush.h>
#include <malloc.h>
#include <twl4030.h>

#include "pandora.h"

#define MBCHG	 0x40
#define RTC_IT	 0x08
#define USB_PRES 0x04
#define CHG_PRES 0x02
#define PWR_BTN  0x01

static int do_pwrtest(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	int err=0;
	u8 imr=0,isr=0;
	int bus = I2C_GET_BUS();
	I2C_SET_BUS(TWL4030_I2C_BUS);

	if ((err=twl4030_i2c_read_u8(TWL4030_CHIP_INT,TWL4030_BASEADD_INT+0,&isr))) goto error;
	if ((err=twl4030_i2c_read_u8(TWL4030_CHIP_INT,TWL4030_BASEADD_INT+1,&imr))) goto error;
	printf("IMR=%02x ISR=%02x\n",imr,isr);
	printf("Masked:");
	if (imr&MBCHG) printf(" MBCHG");
	if (imr&RTC_IT) printf(" RTC_IT");
	if (imr&USB_PRES) printf(" USB_PRES");
	if (imr&CHG_PRES) printf(" CHG_PRES");
	if (imr&PWR_BTN) printf(" PWRON");
	printf("\n");

	printf("Interrupt:");
	if (isr&MBCHG) printf(" MBCHG");
	if (isr&RTC_IT) printf(" RTC_IT");
	if (isr&USB_PRES) printf(" USB_PRES");
	if (isr&CHG_PRES) printf(" CHG_PRES");
	if (!(isr&PWR_BTN)) printf(" PWRON");
	printf("\n");
	printf("Clearing ISR (USB & CHG)\n");
	if ((err=twl4030_i2c_write_u8(TWL4030_CHIP_INT,TWL4030_BASEADD_INT+0,isr&0xF9))) goto error;
	I2C_SET_BUS(bus);
	return 0;
error:
	printf("i2c error %d\n",err);
	I2C_SET_BUS(bus);
	return 1;
}

U_BOOT_CMD(
	pwronrsn, 1, 0, do_pwrtest,
	"print regs relating to poweron reason",
	""
);

#define BQ2750X_I2C_BUS 2
#define BQ_I2C_ADDR 0x55
#define BQ_TEMP 0x06
#define BQ_VOLT 0x08
#define BQ_AI	0x14

static int bq2750x_i2c_read_u16(u8 reg, u16* val) {
	return i2c_read(BQ_I2C_ADDR, reg, 1, (uchar*)val, 2);
}

static int do_bq2750x_v(void)
{
	int err;
	u16 curr,volt,temp;
	signed short currf;
	int bus = I2C_GET_BUS();
	I2C_SET_BUS(BQ2750X_I2C_BUS);
	if ((err=bq2750x_i2c_read_u16(BQ_TEMP,&temp))) goto error;
	if ((err=bq2750x_i2c_read_u16(BQ_VOLT,&volt))) goto error;
	if ((err=bq2750x_i2c_read_u16(BQ_AI,&curr))) goto error;
	currf = curr;
	printf("BQ read TEMP 0x%04X VOLT 0x%04X AI 0x%04X\n",
		temp,volt,curr);
	printf("Temp: %d K\n",temp/10);
	printf("Volt: %d mV\n",volt);
	printf("Curr: %d mA\n",currf);
	I2C_SET_BUS(bus);
	return 0;

error:
	printf("i2c error %d\n",err);	
	I2C_SET_BUS(bus);
	return 1;
}

static int do_bq2750x(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	return do_bq2750x_v();
}

U_BOOT_CMD(
	bqprint, 1, 0, do_bq2750x,
	"print bq2750x power info",
	""
);

static u16 old_lid = 0;
static int pandora_lid_poll(int *ls)
{
	struct gpio *gpio4_base = (struct gpio *)OMAP34XX_GPIO4_BASE;
	u16 lid = readl(&gpio4_base->datain) & GPIO12;
	/* Change? */
	u16 dl = (old_lid ^ lid);
	/* De-bounce */
	if (dl) udelay(5000);
	old_lid = lid;
	if (ls) *ls = lid?1:0;
	return dl?1:0;
}


static int do_lidtest(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	/* Set VSIM to 2.8V for lid sensor (also DAC) */
	twl4030_pmrecv_vsel_cfg(TWL4030_PM_RECEIVER_VSIM_DEDICATED,
				TWL4030_PM_RECEIVER_VSIM_VSEL_28,
				TWL4030_PM_RECEIVER_VSIM_DEV_GRP,
				TWL4030_PM_RECEIVER_DEV_GRP_P1);
	udelay(5000);

	int lid;
	pandora_lid_poll(&lid);
	printf("lid %d\n",lid);
	while (!tstc()) {
		if (pandora_lid_poll(&lid)) {
			printf("lid %d\n",lid);
		}
		udelay(5000);
	}
	return 0;
}

U_BOOT_CMD(
	lidtest, 1, 0, do_lidtest,
	"print lid open/close info",
	""
);


#define BCIMDEN 0x00
#define BCIMSTATEC 0x02
#define BCIVBAT 0x04
#define BCITBAT 0x06
#define BCIICHG 0x08
#define BCIVAC 0x0A
#define BCIVBUS 0x0C

static int twl_bci_i2c_read_u16(u8 reg, u16 *val) {
	return i2c_read(TWL4030_CHIP_MAIN_CHARGE, TWL4030_BASEADD_MAIN_CHARGE+reg, 1, (uchar*)val, 2);
}

static int do_bciinfo(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	u8 bcimden,mstatec;
	int err=0;
	int bus = I2C_GET_BUS();
	I2C_SET_BUS(TWL4030_I2C_BUS);
	if ((err=twl4030_i2c_read_u8(TWL4030_CHIP_MAIN_CHARGE,TWL4030_BASEADD_MAIN_CHARGE+BCIMDEN,&bcimden))) goto error;
	if ((err=twl4030_i2c_read_u8(TWL4030_CHIP_MAIN_CHARGE,TWL4030_BASEADD_MAIN_CHARGE+BCIMSTATEC,&mstatec))) goto error;
	printf("BCI BCIMDEN %02X MSTATEC %02X\n",bcimden,mstatec);
	const char * s;
	switch (mstatec&0x3F) {
		default: s = "<wtf>"; break;
		case 0: s = "No charging device"; break;
		case 1: s = "Off mode"; break;
		case 2: s = "Standby mode"; break;
		case 3: s = "Open battery mode ..."; break;
		case 0x21: s = "CV mode (AC)"; break;
		case 0x22: s = "Quick charge ac 1"; break;
		case 0x23: s = "Quick charge ac 2"; break;
		case 0x24: s = "Quick charge ac 3"; break;
		case 0x25: s = "Quick charge ac 4"; break;
		case 0x26: s = "Quick charge ac 5"; break;
		case 0x27: s = "Quick charge ac 6"; break;
		case 0x28: s = "Charge stop ac 1"; break;
		case 0x29: s = "Charge stop ac 2"; break;
		case 0x2A: s = "Charge stop ac 3"; break;
		case 0x2B: s = "Charge ac comp 1"; break;
		case 0x2C: s = "Charge ac comp 2"; break;
		case 0x2D: s = "Charge ac comp 3"; break;
		case 0x2E: s = "Charge ac comp 4"; break;
		case 0x2F: s = "AC adapter overvolt"; break;
		case 0x12: s = "Quick charge usb 1"; break;
		case 0x13: s = "Quick charge usb 2"; break;
		case 0x14: s = "Quick charge usb 3"; break;
		case 0x15: s = "Quick charge usb 4"; break;
		case 0x16: s = "Quick charge usb 5"; break;
		case 0x17: s = "Quick charge usb 6"; break;
		case 0x18: s = "Charge stop usb 1"; break;
		case 0x19: s = "Charge stop usb 2"; break;
		case 0x1A: s = "Charge stop usb 3"; break;
		case 0x1B: s = "Charge usb comp 1"; break;
		case 0x1C: s = "Charge usb comp 2"; break;
		case 0x1D: s = "Charge usb comp 3"; break;
		case 0x1E: s = "Charge usb comp 4"; break;
		case 0x1F: s = "USB adapter overvolt"; break;
	}
	printf("mstatec = %s\n",s);
	u16 vbat,tbat,ichg,vac,vbus;
	if ((err=twl_bci_i2c_read_u16(BCIVBAT,&vbat))) goto error;
	if ((err=twl_bci_i2c_read_u16(BCITBAT,&tbat))) goto error;
	if ((err=twl_bci_i2c_read_u16(BCIICHG,&ichg))) goto error;
	if ((err=twl_bci_i2c_read_u16(BCIVAC,&vac))) goto error;
	if ((err=twl_bci_i2c_read_u16(BCIVBUS,&vbus))) goto error;
	printf("VBAT=%04X TBAT=%04X ICHG=%04X VAC=%04X VBUS=%04X\n",
		vbat,tbat,ichg,vac,vbus);

	I2C_SET_BUS(bus);
	return 0;
error:
	printf("i2c error %d\n",err);
	I2C_SET_BUS(bus);
	return 1;
}

U_BOOT_CMD(
	bciinfo, 1, 0, do_bciinfo,
	"twl4030 bci info",
	""
);



#define GPBR1 0x0c
#define PMBR1 0x0d
#define GPBR1_PWM1_ENABLE 0x08
#define GPBR1_PWM1_CLK_ENABLE 0x02
#define PWMON	0
#define PWMOFF	1

static int twl4030_clear_set(u8 mod_no, u8 clear, u8 set, u8 reg)
{
	u8 val = 0;
	int ret;
	ret = twl4030_i2c_read_u8(mod_no,reg,&val);
	if (ret)
		return ret;
	val &= ~clear;
	val |= set;
	return twl4030_i2c_write_u8(mod_no,reg,val);
}

static int twl4030_set_chgled(u8 val)
{
	int err=0;
	int bus = I2C_GET_BUS();
	I2C_SET_BUS(TWL4030_I2C_BUS);
	if (val>127) val=127;

	if (!val) { /* off */
		u8 r;
		if ((err=twl4030_i2c_read_u8(TWL4030_CHIP_INTBR,TWL4030_BASEADD_INTBR+GPBR1,&r))) goto error;
		r &= ~GPBR1_PWM1_ENABLE;
		if ((err=twl4030_i2c_write_u8(TWL4030_CHIP_INTBR,TWL4030_BASEADD_INTBR+GPBR1,r))) goto error;
		r &= ~GPBR1_PWM1_CLK_ENABLE;
		if ((err=twl4030_i2c_write_u8(TWL4030_CHIP_INTBR,TWL4030_BASEADD_INTBR+GPBR1,r))) goto error;
	} else {
		u8 r;
		/* Init sequence */
		if ((err=twl4030_clear_set(TWL4030_CHIP_INTBR, 0, 0x30, TWL4030_BASEADD_INTBR+PMBR1))) goto error;
		if ((err=twl4030_clear_set(TWL4030_CHIP_INTBR, 0, GPBR1_PWM1_CLK_ENABLE, TWL4030_BASEADD_INTBR+GPBR1))) goto error;
		if ((err=twl4030_i2c_write_u8(TWL4030_CHIP_PWM1,TWL4030_BASEADD_PWM1+PWMON,0))) goto error;
		/* PWM change */
		if ((err=twl4030_i2c_write_u8(TWL4030_CHIP_PWM1,TWL4030_BASEADD_PWM1+PWMOFF,val))) goto error;
		/* enable */
		if ((err=twl4030_i2c_read_u8(TWL4030_CHIP_INTBR,TWL4030_BASEADD_INTBR+GPBR1,&r))) goto error;
		r &= ~GPBR1_PWM1_ENABLE;
		r |= GPBR1_PWM1_CLK_ENABLE;
		if ((err=twl4030_i2c_write_u8(TWL4030_CHIP_INTBR,TWL4030_BASEADD_INTBR+GPBR1,r))) goto error;
		r |= GPBR1_PWM1_ENABLE;
		if ((err=twl4030_i2c_write_u8(TWL4030_CHIP_INTBR,TWL4030_BASEADD_INTBR+GPBR1,r))) goto error;
	}
	I2C_SET_BUS(bus);
	return 0;
error:
	printf("i2c error %d\n",err);
	I2C_SET_BUS(bus);
	return err;
}

static int do_chgled(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	if (argc < 2) return -1; /* fixme: usage */
	u8 val = simple_strtoul(argv[1], NULL, 0);
	printf("CHGLED:%d\n",val);
	int err = twl4030_set_chgled(val);
	if (err) return 1;
	return 0;
}

U_BOOT_CMD(
	chgled, 2, 0, do_chgled,
	"set pandora pwm charger led",
	""
);

#define RGB(r,g,b) ( (((r)<<8)&0xF800) | (((g)<<3)&0x07E0) | (((b)>>3)&0x001F) )

static void lcd_fillrect(u16 color, int x, int y, int w, int h)
{
	DECLARE_GLOBAL_DATA_PTR;
	unsigned short *fb = (void *)gd->fb_base;
	int c,r;
	for (r=y;r<(y+h);r++) {
		for (c=x;c<(x+w);c++) {
			fb[r*800+c] = color;
		}
	}
}

static void lcd_drawrect(u16 color, int x, int y, int w, int h)
{
	DECLARE_GLOBAL_DATA_PTR;
	unsigned short *fb = (void *)gd->fb_base;
	int c,r;
	/* Top Line */
	for (c=x;c<(x+w);c++) {
		fb[y*800+c] = color;
	}
	/* Bottom Line */
	for (c=x;c<(x+w);c++) {
		fb[(y+h-1)*800+c] = color;
	}
	/* Left Line */
	for (r=y;r<(y+h);r++) {
		fb[r*800+x] = color;
	}
	/* Right Line */
	for (r=y;r<(y+h);r++) {
		fb[r*800+(x+w-1)] = color;
	}
}

static void lcdsync(void)
{
	/* This'll work for testing. */
	flush_dcache_all();
}


static int do_gfxtest(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	lcd_fillrect(0xFFFF, 400,20,300,50);
	lcd_fillrect(RGB(255,0,0), 400,70,300,50);
	lcd_fillrect(RGB(0,255,0), 400,120,300,50);
	lcd_fillrect(RGB(0,0,255), 400,170,300,50);
	lcd_drawrect(RGB(0,255,255), 400,220,300,50);
	lcdsync();
	return 0;
}

U_BOOT_CMD(
	gfxtest, 1, 0, do_gfxtest,
	"draw something stupid",
	""
);


#define TWL_INTBR_PMBR1	0x92
#define GPIODATADIR1	0x9b
#define SETGPIODATAOUT1	0xa4
#define CLEARGPIODATAOUT1 0xa1

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

static void lcd_re_init(void)
{
	u8 d;

	/* make sure LCD nreset is driven low (GPIO157)
	 * (we are called before misc_init_r() which normally handles this stuff) */
	writel(0x20000000, 0x49056090);
	writel(readl(0x49056034) & ~0x20000000, 0x49056034);
	/* also GPIO164 (some audible noise otherwise) */
	writel(0x10, 0x49058094);
	writel(readl(0x49058034) & ~0x10, 0x49058034);

	lcd_spi_init();

	/* set VAUX1 to 3.0V (LCD) */
	twl4030_i2c_write_u8(TWL4030_CHIP_PM_RECEIVER, 
			     TWL4030_PM_RECEIVER_VAUX1_DEDICATED, 0x04);
	twl4030_i2c_write_u8(TWL4030_CHIP_PM_RECEIVER, 
			     TWL4030_PM_RECEIVER_VAUX1_DEV_GRP, 0x20);

	writel(0x20000000, 0x49056094); /* Bring LCD out of reset (157) */
	udelay(2000); /* Need to wait at least 1ms after reset to start sending signals */

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

static void lcd_turn_off(void)
{
	/* BL off */
	twl4030_i2c_write_u8(TWL4030_CHIP_GPIO, CLEARGPIODATAOUT1, 0x40);
	/* LCD power off */
	twl4030_i2c_write_u8(TWL4030_CHIP_PM_RECEIVER, 
			     TWL4030_PM_RECEIVER_VAUX1_DEV_GRP, 0x00);

}

static int do_lcdptest(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	int i;
	printf("Turning LCD off\n");
	lcd_turn_off();	
	for (i=0;i<1000;i++) udelay(2000);
	do_bq2750x_v();
	lcd_re_init();
	printf("LCD re-initialized\n");
	return 0;
}	


U_BOOT_CMD(
	lcdptest, 1, 0, do_lcdptest,
	"test lcd poweroff",
	""
);


void cpu_v7_do_idle(void);

static void intc_init_clear(void) {	
	/* Reset module, wait for reset to complete */
	writel(0x00000002, 0x48200010);
	while ( !(readl(0x48200014) & 1) );

	/* Save power */
	writel(0x00000001, 0x48200010); /* Autoidle */
	writel(0x00000002, 0x48200050); /* Clock autogating */
	/* Also for the modem INTC */
	writel(0x00000001, 0x480C7010); /* Autoidle */
	writel(0x00000002, 0x480C7050); /* Clock autogating */
}
	
static void intc_unmask_irq(int irq)
{
	u32 addr_n = (irq/32);
	u32 mask = 1<<(irq%32);
	/* INTCPS_ILRm */
	writel(0x10, 0x48200100 + (0x04*irq));
	/* INTCPS_MIR_CLEARn */
	writel(mask, 0x48200088 + (0x20 * addr_n));
}

static struct gptimer *timer_base = (struct gptimer *)CONFIG_SYS_TIMERBASE;
#define TIMER_CLOCK		(V_SCLK / (2 << CONFIG_SYS_PTV))

static void intc_gpt_init(void) {
	static int intc_inited = 0;
	if (!intc_inited) {
		printf("INTC init ");
		intc_init_clear();
		intc_unmask_irq(38); /* GPT2 */
		/* NOTE: We unmask only to have WFI wakeup source,
		we never actually enable interrupts in the CPU. */
		printf("done\n");
		intc_inited = 1;
	}
}

static void sleep_ticks(u32 ticks) {
	intc_gpt_init();	
	u32 incr = ticks * (TIMER_CLOCK / CONFIG_SYS_HZ);
	volatile u32 foo = timer_base->tisr; /*Clear match int flag*/
	timer_base->tisr = foo;
	timer_base->tmar = timer_base->tcrr + incr;
	timer_base->tclr |= 0x40; /* CE */
	timer_base->tier |= 0x01; /* Enable match int. */
	/* Hit the hay. */
	cpu_v7_do_idle();
	/* Woah, we woke up... */
	foo = timer_base->tisr; /*Clear match int flag*/
	timer_base->tisr = foo;
	/* Clear/Ack the INTC interrupt */
	writel(0x1, 0x48200048);
	printf("Wakeup TISR %08X\n",foo);
}

static int do_tsleep(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	if (argc < 2) return -1; /* fixme: usage */
	u16 val = simple_strtoul(argv[1], NULL, 0);
	printf("Sleeping %d ticks with wfi\n", val);
	sleep_ticks(val);
	printf("done\n");
	return 0;
}

U_BOOT_CMD(
	tsleep, 2, 0, do_tsleep,
	"try to truly sleep (with wfi) n ticks",
	""
);

#define F_RSTST 1
#define F_WKEN 2
#define F_WKST 4
#define F_WKDEP 8
#define F_PWSTCTRL 16
#define F_PWSTST 32
#define F_MPUGRPSEL 64

static void dump_prm(u32 base, int flags, const char* name)
{
	printf("%s:", name);
	if (flags&F_RSTST) printf("RSTST %04X ",readl(base+0x58));
	if (flags&F_WKEN) printf("WKEN %08X ",readl(base+0xA0));
	if (flags&F_MPUGRPSEL) printf("MPUGRPSEL %08X ",readl(base+0xA4));
	if (flags&F_WKST) printf("WKST %08X ",readl(base+0xB0));
	if (flags&F_WKDEP) printf("WKDEP %08X ",readl(base+0xC8));
	if (flags&F_PWSTCTRL) printf("PWSTCTRL %08X ",readl(base+0xE0));
	if (flags&F_PWSTST) printf("PWSTST %08X ",readl(base+0xE4));
	printf("\n");
}


static int do_prmdump(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	printf("PRM Dump\n");
	dump_prm(0x48306000,F_RSTST|F_WKDEP|F_PWSTCTRL|F_PWSTST,"IVA2");
	dump_prm(0x48306900,F_RSTST|F_WKDEP|F_PWSTCTRL|F_PWSTST,"MPU");
	dump_prm(0x48306A00,F_RSTST|F_WKEN|F_MPUGRPSEL|F_PWSTCTRL|F_PWSTST,"CORE");
	dump_prm(0x48306B00,F_RSTST|F_WKDEP|F_PWSTCTRL|F_PWSTST,"SGX");
	dump_prm(0x48306C00,F_WKEN|F_MPUGRPSEL|F_WKST,"WKUP");
	dump_prm(0x48306E00,F_RSTST|F_WKEN|F_WKDEP|F_PWSTCTRL|F_PWSTST ,"DSS");
	dump_prm(0x48306F00,F_RSTST|F_WKDEP|F_PWSTCTRL|F_PWSTST,"CAM");
	dump_prm(0x48307000,F_RSTST|F_WKEN|F_MPUGRPSEL|F_WKST|F_WKDEP|F_PWSTCTRL|F_PWSTST,"PER");
	dump_prm(0x48307100,F_RSTST|F_PWSTST,"EMU");
	dump_prm(0x48307300,F_RSTST|F_WKDEP|F_PWSTCTRL|F_PWSTST,"NEON");
	dump_prm(0x48307400,F_RSTST|F_WKEN|F_MPUGRPSEL|F_WKST|F_WKDEP|F_PWSTCTRL|F_PWSTST,"USBH");
	return 0;
}


U_BOOT_CMD(
	prmdump, 1, 0, do_prmdump,
	"prm dump",
	""
);


static void bl_off(void) {
	twl4030_i2c_write_u8(TWL4030_CHIP_GPIO, CLEARGPIODATAOUT1, 0x40);
}

static void bl_on(void) {
	twl4030_i2c_write_u8(TWL4030_CHIP_GPIO, SETGPIODATAOUT1, 0x40);
}


static void bl_escale_tx(u8 txbyte) {
	int i;
	for (i=0;i<8;i++) {
		if (txbyte&0x80) {
			/* TX a 1 */
			bl_off();
			bl_on();
			bl_on();
			bl_on();
		} else {
			/* TX a 0. */
			bl_off();
			bl_off();
			bl_off();
			bl_on();
		}
		txbyte = txbyte<<1;
	}
	/* Generate tEOS. */
	bl_off();
	bl_on();
}

static void bl_easyscale(u8 v) {
	int bus = I2C_GET_BUS();
	I2C_SET_BUS(TWL4030_I2C_BUS);
	int speed = i2c_get_bus_speed();
	i2c_set_bus_speed(400000); /* We need the speed, the need for speed. */
	/* Bit write delay is now roughly 68us. */
	if (v>31) v=31;
	
	/* BL off */
	bl_off();
	udelay(3000); /* Wait the minimum time to shutdown the TPS. */
	/* Generate EasyScale init sequence. */
	bl_on();
	bl_on();
	/* 52 us */
	bl_off();
	bl_off();
	bl_off();
	bl_off();
	bl_off();
	bl_on();
	/* 290us > 260us. Also total 290+120us (410) < 1000us. */

	bl_escale_tx(0x72);
	bl_escale_tx(0x40|(v&31));

	i2c_set_bus_speed(speed);
	I2C_SET_BUS(bus);
}

static int do_escale(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	if (argc < 2) return -1; /* fixme: usage */
	u8 val = simple_strtoul(argv[1], NULL, 0);
	printf("Setting brightness to %d\n", val);
	bl_easyscale(val);
	printf("done\n");
	return 0;
}

U_BOOT_CMD(
	escale, 2, 0, do_escale,
	"try to set bl brightness w EasyScale",
	""
);

#include <common.h>
#include <asm/io.h>
#include <asm/arch/gpio.h>
#include <hush.h>
#include <malloc.h>
#include <lcd.h>
#include <twl4030.h>

/* game buttons as in GPIO bank 4 */
#define BTN_R		GPIO9
#define BTN_UP		GPIO14
#define BTN_DOWN	GPIO7
#define BTN_G2		GPIO15
#define BTN_G3		GPIO10

struct menu_item {
	const char *name;
	int (*handler)(struct menu_item *item);
	char *cmd;
};

static struct menu_item *menu_items[24];
static int menu_item_count;

static int do_cmd(const char *fmt, ...)
{
	char cmdbuff[256];
	va_list args;

	va_start(args, fmt);
	vsprintf(cmdbuff, fmt, args);
	va_end(args);

	return !parse_string_outer(cmdbuff,
		FLAG_PARSE_SEMICOLON | FLAG_EXIT_FROM_LOOP);
}

static u32 menu_wait_for_input(int down)
{
	struct gpio *gpio4_base = (struct gpio *)OMAP34XX_GPIO4_BASE;
	u32 btns;

	while (1) {
		btns = ~readl(&gpio4_base->datain) &
			(BTN_UP|BTN_DOWN|BTN_G2|BTN_G3);
		if (!btns == !down)
			break;
		udelay(5000);
	}

	return btns;
}

static int menu_do_default(struct menu_item *item)
{
	return 1;
}

static int menu_do_poweroff(struct menu_item *item)
{
	u8 d;

	printf("power off.\n");

	twl4030_i2c_read_u8(TWL4030_CHIP_PM_MASTER, TWL4030_PM_MASTER_P1_SW_EVENTS, &d);
	d |= TWL4030_PM_MASTER_SW_EVENTS_DEVOFF;
	twl4030_i2c_write_u8(TWL4030_CHIP_PM_MASTER, TWL4030_PM_MASTER_P1_SW_EVENTS, d);

	return 0;
}

static int menu_do_usb_serial(struct menu_item *item)
{
	do_cmd("dcache off"); /* doesn't work with dcache */
	do_cmd("usbinit");
	printf("Switched to USB serial.\n");

	setenv("stdout", "usbtty");
	setenv("stdin", "usbtty");
	setenv("stderr", "usbtty");
	setenv("bootcmd", "");
	return 1;
}

static int menu_do_serial(struct menu_item *item)
{
	printf("Switched to serial.\n");

	setenv("stdout", "serial");
	setenv("bootcmd", "");
	return 1;
}

static int menu_do_console(struct menu_item *item)
{
	lcd_set_flush_dcache(1);
	printf("Switched to console.\n");
	setenv("stdout", "lcd");
	setenv("stderr", "lcd");
	setenv("stdin",  "kbd");
	setenv("bootcmd", "");
	return 1;
}

static int menu_do_script_cmd(struct menu_item *item)
{
	int failed = 0;

	if (item->cmd == NULL || !do_cmd(item->cmd))
		failed = 1;

	printf("script %s.\n", failed ? "failed" : "finished");
	flush_dcache_all();
	menu_wait_for_input(0);
	menu_wait_for_input(1);
	return 0;
}

static void add_menu_item(const char *name,
	int (*handler)(struct menu_item *), const char *cmd)
{
	struct menu_item *mitem;

	mitem = malloc(sizeof(*mitem));
	if (mitem == NULL)
		return;
	mitem->name = strdup(name);
	mitem->handler = handler;
	mitem->cmd = strdup(cmd);

	if (menu_item_count < ARRAY_SIZE(menu_items))
		menu_items[menu_item_count++] = mitem;
}

static char *bootmenu_next_ctl(char *p)
{
	while (*p && *p != '|' && *p != '\r' && *p != '\n')
		p++;

	return p;
}

static char *bootmenu_skip_blanks(char *p)
{
	while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
		p++;

	return p;
}

static char *bootmenu_skip_line(char *p)
{
	while (*p && *p != '\r' && *p != '\n')
		p++;

	return p;
}

static void parse_bootmenu(char *buf)
{
	char *p = buf, *name, *cmd;
	int i;

	for (i = 1; ; i++)
	{
		p = bootmenu_skip_blanks(p);
		if (*p == 0)
			break;
		if (*p == '#') {
			p = bootmenu_skip_line(p);
			continue;
		}

		name = p;
		p = bootmenu_next_ctl(p);
		if (*p != '|') {
			printf("bootmenu.txt: invalid line ~%d\n", i);
			p = bootmenu_skip_line(p);
			continue;
		}
		*p++ = 0;

		cmd = p;
		p = bootmenu_skip_line(p);
		if (*p != 0)
			*p++ = 0;

		add_menu_item(name, menu_do_script_cmd, cmd);
	}
}

static struct menu_item default_menu_items[] = {
	{ "default boot",	menu_do_default, },
	{ "power off",		menu_do_poweroff, },
	{ "USB serial prompt",	menu_do_usb_serial, },
	{ "serial prompt",	menu_do_serial, },
	{ "console prompt",	menu_do_console, },
};

static void menu_init(void)
{
	const char *check_format1 = "%sload mmc1 0:%d ${loadaddr} boot.scr 4";
	const char *check_format2 = "%sload mmc1 0:%d ${loadaddr} boot.txt 4";
	const char *run_format1 = "%sload mmc1 0:%d ${loadaddr} boot.scr;source ${loadaddr}";
	const char *run_format2 = "%sload mmc1 0:%d ${loadaddr} boot.txt;ssource ${loadaddr} ${filesize}";
	disk_partition_t part_info;
	block_dev_desc_t *dev_desc;
	char tmp_name[32], tmp_cmd[128];
	int i;

	for (i = 0; i < 2; i++)
		menu_items[i] = &default_menu_items[i];
	menu_item_count = i;

	if (!do_cmd("mmc rescan"))
		goto no_mmc;

	dev_desc = get_dev("mmc1", 0);
	if (dev_desc == NULL) {
		printf("dev desc null\n");
		goto no_mmc;
	}

	/* kill stdout while we search for bootfiles */
	setenv("stdout", "nulldev");

	for (i = 1; menu_item_count < ARRAY_SIZE(menu_items); i++) {
		if (get_partition_info(dev_desc, i, &part_info))
			break;
		if (do_cmd("fatls mmc1 0:%d", i)) {
			if (do_cmd(check_format1, "fat", i)) {
				sprintf(tmp_cmd, run_format1, "fat", i);
				goto found;
			}
			if (do_cmd(check_format2, "fat", i)) {
				sprintf(tmp_cmd, run_format2, "fat", i);
				goto found;
			}
			continue;
		}
		if (do_cmd("ext2ls mmc1 0:%d", i)) {
			if (do_cmd(check_format1, "ext2", i)) {
				sprintf(tmp_cmd, run_format1, "ext2", i);
				goto found;
			}
			if (do_cmd(check_format2, "ext2", i)) {
				sprintf(tmp_cmd, run_format2, "ext2", i);
				goto found;
			}
			continue;
		}
		continue;

found:
		sprintf(tmp_name, "boot from SD1:%d", i);
		add_menu_item(tmp_name, menu_do_script_cmd, tmp_cmd);
	}

no_mmc:
	setenv("stdout", "serial");

	if (do_cmd("ubi part boot && ubifsmount ubi0:boot")) {
		ulong addr = getenv_ulong("loadaddr", 16, 0);
		if ((int)addr < (int)0x90000000) {
			if (do_cmd("ubifsload ${loadaddr} bootmenu.txt")) {
				ulong size = getenv_ulong("filesize", 16, 0);
				*(char *)(addr + size) = 0;
				parse_bootmenu((char *)addr);
			}
		}
	}

	for (i = 2; i < ARRAY_SIZE(default_menu_items); i++) {
		if (menu_item_count >= ARRAY_SIZE(menu_items))
			break;
		menu_items[menu_item_count++] = &default_menu_items[i];
	}

	setenv("stdout", "lcd");
}

static int boot_menu(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	int console_col,console_row;
	int i, sel = 0, max_sel;
	int tl_row;
	u32 btns;

	menu_init();

	tl_row = panel_info.vl_row / 16 / 2 - (menu_item_count + 2) / 2;
	max_sel = menu_item_count - 1;

	console_col = 3;
	console_row = tl_row;
	lcd_position_cursor(console_col,console_row);
	lcd_printf("Boot menu");

	while (1)
	{
		for (i = 0; i < menu_item_count; i++) {
			console_col = 3;
			console_row = tl_row + 2 + i;
			lcd_position_cursor(console_col,console_row);
			lcd_printf(menu_items[i]->name);
		}

		for (i = 0; i < menu_item_count; i++) {
			console_col = 1;
			console_row = tl_row + 2 + i;
			lcd_position_cursor(console_col,console_row);
			lcd_printf(i == sel ? ">" : " ");
		}

		flush_dcache_all();
		menu_wait_for_input(0);
		btns = menu_wait_for_input(1);
		if (btns & BTN_UP) {
			sel--;
			if (sel < 0)
				sel = max_sel;
		}
		else if (btns & BTN_DOWN) {
			sel++;
			if (sel > max_sel)
				sel = 0;
		}
		else {
			do_cmd("cls");
			if (menu_items[sel]->handler(menu_items[sel]))
				break;
			do_cmd("cls");
		}
	}

	return 0;
}

U_BOOT_CMD(
	pmenu, 1, 1, boot_menu,
	"show pandora's boot menu",
	""
);

/* helpers */
static int do_ssource(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	ulong addr, size = 0;

	if (argc < 2)
		return 1;

	addr = simple_strtoul(argv[1], NULL, 16);
	if (argc >= 3) {
		size = simple_strtoul(argv[2], NULL, 16);
		*(char *)(addr + size) = 0;
	}

	printf("## Executing plain script at %08lx, size %ld\n", addr, size);
	return parse_string_outer((char *)addr, FLAG_PARSE_SEMICOLON);
}

U_BOOT_CMD(
	ssource, 3, 0, do_ssource,
	"run script from memory (no header)",
	"<addr> [size_hex]"	/* note: without size may parse trash after the script */
);

static int do_usbinit(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	extern int drv_usbtty_init(void);
	static int usbinit_done;
	if (!usbinit_done) {
		usbinit_done = 1;
		return !drv_usbtty_init();
	}
	return 0;
}

U_BOOT_CMD(
	usbinit, 1, 0, do_usbinit,
	"initialize USB",
	""
);

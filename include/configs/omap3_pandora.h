/*
 * (C) Copyright 2008-2010
 * Gražvydas Ignotas <notasas@gmail.com>
 *
 * Configuration settings for the OMAP3 Pandora.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#define CONFIG_NR_DRAM_BANKS	2	/* CS1 may or may not be populated */
#define CONFIG_NAND

/* override base for compatibility with MLO the device ships with */
#define CONFIG_SYS_TEXT_BASE		0x80008000

#include <configs/ti_omap3_common.h>

/*
 * Display CPU and Board information
 */
#define CONFIG_DISPLAY_CPUINFO		1
#define CONFIG_DISPLAY_BOARDINFO	1

#define CONFIG_MISC_INIT_R
#define CONFIG_REVISION_TAG		1

#define CONFIG_ENV_SIZE			(128 << 10)	/* 128 KiB */

#define CONFIG_SYS_CONSOLE_IS_IN_ENV	1
#define CONFIG_SYS_DEVICE_NULLDEV	1

/* LCD support */
#define CONFIG_VIDEO_OMAP3		1
#define CONFIG_LCD			1
#define LCD_BPP				LCD_COLOR16
#define CONFIG_SYS_WHITE_ON_BLACK	1

/* used by menu code */
#define CONFIG_PREBOOT
#define CONFIG_SYS_CONSOLE_IS_IN_ENV	1
#define CONFIG_SYS_DEVICE_NULLDEV	1

#define CONFIG_USB_TTY			1

/*
 * Hardware drivers
 */

/* I2C Support */
#define CONFIG_SYS_I2C_OMAP34XX

/* TWL4030 LED */
#define CONFIG_TWL4030_LED		1

/* TWL4030 keyboard */
#define CONFIG_KEYBOARD			1

/* USB */
#define CONFIG_USB_DEVICE		1
#define CONFIG_USB_MUSB_UDC		1
#define CONFIG_USB_OMAP3		1
#define CONFIG_TWL4030_USB		1

/* Initialize GPIOs by default */
#define CONFIG_OMAP3_GPIO_4	/* GPIO96..127 is in GPIO Bank 4 */
#define CONFIG_OMAP3_GPIO_6	/* GPIO160..191 is in GPIO Bank 6 */

/*
 * NS16550 Configuration
 */
#undef CONFIG_SYS_NS16550_CLK
#define CONFIG_SYS_NS16550_SERIAL
#define CONFIG_SYS_NS16550_REG_SIZE	(-4)
#define CONFIG_SYS_NS16550_CLK		V_NS16550_CLK
#define CONFIG_SYS_NS16550_COM3		OMAP34XX_UART3
#define CONFIG_SERIAL3			3

/* commands to include */
#define CONFIG_CMD_CACHE	/* Cache control		*/

#undef CONFIG_CMD_FLASH		/* flinfo, erase, protect	*/
#undef CONFIG_CMD_FPGA		/* FPGA configuration Support	*/
#undef CONFIG_CMD_IMI		/* iminfo			*/
#undef CONFIG_CMD_IMLS		/* List all found images	*/

#define CONFIG_SYS_NO_FLASH
#define CONFIG_HARD_I2C			1
#define CONFIG_SYS_I2C_SPEED		100000
#define CONFIG_SYS_I2C_SLAVE		1
#define CONFIG_DRIVER_OMAP34XX_I2C	1

#if 1 /* USB EHCI - and everything related */

#define CONFIG_USB_EHCI			1
#define CONFIG_USB_EHCI_OMAP		1
#define CONFIG_CMD_USB			1
#define CONFIG_USB_STORAGE		1
#define CONFIG_OMAP_EHCI_PHY1_RESET_GPIO 16
#define CONFIG_USB_ULPI			1
#define CONFIG_USB_ULPI_VIEWPORT_OMAP	1
#define CONFIG_SYS_USB_EHCI_MAX_ROOT_PORTS 3
#define CONFIG_USB_HOST_ETHER
#define CONFIG_USB_ETHER_SMSC95XX
#define CONFIG_USB_ETHER_ASIX

#define CONFIG_CMD_NET
#define CONFIG_CMD_NFS
#define CONFIG_CMD_PING
#define CONFIG_CMD_DHCP

#else

#undef CONFIG_CMD_NET
#undef CONFIG_CMD_NFS
#undef CONFIG_CMD_PING
#undef CONFIG_CMD_DHCP

#endif


/*
 * Board NAND Info.
 */
#define CONFIG_SYS_NAND_ADDR		NAND_BASE	/* physical address */
							/* to access nand */
#define CONFIG_SYS_NAND_BUSWIDTH_16BIT
#define CONFIG_NAND_OMAP_ECCSCHEME	OMAP_ECC_HAM1_CODE_SW
#define CONFIG_SYS_NAND_PAGE_SIZE	2048
#define CONFIG_SYS_NAND_OOBSIZE		64

#ifdef CONFIG_NAND
#define CONFIG_CMD_UBI		/* UBI-formated MTD partition support */
#define CONFIG_CMD_UBIFS	/* Read-only UBI volume operations */

#define CONFIG_RBTREE		/* required by CONFIG_CMD_UBI */
#define CONFIG_LZO		/* required by CONFIG_CMD_UBIFS */

#define CONFIG_MTD_PARTITIONS	/* required for UBI partition support */

#define MTDIDS_DEFAULT			"nand0=omap2-nand.0"
#define MTDPARTS_DEFAULT		"mtdparts=omap2-nand.0:512k(xloader),"\
					"1920k(uboot),128k(uboot-env),"\
					"10m(boot),-(rootfs)"
#else
#define MTDPARTS_DEFAULT
#endif

/* Environment information */

#undef CONFIG_BOOTDELAY
#define CONFIG_BOOTDELAY		0

#define CONFIG_EXTRA_ENV_SETTINGS \
	DEFAULT_LINUX_BOOT_ENV \
	"usbtty=cdc_acm\0" \
	"bootargs=ubi.mtd=4 ubi.mtd=3 root=ubi0:rootfs rootfstype=ubifs " \
	"rw rootflags=bulk_read vram=6272K omapfb.vram=0:3000K quiet\0" \
	"mtdparts=" MTDPARTS_DEFAULT "\0" \

#define CONFIG_BOOTCOMMAND \
	"if mmc rescan && fatload mmc1 0 ${loadaddr} autoboot.txt || " \
			"ext2load mmc1 0 ${loadaddr} autoboot.txt; then " \
		"ssource ${loadaddr} ${filesize}; " \
	"fi; " \
	"if ubi part boot && ubifsmount ubi0:boot; then " \
		"ubifsload ${loadaddr} autoboot.txt && ssource ${loadaddr} ${filesize}; " \
		"ubifsload ${loadaddr} uImage && bootm ${loadaddr}; " \
	"fi; " \
	"setenv stdout lcd; echo Failed to load kernel, you may need to reflash the firmware.; " \
	"pmenu"

/* memtest works on */
#define CONFIG_SYS_MEMTEST_START	(OMAP34XX_SDRC_CS0)
#define CONFIG_SYS_MEMTEST_END		(OMAP34XX_SDRC_CS0 + \
					0x01F00000) /* 31MB */

#if defined(CONFIG_NAND)
#define CONFIG_SYS_FLASH_BASE		NAND_BASE
#endif

/* Monitor at start of flash */
#define CONFIG_SYS_MONITOR_BASE		CONFIG_SYS_FLASH_BASE

#define CONFIG_ENV_IS_IN_NAND		1
#define SMNAND_ENV_OFFSET		0x260000 /* environment starts here */

#define CONFIG_SYS_ENV_SECT_SIZE	(128 << 10)	/* 128 KiB */
#define CONFIG_ENV_OFFSET		SMNAND_ENV_OFFSET
#define CONFIG_ENV_ADDR			SMNAND_ENV_OFFSET

#define CONFIG_SYS_CACHELINE_SIZE	64

#endif				/* __CONFIG_H */

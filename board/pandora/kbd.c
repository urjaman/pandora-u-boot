/*
 * Based on board/rbc823/kbd.c, which is:
 *  (C) Copyright 2000
 *  Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * Also based on mainline board/nokia/rx51/rx51.c, which is:
 *  (C) Copyright 2012
 *  Ивайло Димитров <freemangordon@abv.bg>
 *  (C) Copyright 2011-2012
 *  Pali Rohár <pali.rohar@gmail.com>
 *  (C) Copyright 2010
 *  Alistair Buxton <a.j.buxton@gmail.com>
 *  Derived from Beagle Board and 3430 SDP code:
 *  (C) Copyright 2004-2008
 *  Texas Instruments, <www.ti.com>
 *
 * (C) Copyright 2013
 * Urja Rannikko <urjaman@gmail.com>
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
#include <stdio_dev.h>
#include <lcd.h>
#include <twl4030.h>

DECLARE_GLOBAL_DATA_PTR;

#ifdef CONFIG_KEYBOARD

/*
 * TWL4030 keypad handler
 */

static unsigned long int twl_i2c_lock;

static const char keymap[] = {
	/* normal */
	'9',  '8',  'i',  'j',  'n',   'm',	0,  0,
	'0',  '7',  'u',  'h',  'b',   ' ',	0,  0,
	 8,   '6',  'y',  'g',  'v',  /*Fn*/0,  0,  0,
	'o',  '5',  't',  'f',  'c',    0,    0,    0,
	'p',  '4',  'r',  'd',  'x',    0,    0,    0,
	'k',  '3',  'e',  's',  'z',    0,    0,    0,
	'l',  '2',  'w',  'a',  '.',    0,    0,    0,
	'\r', '1',  'q',/*Sh*/0,',',    0,    0,    0,
	/* fn */
	 0,    0,    0,    0,  '$',    0,    0,    0,
	 0,    0,    0,    0,  '|',  '\t',   0,    0,
	 0,    0,   '_',  '=', '#',    0,    0,    0,
	 0,    0,   '!',  '+', '\\',   0,    0,    0,
	 0,    0,   ')',  '-', '?',    0,    0,    0,
	 0,    0,   '(',  '"', '/',    0,    0,    0,
	 0,    0,   '@',  '\'', ':',   0,    0,    0,
	 0,    0,    0,   0,    ';',   0,    0,    0,
	/* shift */
	'[',  '*',  'I',  'J',  'N',   'M',	0,  0,
	']',  '&',  'U',  'H',  'B',   ' ',	0,  0,
	127,  '^',  'Y',  'G',  'V',    0,      0,  0,
	'O',  '%',  'T',  'F',  'C',    0,    0,    0,
	'P',  '~',  'R',  'D',  'X',    0,    0,    0,
	'K',  '}',  'E',  'S',  'Z',    0,    0,    0,
	'L',  '{',  'W',  'A',  '>',    0,    0,    0,
	'\r',  0,   'Q',   0,   '<',    0,    0,    0,
};

static u8 keys[8];
static u8 old_keys[8] = {0, 0, 0, 0, 0, 0, 0, 0};
#define KEYBUF_SIZE 32
static u8 keybuf[KEYBUF_SIZE];
static u8 keybuf_head;
static u8 keybuf_tail;

static void twl4030_kp_fill(u8 k, u8 mods)
{
	if (mods & 2) { /* fn meta key was pressed */
		k = keymap[k+64];
	} else if (mods & 4) { /* shift key was pressed */
		k = keymap[k+128];
	} else {
		k = keymap[k];
	}
	if (k) {
		keybuf[keybuf_tail++] = k;
		keybuf_tail %= KEYBUF_SIZE;
	}
}

int twl4030_kbd_init (void)
{
	int ret = 0;
	u8 ctrl;
	ret = twl4030_i2c_read_u8(TWL4030_CHIP_KEYPAD,
				  TWL4030_KEYPAD_KEYP_CTRL_REG, &ctrl);

	if (ret)
		return ret;

	/* turn on keyboard and use hardware scanning */
	ctrl |= TWL4030_KEYPAD_CTRL_KBD_ON;
	ctrl |= TWL4030_KEYPAD_CTRL_SOFT_NRST;
	ctrl |= TWL4030_KEYPAD_CTRL_SOFTMODEN;
	ret |= twl4030_i2c_write_u8(TWL4030_CHIP_KEYPAD, 
				    TWL4030_KEYPAD_KEYP_CTRL_REG, ctrl);
	/* enable key event status */
	ret |= twl4030_i2c_write_u8(TWL4030_CHIP_KEYPAD,
				    TWL4030_KEYPAD_KEYP_IMR1, 0xfe);
	/* enable interrupt generation on rising and falling */
	/* this is a workaround for qemu twl4030 emulation */
	ret |= twl4030_i2c_write_u8(TWL4030_CHIP_KEYPAD, 
				    TWL4030_KEYPAD_KEYP_EDR, 0x57);
	/* enable ISR clear on read */
	ret |= twl4030_i2c_write_u8(TWL4030_CHIP_KEYPAD, 
				    TWL4030_KEYPAD_KEYP_SIH_CTRL, 0x05);

	twl_i2c_lock = 0;
	return 0;
}

int twl4030_kbd_tstc(void)
{
	u8 c, r, dk, i;
	u8 intr;
	u8 mods;

	/* localy lock twl4030 i2c bus */
	if (test_and_set_bit(0, &twl_i2c_lock))
		return 0;

	/* twl4030 remembers up to 2 events */
	for (i = 0; i < 2; i++) {

		/* check interrupt register for events */
		twl4030_i2c_read_u8(TWL4030_CHIP_KEYPAD,
				    TWL4030_KEYPAD_KEYP_ISR1 + (2 * i), &intr);

		/* no event */
		if (!(intr&1))
			continue;

		/* read the key state */
		i2c_read(TWL4030_CHIP_KEYPAD,
			TWL4030_KEYPAD_FULL_CODE_7_0, 1, keys, 8);

		/* take modifier keys from the keystate */
		mods = keys[2]&0x20?2:0;     /* Fn */
		if (keys[7]&0x08) mods |= 4; /* Shift */

		for (c = 0; c < 8; c++) {

			/* get newly pressed keys only */
			dk = ((keys[c] ^ old_keys[c])&keys[c]);
			old_keys[c] = keys[c];

			/* fill the keybuf */
			for (r = 0; r < 8; r++) {
				if (dk&1)
					twl4030_kp_fill((c*8)+r, mods);
				dk = dk >> 1;
			}

		}

	}

	/* localy unlock twl4030 i2c bus */
	test_and_clear_bit(0, &twl_i2c_lock);

	return (KEYBUF_SIZE + keybuf_tail - keybuf_head)%KEYBUF_SIZE;
}

int twl4030_kbd_getc(void)
{
	keybuf_head %= KEYBUF_SIZE;
	while (!twl4030_kbd_tstc())
		/* Just pass the time and hope no watchdogs eat us.. */;
	return keybuf[keybuf_head++];
}

/* search for keyboard and register it if found */
int drv_keyboard_init(void)
{
	int error = 0;
	struct stdio_dev kbd_dev;

	if ((error=twl4030_kbd_init())) return error;

	/* register the keyboard */
	memset (&kbd_dev, 0, sizeof(struct stdio_dev));
	strcpy(kbd_dev.name, "kbd");
	kbd_dev.flags =  DEV_FLAGS_INPUT | DEV_FLAGS_SYSTEM;
	kbd_dev.putc = NULL;
	kbd_dev.puts = NULL;
	kbd_dev.getc = twl4030_kbd_getc;
	kbd_dev.tstc = twl4030_kbd_tstc;
	error = stdio_register (&kbd_dev);
	return error;
}

#endif /* CONFIG_KEYBOARD */

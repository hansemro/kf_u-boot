/*
 * Initially based on common/bootmenu.c
 * Additional code by Michael Scott
 *
 * ---------------------------
 *
 * (C) Copyright 2011-2013 Pali Rohár <pali.rohar@gmail.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <command.h>
#include <ansi.h>
#include <menu.h>
#include <malloc.h>
#include <linux/string.h>
#include <lcd.h>

#include "fg_bq27541.h"

/* maximum lcdmenu entries */
#define MAX_COUNT	99
#define MAX_MENU_NAME	100

#define DEFAULT_WIDTH	40

#define MENUBOX_TOP		"\xC4"
#define MENUBOX_TOPLEFT		"\xDA"
#define MENUBOX_TOPRIGHT	"\xBF"
#define MENUBOX_SIDE		"\xB3"

static char spacer[200] = "                                                                                                                                                                                             ";
static char spacer2[200] = "                                                                                                                                                                                             ";
static char topbox[200] = "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4";

/* maximal size of lcdmenu env
 *  14 = strlen("lcdmenuentry_")
 *  1 = strlen(MENUID)
 *  2 = strlen(MAX_COUNT)
 *  1 = NULL term
 */
#define MAX_ENV_SIZE	(14 + 1 + 2 + 1)

struct lcdmenu_config {
	int screen_cols;
	int screen_rows;
	int fg_color;
	int bg_color;
	int menu_top;
	int menu_left;
	int menu_width;
	int menu_height;
	int visible;

	int id;
	int active;			/* active menu entry */
	int count;			/* total count of menu entries */
	char name[MAX_MENU_NAME + 1];	/* name of the menu */
	struct lcdmenu_entry *first;	/* first menu entry */
} config;

struct lcdmenu_entry {
	unsigned short int num;		/* unique number 0 .. MAX_COUNT */
	char key[3];			/* key identifier of number */
	char *title;			/* title of entry */
	char *command;			/* command of entry */
	struct lcdmenu_entry *next;	/* next menu entry (num+1) */
};

// ENV read menu and menuentry
static char *lcdmenu_getmenu(unsigned short int n)
{
	char name[MAX_ENV_SIZE];

	if (n > MAX_COUNT)
		return NULL;

	sprintf(name, "lcdmenu_%1d", n);
	return getenv(name);
}

static char *lcdmenu_getmenuentry(unsigned short int m, unsigned short int n)
{
	char name[MAX_ENV_SIZE];

	if (n > MAX_COUNT)
		return NULL;

	sprintf(name, "lcdmenuentry_%1d%1d", m, n);
	return getenv(name);
}


// MEMORY functions
static void lcdmenu_destroy(void)
{
	struct lcdmenu_entry *iter = config.first;
	struct lcdmenu_entry *next;

	while (iter) {
		next = iter->next;
		free(iter->title);
		free(iter->command);
		free(iter);
		iter = next;
	}
	config.first = NULL;
}

static int lcdmenu_create(int menuid)
{
	unsigned short int i = 0;
	const char *env_menu;
	const char *env_entry;
	struct lcdmenu_entry *iter = NULL;

	int len;
	char *sep;
	struct lcdmenu_entry *entry;

	env_menu = lcdmenu_getmenu(menuid);
	if (!env_menu) {
		printf("ERROR: menu %d not found\n", menuid);
		return 1;
	}

	config.id = menuid;
	config.active = 0;
	config.first = NULL;
	strncpy(config.name, env_menu, MAX_MENU_NAME);

	while ((env_entry = lcdmenu_getmenuentry(menuid, i))) {
		sep = strchr(env_entry, '=');
		if (!sep) {
			printf("Invalid lcdmenu entry: %s\n", env_entry);
			break;
		}

		entry = malloc(sizeof(struct lcdmenu_entry));
		if (!entry)
			goto cleanup;

		len = sep-env_entry;
		entry->title = malloc(len + 1);
		if (!entry->title) {
			free(entry);
			goto cleanup;
		}
		memcpy(entry->title, env_entry, len);
		entry->title[len] = 0;

		len = strlen(sep + 1);
		if (len > 0) {
			entry->command = malloc(len + 1);
			memcpy(entry->command, sep + 1, len);
			entry->command[len] = 0;
		}
		else
			entry->command = strdup("");

		sprintf(entry->key, "%d", i);

		entry->num = i;
		entry->next = NULL;

		if (!iter)
			config.first = entry;
		else
			iter->next = entry;

		iter = entry;
		++i;

		if (i == MAX_COUNT - 1)
			break;
	}
	config.count = i;
	return 0;

cleanup:
	lcdmenu_destroy();
	return 1;
}


// RENDER functions
static void lcdmenu_clear(void)
{
	int i;

	spacer[config.menu_width+1] = 0;
	for (i = 0; i < config.menu_height+1; i++) {
		lcd_position_cursor(config.menu_left, (config.menu_top-1)+i);
		lcd_printf("%s", spacer);
	}
	spacer[config.menu_width+1] = 0x20;
}

void process_lcdmenuvars(char *orig, char *buffer);

static int lcdmenu_refresh(void)
{
	struct lcdmenu_entry *iter = config.first;
	int len, len2;
	char buffer[255];

	// re-read screen info
	config.screen_rows = lcd_get_screen_rows();
	config.screen_cols = lcd_get_screen_columns();
	config.fg_color    = simple_strtoul(getenv("lcdmenu_fg_color"), NULL, 16);
	config.bg_color    = simple_strtoul(getenv("lcdmenu_bg_color"), NULL, 16);

	// calc dimensions / locations
	config.menu_height = config.count+1;
	config.menu_top = config.screen_rows - config.menu_height;
	config.menu_width = getenv_hex("lcdmenu_width", DEFAULT_WIDTH) + 2; // add 2 for border
	config.menu_left = (config.screen_cols/2) - (config.menu_width/2) - 1; // adjust for border

	// RENDER STATUS BAR
	int capacity = get_bat_capacity();
	if (capacity < 10)
		lcd_setfgcolor(CONSOLE_COLOR_RED);
	else if (capacity < 50)
		lcd_setfgcolor(CONSOLE_COLOR_YELLOW);
	else
		lcd_setfgcolor(CONSOLE_COLOR_GREEN);
	lcd_position_cursor(0, 0);
	lcd_setbgcolor(CONSOLE_COLOR_BLACK);
	lcd_printf("%d%%", capacity);
#ifdef CONFIG_OMAP4KC1_VERSION
	// PRINT VERSION IN UPPER RIGHT CORNER
	len = lcd_strlen(CONFIG_OMAP4KC1_VERSION);
	lcd_position_cursor(lcd_get_screen_columns()-len, 0);
	lcd_setfgcolor(CONSOLE_COLOR_WHITE);
	lcd_setbgcolor(CONSOLE_COLOR_BLACK);
	lcd_printf(CONFIG_OMAP4KC1_VERSION);
#endif
	
	// RENDER TOP OF BOX
	topbox[config.menu_width-1] = 0;
	lcd_position_cursor(config.menu_left, config.menu_top - 1);
	lcd_setfgcolor(config.fg_color);
	lcd_setbgcolor(config.bg_color);
	lcd_printf(MENUBOX_TOPLEFT "%s" MENUBOX_TOPRIGHT, topbox);
	topbox[config.menu_width-1] = MENUBOX_TOP;

	// RENDER MENU NAME
	len = ((config.menu_width - (lcd_strlen(config.name) + 1)) / 2) - 1;
	spacer[len] = 0;
	len2 = config.menu_width - (lcd_strlen(config.name) + 1) - len - 1;
	spacer2[len2] = 0;
	lcd_position_cursor(config.menu_left, config.menu_top);
	lcd_setfgcolor(config.fg_color);
	lcd_setbgcolor(config.bg_color);
	lcd_printf(MENUBOX_SIDE "%s%s:%s" MENUBOX_SIDE, spacer, config.name, spacer2);
	spacer[len] = 0x20;
	spacer2[len2] = 0x20;

	// LOOP through menu entries
	iter = config.first;
	while (iter) {
		lcd_position_cursor(config.menu_left, (config.menu_top + iter->num + 1));
		lcd_setfgcolor(config.fg_color);
		lcd_setbgcolor(config.bg_color);
		strcpy(buffer, iter->title);
		lcdmenu_processvars(buffer);
		len = config.menu_width - lcd_strlen(buffer) - 2;
		spacer[len] = 0;
		if (config.active == iter->num)
			lcd_printf(MENUBOX_SIDE " %s%s%s%s" MENUBOX_SIDE, ANSI_COLOR_REVERSE, buffer, ANSI_COLOR_RESET, spacer);
		else
			lcd_printf(MENUBOX_SIDE " %s%s" MENUBOX_SIDE, buffer, spacer);
		iter = iter->next;
		spacer[len] = 0x20;
	}

	return 0;
}

static void lcdmenu_hide(void)
{
	if (config.visible)
		lcdmenu_clear();

	lcdmenu_destroy();
	config.visible = 0;
}

static int lcdmenu_show(int menuid)
{
	if (lcdmenu_create(menuid)) {
		return 1;
	}

	lcdmenu_refresh();

	config.visible = 1;
	return 0;
}

int do_lcdmenu(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
	int i;
	int menuid = 0;
	char *command = NULL;
	struct lcdmenu_entry *iter = config.first;

	if (argc < 2)
		return 1;

	// lcdmenu set <index>
	if (!(strcmp(argv[1], "show"))) {
		if (argc < 3)
			return 1;

		menuid = simple_strtoul(argv[2], NULL, 16);
		lcdmenu_hide();
		lcdmenu_show(menuid);
	}
	else if (!(strcmp(argv[1], "hide"))) {
		lcdmenu_hide();
	}
	else if (!(strcmp(argv[1], "key"))) {

		if (argc < 3)
			return 1;

		if (config.visible == 0)
			return 1;

		if (!(strcmp(argv[2], "up"))) {
			if (config.active > 0)
				--config.active;
			else
				config.active = config.count-1;
		}
		else if (!(strcmp(argv[2], "down"))) {
			if (config.active < config.count - 1)
				++config.active;
			else
				config.active = 0;
		}
		else if (!(strcmp(argv[2], "select"))) {
			iter = config.first;
			for (i = 0; i < config.active; ++i)
				iter = iter->next;
			if (iter->command) {
				debug("Starting entry '%s'\n", iter->title);
				command = strdup(iter->command);
				lcdmenu_processvars(command);
				run_command(command, 0);
				free(command);
			}
			/* don't continue here as we're probably nested */
			return 0;
		}

		lcdmenu_refresh();
	}

	return 0;
}


U_BOOT_CMD( lcdmenu, 3, 1, do_lcdmenu, "ANSI lcdmenu <show | hide | key>", NULL );

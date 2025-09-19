/*
	Adrenaline XmbControl
	Copyright (C) 2011, Total_Noob
	Copyright (C) 2011, Frostegater
	Copyright (C) 2025, GrayJack

	main.h: XmbControl main header file

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __MAIN_H__
#define __MAIN_H__

#include <psptypes.h>

enum {
	CPU_CLOCK_VSH,
	CPU_CLOCK_GAME,
	UMD_DRIVER,
	SKIP_LOGO_COLDBOOT,
	SKIP_LOGO_GAMEBOOT,
	HIDE_CORRUPT_ICONS,
	HIDE_MAC_ADDR,
	HID_DLCS,
	HIDE_PIC_0_1,
	AUTORUN_BOOT,
	VSH_REGION,
	EXT_COLORS,
	USE_SONY_OSK,
	USE_NO_DRM,
	XMBCNTRL,
	FORCE_HIGHMEM,
	EXEC_BOOT_BIN,
	INFERNO_CACHE_POLICY,
	INFERNO_CACHE_NUM,
	INFERNO_CACHE_SIZE,
	INFERNO_SIM_UMD_SEEK,
	INFERNO_SIM_UMD_SPEED,
	VSH_PLUGINS,
	GAME_PLUGINS,
	POPS_PLUGINS,
};

typedef struct {
	u8 mode;
	u8 negative;
	char *item;
	u8 need_reboot;
	u8 advanced;
	u8 *value;
} GetItem;

typedef struct {
  u8 n;
  char **c;
} ItemOptions;

extern GetItem GetItemes[];
extern int num_items;
extern ItemOptions item_opts[];
extern int psp_model;

#define sysconf_console_id 4
#define sysconf_console_action 2
#define sysconf_console_action_arg 2

enum {
	sysconf_cfwconfig_action_arg = 0x1010,
	sysconf_plugins_action_arg = 0x1012,
};

typedef struct {
	char text[48];
	int play_sound;
	int action;
	int action_arg;
} SceContextItem;

typedef struct {
	int id;
	int relocate;
	int action;
	int action_arg;
	SceContextItem *context;
	char *subtitle;
	int unk;
	char play_sound;
	char memstick;
	char umd_icon;
	char image[4];
	char image_shadow[4];
	char image_glow[4];
	char text[0x25];
} SceVshItem;

typedef struct {
	void *unk;
	int id;
	char *regkey;
	char *text;
	char *subtitle;
	char *page;
} SceSysconfItem;

typedef struct {
	u8 id;
	u8 type;
	u16 unk1;
	u32 label;
	u32 param;
	u32 first_child;
	int child_count;
	u32 next_entry;
	u32 prev_entry;
	u32 parent;
	u32 unknown[2];
} SceRcoEntry;

int sce_paf_private_wcslen(wchar_t *);
int sce_paf_private_sprintf(char *, const char *, ...);
void *sce_paf_private_memcpy(void *, void *, int);
void *sce_paf_private_memset(void *, char, int);
int sce_paf_private_strlen(char *);
char *sce_paf_private_strcpy(char *, const char *);
char *sce_paf_private_strncpy(char *, const char *, int);
int sce_paf_private_strcmp(const char *, const char *);
int sce_paf_private_strncmp(const char *, const char *, int);
char *sce_paf_private_strchr(const char *, int);
char *sce_paf_private_strrchr(const char *, int);
int sce_paf_private_strpbrk(const char *, const char *);
int sce_paf_private_strtoul(const char *, char **, int);
void *sce_paf_private_malloc(int);
void sce_paf_private_free(void *);

wchar_t *scePafGetText(void *, char *);
int PAF_Resource_GetPageNodeByID(void *, char *, SceRcoEntry **);
int PAF_Resource_ResolveRefWString(void *, u32 *, int *, char **, int *);

int vshGetRegistryValue(u32 *, char *, void *, int , int *);
int vshSetRegistryValue(u32 *, char *, int , int *);

int sceVshCommonGuiBottomDialog(void *a0, void *a1, void *a2, int (* cancel_handler)(), void *t0, void *t1, int (* handler)(), void *t3);

void saveSettings(void);
void loadSettings(void);

void PatchVshMain(u32 text_addr, u32 text_size);
void PatchAuthPlugin(u32 text_addr, u32 text_size);
void PatchSysconfPlugin(u32 text_addr, u32 text_size);

#endif

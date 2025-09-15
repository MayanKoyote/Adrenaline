/*
	Adrenaline
	Copyright (C) 2016-2018, TheFloW
	Copyright (C) 2025, GrayJack

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

#include <common.h>
#include <pspusbcam.h>
#include <pspdisplay.h>

#define _ADRENALINE_LOG_IMPL_
#include <adrenaline_log.h>

#include "main.h"
#include "adrenaline.h"
#include "executable_patch.h"
#include "libraries_patch.h"
#include "io_patch.h"
#include "init_patch.h"
#include "power_patch.h"
#include "usbcam_patch.h"
#include "utility_patch.h"
#include "sysmodpatches.h"
#include "nodrm.h"
#include "malloc.h"
#include "ttystdio.h"
#include "gameinfo.h"
#include "plugin.h"

PSP_MODULE_INFO("SystemControl", 0x1007, 1, 1);

int (* PrologueModule)(void *modmgr_param, SceModule *mod);

STMOD_HANDLER module_handler;
STMOD_HANDLER game_previous = NULL;

RebootexConfig rebootex_config;
AdrenalineConfig config;

int idle = 0;

static u32 MakeSyscallStub(void *function) {
	SceUID block_id = sceKernelAllocPartitionMemory(PSP_MEMORY_PARTITION_USER, "", PSP_SMEM_High, 2 * sizeof(u32), NULL);
	u32 stub = (u32)sceKernelGetBlockHeadAddr(block_id);
	MAKE_SYSCALL_FUNCTION(stub, sceKernelQuerySystemCall(function));
	return stub;
}

int ReadFile(char *file, void *buf, int size) {
	SceUID fd = sceIoOpen(file, PSP_O_RDONLY, 0);
	if (fd < 0)
		return fd;

	int read = sceIoRead(fd, buf, size);

	sceIoClose(fd);
	return read;
}

int WriteFile(char *file, void *buf, int size) {
	SceUID fd = sceIoOpen(file, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
	if (fd < 0)
		return fd;

	int written = sceIoWrite(fd, buf, size);

	sceIoClose(fd);
	return written;
}

void sctrlFlushCache() {
	sceKernelIcacheInvalidateAll();
	sceKernelDcacheWritebackInvalidateAll();
}

int PrologueModulePatched(void *modmgr_param, SceModule *mod) {
	int res = PrologueModule(modmgr_param, mod);

	if (res >= 0 && module_handler)
		module_handler(mod);

	return res;
}

STMOD_HANDLER sctrlHENSetStartModuleHandler(STMOD_HANDLER handler) {
	STMOD_HANDLER prev = module_handler;
	module_handler = (STMOD_HANDLER)((u32)handler | 0x80000000);
	return prev;
}

int sceResmgrDecryptIndexPatched(void *buf, int size, int *retSize) {
	int k1 = pspSdkSetK1(0);
	*retSize = ReadFile("flash0:/vsh/etc/version.txt", buf, size);
	pspSdkSetK1(k1);
	return 0;
}

int memcmp_patched(const void *b1, const void *b2, size_t len) {
	u32 tag = 0x4C9494F0;
	if (memcmp(&tag, b2, len) == 0) {
		static u8 kernel661_keys[0x10] = { 0x76, 0xF2, 0x6C, 0x0A, 0xCA, 0x3A, 0xBA, 0x4E, 0xAC, 0x76, 0xD2, 0x40, 0xF5, 0xC3, 0xBF, 0xF9 };
		memcpy((void *)0xBFC00220, kernel661_keys, sizeof(kernel661_keys));
		return 0;
	}

	return memcmp(b1, b2, len);
}

static void PatchMemlmd() {
	SceModule *mod = sceKernelFindModuleByName("sceMemlmd");
	u32 text_addr = mod->text_addr;

	// Allow 6.61 kernel modules
	MAKE_CALL(text_addr + 0x2C8, memcmp_patched);
}

static void PatchInterruptMgr() {
	SceModule *mod = sceKernelFindModuleByName("sceInterruptManager");
	u32 text_addr = mod->text_addr;

	// Allow execution of syscalls in kernel mode
	MAKE_INSTRUCTION(text_addr + 0xE98, 0x408F7000);
	MAKE_NOP(text_addr + 0xE9C);
}

static void PatchModuleMgr() {
	SceModule *mod = sceKernelFindModuleByName("sceModuleManager");
	u32 text_addr = mod->text_addr;

	for (int i = 0; i < mod->text_size; i += 4) {
		u32 addr = text_addr + i;
		u32 data = VREAD32(addr);

		if (data == 0xA4A60024) {
			// Patch to allow a full coverage of loaded modules
			PrologueModule = (void *)K_EXTRACT_CALL(addr - 4);
			MAKE_CALL(addr - 4, PrologueModulePatched);
			continue;
		}

		if (data == 0x27BDFFE0 && VREAD32(addr + 4) == 0xAFB10014) {
			HIJACK_FUNCTION(addr, PartitionCheckPatched, PartitionCheck);
			continue;
		}
	}

	// Dummy patch for LEDA
	MAKE_JUMP(sctrlHENFindImport(mod->modname, "ThreadManForKernel", 0x446D8DE6), sceKernelCreateThread);
}


static void PatchLoadCore() {
	SceModule *mod = sceKernelFindModuleByName("sceLoaderCore");
	u32 text_addr = mod->text_addr;

	HIJACK_FUNCTION(K_EXTRACT_IMPORT(&sceKernelCheckExecFile), sceKernelCheckExecFilePatched, _sceKernelCheckExecFile);
	HIJACK_FUNCTION(K_EXTRACT_IMPORT(&sceKernelProbeExecutableObject), sceKernelProbeExecutableObjectPatched, _sceKernelProbeExecutableObject);

	for (int i = 0; i < mod->text_size; i += 4) {
		u32 addr = text_addr + i;
		u32 data = VREAD32(addr);

		// Allow custom modules
		if (data == 0x1440FF55) {
			PspUncompress = (void *)K_EXTRACT_CALL(addr - 8);
			MAKE_CALL(addr - 8, PspUncompressPatched);
			continue;
		}

		// Patch relocation check in switch statement (7 -> 0)
		if (data == 0x00A22021) {
			u32 high = (((u32)VREAD16(addr - 0xC)) << 16);
			u32 low = ((u32)VREAD16(addr - 0x4));

			if (low & 0x8000) high -= 0x10000;

			u32 *RelocationTable = (u32 *)(high | low);

			RelocationTable[7] = RelocationTable[0];

			continue;
		}

		// Allow kernel modules to have syscall imports
		if (data == 0x30894000) {
			VWRITE32(addr, 0x3C090000);
			continue;
		}

		// Allow lower devkit version
		if (data == 0x14A0FFCB) {
			VWRITE16(addr + 2, 0x1000);
			continue;
		}

		// Allow higher devkit version
		if (data == 0x14C0FFDF) {
			MAKE_NOP(addr);
			continue;
		}

		// Patch to resolve NIDs
		if (data == 0x8D450000) {
			MAKE_INSTRUCTION(addr + 4, 0x02203021); // move $a2, $s1
			search_nid_in_entrytable = (void *)K_EXTRACT_CALL(addr + 8);
			MAKE_CALL(addr + 8, search_nid_in_entrytable_patched);
			MAKE_INSTRUCTION(addr + 0xC, 0x02403821); // move $a3, $s2
			continue;
		}

		if (data == 0xADA00004) {
			// Patch to resolve NIDs
			MAKE_NOP(addr);
			MAKE_NOP(addr + 8);

			// Patch to undo prometheus patches
			HIJACK_FUNCTION(addr + 0xC, aLinkLibEntriesPatched, aLinkLibEntries);

			continue;
		}

		// Patch call to init module_bootstart
		if (data == 0x02E0F809) {
			MAKE_CALL(addr, PatchInit);
			MAKE_INSTRUCTION(addr + 4, 0x02E02021); // move $a0, $s7
			continue;
		}

		// Restore original call
		if (data == 0xAE2D0048) {
			MAKE_CALL(addr + 8, FindProc("sceMemlmd", "memlmd", 0xEF73E85B));
			continue;
		}

		if (data == 0x40068000 && VREAD32(addr + 4) == 0x7CC51180) {
			LoadCoreForKernel_nids[0].function = (void *)addr;
			continue;
		}

		if (data == 0x40068000 && VREAD32(addr + 4) == 0x7CC51240) {
			LoadCoreForKernel_nids[1].function = (void *)addr;
			continue;
		}
	}
}

// Taken from ARK-4
u32 _findJAL(u32 addr, int reversed, int skip) {
	if (addr != 0) {
		int add = 4;
		if (reversed) {
			add = -4;
		}
		for(;;addr += add) {
			if ((VREAD32(addr) >= 0x0C000000) && (VREAD32(addr) < 0x10000000)){
				if (skip == 0) {
					return (((VREAD32(addr) & 0x03FFFFFF) << 2) | 0x80000000);
				} else {
					skip--;
				}
			}
		}
	}

	return 0;
}

// Taken from ARK-3
u32 FindFirstBEQ(u32 addr) {
	for (;; addr += 4){
		u32 data = VREAD32(addr);
		if ((data & 0xFC000000) == 0x10000000) {
			return addr;
		}
	}

	return 0;
}

static void PatchSysmem() {
	u32 nids[] = { 0x7591C7DB, 0x342061E5, 0x315AD3A0, 0xEBD5C3E6, 0x057E7380, 0x91DE343C, 0x7893F79A, 0x35669D4C, 0x1B4217BC, 0x358CA1BB };

	for (int i = 0; i < sizeof(nids) / sizeof(u32); i++) {
		u32 addr = FindFirstBEQ(FindProc("sceSystemMemoryManager", "SysMemUserForUser", nids[i]));
		if (addr) {
			VWRITE16(addr + 2, 0x1000);
		}
	}
}

int (* sceKernelVolatileMemTryLock)(int unk, void **ptr, int *size);
int sceKernelVolatileMemTryLockPatched(int unk, void **ptr, int *size) {
	int res = 0;

	for (int i = 0; i < 0x10; i++) {
		res = sceKernelVolatileMemTryLock(unk, ptr, size);
		if (res >= 0) {
			break;
		}

		sceKernelDelayThread(100);
	}

	return res;
}

static void PatchVolatileMemBug() {
	if (sceKernelBootFrom() == SCE_BOOT_DISC) {
		sceKernelVolatileMemTryLock = (void *)FindProc("sceSystemMemoryManager", "sceSuspendForUser", 0xA14F40B2);
		sctrlHENPatchSyscall((u32)sceKernelVolatileMemTryLock, sceKernelVolatileMemTryLockPatched);
		sctrlFlushCache();
	}
}

int sceUmdRegisterUMDCallBackPatched(int cbid) {
	int k1 = pspSdkSetK1(0);
	int res = sceKernelNotifyCallback(cbid, PSP_UMD_NOT_PRESENT);
	pspSdkSetK1(k1);
	return res;
}

int cpu_list[] = { 0, 20, 75, 100, 133, 222, 266, 300, 333 };
int bus_list[] = { 0, 10, 37,  50,  66, 111, 133, 150, 166 };
u32 cache_size_list[] = { 0, 1024, 2*1024, 4*1024, 8*1024, 16*1024, 32*1024, 64*1024 };
u8 cache_num_list[] = { 0, 1, 2, 4, 8, 16, 32, 64, 128 };

#define N_CPU (sizeof(cpu_list) / sizeof(int))
#define N_CACHE_SIZE (sizeof(cache_size_list) / sizeof(u32))
#define N_CACHE_NUM (sizeof(cache_num_list) / sizeof(u8))

static int (*utilityGetParam)(int, int*) = NULL;
static int utilityGetParamPatched_ULJM05221(int param, int* value) {
	int res = utilityGetParam(param, value);
	if (param == PSP_SYSTEMPARAM_ID_INT_LANGUAGE && *value > 1) {
		*value = 0;
	}
	return res;
}

static int wweModuleOnStart(SceModule * mod) {
	// Boot Complete Action not done yet
	if (strcmp(mod->modname, "mainPSP") == 0) {
		sctrlHENHookImportByNID(mod, "scePower", 0x34F9C463, NULL, 222); // scePowerGetPllClockFrequencyInt
		sctrlHENHookImportByNID(mod, "scePower", 0x843FBF43, NULL, 0);   // scePowerSetCpuClockFrequency
		sctrlHENHookImportByNID(mod, "scePower", 0xFDB5BFE9, NULL, 222); // scePowerGetCpuClockFrequencyInt
		sctrlHENHookImportByNID(mod, "scePower", 0xBD681969, NULL, 111); // scePowerGetBusClockFrequencyInt
	}

	// Call Previous Module Start Handler
	if(game_previous) game_previous(mod);

	return 0;
}

static void PatchGameByGameId() {
	char* game_id = rebootex_config.game_id;

	// Fix TwinBee Portable when not using English or Japanese language
	if (strcasecmp("ULJM05221", game_id) == 0) {
		utilityGetParam = (void*)FindProc("sceUtility_Driver", "sceUtility", 0xA5DA2406);
		sctrlHENPatchSyscall((u32)utilityGetParam, utilityGetParamPatched_ULJM05221);

	} else if (strcasecmp("ULES01472", game_id) == 0 || strcasecmp("ULUS10543", game_id) == 0) {
		// Patch Smakdown vs RAW 2011 anti-CFW check (CPU speed)
		game_previous = sctrlHENSetStartModuleHandler(wweModuleOnStart);

	} else if (strcasecmp("ULES00590", game_id) == 0 || strcasecmp("ULJM05075", game_id) == 0) {
		// Patch Aces of War anti-CFW check (UMD speed)
		if (config.umd_seek == 0 && config.umd_speed == 0) {
			void (*SetUmdDelay)(int, int) = NULL;
			if (rebootex_config.bootfileindex == BOOT_INFERNO) {
				SetUmdDelay = (void*)sctrlHENFindFunction("EPI-InfernoDriver", "inferno_driver", 0xB6522E93);
			} else if (rebootex_config.bootfileindex == BOOT_MARCH33) {
				SetUmdDelay = (void*)sctrlHENFindFunction("EPI-March33Driver", "march33_driver", 0xFAEC97D6);
			} else if (rebootex_config.bootfileindex == BOOT_NP9660) {
				SetUmdDelay = (void*)sctrlHENFindFunction("EPI-GalaxyController", "galaxy_driver", 0xFAEC97D6);
			}

			if (SetUmdDelay != NULL) {
				SetUmdDelay(1, 1);
			}
		}

	}
}

static int moduleLoaderJackass(char* name, int value) {
	char path[256];

	SceKernelLMOption option;
	memset(&option, 0, sizeof(option));
	option.size = sizeof(option);
	option.mpidtext = 2;
	option.mpiddata = 2;
	option.position = 0;
	option.access = 1;

	int res = sceKernelLoadModule(path, 0, &option);
	if (res >= 0)
	{
		int status;
		res = sceKernelStartModule(res,0,0,&status,0);
	}
	return res;
}


static void PatchGamesByMod(SceModule* mod) {
	char *modname = mod->modname;

	if (strcmp(modname, "DJMAX") == 0 || strcmp(modname, "djmax") == 0) {
		sctrlHENHookImportByNID(mod, "IoFileMgrForUser", 0xE3EB004C, NULL, 0);

		if (rebootex_config.bootfileindex == BOOT_INFERNO) {
			SceModule* inferno_mod = sceKernelFindModuleByName("EPI-InfernoDriver");

			if (config.umd_seek == 0 && config.umd_speed == 0) {
				// enable UMD reading speed
				void (*SetUmdDelay)(int, int) = (void*)sctrlHENFindFunctionInMod(inferno_mod, "inferno_driver", 0xB6522E93);
				if (SetUmdDelay != NULL) {
					SetUmdDelay(2, 2);
				}
			}

			if (config.iso_cache != CACHE_CONFIG_OFF) {
				// Disable Inferno cache
				int (*CacheInit)(int, int, int) = (void*)sctrlHENFindFunctionInMod(inferno_mod, "inferno_driver", 0x8CDE7F95);
				if (CacheInit != NULL) {
					CacheInit(0, 0, 0);
				}
			}
		} else if (rebootex_config.bootfileindex == BOOT_MARCH33) {
			if (config.umd_seek == 0 && config.umd_speed == 0) {
				// enable UMD reading speed
				void (*SetUmdDelay)(int, int) = (void*)sctrlHENFindFunction("EPI-March33Driver", "inferno_driver", 0xFAEC97D6);
				if (SetUmdDelay != NULL) {
					SetUmdDelay(2, 2);
				}
			}
		} else if (rebootex_config.bootfileindex == BOOT_NP9660) {
			if (config.umd_seek == 0 && config.umd_speed == 0) {
				// enable UMD reading speed
				void (*SetUmdDelay)(int, int) = (void*)sctrlHENFindFunction("EPI-GalaxyController", "galaxy_driver", 0xFAEC97D6);
				if (SetUmdDelay != NULL) {
					SetUmdDelay(2, 2);
				}
			}
		}

	} else if (strcmp(mod->modname, "ATVPRO") == 0){
		// Remove "overclock" message in ATV PRO
		// scePowerSetCpuClockFrequency, scePowerGetCpuClockFrequencyInt and scePowerGetBusClockFrequencyInt respectively
		sctrlHENHookImportByNID(mod, "scePower", 0x843FBF43, NULL, 0);
		sctrlHENHookImportByNID(mod, "scePower", 0xFDB5BFE9, NULL, 222);
		sctrlHENHookImportByNID(mod, "scePower", 0xBD681969, NULL, 111);

	} else if (strcmp(modname, "tekken") == 0) {
		// Fix back screen on Tekken 6
		// scePowerGetPllClockFrequencyInt
		sctrlHENHookImportByNID(mod, "scePower", 0x34F9C463, NULL, 222);

	} else if (strcmp(modname, "KHBBS_patch") == 0) {
		MAKE_DUMMY_FUNCTION(mod->entry_addr, 1);

	} else if (strcmp(modname, "Jackass") == 0) {
		char* game_id = rebootex_config.game_id;
		if (strcasecmp("ULES00897", game_id) == 0)
		{
			logmsg4("%s: [DEBUG]: Patching Jackass PAL\n", __func__);
			REDIRECT_FUNCTION(mod->text_addr + 0x35A204, MakeSyscallStub(moduleLoaderJackass));
		}
		else if (strcasecmp("ULUS10303", game_id) == 0)
		{
			logmsg4("%s: [DEBUG]: Patching Jackass NTSC\n", __func__);
			REDIRECT_FUNCTION(mod->text_addr + 0x357B54, MakeSyscallStub(moduleLoaderJackass));
		}
	}

	sctrlFlushCache();
}


static void OnSystemStatusIdle() {
	SceAdrenaline *adrenaline = (SceAdrenaline *)ADRENALINE_ADDRESS;

	initAdrenalineInfo();
	PatchVolatileMemBug();

	// March33 UMD seek/speed simulation
	SceModule* march33_mod = sceKernelFindModuleByName("EPI-March33Driver");

	if (march33_mod != NULL) {
		// Handle March33's UMD seek and UMD speed setting
		if (config.umd_seek > 0 || config.umd_speed > 0) {
			config.iso_cache = CACHE_CONFIG_OFF;

			void (*SetUmdDelay)(int, int) = (void*)sctrlHENFindFunctionInMod(march33_mod, "march33_driver", 0xFAEC97D6);

			if (SetUmdDelay != NULL) {
				SetUmdDelay(config.umd_seek, config.umd_speed);
				logmsg3("[INFO]: March33 ISO UMD seek/speed factor: %d seek factor; %d speed factor\n", config.umd_seek, config.umd_speed);
			}
		}
	}

	// NP9660 UMD seek/speed simulation
	SceModule* galaxy_mod = sceKernelFindModuleByName("EPI-GalaxyController");

	if (galaxy_mod != NULL) {
		// Handle March33's UMD seek and UMD speed setting
		if (config.umd_seek > 0 || config.umd_speed > 0) {
			config.iso_cache = CACHE_CONFIG_OFF;

			void (*SetUmdDelay)(int, int) = (void*)sctrlHENFindFunctionInMod(galaxy_mod, "galaxy_driver", 0xFAEC97D6);

			if (SetUmdDelay != NULL) {
				SetUmdDelay(config.umd_seek, config.umd_speed);
				logmsg3("[INFO]: NP9660 ISO UMD seek/speed factor: %d seek factor; %d speed factor\n", config.umd_seek, config.umd_speed);
			}
		}
	}

	// Inferno cache config and UMD seek/speed simulation
	SceModule* inferno_mod = sceKernelFindModuleByName("EPI-InfernoDriver");

	// Inferno driver is loaded
	if (inferno_mod != NULL) {
		// Handle Inferno's UMD seek and UMD speed setting
		if (config.umd_seek > 0 || config.umd_speed > 0) {
			config.iso_cache = CACHE_CONFIG_OFF;

			void (*SetUmdDelay)(int, int) = (void*)sctrlHENFindFunctionInMod(inferno_mod, "inferno_driver", 0xB6522E93);

			if (SetUmdDelay != NULL) {
				SetUmdDelay(config.umd_seek, config.umd_speed);
				logmsg3("[INFO]: Inferno ISO UMD seek/speed factor: %d seek factor; %d speed factor\n", config.umd_seek, config.umd_speed);
			}
		}

		// Handle Inferno Iso cache
		if (config.iso_cache != CACHE_CONFIG_OFF) {
			if (rebootex_config.ram2 > 24 || config.force_high_memory != HIGHMEM_OPT_OFF) {
				config.iso_cache_partition = 2;
			} else {
				config.iso_cache_partition = 11;
			}

			int (*CacheInit)(int, int, int) = (void*)sctrlHENFindFunctionInMod(inferno_mod, "inferno_driver", 0x8CDE7F95);
			if (CacheInit != NULL) {
				u32 cache_size = (config.iso_cache_size == ISO_CACHE_SIZE_AUTO) ? 32*1024 : cache_size_list[config.iso_cache_size%N_CACHE_SIZE];
				u8 cache_num = (config.iso_cache_num == ISO_CACHE_NUM_AUTO) ? 32 : cache_num_list[config.iso_cache_num%N_CACHE_NUM];
				CacheInit(cache_size, cache_num, config.iso_cache_partition);
				logmsg3("[INFO]: Inferno ISO cache: %d caches of %ld KiB in partition %d — Total: %ld KiB\n", cache_num, cache_size/1024, config.iso_cache_partition, (cache_num*cache_size)/1024);
			}

			int (*CacheSetPolicy)(int) = (void*)sctrlHENFindFunctionInMod(inferno_mod, "inferno_driver", 0xC0736FD6);
			if (CacheSetPolicy != NULL) {
				if (config.iso_cache == CACHE_CONFIG_LRU) {
					CacheSetPolicy(CACHE_POLICY_LRU);
					logmsg3("[INFO]: Inferno ISO cache policy: LRU\n");
				} else if (config.iso_cache == 2) {
					CacheSetPolicy(CACHE_POLICY_RR);
					logmsg3("[INFO]: Inferno ISO cache policy: RR\n");
				}
			}
		}
	}

	// Set CPU/BUS speed on apps/games
	SceBootMediumType medium_type = sceKernelBootFrom();
	SceApplicationType app_type = sceKernelApplicationType();
	u8 is_correct_medium = (medium_type == SCE_BOOT_DISC || medium_type == SCE_BOOT_MS || medium_type == SCE_BOOT_EF);

	if (app_type != SCE_APPTYPE_VSH && is_correct_medium) {
		SetSpeed(cpu_list[config.app_cpu_speed % N_CPU], bus_list[config.app_cpu_speed % N_CPU]);
	}

	// Set fake framebuffer so that cwcheat can be displayed
	if (adrenaline->pops_mode) {
		sceDisplaySetFrameBuf((void *)NATIVE_FRAMEBUFFER, PSP_SCREEN_LINE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
		memset((void *)NATIVE_FRAMEBUFFER, 0, SCE_PSPEMU_FRAMEBUFFER_SIZE);
	} else {
		SendAdrenalineCmd(ADRENALINE_VITA_CMD_RESUME_POPS);
	}
	SendAdrenalineCmd(ADRENALINE_VITA_CMD_APP_STARTED);
}

int (* sceMeAudio_driver_C300D466)(int codec, int unk, void *info);
int sceMeAudio_driver_C300D466_Patched(int codec, int unk, void *info) {
	int res = sceMeAudio_driver_C300D466(codec, unk, info);

	if (res < 0 && codec == 0x1002 && unk == 2) {
		return 0;
	}

	return res;
}

int sceKernelSuspendThreadPatched(SceUID thid) {
	SceKernelThreadInfo info;
	info.size = sizeof(SceKernelThreadInfo);
	if (sceKernelReferThreadStatus(thid, &info) == 0) {
		if (strcmp(info.name, "popsmain") == 0) {
			SendAdrenalineCmd(ADRENALINE_VITA_CMD_PAUSE_POPS);
		}
	}

	return sceKernelSuspendThread(thid);
}

int sceKernelResumeThreadPatched(SceUID thid) {
	SceKernelThreadInfo info;
	info.size = sizeof(SceKernelThreadInfo);
	if (sceKernelReferThreadStatus(thid, &info) == 0) {
		if (strcmp(info.name, "popsmain") == 0) {
			SendAdrenalineCmd(ADRENALINE_VITA_CMD_RESUME_POPS);
		}
	}

	return sceKernelResumeThread(thid);
}

static int OnModuleStart(SceModule *mod) {
	static int ready_gamepatch_mod = 0;

	PatchGameInfoGetter(mod);

	char *modname = mod->modname;
	u32 text_addr = mod->text_addr;

	// Game/App module patches
	if (ready_gamepatch_mod) {
		PatchGamesByMod(mod);
		PatchDrmGameModule(mod);
		ready_gamepatch_mod = 0;
	}

	// Third-party Plugins modules
	if (isLoadingPlugins()) {
		// Fix 6.60 plugins on 6.61
		sctrlHENHookImportByNID(mod, "SysMemForKernel", 0x3FC9AE6A, &sctrlHENFakeDevkitVersion, 0);
        sctrlHENHookImportByNID(mod, "SysMemUserForUser", 0x3FC9AE6A, &sctrlHENFakeDevkitVersion, 0);
	}

	if (strcmp(modname, "sceLowIO_Driver") == 0) {
		if (mallocinit() < 0) {
			while (1) {
				VWRITE32(0, 0);
			}
		}

		// Protect pops memory
		if (sceKernelApplicationType() == SCE_APPTYPE_POPS) {
			sceKernelAllocPartitionMemory(6, "", PSP_SMEM_Addr, 0x80000, (void *)0x09F40000);
		}

		memset((void *)0x49F40000, 0, 0x80000);
		memset((void *)0xABCD0000, 0, 0x1B0);

		PatchLowIODriver2(mod);

	} else if (strcmp(mod->modname, "sceController_Service") == 0) {
		PatchController(mod);

	} else if (strcmp(modname, "sceLoadExec") == 0) {
		PatchLoadExec(mod);

		#if defined(DEBUG) && DEBUG >= 3
		extern int (* _runExec)(RunExecParams*);
		extern int runExecPatched(RunExecParams* args);
		HIJACK_FUNCTION(text_addr+0x2148, runExecPatched, _runExec);
		#endif // defined(DEBUG) && DEBUG >= 3

		// Hijack all execute calls
		extern int (* _sceLoadExecVSHWithApitype)(int, const char*, SceKernelLoadExecVSHParam*, unsigned int);
		extern int sctrlKernelLoadExecVSHWithApitype(int apitype, const char * file, SceKernelLoadExecVSHParam * param);
		u32 _LoadExecVSHWithApitype = findFirstJAL(sctrlHENFindFunctionInMod(mod, "LoadExecForKernel", 0xD8320A28));
		HIJACK_FUNCTION(_LoadExecVSHWithApitype, sctrlKernelLoadExecVSHWithApitype, _sceLoadExecVSHWithApitype);

		// Hijack exit calls
		extern int (*_sceKernelExitVSH)(void*);
		extern int sctrlKernelExitVSH(SceKernelLoadExecVSHParam *param);
		u32 _KernelExitVSH = sctrlHENFindFunctionInMod(mod, "LoadExecForKernel", 0x08F7166C);
		HIJACK_FUNCTION(_KernelExitVSH, sctrlKernelExitVSH, _sceKernelExitVSH);

	} else if (strcmp(modname, "scePower_Service") == 0) {
		logmsg3("[INFO]: Built: %s %s\n", __DATE__, __TIME__);
		logmsg3("[INFO]: Boot From: 0x%X\n", sceKernelBootFrom());
		logmsg3("[INFO]: App Type: 0x%X\n", sceKernelApplicationType());
		logmsg3("[INFO]: Apitype: 0x%X\n", sceKernelInitApitype());
		logmsg3("[INFO]: Filename: %s\n", sceKernelInitFileName());

		sctrlSEGetConfig(&config);

		if (sceKernelApplicationType() == SCE_APPTYPE_GAME  && config.force_high_memory != HIGHMEM_OPT_OFF) {
			if (config.force_high_memory == HIGHMEM_OPT_STABLE) {
				sctrlHENSetMemory(40, 9);
			} else if (config.force_high_memory == HIGHMEM_OPT_MAX) {
				sctrlHENSetMemory(52, 0);
			}
			ApplyMemory();
		} else {
			// If not force-high-memory. Make sure to make p11 as big as
			// possible (stable), but in a state that if a game request
			// high-memory, re-setting and re-applying is possible.
			sctrlHENSetMemory(24, 16);
			ApplyAndResetMemory();
		}

		PatchPowerService(mod);
		PatchPowerService2(mod);

	} else if (strcmp(modname, "sceChkreg") == 0) {
		PatchChkreg();

	} else if (strcmp(modname, "sceMesgLed") == 0) {
		REDIRECT_FUNCTION(FindProc("sceMesgLed", "sceResmgr_driver", 0x9DC14891), sceResmgrDecryptIndexPatched);
		sctrlFlushCache();

	} else if (strcmp(modname, "scePspNpDrm_Driver") == 0) {
		PatchNpDrmDriver(mod);

	} else if (strcmp(modname, "sceNp9660_driver") == 0) {
		PatchNp9660Driver(mod);

	} else if (strcmp(modname, "sceUmd_driver") == 0) {
		REDIRECT_FUNCTION(text_addr + 0xC80, sceUmdRegisterUMDCallBackPatched);
		sctrlFlushCache();

	} else if(strcmp(modname, "sceMeCodecWrapper") == 0) {
		HIJACK_FUNCTION(FindProc(modname, "sceMeAudio_driver", 0xC300D466), sceMeAudio_driver_C300D466_Patched, sceMeAudio_driver_C300D466);
		sctrlFlushCache();

	} else if (strcmp(modname, "sceUtility_Driver") == 0) {
		PatchUtility();
		findAndSetGameId();
		logmsg3("[INFO]: Game ID: %s\n", rebootex_config.game_id);
		CheckControllerInput();

	} else if (strcmp(modname, "sceImpose_Driver") == 0) {
		PatchImposeDriver(mod);

	} else if (strcmp(modname, "sceMediaSync") == 0) {
		PatchMediaSync(mod);

	} else if (strcmp(modname, "sceNpSignupPlugin_Module") == 0) {
		// ImageVersion = 0x10000000
		MAKE_INSTRUCTION(text_addr + 0x38CBC, 0x3C041000);
		sctrlFlushCache();

	} else if (strcmp(modname, "sceVshNpSignin_Module") == 0) {
		// Kill connection error
		MAKE_INSTRUCTION(text_addr + 0x6CF4, 0x10000008);

		// ImageVersion = 0x10000000
		MAKE_INSTRUCTION(text_addr + 0x96C4, 0x3C041000);

		sctrlFlushCache();

	} else if (strcmp(modname, "sceSAScore") == 0) {
		PatchSasCore(mod);

	} else if (strcmp(modname, "sceUSBCam_Driver") == 0) {
		PatchUSBCamDriver(mod);

	} else if (strcmp(modname, "vsh_module") == 0) {
		PatchVshForDrm(mod);
		PatchDrmOnVsh();

	} else if (strcmp(modname, "sysconf_plugin_module") == 0) {
		PatchSysconfForDrm(mod);

	} else if (strcmp(modname, "sceKernelLibrary") == 0) { // last kernel module to load before user/game
		ready_gamepatch_mod = 1;
		PatchGameByGameId();

	} else if (strcmp(modname, "VLF_Module") == 0) {
		static u32 nids[] = { 0x2A245FE6, 0x7B08EAAB, 0x22050FC0, 0x158BE61A, 0xD495179F };

		for (int i = 0; i < (sizeof(nids) / sizeof(u32)); i++) {
			sctrlHENHookImportByNID(mod, "VlfGui", nids[i], NULL, 0);
		}

		sctrlFlushCache();

	} else if (strcmp(mod->modname, "CWCHEATPRX") == 0) {
		if (sceKernelApplicationType() == SCE_APPTYPE_POPS) {
			sctrlHENHookImportByNID(mod, "ThreadManForKernel", 0x9944F31F, sceKernelSuspendThreadPatched, 0);
			sctrlHENHookImportByNID(mod, "ThreadManForKernel", 0x75156E8F, sceKernelResumeThreadPatched, 0);
			sctrlFlushCache();
		}
	}

	if (!idle) {
		if (sctrlHENIsSystemBooted()) {
			idle = 1;
			OnSystemStatusIdle();
		}
	}

	logmsg4("%s: [DEBUG]: mod_name=%s — text_addr=0x%08X\n", __func__, modname, text_addr);

	return 0;
}

int module_start(SceSize args, void *argp) {
	logInit("ms0:/log_systemctrl.txt");
	logmsg("SystemControl started...\n")

	PatchSysmem();
	PatchLoadCore();
	PatchInterruptMgr();
	PatchIoFileMgr();
	PatchMemlmd();
	PatchModuleMgr();
	sctrlFlushCache();

	sctrlHENSetStartModuleHandler((STMOD_HANDLER)OnModuleStart);

	UnprotectExtraMemory();

	initAdrenaline();

	memcpy(&rebootex_config, (void *)0x88FB0000, sizeof(RebootexConfig));

	tty_init();

	return 0;
}

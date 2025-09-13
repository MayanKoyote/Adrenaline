/*
	Adrenaline
	Copyright (C) 2016-2018, TheFloW

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

#define _ADRENALINE_LOG_IMPL_
#include <adrenaline_log.h>

#include "virtualpbpmgr.h"
#include "isofs_driver/umd9660_driver.h"
#include "isofs_driver/isofs_driver.h"

PSP_MODULE_INFO("VshControl", 0x1007, 1, 2);

#define BOOT_BIN "disc0:/PSP_GAME/SYSDIR/BOOT.BIN"
#define EBOOT_BIN "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN"
#define EBOOT_OLD "disc0:/PSP_GAME/SYSDIR/EBOOT.OLD"

#define DUMMY_CAT_ISO_EXTENSION "     "

#define MAX_FILES 128

char categorypath[256];
SceUID categorydfd = -1;
int exec_boot_bin = 0;

SceUID gamedfd = -1, isodfd = -1, overiso = 0;
int vpbpinited = 0, isoindex = 0, cachechanged = 0;
VirtualPbp *cache = NULL;

STMOD_HANDLER previous;

AdrenalineConfig config;

void KXploitString(char *str) {
	if (str) {
		char *perc = strchr(str, '%');
		if (perc) {
			strcpy(perc, perc + 1);
		}
	}
}

int CorruptIconPatch(char *name) {
	// Hide ARK bubble launchers
	if (strcasecmp(name, "SCPS10084") == 0 || strcasecmp(name, "NPUZ01234") == 0){
		strcpy(name, "__SCE"); // hide icon
		return 1;
	}

	char path[256];
	sprintf(path, "ms0:/PSP/GAME/%s%%/EBOOT.PBP", name);

	SceIoStat stat;
	memset(&stat, 0, sizeof(stat));
	if (sceIoGetstat(path, &stat) >= 0) {
		strcpy(name, "__SCE"); // hide icon
		return 1;
	}

	return 0;
}

int HideDlc(char *name) {
	char path[256];
	sprintf(path, "ms0:/PSP/GAME/%s/PARAM.PBP", name);

	SceIoStat stat;
	memset(&stat, 0, sizeof(stat));
	if (sceIoGetstat(path, &stat) >= 0) {
		sprintf(path, "ms0:/PSP/GAME/%s/EBOOT.PBP", name);

		memset(&stat, 0, sizeof(stat));
		if (sceIoGetstat(path, &stat) < 0) {
			strcpy(name, "__SCE"); // hide icon
			return 1;
		}
	}

	return 0;
}

int GetIsoIndex(const char *file) {
	char *p = strstr(file, "/MMMMMISO");
	if (!p) {
		return -1;
	}

	char *q = strchr(p + 9, '/');
	if (!q) {
		return strtol(p + 9, NULL, 10);
	}

	char number[5];
	memset(number, 0, 5);
	strncpy(number, p + 9, q - (p + 9));

	return strtol(number, NULL, 10);
}

void CheckControllerInput() {
	SceCtrlData pad_data;
	sceCtrlPeekBufferPositive(&pad_data, 1);
	if ((pad_data.Buttons & PSP_CTRL_RTRIGGER) == PSP_CTRL_RTRIGGER) {
		exec_boot_bin = 1;
		logmsg2("[INFO]: Set to exec BOOT.BIN (if exist) by holding `R` at the ISO application start\n");
	}
}

int LoadExecVSHCommonPatched(int apitype, char *file, SceKernelLoadExecVSHParam *param, int unk2) {
	int k1 = pspSdkSetK1(0);

	VshCtrlSetUmdFile("");

	int index = GetIsoIndex(file);
	if (index >= 0) {
		int has_pboot = 0;
		CheckControllerInput();
		VshCtrlSetUmdFile(virtualpbp_getfilename(index));

		u32 opn_type = virtualpbp_get_isotype(index);
		SceGameInfo *info = sceKernelGetGameInfo();
		if (opn_type) {
			info->opnssmp_ver = opn_type;
		}

		int uses_prometheus = 0;
		int has_bootbin = 0;

		// Execute patched ISOs
		if (isofs_init() < 0) {
			isofs_exit();
			return -1;
		}

		SceIoStat stat;
		if (isofs_getstat("/PSP_GAME/SYSDIR/EBOOT.OLD", &stat) >= 0) {
			uses_prometheus = 1;
		}

		if (isofs_getstat("/PSP_GAME/SYSDIR/BOOT.BIN", &stat) >= 0) {
			has_bootbin = 1;
		}

		isofs_exit();


        if(strstr( param->argp , "PBOOT.PBP") != NULL) {
            has_pboot = 1;
        }

        if (!has_pboot) {
			if (uses_prometheus) {
				param->argp = EBOOT_OLD;
			} else {
				SceIoStat stat;
				if ((config.execute_boot_bin || exec_boot_bin) && has_bootbin) {
					param->argp = BOOT_BIN;
				} else {
					param->argp = EBOOT_BIN;
				}
			}
		}

		// Update path and key
		file = param->argp;
		param->key = "umdemu";

		// Set umd_mode
		if (config.umd_mode == MODE_INFERNO) {
			logmsg2("[INFO]: Launching with Inferno Driver\n");
			sctrlSESetBootConfFileIndex(BOOT_INFERNO);
		} else if (config.umd_mode == MODE_MARCH33) {
			logmsg2("[INFO]: Launching with March33 Driver\n");
			sctrlSESetBootConfFileIndex(BOOT_MARCH33);
		} else if (config.umd_mode == MODE_NP9660) {
			logmsg2("[INFO]: Launching with NP9660 Driver\n");
			sctrlSESetBootConfFileIndex(BOOT_NP9660);
		}

		if (has_pboot) {
			apitype = SCE_APITYPE_UMD_EMU_MS2;
		} else {
			apitype = SCE_APITYPE_UMD_EMU_MS1;
		}

		param->args = strlen(param->argp) + 1; //Update length

		pspSdkSetK1(k1);

		return sctrlKernelLoadExecVSHWithApitype(apitype, file, param);
	}

	// Enable 1.50 homebrews boot
	char *perc = strchr(param->argp, '%');
	if (perc) {
		strcpy(perc, perc + 1);
		file = param->argp;
		param->args = strlen(param->argp) + 1; //Update length
	}

	pspSdkSetK1(k1);

	return sctrlKernelLoadExecVSHWithApitype(apitype, file, param);
}

SceUID sceIoDopenPatched(const char *dirname) {
	int res, game = 0;
	int k1 = pspSdkSetK1(0);

	int index = GetIsoIndex(dirname);
	if (index >= 0) {
		int res = virtualpbp_open(index);
		pspSdkSetK1(k1);
		return res;
	}

	if (strcmp(dirname, "ms0:/PSP/GAME") == 0) {
		game = 1;
	}

	if (strstr(dirname, DUMMY_CAT_ISO_EXTENSION)) {
		char *p = strrchr(dirname, '/');
		if (p) {
			strcpy(categorypath, "ms0:/ISO");
			strcat(categorypath, p);
			categorypath[8 + strlen(p) - 5] = '\0';

			categorydfd = sceIoDopen(categorypath);
			pspSdkSetK1(k1);
			return categorydfd;
		}
	}

	pspSdkSetK1(k1);
	res = sceIoDopen(dirname);
	pspSdkSetK1(0);

	if (game) {
		gamedfd = res;
		overiso = 0;
	}

	pspSdkSetK1(k1);
	return res;
}

void ApplyIsoNamePatch(SceIoDirent *dir) {
	if (dir->d_name[0] != '.') {
		memset(dir->d_name, 0, 256);
		sprintf(dir->d_name, "MMMMMISO%d", isoindex++);
	}
}

int ReadCache() {
	SceUID fd;
	int i;

	if (!cache) {
		cache = (VirtualPbp *)oe_malloc(MAX_FILES * sizeof(VirtualPbp));
	}

	memset(cache, 0, sizeof(VirtualPbp) * MAX_FILES);

	for (i = 0; i < 0x10; i++) {
		fd = sceIoOpen("ms0:/PSP/SYSTEM/isocache2.bin", PSP_O_RDONLY, 0);
		if (fd >= 0) {
			break;
		}
	}

	if (i == 0x10) {
		return -1;
	}

	sceIoRead(fd, cache, sizeof(VirtualPbp) * MAX_FILES);
	sceIoClose(fd);

	return 0;
}

int SaveCache() {
	SceUID fd;
	int i;

	if (!cache) {
		return -1;
	}

	for (i = 0; i < MAX_FILES; i++) {
		if (cache[i].isofile[0] != 0) {
			SceIoStat stat;
			memset(&stat, 0, sizeof(stat));
			if (sceIoGetstat(cache[i].isofile, &stat) < 0) {
				cachechanged = 1;
				memset(&cache[i], 0, sizeof(VirtualPbp));
			}
		}
	}

	if (!cachechanged) {
		return 0;
	}

	cachechanged = 0;

	sceIoMkdir("ms0:/PSP", 0777);
	sceIoMkdir("ms0:/PSP/SYSTEM", 0777);

	for (i = 0; i < 0x10; i++) {
		fd = sceIoOpen("ms0:/PSP/SYSTEM/isocache2.bin", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
		if (fd >= 0) {
			break;
		}
	}

	if (i == 0x10) {
		return -1;
	}

	sceIoWrite(fd, cache, sizeof(VirtualPbp) * MAX_FILES);
	sceIoClose(fd);

	return 0;
}

int IsCached(char *isofile, ScePspDateTime *mtime, VirtualPbp *res) {
	for (int i = 0; i < MAX_FILES; i++) {
		if (cache[i].isofile[0] != 0) {
			if (strcmp(cache[i].isofile, isofile) == 0) {
				if (memcmp(mtime, &cache[i].mtime, sizeof(ScePspDateTime)) == 0) {
					memcpy(res, &cache[i], sizeof(VirtualPbp));
					return 1;
				}
			}
		}
	}

	return 0;
}

int Cache(VirtualPbp *pbp) {
	for (int i = 0; i < MAX_FILES; i++) {
		if (cache[i].isofile[0] == 0) {
			memcpy(&cache[i], pbp, sizeof(VirtualPbp));
			cachechanged = 1;
			return 1;
		}
	}

	return 0;
}

int AddIsoDirent(char *path, SceUID fd, SceIoDirent *dir, int readcategories) {
	int res;

NEXT:
	if ((res = sceIoDread(fd, dir)) > 0) {
		static VirtualPbp vpbp;
		static char fullpath[256];
		int res2 = -1;
		int docache;

		if (!FIO_S_ISDIR(dir->d_stat.st_mode)) {
			strcpy(fullpath, path);
			strcat(fullpath, "/");
			strcat(fullpath, dir->d_name);

			if (IsCached(fullpath, &dir->d_stat.sce_st_mtime, &vpbp)) {
				res2 = virtualpbp_fastadd(&vpbp);
				docache = 0;
			} else {
				res2 = virtualpbp_add(fullpath, &dir->d_stat.sce_st_mtime, &vpbp);
				docache = 1;
			}

			if (res2 >= 0) {
				ApplyIsoNamePatch(dir);

				// Fake the entry from file to directory
				dir->d_stat.st_mode = 0x11FF;
				dir->d_stat.st_attr = 0x0010;
				dir->d_stat.st_size = 0;

				// Change the modifcation time to creation time
				memcpy(&dir->d_stat.sce_st_mtime, &dir->d_stat.sce_st_ctime, sizeof(ScePspDateTime));

				if (docache) {
					Cache(&vpbp);
				}
			}
		} else {
			if (readcategories && dir->d_name[0] != '.' && strcmp(dir->d_name, "VIDEO") != 0) {
				strcat(dir->d_name, DUMMY_CAT_ISO_EXTENSION);
			} else {
				goto NEXT;
			}
		}

		return res;
	}

	return -1;
}

int sceIoDreadPatched(SceUID fd, SceIoDirent *dir) {
	int res;
	int k1 = pspSdkSetK1(0);

	if (vpbpinited) {
		res = virtualpbp_dread(fd, dir);
		if (res >= 0) {
			pspSdkSetK1(k1);
			return res;
		}
	}

	if (fd >= 0) {
		if (fd == gamedfd) {
			if (isodfd < 0 && !overiso) {
				isodfd = sceIoDopen("ms0:/ISO");
				if (isodfd >= 0) {
					if (!vpbpinited) {
						virtualpbp_init();
						vpbpinited = 1;
					} else {
						virtualpbp_reset();
					}

					ReadCache();
					isoindex = 0;
				} else {
					overiso = 1;
				}
			}

			if (isodfd >= 0) {
				res = AddIsoDirent("ms0:/ISO", isodfd, dir, 1);
				if (res > 0) {
					pspSdkSetK1(k1);
					return res;
				}
			}
		} else if (fd == categorydfd) {
			res = AddIsoDirent(categorypath, categorydfd, dir, 0);
			if (res > 0) {
				pspSdkSetK1(k1);
				return res;
			}
		}
	}

	res = sceIoDread(fd, dir);

	if (res > 0) {
		if (config.hide_corrupt) {
			CorruptIconPatch(dir->d_name);
		}

		if (config.hide_dlcs) {
			HideDlc(dir->d_name);
		}
	}

	pspSdkSetK1(k1);
	return res;
}

int sceIoDclosePatched(SceUID fd) {
	int k1 = pspSdkSetK1(0);
	int res;

	if (vpbpinited) {
		res = virtualpbp_close(fd);
		if (res >= 0) {
			pspSdkSetK1(k1);
			return res;
		}
	}

	if (fd == categorydfd) {
		categorydfd = -1;
	} else if (fd == gamedfd) {
		sceIoDclose(isodfd);
		isodfd = -1;
		gamedfd = -1;
		overiso = 0;
		SaveCache();
	}

	res = sceIoDclose(fd);
	pspSdkSetK1(k1);
	return res;
}

SceUID sceIoOpenPatched(const char *file, int flags, SceMode mode) {
	int k1 = pspSdkSetK1(0);

	int res = SCE_ENOENT;
	int index = GetIsoIndex(file);
	if (index >= 0) {
		if (strcmp(strrchr(file,'/')+1, "DOCUMENT.DAT") == 0) {
			char path[97];
			sprintf(path, "%s", virtualpbp_getfilename(index));
			((char*)path)[strlen(path)-3] = 'D';
			((char*)path)[strlen(path)-2] = 'A';
			((char*)path)[strlen(path)-1] = 'T';
			res = sceIoOpen(path, flags, mode);
			pspSdkSetK1(k1);
			return res;
		}

		if (strcmp(strrchr(file,'/')+1, "PARAM.PBP") ==0 || strcmp(strrchr(file,'/')+1, "PBOOT.PBP") == 0) {
			char path[255];
			strcpy(path, file);
			virtualpbp_fixisopath(index, path);
			res = sceIoOpen(path, flags, mode);
			pspSdkSetK1(k1);
			return res;
		}

		int res = virtualpbp_open(index);
		pspSdkSetK1(k1);
		return res;
	}

	pspSdkSetK1(k1);
	res = sceIoOpen(file, flags, mode);
	return res;
}

int sceIoClosePatched(SceUID fd) {
	int k1 = pspSdkSetK1(0);
	int res = -1;

	if (vpbpinited) {
		res = virtualpbp_close(fd);
	}


	if (res < 0) {
		return sceIoClose(fd);
	}

	pspSdkSetK1(k1);
	return res;
}

int sceIoReadPatched(SceUID fd, void *data, SceSize size) {
	int k1 = pspSdkSetK1(0);
	int res = -1;

	if (vpbpinited) {
		res = virtualpbp_read(fd, data, size);
	}


	if (res < 0) {
		return sceIoRead(fd, data, size);
	}

	pspSdkSetK1(k1);
	return res;
}

SceOff sceIoLseekPatched(SceUID fd, SceOff offset, int whence) {
	int k1 = pspSdkSetK1(0);
	int res = -1;

	if (vpbpinited) {
		res = virtualpbp_lseek(fd, offset, whence);
	}


	if (res < 0) {
		return sceIoLseek(fd, offset, whence);
	}

	pspSdkSetK1(k1);
	return res;
}

int sceIoLseek32Patched(SceUID fd, int offset, int whence) {
	int k1 = pspSdkSetK1(0);
	int res = -1;

	if (vpbpinited) {
		res = virtualpbp_lseek(fd, offset, whence);
	}


	if (res < 0) {
		return sceIoLseek32(fd, offset, whence);
	}

	pspSdkSetK1(k1);
	return res;
}

int sceIoGetstatPatched(const char *file, SceIoStat *stat) {
	int k1 = pspSdkSetK1(0);
	int index = GetIsoIndex(file);
	if (index >= 0) {
		if (strcmp(strrchr(file,'/')+1, "DOCUMENT.DAT") == 0) {
			char path[97];
			sprintf(path, "%s", virtualpbp_getfilename(index));
			((char*)path)[strlen(path)-3] = 'D';
			((char*)path)[strlen(path)-2] = 'A';
			((char*)path)[strlen(path)-1] = 'T';
			pspSdkSetK1(k1);
			return sceIoGetstat(path, stat);
		}

		if (strcmp(strrchr(file,'/')+1, "DOCINFO.EDAT") == 0) {
			pspSdkSetK1(k1);
			return SCE_ENOENT;
		}

		if (strcmp(strrchr(file,'/')+1, "PARAM.PBP") == 0 || strcmp(strrchr(file,'/')+1, "PBOOT.PBP") == 0 ) {
			char path[255];
			strcpy(path, file);
			virtualpbp_fixisopath(index, path);
			int res = sceIoGetstat(path, stat);
			logmsg("%s: file=%s -> 0x%08X\n", __func__, path, res);
			pspSdkSetK1(k1);
			return res;
		}

		int res = virtualpbp_getstat(index, stat);
		pspSdkSetK1(k1);
		return res;
	}

	int game = 0;
	if (strcmp(file, "ms0:/PSP/GAME") == 0) {
		game = 1;
	}


	int res = sceIoGetstat(file, stat);

	if (game && res < 0) {
		sceIoMkdir("ms0:/PSP", 0777);
		sceIoMkdir("ms0:/PSP/GAME", 0777);

		res = sceIoGetstat(file, stat);
	}

	pspSdkSetK1(k1);
	return res;
}

int sceIoChstatPatched(const char *file, SceIoStat *stat, int bits) {
	int k1 = pspSdkSetK1(0);

	int res = SCE_EINVAL;
	int index = GetIsoIndex(file);
	if (index >= 0) {
		res = virtualpbp_chstat(index, stat, bits);
		pspSdkSetK1(k1);
		return res;
	}

	res = sceIoChstat(file, stat, bits);
	pspSdkSetK1(k1);
	return res;
}

int sceIoRemovePatched(const char *file) {
	int k1 = pspSdkSetK1(0);

	int res;
	int index = GetIsoIndex(file);
	if (index >= 0) {
		res = virtualpbp_remove(index);
		pspSdkSetK1(k1);
		return res;
	}

	res = sceIoRemove(file);
	pspSdkSetK1(k1);
	return res;
}

int sceIoRmdirPatched(const char *path) {
	int k1 = pspSdkSetK1(0);

	int res;
	int index = GetIsoIndex(path);
	if (index >= 0) {
		res = virtualpbp_rmdir(index);
		pspSdkSetK1(k1);
		return res;
	}

	res = sceIoRmdir(path);
	pspSdkSetK1(k1);
	return res;
}

int sceIoMkdirPatched(const char *dir, SceMode mode) {
	int k1 = pspSdkSetK1(0);

	if (strcmp(dir, "ms0:/PSP/GAME") == 0) {
		sceIoMkdir("ms0:/ISO", mode);
	}

	int res = sceIoMkdir(dir, mode);
	pspSdkSetK1(k1);
	return res;
}

///////////////////////////////////////////////////////

u32 firsttick;
u8 set;

int (* vshmenu_ctrl)(SceCtrlData *pad_data, int count);

SceUID modid = -1;

int cpu_list[] = { 0, 20, 75, 100, 133, 222, 266, 300, 333 };
int bus_list[] = { 0, 10, 37,  50,  66, 111, 133, 150, 166 };

#define N_CPU (sizeof(cpu_list) / sizeof(int))

int sceCtrlReadBufferPositivePatched(SceCtrlData *pad_data, int count) {
	int res = sceCtrlReadBufferPositive(pad_data, count);
	int k1 = pspSdkSetK1(0);

	if (!set && config.vsh_cpu_speed != 0) {
		u32 curtick = sceKernelGetSystemTimeLow();
		curtick -= firsttick;

		u32 t = (u32)curtick;
		if (t >= (10 * 1000 * 1000)) {
			set = 1;
			SetSpeed(cpu_list[config.vsh_cpu_speed % N_CPU], bus_list[config.vsh_cpu_speed % N_CPU]);
		}
	}

	if (!sceKernelFindModuleByName("EPI-VshCtrlSatelite")) {
		if (pad_data->Buttons & PSP_CTRL_SELECT) {
			if (!sceKernelFindModuleByName("htmlviewer_plugin_module")
				&& !sceKernelFindModuleByName("sceVshOSK_Module")
				&& !sceKernelFindModuleByName("camera_plugin_module")) {
				modid = sceKernelLoadModule("flash0:/vsh/module/satelite.prx", 0, NULL);

				if (modid >= 0) {
					sceKernelDelayThread(4000);
					sceKernelStartModule(modid, 0, NULL, NULL, NULL);
					pad_data->Buttons &= ~PSP_CTRL_SELECT;
				}
			}
		}
	} else {
		if (vshmenu_ctrl) {
			vshmenu_ctrl(pad_data, count);
		} else if (modid >= 0) {
			if (sceKernelStopModule(modid, 0, NULL, NULL, NULL) >= 0) {
				sceKernelUnloadModule(modid);
			}
		}
	}

	pspSdkSetK1(k1);
	return res;
}

int vctrlVSHRegisterVshMenu(int (* ctrl)(SceCtrlData *, int)) {
	int k1 = pspSdkSetK1(0);
	vshmenu_ctrl = (void *)((u32)ctrl | 0x80000000);
	pspSdkSetK1(k1);
	return 0;
}

int vctrlVSHExitVSHMenu(AdrenalineConfig *conf) {
	int k1 = pspSdkSetK1(0);
	int oldspeed = config.vsh_cpu_speed;

	vshmenu_ctrl = NULL;
	memcpy(&config, conf, sizeof(AdrenalineConfig));
	sctrlSEApplyConfig(&config);

	if (set) {
		if (config.vsh_cpu_speed != oldspeed) {
			if (config.vsh_cpu_speed) {
				SetSpeed(cpu_list[config.vsh_cpu_speed % N_CPU], bus_list[config.vsh_cpu_speed % N_CPU]);
			} else {
				SetSpeed(222, 111);
			}
		}
	}

	pspSdkSetK1(k1);
	return 0;
}

int sceKernelQuerySystemCall(void *function);

u32 MakeSyscallStub(void *function) {
	SceUID block_id = sceKernelAllocPartitionMemory(PSP_MEMORY_PARTITION_USER, "", PSP_SMEM_High, 2 * sizeof(u32), NULL);
	u32 stub = (u32)sceKernelGetBlockHeadAddr(block_id);
	MAKE_SYSCALL_FUNCTION(stub, sceKernelQuerySystemCall(function));
	return stub;
}

int InitUsbPatched() {
	return sctrlStartUsb();
}

int ShutdownUsbPatched() {
	return sctrlStopUsb();
}

int GetUsbStatusPatched() {
	int state = sctrlGetUsbState();

	if (state & 0x20)
		return 1; // Connected

	return 2; // Not connected
}

typedef struct {
	void *import;
	void *patched;
} ImportPatch;

ImportPatch import_patch[] = {
	// Directory functions
	{ &sceIoDopen, sceIoDopenPatched },
	{ &sceIoDread, sceIoDreadPatched },
	{ &sceIoDclose, sceIoDclosePatched },

	// File functions
	{ &sceIoOpen, sceIoOpenPatched },
	{ &sceIoClose, sceIoClosePatched },
	{ &sceIoRead, sceIoReadPatched },
	{ &sceIoLseek, sceIoLseekPatched },
	{ &sceIoLseek32, sceIoLseek32Patched },
	{ &sceIoGetstat, sceIoGetstatPatched },
	{ &sceIoChstat, sceIoChstatPatched },
	{ &sceIoRemove, sceIoRemovePatched },
	{ &sceIoRmdir, sceIoRmdirPatched },
	{ &sceIoMkdir, sceIoMkdirPatched },
};

void IoPatches() {
	for (int i = 0; i < (sizeof(import_patch) / sizeof(ImportPatch)); i++) {
		sctrlHENPatchSyscall(K_EXTRACT_IMPORT(import_patch[i].import), import_patch[i].patched);
	}
}

void PatchVshMain(u32 text_addr) {
	// Allow old sfo's
	MAKE_NOP(text_addr + 0x122B0);
	MAKE_NOP(text_addr + 0x12058); //DISC_ID
	MAKE_NOP(text_addr + 0x12060); //DISC_ID

	IoPatches();

	SceModule *mod = sceKernelFindModuleByName("sceVshBridge_Driver");
	sctrlHENHookImportByNID(mod, "sceCtrl_driver", 0xBE30CED0, sceCtrlReadBufferPositivePatched, 0);
	sctrlHENPatchSyscall(K_EXTRACT_IMPORT(&sceCtrlReadBufferPositive), sceCtrlReadBufferPositivePatched);

	// Dummy usb detection functions
	// Those break camera, but doesn't seem to affect usb connection
//	MAKE_DUMMY_FUNCTION(text_addr + 0x38C94, 0);
//	MAKE_DUMMY_FUNCTION(text_addr + 0x38D68, 0);

	if (config.skip_game_boot_logo) {
		// Disable sceDisplaySetHoldMode
		MAKE_NOP(text_addr + 0xCA88);
	}

	if (config.extended_colors == 1) {
		VWRITE16(text_addr + 0x3174A, 0x1000);
	}

	sctrlFlushCache();
}

//wchar_t verinfo[] = L"6.61 Adrenaline-     ";
wchar_t macinfo[] = L"00:00:00:00:00:00";

// Credits: ARK-4
static inline void ascii2utf16(char *dest, const char *src) {
    while(*src != '\0') {
        *dest++ = *src;
        *dest++ = '\0';
        src++;
    }
    *dest++ = '\0';
    *dest++ = '\0';
}

int SetDefaultNicknamePatched() {
    int k1 = pspSdkSetK1(0);

    struct RegParam reg;
    REGHANDLE h;
    memset(&reg, 0, sizeof(reg));
    reg.regtype = 1;
    reg.namelen = strlen("flash1:/registry/system");
    reg.unk2 = 1;
    reg.unk3 = 1;
    strcpy(reg.name, "flash1:/registry/system");

    if(sceRegOpenRegistry(&reg, 2, &h) == 0) {
        REGHANDLE hd;
        if(!sceRegOpenCategory(h, "/CONFIG/SYSTEM", 2, &hd)) {
            char* name = (char*)0xa83ff050;
            if(sceRegSetKeyValue(hd, "owner_name", name, strlen(name)) == 0) {
                printf("Set registry value\n");
                sceRegFlushCategory(hd);
            }
            sceRegCloseCategory(hd);
        }
        sceRegFlushRegistry(h);
        sceRegCloseRegistry(h);
    }

    pspSdkSetK1(k1);
    return 0;
}

void PatchSysconfPlugin(u32 text_addr) {
	int version = sctrlSEGetVersion();
	int version_major = version >> 24;
	int version_minor = version >> 16 & 0xFF;
	int version_micro = version >> 8 & 0xFF;

	char verinfo[50] = {0};
	sprintf(verinfo, "6.61 Adrenaline-%d.%d.%d", version_major, version_minor, version_micro );

	ascii2utf16( (char*)((void *)text_addr + 0x2A62C), verinfo);

	MAKE_INSTRUCTION(text_addr + 0x192E0, 0x3C020000 | ((u32)(text_addr + 0x2A62C) >> 16));
	MAKE_INSTRUCTION(text_addr + 0x192E4, 0x34420000 | ((u32)(text_addr + 0x2A62C) & 0xFFFF));

	if (config.hide_mac_addr) {
		memcpy((void *)text_addr + 0x2E9A0, macinfo, sizeof(macinfo));
	}

	// Allow slim colors
	if (config.extended_colors != 0) {
		MAKE_INSTRUCTION(text_addr + 0x76EC, VREAD32(text_addr + 0x76F0));
		MAKE_INSTRUCTION(text_addr + 0x76F0, LI_V0(1));
	}

	// Dummy all vshbridge usbstor functions
	MAKE_INSTRUCTION(text_addr + 0xCD78, LI_V0(1));   // sceVshBridge_ED978848 - vshUsbstorMsSetWorkBuf
	MAKE_INSTRUCTION(text_addr + 0xCDAC, MOVE_V0_ZR); // sceVshBridge_EE59B2B7
	MAKE_INSTRUCTION(text_addr + 0xCF0C, MOVE_V0_ZR); // sceVshBridge_6032E5EE - vshUsbstorMsSetProductInfo
	MAKE_INSTRUCTION(text_addr + 0xD218, MOVE_V0_ZR); // sceVshBridge_360752BF - vshUsbstorMsSetVSHInfo

	// Dummy LoadUsbModules, UnloadUsbModules
	MAKE_DUMMY_FUNCTION(text_addr + 0xCC70, 0);
	MAKE_DUMMY_FUNCTION(text_addr + 0xD2C4, 0);

	// Redirect USB functions
	REDIRECT_FUNCTION(text_addr + 0xAE9C, MakeSyscallStub(InitUsbPatched));
	REDIRECT_FUNCTION(text_addr + 0xAFF4, MakeSyscallStub(ShutdownUsbPatched));
	REDIRECT_FUNCTION(text_addr + 0xB4A0, MakeSyscallStub(GetUsbStatusPatched));

	// Ignore wait thread end failure
	MAKE_NOP(text_addr + 0xB264);

	// Do not set nickname to PXXX on initial setup/reset
	REDIRECT_FUNCTION(text_addr + 0x1520, MakeSyscallStub(SetDefaultNicknamePatched));

	sctrlFlushCache();
}

void PatchGamePlugin(u32 text_addr) {
	// Allow homebrew launch
	MAKE_DUMMY_FUNCTION(text_addr + 0x20528, 0);

	// Allow PSX launch
	MAKE_DUMMY_FUNCTION(text_addr + 0x20E6C, 0);

	// Allow custom multi-disc PSX
	MAKE_NOP(text_addr + 0x14850);

	// if check patch
	MAKE_INSTRUCTION(text_addr + 0x20620, MOVE_V0_ZR);

	if (config.hide_pic0pic1 != PICS_OPT_DISABLED) {
		if (config.hide_pic0pic1 == PICS_OPT_BOTH || config.hide_pic0pic1 == PICS_OPT_PIC0_ONLY) {
			MAKE_INSTRUCTION(text_addr + 0x1D858, 0x00601021);
		}

		if (config.hide_pic0pic1 == PICS_OPT_BOTH || config.hide_pic0pic1 == PICS_OPT_PIC1_ONLY) {
			MAKE_INSTRUCTION(text_addr + 0x1D864, 0x00601021);
		}
	}

	if (config.skip_game_boot_logo) {
		MAKE_CALL(text_addr + 0x19130, text_addr + 0x194B0);
		MAKE_INSTRUCTION(text_addr + 0x19134, 0x24040002);
	}

	sctrlFlushCache();
}

int sceUpdateDownloadSetVersionPatched(int version) {
	int k1 = pspSdkSetK1(0);

	int (* sceUpdateDownloadSetVersion)(int version) = (void *)FindProc("SceUpdateDL_Library", "sceLibUpdateDL", 0xC1AF1076);
	int (* sceUpdateDownloadSetUrl)(const char *url) = (void *)FindProc("SceUpdateDL_Library", "sceLibUpdateDL", 0xF7E66CB4);

	sceUpdateDownloadSetUrl("http://adrenaline.sarcasticat.com/psp-updatelist.txt");
	int res = sceUpdateDownloadSetVersion(sctrlSEGetVersion());

	pspSdkSetK1(k1);
	return res;
}

void PatchUpdatePlugin(u32 text_addr) {
	MAKE_CALL(text_addr + 0x82A8, MakeSyscallStub(sceUpdateDownloadSetVersionPatched));
	sctrlFlushCache();
}

int OnModuleStart(SceModule *mod) {
	char *modname = mod->modname;
	u32 text_addr = mod->text_addr;

	if (strcmp(modname, "vsh_module") == 0) {
		PatchVshMain(text_addr);
	} else if (strcmp(modname, "sysconf_plugin_module") == 0) {
		PatchSysconfPlugin(text_addr);
	} else if (strcmp(modname, "game_plugin_module") == 0) {
		PatchGamePlugin(text_addr);
	} else if (strcmp(modname, "update_plugin_module") == 0) {
		PatchUpdatePlugin(text_addr);
	}

	if (!previous)
		return 0;

	return previous(mod);
}

int module_start(SceSize args, void *argp) {
	logInit("ms0:/log_vshctrl.txt");
	logmsg("VshCtrl started\n");

	SceModule *mod = sceKernelFindModuleByName("sceLoadExec");
	u32 text_addr = mod->text_addr;

	MAKE_CALL(text_addr + 0x1DC0, LoadExecVSHCommonPatched); //sceKernelLoadExecVSHMs2
	MAKE_CALL(text_addr + 0x1BE0, LoadExecVSHCommonPatched); //sceKernelLoadExecVSHMsPboot
	MAKE_CALL(text_addr + 0x1BB8, LoadExecVSHCommonPatched); //sceKernelLoadExecVSHUMDEMUPboot

	sctrlSEGetConfig(&config);

	if (config.vsh_cpu_speed != 0) {
		firsttick = sceKernelGetSystemTimeLow();
	}

	sctrlFlushCache();

	previous = sctrlHENSetStartModuleHandler(OnModuleStart);

	return 0;
}
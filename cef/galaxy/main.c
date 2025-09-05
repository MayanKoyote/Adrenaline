/*
	Adrenaline Galaxy Controller
	Copyright (C) 2025, GrayJack
	Copyright (C) 2021, PRO CFW
	Copyright (C) 2008, M33 Team Developers (Dark_Alex, adrahil, Mathieulh)

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

#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspthreadman_kernel.h>
#include <psputilsforkernel.h>

#include <stdio.h>
#include <string.h>

#include <systemctrl.h>
#include <systemctrl_se.h>
#include <iso_common.h>
#include <macros.h>

#define _ADRENALINE_LOG_IMPL_
#include <adrenaline_log.h>

#include "galaxy.h"

PSP_MODULE_INFO("EPI-GalaxyController", 0x1006, 1, 0);

// SceNpUmdMount Thread ID
SceUID g_SceNpUmdMount_thid = -1;

// np9660.prx Text Address
unsigned int g_sceNp9660_driver_text_addr = 0;

// Dummy UMD Data for Global Pointer
unsigned char g_umddata[16] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

// NP9660 Initialization Call
void (* initNP9660)(unsigned int zero) = NULL;

// NP9660 Disc Sector Reader
int (* readDiscSectorNP9660)(unsigned int sector, unsigned char * buffer, unsigned int size) = NULL;

// NP9660 Sector Flush
void (* readSectorFlushNP9660)(void) = NULL;

// Set Global Pointer
int sceKernelSetQTGP3(void *data);

u32 findRefInGlobals(char* libname, u32 addr, u32 ptr) {
	while (strcmp(libname, (char*)addr)) {
		addr++;
	}

	if (addr%4) {
		addr &= -0x4; // align to 4 bytes
	}

	while (VREAD32(addr += 4) != ptr);

	return addr;
}


// Open ISO File
int open_iso(void) {
	int res = iso_open();

	if (res < 0) {
		return res;
	}

	// Update NP9660 File Descriptor Variable
	VWRITE32(g_sceNp9660_driver_text_addr + NP9660_ISO_FD, g_iso_fd);

	return 0;
}

int readDiscSectorNP9660Patched(unsigned int sector, unsigned char * buffer, unsigned int size) {
	// This will fail but it sets some needed variables
	readDiscSectorNP9660(sector, buffer, size);

	// Prepare Arguments for our custom ISO Sector Reader
	IoReadArg args;
	args.offset = sector;
	args.address = buffer;
	args.size = size;

	int res = sceKernelExtendKernelStack(0x2000, (void *)iso_read, &args);

	// Flush & Update NP9660 Sector Information
	readSectorFlushNP9660();

	return res;
}

int sceIoClosePatched(int fd) {
	int res = sceIoClose(fd);

	if(fd == g_iso_fd) {
		g_iso_fd = -1;

		// Remove File Descriptor
		VWRITE32(g_sceNp9660_driver_text_addr + NP9660_ISO_FD, -1);

		sctrlFlushCache();
	}

	return res;
}

// Init ISO Emulator
int initEmulator(void) {
	static int firstrun = 1;

	initNP9660(0);
	open_iso();

	// Suspend interrupts
	int interrupts = sceKernelCpuSuspendIntr();

	// sceUmdManGetUmdDiscInfo Patch
	VWRITE32(g_sceNp9660_driver_text_addr + NP9660_DATA_1, 0xE0000800);
	VWRITE32(g_sceNp9660_driver_text_addr + NP9660_DATA_2, 9);
	VWRITE32(g_sceNp9660_driver_text_addr + NP9660_DATA_3, g_total_sectors);
	VWRITE32(g_sceNp9660_driver_text_addr + NP9660_DATA_4, g_total_sectors);
	VWRITE32(g_sceNp9660_driver_text_addr + NP9660_DATA_5, 0);

	// Resume interrupts
	sceKernelCpuResumeIntr(interrupts);

	// First run delay
	if (firstrun) {
		sceKernelDelayThread(800000);
		firstrun = 0;
	}

	sctrlFlushCache();

	// Set global ptr for UMD Data
	sceKernelSetQTGP3(g_umddata);

	return 0;
}

int sceKernelStartThreadPatched(SceUID thid, SceSize arglen, void * argp) {
	// SceNpUmdMount Thread
	if(g_SceNpUmdMount_thid == thid) {
		SceModule * pMod = sceKernelFindModuleByName("sceNp9660_driver");
		g_sceNp9660_driver_text_addr = pMod->text_addr;

		// Make InitForKernel Call return 0x80000000 always
		MAKE_INSTRUCTION(g_sceNp9660_driver_text_addr + 0x3C5C, 0x3C028000); // jal InitForKernel_23458221 to lui $v0, 0x00008000

		// Hook Function #1
		MAKE_CALL(g_sceNp9660_driver_text_addr + 0x3C78, initEmulator);

		// Hook Function #2
		MAKE_CALL(g_sceNp9660_driver_text_addr + 0x4414, readDiscSectorNP9660Patched); // jal sub_3948 to jal sub_00000054
		MAKE_CALL(g_sceNp9660_driver_text_addr + 0x596C, readDiscSectorNP9660Patched); // jal sub_3948 to jal sub_00000054

		// Hook sceIoClose Stub
		MAKE_JUMP(g_sceNp9660_driver_text_addr + 0x7D68, sceIoClosePatched); // hook sceIoClose import

		// Backup Function Pointer
		initNP9660 = (void *)(pMod->text_addr + 0x36A8);
		readDiscSectorNP9660 = (void *)(pMod->text_addr + 0x4FEC);
		readSectorFlushNP9660 = (void *)(pMod->text_addr + 0x505C);

		sctrlFlushCache();
	}

	return sceKernelStartThread(thid, arglen, argp);
}

SceUID sceKernelCreateThreadPatched(const char * name, SceKernelThreadEntry entry, int initPriority, int stackSize, SceUInt attr, SceKernelThreadOptParam * option) {
	SceUID thid = sceKernelCreateThread(name, entry, initPriority, stackSize, attr,	option);

	if(strncmp(name, "SceNpUmdMount", 13) == 0) {
		// Save Thread ID
		g_SceNpUmdMount_thid = thid;
	}

	return thid;
}

u32 findReferenceInGlobalScope(u32 ref){
	u32 addr = ref;
	while (strcmp("ThreadManForKernel", (char*)addr)) {
		addr++;
	}
	addr+=18;

	if (addr%4) {
		addr &= -0x4;
	}

	while (VREAD32(addr += 4) != ref);

	return addr;
}

// Entry Point
int module_start(SceSize args, void* argp) {
	logInit("ms0:/log_galaxy.txt");
	logmsg("Galaxy driver started...\n")

	// Get ISO path
	memset(g_iso_fn, 0, sizeof(g_iso_fn));
	strncpy(g_iso_fn, sctrlSEGetUmdFile(), sizeof(g_iso_fn)-1);
	logmsg3("[INFO] UMD File: %s\n", g_iso_fn);

	// Leave NP9660 alone, we got no ISO
	if(g_iso_fn[0] == 0) {
		return 1;
	}

	SceModule * pMod = (SceModule *)sceKernelFindModuleByName("sceThreadManager");

	if(pMod != NULL) {
		u32 ref;
		// Hook sceKernelCreateThread
		ref = K_EXTRACT_CALL(&sceKernelCreateThread);
		VWRITE32(findRefInGlobals("ThreadManForKernel", ref, ref), (u32)sceKernelCreateThreadPatched);

		// Hook sceKernelStartThread
		ref = K_EXTRACT_CALL(&sceKernelStartThread);
		VWRITE32(findRefInGlobals("ThreadManForKernel", ref, ref), (u32)sceKernelStartThreadPatched);
	}

	sctrlFlushCache();

	// ISO File Descriptor
	int fd = -1;

	// Wait for MS
	while (1) {
		fd = sceIoOpen(g_iso_fn, PSP_O_RDONLY, 0);

		if (fd >= 0) {
			break;
		}

		// Delay and retry
		sceKernelDelayThread(10000);
	}

	sceIoClose(fd);

	return 0;
}


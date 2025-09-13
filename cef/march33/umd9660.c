#include <pspsdk.h>
#include <pspkernel.h>
#include <pspthreadman_kernel.h>
#include <psperror.h>
#include <pspsysevent.h>

#include <string.h>
#include <systemctrl_se.h>
#include <adrenaline_log.h>

#include <iso_common.h>
#include "malloc.h"
#include "umd9660.h"

int game_group = 0;
SceUID umdsema = -1;
int discsize=0x7FFFFFFF;
UmdFD descriptors[MAX_DESCRIPTORS];
u8 *umdpvd = NULL;

static u8 umd_seek = 0;
static u8 umd_speed = 0;
static u32 cur_offset = 0;
static u32 last_read_offset = 0;

#define N_GAME_GROUP1	4
#define N_GAME_GROUP2	1

char *game_group1[N_GAME_GROUP1] = {
	"ULES-00124", "ULUS-10019", "ULJM-05024", "ULAS-42009" // Coded Arms
};

char *game_group2[N_GAME_GROUP2] = {
	"TERTURADOR" // NPUG-80086 (flow PSN)
};

#define LOCK() \
	if (sceKernelWaitSema(umdsema, 1, NULL) < 0) \
		return -1;

#define UNLOCK() \
	if (sceKernelSignalSema(umdsema, 1) < 0) \
		return -1;


// iso_read_with_stack
static int ReadUmdFile(int offset, void *buf, int outsize) {
	LOCK();

	g_read_arg.offset = offset;
	g_read_arg.address = buf;
	g_read_arg.size = outsize;

	int res = sceKernelExtendKernelStack(0x2000, (void *)iso_read, &g_read_arg);

	UNLOCK();


	if (umd_seek){
		// simulate seek time
		u32 diff = 0;
		last_read_offset = offset+outsize;
		if (cur_offset > last_read_offset) {
			diff = cur_offset-last_read_offset;
		} else {
			diff = last_read_offset-cur_offset;
		}
		cur_offset = last_read_offset;
		u32 seek_time = (diff*umd_seek)/1024;
		sceKernelDelayThread(seek_time);
	}
	if (umd_speed){
		// simulate read time
		sceKernelDelayThread(outsize*umd_speed);
	}

	return res;
}

void isoSetUmdDelay(int seek, int speed) {
	umd_seek = seek;
	umd_speed = speed;
}

int umd_init(PspIoDrvArg* arg) {
	int i;
	logmsg("%s: start.\n", __func__);

	umdpvd = (u8 *)oe_malloc(SECTOR_SIZE);

	if (!umdpvd) {
		return -1;
	}

	umdsema = sceKernelCreateSema("EcsUmd9660DeviceFile", 0, 1, 1, NULL);

	if (umdsema < 0) {
		return umdsema;
	}

	while (!g_iso_opened) {
		logmsg("%s: Attempting to open iso.\n", __func__);
		iso_open();
		sceKernelDelayThread(20000);
	}

	memset(&descriptors, 0, sizeof(descriptors));

	g_read_arg.offset = 0x10*SECTOR_SIZE;
	g_read_arg.address = umdpvd;
	g_read_arg.size = SECTOR_SIZE;

	iso_read(&g_read_arg);

	char *gamecode = (char *)umdpvd+0x373;

	for (i = 0; i < N_GAME_GROUP1; i++) {
		if (memcmp(gamecode, game_group1[i], 10) == 0) {
			game_group = 1;
			break;
		}
	}

	if (game_group == 0) {
		for (i = 0; i < N_GAME_GROUP2; i++) {
			if (memcmp(gamecode, game_group2[i], 10) == 0) {
				game_group = 2;
				break;
			}
		}
	}

	logmsg("%s: end.\n", __func__);
	return 0;
}

int umd_exit(PspIoDrvArg* arg) {
	logmsg("%s: start.\n", __func__);
	SceUInt timeout = 500000;

	sceKernelWaitSema(umdsema, 1, &timeout);

	if (umdpvd) {
		oe_free(umdpvd);
		umdpvd = NULL;
	}

	if (umdsema >= 0) {
		sceKernelDeleteSema(umdsema);
		umdsema = -1;
	}

	logmsg("%s: end.\n", __func__);
	return 0;
}

int umd_mount(PspIoDrvFileArg *arg) {
	return 0;
}

int umd_umount(PspIoDrvFileArg *arg) {
	return 0;
}

int umd_open(PspIoDrvFileArg *arg, char *file, int flags, SceMode mode) {
	int res = 0;
	int i;
	for (i = 0; i < 0x10; i++) {
		if (sceIoLseek32(g_iso_fd, 0, PSP_SEEK_SET) < 0) {
			iso_open();
		} else {
			break;
		}
	}

	if (i == 0x10) {
		res = SCE_ENODEV;
		goto out;
	}

	LOCK();

	for (i = 0; i < MAX_DESCRIPTORS; i++) {
		if (!descriptors[i].busy) {
			break;
		}
	}

	if (i == MAX_DESCRIPTORS) {
		UNLOCK();
		res = SCE_EMFILE;
		goto out;
	}

	arg->arg = (void *)i;
	descriptors[i].busy = 1;
	descriptors[i].discpointer = 0;

	UNLOCK();

out:
	logmsg("%s: arg=0x%p, file=%s, flags=0x%08X, mode=0x%08X -> 0x%08X\n", __func__, arg, file, flags, mode, res);
	return res;
}

int umd_close(PspIoDrvFileArg *arg) {
	int res = 0;
	int i = (int)arg->arg;

	LOCK();

	if (!descriptors[i].busy) {
		res = SCE_EINVAL;
	} else {
		descriptors[i].busy = 0;
	}

	UNLOCK();

	logmsg("%s: arg=0x%p -> 0x%08X\n", __func__, arg, res);
	return res;
}

int umd_read(PspIoDrvFileArg *arg, char *data, int len) {
	int i = (int)arg->arg;

	LOCK();

	int discpointer = descriptors[i].discpointer;

	UNLOCK();

	if (discpointer + len > discsize) {
		len = discsize - discpointer;
	}

	int res = ReadUmdFile(discpointer*SECTOR_SIZE, data, len*SECTOR_SIZE); //***

	if (res > 0) {
		res = res / SECTOR_SIZE;

		LOCK();
		descriptors[i].discpointer += res;
		UNLOCK();
	}

	logmsg("%s: arg=0x%p, data=%s, len=0x%08X -> 0x%08X\n", __func__, arg, data, len, res);
	return res;
}

SceOff umd_lseek(PspIoDrvFileArg *arg, SceOff ofs, int whence) {
	int i = (int)arg->arg;

	LOCK();

	if (whence == PSP_SEEK_SET) {
		descriptors[i].discpointer = ofs;
	} else if (whence == PSP_SEEK_CUR) {
		descriptors[i].discpointer += ofs;
	} else if (whence == PSP_SEEK_END) {
		descriptors[i].discpointer = discsize-ofs;
	} else {
		UNLOCK();
		return SCE_EINVAL;
	}

	if (descriptors[i].discpointer > discsize) {
		descriptors[i].discpointer = discsize;
	}

	int res = descriptors[i].discpointer;

	UNLOCK();
	logmsg("%s: arg=0x%p, ofs=0x%08llX, whence=0x%08X -> 0x%08X\n", __func__, arg, ofs, whence, res);
	return res;
}

typedef struct {
	SceOff sk_off;
	SceInt32 sk_reserved;
	SceInt32 sk_whence;
} SceUmdSeekParam;

int umd_ioctl(PspIoDrvFileArg *arg, unsigned int cmd, void *indata, int inlen, void *outdata, int outlen) {
	u32 *outdata32 = (u32 *)outdata;
	u32 *indata32 = (u32 *)indata;
	int i = (int)arg->arg;

	switch (cmd) {
		case 0x01d20001: // SCE_UMD_GET_FILEPOINTER
		{
			LOCK();

			outdata32[0] = descriptors[i].discpointer;

			UNLOCK();
			return 0;
		}

		case 0x01f010db:
		{
			return 0;
		}

		case 0x01f100a6: // SCE_UMD_SEEK_FILE
		{
			if (!indata || inlen < 4)
				return SCE_EINVAL;

			SceUmdSeekParam *seekparam = (SceUmdSeekParam *)indata;

			return (int)umd_lseek(arg, seekparam->sk_off, seekparam->sk_whence);
		}

		case 0x01f30003: // SCE_UMD_UNCACHED_READ
		{
			if (!indata || inlen < 4) {
				return SCE_EINVAL;
			}

			if (!outdata || outlen < indata32[0]) {
				return SCE_EINVAL;
			}

			return umd_read(arg, outdata, indata32[0]);
		}
	}

	logmsg("%s: Unknown ioctl 0x%08X\n", __func__, cmd);
	return SCE_ENOSYS;
}

int ProcessDevctlRead(void *outdata, int size, u32 *indata) {
	int datasize = indata[4]; // 0x10
	int lba = indata[2]; // 0x08
	int dataoffset = indata[6]; // 0x18

	int offset;

	if (size < datasize) {
		return SCE_ENOBUFS;
	}

	if (dataoffset == 0) {
		offset = lba*ISO_SECTOR_SIZE;
	} else if (indata[5] != 0) {
		offset = (lba*ISO_SECTOR_SIZE)-dataoffset+ISO_SECTOR_SIZE;
	} else if (indata[7] == 0) {
		offset = (lba*ISO_SECTOR_SIZE)+dataoffset;
	} else {
		offset = (lba*ISO_SECTOR_SIZE)-dataoffset+ISO_SECTOR_SIZE;
	}

	return ReadUmdFile(offset, outdata, datasize);
}

int umd_devctl(PspIoDrvFileArg *arg, const char *devname, unsigned int cmd, void *indata, int inlen, void *outdata, int outlen) {
	u32 *outdata32 = (u32 *)outdata;
	int res;

	// Devctl seen in np9660, and not in isofs:
	// 01e18024 -> return 0x80010086
	// 01e000d5 -> return 0x80010086
	// 01e18033 -> return 0x80010086
	// 01e180c1 -> return 0x80010086
	// 01e180d0 -> return 0x80010086
	// 01e180d6 -> return 0x80010086
	// 01f000a6 -> return 0x80010086

	switch (cmd) {
		case 0x01e28035:
		{
			*outdata32 = (u32)umdpvd;
			return 0;
		}

		case 0x01e280a9:
		{
			*outdata32 = ISO_SECTOR_SIZE;
			return 0;
		}

		case 0x01e380c0: case 0x01f200a1: case 0x01f200a2:
		{
			if (!indata ||!outdata) {
				return SCE_EINVAL;
			}

			res = ProcessDevctlRead(outdata, outlen, indata);

			return res;
		}

		case 0x01e18030:
		{
			// region related
			return 1;
		}

		case 0x01e38012:
		{
			if (outlen < 0) {
				outlen += 3;
			}

			memset(outdata32, 0, outlen);
			outdata32[0] = 0xe0000800;
			outdata32[7] = outdata32[9] = discsize;
			outdata32[2] = 0;

			return 0;
		}

		case 0x01e38034:
		{
			if (!indata || !outdata) {
				return SCE_EINVAL;
			}

			*outdata32 = 0;

			return 0;
		}

		case 0x01f20001: /* get disc type */
		{
			outdata32[1] = 0x10; /* game */
			outdata32[0] = 0xFFFFFFFF;

			return 0;
		}

		case 0x01f00003:
		{
			return 0;
		}

		case 0x01f20002:
		{
			outdata32[0] = discsize;
			return 0;
		}

		case 0x01f20003:
		{
			*outdata32 = discsize;
			return 0;
		}

		case 0x01e180d3: case 0x01e080a8:
		{
			return SCE_ENOSYS;
		}

		case 0x01f100a3: case 0x01f100a4: case 0x01f010db:
		{
			return 0;
		}
	}

	logmsg("%s: Unknown devctl 0x%08X\n", __func__, cmd);
	//WriteFile("ms0:/unknown_devctl.bin", &cmd, 4);
	//WriteFile("ms0:/unknown_devctl_indata.bin", indata, inlen);

	return SCE_ENOSYS;
}

int sceUmd9660_driver_C0933C16() {
	return 0;
}

int sceUmd9660_driver_887C3193() {
	return 0;
}

int sceUmd9660_driver_7EB57F56() {
	return 0;
}

void sceUmd9660_driver_3CC9CE54() {}
void sceUmd9660_driver_FE3A8B67() {}

int sceNp9660_driver_B925CA6C() {
	return 0;
}

int sceNp9660_driver_8EF69DC6() {
	return 0;
}

int sceNp9660_driver_7A05EB3C(int *a0) {
	if (NULL != a0) {
		*a0 = 0;
	}
	return 0;
}

PspIoDrvFuncs umd_funcs = {
	umd_init,
	umd_exit,
	umd_open,
	umd_close,
	umd_read,
	NULL,
	umd_lseek,
	umd_ioctl,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,/*umd_mount,*/
	NULL,/*umd_umount,*/
	umd_devctl,
	NULL
};

PspIoDrv umd_driver = { "umd", 0x4, 0x800, "UMD9660", &umd_funcs };

int sceKernelSetQTGP3(const u8 *umdid);

static const u8 dummy_umd_id[16] = {
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
};

void UmdCallback(int stat);
static int SysEventHandler(int ev_id, char* ev_name, void* param, int* result) {
	if ( ev_id == 0x400 ){
		if (sceKernelWaitSema(umdsema, 1, 0) >= 0) {
			UmdCallback( 0x9 );
		}
	} else if ( ev_id == 0x400000 ) {
		if (sceKernelSignalSema(umdsema, 1) >= 0) {
			UmdCallback( 0x32 );
		}
	}

	return 0;
}

static PspSysEventHandler event_handler = {
	.size = sizeof(PspSysEventHandler),
	.name = "march33SysEvent",
	.type_mask = 0x00FFFF00, // both suspend / resume
	.handler = &SysEventHandler,
};

int InitUmd9660() {
	int res;

	sceKernelRegisterSysEventHandler(&event_handler);

	// Get ISO path
	memset(g_iso_fn, 0, sizeof(g_iso_fn));
	strncpy(g_iso_fn, sctrlSEGetUmdFile(), sizeof(g_iso_fn)-1);
	logmsg3("[INFO] UMD File: %s\n", g_iso_fn);

	// Leave NP9660 alone, we got no ISO
	if(g_iso_fn[0] == 0) {
		return SCE_ENOMEDIUM;
	}

	res = sceIoAddDrv(&umd_driver);

	if (res < 0) {
		return res;
	}

	sceKernelSetQTGP3(dummy_umd_id);

	return 0;
}

int module_stop(SceSize args, void *argp) {
	sceKernelUnregisterSysEventHandler(&event_handler);
	sceIoDelDrv("umd");
	return 0;
}

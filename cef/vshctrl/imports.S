	.set noreorder

#include "pspstub.s"
	STUB_START "SysMemForKernel", 0x00010011, 0x00010005
	STUB_FUNC 0xEF29061C, sceKernelGetGameInfo
	STUB_END
	STUB_START "SysclibForKernel", 0x00010000, 0x00010005
	STUB_FUNC 0xC2145E80, snprintf
	STUB_END

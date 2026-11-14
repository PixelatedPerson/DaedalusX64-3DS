
// Taken from PicoDrive's libretro implementation for 3DS.
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include "LogCTR.h"

#include "MemoryCTR.h"

typedef int (*ctr_callback_type)(void);

int __stacksize__ = 2 * 1024 * 1024;

static int hack3dsSvcInitialized = 0;

static unsigned int s1, s2, s3, s0;

int gCtrDynarecAvailable = 0;

//-----------------------------------------------------------------------------
// Sets the permissions for memory.
// buffer and size must be 0x1000-aligned. Size is in BYTES.
//-----------------------------------------------------------------------------
int _SetMemoryPermission(void *buffer, int size, int permission)
{
	Handle currentHandle;
	Result res = svcDuplicateHandle(&currentHandle, 0xFFFF8001);
	if (R_FAILED(res))
		return res;
	res = svcControlProcessMemory(currentHandle, (u32)buffer, 0, size, MEMOP_PROT, permission);
	svcCloseHandle(currentHandle);
	return res;
}

//-----------------------------------------------------------------------------
// The old version of this function patched kernel memory through
// svcBackdoor at hard-coded offsets (written for 2015-era firmware).
// On current firmware + Luma3DS that can corrupt kernel state and hard-lock
// the console. Luma's homebrew loader and our CIA exheader already grant
// svcControlProcessMemory, so we just probe that it works.
// Returns 1 if we can make memory executable (dynarec usable), else 0.
//-----------------------------------------------------------------------------
int _InitializeSvcHack(void)
{
	void *probe = memalign(0x1000, 0x1000);
	if (probe == NULL)
		return 0;

	Result rx = _SetMemoryPermission(probe, 0x1000, MEMPERM_READ | MEMPERM_WRITE | MEMPERM_EXECUTE);
	Result rw = _SetMemoryPermission(probe, 0x1000, MEMPERM_READ | MEMPERM_WRITE);
	free(probe);

	CtrLog("svc probe: set RWX=0x%08lX, restore RW=0x%08lX", rx, rw);
	gCtrDynarecAvailable = R_SUCCEEDED(rx) ? 1 : 0;
	return gCtrDynarecAvailable;
}

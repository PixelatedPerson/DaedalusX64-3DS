#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>

#include <3ds.h>
#include <GL/picaGL.h>

#include "BuildOptions.h"
#include "Config/ConfigOptions.h"
#include "Core/Cheats.h"
#include "Core/CPU.h"
#include "OSHLE/ultra_R4300.h"
#include "Core/CPU.h"
#include "Core/Memory.h"
#include "Core/PIF.h"
#include "Core/RomSettings.h"
#include "Core/Save.h"
#include "Debug/DBGConsole.h"
#include "Debug/DebugLog.h"
#include "Graphics/GraphicsContext.h"
#include "HLEGraphics/TextureCache.h"
#include "Input/InputManager.h"
#include "Interface/RomDB.h"
#include "System/Paths.h"
#include "System/System.h"
#include "Test/BatchTest.h"

#include "UI/UserInterface.h"
#include "UI/RomSelector.h"

#include "Utility/IO.h"
#include "Utility/Preferences.h"
#include "Utility/Profiler.h"
#include "Utility/Thread.h"
#include "Utility/Translate.h"
#include "Utility/Timer.h"
#include "Utility/ROMFile.h"
#include "Utility/MemoryCTR.h"
#include "Utility/LogCTR.h"

bool isN3DS = false;
bool shouldQuit = false;

EAudioPluginMode enable_audio = APM_ENABLED_ASYNC;

#ifdef DAEDALUS_LOG
void log2file(const char *format, ...) {
	__gnuc_va_list arg;
	int done;
	va_start(arg, format);
	char msg[512];
	done = vsprintf(msg, format, arg);
	va_end(arg);
	sprintf(msg, "%s\n", msg);
	FILE *log = fopen("sdmc:/DaedalusX64.log", "a+");
	if (log != NULL) {
		fwrite(msg, 1, strlen(msg), log);
		fclose(log);
	}
}
#endif

static void CheckDSPFirmware()
{
	FILE *firmware = fopen("sdmc:/3ds/dspfirm.cdc", "rb");

	if(firmware != NULL)
	{
		fclose(firmware);
		return;
	}

	gfxInitDefault();
	consoleInit(GFX_BOTTOM, NULL);

	printf("DSP Firmware not found!\n\n");
	printf("Press START to exit\n");

	while(aptMainLoop())
	{
		hidScanInput();

		if(hidKeysDown() == KEY_START)
			exit(1);
	}
}

static void Initialize()
{
	CheckDSPFirmware();

	CtrLogOpen();
	CtrLog("DaedalusX64-3DS (fix/n3ds-freeze) starting");

	int canExec = _InitializeSvcHack();
	CtrLog("dynarec available: %s", canExec ? "yes" : "NO - using interpreter");

	romfsInit();
	
	APT_CheckNew3DS(&isN3DS);
	CtrLog("New 3DS: %s", isN3DS ? "yes" : "no");
	osSetSpeedupEnable(true);
	
	gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, true);

	if(isN3DS)
		gfxSetWide(true);
	
	pglInitEx(0x080000, 0x040000);
	CtrLog("picaGL init done");

	strcpy(gDaedalusExePath, DAEDALUS_CTR_PATH(""));
	strcpy(g_DaedalusConfig.mSaveDir, DAEDALUS_CTR_PATH("SaveGames/"));

	IO::Directory::EnsureExists( DAEDALUS_CTR_PATH("SaveStates/") );
	UI::Initialize();

	System_Init();
	CtrLog("System_Init done, entering ROM selector");
}


void HandleEndOfFrame()
{
	shouldQuit = !aptMainLoop();
	
	if (shouldQuit)
	{
		CPU_Halt("Exiting");
	}
}

extern u32 __ctru_heap_size;
extern u32 sDListCount;
extern u32 sVICount;

// Watchdog: logs where the emulated CPU is once per second, so a hang
// shows up in log.txt as the same PC/Count repeating.
static volatile bool sWatchdogRun = false;
static Thread sWatchdogThread = nullptr;

static void WatchdogFunc(void *)
{
	u32 tick = 0;
	while (sWatchdogRun)
	{
		svcSleepThread(1000000000LL); // 1 second (nanoseconds)
		if (!sWatchdogRun) break;
		tick++;
		CtrLog("WD %us: PC=0x%08x Count=0x%08x Delay=%u Jobs=0x%x VIs=%u DLists=%u dynarec=%d",
			(unsigned)tick, (unsigned)gCPUState.CurrentPC,
			(unsigned)gCPUState.CPUControl[C0_COUNT]._u32,
			(unsigned)gCPUState.Delay, (unsigned)gCPUState.StuffToDo,
			(unsigned)sVICount, (unsigned)sDListCount, (int)gDynarecEnabled);
		CtrLog("   IRQ: MI_INTR=0x%02x MASK=0x%02x SR=0x%08x CAUSE=0x%08x COMPARE=0x%08x | SP=0x%04x DPC=0x%04x AI=0x%08x PI=0x%x SI=0x%x",
			(unsigned)Memory_MI_GetRegister(MI_INTR_REG), (unsigned)Memory_MI_GetRegister(MI_INTR_MASK_REG),
			(unsigned)gCPUState.CPUControl[C0_SR]._u32, (unsigned)gCPUState.CPUControl[C0_CAUSE]._u32,
			(unsigned)gCPUState.CPUControl[C0_COMPARE]._u32,
			(unsigned)Memory_SP_GetRegister(SP_STATUS_REG), (unsigned)Memory_DPC_GetRegister(DPC_STATUS_REG),
			(unsigned)Memory_AI_GetRegister(AI_STATUS_REG), (unsigned)Memory_PI_GetRegister(PI_STATUS_REG),
			(unsigned)Memory_SI_GetRegister(SI_STATUS_REG));
	}
}

static void WatchdogStart()
{
	sWatchdogRun = true;
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	// Higher priority (lower number) than the emulator thread so it still
	// runs if the CPU core is stuck in a tight loop.
	s32 wprio = prio > 0x19 ? prio - 1 : 0x18;
	sWatchdogThread = threadCreate(WatchdogFunc, nullptr, 0x4000, wprio, -2, false);
	if (!sWatchdogThread) // try the New 3DS extra core
		sWatchdogThread = threadCreate(WatchdogFunc, nullptr, 0x4000, wprio, 2, false);
	CtrLog("watchdog thread: %s (main prio 0x%lx, wd prio 0x%lx)", sWatchdogThread ? "started" : "FAILED", prio, wprio);
}

static void WatchdogStop()
{
	sWatchdogRun = false;
	if (sWatchdogThread)
	{
		threadJoin(sWatchdogThread, 2000000000LL);
		threadFree(sWatchdogThread);
		sWatchdogThread = nullptr;
	}
}

int main(int argc, char* argv[])
{
	char fullpath[512];

	Initialize();
	
	while(shouldQuit == false)
	{
		std::string rom = UI::DrawRomSelector();
		sprintf(fullpath, "%s%s", DAEDALUS_CTR_PATH("Roms/"), rom.c_str());

		CtrLog("Opening ROM: %s", fullpath);
		bool opened = System_Open(fullpath);
		CtrLog("System_Open returned %d (dynarec=%d)", (int)opened, (int)gDynarecEnabled);
		{
			FILE *f = fopen(DAEDALUS_CTR_PATH("nodynarec.txt"), "r");
			if (f) { fclose(f); gDynarecEnabled = false; CPU_SelectCore(); CtrLog("nodynarec.txt found: forcing interpreter"); }
		}
		{
			extern bool gCtrForceSyncAudio;
			FILE *f = fopen(DAEDALUS_CTR_PATH("syncaudio.txt"), "r");
			gCtrForceSyncAudio = (f != NULL);
			if (f) { fclose(f); CtrLog("syncaudio.txt found: audio processed synchronously"); }
		}
		CtrLog("CPU_Run start (dynarec=%d)", (int)gDynarecEnabled);
		WatchdogStart();
		CPU_Run();
		WatchdogStop();
		CtrLog("CPU_Run returned, closing");
		System_Close();
		CtrLog("System_Close done");
	}
	
	System_Finalize();
	pglExit();

	return 0;
}

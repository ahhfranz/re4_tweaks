#include <iostream>
#include <mutex>
#include "dllmain.h"
#include "ConsoleWnd.h"
#include "Game.h"
#include "Settings.h"
#include <timeapi.h>

uintptr_t ptrGXCopyTex;
uintptr_t ptrAfterItemExamineHook;
uintptr_t ptrFilter03;
uintptr_t ptrAfterEsp04TransHook;

uintptr_t* ptrNonBlurryVertex;
uintptr_t* ptrBlurryVertex;

uint32_t* ptrLaserR;
uint32_t* ptrLaserG;
uint32_t* ptrLaserB;
uint32_t* ptrLaserA;

const uint8_t rgbcycle[360] = {
  0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 15, 17, 18, 20, 22, 24, 26, 28, 30, 32, 35, 37, 39,
 42, 44, 47, 49, 52, 55, 58, 60, 63, 66, 69, 72, 75, 78, 81, 85, 88, 91, 94, 97, 101, 104, 107, 111, 114, 117, 121, 124, 127, 131, 134, 137,
141, 144, 147, 150, 154, 157, 160, 163, 167, 170, 173, 176, 179, 182, 185, 188, 191, 194, 197, 200, 202, 205, 208, 210, 213, 215, 217, 220, 222, 224, 226, 229,
231, 232, 234, 236, 238, 239, 241, 242, 244, 245, 246, 248, 249, 250, 251, 251, 252, 253, 253, 254, 254, 255, 255, 255, 255, 255, 255, 255, 254, 254, 253, 253,
252, 251, 251, 250, 249, 248, 246, 245, 244, 242, 241, 239, 238, 236, 234, 232, 231, 229, 226, 224, 222, 220, 217, 215, 213, 210, 208, 205, 202, 200, 197, 194,
191, 188, 185, 182, 179, 176, 173, 170, 167, 163, 160, 157, 154, 150, 147, 144, 141, 137, 134, 131, 127, 124, 121, 117, 114, 111, 107, 104, 101, 97, 94, 91,
 88, 85, 81, 78, 75, 72, 69, 66, 63, 60, 58, 55, 52, 49, 47, 44, 42, 39, 37, 35, 32, 30, 28, 26, 24, 22, 20, 18, 17, 15, 13, 12,
 11, 9, 8, 7, 6, 5, 4, 3, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

void __declspec(naked) ItemExamineHook()
{
	if (!re4t::cfg->bRestorePickupTransparency)
		_asm
		{
			call ptrGXCopyTex
			jmp ptrAfterItemExamineHook
		}
	else
		_asm {jmp ptrAfterItemExamineHook}
}

void __declspec(naked) Esp04TransHook()
{
	if (!re4t::cfg->bDisableFilmGrain)
		_asm
		{
			push ebp
			mov  ebp, esp
			sub  esp, 0x68
			jmp ptrAfterEsp04TransHook
		}
	else
		_asm {ret}
}

double FramelimiterFrequency = 0;
double FramelimiterPrevTicks = 0;
void Framelimiter_Hook(uint8_t isAliveEvt_result)
{
	// Change thread to core 0 before running QueryPerformance* funcs, game does this, so guess we should too
	HANDLE curThread = GetCurrentThread();
	DWORD_PTR prevAffinityMask = SetThreadAffinityMask(curThread, 0);

	static bool framelimiterInited = false;
	if (!framelimiterInited)
	{
		LARGE_INTEGER result;
		QueryPerformanceFrequency(&result);
		FramelimiterFrequency = (double)result.QuadPart / 1000.0;

		QueryPerformanceCounter(&result);
		FramelimiterPrevTicks = (double)result.QuadPart / FramelimiterFrequency;

		typedef MMRESULT(__stdcall* timeBeginPeriod_Fn) (UINT Period);
		timeBeginPeriod_Fn timeBeginPeriod_actual = (timeBeginPeriod_Fn)GetProcAddress(LoadLibraryA("winmm.dll"), "timeBeginPeriod");
		timeBeginPeriod_actual(1);

		framelimiterInited = true;
	}

	int gameFramerate = GetGameVariableFrameRate();

	// The games IsAliveEvt check seems to (indirectly) result in framelimiter loop limiting to 60FPS
	// maybe a remnant of some time when more framerate options were available?
	if (isAliveEvt_result && gameFramerate != 30)
		gameFramerate = 60;

	double TargetFrametime = 1000.0 / (double)gameFramerate;

	double timeElapsed = 0;
	double timeCurrent = 0;
	do
	{
		// Framelimiter based on FPS_ACCURATE code from
		// https://github.com/ThirteenAG/d3d9-wrapper/blob/c1480b0c1b40e0ba7b55b8660bd67f911a967421/source/dllmain.cpp#L46

		LARGE_INTEGER counter;
		QueryPerformanceCounter(&counter);
		timeCurrent = (double)counter.QuadPart / FramelimiterFrequency;
		timeElapsed = timeCurrent - FramelimiterPrevTicks;

		if (TargetFrametime <= timeElapsed || re4t::cfg->bDisableFramelimiting)
			break;
		else if (TargetFrametime - timeElapsed > 2.0) // > 2ms
			Sleep(1); // Sleep for ~1ms
		else
			Sleep(0); // yield thread's time-slice (does not actually sleep)
	}
	while (TargetFrametime > timeElapsed);

	FramelimiterPrevTicks = timeCurrent;

	// HACK: workaround for chandelier flicker/shake bug in r117s10 cutscene
	// during gameplay a chandelier object has a motion animation enabled, afterward object gets left with a neutral animation running
	// when cutscene happens the (isAliveEvt_result && gameFramerate != 30) block below would be ran instead, resulting in deltaTimes like 0.500000122, varying a small amount each frame
	// anim code that handles the neutral animation doesn't like the varying frametimes, resulting in the chandelier flickering/shaking around during cutscene
	// (this doesn't happen in movie browser though since the chandelier won't have any motion animation applied to it there, anim gets applied during gameplay, and ends up carrying over to the cutscene)
	// this bug happens with the vanilla framelimiter code, and ended up being copied in this reimpl, fortunately we can get around it here though
	// more info at https://github.com/nipkownix/re4_tweaks/issues/333#issuecomment-1257052215
	// TODO: find an actual fix for the broken motion code instead, would likely fix other motion issues at non 60/30 framerates too...
	// TODO2: alternately, find a way to disable the neutral animation properly after the anim has played out?
	if (isAliveEvt_result)
	{
		// Only let isAliveEvt_result remain as true if r117s10 isn't playing:
		isAliveEvt_result = EvtMgr->IsAliveEvt("event/evd/r117s10.evd", 0, AliveEvtType::AliveEvtTypeNormal) == false;
	}

	// Not really sure what the second part of IsAliveEvt check is doing
	// Seems to skip setting timeElapsed to the fixed FramelimiterTargetFrametime at least
	// Guess that means the true timeElapsed gets passed to the game? (after being limited to 60 like above)
	if (isAliveEvt_result && gameFramerate != 30)
	{
		if (timeElapsed > 33.333333333333333)
			timeElapsed = 33.333333333333333;
	}
	else
	{
		// TODO: using the actual time elapsed since last frame instead of FramelimiterTargetFrametime would solve slowdown issues
		// (similar to https://github.com/nipkownix/re4_tweaks/pull/25)
		// but it's not known how well the game works with values that aren't 0.5 (60fps) or 1 (30fps)
		// so for now we'll just work pretty much the same as the game itself, unless UseDynamicFrametime is set
		if (!re4t::cfg->bUseDynamicFrametime)
			timeElapsed = TargetFrametime;
	}

	GlobalPtr()->deltaTime_70 = float((timeElapsed / 1000) * 30.0);

	SetThreadAffinityMask(curThread, prevAffinityMask);
}

std::recursive_mutex g_D3DMutex;

void __cdecl D3D_LockMutex_Hook() // hooks 0x9391C0
{
	g_D3DMutex.lock();
}

void __cdecl D3D_UnlockMutex_Hook() // hooks 0x9391D0
{
	g_D3DMutex.unlock();
}

int(__stdcall* D3DXCreateTextureFromFileInMemoryEx_Orig)(
	int a1,
	int a2,
	int a3,
	int a4,
	int a5,
	int a6,
	int a7,
	int a8,
	int a9,
	int a10,
	int a11,
	int a12,
	int a13,
	int a14,
	int a15);

int __stdcall D3DXCreateTextureFromFileInMemoryEx_Hook(
	int a1,
	int a2,
	int a3,
	int a4,
	int a5,
	int a6,
	int a7,
	int a8,
	int a9,
	int a10,
	int a11,
	int a12,
	int a13,
	int a14,
	int a15)
{
	auto ret = D3DXCreateTextureFromFileInMemoryEx_Orig(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15);
	D3D_UnlockMutex_Hook();
	return ret;
}

void re4t::init::MultithreadFix()
{
	// Clear D3DCREATE_MULTITHREADED flag from D3D CreateDevice call for slight FPS improvement
	auto pattern = hook::pattern("68 ? ? ? ? 68 ? ? ? ? 6A 44 56 8B 35");
	auto ptr_CreateDevice_BehaviorFlags = pattern.count(1).get(0).get<uint8_t>(0xB);
	Patch(ptr_CreateDevice_BehaviorFlags, uint8_t(*ptr_CreateDevice_BehaviorFlags & ~D3DCREATE_MULTITHREADED));

	// Game has a pair of nullsubs that are always called just before graphics-threading related code is used
	// Kinda seems like they were meant to be a pair of funcs for locking/unlocking a mutex, but that's just a guess.
	// Restore these so that flag removal above can be made more stable
	pattern = hook::pattern("E8 ? ? ? ? A1 ? ? ? ? A3 ? ? ? ? 89 1D ? ? ? ? A3 ? ? ? ? E8 ? ? ? ? 8B 35");
	auto ptr_D3D_LockMutex = injector::GetBranchDestination(pattern.count(1).get(0).get<uint32_t>(0)).as_int();
	InjectHook(ptr_D3D_LockMutex, D3D_LockMutex_Hook);

	pattern = hook::pattern("E8 ? ? ? ? 68 ? ? ? ? FF 15 ? ? ? ? A1 ? ? ? ? 50 FF 15");
	auto ptr_D3D_UnlockMutex = injector::GetBranchDestination(pattern.count(1).get(0).get<uint32_t>(0)).as_int();
	InjectHook(ptr_D3D_UnlockMutex, D3D_UnlockMutex_Hook);

	// Game calls D3D_LockDevice before D3DXCreateTextureFromFileInMemoryEx
	// LockDevice returns pointer to D3D device, so they probably used that as a quick way to retrieve it, but forgot/didn't care about using UnlockDevice afterward
	// Hook the misbehaving calls so we can add UnlockMutex calls to them
	// (not sure if this is safest way to do it though - game seems to do something with the ptr returned by D3DXCreate...
	// maybe UnlockMutex should be after it's finished with that ptr, would be harder to patch in tho...)
	pattern = hook::pattern("53 E8 ? ? ? ? 50 E8 ? ? ? ? 8B 36 8B 0E 8D 55 ?");
	auto ptr_caller1 = pattern.count(1).get(0).get<uint32_t>(7); // 0x98009D
	ReadCall(ptr_caller1, D3DXCreateTextureFromFileInMemoryEx_Orig);
	InjectHook(ptr_caller1, D3DXCreateTextureFromFileInMemoryEx_Hook);

	pattern = hook::pattern("57 E8 ? ? ? ? 50 E8 ? ? ? ? 8B 06 8B 10 8B 52 ? 8D 4D ?"); // 0x980234 & 0x981049
	InjectHook(pattern.count(2).get(0).get<uint32_t>(7), D3DXCreateTextureFromFileInMemoryEx_Hook);
	InjectHook(pattern.count(2).get(1).get<uint32_t>(7), D3DXCreateTextureFromFileInMemoryEx_Hook);

	pattern = hook::pattern("53 E8 ? ? ? ? 50 E8 ? ? ? ? 8B 36 8B 0E 8D 55 ? 52"); // 0x9E4B82, unused?
	InjectHook(pattern.count(1).get(0).get<uint32_t>(7), D3DXCreateTextureFromFileInMemoryEx_Hook);

	pattern = hook::pattern("57 E8 ? ? ? ? 50 E8 ? ? ? ? 8D 4D ? 8B F8 8B 06 8B 10 8B 52 ?"); // 0x9E6619
	InjectHook(pattern.count(1).get(0).get<uint32_t>(7), D3DXCreateTextureFromFileInMemoryEx_Hook);

	// Lone UnlockMutex call at 0x955792 - doesn't have a LockMutex call before it for some reason
	// this would cause game crash on startup, and skipping it caused game crash on exit - nopping it instead seems to fix both
	pattern = hook::pattern("89 0D ? ? ? ? A3 ? ? ? ? 89 99 ? ? ? ? 89 99 ? ? ? ? E8 ? ? ? ?");
	Nop(pattern.count(1).get(0).get<uint8_t>(0x17), 5);

	spd::log()->info("MultithreadFix applied");
}

void re4t::init::DisplayTweaks()
{
	re4t::init::DisplayModeFix();

	// Implements new reduced-CPU-usage limiter
	if (re4t::cfg->bReplaceFramelimiter)
	{
		// nop beginning of framelimiter code (sets up thread affinity to core 0)
		auto pattern = hook::pattern("A3 ? ? ? ? 6A 00 FF 15 ? ? ? ? 50 FF");
		uint8_t* framelimiterStart = pattern.count(1).get(0).get<uint8_t>(5);
		pattern = hook::pattern("E8 ? ? ? ? 85 C0 75 ? D9 EE EB ?");
		uint8_t* framelimiterEnd = pattern.count(1).get(0).get<uint8_t>(0);
		Nop(framelimiterStart, framelimiterEnd - framelimiterStart); // 6549C3 to 6549D9 (1.1.0)

		pattern = hook::pattern("B9 ? ? ? ? E8 ? ? ? ? 84 C0 74 ? E8 ? ? ? ? 83 F8 1E");
		framelimiterStart = pattern.count(1).get(0).get<uint8_t>(0xA); // 654A1E
		pattern = hook::pattern("8D 85 ? ? ? ? 50 FF D3 DF AD ? ? ? ? 8D 8D");
		framelimiterEnd = pattern.count(1).get(0).get<uint8_t>(0); // 654B0A

		Nop(framelimiterStart, framelimiterEnd - framelimiterStart);

		Patch(framelimiterStart, uint8_t(0x50)); // push eax (pass return value of EventMgr::IsAliveEvt)
		InjectHook(framelimiterStart + 1, Framelimiter_Hook, PATCH_CALL);
		Patch(framelimiterStart + 1 + 5, { 0x83, 0xc4, 0x04 }); // add esp, 4 (needed to allow WinMain to exit properly)

		spd::log()->info("Framelimiter replaced");
	}

	// Fix broken effects
	re4t::init::FilterXXFixes();

	// Install a D3D9 hook
	re4t::init::D3D9Hook();

	// Aspect ratio-related tweaks
	re4t::init::AspectRatioTweaks();

	// Hook function that loads the FOV
	{
		auto pattern = hook::pattern("D9 45 ? 89 4E ? D9 5E ? 8B 8D ? ? ? ? 89 46 ? 8B 85 ? ? ? ? 8D 7E");
		struct ScaleFOV
		{
			void operator()(injector::reg_pack& regs)
			{
				float FOV = *(float*)(regs.ebp - 0x34);

				if (re4t::cfg->fFOVAdditional > 0.0f)
					FOV += re4t::cfg->fFOVAdditional;

				_asm {fld FOV}

				*(int32_t*)(regs.esi + 0x4) = regs.ecx;
			}
		}; injector::MakeInline<ScaleFOV>(pattern.count(1).get(0).get<uint32_t>(0), pattern.count(1).get(0).get<uint32_t>(6));

		if (re4t::cfg->fFOVAdditional > 0.0f)
			spd::log()->info("FOV increased");
	}

	// Force v-sync off
	if (re4t::cfg->bDisableVsync)
	{
		// See D3D9Hook.cpp -> hook_Direct3D9::CreateDevice and hook_Direct3D9::Reset

		auto pattern = hook::pattern("8B 56 ? 8B 85 ? ? ? ? 83 C4 ? 52 68 ? ? ? ? 50");
		struct WriteIniHook
		{
			void operator()(injector::reg_pack& regs)
			{
				regs.edx = 0; // This is what the game will write as the v-sync value inside config.ini

				regs.eax = *(int32_t*)(regs.ebp - 0x808);
			}
		}; injector::MakeInline<WriteIniHook>(pattern.count(1).get(0).get<uint32_t>(0), pattern.count(1).get(0).get<uint32_t>(9));

		spd::log()->info("Vsync disabled");
	}

	// Restore missing transparency in the item pickup screen by
	// removing a call to GXCopyTex inside ItemExamine::gxDraw
	{
		auto pattern = hook::pattern("E8 ? ? ? ? D9 05 ? ? ? ? D9 7D ? 6A ? 0F B7 45");
		ptrGXCopyTex = injector::GetBranchDestination(pattern.get_first(0)).as_int();
		ptrAfterItemExamineHook = (uintptr_t)pattern.count(1).get(0).get<uint32_t>(5);
		injector::MakeNOP(pattern.get_first(0), 5);
		injector::MakeJMP(pattern.get_first(0), ItemExamineHook, true);

		if (re4t::cfg->bRestorePickupTransparency)
			spd::log()->info("RestorePickupTransparency enabled");
	}

	// Override laser sight colors
	{
		auto pattern = hook::pattern("D9 05 ? ? ? ? D9 99 ? ? ? ? 8B 55 ? D9 05 ? ? ? ? D9");
		ptrLaserR = *pattern.count(1).get(0).get<uint32_t*>(2);
		ptrLaserG = *pattern.count(1).get(0).get<uint32_t*>(17);
		ptrLaserB = *pattern.count(1).get(0).get<uint32_t*>(32);
		ptrLaserA = *pattern.count(1).get(0).get<uint32_t*>(47);

		static int rgbindex = 0;

		struct DrawLaserStruct
		{
			void operator()(injector::reg_pack& regs)
			{
				if (re4t::cfg->bOverrideLaserColor)
				{
					if (re4t::cfg->bRainbowLaser)
					{
						*(float*)(ptrLaserR) = rgbcycle[(rgbindex + 120) % 360];
						*(float*)(ptrLaserG) = rgbcycle[rgbindex];
						*(float*)(ptrLaserB) = rgbcycle[(rgbindex + 240) % 360];

						rgbindex++;
						if (rgbindex > 360)
							rgbindex = 0;
					}
					else
					{
						*(float*)(ptrLaserR) = (float)re4t::cfg->iLaserR;
						*(float*)(ptrLaserG) = (float)re4t::cfg->iLaserG;
						*(float*)(ptrLaserB) = (float)re4t::cfg->iLaserB;

					}

					// Seems the game disables reading custom RGB colors if the alpha is set to 255?
					// The alpha value also seems to be doing nothing useful. It does affect the opacity of the laser dot, but not the line itself.
					// Forcing 220 makes it look okay-ish.
					*(int8_t*)(ptrLaserA) = (int8_t)220;
				}
				else
				{
					*(float*)(ptrLaserR) = 255.0f;
					*(float*)(ptrLaserG) = 0.0f;
					*(float*)(ptrLaserB) = 0.0f;
					*(int8_t*)(ptrLaserA) = (int8_t)255;
				}

				regs.eax = *(int8_t*)(ptrLaserA);
			}
		}; 
		
		pattern = hook::pattern("A1 ? ? ? ? D9 5D ? 83 F8 ? 75 ? 8B 15 ? ? ? ? D9 45 ? 8B 0D ? ? ? ? 83");
		injector::MakeInline<DrawLaserStruct>(pattern.count(1).get(0).get<uint32_t>(0), pattern.count(1).get(0).get<uint32_t>(5));

		pattern = hook::pattern("a1 ? ? ? ? 83 f8 ? 75 ? 8b 0d ? ? ? ? d9 45 ? 8b 15 ? ? ? ? 83 ec");
		injector::MakeInline<DrawLaserStruct>(pattern.count(1).get(0).get<uint32_t>(0), pattern.count(1).get(0).get<uint32_t>(5));
	}

	// Disable Filter03 for now, as we have yet to find a way to actually fix it
	{
		auto pattern = hook::pattern("E8 ? ? ? ? B9 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? 39 1D");
		ptrFilter03 = injector::GetBranchDestination(pattern.get_first(0)).as_int();
		struct DisableBrokenFilter03
		{
			void operator()(injector::reg_pack& regs)
			{
				if (!re4t::cfg->bDisableBrokenFilter03)
					_asm {call ptrFilter03}
			}
		}; injector::MakeInline<DisableBrokenFilter03>(pattern.count(1).get(0).get<uint32_t>(0), pattern.count(1).get(0).get<uint32_t>(5));

		spd::log()->info("DisableBrokenFilter03 applied");
	}

	// Fix a problem related to a vertex buffer that caused the image to be slightly blurred
	{
		auto pattern = hook::pattern("E8 ? ? ? ? 8B 15 ? ? ? ? A1 ? ? ? ? 8B 08 6A ? 6A ? 52");
		ptrNonBlurryVertex = *pattern.count(1).get(0).get<uint32_t*>(7); // Replacement buffer

		// Hook struct
		struct BlurryBuffer
		{
			void operator()(injector::reg_pack& regs)
			{
				if (!re4t::cfg->bFixBlurryImage)
					regs.edx = *(int32_t*)ptrBlurryVertex;
				else
					regs.edx = *(int32_t*)ptrNonBlurryVertex;
			}
		};

		// First buffer
		pattern = hook::pattern("8B 15 ? ? ? ? A1 ? ? ? ? 8B 08 56 57 6A ? 6A");
		ptrBlurryVertex = *pattern.count(1).get(0).get<uint32_t*>(2);
		injector::MakeInline<BlurryBuffer>(pattern.count(1).get(0).get<uint32_t>(0), pattern.count(1).get(0).get<uint32_t>(6));

		// Second buffer
		pattern = hook::pattern("D9 5D ? FF D0 D9 E8 A1 ? ? ? ? 8B 08 8B 91 ? ? ? ? 6A ? 51 D9 1C ? 68");
		injector::MakeInline<BlurryBuffer>(pattern.count(1).get(0).get<uint32_t>(40), pattern.count(1).get(0).get<uint32_t>(46));

		if (re4t::cfg->bFixBlurryImage)
			spd::log()->info("FixBlurryImage enabled");
	}

	// Disable film grain (Esp04)
	{
		auto pattern = hook::pattern("55 8B EC 83 EC ? A1 ? ? ? ? 33 C5 89 45 ? 56 8B 75 ? 0F B6 4E");
		ptrAfterEsp04TransHook = (uintptr_t)pattern.count(1).get(0).get<uint32_t>(6);
		injector::MakeNOP(pattern.get_first(0), 6);
		injector::MakeJMP(pattern.get_first(0), Esp04TransHook, true);

		if (re4t::cfg->bDisableFilmGrain)
			spd::log()->info("DisableFilmGrain applied");
	}

	// Improve Water
	{
		auto pattern = hook::pattern("DC 0D ? ? ? ? D9 59 ? 3C ? 72 ? C6 45 ? ? 8B 9D");
		struct FixWaterScaling
		{
			void operator()(injector::reg_pack& regs)
			{
				double multi = 0.25; // Original value

				if (re4t::cfg->bImproveWater)
					multi = 2.0;

				__asm {fmul multi}
			}
		}; injector::MakeInline<FixWaterScaling>(pattern.count(1).get(0).get<uint32_t>(0), pattern.count(1).get(0).get<uint32_t>(6));

		spd::log()->info("ImproveWater applied");
	}

	// Disable Windows DPI scaling
	if (re4t::cfg->bFixDPIScale)
	{
		SetProcessDPIAware();
	}

	// Multithread flag removal + deadlock fixes
	if (re4t::cfg->bMultithreadFix)
	{
		re4t::init::MultithreadFix();
	}
}

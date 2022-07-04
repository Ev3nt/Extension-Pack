#include <windows.h>
#include <detours.h>
#include "fp_call.h"
#include <math.h>
#include <vector>

#define AttachDetour(pointer, detour) (DetourUpdateThread(GetCurrentThread()), DetourAttach(&(PVOID&)pointer, detour))
#define DetachDetour(pointer, detour) (DetourUpdateThread(GetCurrentThread()), DetourDetach(&(PVOID&)pointer, detour))

typedef struct {
	UINT vTable; // reserved
	UINT unk1;
	UINT Unit;
	UINT StatBar;
	UINT unk2;
	UINT unk3; // reserved
} CPreselectUI, * PCPreselectUI;

UINT_PTR gameBase = (UINT_PTR)GetModuleHandle("game.dll");
HMODULE thismodule = NULL;
float wideScreenMultiplier = 1.0f;
bool fixWideScreen = false;

UINT_PTR pCGxDeviceD3D = gameBase + 0xacbd40; // 0x574 = [349] - wnd
UINT_PTR lockFps = gameBase + 0x62d7f9;

auto CreateMatrixPerspectiveFov = (void(__thiscall*)(float* viewMatrix, float fovY, float aspectRatio, float nearZ, float farZ))(gameBase + 0x7b66f0);
auto BuildHPBars = (void(__thiscall*)(CPreselectUI* preselectUI, UINT unit, BOOL flag))(gameBase + 0x379a30);
auto SetFrameWidth = (BOOL(__thiscall*)(UINT frame, float width))(gameBase + 0x605d90);
auto SetJassState = (void(__thiscall*)(BOOL jassState))(gameBase + 0x2ab0e0);

void __fastcall CreateMatrixPerspectiveFovCustom(float viewMatrix[16], LPVOID, float fovY, float aspectRatio, float nearZ, float farZ);
void __fastcall BuildHPBarsCustom(CPreselectUI* preselectUI, LPVOID, UINT unit, BOOL flag);
void __fastcall SetJassStateCustom(BOOL jassState);

bool Patch(UINT_PTR address, std::vector<BYTE> data);
bool Patch(LPVOID address, std::vector<BYTE> data);

float GetFrameWidth(UINT frame);

bool ValidVersion();

extern "C" void __stdcall UnlockFPS(BOOL state) {
	state ? Patch(lockFps, { 0x90, 0x90, 0x90 }) : Patch(lockFps, { 0x83, 0xE0, 0xFB });
}

extern "C" void __stdcall SetWidescreenFix(BOOL state) {
	fixWideScreen = state != FALSE;
}

//---------------------------------------------------

BOOL APIENTRY DllMain(HMODULE module, UINT reason, LPVOID reserved) {
	switch (reason) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(module);

		if (!gameBase || !ValidVersion()) {
			return FALSE;
		}

		thismodule = module;

		DetourTransactionBegin();

		AttachDetour(CreateMatrixPerspectiveFov, CreateMatrixPerspectiveFovCustom);
		AttachDetour(BuildHPBars, BuildHPBarsCustom);
		//SetWidescreenFix(true); // - Should be commented to use in prod
		//UnlockFPS(true);		// - Should be commented to use in prod
		AttachDetour(SetJassState, SetJassStateCustom); // - Should be recommented to use in prod

		DetourTransactionCommit();

		break;
	case DLL_PROCESS_DETACH:
		DetourTransactionBegin();

		DetachDetour(CreateMatrixPerspectiveFov, CreateMatrixPerspectiveFovCustom);
		DetachDetour(BuildHPBars, BuildHPBarsCustom);
		DetachDetour(SetJassState, SetJassStateCustom); // - Should be recommented to use in prod
		UnlockFPS(false);

		DetourTransactionCommit();

		break;
	}
	return TRUE;
}

//---------------------------------------------------

void __fastcall CreateMatrixPerspectiveFovCustom(float viewMatrix[16], LPVOID, float fovY, float aspectRatio, float nearZ, float farZ) {
	RECT rect;
	wideScreenMultiplier = GetWindowRect((*(HWND**)pCGxDeviceD3D)[349], &rect) && fixWideScreen ? float(rect.right - rect.left) * (1.0f / (rect.bottom - rect.top)) * 0.75f : 1.f;

	float yScale = 1.f / (float)tan(fovY * 0.5f / (float)sqrt(aspectRatio * aspectRatio + 1.f));
	float xScale = yScale / (aspectRatio * wideScreenMultiplier);

	float tempMatrix[16] = {xScale,		0.f,	0.f,									0.f,
							0.f,		yScale, 0.f,									0.f,
							0.f,		0.f,	(nearZ + farZ) / (farZ - nearZ),		1.f,
							0.f,		0.f,	(-2.f * nearZ * farZ) / (farZ - nearZ), 0.f};

	CopyMemory(viewMatrix, tempMatrix, sizeof(tempMatrix));
}

void __fastcall BuildHPBarsCustom(CPreselectUI* preselectUI, LPVOID, UINT unit, BOOL flag) {
	BuildHPBars(preselectUI, unit, flag);

	if (!fixWideScreen) {
		return;
	}

	UINT statBar = preselectUI->StatBar;

	statBar ? SetFrameWidth(statBar, GetFrameWidth(statBar) / wideScreenMultiplier) : NULL;
}

void __fastcall SetJassStateCustom(BOOL jassState) {
	if (jassState == TRUE && thismodule) {
		CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)FreeLibrary, thismodule, NULL, NULL);
	}

	return SetJassState(jassState);
}

//---------------------------------------------------

bool Patch(UINT_PTR address, std::vector<BYTE> data) {
	return Patch((LPVOID)address, data);
}

bool Patch(LPVOID address, std::vector<BYTE> data) {
	if (memcmp(address, &data[0], data.size()) == 0) {
		return true;
	}
	
	DWORD oldProtect;

	if (VirtualProtect(address, data.size(), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		CopyMemory(address, &data[0], data.size());
		VirtualProtect(address, data.size(), oldProtect, &oldProtect);

		FlushInstructionCache(GetCurrentProcess(), address, data.size());

		return true;
	}
	
	return false;
}

float GetFrameWidth(UINT frame) {
	return frame ? ((float*)frame)[22] : NULL;
}

bool ValidVersion() {
	DWORD handle;
	DWORD size = GetFileVersionInfoSize("game.dll", &handle);

	LPSTR buffer = new char[size];
	GetFileVersionInfo("game.dll", handle, size, buffer);

	VS_FIXEDFILEINFO* verInfo;
	size = sizeof(VS_FIXEDFILEINFO);
	VerQueryValue(buffer, "\\", (LPVOID*)&verInfo, (UINT*)&size);
	delete[] buffer;

	return (((verInfo->dwFileVersionMS >> 16) & 0xffff) == 1 && (verInfo->dwFileVersionMS & 0xffff) == 26 && ((verInfo->dwFileVersionLS >> 16) & 0xffff) == 0 && ((verInfo->dwFileVersionLS >> 0) & 0xffff) == 6401);
}
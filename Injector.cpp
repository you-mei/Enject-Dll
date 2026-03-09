// Injector.cpp - DLL Injection Tool
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

HINSTANCE g_hint=0;

//线程参数结构体定义
typedef struct _RemoteParam {
	char szMsg[MAX_PATH];    //
	
} RemoteParam, * PRemoteParam;


//????????
bool enableDebugPriv()
{
    HANDLE hToken;
    LUID sedebugnameValue;
    TOKEN_PRIVILEGES tkp;
	
    if (!OpenProcessToken(GetCurrentProcess(), 
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }
    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &sedebugnameValue)) {
        CloseHandle(hToken);
        return false;
    }
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Luid = sedebugnameValue;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), NULL, NULL)) {
        CloseHandle(hToken);
        return false;
    }
    return true;
}

// Find process ID by name
DWORD FindProcessId(const char* processName)
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hSnapshot, &pe32))
    {
        CloseHandle(hSnapshot);
        return 0;
    }

    do
    {
        if (_stricmp(pe32.szExeFile, processName) == 0)
        {
            CloseHandle(hSnapshot);
            return pe32.th32ProcessID;
        }
    } while (Process32Next(hSnapshot, &pe32));

    CloseHandle(hSnapshot);
    return 0;
}

// Inject DLL into target process
bool InjectDLL(DWORD processId, const char* dllPath)
{
	DWORD dwWriteBytes;
	//????????
    enableDebugPriv();
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (hProcess == NULL)
    {
        printf("[-] Failed to open process: %d\n", GetLastError());
        return false;
    }

	//定义线程参数结构体变量
    RemoteParam remoteData;
    ZeroMemory(&remoteData, sizeof(RemoteParam));
	
    //填充结构体变量中的成员
    
    lstrcat(remoteData.szMsg, dllPath);
    // Allocate memory in remote process
    size_t pathLen = strlen(dllPath) + 1;
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, NULL, pathLen,
                                       MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (pRemoteMem == NULL)
    {
        printf("[-] Failed to allocate remote memory: %d\n", GetLastError());
        CloseHandle(hProcess);
        return false;
    }

    // Write DLL path to remote memory
    if (!WriteProcessMemory(hProcess, pRemoteMem, (void*)&remoteData,pathLen, &dwWriteBytes))
    {
        printf("[-] Failed to write process memory: %d\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteMem, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        CloseHandle(hProcess);
        return false;
    }

    // Get address of LoadLibraryA
    HMODULE hKernel32 = GetModuleHandle("kernel32.dll");
    LPVOID pLoadLibrary = (LPVOID)GetProcAddress(hKernel32, "LoadLibraryA");
    if (pLoadLibrary == NULL)
    {
        printf("[-] Failed to get LoadLibraryA address\n");
        VirtualFreeEx(hProcess, pRemoteMem, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        CloseHandle(hProcess);
        return false;
    }

    // Create remote thread to load DLL
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
                                        (LPTHREAD_START_ROUTINE)pLoadLibrary,
                                        pRemoteMem, 0, &dwWriteBytes);
    if (hThread == NULL)
    {
        printf("[-] Failed to create remote thread: %d\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteMem, MEM_COMMIT, PAGE_EXECUTE_READWRITE);		
        CloseHandle(hProcess);
        return false;
    }

    printf("[+] DLL injection successful!\n");
    printf("[+] Waiting for thread to complete...\n");

	//fflush(stdout); // 强制刷新输出缓冲区
	//Sleep(5000);
    // Wait for thread to complete
    WaitForSingleObject(hThread, INFINITE);
	//GetExitCodeThread( hThread,(DWORD*) &g_hint );

    // Cleanup
    CloseHandle(hThread);
   // VirtualFreeEx(hProcess, pRemoteMem, MEM_COMMIT, PAGE_READWRITE);
    CloseHandle(hProcess);

    return true;
}

int main(int argc, char* argv[])

{
    printf("========================================\n");
    printf("       DLL Injector (VC6 Build)\n");
    printf("========================================\n\n");

    if (argc < 3)
    {
        printf("Usage: %s <process name> <DLL path>\n", argv[0]);
        printf("\nExamples:\n");
        printf("  %s notepad.exe C:\\test.dll\n", argv[0]);
        printf("  %s calc.exe test.dll\n", argv[0]);
        printf("\nNote: DLL path can be absolute or relative\n");
        return 1;
    }

    const char* processName = argv[1];
    const char* dllPath = argv[2];

    printf("[+] Target process: %s\n", processName);
    printf("[+] DLL path: %s\n\n", dllPath);

    // Find process ID
    printf("[*] Searching for process...\n");
    DWORD processId = FindProcessId(processName);
    if (processId == 0)
    {
        printf("[-] Process not found: %s\n", processName);
        printf("\nMake sure the target process is running\n");
        return 1;
    }

    printf("[+] Process ID found: %d\n\n", processId);

    // Verify DLL exists
    printf("[*] Checking DLL file...\n");
    DWORD fileAttr = GetFileAttributes(dllPath);
    if (fileAttr == -1)//INVALID_FILE_ATTRIBUTES,vc6 no define
    {
        printf("[-] DLL file not found: %s\n", dllPath);
        return 1;
    }
    printf("[+] DLL file verified\n\n");

    // Inject DLL
    printf("[*] Injecting...\n");
    if (InjectDLL(processId, dllPath))
    {
        printf("\n[========================================]\n");
        printf("[+] Injection completed successfully!\n");
        printf("[========================================]\n");
        return 0;
    }
    else
    {
        printf("\n[========================================]\n");
        printf("[-] Injection failed!\n");
        printf("[========================================]\n");
        return 1;
    }
}
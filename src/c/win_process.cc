
#ifdef _WIN32
#include "process.h"
#ifndef WIN_PROCESS_PROCESS_H
#define WIN_PROCESS_PROCESS_H

#endif //WIN_PROCESS_PROCESS_H
#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
#pragma ide diagnostic ignored "UnreachableCode"


#include <fstream>
#include <windows.h>
#include <psapi.h>
#include <lmcons.h>
#include <tchar.h>
#include <tlhelp32.h>
#include <assert.h>
#include <math.h>
#include <algorithm> // 包含了min函数
#include <stdlib.h>
typedef struct process {
    HANDLE Handle;
    DWORD ID;
    TCHAR UserName[UNLEN];
    DWORD BasePriority;
    double PercentProcessorTime;
    unsigned __int64 UsedMemory;
    DWORD ThreadCount;
    ULONGLONG UpTime;
    TCHAR ExeName[MAX_PATH];
    DWORD ParentPID;
    ULONGLONG DiskOperationsPrev;
    ULONGLONG DiskOperations;
    DWORD DiskUsage;
    DWORD TreeDepth;

    struct process *Next;
    struct process *Parent;
    struct process *FirstChild;
} process;
//typedef void (*FunctionPtr)( Napi::Object object);

static ULONGLONG SubtractTimes(const FILETIME *A, const FILETIME *B)
{
    LARGE_INTEGER lA, lB;

    lA.LowPart = A->dwLowDateTime;
    lA.HighPart = A->dwHighDateTime;

    lB.LowPart = B->dwLowDateTime;
    lB.HighPart = B->dwHighDateTime;

    return lA.QuadPart - lB.QuadPart;
}
// 标准输入句柄
HANDLE ConsoleHandle;
// 线程
static HANDLE ProcessListThread;

static CRITICAL_SECTION SyncLock;
static int init_lock = 0;
void *xmalloc(size_t size)
{
    void *m = malloc(size);

    return m;
}
void *xrealloc(void *ptr, size_t size)
{
    void *m = realloc(ptr, size);

    return m;
}

static double CPUUsage;

#define PROCLIST_BUF_INCREASE 64
static DWORD ProcessListSize = PROCLIST_BUF_INCREASE;
static process *ProcessList;
static process *NewProcessList;
static DWORD RunningProcessCount = 0;
static DWORD ProcessCount = 0;


extern std::map<std::string ,Napi::ThreadSafeFunction> name_tsfn;
extern std::set<std::string> name_set;
extern std::map<std::string,std::set<int>> name_pids;

static void IncreaseProcListSize(void)
{
    ProcessListSize += PROCLIST_BUF_INCREASE;
    NewProcessList = ( process *)xrealloc(NewProcessList, ProcessListSize * sizeof *NewProcessList);
    ProcessList = ( process *)xrealloc(ProcessList, ProcessListSize * sizeof *ProcessList);
}
static void PollProcessList(DWORD UpdateTime)
{
    HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);

    PROCESSENTRY32 processentry32;
    processentry32.dwSize = sizeof(processentry32);

    BOOL Status = Process32First(Snapshot, &processentry32);
    DWORD i = 0;
    // 遍历所有进程
    for(; Status; Status = Process32Next(Snapshot, &processentry32)) {

        process Process = { 0 };
        Process.ID = processentry32.th32ProcessID;
        if(Process.ID == 0)
            continue;
        Process.ThreadCount = processentry32.cntThreads;
        Process.BasePriority = processentry32.pcPriClassBase;
        Process.ParentPID = processentry32.th32ParentProcessID;

        _tcsncpy_s(Process.ExeName, MAX_PATH, processentry32.szExeFile, MAX_PATH);
        _tcsncpy_s(Process.UserName, UNLEN, _T("SYSTEM"), UNLEN);
        // 获取目标进程句柄
        Process.Handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processentry32.th32ProcessID);
        if(Process.Handle) {
            PROCESS_MEMORY_COUNTERS ProcMemCounters;
            if(GetProcessMemoryInfo(Process.Handle, &ProcMemCounters, sizeof(ProcMemCounters))) {
                Process.UsedMemory = (unsigned __int64)ProcMemCounters.WorkingSetSize;
            }
            HANDLE ProcessTokenHandle;
            if(OpenProcessToken(Process.Handle, TOKEN_READ, &ProcessTokenHandle)) {
                // 访问句柄
                DWORD ReturnLength;

                GetTokenInformation(ProcessTokenHandle, TokenUser, 0, 0, &ReturnLength);
                if(GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    PTOKEN_USER TokenUserStruct = (PTOKEN_USER)xmalloc(ReturnLength);

                    if(GetTokenInformation(ProcessTokenHandle, TokenUser, TokenUserStruct, ReturnLength, &ReturnLength)) {
                        SID_NAME_USE NameUse;
                        DWORD NameLength = UNLEN;
                        TCHAR DomainName[MAX_PATH];
                        DWORD DomainLength = MAX_PATH;
                        LookupAccountSid(0, TokenUserStruct->User.Sid, Process.UserName, &NameLength, DomainName, &DomainLength, &NameUse);
                        Process.UserName[9] = 0;
                    }
                    free(TokenUserStruct);
                }
                CloseHandle(ProcessTokenHandle);
            }
        }
        NewProcessList[i++] = Process;

        if(i >= ProcessListSize) {
            IncreaseProcListSize();
        }
    }
    CloseHandle(Snapshot);

    DWORD NewProcessCount = i;
    typedef struct system_times
    {// cpu只能获取运行的时间，cpu只有这一个信息可以获取，其它信息需要计算出来
        FILETIME
                IdleTime,//空闲时间
        KernelTime,//内核时间
        UserTime;//用户时间
    } system_times;

    system_times PrevSysTimes;

    GetSystemTimes(&PrevSysTimes.IdleTime, &PrevSysTimes.KernelTime, &PrevSysTimes.UserTime);

    typedef struct process_times
    {
        FILETIME CreationTime, ExitTime, KernelTime, UserTime;
    } process_times;

    process_times *ProcessTimes = (process_times *)xmalloc(NewProcessCount * sizeof(*ProcessTimes));

    for(DWORD ProcIndex = 0; ProcIndex < NewProcessCount; ProcIndex++) {
        // 遍历这一次获取到的所有进程 获取进程的额外信息
        process *Process = &NewProcessList[ProcIndex];
        process_times *ProcessTime = &ProcessTimes[ProcIndex];
        if(Process->Handle) {
            GetProcessTimes(Process->Handle, &ProcessTime->CreationTime, &ProcessTime->ExitTime, &ProcessTime->KernelTime, &ProcessTime->UserTime);
            IO_COUNTERS IoCounters;
            if(GetProcessIoCounters(Process->Handle, &IoCounters)) {
                Process->DiskOperationsPrev = IoCounters.ReadTransferCount + IoCounters.WriteTransferCount;
            }
        }
    }

    // 暂停一下重新获取信息
    Sleep(UpdateTime);

    system_times SysTimes;
    GetSystemTimes(&SysTimes.IdleTime, &SysTimes.KernelTime, &SysTimes.UserTime);
    ULONGLONG SysKernelDiff = SubtractTimes(&SysTimes.KernelTime, &PrevSysTimes.KernelTime);
    ULONGLONG SysUserDiff = SubtractTimes(&SysTimes.UserTime, &PrevSysTimes.UserTime);
    ULONGLONG SysIdleDiff = SubtractTimes(&SysTimes.IdleTime, &PrevSysTimes.IdleTime);

    RunningProcessCount = 0;
    for(DWORD ProcIndex = 0; ProcIndex < NewProcessCount; ProcIndex++) {
        process_times ProcessTime = { 0 };
        process_times *PrevProcessTime = &ProcessTimes[ProcIndex];
        process *Process = &NewProcessList[ProcIndex];
        if(Process->Handle) {
            GetProcessTimes(Process->Handle, &ProcessTime.CreationTime, &ProcessTime.ExitTime, &ProcessTime.KernelTime, &ProcessTime.UserTime);
            ULONGLONG ProcKernelDiff = SubtractTimes(&ProcessTime.KernelTime, &PrevProcessTime->KernelTime);
            ULONGLONG ProcUserDiff = SubtractTimes(&ProcessTime.UserTime, &PrevProcessTime->UserTime);
            ULONGLONG TotalSys = SysKernelDiff + SysUserDiff;
            ULONGLONG TotalProc = ProcKernelDiff + ProcUserDiff;
            if(TotalSys > 0) {
                Process->PercentProcessorTime = (double)((100.0 * (double)TotalProc) / (double)TotalSys);
                if(Process->PercentProcessorTime >= 0.01) {
                    RunningProcessCount++;
                }
            }
            FILETIME SysTime;
            GetSystemTimeAsFileTime(&SysTime);

            Process->UpTime = SubtractTimes(&SysTime, &ProcessTime.CreationTime) / 10000;

            IO_COUNTERS IoCounters;
            if(GetProcessIoCounters(Process->Handle, &IoCounters)) {
                Process->DiskOperations = IoCounters.ReadTransferCount + IoCounters.WriteTransferCount;
                Process->DiskUsage = (DWORD)((Process->DiskOperations - Process->DiskOperationsPrev) * (1000/UpdateTime));
            }

            CloseHandle(Process->Handle);
            Process->Handle = 0;
        }
    }
    free(ProcessTimes);
    ULONGLONG SysTime = SysKernelDiff + SysUserDiff;
    if(SysTime > 0) {
        double Percentage = (double)(SysTime - SysIdleDiff) / (double)SysTime;
        CPUUsage = std::min(Percentage, 1.0);
    }
    EnterCriticalSection(&SyncLock);
    memcpy(ProcessList, NewProcessList, NewProcessCount * sizeof *ProcessList);
    ProcessCount = NewProcessCount;
    LeaveCriticalSection(&SyncLock);
}
DWORD WINAPI PollProcessListThreadProc(LPVOID lpParam)
{
    UNREFERENCED_PARAMETER(lpParam);

    for(;;)  {
        if (name_set.size() == 0) {
            break;
        }
    PollProcessList(1000);
    }
    return 0;
}

// 向js监听函数发送数据
void send_data_to_on(){
    // 只能保证在作用域范围内清楚变量，所以这里要使用一个函数来做这一切

    // 全部的进程
//    std::vector<process>  all_process ;

//    for(DWORD i = 0; i < ProcessCount; i++) {
//        const process *Process = &ProcessList[i];
////        Napi::Object object =  Napi::Object::New(env);
////        object.Set("id",Process->ID);
////        object.Set("user_name",Process->UserName);
////        // 使用率
////        object.Set("cpu",Process->PercentProcessorTime);
////        // 字节
////        object.Set("mem",Process->UsedMemory);
////        object.Set("name",Process->ExeName);
//        // 这样效率最高，但是要和linux下获取系统cpu同步 先使用全部的
////            callback.Call({object});
//        all_process.push_back(*Process);
//    }
    for (const  auto& name : name_set) {
            Napi::ThreadSafeFunction  tsfn = name_tsfn[name];
            std::set<int> pid_set = name_pids[name];
          tsfn.BlockingCall([pid_set](Napi::Env env, Napi::Function jsCallback) {
            // 但是这里是可以使用 env的
            Napi::HandleScope scope(Napi::Env);
            Napi::Array  list = Napi::Array::New(env);
            int push_count = 0;
            for (size_t i = 0; i < ProcessCount; ++i) {
                const process process = ProcessList[i];
                 if (pid_set.size()!=0 && pid_set.find(process.ID) == pid_set.end()) {
                    continue;
                }
                Napi::Object object =  Napi::Object::New(env);
                object.Set("id",process.ID);
                object.Set("user_name",process.UserName);
                // 使用率
                object.Set("cpu",process.PercentProcessorTime);
                // 字节
                object.Set("mem",process.UsedMemory);
                object.Set("name",process.ExeName);
                list.Set(push_count,object);
                push_count++;
            }
            // 内部线程调用jsFunc(就是外部传进来的handler)子函数
            jsCallback.Call({ list});
        });
    }


}

void start() {

    // 初始化锁，没有对象概念，需要初始化数据
    if (init_lock ==0 ) {
        InitializeCriticalSection(&SyncLock);
        init_lock = 1;
    }
    ProcessList = (process *)xmalloc(ProcessListSize * sizeof *ProcessList);
    NewProcessList = (process *)xmalloc(ProcessListSize * sizeof *ProcessList);
    // 为线程获取创建一个线程
    ProcessListThread = CreateThread(0, 0,
                                     PollProcessListThreadProc, // 线程要执行的函数
                                     0, 0, 0);
    for(;;) {
        if (name_set.size() == 0) {
            break;
        }
        if (ProcessCount==0) {
            Sleep(500);
            continue;
        }
        EnterCriticalSection(&SyncLock);
        send_data_to_on();
        LeaveCriticalSection(&SyncLock);
        Sleep(500);
    };
}

void stop(std::string name) {
     if (init_lock ==0 ) {
        InitializeCriticalSection(&SyncLock);
        init_lock = 1;
    }
     EnterCriticalSection(&SyncLock);
     name_set.erase(name);
    name_pids.erase(name);
    name_tsfn[name].Release();
    name_tsfn.erase(name);
    LeaveCriticalSection(&SyncLock);

    // 竞争失败会报错。从而让整个线程退出，先使用这个办法
//    EnterCriticalSection(&SyncLock);

}


void set_pids(std::string name,std::set<int> pid_set) {
    if (init_lock ==0 ) {
        InitializeCriticalSection(&SyncLock);
        init_lock = 1;
    }
    EnterCriticalSection(&SyncLock);
     name_pids[name] = pid_set;
    LeaveCriticalSection(&SyncLock);
}

#pragma clang diagnostic pop

#endif
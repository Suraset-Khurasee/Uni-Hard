/*
========================================================================================================
 MIT License

 Copyright (c) 2026 gafoo

 This file is part of the HardView project:
 https://github.com/gafoo173/HardView

 Licensed under the MIT License
 See the LICENSE file in the project root for more details.
========================================================================================================
Process6 - Lightweight C++ Library for Cross-Platform Process Management (MIT Licensed)
Supports both Windows (64-bit) and Linux (/proc, process_vm_readv/writev, signals, priority).
========================================================================================================
*/
#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <map>
#include <unordered_map>
#include <stdexcept>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <Winsvc.h>
#include <processthreadsapi.h>
#include <winternl.h>
#pragma comment(lib, "ntdll.lib")

typedef struct {
    BYTE Reserved1[16];
    PVOID Reserved2[5];
    UNICODE_STRING CurrentDirectoryPath;
    PVOID CurrentDirectoryHandle;
    UNICODE_STRING DllPath;
    UNICODE_STRING ImagePathName;
    UNICODE_STRING CommandLine;
    LPCWSTR env;
} RTL_USER_PROCESS_PARAMETERS_, * PRTL_USER_PROCESS_PARAMETERS_;

typedef struct {
    BYTE Reserved1[2];
    BYTE BeingDebugged;
    BYTE Reserved2[21];
    PVOID LoaderData;
    PRTL_USER_PROCESS_PARAMETERS_ ProcessParameters;
} PEB_;

#else // Linux / POSIX
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/uio.h>

typedef uint8_t BYTE;
typedef uint32_t DWORD;
typedef uint32_t ULONG;
typedef uint64_t ULONGLONG;
typedef size_t SIZE_T;
typedef int BOOL;
typedef long KPRIORITY;
typedef void* HANDLE;
typedef void* HMODULE;
typedef const char* LPCSTR;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif

// Priority class constants matching Windows API values
#ifndef IDLE_PRIORITY_CLASS
#define IDLE_PRIORITY_CLASS         0x00000040
#define BELOW_NORMAL_PRIORITY_CLASS 0x00004000
#define NORMAL_PRIORITY_CLASS       0x00000020
#define ABOVE_NORMAL_PRIORITY_CLASS 0x00008000
#define HIGH_PRIORITY_CLASS         0x00000080
#define REALTIME_PRIORITY_CLASS     0x00000100
#define PROCESS_ALL_ACCESS          0x001FFFFF
#endif

struct PROCESSENTRY32W {
    DWORD dwSize = sizeof(PROCESSENTRY32W);
    DWORD cntUsage = 0;
    DWORD th32ProcessID = 0;
    uintptr_t th32DefaultHeapID = 0;
    DWORD th32ModuleID = 0;
    DWORD cntThreads = 0;
    DWORD th32ParentProcessID = 0;
    long pcPriClassBase = 0;
    DWORD dwFlags = 0;
    wchar_t szExeFile[260] = {0};
};

inline DWORD GetLastError() {
    return static_cast<DWORD>(errno);
}
#endif

struct PROCESS_INFO_EX {
    ULONG NumberOfThreads = 0;
    std::wstring ImageName;
    KPRIORITY BasePriority = 0;
    DWORD PriorityClass = 0;
    HANDLE UniqueProcessId = nullptr;
    ULONG HandleCount = 0;
    ULONG SessionId = 0;
    SIZE_T PeakVirtualSize = 0;
    SIZE_T VirtualSize = 0;
    ULONG Reserved4 = 0;
    SIZE_T PeakWorkingSetSize = 0;
    SIZE_T WorkingSetSize = 0;
    SIZE_T QuotaPagedPoolUsage = 0;
    SIZE_T QuotaNonPagedPoolUsage = 0;
    SIZE_T PagefileUsage = 0;
    SIZE_T PeakPagefileUsage = 0;
    SIZE_T PrivatePageCount = 0;
};

inline std::string GetPriorityName(DWORD priority) {
    switch (priority) {
    case ABOVE_NORMAL_PRIORITY_CLASS: return "ABOVE_NORMAL_PRIORITY_CLASS";
    case BELOW_NORMAL_PRIORITY_CLASS: return "BELOW_NORMAL_PRIORITY_CLASS";
    case HIGH_PRIORITY_CLASS: return "HIGH_PRIORITY_CLASS";
    case IDLE_PRIORITY_CLASS: return "IDLE_PRIORITY_CLASS";
    case NORMAL_PRIORITY_CLASS: return "NORMAL_PRIORITY_CLASS";
    case REALTIME_PRIORITY_CLASS: return "REALTIME_PRIORITY_CLASS";
    default: return "UNKNOWN_PRIORITY_CLASS";
    }
}

#ifdef _WIN32

inline bool GetProcessPriority(DWORD pid, DWORD& priorityOut) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) return false;
    int prio = GetPriorityClass(hProcess);
    CloseHandle(hProcess);
    if (!prio) return false;
    priorityOut = prio;
    return true;
}

inline bool EnablePrivilege(LPCSTR privilegeName, HANDLE hProc = nullptr) {
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    LUID luid;
    if (!hProc) hProc = GetCurrentProcess();
    if (!OpenProcessToken(hProc, TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }
    if (!LookupPrivilegeValueA(NULL, privilegeName, &luid)) {
        CloseHandle(hToken);
        return false;
    }
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
        CloseHandle(hToken);
        return false;
    }
    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        CloseHandle(hToken);
        return false;
    }
    CloseHandle(hToken);
    return true;
}

#else // Linux helpers

inline std::wstring Utf8ToWString(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

inline DWORD NiceToPriorityClass(int nice_val) {
    if (nice_val <= -15) return REALTIME_PRIORITY_CLASS;
    if (nice_val <= -10) return HIGH_PRIORITY_CLASS;
    if (nice_val < 0)    return ABOVE_NORMAL_PRIORITY_CLASS;
    if (nice_val == 0)   return NORMAL_PRIORITY_CLASS;
    if (nice_val <= 10)  return BELOW_NORMAL_PRIORITY_CLASS;
    return IDLE_PRIORITY_CLASS;
}

inline int PriorityClassToNice(DWORD pc) {
    switch (pc) {
        case REALTIME_PRIORITY_CLASS:     return -20;
        case HIGH_PRIORITY_CLASS:         return -10;
        case ABOVE_NORMAL_PRIORITY_CLASS: return -5;
        case NORMAL_PRIORITY_CLASS:       return 0;
        case BELOW_NORMAL_PRIORITY_CLASS: return 5;
        case IDLE_PRIORITY_CLASS:         return 19;
        default:                          return 0;
    }
}

inline bool GetProcessPriority(DWORD pid, DWORD& priorityOut) {
    errno = 0;
    int nice_val = getpriority(PRIO_PROCESS, static_cast<id_t>(pid));
    if (nice_val == -1 && errno != 0) {
        priorityOut = NORMAL_PRIORITY_CLASS;
        return false;
    }
    priorityOut = NiceToPriorityClass(nice_val);
    return true;
}

inline bool EnablePrivilege(LPCSTR privilegeName, HANDLE hProc = nullptr) {
    (void)privilegeName;
    (void)hProc;
    return (geteuid() == 0);
}

#endif

struct Module {
    HMODULE hModule = nullptr;
    std::string name;
    unsigned long size = 0;
};

class ProcessControl {
    HANDLE hProcess = nullptr;
    DWORD PID = 0;

#ifndef _WIN32
    static inline bool ParseLinuxProcInfo(DWORD pid, PROCESS_INFO_EX& info, DWORD* parent_pid_out = nullptr) {
        namespace fs = std::filesystem;
        std::string proc_dir = "/proc/" + std::to_string(pid);
        if (!fs::exists(proc_dir)) return false;

        info.UniqueProcessId = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(pid));
        GetProcessPriority(pid, info.PriorityClass);

        // Read /proc/<pid>/stat
        std::ifstream stat_f(proc_dir + "/stat");
        if (stat_f.is_open()) {
            std::string line;
            std::getline(stat_f, line);
            auto lparen = line.find('(');
            auto rparen = line.rfind(')');
            if (lparen != std::string::npos && rparen != std::string::npos && rparen > lparen) {
                std::string comm = line.substr(lparen + 1, rparen - lparen - 1);
                info.ImageName = Utf8ToWString(comm);
                std::stringstream ss(line.substr(rparen + 2));
                char state = 'R';
                long ppid = 0, pgrp = 0, session = 0, tty_nr = 0, tpgid = 0;
                unsigned long flags = 0, minflt = 0, cminflt = 0, majflt = 0, cmajflt = 0, utime = 0, stime = 0;
                long cutime = 0, cstime = 0, priority = 0, nice_v = 0, num_threads = 1, itrealvalue = 0;
                unsigned long long starttime = 0, vsize = 0;
                long rss = 0;
                ss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
                   >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime
                   >> cutime >> cstime >> priority >> nice_v >> num_threads >> itrealvalue
                   >> starttime >> vsize >> rss;
                if (parent_pid_out) *parent_pid_out = static_cast<DWORD>(ppid);
                info.SessionId = static_cast<ULONG>(session > 0 ? session : 0);
                info.BasePriority = priority;
                info.NumberOfThreads = static_cast<ULONG>(num_threads > 0 ? num_threads : 1);
                info.VirtualSize = static_cast<SIZE_T>(vsize);
                long page_sz = sysconf(_SC_PAGESIZE);
                if (page_sz <= 0) page_sz = 4096;
                info.WorkingSetSize = static_cast<SIZE_T>(rss > 0 ? rss * page_sz : 0);
            }
        }

        // Read /proc/<pid>/status for peak sizes
        std::ifstream status_f(proc_dir + "/status");
        if (status_f.is_open()) {
            std::string line;
            while (std::getline(status_f, line)) {
                if (line.rfind("VmPeak:", 0) == 0) {
                    std::stringstream ss(line.substr(7));
                    size_t kb = 0; ss >> kb;
                    info.PeakVirtualSize = kb * 1024;
                } else if (line.rfind("VmHWM:", 0) == 0) {
                    std::stringstream ss(line.substr(6));
                    size_t kb = 0; ss >> kb;
                    info.PeakWorkingSetSize = kb * 1024;
                } else if (line.rfind("VmData:", 0) == 0) {
                    std::stringstream ss(line.substr(7));
                    size_t kb = 0; ss >> kb;
                    info.PrivatePageCount = (kb * 1024) / 4096;
                } else if (line.rfind("VmSwap:", 0) == 0) {
                    std::stringstream ss(line.substr(7));
                    size_t kb = 0; ss >> kb;
                    info.PagefileUsage = kb * 1024;
                    info.PeakPagefileUsage = kb * 1024;
                }
            }
        }

        // Count open file descriptors in /proc/<pid>/fd if readable
        std::error_code ec;
        ULONG fd_count = 0;
        for (const auto& _ : fs::directory_iterator(proc_dir + "/fd", ec)) {
            (void)_;
            fd_count++;
        }
        info.HandleCount = fd_count;
        return true;
    }
#endif

public:
    ProcessControl(DWORD pid, bool EnableDebugPrivilege = false, DWORD Access = PROCESS_ALL_ACCESS) : PID(pid) {
#ifdef _WIN32
        if (EnableDebugPrivilege) {
            if (!EnablePrivilege("SeDebugPrivilege")) {
                throw std::runtime_error("Unable to enable SE_DEBUG_NAME privilege");
            }
        }
        hProcess = OpenProcess(Access, FALSE, PID);
        if (!hProcess) {
            throw std::runtime_error("Failed to open process Error: " +
                                     std::to_string(GetLastError()));
        }
#else
        (void)Access;
        if (EnableDebugPrivilege && geteuid() != 0) {
            throw std::runtime_error("Unable to enable debug privilege (not root)");
        }
        if (!std::filesystem::exists("/proc/" + std::to_string(PID))) {
            throw std::runtime_error("Failed to open process " + std::to_string(PID) + ": No such process");
        }
        hProcess = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(PID));
#endif
    }

    ~ProcessControl() {
#ifdef _WIN32
        if (hProcess) CloseHandle(hProcess);
#endif
    }

    size_t GetRamUsage() {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
            return pmc.WorkingSetSize;
        }
        return 0;
#else
        PROCESS_INFO_EX info;
        if (ParseLinuxProcInfo(PID, info)) {
            return info.WorkingSetSize;
        }
        return 0;
#endif
    }

    bool GetCPUTimes(size_t& kernal, size_t& user, size_t& create, size_t& exit) {
#ifdef _WIN32
        FILETIME ftCreation, ftExit, ftKernel1, ftUser1;
        if (!GetProcessTimes(hProcess, &ftCreation, &ftExit, &ftKernel1, &ftUser1)) {
            return false;
        }
        ULARGE_INTEGER Tmp;
        Tmp.LowPart = ftKernel1.dwLowDateTime;
        Tmp.HighPart = ftKernel1.dwHighDateTime;
        kernal = Tmp.QuadPart;

        Tmp.LowPart = ftUser1.dwLowDateTime;
        Tmp.HighPart = ftUser1.dwHighDateTime;
        user = Tmp.QuadPart;

        Tmp.LowPart = ftCreation.dwLowDateTime;
        Tmp.HighPart = ftCreation.dwHighDateTime;
        create = Tmp.QuadPart;

        Tmp.LowPart = ftExit.dwLowDateTime;
        Tmp.HighPart = ftExit.dwHighDateTime;
        exit = Tmp.QuadPart;

        return true;
#else
        std::ifstream stat_f("/proc/" + std::to_string(PID) + "/stat");
        if (!stat_f.is_open()) return false;
        std::string line;
        std::getline(stat_f, line);
        auto rparen = line.rfind(')');
        if (rparen == std::string::npos) return false;
        std::stringstream ss(line.substr(rparen + 2));
        char state = 'R';
        long ppid = 0, pgrp = 0, session = 0, tty_nr = 0, tpgid = 0;
        unsigned long flags = 0, minflt = 0, cminflt = 0, majflt = 0, cmajflt = 0, utime = 0, stime = 0;
        long cutime = 0, cstime = 0, priority = 0, nice_v = 0, num_threads = 1, itrealvalue = 0;
        unsigned long long starttime = 0;
        ss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
           >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime
           >> cutime >> cstime >> priority >> nice_v >> num_threads >> itrealvalue
           >> starttime;
        long ticks_per_sec = sysconf(_SC_CLK_TCK);
        if (ticks_per_sec <= 0) ticks_per_sec = 100;
        // Convert clock ticks to 100-ns units to match Windows FILETIME scale
        const size_t scale = static_cast<size_t>(10000000ULL / static_cast<unsigned long long>(ticks_per_sec));
        kernal = static_cast<size_t>(stime) * scale;
        user = static_cast<size_t>(utime) * scale;
        create = static_cast<size_t>(starttime) * scale;
        exit = 0;
        return true;
#endif
    }

    std::vector<Module> GetModules() {
#ifdef _WIN32
        DWORD cbNeeded;
        HMODULE hmods[1024];
        if (EnumProcessModules(hProcess, hmods, sizeof(hmods), &cbNeeded)) {
            std::vector<Module> Modules;
            int modcount = cbNeeded / sizeof(HMODULE);
            for (int i = 0; i < modcount; i++) {
                char modnm[256];
                MODULEINFO mi;
                Module mod;
                mod.hModule = hmods[i];
                if (GetModuleFileNameExA(hProcess, hmods[i], modnm, sizeof(modnm))) {
                    mod.name = modnm;
                } else mod.name = "Unknown";
                if (GetModuleInformation(hProcess, hmods[i], &mi, sizeof(mi))) {
                    mod.size = mi.SizeOfImage;
                } else mod.size = 0;
                Modules.push_back(mod);
            }
            return Modules;
        }
        throw std::runtime_error("Failed to get modules");
#else
        std::ifstream maps_f("/proc/" + std::to_string(PID) + "/maps");
        if (!maps_f.is_open()) {
            throw std::runtime_error("Failed to open /proc/" + std::to_string(PID) + "/maps");
        }
        struct MapAccum {
            uintptr_t base = 0;
            uintptr_t end = 0;
        };
        std::map<std::string, MapAccum> by_path;
        std::vector<std::string> order;
        std::string line;
        while (std::getline(maps_f, line)) {
            auto slash = line.find('/');
            if (slash == std::string::npos) continue;
            std::string path = line.substr(slash);
            while (!path.empty() && (path.back() == '\n' || path.back() == '\r' || path.back() == ' '))
                path.pop_back();
            uintptr_t start_addr = 0, end_addr = 0;
            if (std::sscanf(line.c_str(), "%lx-%lx", &start_addr, &end_addr) == 2) {
                auto it = by_path.find(path);
                if (it == by_path.end()) {
                    by_path[path] = {start_addr, end_addr};
                    order.push_back(path);
                } else {
                    if (end_addr > it->second.end) it->second.end = end_addr;
                }
            }
        }
        std::vector<Module> result;
        for (const auto& p : order) {
            const auto& acc = by_path[p];
            Module m;
            m.hModule = reinterpret_cast<HMODULE>(acc.base);
            m.name = p;
            m.size = static_cast<unsigned long>(acc.end > acc.base ? (acc.end - acc.base) : 0);
            result.push_back(m);
        }
        return result;
#endif
    }

    bool Suspend(bool onethread = false, DWORD threadid = 0) {
#ifdef _WIN32
        bool found = false;
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Failed to create thread snapshot.");

        THREADENTRY32 te;
        ZeroMemory(&te, sizeof(te));
        te.dwSize = sizeof(te);

        if (!Thread32First(hSnapshot, &te)) {
            CloseHandle(hSnapshot);
            throw std::runtime_error("Failed to retrieve first thread.");
        }

        do {
            if (te.th32OwnerProcessID == PID && (!onethread || te.th32ThreadID == threadid)) {
                HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (!hThread) {
                    CloseHandle(hSnapshot);
                    throw std::runtime_error("Failed to open thread.");
                }
                if (SuspendThread(hThread) == (DWORD)-1) {
                    CloseHandle(hThread);
                    CloseHandle(hSnapshot);
                    throw std::runtime_error("Failed to suspend thread.");
                }
                CloseHandle(hThread);
                found = true;
            }
        } while (Thread32Next(hSnapshot, &te));

        CloseHandle(hSnapshot);
        return found;
#else
        pid_t target = onethread && threadid != 0 ? static_cast<pid_t>(threadid) : static_cast<pid_t>(PID);
        return (kill(target, SIGSTOP) == 0);
#endif
    }

    bool Resume(bool onethread = false, DWORD threadid = 0) {
#ifdef _WIN32
        bool found = false;
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Failed to create thread snapshot.");

        THREADENTRY32 te;
        ZeroMemory(&te, sizeof(te));
        te.dwSize = sizeof(te);

        if (!Thread32First(hSnapshot, &te)) {
            CloseHandle(hSnapshot);
            throw std::runtime_error("Failed to retrieve first thread.");
        }

        do {
            if (te.th32OwnerProcessID == PID && (!onethread || te.th32ThreadID == threadid)) {
                HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (!hThread) {
                    CloseHandle(hSnapshot);
                    throw std::runtime_error("Failed to open thread.");
                }
                if (ResumeThread(hThread) == (DWORD)-1) {
                    CloseHandle(hThread);
                    CloseHandle(hSnapshot);
                    throw std::runtime_error("Failed to resume thread.");
                }
                CloseHandle(hThread);
                found = true;
            }
        } while (Thread32Next(hSnapshot, &te));

        CloseHandle(hSnapshot);
        return found;
#else
        pid_t target = onethread && threadid != 0 ? static_cast<pid_t>(threadid) : static_cast<pid_t>(PID);
        return (kill(target, SIGCONT) == 0);
#endif
    }

    bool Kill(unsigned int exitcode = 0) {
#ifdef _WIN32
        return TerminateProcess(hProcess, exitcode);
#else
        (void)exitcode;
        return (kill(static_cast<pid_t>(PID), SIGKILL) == 0);
#endif
    }

    PROCESSENTRY32W GetProcessInfo() {
#ifdef _WIN32
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) {
            return {0};
        }
        PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (pe.th32ProcessID == PID) {
                    CloseHandle(snap);
                    return pe;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return {0};
#else
        PROCESSENTRY32W pe{};
        PROCESS_INFO_EX ex{};
        DWORD ppid = 0;
        if (ParseLinuxProcInfo(PID, ex, &ppid)) {
            pe.th32ProcessID = PID;
            pe.th32ParentProcessID = ppid;
            pe.cntThreads = ex.NumberOfThreads;
            pe.pcPriClassBase = ex.BasePriority;
            size_t copy_len = std::min<size_t>(ex.ImageName.size(), 259);
            std::wcsncpy(pe.szExeFile, ex.ImageName.c_str(), copy_len);
            pe.szExeFile[copy_len] = L'\0';
        }
        return pe;
#endif
    }

    bool ReadMemory(unsigned char* bytes, size_t size, void* address, SIZE_T* bytesRead = nullptr) {
#ifdef _WIN32
        return ReadProcessMemory(hProcess, address, bytes, size, bytesRead);
#else
        struct iovec local_iov = { bytes, size };
        struct iovec remote_iov = { address, size };
        ssize_t n = process_vm_readv(static_cast<pid_t>(PID), &local_iov, 1, &remote_iov, 1, 0);
        if (n >= 0) {
            if (bytesRead) *bytesRead = static_cast<SIZE_T>(n);
            return true;
        }
        // Fallback to /proc/<pid>/mem
        int fd = open(("/proc/" + std::to_string(PID) + "/mem").c_str(), O_RDONLY);
        if (fd < 0) return false;
        ssize_t r = pread(fd, bytes, size, reinterpret_cast<off_t>(address));
        close(fd);
        if (r < 0) return false;
        if (bytesRead) *bytesRead = static_cast<SIZE_T>(r);
        return true;
#endif
    }

    bool WriteMemory(unsigned char* bytes, size_t size, void* address, SIZE_T* bytesWritten = nullptr) {
#ifdef _WIN32
        return WriteProcessMemory(hProcess, address, bytes, size, bytesWritten);
#else
        struct iovec local_iov = { bytes, size };
        struct iovec remote_iov = { address, size };
        ssize_t n = process_vm_writev(static_cast<pid_t>(PID), &local_iov, 1, &remote_iov, 1, 0);
        if (n >= 0) {
            if (bytesWritten) *bytesWritten = static_cast<SIZE_T>(n);
            return true;
        }
        int fd = open(("/proc/" + std::to_string(PID) + "/mem").c_str(), O_WRONLY);
        if (fd < 0) return false;
        ssize_t w = pwrite(fd, bytes, size, reinterpret_cast<off_t>(address));
        close(fd);
        if (w < 0) return false;
        if (bytesWritten) *bytesWritten = static_cast<SIZE_T>(w);
        return true;
#endif
    }

    bool SetPriority(DWORD priorityClass) {
#ifdef _WIN32
        return SetPriorityClass(hProcess, priorityClass);
#else
        int nice_val = PriorityClassToNice(priorityClass);
        return (setpriority(PRIO_PROCESS, static_cast<id_t>(PID), nice_val) == 0);
#endif
    }

    DWORD GetPriority() {
#ifdef _WIN32
        return GetPriorityClass(hProcess);
#else
        DWORD p = NORMAL_PRIORITY_CLASS;
        GetProcessPriority(PID, p);
        return p;
#endif
    }

    PROCESS_INFO_EX GetProcessInfoEx() {
#ifdef _WIN32
        std::vector<BYTE> buffer;
        ULONG bytesneed = 0;
        NTSTATUS st = NtQuerySystemInformation(SystemProcessInformation, nullptr, 0, &bytesneed);
        if (bytesneed == 0) {
            throw std::runtime_error("NtQuerySystemInformation failed");
        }
        buffer.resize(bytesneed);
        st = NtQuerySystemInformation(SystemProcessInformation, buffer.data(), bytesneed, &bytesneed);
        if (!NT_SUCCESS(st)) {
            throw std::runtime_error("NtQuerySystemInformation failed NTSTATUS: " + std::to_string(st));
        }
        SYSTEM_PROCESS_INFORMATION* spi = reinterpret_cast<SYSTEM_PROCESS_INFORMATION*>(buffer.data());
        while (true) {
            if ((ULONGLONG)spi->UniqueProcessId == (ULONGLONG)PID) {
                PROCESS_INFO_EX info;
                info.NumberOfThreads = spi->NumberOfThreads;
                info.BasePriority = spi->BasePriority;
                info.UniqueProcessId = spi->UniqueProcessId;
                info.HandleCount = spi->HandleCount;
                info.SessionId = spi->SessionId;
                info.PeakVirtualSize = spi->PeakVirtualSize;
                info.VirtualSize = spi->VirtualSize;
                info.Reserved4 = spi->Reserved4;
                info.PeakWorkingSetSize = spi->PeakWorkingSetSize;
                info.WorkingSetSize = spi->WorkingSetSize;
                info.QuotaPagedPoolUsage = spi->QuotaPagedPoolUsage;
                info.QuotaNonPagedPoolUsage = spi->QuotaNonPagedPoolUsage;
                info.PagefileUsage = spi->PagefileUsage;
                info.PeakPagefileUsage = spi->PeakPagefileUsage;
                info.PrivatePageCount = spi->PrivatePageCount;
                if (spi->ImageName.Buffer) {
                    info.ImageName = std::wstring(spi->ImageName.Buffer, spi->ImageName.Length / sizeof(WCHAR));
                } else {
                    info.ImageName = L"";
                }
                GetProcessPriority(PID, info.PriorityClass);
                return info;
            }
            if (spi->NextEntryOffset == 0) break;
            spi = reinterpret_cast<SYSTEM_PROCESS_INFORMATION*>(reinterpret_cast<BYTE*>(spi) + spi->NextEntryOffset);
        }
        return {0};
#else
        PROCESS_INFO_EX info{};
        if (!ParseLinuxProcInfo(PID, info)) {
            throw std::runtime_error("Process not found: " + std::to_string(PID));
        }
        return info;
#endif
    }

#ifdef _WIN32
    RTL_USER_PROCESS_PARAMETERS_ LoadPP() {
        PROCESS_BASIC_INFORMATION pib;
        RTL_USER_PROCESS_PARAMETERS_ PP;
        NTSTATUS st = NtQueryInformationProcess(
            hProcess,
            ProcessBasicInformation,
            &pib,
            sizeof(pib),
            nullptr
        );
        if (!NT_SUCCESS(st)) {
            return {0};
        }
        PEB_ peb;
        if (!ReadProcessMemory(hProcess, pib.PebBaseAddress, &peb, sizeof(peb), nullptr)) {
            throw std::runtime_error("Failed to Read PEB");
        }
        if (!ReadProcessMemory(hProcess, peb.ProcessParameters, &PP, sizeof(PP), nullptr)) {
            throw std::runtime_error("Failed to Read CMD Line");
        }
        return PP;
    }
#endif

    std::wstring Getcmdline() {
#ifdef _WIN32
        auto PP = LoadPP();
        std::wstring x;
        x.resize((PP.CommandLine.Length / sizeof(WCHAR)));
        if (!ReadProcessMemory(hProcess, PP.CommandLine.Buffer, (void*)x.data(), PP.CommandLine.Length, nullptr)) {
            throw std::runtime_error("Failed to Read CMD Line");
        }
        return x;
#else
        std::ifstream f("/proc/" + std::to_string(PID) + "/cmdline", std::ios::binary);
        if (!f.is_open()) {
            throw std::runtime_error("Failed to Read CMD Line");
        }
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        for (char& c : content) {
            if (c == '\0') c = ' ';
        }
        while (!content.empty() && content.back() == ' ') content.pop_back();
        return Utf8ToWString(content);
#endif
    }

    std::wstring GetCD() {
#ifdef _WIN32
        auto PP = LoadPP();
        std::wstring x;
        x.resize((PP.CurrentDirectoryPath.Length / sizeof(WCHAR)));
        if (!ReadProcessMemory(hProcess, PP.CurrentDirectoryPath.Buffer, (void*)x.data(), PP.CurrentDirectoryPath.Length, nullptr)) {
            throw std::runtime_error("Failed to Read Current Directory");
        }
        return x;
#else
        std::error_code ec;
        auto target = std::filesystem::read_symlink("/proc/" + std::to_string(PID) + "/cwd", ec);
        if (ec) {
            throw std::runtime_error("Failed to Read Current Directory");
        }
        return target.wstring();
#endif
    }

    bool EnablePriv(std::string name) {
        return EnablePrivilege(name.c_str(), hProcess);
    }

    static std::unordered_map<std::wstring, DWORD> GetProcessMap() {
        std::unordered_map<std::wstring, DWORD> ProcMap = {};
#ifdef _WIN32
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) {
            return ProcMap;
        }
        PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
        if (!Process32FirstW(snap, &pe)) {
            CloseHandle(snap);
            return ProcMap;
        }
        do {
            ProcMap[std::wstring(pe.szExeFile)] = pe.th32ProcessID;
        } while (Process32NextW(snap, &pe));
        CloseHandle(snap);
        return ProcMap;
#else
        namespace fs = std::filesystem;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator("/proc", ec)) {
            std::string fn = entry.path().filename().string();
            if (fn.empty() || !std::all_of(fn.begin(), fn.end(), [](unsigned char c){ return std::isdigit(c); }))
                continue;
            DWORD pid = static_cast<DWORD>(std::strtoul(fn.c_str(), nullptr, 10));
            std::ifstream comm_f(entry.path().string() + "/comm");
            if (comm_f.is_open()) {
                std::string comm;
                std::getline(comm_f, comm);
                while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r'))
                    comm.pop_back();
                if (!comm.empty()) {
                    ProcMap[Utf8ToWString(comm)] = pid;
                }
            }
        }
        return ProcMap;
#endif
    }

    static std::unordered_map<unsigned long long, PROCESS_INFO_EX> GetProcessInfoMap() {
        std::unordered_map<unsigned long long, PROCESS_INFO_EX> infomap;
#ifdef _WIN32
        std::vector<BYTE> buffer;
        ULONG bytesneed = 0;
        NTSTATUS st = NtQuerySystemInformation(SystemProcessInformation, nullptr, 0, &bytesneed);
        if (bytesneed == 0) {
            throw std::runtime_error("NtQuerySystemInformation failed");
        }
        bytesneed = bytesneed + (bytesneed / 2);
        buffer.resize(bytesneed);
        st = NtQuerySystemInformation(SystemProcessInformation, buffer.data(), bytesneed, &bytesneed);
        if (!NT_SUCCESS(st)) {
            throw std::runtime_error("NtQuerySystemInformation failed NTSTATUS: " + std::to_string(st));
        }
        SYSTEM_PROCESS_INFORMATION* spi = reinterpret_cast<SYSTEM_PROCESS_INFORMATION*>(buffer.data());
        while (true) {
            unsigned long long pid = (unsigned long long)spi->UniqueProcessId;
            PROCESS_INFO_EX info;
            info.NumberOfThreads = spi->NumberOfThreads;
            info.BasePriority = spi->BasePriority;
            info.UniqueProcessId = spi->UniqueProcessId;
            info.HandleCount = spi->HandleCount;
            info.SessionId = spi->SessionId;
            info.PeakVirtualSize = spi->PeakVirtualSize;
            info.VirtualSize = spi->VirtualSize;
            info.Reserved4 = spi->Reserved4;
            info.PeakWorkingSetSize = spi->PeakWorkingSetSize;
            info.WorkingSetSize = spi->WorkingSetSize;
            info.QuotaPagedPoolUsage = spi->QuotaPagedPoolUsage;
            info.QuotaNonPagedPoolUsage = spi->QuotaNonPagedPoolUsage;
            info.PagefileUsage = spi->PagefileUsage;
            info.PeakPagefileUsage = spi->PeakPagefileUsage;
            info.PrivatePageCount = spi->PrivatePageCount;
            if (spi->ImageName.Buffer) {
                info.ImageName = std::wstring(spi->ImageName.Buffer, spi->ImageName.Length / sizeof(WCHAR));
            } else {
                info.ImageName = L"";
            }
            GetProcessPriority(pid, info.PriorityClass);
            infomap[pid] = info;
            if (spi->NextEntryOffset == 0) break;
            spi = reinterpret_cast<SYSTEM_PROCESS_INFORMATION*>(reinterpret_cast<BYTE*>(spi) + spi->NextEntryOffset);
        }
        return infomap;
#else
        namespace fs = std::filesystem;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator("/proc", ec)) {
            std::string fn = entry.path().filename().string();
            if (fn.empty() || !std::all_of(fn.begin(), fn.end(), [](unsigned char c){ return std::isdigit(c); }))
                continue;
            DWORD pid = static_cast<DWORD>(std::strtoul(fn.c_str(), nullptr, 10));
            PROCESS_INFO_EX info{};
            if (ParseLinuxProcInfo(pid, info)) {
                infomap[static_cast<unsigned long long>(pid)] = std::move(info);
            }
        }
        return infomap;
#endif
    }

#ifdef _WIN32
    static uint8_t* LowLevelProcessInfo() {
        ULONG bytesneed = (1024 * 1024);
        uint8_t* buffer = nullptr;
        while (true) {
            delete[] buffer;
            buffer = new uint8_t[bytesneed];
            NTSTATUS st = NtQuerySystemInformation(SystemProcessInformation, buffer, bytesneed, &bytesneed);
            if (st == 0xC0000004) continue;
            if (!NT_SUCCESS(st)) {
                delete[] buffer;
                throw std::runtime_error("NtQuerySystemInformation failed NTSTATUS: " + std::to_string(st));
            }
            break;
        }
        return buffer;
    }

    static void FreeBuffer(uint8_t* buffer) {
        if (buffer) delete[] buffer;
    }

    DWORD IsCritcal(BOOL& value) {
        if (!IsProcessCritical(hProcess, &value)) {
            return GetLastError();
        }
        return 0;
    }
#else
    DWORD IsCritcal(BOOL& value) {
        value = (PID == 1) ? TRUE : FALSE;
        return 0;
    }
#endif
};

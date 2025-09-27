#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <cwctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace
{
    std::wstring to_lower(std::wstring value)
    {
        for (auto& ch : value)
        {
            ch = static_cast<wchar_t>(::towlower(ch));
        }
        return value;
    }

    std::optional<DWORD> find_process_by_name(const std::wstring& name)
    {
        const std::wstring needle = to_lower(name);

        HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            std::wcerr << L"[error] CreateToolhelp32Snapshot failed: " << ::GetLastError() << std::endl;
            return std::nullopt;
        }

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(PROCESSENTRY32W);

        if (!::Process32FirstW(snapshot, &entry))
        {
            std::wcerr << L"[error] Process32FirstW failed: " << ::GetLastError() << std::endl;
            ::CloseHandle(snapshot);
            return std::nullopt;
        }

        std::optional<DWORD> pid;

        do
        {
            const std::wstring processName = to_lower(entry.szExeFile);
            if (processName == needle)
            {
                pid = entry.th32ProcessID;
                break;
            }
        } while (::Process32NextW(snapshot, &entry));

        ::CloseHandle(snapshot);
        return pid;
    }

    std::optional<DWORD> parse_target(const std::wstring& token)
    {
        try
        {
            size_t idx = 0;
            const unsigned long value = std::stoul(token, &idx, 10);
            if (idx == token.size())
            {
                return static_cast<DWORD>(value);
            }
        }
        catch (...)
        {
            // fallthrough
        }

        return find_process_by_name(token);
    }

    bool inject_dll(DWORD pid, const std::wstring& dllPath)
    {
        HANDLE process = ::OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
        if (!process)
        {
            std::wcerr << L"[error] OpenProcess failed for PID " << pid << L": " << ::GetLastError() << std::endl;
            return false;
        }

        const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
        LPVOID remotePath = ::VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remotePath)
        {
            std::wcerr << L"[error] VirtualAllocEx failed: " << ::GetLastError() << std::endl;
            ::CloseHandle(process);
            return false;
        }

        if (!::WriteProcessMemory(process, remotePath, dllPath.c_str(), bytes, nullptr))
        {
            std::wcerr << L"[error] WriteProcessMemory failed: " << ::GetLastError() << std::endl;
            ::VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            ::CloseHandle(process);
            return false;
        }

        const HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
        const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(::GetProcAddress(kernel32, "LoadLibraryW"));
        if (!loadLibrary)
        {
            std::wcerr << L"[error] Unable to resolve LoadLibraryW" << std::endl;
            ::VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            ::CloseHandle(process);
            return false;
        }

        HANDLE thread = ::CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath, 0, nullptr);
        if (!thread)
        {
            std::wcerr << L"[error] CreateRemoteThread failed: " << ::GetLastError() << std::endl;
            ::VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            ::CloseHandle(process);
            return false;
        }

        ::WaitForSingleObject(thread, INFINITE);

        DWORD exitCode = 0;
        ::GetExitCodeThread(thread, &exitCode);
        if (exitCode == 0)
        {
            std::wcerr << L"[warn] LoadLibraryW returned 0; check if the DLL path is valid." << std::endl;
        }

        ::CloseHandle(thread);
        ::VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        ::CloseHandle(process);

        std::wcout << L"[info] Injection completed (PID=" << pid << L")" << std::endl;
        return true;
    }
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 3)
    {
        std::wcerr << L"Usage: ef-overlay-injector <process-name-or-pid> <path-to-overlay-dll>" << std::endl;
        return 1;
    }

    const std::wstring targetToken = argv[1];
    std::filesystem::path dllPath(argv[2]);

    if (!std::filesystem::exists(dllPath))
    {
        std::wcerr << L"[error] DLL path does not exist: " << dllPath << std::endl;
        return 1;
    }

    dllPath = std::filesystem::absolute(dllPath);

    auto pidOpt = parse_target(targetToken);
    if (!pidOpt.has_value())
    {
        std::wcerr << L"[error] Unable to resolve target process: " << targetToken << std::endl;
        return 1;
    }

    if (!inject_dll(*pidOpt, dllPath.wstring()))
    {
        return 1;
    }

    return 0;
}

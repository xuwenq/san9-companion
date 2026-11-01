#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>

namespace san9 {
inline std::filesystem::path executableFile(const wchar_t* name) {
    static const auto directory = [] {
        wchar_t module[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, module, _countof(module));
        if (!length || length >= _countof(module))
            throw std::runtime_error("cannot locate executable directory");
        return std::filesystem::path(module).parent_path();
    }();
    return directory / name;
}
// 用户设置文件。与可执行文件同目录，便于整包分发。
inline std::filesystem::path preferencesFile() {
    return executableFile(L"San9PKHardSettings.ini");
}
}

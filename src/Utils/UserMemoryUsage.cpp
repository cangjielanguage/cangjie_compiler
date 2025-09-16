// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "UserMemoryUsage.h"

#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <sstream>

#if defined(_WIN32) && defined(__MINGW64__)
#include <windows.h>
#include <psapi.h>
#endif
#ifdef __APPLE__
#include <mach/task.h>
#include <mach/mach_init.h>
#endif
#include "cangjie/Basic/Color.h"
#include "cangjie/Utils/CheckUtils.h"

#if defined(__linux__) || (defined(_WIN32) && defined(__MINGW64__))
static const int PAGE_SIZE = 4;
#endif

static const int DISPLAY_PRECISION = 2;
static const int KILOBYTE = 1024;

namespace Cangjie {

std::string UserMemoryUsage::GetJson() const
{
    std::ostringstream out;
    out << "{";
    out << std::fixed;
    out.precision(DISPLAY_PRECISION);
    for (auto& phrase : titleOrder) {
        out << "\n   \"" + phrase + "\": {";
        auto it = titleInfoMap.find(phrase);
        CJC_ASSERT(it != titleInfoMap.end());
        for (auto& sec : it->second) {
            out << "\n      \"" + sec.subtitle + "\": " << sec.end << ",";
        }
        if (!it->second.empty()) {
            // remove last ','
            out.seekp(-1, std::ios_base::cur);
        }
        out << "   \n    },";
    }
    if (!titleOrder.empty()) {
        out.seekp(-1, std::ios_base::cur);
    }
    out << "\n}\n";
    return out.str();
}

void UserMemoryUsage::Start(const std::string& title, const std::string& subtitle, const std::string& desc)
{
    auto infos = titleInfoMap.find(title);
    if (std::count(titleOrder.begin(), titleOrder.end(), title) == 0) {
        titleOrder.push_back(title);
    }

    if (infos != titleInfoMap.end()) {
        auto it = std::find_if(infos->second.begin(), infos->second.end(),
            [&subtitle](const Info& info) { return info.subtitle == subtitle; });
        if (it != infos->second.end()) {
            // overwrite existing info
            it->start = Sampling();
            return;
        }
    }
    titleInfoMap[title].emplace_back(Info(title, subtitle, desc, Sampling()));
}

void UserMemoryUsage::Stop(const std::string& title, const std::string& subtitle, const std::string& /* desc */)
{
    auto infos = titleInfoMap.find(title);
    if (infos == titleInfoMap.end()) {
        return;
    }
    auto it = std::find_if(infos->second.begin(), infos->second.end(),
        [&subtitle](const Info& info) { return info.subtitle == subtitle; });
    if (it != infos->second.end()) {
        it->end = Sampling();
    }
}

// Memory Size Unit: MB
float UserMemoryUsage::Sampling()
{
#ifdef __linux__
    std::ifstream mem("/proc/self/statm");
    long totalProgramSize = 0;
    long residentSetSize = 0;
    mem >> totalProgramSize >> residentSetSize;
    // `residentSetSize` means page size (usually 4 KB one page on x86 and x86_64 systems)
    return static_cast<float>(residentSetSize) * PAGE_SIZE / KILOBYTE;
#elif defined(_WIN32) && defined(__MINGW64__)
    HANDLE hProcess;
    PROCESS_MEMORY_COUNTERS pmc;
    auto curPid = GetCurrentProcessId();
    // obtain the process handle.
    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, curPid);
    CJC_ASSERT(hProcess && "Get process handle failed");
    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
        return float(pmc.WorkingSetSize) / KILOBYTE / KILOBYTE;
    } else {
        CJC_ASSERT(false && "Get process memory info failed.");
    }
    CloseHandle(hProcess);
    return 0.0f;
#elif defined(__APPLE__)
    task_basic_info_data_t info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return float(info.resident_size) / KILOBYTE / KILOBYTE;
    } else {
        CJC_ASSERT(false && "Get process memory info failed.");
    }
    return 0.0f;
#else
    // Other platforms need to be adapted.
    CJC_ASSERT(false && "Not support for current platform.");
    return 0.0f;
#endif
}
} // namespace Cangjie

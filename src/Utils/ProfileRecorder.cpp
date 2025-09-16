// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/Utils/ProfileRecorder.h"

#include "UserBase.h"
#include "UserCodeInfo.h"
#include "UserMemoryUsage.h"
#include "UserTimer.h"

using namespace Cangjie;
using namespace Cangjie::Utils;

namespace {
inline bool operator&(ProfileRecorder::Type a, ProfileRecorder::Type b)
{
    return static_cast<bool>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}
} // namespace

ProfileRecorder::ProfileRecorder(
    const std::string& title, const std::string& subtitle, const std::string& desc)
    : title_(title), subtitle_(subtitle), desc_(desc)
{
    Start(title, subtitle, desc);
}

ProfileRecorder::~ProfileRecorder()
{
#ifndef CANGJIE_ENABLE_GCOV
    try {
#endif
        Stop(title_, subtitle_, desc_);
#ifndef CANGJIE_ENABLE_GCOV
    } catch (...) {
        // The try-catch block is placed here as the used functions above do not declare with noexcept.
        // We shall avoid any exception occurring in the destruction function.
    }
#endif
}

void ProfileRecorder::SetPackageName(const std::string& name)
{
    UserTimer::Instance().SetPackageName(name);
    UserMemoryUsage::Instance().SetPackageName(name);
    UserCodeInfo::Instance().SetPackageName(name);
}

void ProfileRecorder::SetOutputDir(const std::string& path)
{
    UserTimer::Instance().SetOutputDir(path);
    UserMemoryUsage::Instance().SetOutputDir(path);
    UserCodeInfo::Instance().SetOutputDir(path);
}

void ProfileRecorder::Start(
    const std::string& title, const std::string& subtitle, const std::string& desc)
{
    if (UserTimer::Instance().IsEnable()) {
        UserTimer::Instance().Start(title, subtitle, desc);
    }
    if (UserMemoryUsage::Instance().IsEnable()) {
        UserMemoryUsage::Instance().Start(title, subtitle, desc);
    }
}

void ProfileRecorder::Stop(
    const std::string& title, const std::string& subtitle, const std::string& desc)
{
    if (UserTimer::Instance().IsEnable()) {
        UserTimer::Instance().Stop(title, subtitle, desc);
    }
    if (UserMemoryUsage::Instance().IsEnable()) {
        UserMemoryUsage::Instance().Stop(title, subtitle, desc);
    }
}

void ProfileRecorder::RecordCodeInfo(const std::string& item, const std::function<int64_t(void)>& getData)
{
    if (UserTimer::Instance().IsEnable() || UserMemoryUsage::Instance().IsEnable()) {
        UserCodeInfo::Instance().RecordInfo(item, getData());
    }
}

void ProfileRecorder::RecordCodeInfo(const std::string& item, int64_t value)
{
    UserCodeInfo::Instance().RecordInfo(item, value);
}

void ProfileRecorder::Enable(bool en, const Type& type)
{
    if (type & ProfileRecorder::Type::TIMER) {
        UserTimer::Instance().Enable(en);
    }
    if (type & ProfileRecorder::Type::MEMORY) {
        UserMemoryUsage::Instance().Enable(en);
    }
    if (UserTimer::Instance().IsEnable() || UserMemoryUsage::Instance().IsEnable()) {
        UserCodeInfo::Instance().Enable(true);
    } else {
        UserCodeInfo::Instance().Enable(false);
    }
}

std::string ProfileRecorder::GetResult(const Type& type)
{
    std::string result;

    if ((type & ProfileRecorder::Type::TIMER) || (type & ProfileRecorder::Type::MEMORY)) {
        result += UserCodeInfo::Instance().GetResult();
    }
    if (type & ProfileRecorder::Type::TIMER) {
        result += UserTimer::Instance().GetResult();
    }
    if (type & ProfileRecorder::Type::MEMORY) {
        result += UserMemoryUsage::Instance().GetResult();
    }
    return result;
}

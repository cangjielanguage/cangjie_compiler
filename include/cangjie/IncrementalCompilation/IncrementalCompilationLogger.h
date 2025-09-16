// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_INCREMENTAL_COMPILATION_LOGER_H
#define CANGJIE_INCREMENTAL_COMPILATION_LOGER_H

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/ConstantsUtils.h"

/// Log states of incremental compilation into log file
class IncrementalCompilationLogger {
public:
    static IncrementalCompilationLogger& GetInstance()
    {
        static IncrementalCompilationLogger logger = IncrementalCompilationLogger();
        return logger;
    }
    void SetDebugPrint(bool flag)
    {
        debugPrint = flag;
    }
    void InitLogFile(const std::string& logFilePath)
    {
        if (logFilePath.empty()) {
            return;
        }
        if (!Cangjie::FileUtil::HasExtension(logFilePath, "log")) {
            return;
        }
        auto realDirPath = Cangjie::FileUtil::GetAbsPath(Cangjie::FileUtil::GetDirPath(logFilePath));
        if (!realDirPath.has_value()) {
            return;
        }
        auto fileNameWithExt = Cangjie::FileUtil::GetFileName(logFilePath);
        auto ret = Cangjie::FileUtil::JoinPath(realDirPath.value(), fileNameWithExt);
        fileStream.open(ret, std::ofstream::out);
        if (fileStream.is_open()) {
            writeKind = WriteKind::FILE;
            saveLogFile = true;
        }
    }
    void LogLn(const std::string& input)
    {
        if (debugPrint) {
            std::cout << input << std::endl;
        }
        if (writeKind == WriteKind::FILE) {
            fileStream << input << std::endl;
        } else {
            strStream << input << std::endl;
        }
    }
    void Log(const std::string& input)
    {
        if (debugPrint) {
            std::cout << input;
        }
        if (writeKind == WriteKind::FILE) {
            fileStream << input;
        } else {
            strStream << input;
        }
    }
    bool IsEnable() const
    {
        return debugPrint || saveLogFile;
    }
    enum class WriteKind : uint8_t { BUFF, FILE };
    void SetWriteKind(WriteKind kind)
    {
        writeKind = kind;
    }
    void WriteBuffToFile()
    {
        if (writeKind == WriteKind::FILE) {
            if (fileStream.is_open()) {
                fileStream << strStream.str();
                writeKind = WriteKind::FILE;
            }
        }
    }

private:
    IncrementalCompilationLogger() {}
    ~IncrementalCompilationLogger() noexcept
    {
#ifndef CANGJIE_ENABLE_GCOV
        try {
#endif
            if (fileStream.is_open()) {
                fileStream.close();
            }
#ifndef CANGJIE_ENABLE_GCOV
        } catch (const std::exception& e) {
            // do nothing for noexcept
        } catch (...) {
            // do nothing for noexcept
        }
#endif
    }
    bool debugPrint{false};
    bool saveLogFile{false};
    std::ostringstream strStream;
    std::ofstream fileStream;
    WriteKind writeKind{WriteKind::BUFF};
};

#endif
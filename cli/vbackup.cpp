#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <string>

#include "GetOption.h"
#include "native/FileSystemAPI.h"
#include "VolumeProtector.h"
#include "Logger.h"

using namespace volumeprotect;
using namespace volumeprotect::task;
using namespace xuranus::getopt;
using namespace xuranus::minilogger;

static const char* g_helpMessage =
    "Volume Backup Cli\n"
    "==============================================================\n"
    "Options:\n"
    "-v | --volume=     \t  specify volume path\n"
    "-n | --name=       \t  specify copy name\n"
#ifdef _WIN32
    "-f | --format=     \t  specify copy format [BIN, IMAGE, VHD_FIXED, VHD_DYNAMIC, VHDX_FIXED, VHDX_DYNAMIC]\n"
#else
    "-f | --format=     \t  specify copy format [BIN, IMAGE]\n"
#endif
    "-d | --data=       \t  specify copy data directory\n"
    "-m | --meta=       \t  specify copy meta directory\n"
    "-p | --prevmeta=   \t  specify previous copy meta directory\n"
    "-r | --restore     \t  used when performing restore operation\n"
    "-l | --loglevel=   \t  specify logger level [INFO, DEBUG]\n"
    "-h | --help        \t  print help\n";

struct CliArgs {
    std::string     volumePath;
    std::string     copyName;
    CopyFormat      copyFormat;
    std::string     copyDataDirPath;
    std::string     copyMetaDirPath;
    std::string     prevCopyMetaDirPath;
    LoggerLevel     logLevel             { LoggerLevel::DEBUG };
    bool            isRestore            { false };
    bool            printHelp            { false };
};

static void PrintHelp()
{
    ::printf("%s\n", g_helpMessage);
}

static CopyFormat ParseCopyFormat(const std::string& copyFormat)
{
    CopyFormat copyFormatEnum = CopyFormat::BIN;
    if (copyFormat == "BIN") {
        copyFormatEnum = CopyFormat::BIN;
    } else if (copyFormat == "IMAGE") {
        copyFormatEnum = CopyFormat::IMAGE;
#ifdef _WIN32
    } else if (copyFormat == "VHD_FIXED") {
        copyFormatEnum = CopyFormat::VHD_FIXED;
    } else if (copyFormat == "VHD_DYNAMIC") {
        copyFormatEnum = CopyFormat::VHD_DYNAMIC;
    } else if (copyFormat == "VHDX_FIXED") {
        copyFormatEnum = CopyFormat::VHDX_FIXED;
    } else if (copyFormat == "VHDX_DYNAMIC") {
        copyFormatEnum = CopyFormat::VHDX_DYNAMIC;
#endif
    } else {
        std::cerr << "invalid copy format input: " << copyFormat << std::endl;
        assert(false);
    }
    return copyFormatEnum;
}

static LoggerLevel ParseLoggerLevel(const std::string& loggerLevelStr)
{
    LoggerLevel loggerLevel = LoggerLevel::DEBUG;
    if (loggerLevelStr == "INFO") {
        loggerLevel = LoggerLevel::INFO;
    }
    return loggerLevel;
}

static void PrintTaskStatistics(const TaskStatistics& statistics)
{
    ::printf("checkStatistics: bytesToReaded: %llu, bytesRead: %llu, "
        "blocksToHash: %llu, blocksHashed: %llu, "
        "bytesToWrite: %llu, bytesWritten: %llu\n",
        statistics.bytesToRead, statistics.bytesRead,
        statistics.blocksToHash, statistics.blocksHashed,
        statistics.bytesToWrite, statistics.bytesWritten);
}

static CliArgs ParseCliArgs(int argc, const char** argv)
{
    CliArgs cliAgrs;
    GetOptionResult result = GetOption(
        argv + 1, argc - 1,
        "v:n:f:d:m:p:h:r:l:",
        {"--volume=", "--name=", "--format", "--data=", "--meta=",
        "--prevmeta=", "--help", "--restore", "--loglevel="});
    for (const OptionResult opt: result.opts) {
        if (opt.option == "v" || opt.option == "volume") {
            cliAgrs.volumePath = opt.value;
        } else if (opt.option == "n" || opt.option == "name") {
            cliAgrs.copyName = opt.value;
        } else if (opt.option == "f" || opt.option == "format") {
            cliAgrs.copyFormat = ParseCopyFormat(opt.value);
        } else if (opt.option == "d" || opt.option == "data") {
            cliAgrs.copyDataDirPath = opt.value;
        } else if (opt.option == "m" || opt.option == "meta") {
            cliAgrs.copyMetaDirPath = opt.value;
        } else if (opt.option == "p" || opt.option == "prevmeta") {
            cliAgrs.prevCopyMetaDirPath = opt.value;
        } else if (opt.option == "r" || opt.option == "restore") {
            cliAgrs.isRestore = true;
        } else if (opt.option == "l" || opt.option == "loglevel") {
            cliAgrs.logLevel = ParseLoggerLevel(opt.value);
        } else if (opt.option == "h" || opt.option == "help") {
            cliAgrs.printHelp = true;
        }
    }
    return cliAgrs;
}

static void PrintCliArgs(const CliArgs& cliArgs)
{
    static std::unordered_map<int, std::string> g_copyFormatStringTable {
        { static_cast<int>(CopyFormat::BIN), "BIN" },
        { static_cast<int>(CopyFormat::IMAGE), "IMAGE" },
#ifdef _WIN32
        { static_cast<int>(CopyFormat::VHD_FIXED), "VHD_FIXED" },
        { static_cast<int>(CopyFormat::VHD_DYNAMIC), "VHD_DYNAMIC" },
        { static_cast<int>(CopyFormat::VHDX_FIXED), "VHDX_FIXED" },
        { static_cast<int>(CopyFormat::VHDX_DYNAMIC), "VHDX_DYNAMIC" },
#endif
    };
    std::cout << "VolumePath: " << cliArgs.volumePath << std::endl;
    std::cout << "CopyName: " << cliArgs.copyName << std::endl;
    std::cout << "CopyFormat: " << g_copyFormatStringTable[static_cast<int>(cliArgs.copyFormat)] << std::endl;
    std::cout << "CopyDataDirPath: " << cliArgs.copyDataDirPath << std::endl;
    std::cout << "CopyMetaDirPath: " << cliArgs.copyMetaDirPath << std::endl;
    std::cout << "PrevCopyMetaDirPath: " << cliArgs.prevCopyMetaDirPath << std::endl;
}

static bool ValidateCliArgs(const CliArgs& cliArgs)
{
    return !cliArgs.volumePath.empty()
        && !cliArgs.copyDataDirPath.empty()
        && !cliArgs.copyMetaDirPath.empty()
        && !cliArgs.copyName.empty();
}

void InitLogger(const CliArgs& cliArgs)
{
    Logger::GetInstance()->SetLogLevel(cliArgs.logLevel);
    using namespace xuranus::minilogger;
    LoggerConfig conf {};
    conf.target = LoggerTarget::FILE;
    conf.archiveFilesNumMax = 10;
    conf.fileName = "vbackup.log";
#ifdef __linux__
    conf.logDirPath = "/tmp/LoggerTest";
#endif
#ifdef _WIN32
    conf.logDirPath = R"(C:\LoggerTest)";
#endif
    Logger::GetInstance()->SetLogLevel(LoggerLevel::DEBUG);
    if (!Logger::GetInstance()->Init(conf)) {
        std::cerr << "Init logger failed" << std::endl;
    }
    return;
}

static int ExecVolumeBackup(const CliArgs& cliArgs)
{
    uint32_t hasherWorkerNum = fsapi::ProcessorsNum();
    VolumeBackupConfig backupConfig {};
    backupConfig.copyFormat = cliArgs.copyFormat;
    backupConfig.copyName = cliArgs.copyName;
    backupConfig.volumePath = cliArgs.volumePath;
    backupConfig.prevCopyMetaDirPath = cliArgs.prevCopyMetaDirPath;
    backupConfig.outputCopyDataDirPath = cliArgs.copyDataDirPath;
    backupConfig.outputCopyMetaDirPath = cliArgs.copyMetaDirPath;
    backupConfig.blockSize = DEFAULT_BLOCK_SIZE;
    backupConfig.sessionSize = 3 * ONE_GB;
    backupConfig.hasherNum = hasherWorkerNum;
    backupConfig.hasherEnabled = true;
    backupConfig.enableCheckpoint = true;

    if (backupConfig.prevCopyMetaDirPath.empty()) {
        std::cout << "----- Perform Full Backup -----" << std::endl;
        backupConfig.backupType = BackupType::FULL;
    } else {
        std::cout << "----- Perform Forever Increment Backup -----" << std::endl;
        backupConfig.backupType = BackupType::FOREVER_INC;
    }

    std::cout << "using " << hasherWorkerNum << " processing units" << std::endl;
    std::shared_ptr<VolumeProtectTask> task = VolumeProtectTask::BuildBackupTask(backupConfig);
    if (task == nullptr) {
        std::cerr << "failed to build backup task" << std::endl;
        return -1;
    }
    task->Start();
    uint64_t prevWrittenBytes = 0;
    while (!task->IsTerminated()) {
        TaskStatistics statistics =  task->GetStatistics();
        PrintTaskStatistics(statistics);
        uint64_t speedMB = (statistics.bytesWritten - prevWrittenBytes) / 1024 / 1024;
        std::cout << "Speed: " << speedMB << " MB/s" << std::endl;
        prevWrittenBytes = statistics.bytesWritten;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    PrintTaskStatistics(task->GetStatistics());
    std::cout << "volume backup task completed with status " << task->GetStatusString() << std::endl;
    return 0;
}

static int ExecVolumeRestore(const CliArgs& cliAgrs)
{
    std::cout << "----- Perform Copy Restore -----" << std::endl;
    VolumeRestoreConfig restoreConfig {};
    restoreConfig.copyName = cliAgrs.copyName;
    restoreConfig.volumePath = cliAgrs.volumePath;
    restoreConfig.copyDataDirPath = cliAgrs.copyDataDirPath;
    restoreConfig.copyMetaDirPath = cliAgrs.copyMetaDirPath;

    std::shared_ptr<VolumeProtectTask> task = VolumeProtectTask::BuildRestoreTask(restoreConfig);
    if (task == nullptr) {
        std::cerr << "failed to build restore task" << std::endl;
        return -1;
    }
    task->Start();
    uint64_t prevWrittenBytes = 0;
    while (!task->IsTerminated()) {
        TaskStatistics statistics =  task->GetStatistics();
        PrintTaskStatistics(statistics);
        uint64_t speedMB = (statistics.bytesWritten - prevWrittenBytes) / 1024 / 1024;
        std::cout << "Speed: " << speedMB << " MB/s" << std::endl;
        prevWrittenBytes = statistics.bytesWritten;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    PrintTaskStatistics(task->GetStatistics());
    std::cout << "volume restore task completed!" << std::endl;
    return 0;
}

int main(int argc, const char** argv)
{
    CliArgs cliArgs  = ParseCliArgs(argc, argv);
    if (!ValidateCliArgs(cliArgs)) {
        PrintHelp();
        return -1;
    }
    if (cliArgs.printHelp) {
        PrintHelp();
        return 0;
    }
    std::cout << "----- Volume Backup Cli -----" << std::endl;
    PrintCliArgs(cliArgs);
    InitLogger(cliArgs);

    if (cliArgs.isRestore) {
        ExecVolumeRestore(cliArgs);
    } else {
        ExecVolumeBackup(cliArgs);
    }
    Logger::GetInstance()->Destroy();
    return 0;
}
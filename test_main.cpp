#include <iostream>
#include <string>
#include <vector>
#include <desktop_snapshot_api.h>

void printUsage(const char* progName) {
    std::cout << "用法: " << progName << " <命令> [目标]" << std::endl;
    std::cout << "\n命令:" << std::endl;
    std::cout << "  arm <target>      - 为 'desktop' 或 'home_folders' 创建快照并开启恢复。" << std::endl;
    std::cout << "  disarm <target>   - 为 'desktop' 或 'home_folders' 移除快照并关闭恢复。" << std::endl;
    std::cout << "  restore <target>  - 立即恢复 'desktop' 或 'home_folders'。" << std::endl;
    std::cout << "  status            - 检查所有目标的恢复状态。" << std::endl;
    std::cout << "\n目标 (target): 'desktop' 或 'home_folders'" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }


    std::string command = argv[1];

    if (command == "status") {
        std::cout << "--- 快照恢复状态检查 ---" << std::endl;
        bool desktopArmed = IsRestoreArmed("desktop") == 1;
        bool homeFoldersArmed = IsRestoreArmed("home_folders") == 1;
        std::cout << "  - 桌面 (desktop): " << (desktopArmed ? "已开启" : "已关闭") << std::endl;
        std::cout << "  - 用户文件夹 (home_folders): " << (homeFoldersArmed ? "已开启" : "已关闭") << std::endl;

    } else if (command == "arm" && argc == 3) {
        std::string target = argv[2];
        std::cout << "正在为 '" << target << "' 创建快照并开启恢复..." << std::endl;
        TakeSnapshotAndArm(target.c_str());

    } else if (command == "disarm" && argc == 3) {
        std::string target = argv[2];
        std::cout << "正在为 '" << target << "' 移除快照并关闭恢复..." << std::endl;
        RemoveSnapshotAndCancel(target.c_str());

    } else if (command == "restore" && argc == 3) {
        std::string target = argv[2];
        std::cout << "正在为 '" << target << "' 执行立即恢复..." << std::endl;
        RestoreSnapshotImmediate(target.c_str());
        
    } else {
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}
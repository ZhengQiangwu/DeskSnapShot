#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include "../include/desktop_snapshot_api.h"

// 打印帮助信息
void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " <command> [target]" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  freeze <target>   (创建冰点，并设置自动恢复)" << std::endl;
    std::cout << "  unfreeze <target> (移除冰点，并移除自动恢复)" << std::endl;
    std::cout << "  restore <target>  (不重启，立即恢复)" << std::endl;
    std::cout << "  status            (检查冰点状态)" << std::endl;
    std::cout << "Targets: desktop, home_folders" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    // 1. 冰冻 (创建快照并开启恢复)
    if (command == "freeze") {
        if (argc < 3) { std::cerr << "Missing target" << std::endl; return 1; }
        std::string target = argv[2];
        
        // 调用库函数
        if (TakeSnapshotAndArm(target.c_str()) == 0) {
            std::cout << "成功为 '" << target << "' 进行冰冻并开启恢复..." << std::endl;
            return 0;
        } else {
            std::cerr << "ERROR: Failed to freeze " << target << std::endl;
            return 1;
        }
    }
    
    // 2. 解冻 (移除快照)
    else if (command == "unfreeze") {
        if (argc < 3) { std::cerr << "Missing target" << std::endl; return 1; }
        std::string target = argv[2];
        
        RemoveSnapshotAndCancel(target.c_str());
        std::cout << "成功为 '" << target << "' 移除冰冻并关闭恢复..." << std::endl;
        return 0;
    }
    
    // 3. 还原 (立即执行)
    else if (command == "restore") {
        if (argc < 3) { std::cerr << "Missing target" << std::endl; return 1; }
        std::string target = argv[2];
        
        if (RestoreSnapshotImmediate(target.c_str()) == 0) {
            std::cout << "成功为 '" << target << "' 执行立即恢复..." << std::endl;
            return 0;
        } else {
            std::cerr << "ERROR: Failed to restore " << target << std::endl;
            return 1;
        }
    }
    
    // 4. 状态查询 (输出 JSON 格式方便 Electron 解析)
    else if (command == "status") {
        std::cout << "--- 快照恢复状态检查 ---" << std::endl;
        bool desktop = IsRestoreArmed("desktop");
        bool home = IsRestoreArmed("home_folders");
        
        // 手动拼接 JSON 字符串
        std::cout << "{" 
                  << "\"桌面 (desktop)\": " << (desktop ? "已开启" : "已关闭") << ", "
                  << "\"用户文件夹 (home_folders)\": " << (home ? "已开启" : "已关闭")
                  << "}" << std::endl;
        return 0;
    }

    else {
        printUsage(argv[0]);
        return 1;
    }
}
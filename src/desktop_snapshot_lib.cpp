#include "../include/desktop_snapshot_api.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <stdexcept>
#include <filesystem>
#include <memory>
#include <array>
#include <regex>

namespace fs = std::filesystem;

// ----- 配置常量 -----
const std::string BASE_SNAPSHOT_DIR = ".snapshot_manager";
const std::string ICON_POSITION_KEY = "metadata::dde-file-manager-icon-position";
const std::string SNAPSHOT_MANIFEST_NAME = "snapshot.manifest";
const std::string BOOT_TRIGGER_FILENAME = "restore_on_boot.flag";
const std::vector<std::string> SUPPORTED_TARGETS = {"desktop", "home_folders"};

// ----- 内部辅助函数 -----

// 获取用户主目录
fs::path getUserHome() {
    const char* homeDir = getenv("HOME");
    if (homeDir == nullptr || *homeDir == '\0') {
        // 打印到标准错误流，这样会被我们的日志脚本捕捉到
        std::cerr << "CRITICAL ERROR in C++: getenv(\"HOME\") returned null or empty." << std::endl;
        throw std::runtime_error("无法找到 HOME 环境变量。");
    }
    return fs::path(homeDir);
}
	 
// 获取快照目录路径
fs::path getBaseSnapshotPath() {
    return getUserHome() / BASE_SNAPSHOT_DIR;
}

//获取目标路径快照
fs::path getSnapshotPathForTarget(const std::string& target) {
    return getBaseSnapshotPath() / target;
}

// 获取触发器文件路径
fs::path getTriggerFilePath(const std::string& target) {
    return getSnapshotPathForTarget(target) / BOOT_TRIGGER_FILENAME;
}

/**
 * @brief 智能地复制一个目录的内容到另一个目录，能正确处理子目录、文件和符号链接。
 * @param sourceDir 源目录。
 * @param destDir 目标目录。
 * @return true 如果成功, false 如果发生严重错误。
 */
bool performIntelligentCopy(const fs::path& sourceDir, const fs::path& destDir) {
    try {
        // 确保目标目录存在
        fs::create_directories(destDir);

        for (const auto& entry : fs::directory_iterator(sourceDir)) {
            const auto& sourcePath = entry.path();
            const auto& filename = sourcePath.filename();
            fs::path destinationPath = destDir / filename;

            try {
                if (fs::is_symlink(sourcePath)) {
                    // 1. 如果是符号链接，只复制链接本身
                    fs::copy_symlink(sourcePath, destinationPath);
                } else if (fs::is_directory(sourcePath)) {
                    // 2. 如果是目录，递归复制
                    fs::copy(sourcePath, destinationPath, fs::copy_options::recursive);
                } else {
                    // 3. 其他情况（如普通文件），直接复制
                    fs::copy(sourcePath, destinationPath);
                }
            } catch (const fs::filesystem_error& e) {
                // 如果复制单个项目失败，打印警告并继续
                std::cerr << "  -> 警告: 复制 '" << sourcePath.string() << "' 时失败: " << e.what() << std::endl;
                continue;
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "  -> 错误: 从 '" << sourceDir.string() << "' 智能复制时发生严重错误: " << e.what() << std::endl;
        return false;
    }
}
// 为了简洁，这里不再重复粘贴，请将上一个回答中的这三个函数复制到此处。
std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) { throw std::runtime_error("popen() failed!"); }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) { result += buffer.data(); }
    return result;
}
std::string getIconPosition(const fs::path& filePath) {
    std::string command = "gvfs-info -a " + ICON_POSITION_KEY + " \"" + filePath.string() + "\"";
    try {
        std::string output = exec(command.c_str());
        std::regex re(ICON_POSITION_KEY + ": (\\d+,\\d+)");
        std::smatch match;
        if (std::regex_search(output, match, re) && match.size() > 1) { return match.str(1); }
    } catch (const std::exception& e) {
        std::cerr << "获取位置时出错 '" << filePath.string() << "': " << e.what() << std::endl;
    }
    return "";
}
void setIconPosition(const fs::path& filePath, const std::string& position) {
    std::string command = "gvfs-set-attribute -t string \"" + filePath.string() + "\" " + ICON_POSITION_KEY + " '" + position + "'";
    system(command.c_str());
}
// [新增] 回收站路径的辅助函数
fs::path getTrashPath() {
    return getUserHome() / ".local/share/Trash";
}

// 快照和恢复核心逻辑 (内部实现)
int do_snapshot(const std::string& target) {
    try {
        // [修正] 在使用子目录之前，先确保基础目录存在
        fs::path baseSnapshotPath = getBaseSnapshotPath();
        fs::create_directories(baseSnapshotPath); // create_directories 会在不存在时创建，存在时什么也不做

        fs::path snapshotPath = getSnapshotPathForTarget(target);
        fs::path trashPath = getTrashPath(); // 获取回收站路径

        if (fs::exists(snapshotPath)) {
            std::cout << "  -> 正在删除旧快照..." << std::endl;
            fs::remove_all(snapshotPath);
        }
        fs::create_directory(snapshotPath);

        if (target == "desktop") {
            // --- 桌面快照逻辑 (处理图标位置) ---
            fs::path desktopPath = getUserHome() / "Desktop";
            std::ofstream manifestFile(snapshotPath / SNAPSHOT_MANIFEST_NAME);
            if (!manifestFile.is_open()) return -1;
            
            std::cout << "  -> 正在备份桌面..." << std::endl;

        // [核心修改] 遍历桌面并根据文件类型进行处理
        for (const auto& entry : fs::directory_iterator(desktopPath)) {
            const auto& path = entry.path();
            std::string filename = path.filename().string();
            fs::path destination = snapshotPath / filename;

            try {
                // [新增] 判断文件类型
                if (fs::is_symlink(path)) {
                    // 1. 如果是符号链接，只复制链接本身
                    std::cout << "      备份 (符号链接): " << filename << std::endl;
                    // 使用 copy_symlink 来精确处理
                    fs::copy_symlink(path, destination);

                } else if (fs::is_directory(path)) {
                    // 2. 如果是目录，递归复制
                    std::cout << "      备份 (目录): " << filename << std::endl;
                    fs::copy(path, destination, fs::copy_options::recursive);

                } else {
                    // 3. 其他情况（如普通文件），直接复制
                    std::cout << "      备份 (文件): " << filename << std::endl;
                    fs::copy(path, destination);
                }
            } catch (const fs::filesystem_error& e) {
                // 如果复制单个文件失败，打印错误并继续处理下一个
                std::cerr << "无法复制 '" << path.string() << "': " << e.what() << std::endl;
                continue;
            }

            // 获取并记录图标位置的逻辑保持不变
            std::string position = getIconPosition(path);
            manifestFile << filename << "|" << position << std::endl;
        }
        // --- 2. [新增] 备份回收站 ---
        std::cout << "  -> 正在备份回收站..." << std::endl;
        if (fs::exists(trashPath)) {
            try {
                // 将整个 Trash 目录复制到快照目录下一个名为 'TrashBackup' 的子目录中
                fs::copy(trashPath, snapshotPath / "TrashBackup", fs::copy_options::recursive);
                std::cout << "      回收站备份成功。" << std::endl;
            } catch (const fs::filesystem_error& e) {
                std::cerr << "警告: 备份回收站时出错: " << e.what() << std::endl;
            }
        } else {
            std::cout << "      未找到回收站目录，跳过备份。" << std::endl;
        }
        manifestFile.close();
       } else if (target == "home_folders") {
            // --- 用户文件夹快照逻辑 (只复制目录) ---
            std::vector<fs::path> homeFolderPaths = {
                getUserHome() / "Videos",
                getUserHome() / "Pictures",
                getUserHome() / "Documents",
                getUserHome() / "Music"
            };
            
            std::cout << "  -> 正在备份用户文件夹..." << std::endl;
            for (const auto& path : homeFolderPaths) {
	         fs::path sourcePath = getUserHome() / path.filename().string();
	         fs::path destPath = snapshotPath / path.filename().string();
                if (fs::exists(path)) {
                    std::cout << "      备份: " << path.filename().string() << std::endl;
            		// [核心修改] 使用新的智能复制函数
            		performIntelligentCopy(sourcePath, destPath);
                }
            }
        } else {
            return -1; // 不支持的目标
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "快照出错: " << e.what() << std::endl;
        return -1;
    }
}

int do_restore(const std::string& target) {
    try {
        fs::path snapshotPath = getSnapshotPathForTarget(target);
        fs::path trashPath = getTrashPath(); // 获取回收站路径

        // ===== [核心修正] 分离不同目标的有效性检查 =====
        if (target == "desktop") {
            if (!fs::exists(snapshotPath) || !fs::exists(snapshotPath / SNAPSHOT_MANIFEST_NAME)) {
                std::cerr << "错误：未找到桌面快照或其清单文件。" << std::endl;
                return -1;
            }
        } else if (target == "home_folders") {
            if (!fs::exists(snapshotPath)) {
                std::cerr << "错误：未找到用户文件夹快照。" << std::endl;
                return -1;
            }
        } else {
            // 对于任何其他未知目标，直接失败
            return -1;
        }

        // --- 1. [核心修改] 恢复回收站 (采用更温和的方式) ---
     if (target == "desktop") {
        // --- 桌面恢复逻辑 (处理图标位置) ---
        fs::path desktopPath = getUserHome() / "Desktop";
        // --- 2. 恢复桌面 (逻辑和之前一样) ---
        std::cout << "  -> 正在恢复桌面..." << std::endl;
	 // 清空桌面
        for (const auto& entry : fs::directory_iterator(desktopPath)) {
            fs::remove_all(entry.path());
        }

        // [核心修改] 从快照恢复时，同样需要判断文件类型
        for (const auto& entry : fs::directory_iterator(snapshotPath)) {
            const auto& sourcePath = entry.path();
            const auto& filename = sourcePath.filename();
            fs::path destinationPath = desktopPath / filename;

            // 忽略快照的元数据文件和备份目录
            if (filename == SNAPSHOT_MANIFEST_NAME ||
                filename == BOOT_TRIGGER_FILENAME ||
                filename == "TrashBackup") {
                continue;
            }

            try {
                // [新增] 判断文件类型，使用与 do_snapshot 对称的逻辑
                if (fs::is_symlink(sourcePath)) {
                    // 1. 如果是符号链接，只复制链接本身
                    fs::copy_symlink(sourcePath, destinationPath);
                } else if (fs::is_directory(sourcePath)) {
                    // 2. 如果是目录，递归复制
                    fs::copy(sourcePath, destinationPath, fs::copy_options::recursive);
                } else {
                    // 3. 其他情况（如普通文件），直接复制
                    fs::copy(sourcePath, destinationPath);
                }
            } catch (const fs::filesystem_error& e) {
                std::cerr << "无法恢复 '" << sourcePath.string() << "': " << e.what() << std::endl;
                continue;
            }
        }
        
        system("sleep 1"); // 等待桌面响应

        std::ifstream manifestFile(snapshotPath / SNAPSHOT_MANIFEST_NAME);
        std::string line;
        while (std::getline(manifestFile, line)) {
            size_t delimiterPos = line.find('|');
            if (delimiterPos != std::string::npos) {
                std::string filename = line.substr(0, delimiterPos);
                std::string position = line.substr(delimiterPos + 1);
                if (!position.empty()) {
                    setIconPosition(desktopPath / filename, position);
                }
            }
        }
        system("xrefresh > /dev/null 2>&1");
        std::cout << "  -> 正在恢复回收站..." << std::endl;
        fs::path trashBackupPath = snapshotPath / "TrashBackup";
        if (fs::exists(trashBackupPath)) {
            try {
                // a. 确保当前回收站的子目录存在
                fs::create_directories(trashPath / "files");
                fs::create_directories(trashPath / "info");

                // b. 清空当前回收站的子目录内容，而不是删除 Trash 根目录
                for (const auto& entry : fs::directory_iterator(trashPath / "files")) {
                    fs::remove_all(entry.path());
                }
                for (const auto& entry : fs::directory_iterator(trashPath / "info")) {
                    fs::remove_all(entry.path());
                }
                std::cout << "      当前回收站已清空。" << std::endl;

                // c. 将备份的内容复制到对应的子目录中
                fs::path trashBackupFiles = trashBackupPath / "files";
                fs::path trashBackupInfo = trashBackupPath / "info";

                if (fs::exists(trashBackupFiles)) {
                    fs::copy(trashBackupFiles, trashPath / "files", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
                }
                if (fs::exists(trashBackupInfo)) {
                    fs::copy(trashBackupInfo, trashPath / "info", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
                }
                std::cout << "      回收站已从快照恢复。" << std::endl;
            } catch (const fs::filesystem_error& e) {
                // 如果恢复回收站失败，只打印警告，不中断后续的桌面恢复
                std::cerr << "警告: 恢复回收站时发生错误: " << e.what() << std::endl;
            }
        } else {
            std::cout << "      快照中未找到回收站备份，跳过恢复。" << std::endl;
        }
        } 
	  else if (target == "home_folders") {
            // --- 用户文件夹快照逻辑 (只复制目录) ---
            std::vector<fs::path> homeFolderPaths = {
                getUserHome() / "Videos",
                getUserHome() / "Pictures",
                getUserHome() / "Documents",
                getUserHome() / "Music"
            };
            
            std::cout << "  -> 正在恢复用户文件夹..." << std::endl;
            for (const auto& path : homeFolderPaths) {
	         fs::path backupPath = snapshotPath / path.filename().string();
	         fs::path restorePath = getUserHome() / path.filename().string();
                if (fs::exists(backupPath)) {
                    std::cout << "      恢复: " << path.filename().string() << std::endl;
                    if (fs::exists(path)) {
                        fs::remove_all(path);
                    }
	             // [核心修改] 使用新的智能复制函数
	             performIntelligentCopy(backupPath, restorePath);
                }
            }
        } else {
            return -1; // 不支持的目标
        }
        return 0;
    }catch (const std::exception& e) {
        std::cerr << "恢复出错: " << e.what() << std::endl;
        return -1;
    }
}

// ----- API 实现 -----

extern "C" {

int TakeSnapshotAndArm(const char* target_c) {
    std::string target(target_c);
    if (do_snapshot(target) == 0) {
        // [修正] 补上创建标志文件的关键一步
        std::ofstream triggerFile(getTriggerFilePath(target));
        if (triggerFile.is_open()) {
            triggerFile.close();
            return 0; // 成功
        }
    }
    return -1; // 失败
}

void RemoveSnapshotAndCancel(const char* target_c) {
    std::string target(target_c);
    fs::path snapshotPath = getSnapshotPathForTarget(target);

    if (fs::exists(snapshotPath)) {
        std::cout << "正在为 '" << target << "' 移除快照..." << std::endl;
        fs::remove_all(snapshotPath);
        
        // [新增] 在移除子目录后，检查基础目录是否已空，如果空了就一并删除
        fs::path basePath = getBaseSnapshotPath();
        if (fs::exists(basePath) && fs::is_empty(basePath)) {
            std::cout << "所有快照均已移除，正在清理基础目录..." << std::endl;
            fs::remove(basePath);
        }
    } else {
        // [修改] 让输出更清晰
        std::cout << "未找到 '" << target << "' 的快照，无需移除。" << std::endl;
    }
}

int RestoreSnapshotImmediate(const char* target_c) {
    return do_restore(std::string(target_c));
}

int IsRestoreArmed(const char* target_c) {
    // [修正] 调用新的 getTriggerFilePath 函数来获取正确的路径
    fs::path triggerFile = getTriggerFilePath(std::string(target_c));
    return fs::exists(triggerFile) ? 1 : 0;
}

// [修改] 自启动执行器
void ExecuteRestoreOnBoot() {
    // 依次检查所有支持的目标
    for (const auto& target : SUPPORTED_TARGETS) {
        if (IsRestoreArmed(target.c_str()) == 1) {
            std::cout << "检测到 " << target << " 的恢复标志，正在执行恢复..." << std::endl;
            if (do_restore(target) == 0) {
                std::cout << target << " 已根据快照恢复。" << std::endl;
            } else {
                std::cerr << "恢复 " << target << " 时失败。" << std::endl;
            }
        }
    }
}

} // extern "C"
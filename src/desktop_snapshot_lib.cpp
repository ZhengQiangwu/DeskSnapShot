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
#include <unistd.h> // 必须包含，用于 chown, lchown, getuid, getgid
#include <sys/stat.h>

namespace fs = std::filesystem;

// 1. 定义要备份的用户文件夹列表 (已移除 Downloads)
const std::vector<std::string> HOME_FOLDER_TARGETS = {
    "Videos", "Pictures", "Documents", "Music"
};
// 2. [缺失的部分] 定义启动器和用户快捷方式的路径 (相对于 HOME)
const std::vector<std::string> LAUNCHER_TARGETS = {
    ".local/share/applications",       // 用户自行安装的软件图标
    ".config/dde-launcher",            // 启动器布局配置 (新版 UOS)
    ".config/deepin/dde-launcher",     // 启动器布局配置 (旧版/兼容)
    ".config/deepin/dde-dock"          // 任务栏 (Dock) 配置
};
// 3. 定义系统级目标 (绝对路径)
const std::vector<std::string> SYSTEM_TARGETS = {
    "/usr/share/applications"          // 全局启动器图标
    // "/usr/share/icons"              // [可选] 全局图标资源
};

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
 * @brief 递归修改文件或目录的所有者 (chown)
 * @param path 目标路径
 * @param owner_uid 目标用户 ID
 * @param owner_gid 目标组 ID
 */
void chownRecursive(const fs::path& path, uid_t owner_uid, gid_t owner_gid) {
    try {
        // 1. 修改当前路径本身的权限 (lchown 不会跟随符号链接，修改链接本身)
        if (lchown(path.c_str(), owner_uid, owner_gid) != 0) {
            // 忽略错误，或者打印调试信息
        }

        // 2. 如果是目录且不是符号链接，则递归修改其内容
        if (fs::is_directory(path) && !fs::is_symlink(path)) {
            for (const auto& entry : fs::recursive_directory_iterator(path)) {
                // 对每一个子文件/子目录执行 lchown
                lchown(entry.path().c_str(), owner_uid, owner_gid);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "权限修复警告: " << path.string() << " -> " << e.what() << std::endl;
    }
}
/**
 * @brief 智能地复制一个目录的内容到另一个目录，能正确处理子目录、文件和符号链接。并自动修复所有权。
 * @param sourceDir 源目录。
 * @param destDir 目标目录。
 * @param dereference 如果为 true，遇到符号链接时，会复制其指向的真实文件（变成普通文件）。
 *                    如果为 false，则保留符号链接的属性（依然是链接）。
 * @param target_uid 目标文件的拥有者 UID (-1 表示不修改)
 * @param target_gid 目标文件的拥有者 GID (-1 表示不修改)
 */
bool performIntelligentCopy(const fs::path& sourceDir, const fs::path& destDir, 
				bool dereference, uid_t target_uid, gid_t target_gid) {
    try {
        // 1. 确保目标目录存在
        if (!fs::exists(destDir)) {
            fs::create_directories(destDir);
            // 如果创建了新目录，立即修复其权限
            if (target_uid != (uid_t)-1) {
                lchown(destDir.c_str(), target_uid, target_gid);
            }
        }

        for (const auto& entry : fs::directory_iterator(sourceDir)) {
            const auto& sourcePath = entry.path();
            const auto& filename = sourcePath.filename();
            fs::path destinationPath = destDir / filename;

            try {
                // [逻辑升级]
                // 如果是符号链接，且开启了 dereference (解引用) 模式
                if (fs::is_symlink(sourcePath) && dereference) {
                    // 使用 fs::copy 会自动跟随链接，复制真实内容
                    // 使用 overwrite_existing 以防目标已存在
                    fs::copy(sourcePath, destinationPath, fs::copy_options::overwrite_existing | fs::copy_options::recursive);
                }
                // 如果是符号链接，且没有开启解引用 (保持原样)
                else if (fs::is_symlink(sourcePath)) {
                    fs::copy_symlink(sourcePath, destinationPath);
                }
                // 如果是目录
                else if (fs::is_directory(sourcePath)) {
                    fs::copy(sourcePath, destinationPath, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
                } 
                // 普通文件
                else {
                    fs::copy(sourcePath, destinationPath, fs::copy_options::overwrite_existing);
                }
                // --- 权限修复逻辑 (核心修改) ---
                if (target_uid != (uid_t)-1) {
                    chownRecursive(destinationPath, target_uid, target_gid);
                }
            } catch (const fs::filesystem_error& e) {
                // 忽略一些特殊文件的复制错误 (如 socket 或 pipe)
                std::cerr << "  -> 警告: 复制 '" << sourcePath.string() << "' 失败" << std::endl;
                continue;
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "  -> 错误: 智能复制失败 " << sourceDir.string() << ": " << e.what() << std::endl;
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
        // 1. 确保基础目录存在
        // 在使用子目录之前，先确保基础目录存在
        fs::path baseSnapshotPath = getBaseSnapshotPath();
        fs::create_directories(baseSnapshotPath); // create_directories 会在不存在时创建，存在时什么也不做

        fs::path snapshotPath = getSnapshotPathForTarget(target);
        fs::path trashPath = getTrashPath(); // 获取回收站路径

        // 2. 清理旧快照
        if (fs::exists(snapshotPath)) {
            std::cout << "  -> 正在删除旧快照..." << std::endl;
            fs::remove_all(snapshotPath);
        }
        fs::create_directory(snapshotPath);

        // ====================================================================
        //  TARGET: DESKTOP (桌面文件 + 图标布局 + 系统应用图标)
        // ====================================================================
        if (target == "desktop") {
            // [新增] 定义子目录路径
            fs::path desktopFilesDir = snapshotPath / "DesktopFiles";
            fs::path iconConfigsDir = snapshotPath / "IconConfigs";
            // [新增] 创建子目录
            fs::create_directories(desktopFilesDir);
            fs::create_directories(iconConfigsDir);

            // --- 桌面快照逻辑 (处理图标位置) ---
            fs::path desktopPath = getUserHome() / "Desktop";
            std::ofstream manifestFile(snapshotPath / SNAPSHOT_MANIFEST_NAME);// 清单文件依然放在根目录
            if (!manifestFile.is_open()) return -1;
            
            std::cout << "  -> 正在备份桌面..." << std::endl;

        // [核心修改] 遍历桌面并根据文件类型进行处理
        for (const auto& entry : fs::directory_iterator(desktopPath)) {
            const auto& path = entry.path();
            std::string filename = path.filename().string();
            fs::path destination = desktopFilesDir / filename;

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
            // --- B. [新增] 备份启动器配置和系统图标 (从 home_folders 移过来的逻辑) ---
            std::cout << "  -> 正在备份启动器配置及系统图标..." << std::endl;
            
            std::vector<std::string> extraTargets = LAUNCHER_TARGETS;
            extraTargets.insert(extraTargets.end(), SYSTEM_TARGETS.begin(), SYSTEM_TARGETS.end());

            for (const auto& folderName : extraTargets) {
                fs::path sourcePath;
                fs::path destPath;
                bool shouldDereference = false;

                // 判断路径类型 (系统绝对路径 vs 用户相对路径)
                if (!folderName.empty() && folderName[0] == '/') {
                    sourcePath = folderName;
                    destPath = iconConfigsDir / folderName.substr(1);
                    // 对于 /usr/share/applications，开启解引用，备份真实文件
                    if (folderName.find("/usr/share/applications") != std::string::npos ||
                        folderName.find("dde-launcher") != std::string::npos) {
                        shouldDereference = true;
                    }
                } else {
                    // 相对路径
                    sourcePath = getUserHome() / folderName;
                    // [修改] 目标路径基于 iconConfigsDir
                    destPath = iconConfigsDir / folderName;
                }

                if (fs::exists(sourcePath)) {
                    std::cout << "      备份配置: " << sourcePath.string() << std::endl;
                    if (destPath.has_parent_path()) {
                        fs::create_directories(destPath.parent_path());
                    }
                    performIntelligentCopy(sourcePath, destPath, shouldDereference,-1,-1);
                }
            }
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
            		performIntelligentCopy(sourcePath, destPath, false,-1,-1); // 数据文件通常不解引用
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

        // 获取当前实际登录用户的 ID (即使程序以 Root 运行，getuid 也会返回普通用户 ID)
        uid_t user_uid = getuid();
        gid_t user_gid = getgid();
        
        // 定义 Root 的 ID
        uid_t root_uid = 0;
        gid_t root_gid = 0;

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

        // ====================================================================
        //  TARGET: DESKTOP (恢复桌面 + 回收站 + 启动器 + 系统图标)
        // ====================================================================
     if (target == "desktop") {
            // 3. 恢复启动器配置和系统图标 -> 【混合权限】
            // 这里需要根据路径判断是系统文件还是用户配置
            std::cout << "  -> 正在恢复启动器及系统配置..." << std::endl;
            // [关键修改] 指定子目录
            fs::path iconConfigsBackupDir = snapshotPath / "IconConfigs";

            std::vector<std::string> extraTargets = LAUNCHER_TARGETS;
            extraTargets.insert(extraTargets.end(), SYSTEM_TARGETS.begin(), SYSTEM_TARGETS.end());

            for (const auto& folderName : extraTargets) {
                fs::path backupPath;
                fs::path restorePath;
                // 决定使用什么权限
                uid_t target_owner_uid = user_uid;
                gid_t target_owner_gid = user_gid;

                if (!folderName.empty() && folderName[0] == '/') {
                    // 绝对路径 (如 /usr/share/applications) -> 使用 Root 权限
                    backupPath = iconConfigsBackupDir / folderName.substr(1);
                    restorePath = folderName;
                    target_owner_uid = root_uid;
                    target_owner_gid = root_gid;
                } else {
                    // 相对路径 (如 .config/dde-launcher) -> 使用用户权限
                    backupPath = iconConfigsBackupDir / folderName;
                    restorePath = getUserHome() / folderName;
                    // 保持默认 user_uid
                }

                if (fs::exists(backupPath)) {
                    std::cout << "      恢复配置: " << restorePath.string() 
                              << (target_owner_uid == 0 ? " [Root]" : " [User]") << std::endl;

                    // 如果是系统目录，我们只清空内容，不删除文件夹本身，以保留文件夹权限
                    if (fs::exists(restorePath)) {
                        // 如果是系统目录，只清空内容；如果是用户目录，可以删除重建
                        if (target_owner_uid == 0) {
                             for (const auto& e : fs::directory_iterator(restorePath)) fs::remove_all(e.path());
                        } else {
                             fs::remove_all(restorePath);
                             if (restorePath.has_parent_path()) fs::create_directories(restorePath.parent_path());
                             // 记得修复重建目录的权限
                             lchown(restorePath.c_str(), user_uid, user_gid);
                        }
                    } else {
                        if (restorePath.has_parent_path()) fs::create_directories(restorePath.parent_path());
                    }
                    
                    // [修改] 传入这一轮循环决定的正确 UID/GID
                    performIntelligentCopy(backupPath, restorePath, false, target_owner_uid, target_owner_gid);
                }
            }
//===================================================================
        // --- 桌面恢复逻辑 (处理图标位置) ---
        fs::path desktopPath = getUserHome() / "Desktop";

        // --- 2. 恢复桌面 (逻辑和之前一样) ---
        std::cout << "  -> 正在恢复桌面..." << std::endl;
	 // 清空桌面
        for (const auto& entry : fs::directory_iterator(desktopPath)) {
            fs::remove_all(entry.path());
        }
        // [关键修改] 直接遍历 DesktopFiles 子目录，无需任何过滤逻辑！
        fs::path desktopFilesBackupDir = snapshotPath / "DesktopFiles";
        if (fs::exists(desktopFilesBackupDir)) {
            // [修改] 直接调用一次即可，performIntelligentCopy 会遍历目录
            // 注意：这里 dest 是 desktopPath 本身，我们需要把备份目录里的东西拷进去
            performIntelligentCopy(desktopFilesBackupDir, desktopPath, false, user_uid, user_gid);
        }
//===================================================================
            // 4. [性能优化] 批量恢复图标位置 (不再使用循环调用 system)
            std::cout << "  -> 正在批量恢复图标位置..." << std::endl;
            
            // 创建一个临时 shell 脚本
            std::string batchScriptPath = "/tmp/restore_icons_" + std::to_string(getpid()) + ".sh";
            std::ofstream scriptFile(batchScriptPath);
            
            if (scriptFile.is_open()) {
                scriptFile << "#!/bin/sh\n";
                
                std::ifstream manifestFile(snapshotPath / SNAPSHOT_MANIFEST_NAME);
                std::string line;
                while (std::getline(manifestFile, line)) {
                    size_t delimiterPos = line.find('|');
                    if (delimiterPos != std::string::npos) {
                        std::string filename = line.substr(0, delimiterPos);
                        std::string position = line.substr(delimiterPos + 1);
                        
                        if (!position.empty()) {
                            // 构造目标文件的完整路径
                            fs::path targetFile = desktopPath / filename;
                            // 将 gvfs 命令写入脚本，而不是立即执行
                            // 注意：我们需要转义文件名中的潜在特殊字符，这里简单处理加引号
                            scriptFile << "gvfs-set-attribute -t string \"" << targetFile.string() << "\" " 
                                       << ICON_POSITION_KEY << " '" << position << "'\n";
                        }
                    }
                }
                scriptFile.close();
                
                // 赋予脚本执行权限
                chmod(batchScriptPath.c_str(), 0755);
                
                // 一次性执行脚本
                // 注意：因为我们是 SUID Root 运行，gvfs 需要连接用户的 Session Bus
                // 之前的环境通常已经设置好了 DBUS_SESSION_BUS_ADDRESS，所以直接运行通常可行
                // 如果有问题，可能需要用 `su -c ...` 切换回用户执行，但在 autostart 环境下通常不需要
                std::string runCmd = "sh " + batchScriptPath + " > /dev/null 2>&1";
                system(runCmd.c_str());
                
                // 清理临时脚本
                fs::remove(batchScriptPath);
            }

            // 5. [修改 2] 异步刷新桌面环境
            // 移除了 "killall -9 dde-desktop"，保留 dock 和 launcher 的重启
            // 这样任务栏会刷新（因为配置变了），但壁纸不会消失
            std::cout << "  -> 触发后台刷新..." << std::endl;
            std::string refreshCmd = 
                "nohup sh -c '"
                "update-desktop-database /usr/share/applications > /dev/null 2>&1; "
                //"killall -9 dde-dock > /dev/null 2>&1; "
                //"killall -9 dde-launcher > /dev/null 2>&1; "
                // 注意：这里删除了 killall dde-desktop
                // 让 DDE 自动监测文件变化并更新，而不是强制重启
                "xrefresh > /dev/null 2>&1"
                "' > /dev/null 2>&1 &";
            
            system(refreshCmd.c_str());
//===================================================================
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
                    // [修改] 传入 user_uid
                    performIntelligentCopy(trashBackupFiles, trashPath / "files", false, user_uid, user_gid);
                }
                if (fs::exists(trashBackupInfo)) {
                    // [修改] 传入 user_uid
                    performIntelligentCopy(trashBackupInfo, trashPath / "info", false, user_uid, user_gid);
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
            std::cout << "  -> 正在恢复用户文件夹..." << std::endl;
            // 恢复用户数据 -> 必须是【普通用户权限】
            for (const auto& folderName : HOME_FOLDER_TARGETS) {
                fs::path backupPath = snapshotPath / folderName;
                fs::path restorePath = getUserHome() / folderName;
                if (fs::exists(backupPath)) {
                    std::cout << "      恢复: " << folderName << std::endl;
                    if (fs::exists(restorePath)) fs::remove_all(restorePath);
                    // [修改] 传入 user_uid
                    performIntelligentCopy(backupPath, restorePath, false, user_uid, user_gid);
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
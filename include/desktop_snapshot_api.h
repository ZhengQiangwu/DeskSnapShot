#ifndef DESKTOP_SNAPSHOT_API_H
#define DESKTOP_SNAPSHOT_API_H

// 使用 extern "C" 来确保 C 语言调用约定
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 为指定目标创建快照，并设置一个标志以便在下次启动时自动恢复。
 * @param target 快照目标, e.g., "desktop" 或 "home_folders".
 * @return 0 表示成功, -1 表示失败。
 */
int TakeSnapshotAndArm(const char* target);

/**
 * @brief 移除指定目标的快照数据，并取消其未来的所有自动恢复。
 * @param target 快照目标, e.g., "desktop" 或 "home_folders".
 */
void RemoveSnapshotAndCancel(const char* target);

/**
 * @brief 检查指定目标的“下次启动时恢复”标志是否已设置。
 * @param target 快照目标, e.g., "desktop" 或 "home_folders".
 * @return 1 表示已设置, 0 表示未设置。
 */
int IsRestoreArmed(const char* target);

/**
 * @brief 立即从最新的快照恢复指定目标。
 * @param target 快照目标, e.g., "desktop" 或 "home_folders".
 * @return 0 表示成功, -1 表示失败。
 */
int RestoreSnapshotImmediate(const char* target);

/**
 * @brief [内部使用] 供自启动程序调用。
 *        依次检查所有受支持的目标，如果存在恢复标志则执行恢复。
 */
void ExecuteRestoreOnBoot();

#ifdef __cplusplus
}
#endif

#endif // DESKTOP_SNAPSHOT_API_H
#!/bin/bash

# ==============================================================================
#  UOS Desktop Snapshot Library - Debian Packager
# ==============================================================================
#
#  此脚本会自动编译 C++ 源代码，并将其打包成一个 .deb 文件。
#  这个 .deb 文件可以方便地在其他 UOS (或 Debian/Ubuntu) 系统上安装。
#
#  用法: ./install.sh
#
# ==============================================================================

# -- 如果任何命令失败，则立即退出
set -e

# -- 定义包信息
PACKAGE_NAME="uos-deep-freeze"
VERSION="1.2.1"
ARCHITECTURE=$(dpkg --print-architecture)
MAINTAINER="aaawu666 <1214018110@qq.com>"

# -- 定义构建和打包目录
BUILD_DIR="build"
PACKAGE_DIR="package"
# [新增] 定义外部脚本的路径，方便管理
STARTUP_SCRIPT_SOURCE="scripts/autostart.sh"

# ==============================================================================
#  步骤 1: 检查构建依赖
# ==============================================================================
echo "--> 正在检查构建依赖 (cmake, build-essential, dpkg-deb)..."
if ! command -v cmake &> /dev/null || ! command -v g++ &> /dev/null || ! command -v dpkg-deb &> /dev/null; then
    echo "错误: 缺少构建工具。请运行:"
    echo "sudo apt update && sudo apt install cmake build-essential dpkg-dev"
    exit 1
fi
echo "依赖检查通过。"

# ==============================================================================
#  步骤 2: 编译 C++ 项目
# ==============================================================================
echo "--> 正在清理并编译项目..."
# 清理旧的构建目录
if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
fi
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

# 运行 CMake 和 Make
cmake ..
make

echo "项目编译成功。"
cd .. # 返回项目根目录

# ==============================================================================
#  步骤 3: 创建 Debian 包目录结构
# ==============================================================================
echo "--> 正在创建 .deb 包结构..."
# 清理旧的打包目录
if [ -d "$PACKAGE_DIR" ]; then
    rm -rf "$PACKAGE_DIR"
fi

# 创建包的根目录
PKG_ROOT="${PACKAGE_DIR}/${PACKAGE_NAME}_${VERSION}_${ARCHITECTURE}"
mkdir -p "$PKG_ROOT"

# === 创建目录结构 (修改) ===
mkdir -p "$PKG_ROOT/usr/lib"
mkdir -p "$PKG_ROOT/usr/bin"
# [修改] 同时创建两个自启动目录
# mkdir -p "$PKG_ROOT/etc/X11/Xsession.d" 
mkdir -p "$PKG_ROOT/etc/profile.d"     # <--- 新增: profile.d 目录
mkdir -p "$PKG_ROOT/usr/include"
mkdir -p "$PKG_ROOT/DEBIAN"


# ==============================================================================
#  步骤 4: 填充文件到包结构中
# ==============================================================================
echo "--> 正在复制编译好的文件..."

# 1. 复制 .so 库到 /usr/lib
#    我们使用 `libdesktop_snapshot.so.1` 这个 soname，这是 Linux 库的标准做法
cp "${BUILD_DIR}/libdesktop_snapshot.so.1.0.0" "$PKG_ROOT/usr/lib/libdesktop_snapshot.so.1.0.0"
#    创建符号链接
ln -s "libdesktop_snapshot.so.1.0.0" "$PKG_ROOT/usr/lib/libdesktop_snapshot.so.1"
ln -s "libdesktop_snapshot.so.1" "$PKG_ROOT/usr/lib/libdesktop_snapshot.so"


# 2. 复制 autostart_helper 到 /usr/bin
cp "${BUILD_DIR}/autostart_helper" "$PKG_ROOT/usr/bin/"

# 2.1 复制 snapshot_tool 到 /usr/bin
cp "${BUILD_DIR}/snapshot_tool" "$PKG_ROOT/usr/bin/"

# 3. [修改] 检查、复制外部启动脚本到两个目标位置
echo "--> 正在处理自启动脚本..."
if [ ! -f "$STARTUP_SCRIPT_SOURCE" ]; then
    echo "错误: 启动脚本 '$STARTUP_SCRIPT_SOURCE' 未找到!"
    exit 1
fi
# 复制到 Xsession.d
# cp "$STARTUP_SCRIPT_SOURCE" "$PKG_ROOT/etc/X11/Xsession.d/50-uos-desktop-restore.sh"
# 复制到 profile.d
cp "$STARTUP_SCRIPT_SOURCE" "$PKG_ROOT/etc/profile.d/uos-desktop-restore.sh"

# 4. 复制 API 头文件 <--- 新增：让其他开发者也能使用我们的库
cp "include/desktop_snapshot_api.h" "$PKG_ROOT/usr/include/"

# ==============================================================================
#  步骤 5: 创建 DEBIAN/control 控制文件
# ==============================================================================
echo "--> 正在创建 DEBIAN/control 文件..."
cat <<EOF > "$PKG_ROOT/DEBIAN/control"
Package: ${PACKAGE_NAME}
Version: ${VERSION}
Architecture: ${ARCHITECTURE}
Maintainer: ${MAINTAINER}
Description: A library to Deep Freeze restoration on UOS.
 Provides an API for external apps and an autostart helper.
Depends: libc6, libstdc++6, gvfs-bin, x11-xserver-utils
EOF

# ==============================================================================
#  步骤 6: 创建 DEBIAN/postinst (设置权限)
# ==============================================================================
cat <<EOF > "$PKG_ROOT/DEBIAN/postinst"
#!/bin/sh
set -e
ldconfig

# 设置 helper 权限 (用于开机自启)
chown root:root /usr/bin/autostart_helper
chmod 4755 /usr/bin/autostart_helper

# [新增] 设置 CLI 工具权限 (用于 Electron 调用)
chown root:root /usr/bin/snapshot_tool
chmod 4755 /usr/bin/snapshot_tool

# 设置脚本权限
if [ -f /etc/profile.d/uos-desktop-restore.sh ]; then
    chmod +x /etc/profile.d/uos-desktop-restore.sh
fi
exit 0
EOF
chmod 0755 "$PKG_ROOT/DEBIAN/postinst"

# ==============================================================================
#  步骤 7: 构建 .deb 包
# ==============================================================================
echo "--> 正在构建 .deb 包..."
dpkg-deb --build "$PKG_ROOT"

FINAL_DEB_NAME="${PACKAGE_NAME}_${VERSION}_${ARCHITECTURE}.deb"
echo ""
echo "================================================================================"
echo "打包成功！"
echo ""
echo "生成的安装包文件: ${FINAL_DEB_NAME}"
echo ""
echo "您可以将此文件分发给其他 UOS 用户。"
echo "================================================================================"
echo ""
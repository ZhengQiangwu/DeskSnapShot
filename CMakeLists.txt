cmake_minimum_required(VERSION 3.10)
project(DesktopSnapshotLib CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置头文件目录
include_directories(include)

# ----------------- 编译动态库 .so -----------------
add_library(desktop_snapshot SHARED
    src/desktop_snapshot_lib.cpp
)

# 链接库
target_link_libraries(desktop_snapshot PRIVATE -lstdc++fs)

# 设置动态库的版本和 so 名称
set_target_properties(desktop_snapshot PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    PUBLIC_HEADER "include/desktop_snapshot_api.h"
)


# ----------------- 编译自启动辅助程序 -----------------
add_executable(autostart_helper
    src/autostart_helper.cpp
)

# 链接辅助程序到我们自己的库
target_link_libraries(autostart_helper PRIVATE desktop_snapshot)

# 设置 RPATH，让辅助程序能找到同目录下的 .so 文件
set_target_properties(autostart_helper PROPERTIES
    INSTALL_RPATH "$ORIGIN"
)

# 安装规则 (可选，但推荐)
install(TARGETS desktop_snapshot autostart_helper
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES include/desktop_snapshot_api.h DESTINATION include)
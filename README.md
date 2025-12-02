# DeskSnapShot  
Uos系统下模拟Linux快照恢复桌面的.so动态库  
1.提供拍摄桌面快照的接口  
2.提供拍摄视频/音频/文档/图片文件夹快照的接口  
3.提供删除以上两种快照的接口  
4.拍摄快照后重启电脑会恢复至拍摄快照的时候的桌面和文件夹  
5.提供自启动脚本，自启动脚本会自动执行恢复操作  

# 1.2.1
1.增加冰点/拍摄快照后，安装软件能够还原启动器中的图标（root权限）  
2.架构模式叫做 "CLI Wrapper" (命令行包装器)，供electron调用  
冰冻程序：snapshot_tool freeze desktop  
解冻程序：snapshot_tool unfreeze desktop   
还原程序：snapshot_tool restore desktop  
状态查询：snapshot_tool status  

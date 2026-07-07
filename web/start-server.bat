@echo off
chcp 65001 >nul
echo ============================================================
echo 智能CPU水冷系统Web上位机 - 启动脚本
echo ============================================================
echo.

echo 正在启动服务器...
echo.

node backend/server.js

pause

#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 本机 C: 盘已 100% 占满（剩余 < 10MB），NuGet / MSBuild / Roslyn 默认会把
# 临时文件与包缓存写到 C:\Users\<user>\AppData\Local\Temp 和 %USERPROFILE%\.nuget，
# 导致 restore 报 "磁盘空间不足"。此脚本把相关目录全部重定向到 E: 盘。
#
# 用法（Git Bash）—— 必须 source，不能直接执行，否则变量只在子进程里生效：
#   source scripts/env.sh
#   dotnet build src/Vms.Client/Vms.Client.csproj
#
# 用法（PowerShell）—— PowerShell 没有 source，请用同目录下的 env.ps1：
#   . .\scripts\env.ps1
#   dotnet build .\src\Vms.Client\Vms.Client.csproj
# ---------------------------------------------------------------------------
export ROOT="E:/win_e/cs_dev/soft_src/win_vms_v1"

export NUGET_PACKAGES="$ROOT/.nuget/packages"
export NUGET_HTTP_CACHE_PATH="$ROOT/.nuget/http-cache"
export TEMP="$ROOT/.nuget/scratch"
export TMP="$ROOT/.nuget/scratch"
export DOTNET_CLI_HOME="$ROOT/.nuget/cli-home"
export MSBUILDDISABLENODEREUSE=1

mkdir -p "$NUGET_PACKAGES" "$NUGET_HTTP_CACHE_PATH" "$TEMP" "$DOTNET_CLI_HOME"
echo "[env] NUGET_PACKAGES -> $NUGET_PACKAGES"
echo "[env] TEMP           -> $TEMP"

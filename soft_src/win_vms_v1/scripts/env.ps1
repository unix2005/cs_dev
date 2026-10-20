# ---------------------------------------------------------------------------
# 本机 C: 盘空间紧张时，NuGet / MSBuild / Roslyn 默认会把包缓存与临时文件写到
#   %USERPROFILE%\.nuget  和  C:\Users\<user>\AppData\Local\Temp
# 导致 restore 报"磁盘空间不足"。这里把相关目录全部重定向到 E: 盘。
#
# 用法（PowerShell）—— 注意最前面的 "." 和空格，必须"点源"执行，
# 否则变量只在这个脚本内部生效，dotnet 仍然会用默认目录：
#   . .\scripts\env.ps1
#   dotnet build .\src\Vms.Client\Vms.Client.csproj
#
# 这是 scripts/env.sh（Git Bash 版）的 PowerShell 等价物，两者设置一个即可。
# ---------------------------------------------------------------------------
$Root = "E:/win_e/cs_dev/soft_src/win_vms_v1"

$env:NUGET_PACKAGES          = "$Root/.nuget/packages"
$env:NUGET_HTTP_CACHE_PATH   = "$Root/.nuget/http-cache"
$env:TEMP                    = "$Root/.nuget/scratch"
$env:TMP                     = "$Root/.nuget/scratch"
$env:DOTNET_CLI_HOME         = "$Root/.nuget/cli-home"
$env:MSBUILDDISABLENODEREUSE = "1"

foreach ($d in @($env:NUGET_PACKAGES, $env:NUGET_HTTP_CACHE_PATH, $env:TEMP, $env:DOTNET_CLI_HOME)) {
    if (-not (Test-Path -LiteralPath $d)) {
        New-Item -ItemType Directory -Path $d -Force | Out-Null
    }
}

Write-Host "[env] NUGET_PACKAGES -> $env:NUGET_PACKAGES"
Write-Host "[env] TEMP           -> $env:TEMP"

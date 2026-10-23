/**
 * @file qc_conf_default_path.c
 * @brief qc_conf_default_path —— 三端默认配置文件路径
 *
 * 只解析路径，**不创建目录也不创建文件**（创建属于 UI 的安装/首次运行逻辑，
 * 内核擅自写盘会在沙箱/只读安装目录里失败，且失败形态是"配置莫名其妙没生效"）。
 *
 * 平台惯例遵循各端的配置存放规范，而不是图省事都放"当前工作目录"：
 *   - Windows：%LOCALAPPDATA%（每用户可写、随漫游配置走）
 *   - Linux  ：XDG_CONFIG_HOME（~/.config），符合 XDG Base Directory 规范
 *   - macOS  ：~/Library/Application Support，符合 Apple 人机界面规范
 * 放 CWD 的问题是：从不同的启动方式（快捷方式 / 服务 / 终端）起来会读到不同配置，
 * 表现为"双击能用、命令行起不来"。
 */
#include "qc_client.h"
#include "qc_conf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int qc_conf_default_path(char *out, size_t cap)
{
    const char *env = NULL;
    const char *home = NULL;
    const char *xdg = NULL;
#ifdef Q_SYS_WINDOWS
    const char *appdata = NULL;
#endif

    /* 1) 显式指定优先：联调 / 多实例共机时用 */
    env = getenv(QC_ENV_CONF_PATH);
    if (env && env[0])
    {
        if (!out || cap == 0u)
            return 0;
        qc_conf_copy(out, cap, env);
        return 0;
    }

    if (!out || cap == 0u)
        return -1;

    home = getenv("HOME");
    xdg  = getenv("XDG_CONFIG_HOME");
#ifdef Q_SYS_WINDOWS
    appdata = getenv("LOCALAPPDATA");
    if (appdata && appdata[0])
        snprintf(out, cap, "%s\\vms\\vms_client.xml", appdata);
    else
        qc_conf_copy(out, cap, "vms_client.xml");   /* 回退：当前目录 */
#elif defined(Q_SYS_MACOS)
    if (home && home[0])
        snprintf(out, cap, "%s/Library/Application Support/vms/vms_client.xml", home);
    else
        qc_conf_copy(out, cap, "vms_client.xml");
#else
    if (xdg && xdg[0])
        snprintf(out, cap, "%s/vms/vms_client.xml", xdg);
    else if (home && home[0])
        snprintf(out, cap, "%s/.config/vms/vms_client.xml", home);
    else
        qc_conf_copy(out, cap, "vms_client.xml");
#endif

    out[cap - 1u] = '\0';
    return 0;
}

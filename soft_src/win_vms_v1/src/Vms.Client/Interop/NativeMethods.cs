using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Vms.Client.Interop;

/// <summary>
/// P/Invoke 声明层。只做「签名 ↔ 导出符号」的机械映射，不含业务逻辑。
///
/// 用 <c>LibraryImport</c>（.NET 7+ 源生成器）而不是 <c>DllImport</c> 的原因：
///   - 编译期生成 marshalling 代码，省掉运行时 IL stub，调用开销更低；
///   - 字符串统一走 UTF-8（Windows 上 DllImport 默认 ANSI，中文必然踩坑）；
///   - 未来若启用 NativeAOT / 裁剪，源生成版本不受影响。
/// </summary>
internal static partial class NativeMethods
{
    public const string LibraryName = "vms_core";

    // ----------------------------- 生命周期 / 自检 -----------------------------

    [LibraryImport(LibraryName, EntryPoint = "vms_abi_version")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial uint vms_abi_version();

    [LibraryImport(LibraryName, EntryPoint = "vms_version_string")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial IntPtr vms_version_string();

    [LibraryImport(LibraryName, EntryPoint = "vms_struct_size")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial nuint vms_struct_size(int kind);

    [LibraryImport(LibraryName, EntryPoint = "vms_create", StringMarshalling = StringMarshalling.Utf8)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_create(string? configJson, out IntPtr ctx);

    [LibraryImport(LibraryName, EntryPoint = "vms_destroy")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial void vms_destroy(IntPtr ctx);

    // --------------------------------- 会话 ----------------------------------

    [LibraryImport(LibraryName, EntryPoint = "vms_connect", StringMarshalling = StringMarshalling.Utf8)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_connect(IntPtr ctx, string? endpoint);

    [LibraryImport(LibraryName, EntryPoint = "vms_disconnect")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_disconnect(IntPtr ctx);

    // ------------------------- M1：真实登录闭环（ABI v3） -------------------------
    // 返回值：0 = 成功；>0 = 服务端结果码（Q_IPC_R_*，3 = Token/凭据问题需重登）；
    //         <0 = 本库错误码，详情用 vms_last_error 取。

    [LibraryImport(LibraryName, EntryPoint = "vms_login")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_login(IntPtr ctx, in NativeCredentials cred);

    [LibraryImport(LibraryName, EntryPoint = "vms_logout")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_logout(IntPtr ctx);

    [LibraryImport(LibraryName, EntryPoint = "vms_heartbeat")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_heartbeat(IntPtr ctx);

    [LibraryImport(LibraryName, EntryPoint = "vms_is_connected")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_is_connected(IntPtr ctx);

    // --------------------------------- 数据 ----------------------------------

    [LibraryImport(LibraryName, EntryPoint = "vms_get_stats")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_get_stats(IntPtr ctx, out NativeStats stats);

    [LibraryImport(LibraryName, EntryPoint = "vms_get_companies")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_get_companies(IntPtr ctx, IntPtr buffer, nuint cap, out nuint count);

    [LibraryImport(LibraryName, EntryPoint = "vms_query_vehicles")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_query_vehicles(IntPtr ctx, in NativeQuery query, IntPtr buffer, nuint cap, out nuint count);

    // --------------------------- M4：会话治理（ABI v6） ---------------------------
    // 回调走函数指针（与 vms_subscribe 同一套路）：由 [UnmanagedCallersOnly] 静态方法
    // 取得指针，配 GCHandle 找回实例 —— 不持有托管委托，不会被 GC 回收导致崩溃。

    [LibraryImport(LibraryName, EntryPoint = "vms_keeper_start")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_keeper_start(IntPtr ctx, in NativeKeeperCfg cfg);

    [LibraryImport(LibraryName, EntryPoint = "vms_keeper_stop")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_keeper_stop(IntPtr ctx);

    [LibraryImport(LibraryName, EntryPoint = "vms_keeper_wake")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_keeper_wake(IntPtr ctx);

    [LibraryImport(LibraryName, EntryPoint = "vms_get_session_info")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_get_session_info(IntPtr ctx, out NativeSessionInfo info);

    [LibraryImport(LibraryName, EntryPoint = "vms_set_session_callback")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_set_session_callback(IntPtr ctx, IntPtr callback, IntPtr userData);

    [LibraryImport(LibraryName, EntryPoint = "vms_set_relogin_callback")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_set_relogin_callback(IntPtr ctx, IntPtr callback, IntPtr userData);

    [LibraryImport(LibraryName, EntryPoint = "vms_lock")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_lock(IntPtr ctx);

    [LibraryImport(LibraryName, EntryPoint = "vms_unlock")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_unlock(IntPtr ctx);

    /// <summary>
    /// PBKDF2-SM3 派生（仅本地运算）。重登回调里用它把缓存的口令派生成 pwd_hash ——
    /// 口令因此<b>不需要</b>交给 native 长期保存。
    /// </summary>
    [LibraryImport(LibraryName, EntryPoint = "vms_derive_pwd_hash", StringMarshalling = StringMarshalling.Utf8)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_derive_pwd_hash(string password, IntPtr salt, nuint saltLen,
                                                  uint iter, IntPtr outHash);

    // ------------------- v7：客户端配置（XML，三端共用一份键名） -------------------
    // 解析在 native 完成：返回值 0 成功；VMS_ERR_NOT_FOUND(-3) 文件不存在；
    // VMS_ERR_INTERNAL(-8) XML 解析失败。

    [LibraryImport(LibraryName, EntryPoint = "vms_load_config", StringMarshalling = StringMarshalling.Utf8)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_load_config(string? path, out NativeClientConf conf);

    [LibraryImport(LibraryName, EntryPoint = "vms_config_default_path")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe partial int vms_config_default_path(byte* outPath, nuint cap);

    [LibraryImport(LibraryName, EntryPoint = "vms_config_validate")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static unsafe partial int vms_config_validate(in NativeClientConf conf,
                                                         byte* err, nuint errCap);

    [LibraryImport(LibraryName, EntryPoint = "vms_config_free")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial void vms_config_free(ref NativeClientConf conf);

    [LibraryImport(LibraryName, EntryPoint = "vms_login_with_config")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_login_with_config(IntPtr ctx, in NativeClientConf conf);

    // ------------------------------- 推送 / 日志 -------------------------------

    // ⚠ M5：vms_subscribe 仍负责注册**轨迹回调**（vms_push_cb）——真实推送长连接
    //   （vms_push_start）会把轨迹经 ctx->push_cb 抛回 UI，所以即使走真实推送也必须先
    //   调它注册回调（Windows 上它还会顺带拉起本地 mock 退化线程）。
    [LibraryImport(LibraryName, EntryPoint = "vms_subscribe")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_subscribe(IntPtr ctx, IntPtr callback, IntPtr userData);

    [LibraryImport(LibraryName, EntryPoint = "vms_unsubscribe")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_unsubscribe(IntPtr ctx);

    /// <summary>
    /// v8（M5）：启动真实推送长连接（第二条独立 TCP）。会阻塞（SPA 敲门 + SM2 握手 +
    /// SM4-GCM + 独立 LOGIN + PUSH_BEGIN），调用方须放后台线程。连不上真实推送服务时
    /// <b>不</b>返回失败（native 转本地 mock 退化），只有参数非法（未登录 / password 空）才 &lt;0。
    /// </summary>
    [LibraryImport(LibraryName, EntryPoint = "vms_push_start", StringMarshalling = StringMarshalling.Utf8)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_push_start(IntPtr ctx, string? password, string? otp);

    [LibraryImport(LibraryName, EntryPoint = "vms_push_stop")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_push_stop(IntPtr ctx);

    /// <summary>
    /// v8（M5）：增量订阅（车辆树勾选/取消时调用）。op: 1=ADD / 2=REMOVE；
    /// ids 为 <c>const char* const*</c>（车辆主键字符串数组 "车牌+颜色"）。大集合 native 自动分片。
    /// </summary>
    [LibraryImport(LibraryName, EntryPoint = "vms_push_subscribe_delta")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_push_subscribe_delta(IntPtr ctx, int op, IntPtr ids, nuint n);

    /// <summary>v8（M5）：注册报警回调（与轨迹回调 vms_subscribe 相互独立）。</summary>
    [LibraryImport(LibraryName, EntryPoint = "vms_set_alarm_callback")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial int vms_set_alarm_callback(IntPtr ctx, IntPtr callback, IntPtr userData);

    [LibraryImport(LibraryName, EntryPoint = "vms_last_error")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    public static partial IntPtr vms_last_error(IntPtr ctx);

    // ------------------------------- 错误描述 --------------------------------

    public static string DescribeError(IntPtr ctx, int code)
    {
        if (code >= 0) return "OK";
        var name = Enum.IsDefined(typeof(NativeStatus), code)
            ? ((NativeStatus)code).ToString()
            : code.ToString();
        var ptr = vms_last_error(ctx);
        var detail = ptr == IntPtr.Zero ? null : Marshal.PtrToStringUTF8(ptr);
        return string.IsNullOrEmpty(detail) ? name : $"{name} — {detail}";
    }
}

/// <summary>
/// 决定 <c>vms_core.dll</c> 从哪里加载。
///
/// 不用裸 DllImport 依赖系统搜索路径，原因有两个：
///   - 安全：Windows 的 DLL 搜索顺序会优先命中 exe 同目录/PATH，攻击者放一个同名
///     DLL 就能劫持（DLL planting）。这里改成只从我们显式枚举的受控目录加载，
///     并且是绝对路径 <c>NativeLibrary.Load</c>。
///   - 可维护：开发期让 exe 能直接跑到 <c>native/build</c> 的输出。
/// </summary>
internal static class NativeLibraryResolver
{
    private static readonly object Gate = new();
    private static bool _registered;
    private static string? _resolvedPath;

    /// <summary>已解析到的 DLL 绝对路径；null 表示尚未成功解析。</summary>
    public static string? ResolvedPath => _resolvedPath;

    public static void EnsureRegistered()
    {
        lock (Gate)
        {
            if (_registered) return;
            _registered = true;
            // 必须在任何一次 P/Invoke 之前注册
            NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, Resolve);
        }
    }

    private static IntPtr Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPaths)
    {
        if (!string.Equals(libraryName, NativeMethods.LibraryName, StringComparison.OrdinalIgnoreCase))
            return IntPtr.Zero;

        foreach (var dir in EnumerateProbeDirectories())
        {
            foreach (var fileName in (string[])["vms_core.dll", "libvms_core.dll"])
            {
                var candidate = Path.GetFullPath(Path.Combine(dir, fileName));
                if (!File.Exists(candidate)) continue;
                if (NativeLibrary.TryLoad(candidate, out var handle))
                {
                    _resolvedPath = candidate;
                    return handle;
                }
            }
        }
        return IntPtr.Zero; // 交给运行时按默认规则继续找，最终抛 DllNotFoundException
    }

    private static IEnumerable<string> EnumerateProbeDirectories()
    {
        // 1) 环境变量优先，方便灰度/多版本并存切换
        var env = Environment.GetEnvironmentVariable("VMS_NATIVE_DIR");
        if (!string.IsNullOrWhiteSpace(env)) yield return env;

        var baseDir = AppContext.BaseDirectory;
        yield return baseDir;                                        // 随 exe 发布（推荐）
        yield return Path.Combine(baseDir, "runtimes", "win-x64", "native");
        yield return Path.Combine(baseDir, "native");

        // 2) 开发期：源码树里的 native/build 产物
        yield return Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", "..", "..", "native", "build"));
    }
}

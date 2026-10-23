using Vms.Client.Interop;

namespace Vms.Client.Services;

/// <summary>
/// 客户端配置的托管映射（对应 native <c>vms_client_conf</c>）。
///
/// 关键约定：**托管层不解析 XML**。解析在 native（l_4 qc_conf_* + l_1 q_xcfg）完成，
/// 键名、默认值、校验规则只有 C 里那一份 —— 否则三端 UI 各写一份解析，
/// 迟早演化出"Windows 默认心跳 60s、GTK 5s"这种只在现场才暴露的分歧。
/// </summary>
public sealed record ClientConfig(
    string Host,
    int Port,
    string PubKeyPath,
    string User,
    string? Password,
    string? Otp,
    string? DeviceIdHex,
    string? KnockEndpoint,
    string? SpaKeyPath,
    int Pbkdf2Iterations,
    int HeartbeatMs,
    int BackoffBaseMs,
    int BackoffMaxMs,
    int MaxRetry,
    bool AutoStart,
    int ConnectTimeoutMs,
    bool HasPlainPassword,
    bool UsedDefaults,
    string SourcePath,
    string ClientVersion,
    string LogLevel)
{
    /// <summary>
    /// 配置里没有口令 —— 必须走登录框由用户输入。
    /// 明文口令落盘在等保三级 / 密评下是不合格项，所以这不只是"少个字段"，
    /// 而是**合规分界**：为真时 UI 不得尝试自动登录。
    /// </summary>
    public bool NeedsPassword => string.IsNullOrEmpty(Password);

    /// <summary>人类可读摘要（口令与 OTP 一律不显示，可直接上状态栏 / 诊断页）。</summary>
    public string Summary =>
        $"{User}@{Host}:{Port} 心跳{HeartbeatMs}ms 退避{BackoffBaseMs}~{BackoffMaxMs}ms" +
        (HasPlainPassword ? " [配置含明文口令]" : "") +
        (UsedDefaults ? " [部分项走了默认值]" : "");
}

/// <summary>
/// 配置加载结果。<see cref="Error"/> 非空时 <see cref="Config"/> 为 null ——
/// 不静默降级成"半份配置"，否则 UI 会拿着空 host 去连默认值。
/// </summary>
public sealed record ConfigLoadResult(ClientConfig? Config, string? Error, string? Path)
{
    public bool Success => Config is not null;
}

/// <summary>
/// 客户端配置加载入口。
/// 配置文件路径优先级：显式传参 &gt; 环境变量 <c>VMS_CLIENT_CONF</c> &gt; 平台惯例
/// （Windows %LOCALAPPDATA%\vms\ / Linux ~/.config/vms/ / macOS ~/Library/Application Support/vms/，
/// 由 native 按平台给出）。
/// </summary>
public static unsafe class ClientConfigService
{
    public const string PathEnvVar = "VMS_CLIENT_CONF";

    /// <summary>native 给出的默认配置文件路径（已含环境变量优先级）。</summary>
    public static string? DefaultPath()
    {
        var buf = new byte[NativeClientConf.PathMax];
        fixed (byte* p = buf)
        {
            var rc = NativeMethods.vms_config_default_path(p, (nuint)buf.Length);
            return rc != 0 ? null : Utf8.Read(p, buf.Length);
        }
    }

    /// <summary>
    /// 加载并校验配置。path 为 null/空时走默认路径。
    /// 失败时返回人类可读原因（区分"文件不存在"与"XML 语法错"——
    /// 这两类问题的排查方向完全不同，混成一句会让运维先怀疑 XML 写错了）。
    /// </summary>
    public static ConfigLoadResult Load(string? path = null)
    {
        var rc = NativeMethods.vms_load_config(
            string.IsNullOrWhiteSpace(path) ? null : path, out var conf);

        if (rc != 0)
        {
            var why = rc switch
            {
                -3 => "配置文件不存在",
                -8 => "XML 解析失败",
                -1 => "入参非法",
                _ => $"native 返回 {rc}",
            };
            var where = string.IsNullOrWhiteSpace(path) ? DefaultPath() : path;
            return new ConfigLoadResult(null, $"{why}（路径：{where}）", where);
        }

        /* 业务校验（host/port/pubkey 是否齐全）必须在 free **之前**做：
           free 会清零结构体，拿清零后的结构去校验必然报缺 host。 */
        var vbuf = new byte[256];
        string? verr = null;
        fixed (byte* vp = vbuf)
        {
            var vrc = NativeMethods.vms_config_validate(in conf, vp, (nuint)vbuf.Length);
            if (vrc > 0) verr = Utf8.Read(vp, vbuf.Length);
        }

        var cfg = ToManaged(conf);

        /* native 侧结构体里还有一份口令副本，读完立刻让 native 清零。
           两份长期驻留等于把泄露面翻倍。 */
        NativeMethods.vms_config_free(ref conf);

        return new ConfigLoadResult(cfg, verr, cfg.SourcePath);
    }

    private static ClientConfig ToManaged(in NativeClientConf c)
    {
        fixed (NativeClientConf* p = &c)
        {
            var flags = (NativeConfFlags)c.Flags;
            return new ClientConfig(
                Host: Utf8.Read(p->Host, NativeClientConf.HostMax),
                Port: c.Port,
                PubKeyPath: Utf8.Read(p->PubKeyPath, NativeClientConf.PathMax),
                User: Utf8.Read(p->User, NativeClientConf.UserMax),
                Password: NullIfEmpty(Utf8.Read(p->Password, NativeClientConf.PassMax)),
                Otp: NullIfEmpty(Utf8.Read(p->Otp, NativeClientConf.OtpMax)),
                DeviceIdHex: NullIfEmpty(Utf8.Read(p->DevIdHex, NativeClientConf.DevIdHexMax)),
                KnockEndpoint: NullIfEmpty(Utf8.Read(p->Knock, NativeClientConf.HostMax)),
                SpaKeyPath: NullIfEmpty(Utf8.Read(p->SpaKeyPath, NativeClientConf.PathMax)),
                Pbkdf2Iterations: (int)c.Pbkdf2Iter,
                HeartbeatMs: (int)c.HeartbeatMs,
                BackoffBaseMs: (int)c.BackoffBaseMs,
                BackoffMaxMs: (int)c.BackoffMaxMs,
                MaxRetry: (int)c.MaxRetry,
                AutoStart: c.AutoStart != 0,
                ConnectTimeoutMs: (int)c.ConnectTimeoutMs,
                HasPlainPassword: (flags & NativeConfFlags.PlainPassword) != 0,
                UsedDefaults: (flags & NativeConfFlags.Defaulted) != 0,
                SourcePath: Utf8.Read(p->SourcePath, NativeClientConf.PathMax),
                ClientVersion: Utf8.Read(p->ClientVersion, NativeClientConf.CverMax),
                LogLevel: Utf8.Read(p->LogLevel, NativeClientConf.LevelMax));
        }
    }

    private static string? NullIfEmpty(string s) => string.IsNullOrWhiteSpace(s) ? null : s;
}

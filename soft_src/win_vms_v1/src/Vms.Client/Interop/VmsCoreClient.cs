using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;
using Vms.Client.Models;

namespace Vms.Client.Interop;

/// <summary>native 库的可用性探测结果，供 UI 决定是否降级到内置模拟数据。</summary>
public sealed record NativeProbeResult(bool Success, string Message, string? DllPath, string? Version)
{
    public static NativeProbeResult Fail(string message) => new(false, message, null, null);
}

/// <summary>
/// 登录参数（M1）。口令只在 <see cref="VmsCoreClient.Login"/> 调用期间存在于栈上结构体，
/// 调用结束即清零 —— 调用方不要自行缓存/落盘。
/// </summary>
/// <param name="Host">netd 主机（IPv4 / 域名）</param>
/// <param name="Port">netd 端口（默认 18099）</param>
/// <param name="User">用户名</param>
/// <param name="Password">口令（不外发，仅本地派生 PBKDF2-SM3）</param>
/// <param name="Otp">动态口令；服务端未启用校验时可留空</param>
/// <param name="DeviceIdHex">设备指纹 hex（偶数长度）；留空用 native 内置值</param>
/// <param name="ServerPubKeyPath">服务端静态 SM2 公钥 DER 路径（必填，抗中间人）</param>
/// <param name="KnockEndpoint">knockd 的 “ip:port”；未部署端口隐身时留空</param>
/// <param name="SpaKeyPath">SPA 密钥文件路径（16B），随 KnockEndpoint 使用</param>
/// <param name="Pbkdf2Iterations">派生迭代次数；0 = 用协议默认 100000</param>
public sealed record LoginOptions(
    string Host,
    int Port,
    string User,
    string Password,
    string? Otp = null,
    string? DeviceIdHex = null,
    string? ServerPubKeyPath = null,
    string? KnockEndpoint = null,
    string? SpaKeyPath = null,
    int Pbkdf2Iterations = 0);

/// <summary>
/// 对 native 核心库的高层托管封装：负责
///   1. ABI 自检（版本 + 结构体尺寸）；
///   2. 生命周期（SafeHandle 语义的 create/destroy）；
///   3. 把 native 工作线程的推送回调，**编组回 UI 线程**再抛事件。
///
/// 线程模型（WinUI3 上最容易翻车的地方）：
///   native 的推送回调跑在 C 侧自己创建的线程上。该线程里 <b>绝对不能</b>碰任何
///   XAML 对象 —— 不是"可能出错"，是必然抛 RPC_E_WRONG_THREAD / 直接崩。
///   所以这里的做法是：回调内只做数据拷贝（不动 UI），然后通过捕获的
///   <see cref="DispatcherQueue"/> 切回 UI 线程再触发 <see cref="VehiclesPushed"/>。
///
/// 回调指针用的是 <c>[UnmanagedCallersOnly]</c> + 函数指针，而不是
/// Marshal.GetFunctionPointerForDelegate：
///   - 不需要持有委托实例（避免"忘了 _callback 字段被 GC 回收"这类经典崩溃）；
///   - 没有每次调用的 marshalling 分配，热路径更干净；
///   - 只用 static 方法 + GCHandle 找回实例，无状态依赖。
/// </summary>
internal sealed unsafe class VmsCoreClient : IDisposable
{
    private const uint ExpectedAbiVersion = 3;   // v3：新增 vms_credentials + vms_login/logout/heartbeat

    private readonly DispatcherQueue? _uiQueue;
    private GCHandle _selfHandle;
    private IntPtr _ctx;
    private bool _subscribed;
    private bool _disposed;
    private bool _loggedIn;      // M1：是否通过真实后台认证（false = 仍在用 Mock 数据）

    private VmsCoreClient(IntPtr ctx, DispatcherQueue? uiQueue)
    {
        _ctx = ctx;
        _uiQueue = uiQueue;
    }

    /// <summary>native 推送到达时触发；已在 UI 线程上。</summary>
    public event EventHandler<IReadOnlyList<VehicleRow>>? VehiclesPushed;

    public bool IsConnected => _ctx != IntPtr.Zero && NativeMethods.vms_is_connected(_ctx) == 1;

    // ============================== ABI 守卫 ==============================

    public static NativeProbeResult Probe()
    {
        NativeLibraryResolver.EnsureRegistered();

        try
        {
            var abi = NativeMethods.vms_abi_version();
            if (abi != ExpectedAbiVersion)
                return NativeProbeResult.Fail($"native ABI 版本不匹配：DLL={abi}，客户端期望 {ExpectedAbiVersion}");

            var mismatches = new List<string>();
            Check(NativeStructKind.AbiInfo, sizeof(NativeAbiInfo), nameof(NativeAbiInfo), mismatches);
            Check(NativeStructKind.Stats, sizeof(NativeStats), nameof(NativeStats), mismatches);
            Check(NativeStructKind.Company, sizeof(NativeCompany), nameof(NativeCompany), mismatches);
            Check(NativeStructKind.Vehicle, sizeof(NativeVehicle), nameof(NativeVehicle), mismatches);
            Check(NativeStructKind.Query, sizeof(NativeQuery), nameof(NativeQuery), mismatches);
            Check(NativeStructKind.Credentials, sizeof(NativeCredentials), nameof(NativeCredentials), mismatches);

            if (mismatches.Count > 0)
                return NativeProbeResult.Fail("结构体布局与 native 不一致：" + string.Join("; ", mismatches));

            var version = Marshal.PtrToStringUTF8(NativeMethods.vms_version_string()) ?? "unknown";
            return new NativeProbeResult(true, $"native 核心库 v{version}，ABI {abi} 校验通过", NativeLibraryResolver.ResolvedPath, version);
        }
        catch (DllNotFoundException)
        {
            return NativeProbeResult.Fail("未找到 vms_core.dll（已探测 exe 目录 / runtimes\\win-x64\\native / native\\build / VMS_NATIVE_DIR）");
        }
        catch (EntryPointNotFoundException ex)
        {
            return NativeProbeResult.Fail($"DLL 导出符号缺失：{ex.Message}");
        }
        catch (BadImageFormatException ex)
        {
            // 典型的"架构不匹配"：例如 x64 的 vms_core.dll 被 arm64ec 进程加载。
            // 这种必须降级，否则会带着进程一起崩。
            return NativeProbeResult.Fail($"vms_core.dll 架构不匹配（应为与进程一致的位数）：{ex.Message}");
        }
        catch (Exception ex)
        {
            // 任何其它加载/调用异常都走"降级 + 如实告知"，不静默吞掉，也不打崩进程。
            return NativeProbeResult.Fail($"native 探针异常，已降级：{ex.Message}");
        }

        static void Check(NativeStructKind kind, int managedSize, string name, List<string> sink)
        {
            var nativeSize = (long)NativeMethods.vms_struct_size((int)kind);
            if (nativeSize != managedSize)
                sink.Add($"{name}: native={nativeSize}B / managed={managedSize}B");
        }
    }

    public static VmsCoreClient Create(string? configJson = null, string? endpoint = null)
    {
        NativeLibraryResolver.EnsureRegistered();

        var rc = NativeMethods.vms_create(configJson, out var ctx);
        if (rc != 0 || ctx == IntPtr.Zero)
            throw new InvalidOperationException($"vms_create 失败：{NativeMethods.DescribeError(IntPtr.Zero, rc)}");

        var client = new VmsCoreClient(ctx, DispatcherQueue.GetForCurrentThread());
        try
        {
            var crc = NativeMethods.vms_connect(ctx, endpoint);
            if (crc != 0)
                throw new InvalidOperationException($"vms_connect 失败：{NativeMethods.DescribeError(ctx, crc)}");
            return client;
        }
        catch
        {
            client.Dispose();
            throw;
        }
    }

    // ============================ M1：真实登录 ============================

    /// <summary>登录结果。<see cref="Code"/>：0 成功；&gt;0 服务端结果码（Q_IPC_R_*，3=Token/凭据问题）；&lt;0 本库错误码。</summary>
    public sealed record LoginResult(bool Success, int Code, string Message);

    /// <summary>
    /// 真实登录：SPA 敲门（可选）→ SM2 握手 → HELLO → 取 salt → 本地 PBKDF2-SM3 → LOGIN。
    /// 口令只在本结构体内存留到调用结束（native 侧派生后即清零），**不要**记录到日志/配置。
    /// </summary>
    public LoginResult Login(LoginOptions opt)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        ArgumentNullException.ThrowIfNull(opt);
        if (string.IsNullOrWhiteSpace(opt.ServerPubKeyPath))
            throw new ArgumentException("必须提供服务端 SM2 公钥路径（用于校验服务端身份，抗中间人）", nameof(opt));
        if (opt.Port is <= 0 or > 65535) throw new ArgumentOutOfRangeException(nameof(opt), "端口非法");

        var native = new NativeCredentials
        {
            Port = (ushort)opt.Port,
            Pbkdf2Iter = (uint)Math.Max(0, opt.Pbkdf2Iterations),
        };
        Utf8.Write(native.Host, NativeCredentials.HostMax, opt.Host);
        Utf8.Write(native.User, NativeCredentials.UserMax, opt.User);
        Utf8.Write(native.Otp, NativeCredentials.OtpMax, opt.Otp);
        Utf8.Write(native.DevIdHex, NativeCredentials.DevIdHexMax, opt.DeviceIdHex);
        Utf8.Write(native.PubKeyPath, NativeCredentials.PathMax, opt.ServerPubKeyPath);
        Utf8.Write(native.Knock, NativeCredentials.HostMax, opt.KnockEndpoint);
        Utf8.Write(native.SpaKeyPath, NativeCredentials.PathMax, opt.SpaKeyPath);

        int rc;
        try
        {
            Utf8.Write(native.Password, NativeCredentials.PassMax, opt.Password);
            rc = NativeMethods.vms_login(_ctx, in native);
        }
        finally
        {
            // 口令用完即清：结构体在栈上，但仍显式清零，避免被后续栈帧/崩溃转储带走
            Utf8.Write(native.Password, NativeCredentials.PassMax, null);
        }

        _loggedIn = rc == 0;
        if (rc == 0) return new LoginResult(true, 0, "登录成功");

        var msg = rc > 0
            ? $"服务端拒绝登录：结果码 {rc}" + (rc == 3 ? "（Token/凭据问题，需重新登录）" : "")
            : NativeMethods.DescribeError(_ctx, rc);
        return new LoginResult(false, rc, msg);
    }

    public void Logout()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        NativeMethods.vms_logout(_ctx);
        _loggedIn = false;
    }

    /// <summary>
    /// 心跳保活。注意：后台心跳**不续期** Token（TTL 1800s），
    /// 到期后业务请求会返回结果码 3，上层需自动重登。
    /// </summary>
    public int Heartbeat()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        return NativeMethods.vms_heartbeat(_ctx);
    }

    /// <summary>是否已通过真实后台认证（false 表示仍在用内置 Mock 数据）。</summary>
    public bool IsLoggedIn => _loggedIn;

    // ================================ 数据 ================================

    /// <summary>同步读取状态栏统计。调用方负责放到后台线程（见上层 Service）。</summary>
    public SystemStats GetStats()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        var rc = NativeMethods.vms_get_stats(_ctx, out var s);
        if (rc != 0) throw new InvalidOperationException(NativeMethods.DescribeError(_ctx, rc));
        return new SystemStats(s.Total, s.Online, s.Offline, s.Excluded, s.AlarmUnhandled, s.OnlineRate);
    }

    public CompanyRecord[] GetCompanies()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);

        // 两次调用范式：先用 cap=0 问总数，再一次性取回，避免"SIZE 猜小了截断"。
        var rc = NativeMethods.vms_get_companies(_ctx, IntPtr.Zero, 0, out var total);
        if (total == 0) return [];
        if (rc != (int)NativeStatus.BufferSmall && rc != 0)
            throw new InvalidOperationException(NativeMethods.DescribeError(_ctx, rc));

        var buffer = new NativeCompany[(int)total];
        fixed (NativeCompany* p = buffer)
        {
            rc = NativeMethods.vms_get_companies(_ctx, (IntPtr)p, total, out var written);
            if (rc != 0) throw new InvalidOperationException(NativeMethods.DescribeError(_ctx, rc));

            var result = new CompanyRecord[(int)written];
            for (nuint i = 0; i < written; i++)
            {
                var node = &p[i];
                result[(int)i] = new CompanyRecord(
                    node->Id,
                    node->ParentId,
                    node->Online,
                    node->Count,
                    Utf8.Read(node->Name, NativeCompany.TextMax));
            }
            return result;
        }
    }

    public VehicleRow[] QueryVehicles(VehicleQuery query, out int totalCount)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        ArgumentNullException.ThrowIfNull(query);

        // 强制空结果：未勾选任何车辆/组织时，定位数据栏保持空、不加载（需求：未勾选前不加载车辆）。
        // 纯托管短路，不进 native 查询，避免把 3 万台车灌进列表。
        if (query.NoResults)
        {
            totalCount = 0;
            return [];
        }

        var native = new NativeQuery
        {
            CompanyId = query.CompanyId,
            Offset = query.Offset,
            Limit = query.Limit,
            OnlineOnly = query.OnlineOnly ? 1u : 0u,
            AlarmOnly = query.AlarmOnly ? 1u : 0u,
            AlarmMask = query.AlarmMask,
        };
        Utf8.Write(native.Keyword, NativeVehicle.TextMax, query.Keyword);

        // v2：按车辆 ID 精确过滤（树上勾选的车辆）。id_count==0 时 native 端不过滤。
        var ids = query.Ids;
        int n = ids.Count < NativeQuery.QueryMaxIds ? ids.Count : NativeQuery.QueryMaxIds;
        for (var i = 0; i < n; i++)
            native.Ids[i] = ids[i];
        native.IdCount = (uint)n;

        var buffer = new NativeVehicle[query.Limit];
        fixed (NativeVehicle* p = buffer)
        {
            var rc = NativeMethods.vms_query_vehicles(_ctx, in native, (IntPtr)p, query.Limit, out var matched);
            totalCount = (int)matched;

            if (rc is not 0 and not (int)NativeStatus.BufferSmall)
                throw new InvalidOperationException(NativeMethods.DescribeError(_ctx, rc));

            // 实际写入条数 = min(cap, 剩余)
            var written = Math.Min((int)query.Limit, Math.Max(0, totalCount - (int)query.Offset));
            var rows = new VehicleRow[written];
            for (var i = 0; i < written; i++)
                rows[i] = ToRow(&p[i]);
            return rows;
        }
    }

    private static VehicleRow ToRow(NativeVehicle* v) => new()
    {
        Id = v->Id,
        SelfCode = Utf8.Read(v->SelfCode, NativeVehicle.CodeMax),
        Plate = Utf8.Read(v->Plate, NativeVehicle.PlateMax),
        Time = Utf8.Read(v->Time, NativeVehicle.TimeMax),
        SpeedKph = v->SpeedKph,
        Location = Utf8.Read(v->Location, NativeVehicle.TextMax),
        Online = v->Online != 0,
        OfflineSeconds = v->OfflineSeconds,
        Day = Utf8.Read(v->Day, NativeVehicle.TimeMax),
        Issue = Utf8.Read(v->Issue, NativeVehicle.IssueMax),
        Alarm = (AlarmFlags)v->AlarmFlags,
        Lon = v->Lon,
        Lat = v->Lat,
    };

    // ============================== 实时推送 ==============================

    public void Subscribe()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_subscribed) return;

        _selfHandle = GCHandle.Alloc(this, GCHandleType.Normal);
        var fp = (IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, nuint, IntPtr, void>)&OnNativePush;

        var rc = NativeMethods.vms_subscribe(_ctx, fp, GCHandle.ToIntPtr(_selfHandle));
        if (rc != 0)
        {
            _selfHandle.Free();
            throw new InvalidOperationException($"vms_subscribe 失败：{NativeMethods.DescribeError(_ctx, rc)}");
        }
        _subscribed = true;
    }

    public void Unsubscribe()
    {
        if (!_subscribed || _ctx == IntPtr.Zero) return;
        NativeMethods.vms_unsubscribe(_ctx);
        _subscribed = false;
        if (_selfHandle.IsAllocated) _selfHandle.Free();
    }

    /// <summary>native 线程入口。只做数据拷贝，禁止触碰 UI。</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnNativePush(IntPtr items, nuint count, IntPtr userData)
    {
        if (userData == IntPtr.Zero || items == IntPtr.Zero || count == 0) return;

        var client = GCHandle.FromIntPtr(userData).Target as VmsCoreClient;
        if (client is null || client._disposed) return;

        try
        {
            var src = (NativeVehicle*)items;
            var batch = new VehicleRow[(int)count];
            for (nuint i = 0; i < count; i++)
                batch[(int)i] = ToRow(&src[i]);

            client.Post(batch);
        }
        catch (Exception ex)
        {
            // 回调里绝不能让异常跨回 native —— 那会直接把进程打崩
            System.Diagnostics.Debug.WriteLine($"[VmsCoreClient] push failed: {ex}");
        }
    }

    private void Post(VehicleRow[] batch)
    {
        if (_uiQueue is null)
        {
            VehiclesPushed?.Invoke(this, batch);
            return;
        }
        // TryEnqueue 不阻塞 native 线程；队列满时丢弃本帧（实时监控宁可丢帧也不卡采集）
        _uiQueue.TryEnqueue(DispatcherQueuePriority.Normal, () =>
        {
            if (!_disposed) VehiclesPushed?.Invoke(this, batch);
        });
    }

    // =============================== 释放 ===============================

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        Unsubscribe();
        if (_ctx != IntPtr.Zero)
        {
            NativeMethods.vms_destroy(_ctx);
            _ctx = IntPtr.Zero;
        }
        VehiclesPushed = null;
    }
}

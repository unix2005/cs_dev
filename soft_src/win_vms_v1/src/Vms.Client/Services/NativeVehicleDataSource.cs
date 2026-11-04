using Vms.Client.Interop;
using Vms.Client.Models;

namespace Vms.Client.Services;

/// <summary>
/// 由「自己写的 C 库」驱动的数据源。
///
/// 关键约定：<b>所有 P/Invoke 调用都放在线程池线程上</b>（Task.Run），
/// UI 线程只做属性赋值。原因：native 查询 3 万条记录即使在 C 侧只要几毫秒，
/// 但只要它在 UI 线程上执行，就会和 XAML 的渲染/输入共用同一条消息泵，
/// 表现为"界面一顿一顿"。这是桌面端最容易忽略、也最容易修的一类卡顿。
/// </summary>
internal sealed class NativeVehicleDataSource : IVehicleDataSource
{
    private readonly VmsCoreClient _client;
    private readonly string _version;

    public NativeVehicleDataSource(VmsCoreClient client, string version)
    {
        _client = client;
        _version = version;
        _client.VehiclesPushed += (s, e) => VehiclesPushed?.Invoke(this, e);
        // M5：报警推送（与轨迹独立），VmsCoreClient 已编组回 UI 线程，这里只做转发。
        _client.AlarmsPushed += (s, e) => AlarmsPushed?.Invoke(this, e);
        // M4：会话状态（在线 / 重连中 / 已断开）由 native 心跳线程发出，
        // VmsCoreClient 已编组回 UI 线程，这里只做转发。
        _client.SessionStateChanged += (s, e) => SessionStateChanged?.Invoke(this, e);
    }

    public string BackendName => $"native v{_version}";
    public bool IsNative => true;

    public event EventHandler<IReadOnlyList<VehicleRow>>? VehiclesPushed;

    /// <summary>M5：报警推送（与轨迹独立）。</summary>
    public event EventHandler<IReadOnlyList<AlarmRecord>>? AlarmsPushed;

    /// <summary>M4：会话治理状态变化（已在 UI 线程）。</summary>
    public event EventHandler<SessionStatus>? SessionStateChanged;

    /// <summary>M4：读取当前会话状态（心跳/重连/重登计数）。</summary>
    public SessionStatus GetSessionStatus() => _client.GetSessionStatus();

    /// <summary>M4：立即重连（用户点了"重试"时）。</summary>
    public void WakeSession() => _client.WakeKeeper();

    public Task<SystemStats> GetStatsAsync(CancellationToken ct = default)
        => Task.Run(_client.GetStats, ct);

    public Task<IReadOnlyList<CompanyRecord>> GetCompaniesAsync(CancellationToken ct = default)
        => Task.Run<IReadOnlyList<CompanyRecord>>(_client.GetCompanies, ct);

    public Task<(IReadOnlyList<VehicleRow> Rows, int Total)> QueryAsync(VehicleQuery query, CancellationToken ct = default)
        => Task.Run(() =>
        {
            var rows = _client.QueryVehicles(query, out var total);
            return ((IReadOnlyList<VehicleRow>)rows, total);
        }, ct);

    public void StartRealtime() => _client.Subscribe();
    public void StopRealtime() => _client.Unsubscribe();

    /// <summary>M5：启动真实推送长连接。vms_push_start 阻塞（敲门/握手/登录多往返），
    /// 必须放后台线程；连不上真实服务时 native 转本地 mock 退化、不抛错。</summary>
    public Task StartPushAsync(string password, string? otp = null)
        => Task.Run(() =>
        {
            var rc = _client.PushStart(password, otp);
            try
            {
                var dir = System.IO.Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Vms.Client");
                System.IO.Directory.CreateDirectory(dir);
                var si = _client.GetSessionStatus();
                System.IO.File.AppendAllText(System.IO.Path.Combine(dir, "trace.log"),
                    $"[{DateTime.Now:HH:mm:ss.fff}] push_start rc={rc} state={si.State} pushConnected={si.PushConnected} subs={si.PushSubs}\n");
            }
            catch (Exception ex)
            {
                try
                {
                    System.IO.File.AppendAllText(
                        System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Vms.Client", "trace.log"),
                        $"[{DateTime.Now:HH:mm:ss.fff}] push_start trace ex: {ex.Message}\n");
                }
                catch { }
            }
        }, CancellationToken.None);

    /// <summary>M5：停止推送长连接（幂等）。订阅集合保留，下次重连自动重放。</summary>
    public void StopPush() => _client.PushStop();

    /// <summary>M5：增量订阅（车辆树勾选/取消）。大集合 native 端自动分片。</summary>
    public void SubscribeDelta(int op, IReadOnlyList<string> ids)
    {
        try
        {
            var head = string.Join(",", ids.Take(8));
            System.IO.File.AppendAllText(
                System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Vms.Client", "trace.log"),
                $"[{DateTime.Now:HH:mm:ss.fff}] subscribe op={op} n={ids.Count} ids={head}{(ids.Count > 8 ? ",…" : "")}\n");
        }
        catch { }
        _client.SubscribeDelta(op, ids);
    }

    /// <summary>是否已完成真实后台登录（false = 数据仍是 native 内置 Mock）。</summary>
    public bool IsLoggedIn => _client.IsLoggedIn;

    /// <summary>
    /// 真实登录（M2）。**必须**走 Task.Run：native 侧是阻塞式网络调用，
    /// 网络不可达时会卡到 socket 超时（30s）才返回，绝不能在 UI 线程调用。
    /// </summary>
    public Task<VmsCoreClient.LoginResult> LoginAsync(LoginOptions opt, CancellationToken ct = default)
        => Task.Run(() => _client.Login(opt), ct);

    /// <summary>M5：用配置文件登录（会把 push_host/push_port 写进上下文，推送才连推送栈）。</summary>
    public Task<VmsCoreClient.LoginResult> LoginWithConfigAsync(string configPath, CancellationToken ct = default)
        => Task.Run(() => _client.LoginWithConfig(configPath), ct);

    public void Dispose() => _client.Dispose();
}

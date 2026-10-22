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
        // M4：会话状态（在线 / 重连中 / 已断开）由 native 心跳线程发出，
        // VmsCoreClient 已编组回 UI 线程，这里只做转发。
        _client.SessionStateChanged += (s, e) => SessionStateChanged?.Invoke(this, e);
    }

    public string BackendName => $"native v{_version}";
    public bool IsNative => true;

    public event EventHandler<IReadOnlyList<VehicleRow>>? VehiclesPushed;

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

    /// <summary>是否已完成真实后台登录（false = 数据仍是 native 内置 Mock）。</summary>
    public bool IsLoggedIn => _client.IsLoggedIn;

    /// <summary>
    /// 真实登录（M2）。**必须**走 Task.Run：native 侧是阻塞式网络调用，
    /// 网络不可达时会卡到 socket 超时（30s）才返回，绝不能在 UI 线程调用。
    /// </summary>
    public Task<VmsCoreClient.LoginResult> LoginAsync(LoginOptions opt, CancellationToken ct = default)
        => Task.Run(() => _client.Login(opt), ct);
    public void Dispose() => _client.Dispose();
}

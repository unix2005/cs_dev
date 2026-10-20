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
    }

    public string BackendName => $"native v{_version}";
    public bool IsNative => true;

    public event EventHandler<IReadOnlyList<VehicleRow>>? VehiclesPushed;

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
    public void Dispose() => _client.Dispose();
}

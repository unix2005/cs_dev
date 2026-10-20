using Vms.Client.Models;

namespace Vms.Client.Services;

/// <summary>
/// 数据源抽象。UI 只依赖这个接口，因此「接 native 库」和「先跑模拟数据」
/// 对界面完全透明 —— 这也让后续把 C 库换成 HTTP/gRPC 后端时零改动。
/// </summary>
public interface IVehicleDataSource : IDisposable
{
    /// <summary>状态栏展示的后端标识，例如 "native v1.0.0" / "内置模拟数据"。</summary>
    string BackendName { get; }

    /// <summary>是否由真实 native 库驱动。</summary>
    bool IsNative { get; }

    Task<SystemStats> GetStatsAsync(CancellationToken ct = default);

    Task<IReadOnlyList<CompanyRecord>> GetCompaniesAsync(CancellationToken ct = default);

    /// <summary>分页查询车辆；返回本页数据与符合条件的总条数。</summary>
    Task<(IReadOnlyList<VehicleRow> Rows, int Total)> QueryAsync(VehicleQuery query, CancellationToken ct = default);

    /// <summary>实时位置推送（已编组到 UI 线程）。</summary>
    event EventHandler<IReadOnlyList<VehicleRow>>? VehiclesPushed;

    void StartRealtime();
    void StopRealtime();
}

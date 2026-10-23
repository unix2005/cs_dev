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

    /// <summary>M5：报警推送（与轨迹独立，已编组到 UI 线程）。</summary>
    event EventHandler<IReadOnlyList<AlarmRecord>>? AlarmsPushed;

    void StartRealtime();
    void StopRealtime();

    /// <summary>M5：启动真实推送长连接（第二条独立 TCP，会阻塞，内部已放后台线程）。</summary>
    Task StartPushAsync(string password, string? otp = null);

    /// <summary>M5：停止推送长连接（幂等）。订阅集合保留。</summary>
    void StopPush();

    /// <summary>M5：增量订阅（车辆树勾选/取消）。op: 1=ADD / 2=REMOVE；ids 为车辆主键字符串。</summary>
    void SubscribeDelta(int op, IReadOnlyList<string> ids);
}

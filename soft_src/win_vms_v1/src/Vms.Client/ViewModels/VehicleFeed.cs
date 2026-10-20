using System.Collections.ObjectModel;
using Vms.Client.Models;
using Vms.Client.Services;

namespace Vms.Client.ViewModels;

/// <summary>
/// 车辆列表的「分页 + 就地刷新」容器。
///
/// 为什么不一次性把 34554 行灌进 ListView？
///   - 一次性构造 34554 个对象 → 首屏要等几百毫秒到数秒，内存也会明显上涨；
///   - 数据是实时变的，全量加载的"快照"很快就失真。
/// 所以按页拉（默认 200 行/页），滚动接近底部时再取下一页；
/// 推送回来的行如果在已加载窗口内就原地更新，不在就丢弃（用户还在看别的页）。
/// </summary>
public sealed class VehicleFeed : ObservableCollection<VehicleRow>
{
    private readonly IVehicleDataSource _source;
    private readonly Dictionary<uint, VehicleRow> _index = new(512);

    public VehicleFeed(IVehicleDataSource source, VehicleQuery query)
    {
        _source = source;
        Query = query;
    }

    public VehicleQuery Query { get; }

    /// <summary>符合当前条件的总条数（由后端返回，不是本地已加载条数）。</summary>
    public int TotalCount { get; private set; }

    public bool IsLoading { get; private set; }
    public bool HasMore => Count < TotalCount;

    /// <summary>Feed 状态变化（总条数/加载完成），供 ViewModel 更新界面文案。</summary>
    public event EventHandler? StateChanged;

    /// <summary>条件变化后重置并加载第一页。</summary>
    public async Task ResetAsync(CancellationToken ct = default)
    {
        Clear();
        _index.Clear();
        TotalCount = 0;
        Query.Offset = 0;
        await LoadMoreAsync(ct);
    }

    public async Task LoadMoreAsync(CancellationToken ct = default)
    {
        if (IsLoading) return;
        if (Count > 0 && !HasMore) return;

        IsLoading = true;
        StateChanged?.Invoke(this, EventArgs.Empty);
        try
        {
            Query.Offset = (uint)Count;
            var (rows, total) = await _source.QueryAsync(Query, ct);
            TotalCount = total;

            foreach (var row in rows)
            {
                _index[row.Id] = row;
                Add(row);
            }
        }
        finally
        {
            IsLoading = false;
            StateChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    /// <summary>应用一帧实时推送。必须在 UI 线程调用。</summary>
    public int ApplyPush(IReadOnlyList<VehicleRow> batch)
    {
        var applied = 0;
        foreach (var incoming in batch)
        {
            if (!_index.TryGetValue(incoming.Id, out var existing)) continue; // 不在可见窗口，忽略
            existing.CopyFrom(incoming);
            applied++;
        }
        return applied;
    }
}

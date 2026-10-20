using System.Collections.ObjectModel;

namespace Vms.Client.Models;

/// <summary>
/// 组织机构节点（经典三态复选的"父节点"一方）。
///
/// 树上同时挂两类子节点：
///   · <see cref="CompanyNode"/> 下级组织 —— 三态复选（true/false/null），整支勾选/取消都经它；
///   · <see cref="VehicleNode"/> 车辆叶子 —— 两态（只 true/false），单个车辆勾选也经它。
///
/// 三态语义（用户明确要求）：勾选本组织 ⇒ 本级及所有下级组织、车辆全选；
/// 取消某一下级组织 ⇒ 该级及车辆取消，且<b>父组织自动变为"部分选择"(null)</b>。
/// 父级状态不是写死的，而是按子节点实时推导（见 <see cref="RefreshCheckStateFromChildren"/>），
/// 所以一辆车被单独勾上，其父组织也会变成"部分选择"——这正是经典三态树的行为。
/// </summary>
public sealed class CompanyNode : OrgNodeBase
{
    private uint _online;
    private uint _count;
    private bool _vehiclesLoaded;
    private bool _isLoadingVehicles;

    public required uint ParentId { get; init; }

    public override bool IsVehicle => false;

    public uint Online
    {
        get => _online;
        set { if (SetProperty(ref _online, value)) OnPropertyChanged(nameof(CountText)); }
    }

    public uint Count
    {
        get => _count;
        set { if (SetProperty(ref _count, value)) OnPropertyChanged(nameof(CountText)); }
    }

    /// <summary>树上显示的计数徽标，形如 <c>8960/18714</c>。</summary>
    public string CountText => $"{Online:N0}/{Count:N0}";

    /// <summary>车辆是否已拉取（懒加载标记，避免每次展开都重新查一遍）。</summary>
    public bool IsVehiclesLoaded
    {
        get => _vehiclesLoaded;
        set => SetProperty(ref _vehiclesLoaded, value);
    }

    public bool IsLoadingVehicles
    {
        get => _isLoadingVehicles;
        set => SetProperty(ref _isLoadingVehicles, value);
    }

    /// <summary>树上该组织下实际加载出来的车辆（受 <c>VehicleTreePageSize</c> 限制）。</summary>
    public int LoadedVehicleCount => Children.OfType<VehicleNode>().Count(v => v.Id != 0);

    public IEnumerable<CompanyNode> CompanyChildren => Children.OfType<CompanyNode>();

    public IEnumerable<VehicleNode> VehicleChildren => Children.OfType<VehicleNode>();

    /// <summary>
    /// 本组织三态勾选态（继承自 <see cref="OrgNodeBase.IsChecked"/>，类型为 <c>bool?</c>）。
    ///
    /// setter 是<b>纯状态</b>，不参与级联也不回溯父级 —— 级联/回溯由
    /// <see cref="SetCheckedWithPropagation"/> 与 <see cref="OrgNodeBase.RecomputeAncestors"/> 显式完成。
    /// 这样"勾一整支"只需一次递归，不会每设一个值又触发 O(n²) 向下/向上传播。
    ///
    /// 三个取值语义：
    ///   · <c>true</c>  —— 本组织及其全部下级组织、车辆已被选中；
    ///   · <c>false</c> —— 本组织及其全部下级均未选中；
    ///   · <c>null</c>  —— 部分选择：下级里有勾有空（典型：勾了几个下级组织、或只勾了几台车）。
    /// </summary>

    public void AddChild(CompanyNode child) => base.AddChild(child);

    public void AddVehicle(VehicleNode vehicle) => base.AddChild(vehicle);

    /// <summary>清空已加载的车辆（切换数据源 / 强制刷新时用）。</summary>
    public void ClearVehicles()
    {
        foreach (var v in Children.OfType<VehicleNode>().ToList())
            Children.Remove(v);
        IsVehiclesLoaded = false;
        OnPropertyChanged(nameof(LoadedVehicleCount));
    }

    public void RaiseVehicleCountChanged() => OnPropertyChanged(nameof(LoadedVehicleCount));

    /// <summary>
    /// 把<b>本级及整棵子树</b>（下级组织 + 车辆叶子）一次性置成确定勾选态。
    /// 递归一次到位，不重复触发级联；不回溯父级（父级的"部分选择"由随后的一次
    /// <see cref="OrgNodeBase.RecomputeAncestors"/> 统一推导，避免每改一个子节点就向上爬一遍）。
    /// </summary>
    public override void SetSubtreeChecked(bool value)
    {
        IsChecked = value;
        foreach (var child in Children)
            child.SetSubtreeChecked(value);
    }

    /// <summary>
    /// 按子节点的实际勾选态，重算<b>本组织</b>的勾选态（经典三态推导）：
    ///   · 任一子节点为 null（部分选择），或子节点中同时有 true 与 false ⇒ 本组织 = null（部分选择）；
    ///   · 子节点全为 true ⇒ 本组织 = true；
    ///   · 子节点全为 false ⇒ 本组织 = false；
    ///   · 没有任何子节点 ⇒ 保持当前确定态不变（叶子组织未加载车辆时本就是 true/false）。
    /// 这是"父组织随子节点变成部分选择"的核心：父级不存储、只推导。
    /// </summary>
    public void RefreshCheckStateFromChildren()
    {
        if (Children.Count == 0) return;   // 无子节点：保留当前确定态（不猜测）

        bool anyTrue = false, anyFalse = false, anyNull = false;
        foreach (var child in Children)
        {
            var c = child.IsChecked;
            if (c == true) anyTrue = true;
            else if (c == false) anyFalse = true;
            else anyNull = true;
        }

        bool? result = anyNull || (anyTrue && anyFalse) ? null
                      : anyTrue ? true
                      : anyFalse ? false
                      : (bool?)null;
        IsChecked = result;
    }

    /// <summary>
    /// 用户点击组织复选框的"一次性"入口：
    /// 先把本级及整棵子树置成确定态（<paramref name="value"/>），
    /// 再沿父链向上重算每个祖先组织的勾选态（可能变 null=部分选择）。
    /// 既保证"勾一整支"一步到位，又保证"取消某下级后父级变部分选择"。
    /// </summary>
    public void SetCheckedWithPropagation(bool value)
    {
        SetSubtreeChecked(value);
        RecomputeAncestors();
    }
}

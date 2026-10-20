using System.Collections.ObjectModel;

namespace Vms.Client.Models;

/// <summary>
/// 组织机构树节点基类。
///
/// 为什么要有这一层：业务要求"树里既挂组织，也挂车辆"。
/// 若让 <see cref="VehicleNode"/> 再复制一份 Children/Parent/IsExpanded，
/// 树的两个分支就会各写一套展开与遍历逻辑，早晚不同步。
/// 抽成基类后 TreeView 只认一种节点类型，模板按 <see cref="IsVehicle"/> 分派即可。
/// </summary>
public abstract class OrgNodeBase : ObservableObject
{
    private bool _isExpanded;

    public required uint Id { get; init; }

    /// <summary>公司节点=组织名称；车辆节点=车牌号（直接显示在树上，不再额外开一列）。</summary>
    public required string Name { get; init; }

    public ObservableCollection<OrgNodeBase> Children { get; } = [];

    public OrgNodeBase? Parent { get; internal set; }

    public bool IsExpanded
    {
        get => _isExpanded;
        set => SetProperty(ref _isExpanded, value);
    }

    /// <summary>车辆节点是叶子；公司节点可继续展开。</summary>
    public abstract bool IsVehicle { get; }

    /// <summary>
    /// 是否应在 TreeView 上显示"向下展开箭头"。
    /// 组织节点恒为 true —— 要么有下级组织、要么（叶子组织）展开后挂车辆，
    /// 都应有箭头提示可展开；车辆叶子节点为 false。
    /// 绑定到 <c>TreeViewItem.HasUnrealizedChildren</c>，解决"有车辆的叶子组织默认不显示箭头"的问题。
    /// </summary>
    public bool ShowExpandArrow => !IsVehicle;

    /// <summary>状态点/文字的颜色语义：在线蓝、离线灰、报警红。</summary>
    public virtual VehicleState State => VehicleState.Offline;

    // —— 三态复选（经典树选择）——
    // 组织节点是<b>三态</b> bool?：true=全选 / false=全不选 / null=部分选择（子节点有勾有空）。
    // 车辆叶子节点是两态（只承担 true/false，不会是 null）。
    //
    // 联动规则（需求：勾组织→下级组织及车辆全选；取消某下级→该级及车辆取消、父级变部分选择）：
    //   · <see cref="IsChecked"/> setter 是"纯状态"，<b>不级联</b>；级联与回溯由下面两个方法显式完成，
    //     避免任何一处赋值都触发 O(n²) 递归（大组织树 3 万节点会白烧 CPU）。
    //   · <see cref="SetSubtreeChecked"/>：把本级及其整棵子树置成确定值（用于"勾/取消整支"）。
    //   · <see cref="RecomputeAncestors"/>：沿父链向上，按子节点实际勾选态重算每个父组织
    //     （全 true→true；全 false→false；混合或含 null→null），这是"父级变部分选择"的来源。
    private bool? _isChecked = false;

    /// <summary>三态勾选态。组织节点可为 null（部分选择）；车辆叶子恒为 true/false。
    /// 默认 false（未选）—— 否则 bool? 的 null 会被 IsThreeState=True 的复选框渲染成"半选框"，
    /// 导致一启动所有组织都显示成部分选择。</summary>
    public bool? IsChecked
    {
        get => _isChecked;
        set => SetProperty(ref _isChecked, value);
    }

    /// <summary>
    /// 把<b>本级及整棵子树</b>置成确定勾选态（不回溯父级）。
    /// 组织节点会递归到所有下级组织与车辆；车辆叶子只置自身。
    /// 用于"勾选/取消一整支"这类批量操作 —— 一次递归到位，不重复触发级联。
    /// </summary>
    public virtual void SetSubtreeChecked(bool value) => IsChecked = value;

    /// <summary>
    /// 沿父链向上重算每个组织节点的勾选态（子节点推导父节点）。
    /// 这是"取消某下级组织后，当前组织变为部分选择"的关键：被改的节点只动了自己/子树，
    /// 必须由父级重新读一遍子节点来得出 null/true/false。
    /// </summary>
    public void RecomputeAncestors()
    {
        for (var p = Parent as CompanyNode; p is not null; p = p.Parent as CompanyNode)
            p.RefreshCheckStateFromChildren();
    }

    public void AddChild(OrgNodeBase child)
    {
        child.Parent = this;
        Children.Add(child);
    }

    public IEnumerable<OrgNodeBase> DescendantsAndSelf()
    {
        yield return this;
        foreach (var c in Children)
            foreach (var d in c.DescendantsAndSelf())
                yield return d;
    }
}

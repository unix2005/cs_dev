using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Vms.Client.ViewModels;

namespace Vms.Client.Controls;

/// <summary>
/// 导航项模板分派：
///   · 普通模块（重点关注 / 行驶记录 …）用 <see cref="SimpleTemplate"/>（一级按钮 + 二级项）；
///   · 「实时监控」用 <see cref="MonitorTemplate"/> —— 展开后除二级项外，还会内嵌组织/车辆树。
///
/// 为什么不用单个模板 + 条件可见性：那样每个导航项都会实例化一棵 TreeView（共 6 棵），
/// 只有「实时监控」那棵可见，其余 5 棵白白占着内存与初始化开销。
/// 用选择器后，整条导航里只有 1 棵 TreeView。
/// </summary>
public sealed class NavItemTemplateSelector : DataTemplateSelector
{
    public DataTemplate? SimpleTemplate { get; set; }
    public DataTemplate? MonitorTemplate { get; set; }

    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
        => item is NavItem { PageTag: "monitor" } ? MonitorTemplate : SimpleTemplate;
}

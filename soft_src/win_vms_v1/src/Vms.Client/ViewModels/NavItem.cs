using System.Collections.ObjectModel;
using Vms.Client.Models;

namespace Vms.Client.ViewModels;

/// <summary>
/// 左侧导航项（实时监控 / 重点关注 / 行驶记录 …）。
///
/// 两个"选中"状态必须分开，否则点二级项会把一级项的高亮和展开一起弄丢：
///   · <see cref="IsSelected"/> —— 该**模块**是当前活动模块（一级项高亮 + 展开二级列表）；
///   · <see cref="IsCurrent"/>  —— 该**条目**正是当前路由（二级项用强调色文字 + 浅底）。
/// </summary>
public sealed class NavItem : ObservableObject
{
    private bool _isSelected;
    private bool _isExpanded;
    private bool _isCurrent;

    public required string Title { get; init; }

    /// <summary>Segoe Fluent Icons 字形。</summary>
    public required string Glyph { get; init; }

    /// <summary>该导航项对应要显示的页面；二级项留空表示复用一级页面。</summary>
    public required string PageTag { get; init; }

    public ObservableCollection<NavItem> Children { get; } = [];

    public bool IsSelected { get => _isSelected; set => SetProperty(ref _isSelected, value); }

    public bool IsExpanded
    {
        get => _isExpanded;
        set
        {
            if (!SetProperty(ref _isExpanded, value)) return;
            OnPropertyChanged(nameof(HasChildrenAndCollapsed));
        }
    }

    public bool IsCurrent
    {
        get => _isCurrent;
        set
        {
            if (!SetProperty(ref _isCurrent, value)) return;
            OnPropertyChanged(nameof(HasChildrenAndCollapsed));
        }
    }

    public bool HasChildren => Children.Count > 0;

    /// <summary>有二级项但当前是收起的 —— 这时显示向右的箭头。</summary>
    public bool HasChildrenAndCollapsed => HasChildren && !_isExpanded;

    public void AddChild(NavItem child) => Children.Add(child);
}

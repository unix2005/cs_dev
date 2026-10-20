using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Vms.Client.ViewModels;

namespace Vms.Client.Controls;

public sealed class NavigationRequestEventArgs(NavItem item) : EventArgs
{
    public NavItem Item { get; } = item;
}

public sealed partial class NavRail : UserControl
{
    public NavRail()
    {
        ViewModel = ((App)Application.Current).ViewModel;
        InitializeComponent();
    }

    public MainViewModel ViewModel { get; }

    public event EventHandler<NavigationRequestEventArgs>? NavigationRequested;

    private void NavItem_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.Tag is not NavItem item) return;

        // 一级项的展开状态跟着"当前模块"走；二级项的点击不能把一级项的展开状态带塌，
        // 所以先定位它归属的一级项（这也顺带支持"二级项归到模块下但路由不同"的情况）。
        NavItem? owner = ViewModel.NavItems.FirstOrDefault(
            top => ReferenceEquals(top, item) || top.Children.Contains(item));

        // 已选中且已展开的一级项，再次点击 → 折叠（再点一次重新展开）。
        // 这让「实时监控」的箭头/标题可以当展开开关用：点一下出树，再点一下收起。
        // （二级项点击不会命中此分支，因为它们归属于一级项、与 owner 不是同一引用。）
        if (ReferenceEquals(item, owner) && owner is { IsSelected: true, IsExpanded: true })
        {
            owner.IsExpanded = false;
            return;
        }

        foreach (var top in ViewModel.NavItems)
        {
            top.IsSelected = ReferenceEquals(top, owner);
            top.IsExpanded = ReferenceEquals(top, owner);
            foreach (var sub in top.Children) sub.IsCurrent = ReferenceEquals(sub, item);
        }

        if (owner is null) item.IsSelected = true;   // 兜底：不在 NavItems 里的项
        item.IsCurrent = true;

        ViewModel.SelectedNav = item;
        NavigationRequested?.Invoke(this, new NavigationRequestEventArgs(item));
    }
}

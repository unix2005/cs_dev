using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Vms.Client.ViewModels;

namespace Vms.Client.Controls;

/// <summary>工具条动作事件参数（先用状态栏提示占位，接后端时换成命令）。</summary>
public sealed class ToolBarActionEventArgs(string message) : EventArgs
{
    public string Message { get; } = message;
}

public sealed partial class AppHeader : UserControl
{
    public AppHeader()
    {
        InitializeComponent();
    }

    /// <summary>供 Window.SetTitleBar 使用的拖拽区域。</summary>
    public FrameworkElement TitleBarElement => TitleBarDragRegion;

    public event EventHandler<ToolBarActionEventArgs>? ActionRequested;

    private void Action_Click(object sender, RoutedEventArgs e)
    {
        var tag = (sender as FrameworkElement)?.Tag as string ?? "操作";
        ActionRequested?.Invoke(this, new ToolBarActionEventArgs($"「{tag}」功能待接入后端服务"));
    }

    /// <summary>顶部检索框：把关键字交给当前页的监控视图（过滤组织树 + 数据页签）。</summary>
    private void SearchBox_QuerySubmitted(AutoSuggestBox sender, AutoSuggestBoxQuerySubmittedEventArgs args)
    {
        var keyword = args.QueryText?.Trim() ?? string.Empty;
        if (((App)Application.Current).ViewModel?.Monitor is { } monitor)
        {
            monitor.SearchKeyword = keyword;
            _ = monitor.ReloadAsync();
        }
    }
}

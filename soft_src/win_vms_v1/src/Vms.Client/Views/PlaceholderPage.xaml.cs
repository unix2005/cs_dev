using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace Vms.Client.Views;

/// <summary>未实现模块的占位页：保留导航与版式，避免出现"点了没反应"。</summary>
public sealed partial class PlaceholderPage : Page
{
    public PlaceholderPage()
    {
        InitializeComponent();
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);
        if (e.Parameter is string title && !string.IsNullOrWhiteSpace(title))
            TitleText.Text = title;
    }
}

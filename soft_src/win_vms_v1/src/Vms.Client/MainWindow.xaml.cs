using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Vms.Client.Controls;
using Vms.Client.ViewModels;
using Vms.Client.Views;
using Windows.Graphics;

namespace Vms.Client;

public sealed partial class MainWindow : Window
{
    public MainViewModel ViewModel { get; }

    /// <summary>
    /// 缓存「实时监控」页面实例。导航来回切换时复用同一个实例，
    /// 这样里面的 WebView2（高德地图）不会被销毁重建 —— 否则每次切回监控页地图都要重新加载，
    /// 既浪费网络又把"一直在更新的实时数据"断一下。
    /// 页面实例随主窗口生命周期存在，不会被 Frame 导航回收。
    /// </summary>
    private RealtimeMonitorPage? _monitorPage;

    public MainWindow()
    {
        ViewModel = ((App)Application.Current).ViewModel;

        InitializeComponent();

        // Window 根元素不是 FrameworkElement，x:Bind 在 Window 顶层会触发
        // "无法从 Window 转换为 FrameworkElement" 的代码生成错误。
        // 因此主窗口顶层的绑定改用 {Binding}，并把数据上下文挂在 RootGrid 上。
        RootGrid.DataContext = ViewModel;

        Title = "车辆管理系统";
        TryResizeWindow(1560, 950);

        // 自绘标题栏：由我们控制浅色标题条，同时保留系统的最大化/关闭按钮
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(Header.TitleBarElement);

        RootGrid.Loaded += OnRootLoaded;
        Closed += (_, _) => ViewModel.Dispose();
    }

    private async void OnRootLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            // 首次进入用缓存实例，而不是 Frame.Navigate —— 后者每次都 new 一个新页面，
            // 会把里面的 WebView2 地图连同重建，等于"切回监控页地图就重载一次"。
            _monitorPage ??= new RealtimeMonitorPage();
            ContentFrame.Content = _monitorPage;
            await ViewModel.InitializeAsync();

            // 诊断开关 VMS_DIAG=fontcheck：实测字体渲染宽度，验证全局字体确实落到 Noto Sans SC。
            // 放在 Loaded 之后跑，此时可视树与布局都已就绪，量出来的宽度才有意义。
            if (Diagnostics.Diag.FontCheck)
                RootGrid.DispatcherQueue.TryEnqueue(async () => await RunFontCheckAsync());
        }
        catch (Exception ex)
        {
            // 启动期数据加载（组织树/车辆分页/实时订阅）失败不应带着窗口一起崩：
            // 界面照常显示，错误进状态栏 + 调试输出，便于定位。
            ViewModel.StatusMessage = $"初始化失败：{ex.Message}";
            System.Diagnostics.Debug.WriteLine($"[MainWindow] init failed: {ex}");
        }
    }

    private void TryResizeWindow(int width, int height)
    {
        try
        {
            AppWindow.Resize(new SizeInt32(width, height));
        }
        catch (Exception ex)
        {
            // 某些远程桌面/小屏环境下窗口尺寸可能被拒绝，不影响功能
            System.Diagnostics.Debug.WriteLine($"[MainWindow] resize skipped: {ex.Message}");
        }
    }

    /// <summary>
    /// <c>VMS_DIAG=fontcheck</c>：实测"全局字体到底渲染成了哪一款"。
    ///
    /// 为什么要实测而不是读代码确认：字体回退是<b>运行期</b>行为 ——
    /// <c>FontFamily</c> 里写了 <c>Noto Sans SC, Microsoft YaHei UI</c>，
    /// 到底选中哪一款取决于系统装没装、DirectWrite 怎么匹配，编译期和代码审查都看不出来。
    ///
    /// 办法：用同一串中英文分别按不同 <c>FontFamily</c> 渲染，量 <c>ActualWidth</b>。
    /// 不同字体的字形宽度不同 → 宽度不同就说明真的换了字体。
    /// 判定：<c>AppFontFamily</c> 的宽度与 <c>Noto Sans SC</c> 一致、且与 <c>Microsoft YaHei UI</c> 不同
    /// → 全局字体确实落在 Noto Sans SC 上。结果写 <c>%LocalAppData%\Vms.Client\fontcheck.log</c>。
    /// </summary>
    private async Task RunFontCheckAsync()
    {
        try
        {
            const string sample = "车辆管理系统 VMS 0123456789";
            var host = new Grid { Width = 2000, Height = 200, Opacity = 0 };
            RootGrid.Children.Add(host);

            double Measure(string family)
            {
                var tb = new TextBlock
                {
                    Text = sample,
                    FontSize = 20,
                    FontFamily = new FontFamily(family),
                };
                host.Children.Add(tb);
                tb.Measure(new Windows.Foundation.Size(2000, 200));
                var w = tb.DesiredSize.Width;
                host.Children.Remove(tb);
                return w;
            }

            var appFont = (Application.Current.Resources["AppFontFamily"] as FontFamily)?.Source ?? "(未定义)";
            var wApp = Measure(appFont);
            var wNoto = Measure("Noto Sans SC");
            var wYahei = Measure("Microsoft YaHei UI");

            RootGrid.Children.Remove(host);

            bool notoInstalled = Math.Abs(wNoto - wYahei) > 0.01;   // 两者宽度不同 → 都可用且确实不同
            bool appIsNoto = Math.Abs(wApp - wNoto) < 0.01;
            bool appIsYahei = Math.Abs(wApp - wYahei) < 0.01;
            string verdict = appIsNoto
                ? "PASS 全局字体 = Noto Sans SC（思源黑体）"
                : appIsYahei
                    ? "FAIL 全局字体回退成了 Microsoft YaHei UI（本机未装 Noto Sans SC，属预期回退）"
                    : "WARN 全局字体与两者宽度都不一致，需人工确认";

            var sb = new System.Text.StringBuilder();
            sb.AppendLine($"采样文本: \"{sample}\" 字号 20");
            sb.AppendLine($"AppFontFamily 资源值 = \"{appFont}\"");
            sb.AppendLine($"  Measure(AppFontFamily)      = {wApp:F3}");
            sb.AppendLine($"  Measure(Noto Sans SC)       = {wNoto:F3}");
            sb.AppendLine($"  Measure(Microsoft YaHei UI) = {wYahei:F3}");
            sb.AppendLine($"本机 Noto 与雅黑渲染宽度不同 = {notoInstalled}（false 说明两者之一缺失）");
            sb.AppendLine(verdict);

            var dir = System.IO.Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Vms.Client");
            System.IO.Directory.CreateDirectory(dir);
            await System.IO.File.WriteAllTextAsync(
                System.IO.Path.Combine(dir, "fontcheck.log"),
                $"{DateTime.Now:yyyy-MM-dd HH:mm:ss}\n{sb}\n");
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[FontCheck] 失败：{ex}");
        }
    }

    private void Header_ActionRequested(object? sender, ToolBarActionEventArgs e)
    {
        // 工具条按钮目前统一回落到状态栏提示；接后端时把对应命令挂到这里即可
        ViewModel.StatusMessage = e.Message;
    }

    private void Nav_NavigationRequested(object? sender, NavigationRequestEventArgs e)
    {
        ViewModel.SelectedNav = e.Item;

        if (e.Item.PageTag == "monitor")
        {
            // 复用同一个实例：地图（WebView2）一直活着，来回切换不重载；
            // 实时数据也一直在更新（页面卸载时不再停用写入，见 RealtimeMonitorPage）。
            if (ContentFrame.Content is not RealtimeMonitorPage)
            {
                _monitorPage ??= new RealtimeMonitorPage();
                ContentFrame.Content = _monitorPage;
            }
        }
        else
        {
            ContentFrame.Navigate(typeof(PlaceholderPage), e.Item.Title);
        }
    }
}

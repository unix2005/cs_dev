using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Vms.Client.ViewModels;

namespace Vms.Client;

public partial class App : Application
{
    private Window? _window;

    /// <summary>
    /// 应用级视图模型。整个进程只有一条 native 连接 / 一份组织树 / 一个车辆窗口，
    /// 由 App 统一持有，主窗口与各页面共享 —— 页面不再各自 new 一份数据源。
    /// （单窗口桌面客户端用这个方式比引入 DI 容器更直接，也不会踩到
    ///   Frame 导航时"页面无参构造 → x:Bind 拿到 null ViewModel"的经典坑。）
    /// </summary>
    public MainViewModel ViewModel { get; }

    public App()
    {
        ViewModel = new MainViewModel(DispatcherQueue.GetForCurrentThread());

        // 桌面端最忌讳"界面还在、逻辑已经死了"：未处理异常必须留痕 + 弹窗，方便现场排查。
        // 关键：必须 e.Handled = true，否则 WinUI 在处理器返回后仍会终止进程 ——
        // 这正是"窗口闪一下就没了"的根因。
        UnhandledException += OnAppUnhandledException;
        AppDomain.CurrentDomain.UnhandledException += OnDomainUnhandledException;

        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _window = new MainWindow();
        _window.Activate();
    }

    // ----------------------------- 异常处理 -----------------------------

    private void OnAppUnhandledException(object sender, Microsoft.UI.Xaml.UnhandledExceptionEventArgs e)
    {
        e.Handled = true;
        ReportFatal(e.Exception, "UI 线程未处理异常");
    }

    private void OnDomainUnhandledException(object? sender, System.UnhandledExceptionEventArgs e)
    {
        // 非 UI 线程（如 Task.Run 里漏 await 的异常）也会走到这里；
        // 这种多半是真实 bug，仅记录，不强行 Handled（避免掩盖损坏状态）。
        var ex = e.ExceptionObject as Exception ?? new Exception(e.ExceptionObject?.ToString());
        ReportFatal(ex, "非 UI 线程未处理异常");
    }

    /// <summary>
    /// 统一的致命错误出口：写日志文件（便于事后排查）+ 弹 Win32 对话框（不依赖 XAML 根，
    /// 即使窗口还没建好也能弹出来，避免"只在调试器里可见"的静默崩溃）。
    /// </summary>
    private static void ReportFatal(Exception ex, string context)
    {
        try
        {
            var dir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Vms.Client");
            Directory.CreateDirectory(dir);
            var log = Path.Combine(dir, "crash.log");
            File.AppendAllText(log,
                $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {context}\n{ex}\n\n");
        }
        catch
        {
            // 日志写失败也不能再抛，否则会二次崩溃
        }

        try
        {
            MessageBoxW(IntPtr.Zero,
                $"{context}\n\n{ex.GetType().Name}: {ex.Message}\n\n" +
                "（完整堆栈已写入：%LocalAppData%\\Vms.Client\\crash.log）",
                "车辆管理系统 - 未处理异常",
                0x10 /* MB_ICONERROR */);
        }
        catch
        {
        }
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern int MessageBoxW(IntPtr hWnd, string text, string caption, uint type);
}

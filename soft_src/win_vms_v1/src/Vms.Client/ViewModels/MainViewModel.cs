using System.Collections.ObjectModel;
using Microsoft.UI.Dispatching;
using Vms.Client.Models;
using Vms.Client.Services;

namespace Vms.Client.ViewModels;

/// <summary>
/// 主窗口（外壳）的视图模型：导航、全局统计、后端诊断。
/// 数据源由它统一持有，子页面共用同一份连接 —— 避免每个页面各开一条 C 库连接。
/// </summary>
public sealed class MainViewModel : ObservableObject, IDisposable
{
    private readonly VehicleDataSourceBundle _bundle;
    private readonly DispatcherQueueTimer? _statsTimer;

    private NavItem? _selectedNav;
    private SystemStats _stats = SystemStats.Empty;
    private string _statusMessage = "欢迎访问车辆管理系统！";
    private string _backendBadge = "未初始化";
    private bool _usingNativeBackend;
    private SessionStatus? _sessionState;

    public MainViewModel(DispatcherQueue? uiQueue)
    {
        _bundle = VehicleDataSourceFactory.Create(uiQueue);
        try
        {
            var dir = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Vms.Client");
            System.IO.Directory.CreateDirectory(dir);
            System.IO.File.AppendAllText(System.IO.Path.Combine(dir, "trace.log"), $"[{DateTime.Now:HH:mm:ss.fff}] probe: success={_bundle.Probe.Success} msg={_bundle.Probe.Message}\n");
        }
        catch { }
        BackendBadge = _bundle.Source.BackendName;
        UsingNativeBackend = _bundle.Source.IsNative;
        StatusMessage = _bundle.Probe.Success
            ? $"native 核心库已就绪 · {_bundle.Probe.Message}"
            : $"已降级为内置模拟数据 · {_bundle.Probe.Message}";

        Monitor = new RealtimeMonitorViewModel(_bundle.Source, uiQueue);

        // M4：订阅会话状态（在线 / 重连中 / 票据失效重登 / 已断开）。
        // 回调由 native 心跳线程发出，VmsCoreClient 已编组回 UI 线程，这里只做赋值。
        if (_bundle.Source is NativeVehicleDataSource nativeSource)
            nativeSource.SessionStateChanged += (_, s) => SessionState = s;

        BuildNavigation();

        if (uiQueue is not null)
        {
            _statsTimer = uiQueue.CreateTimer();
            _statsTimer.Interval = TimeSpan.FromSeconds(3);
            _statsTimer.Tick += async (_, _) => await RefreshStatsAsync();
            _statsTimer.Start();
        }
    }

    // ------------------------------- 绑定属性 -------------------------------

    public ObservableCollection<NavItem> NavItems { get; } = [];

    public RealtimeMonitorViewModel Monitor { get; }

    public IVehicleDataSource DataSource => _bundle.Source;

    public NavItem? SelectedNav
    {
        get => _selectedNav;
        set
        {
            if (!SetProperty(ref _selectedNav, value)) return;
            OnPropertyChanged(nameof(IsMonitorActive));
        }
    }

    /// <summary>
    /// 当前是否停在「实时监控」模块。
    /// 组织/车辆树是监控模块的一部分（挂在左侧该模块下），切到别的模块就不该再占着左侧版面。
    /// </summary>
    public bool IsMonitorActive => _selectedNav?.PageTag == "monitor";

    public SystemStats Stats
    {
        get => _stats;
        private set
        {
            if (!SetProperty(ref _stats, value)) return;
            OnPropertyChanged(nameof(AlarmCountText));
        }
    }

    /// <summary>对应截图右侧「未处理报警数量：46343 条」。</summary>
    public string AlarmCountText => $"未处理报警数量：{_stats.AlarmUnhandled:N0} 条";

    public string StatusMessage { get => _statusMessage; set => SetProperty(ref _statusMessage, value); }

    public string BackendBadge { get => _backendBadge; private set => SetProperty(ref _backendBadge, value); }

    public bool UsingNativeBackend { get => _usingNativeBackend; private set => SetProperty(ref _usingNativeBackend, value); }

    /// <summary>M4：会话治理状态（null = 未登录 / 无会话治理）。</summary>
    public SessionStatus? SessionState
    {
        get => _sessionState;
        private set
        {
            if (!SetProperty(ref _sessionState, value)) return;
            OnPropertyChanged(nameof(SessionText));
            OnPropertyChanged(nameof(SessionIsDown));
        }
    }

    /// <summary>状态栏用的会话文案（"在线" / "连接中断，正在重连" / "已断开"）。</summary>
    public string SessionText => _sessionState?.StateText ?? "未连接";

    /// <summary>会话是否处于异常态（重连中 / 重登中 / 已断开）—— 供状态栏变色。</summary>
    public bool SessionIsDown => _sessionState?.IsDown ?? false;

    /// <summary>native 自检细节（状态栏 tooltip / 诊断面板用）。</summary>
    public string NativeDiagnostics => _bundle.Probe.Success
        ? $"{_bundle.Probe.Message}\nDLL: {_bundle.Probe.DllPath}"
        : _bundle.Probe.Message;

    // ------------------------------- 导航结构 -------------------------------

    private void BuildNavigation()
    {
        // 与现有系统一致的一级模块；只有「实时监控」有真实页面，其余走占位页
        var items = new List<NavItem>
        {
            new() { Title = "实时监控", Glyph = "\uE9D9", PageTag = "monitor", IsSelected = true, IsExpanded = true, IsCurrent = true },
            new() { Title = "重点关注", Glyph = "\uE734", PageTag = "placeholder" },
            new() { Title = "行驶记录", Glyph = "\uE81C", PageTag = "placeholder" },
            new() { Title = "监控管理", Glyph = "\uE7F4", PageTag = "placeholder" },
            new() { Title = "违规报警", Glyph = "\uE7BA", PageTag = "placeholder" },
            new() { Title = "指令交互", Glyph = "\uE8C8", PageTag = "placeholder" },
        };

        items[1].Children.Add(new NavItem { Title = "关注车辆", Glyph = "\uE734", PageTag = "placeholder" });
        items[1].Children.Add(new NavItem { Title = "关注分组", Glyph = "\uE8B7", PageTag = "placeholder" });
        items[2].Children.Add(new NavItem { Title = "单车查询", Glyph = "\uE81C", PageTag = "placeholder" });
        items[2].Children.Add(new NavItem { Title = "轨迹回放", Glyph = "\uE7C4", PageTag = "placeholder" });
        items[3].Children.Add(new NavItem { Title = "车辆管理", Glyph = "\uE7F4", PageTag = "placeholder" });
        items[3].Children.Add(new NavItem { Title = "分组管理", Glyph = "\uE8B7", PageTag = "placeholder" });
        items[4].Children.Add(new NavItem { Title = "报警查询", Glyph = "\uE7BA", PageTag = "placeholder" });
        items[4].Children.Add(new NavItem { Title = "报警处理", Glyph = "\uE8D4", PageTag = "placeholder" });
        items[5].Children.Add(new NavItem { Title = "下发指令", Glyph = "\uE8C8", PageTag = "placeholder" });
        items[5].Children.Add(new NavItem { Title = "指令记录", Glyph = "\uE81C", PageTag = "placeholder" });

        foreach (var item in items) NavItems.Add(item);
        SelectedNav = items[0];
    }

    // ------------------------------- 生命周期 --------------------------------

    public async Task InitializeAsync(CancellationToken ct = default)
    {
        // 实时推送订阅失败（如 native vms_subscribe 返回错误）不应阻断整个启动：
        // 退化为"无实时刷新"，界面其余部分照常工作。
        try
        {
            _bundle.Source.StartRealtime();
        }
        catch (Exception ex)
        {
            StatusMessage = $"实时推送启动失败，已降级为静态数据：{ex.Message}";
        }

        // M2：环境变量齐备则尝试真实登录（失败不阻断启动，状态栏如实说明"仍是模拟数据"）。
        // 必须在 RefreshStatsAsync 之前：登录成功后 vms_get_stats 才会真正走后台。
        await TryAutoLoginAsync(ct);

        await Monitor.InitializeAsync(ct);
        await RefreshStatsAsync(ct);
    }

    /// <summary>
    /// 自动登录：凭据来自 <b>XML 配置文件</b>（见 VehicleDataSourceFactory.TryCreateLoginOptionsFromConfig）。
    /// 定位仍是**联调通道** —— 让"状态栏显示真实数据"无需 UI 改造即可端到端验证；
    /// 正式的登录对话框 + 凭据保管在 M5 做。
    ///
    /// 配置里没有口令时**不**尝试登录：口令不落盘是合规红线，宁可停在模拟数据，
    /// 也不偷偷用空口令去撞后台（那会在服务端留下一串失败审计）。
    /// </summary>
    private async Task TryAutoLoginAsync(CancellationToken ct)
    {
        void Trace(string msg)
        {
            try
            {
                var dir = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Vms.Client");
                System.IO.Directory.CreateDirectory(dir);
                System.IO.File.AppendAllText(System.IO.Path.Combine(dir, "trace.log"), $"[{DateTime.Now:HH:mm:ss.fff}] {msg}\n");
            }
            catch { }
        }

        Trace("auto-login: begin");
        var (opt, err) = VehicleDataSourceFactory.TryCreateLoginOptionsFromConfig();
        Trace($"auto-login: config opt={(opt == null ? "null" : "ok")} err={err}");
        if (opt is null)
        {
            StatusMessage = err is null
                ? "未配置登录参数，显示内置模拟数据"
                : $"未登录（{err}），显示内置模拟数据";
            return;
        }
        if (_bundle.Source is not NativeVehicleDataSource native)
        {
            Trace("auto-login: source not Native, skip");
            return;
        }

        try
        {
            Trace("auto-login: before LoginAsync");
            var r = await native.LoginAsync(opt, ct);
            Trace($"auto-login: after LoginAsync success={r.Success} msg={r.Message}");
            if (r.Success)
            {
                BackendBadge = $"{_bundle.Source.BackendName} · 已登录";
                StatusMessage = $"已连接后台 {opt.Host}:{opt.Port}，状态栏为真实数据";

                Trace("auto-login: before StartPushAsync");
                try
                {
                    await native.StartPushAsync(opt.Password, opt.Otp);
                    Trace("auto-login: after StartPushAsync");
                }
                catch (Exception ex)
                {
                    Trace($"auto-login: StartPushAsync ex: {ex.Message}");
                    StatusMessage = $"已登录，但实时推送连接启动失败（退化为静态刷新）：{ex.Message}";
                }
            }
            else
            {
                StatusMessage = $"登录失败：{r.Message}（仍显示内置模拟数据）";
            }
        }
        catch (Exception ex)
        {
            Trace($"auto-login: LoginAsync ex: {ex}");
            StatusMessage = $"登录异常：{ex.Message}（仍显示内置模拟数据）";
        }
    }

    public async Task RefreshStatsAsync(CancellationToken ct = default)
    {
        try
        {
            Stats = await _bundle.Source.GetStatsAsync(ct);

            // M4：顺带刷新会话状态（只是一次内存快照，无网络往返）。
            // 状态变化本身由事件推送，这里是兜底 —— 避免事件丢帧后状态栏一直停在旧值。
            if (_bundle.Source is NativeVehicleDataSource nds && nds.IsLoggedIn)
                SessionState = nds.GetSessionStatus();
        }
        catch (Exception ex)
        {
            StatusMessage = $"统计刷新失败：{ex.Message}";
        }
    }

    public void Dispose()
    {
        _statsTimer?.Stop();
        Monitor.Dispose();
        _bundle.Source.Dispose();
    }
}

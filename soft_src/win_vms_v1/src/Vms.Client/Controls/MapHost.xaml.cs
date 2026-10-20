using System.Text.Json;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Web.WebView2.Core;
using Vms.Client.Models;

namespace Vms.Client.Controls;

/// <summary>
/// WebView2 版地图宿主（高德地图 JS API 2.0）。
///
/// 本地页面以「虚拟主机名映射」方式加载（SetVirtualHostNameToFolderMapping），
/// 而不是 file://。原因：file:// 下页面属于不透明源，地图 SDK 的域名白名单
/// 与 CORS 都会异常，而且 WebView2 对 file:// 有额外限制。
/// </summary>
public sealed partial class MapHost : UserControl
{
    private const string VirtualHost = "vms.local";

    /// <summary>默认视野（GCJ-02），与现有系统一致。</summary>
    private const double DefaultLon = 113.572906;
    private const double DefaultLat = 42.443101;
    private const int DefaultZoom = 8;

    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };

    private bool _mapReady;
    private bool _initialized;
    private int _zoomFallback = 8;

    public MapHost()
    {
        InitializeComponent();
        Loaded += OnLoaded;

        // 页面被 Frame 卸载（切到别的模块）后，WebView2 与 XAML 控件随之销毁。
        // 此时若还有推送往里 Post，就是在给已经不存在的对象发消息 ——
        // 轻则异常，重则在本机层访问违例。所以卸载后一律关掉发送通道。
        Unloaded += (_, _) => _mapReady = false;
    }

    // --------------------------- 依赖属性（供页面绑定） ---------------------------

    public static readonly DependencyProperty HintTextProperty = DependencyProperty.Register(
        nameof(HintText), typeof(string), typeof(MapHost), new PropertyMetadata("地图加载中…"));

    public static readonly DependencyProperty PointerTextProperty = DependencyProperty.Register(
        nameof(PointerText), typeof(string), typeof(MapHost), new PropertyMetadata("经度:-- 纬度:--"));

    public string HintText
    {
        get => (string)GetValue(HintTextProperty);
        set => SetValue(HintTextProperty, value);
    }

    /// <summary>鼠标所在位置经纬度读数。</summary>
    public string PointerText
    {
        get => (string)GetValue(PointerTextProperty);
        set => SetValue(PointerTextProperty, value);
    }

    /// <summary>地图/宿主内部状态变化（由页面转交给 ViewModel 展示，避免双向绑定互相覆盖）。</summary>
    public event EventHandler<string>? StatusChanged;

    /// <summary>地图就绪：页面收到后可以补推一次打点数据。</summary>
    public event EventHandler? MapReady;

    /// <summary>点击地图上的车辆图钉，回传车辆 ID（字符串形式，便于与 native 的 uint 对齐）。</summary>
    public event EventHandler<string>? MarkerClicked;

    private void RaiseStatus(string text)
    {
        HintText = text;
        StatusChanged?.Invoke(this, text);
    }

    // ------------------------------- 初始化 -------------------------------

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        // 页面来回切换会重复触发 Loaded；WebView2 环境创建两次既慢又容易留下
        // 半初始化状态，所以只初始化一次（控件实例本身就是随页面重建的）。
        if (_initialized) return;
        _initialized = true;
        await TryInitializeMapAsync();
    }

    private async Task TryInitializeMapAsync()
    {
        if (Diagnostics.Diag.SkipMap)
        {
            ShowFallback("诊断模式（VMS_DIAG=nomap）：地图已跳过");
            return;
        }

        var webAssets = Path.Combine(AppContext.BaseDirectory, "WebAssets");
        var mapPage = Path.Combine(webAssets, "map.html");
        if (!File.Exists(mapPage))
        {
            ShowFallback($"未找到地图页面：{mapPage}（请确认 WebAssets 已随程序输出）");
            return;
        }

        try
        {
            // WebView2 运行时缺失时这里会抛，先探测再创建，避免半初始化状态
            _ = CoreWebView2Environment.GetAvailableBrowserVersionString();

            // 未打包应用必须显式指定用户数据目录，否则会在 exe 目录里写缓存（可能无写权限）
            var userData = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Vms.Client", "WebView2");
            Directory.CreateDirectory(userData);

            var env = await CoreWebView2Environment.CreateWithOptionsAsync(null, userData, new CoreWebView2EnvironmentOptions());
            await MapView.EnsureCoreWebView2Async(env);

            MapView.CoreWebView2.SetVirtualHostNameToFolderMapping(
                VirtualHost, webAssets, CoreWebView2HostResourceAccessKind.Allow);
            MapView.CoreWebView2.WebMessageReceived += OnWebMessageReceived;

            // WebView2 的渲染进程/GPU 进程可能自己挂掉（高德页面异常、显存不足等）。
            // 不处理的话，宿主仍会继续 Post/读写，容易在 XAML 本机层打出访问违例。
            // 这里收到通知就切成占位层，宁可"地图暂时不可用"也不让整个程序陪葬。
            MapView.CoreWebView2.ProcessFailed += OnWebProcessFailed;

            // 注入外部配置的密钥（必须在 Navigate 之前，首个文档才会执行到）
            var (key, jsCode) = LoadAmapCredentials();
            if (!string.IsNullOrWhiteSpace(key))
            {
                await MapView.CoreWebView2.AddScriptToExecuteOnDocumentCreatedAsync(
                    $"window.__vmsAmapKey={JsonSerializer.Serialize(key)};" +
                    $"window.__vmsAmapJsCode={JsonSerializer.Serialize(jsCode)};");
            }

            MapView.CoreWebView2.Navigate($"https://{VirtualHost}/map.html");

            MapView.Visibility = Visibility.Visible;
            FallbackLayer.Visibility = Visibility.Collapsed;
            _mapReady = true;
        }
        catch (Exception ex)
        {
            ShowFallback($"WebView2 不可用，已降级为占位层：{ex.Message}");
        }
    }

    /// <summary>WebView2 子进程异常退出：同样只做"排队 + 降级"，不在本机回调栈里动 XAML。</summary>
    private void OnWebProcessFailed(CoreWebView2 sender, CoreWebView2ProcessFailedEventArgs args)
    {
        var kind = args.ProcessFailedKind;
        DispatcherQueue.TryEnqueue(DispatcherQueuePriority.Normal, () =>
        {
            _mapReady = false;
            ShowFallback($"WebView2 子进程异常退出（{kind}），地图已停用");
            System.Diagnostics.Debug.WriteLine($"[MapHost] ProcessFailed: {kind} / {args.Reason}");
        });
    }

    private void ShowFallback(string reason)
    {
        _mapReady = false;
        MapView.Visibility = Visibility.Collapsed;
        FallbackLayer.Visibility = Visibility.Visible;
        FallbackReason.Text = reason;
        RaiseStatus("地图未启用（占位模式）");
        System.Diagnostics.Debug.WriteLine($"[MapHost] 降级：{reason}");
    }

    /// <summary>
    /// 读取外部配置的高德密钥。优先级：环境变量 &gt; <c>amap-key.json</c>。
    ///
    /// 为什么不让密钥只写死在 map.html 里：
    ///   · 换 key 要改源码 + 重新编译 + 重新分发，运维很痛；
    ///   · 不同客户/不同部署要用不同 key，写死就只能每个客户发一个版本。
    /// 都没有配时返回 null，页面回退到 map.html 里的内置默认值。
    /// </summary>
    private static (string? Key, string JsCode) LoadAmapCredentials()
    {
        var key = Environment.GetEnvironmentVariable("VMS_AMAP_KEY");
        var jsCode = Environment.GetEnvironmentVariable("VMS_AMAP_JSCODE") ?? string.Empty;

        if (string.IsNullOrWhiteSpace(key))
        {
            // 依次在程序目录和用户数据目录找，方便"绿色部署"和"安装部署"两种形态
            var candidates = new[]
            {
                Path.Combine(AppContext.BaseDirectory, "amap-key.json"),
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                             "Vms.Client", "amap-key.json"),
            };

            foreach (var file in candidates)
            {
                if (!File.Exists(file)) continue;
                try
                {
                    using var doc = JsonDocument.Parse(File.ReadAllText(file));
                    var root = doc.RootElement;
                    if (root.TryGetProperty("key", out var k)) key = k.GetString();
                    if (root.TryGetProperty("jscode", out var j)) jsCode = j.GetString() ?? jsCode;
                    break;
                }
                catch (Exception ex)
                {
                    System.Diagnostics.Debug.WriteLine($"[MapHost] 读取 {file} 失败：{ex.Message}");
                }
            }
        }

        return (string.IsNullOrWhiteSpace(key) ? null : key.Trim(), jsCode.Trim());
    }

    private readonly List<string> _consoleLog = new(8);

    /// <summary>把地图页面里 SDK 打印的告警/错误累积显示，便于现场定位授权失败原因。</summary>
    private void AppendConsoleLog(string text)
    {
        _consoleLog.Add(text);
        if (_consoleLog.Count > 4) _consoleLog.RemoveAt(0);

        ConsoleLog.Visibility = Visibility.Visible;
        ConsoleLog.Text = "SDK 日志：" + string.Join(" ｜ ", _consoleLog);
        System.Diagnostics.Debug.WriteLine($"[MapHost/JS] {text}");
    }

    // ---------------------------- 宿主 → JS ----------------------------

    /// <summary>批量更新车辆打点。仅在已加载的页数据上调用，避免把 3 万点全塞给 JS。</summary>
    public void UpdateMarkers(IReadOnlyList<VehicleRow> rows)
    {
        if (!_mapReady || rows.Count == 0) return;

        var payload = new
        {
            type = "vehicles",
            items = rows.Select(r => new
            {
                id = r.Id,
                plate = r.Plate,
                lon = r.Lon,
                lat = r.Lat,
                speed = r.SpeedText,
                online = r.Online,
                alarm = r.Alarm != AlarmFlags.None,
                location = r.Location,
            }).ToArray(),
        };

        Post(payload);
    }

    /// <summary>把地图视野移动到指定坐标（选中车辆 / 组织定位时用）。</summary>
    public void CenterOn(double lon, double lat, int zoom = 14)
    {
        if (!_mapReady) return;
        Post(new { type = "center", lon, lat, zoom });
    }

    /// <summary>清空所有打点（切换过滤范围前调用，避免旧点残留造成"车没走"的错觉）。</summary>
    public void ClearMarkers()
    {
        if (!_mapReady) return;
        Post(new { type = "clear" });
    }

    private void Post(object payload)
    {
        try
        {
            MapView.CoreWebView2.PostWebMessageAsJson(JsonSerializer.Serialize(payload, JsonOptions));
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[MapHost] postMessage 失败：{ex.Message}");
        }
    }

    // ---------------------------- JS → 宿主 ----------------------------

    /// <summary>
    /// WebView2 的消息回调。
    ///
    /// ⚠ 这是**最容易踩的一个坑**：本回调是 WebView2 通过 COM 事件在**本机栈上同步回调**托管代码的，
    ///   此刻 WebView2（以及它所在的 XAML 视觉树）正处在"正在派发消息"的中间状态。
    ///   在这里直接改 XAML（ConsoleLog.Text / PointerText / FallbackLayer.Visibility）、
    ///   或者触发订阅者去改集合，都会把 XAML 本机层的状态搅乱 ——
    ///   结果就是 Microsoft.UI.Xaml.Controls.dll 里的 0xC0000005，进程直接消失。
    ///
    ///   而地图页里的高德 SDK 会**高频**打 console（加载失败/被拒时尤其猛），
    ///   这些日志经 map.html 的钩子全部回传到这里 —— 等于每秒几十次本机回调，
    ///   所以"运行一会儿自己退出"是必然的，不是偶发。
    ///
    ///   正确做法：回调里**只把数据取出来**（字符串），其余一律排队到下一个 UI 节拍。
    /// </summary>
    private void OnWebMessageReceived(CoreWebView2 sender, CoreWebView2WebMessageReceivedEventArgs args)
    {
        string json;
        try
        {
            json = args.WebMessageAsJson;   // 只取值，不碰任何 UI
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[MapHost] 读取 WebMessage 失败：{ex.Message}");
            return;
        }

        DispatcherQueue.TryEnqueue(DispatcherQueuePriority.Normal, () => HandleWebMessage(json));
    }

    /// <summary>日志回传限频：地图页可能一次刷几十条，全量转 UI 没有意义，还会把消息队列堆满。</summary>
    private int _consoleBudget = 8;
    private DateTime _consoleWindowStart = DateTime.UtcNow;

    private bool TryConsumeConsoleBudget()
    {
        var now = DateTime.UtcNow;
        if (now - _consoleWindowStart > TimeSpan.FromSeconds(1))
        {
            _consoleWindowStart = now;
            _consoleBudget = 8;
        }
        if (_consoleBudget <= 0) return false;
        _consoleBudget--;
        return true;
    }

    private void HandleWebMessage(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;
            if (!root.TryGetProperty("type", out var typeProp)) return;

            switch (typeProp.GetString())
            {
                case "ready":
                    RaiseStatus("地图已就绪 · 等待车辆数据");
                    MapReady?.Invoke(this, EventArgs.Empty);
                    break;

                case "pointer":
                    var lon = root.GetProperty("lon").GetString();
                    var lat = root.GetProperty("lat").GetString();
                    PointerText = $"经度:{lon} 纬度:{lat}";
                    break;

                case "markerclick":
                    var id = root.TryGetProperty("id", out var idProp) ? idProp.GetString() : null;
                    if (!string.IsNullOrEmpty(id)) MarkerClicked?.Invoke(this, id!);
                    break;

                case "console":
                    // 高德 SDK 被拒时只往 console 里打原因（INVALID_USER_SCODE 等），
                    // 不转发出来桌面端就只能靠猜。这里收进来直接显示在占位层上。
                    // 限频：SDK 出错时可能每秒刷几百条，全量上屏既没意义又会拖垮 UI。
                    var log = root.TryGetProperty("text", out var t) ? t.GetString() : null;
                    if (!string.IsNullOrEmpty(log) && TryConsumeConsoleBudget()) AppendConsoleLog(log!);
                    break;

                case "error":
                    var stage = root.TryGetProperty("stage", out var st) ? st.GetString() : "unknown";
                    var msg = root.TryGetProperty("message", out var m) ? m.GetString() : "未知错误";
                    ShowFallback($"[{stage}] {msg}");
                    break;
            }
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[MapHost] 解析 WebMessage 失败：{ex.Message}");
        }
    }

    // ------------------------------- 控件动作 -------------------------------

    private void ZoomIn_Click(object sender, RoutedEventArgs e) => Zoom(+1);

    private void ZoomOut_Click(object sender, RoutedEventArgs e) => Zoom(-1);

    private void Locate_Click(object sender, RoutedEventArgs e)
    {
        if (!_mapReady)
        {
            RaiseStatus("占位模式：未接入地图，无法重新定位视野");
            return;
        }
        CenterOn(DefaultLon, DefaultLat, DefaultZoom);
        RaiseStatus("已回到默认视野");
    }

    private void Zoom(int delta)
    {
        if (!_mapReady)
        {
            // 占位模式下也给出反馈，避免"点了没反应"
            _zoomFallback = Math.Clamp(_zoomFallback + delta, 3, 18);
            RaiseStatus($"占位模式：缩放示意 z={_zoomFallback}（接入地图后由地图引擎处理）");
            return;
        }
        Post(new { type = "zoom", delta });
    }
}

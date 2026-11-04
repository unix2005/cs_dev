using System.Collections.Specialized;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Vms.Client.Diagnostics;
using Vms.Client.Models;
using Vms.Client.ViewModels;

namespace Vms.Client.Views;

public sealed partial class RealtimeMonitorPage : Page
{
    public RealtimeMonitorPage()
    {
        ViewModel = ((App)Application.Current).ViewModel;
        InitializeComponent();

        Loaded += OnLoaded;

        // 一次性永久订阅：本页面实例被 MainWindow 缓存复用，整个进程生命周期只构造一次，
        // 所以这里订阅的实时推送 / 地图消息 / 范围变化会一直生效 —— 即使暂时切到别的模块，
        // 数据也继续更新、地图继续打点，切回来时现场是连贯的，而不是"重新加载一次"。
        // （早期版本在 Unloaded 里解绑并停用写入，正好就是"切走再切回地图重载、数据断一下"的根因。）
        Map.StatusChanged += OnMapStatusChanged;
        Map.MarkerClicked += OnMarkerClicked;
        Map.MapReady += OnMapReady;
        ViewModel.DataSource.VehiclesPushed += OnVehiclesPushed;
        ViewModel.Monitor.ScopeChanged += OnScopeChanged;
        // 仅聚焦（点车辆行 / 点图钉）：只居中、不清空其它打点
        ViewModel.Monitor.VehicleFocusRequested += OnVehicleFocusRequested;
        // 双击定位数据行 → 地图聚焦该车
        DataTabs.VehicleFocusRequested += OnPositionRowFocusRequested;

        // 首屏/翻页加载完成 → 补推整页打点（Feed 是 ObservableCollection，加载是异步的，
        // 只靠 Loaded 那一瞬间调用就会在"数据还没回来"时白调一次）
        HookFeed(ViewModel.Monitor.Feed);
    }

    public MainViewModel ViewModel { get; }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        // 页面（重新）进入可视区域：确保数据已填充，并补推一次地图点。
        // 注意：这里不再停用 / 重新订阅数据写入 —— 页面切走期间数据仍在后台更新，
        // 切回来时只是把最新状态刷一次，地图不打断、不重载。
        ViewModel.Monitor.ActivateView();
        PushCurrentPageToMap();
    }

    private void OnMapReady(object? sender, EventArgs e)
    {
        _mapPending.Clear();
        PushCurrentPageToMap();
    }

    // ------------------------------ 实时打点 -------------------------------
    // 推送帧每秒可能来几十上百条（每台车一条），逐条 postMessage 会把 WebView2 的消息通道
    // 打满；而且同一台车在一个刷新周期内会重复改。这里先按车辆主键攒进字典，
    // 每 200ms 合并成一批给地图 —— 用户看到的是"车在走"，不是"消息在刷"。

    private const int MapFlushMs = 200;
    private readonly Dictionary<string, VehicleRow> _mapPending = new(256);
    private DispatcherQueueTimer? _mapTimer;

    private void EnsureMapTimer()
    {
        if (_mapTimer is not null) return;
        _mapTimer = DispatcherQueue.CreateTimer();
        _mapTimer.Interval = TimeSpan.FromMilliseconds(MapFlushMs);
        _mapTimer.IsRepeating = true;
        _mapTimer.Tick += (_, _) => FlushMapPending();
        _mapTimer.Start();
    }

    private void FlushMapPending()
    {
        if (_mapPending.Count == 0) return;
        var rows = _mapPending.Values.ToArray();
        _mapPending.Clear();
        if (Diag.SkipMap) return;
        Map.UpdateMarkers(rows);
    }

    /// <summary>
    /// 推送帧 → 地图打点。
    ///
    /// 关键点：轨迹帧来自 native <c>vms_on_push_traj</c>，只带 vehicle_id / 坐标 / 速度，
    /// <c>id</c> 恒为 0、车牌与位置描述为空。直接拿去打点会让所有车共用一个图钉键并丢车牌，
    /// 所以先经 Feed 按车辆主键解析出"已加载窗口内的同一台车"，把身份字段（id/车牌/自编码）
    /// 与实时字段（坐标/速度/在线/报警）合并成一条再送给地图。
    /// </summary>
    private void OnVehiclesPushed(object? sender, IReadOnlyList<VehicleRow> batch)
    {
        if (Diag.SkipMap) return;

        foreach (var live in batch)
        {
            var baseRow = ViewModel.Monitor.Feed.Resolve(live);
            var marker = MergeForMap(live, baseRow);
            var key = string.IsNullOrEmpty(marker.VehicleId) ? marker.Id.ToString() : marker.VehicleId;
            if (marker.Id == 0 && string.IsNullOrEmpty(marker.VehicleId)) continue;
            _mapPending[key] = marker;
        }

        if (_mapPending.Count > 0) EnsureMapTimer();
    }

    /// <summary>实时字段用推送值，身份/描述字段缺省回落到查询行（推送帧里这些恒为空）。</summary>
    private static VehicleRow MergeForMap(VehicleRow live, VehicleRow? baseRow) => new()
    {
        Id = baseRow?.Id ?? live.Id,
        VehicleId = baseRow?.VehicleId ?? live.VehicleId,
        OrgId = baseRow?.OrgId ?? live.OrgId,
        SelfCode = string.IsNullOrEmpty(live.SelfCode) ? baseRow?.SelfCode ?? string.Empty : live.SelfCode,
        Plate = string.IsNullOrEmpty(live.Plate) ? baseRow?.Plate ?? string.Empty : live.Plate,
        Time = live.Time,
        SpeedKph = live.SpeedKph,
        Location = string.IsNullOrEmpty(live.Location) ? baseRow?.Location ?? string.Empty : live.Location,
        Online = live.Online,
        OfflineSeconds = live.OfflineSeconds,
        Day = live.Day,
        Issue = live.Issue,
        Alarm = live.Alarm,
        Lon = live.Lon,
        Lat = live.Lat,
    };

    private void OnMapStatusChanged(object? sender, string text) => ViewModel.Monitor.MapHint = text;

    /// <summary>
    /// 过滤范围变化 → 地图跟随。
    /// 切范围时先清掉旧打点：否则旧组织的车还留在图上，
    /// 值班人员会误判成"车没被筛掉"，这是监控类界面最常见的错觉之一。
    /// 重新打点时用的是<b>当前 Feed</b>（已经按新范围/勾选重查过），
    /// 所以勾选若干车辆后，地图上只显示这几台，而不是全清空。
    /// </summary>
    /// <summary>
    /// 过滤范围变化 → 只重刷图钉，地图视野不自动平移。
    /// 值班界面被勾选动作反复拽视野是大忌：用户只关心"哪些车上图了"，
    /// 视野的归位由「双击数据行 / 点树上车辆 / 点图钉 / 回默认视野」显式触发。
    /// </summary>
    private void OnScopeChanged(object? sender, EventArgs e)
    {
        // 范围变化时待提交的旧打点也必须丢掉，否则清空后又被写回一批过期位置。
        _mapPending.Clear();
        Map.ClearMarkers();

        HookFeed(ViewModel.Monitor.Feed);
        PushCurrentPageToMap();
    }

    /// <summary>仅聚焦某台车（双击树上车辆行 / 点地图图钉走这条）：只居中，不清空其它打点。
    /// 坐标优先取数据栏里的实时行（推送已刷新到它），树节点坐标只是查询时的静态快照，
    /// 直接用会聚焦到旧位置（曾出现"双击晋L34797、地图跑到津N80851"的错位）。</summary>
    private void OnVehicleFocusRequested(object? sender, VehicleNode vehicle)
    {
        var live = ViewModel.Monitor.Feed.FindByKey(vehicle.VehicleId);
        double lon = live is { Lon: not 0 } ? live.Lon : vehicle.Lon;
        double lat = live is { Lat: not 0 } ? live.Lat : vehicle.Lat;
        Controls.MapHost.Trace($"聚焦 req: 车={vehicle.Plate} key={vehicle.VehicleId} " +
            $"node=({vehicle.Lon:F6},{vehicle.Lat:F6}) live={(live is null ? "null" : $"({live.Lon:F6},{live.Lat:F6})")} " +
            $"feedCount={ViewModel.Monitor.Feed.Count} -> center=({lon:F6},{lat:F6})");
        if (lon == 0 || lat == 0) return;

        Map.CenterOn(lon, lat, FocusZoom);
        ViewModel.Monitor.MapHint = $"已聚焦 {vehicle.Plate}（{lon:F6}, {lat:F6}）";
    }

    /// <summary>数据页签里双击某台车 → 地图聚焦该车当前位置。</summary>
    private void OnPositionRowFocusRequested(object? sender, PositionRecord row)
    {
        if (row.Lon == 0d || row.Lat == 0d) return;
        Map.CenterOn(row.Lon, row.Lat, FocusZoom);
        ViewModel.Monitor.MapHint = $"已聚焦 {row.Plate}（{row.LonText}, {row.LatText}）";
    }

    /// <summary>所有"联动聚焦"统一用的地图缩放档：zoom=12 ≈ 比例尺 2 公里。</summary>
    private const int FocusZoom = 12;

    /// <summary>点击地图上的图钉 → 在树上勾选这台车并联动定位数据栏（同时地图聚焦）。
    /// 图钉回传的 id 是车辆主键字符串（"车牌-颜色"）；查不到时再按查询行 id 兜底。</summary>
    private void OnMarkerClicked(object? sender, string vehicleIdText)
    {
        var all = ViewModel.Monitor.Companies
            .SelectMany(c => c.DescendantsAndSelf())
            .OfType<VehicleNode>();

        VehicleNode? vehicle = null;
        if (!string.IsNullOrEmpty(vehicleIdText))
            vehicle = all.FirstOrDefault(v => string.Equals(v.VehicleId, vehicleIdText, StringComparison.Ordinal));
        if (vehicle is null && uint.TryParse(vehicleIdText, out var id))
            vehicle = all.FirstOrDefault(v => v.Id == id);

        if (vehicle is null)
        {
            ViewModel.Monitor.MapHint = $"车辆「{vehicleIdText}」不在当前展开的组织下，先展开对应组织";
            return;
        }

        // 让树上的复选框也同步勾上，保持 UI 与数据范围一致；并回溯父组织（可能变部分选择）
        vehicle.IsChecked = true;
        vehicle.RecomputeAncestors();
        _ = ViewModel.Monitor.UpdateVehicleSelectionAsync(vehicle, true);
    }

    private void PushCurrentPageToMap()
    {
        if (Diag.SkipMap) return;

        var feed = ViewModel.Monitor.Feed;
        if (feed.Count == 0) return;

        // 只推前 300 个点：地图上肉眼能分辨的标记数量本来就有上限，
        // 全量推送既无意义又把 WebView2 的 postMessage 通道打满。
        Map.UpdateMarkers([.. feed.Take(300)]);
    }

    // -------------------- Feed 加载完成 → 整页打点 --------------------

    private VehicleFeed? _feedHooked;
    private DispatcherQueueTimer? _feedTimer;

    private void HookFeed(VehicleFeed feed)
    {
        if (ReferenceEquals(_feedHooked, feed)) return;
        if (_feedHooked is not null) _feedHooked.CollectionChanged -= OnFeedCollectionChanged;
        _feedHooked = feed;
        feed.CollectionChanged += OnFeedCollectionChanged;
        PushCurrentPageToMap();
    }

    private void OnFeedCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        // 逐条 Add 会触发上百次，这里合并成一次（300ms 静默后统一重推）
        if (e.Action is not (NotifyCollectionChangedAction.Add
            or NotifyCollectionChangedAction.Reset
            or NotifyCollectionChangedAction.Remove)) return;

        if (_feedTimer is null)
        {
            _feedTimer = DispatcherQueue.CreateTimer();
            _feedTimer.Interval = TimeSpan.FromMilliseconds(300);
            _feedTimer.IsRepeating = false;
            _feedTimer.Tick += (_, _) =>
            {
                _feedTimer?.Stop();
                PushCurrentPageToMap();
            };
        }
        _feedTimer.Stop();
        _feedTimer.Start();
    }
}

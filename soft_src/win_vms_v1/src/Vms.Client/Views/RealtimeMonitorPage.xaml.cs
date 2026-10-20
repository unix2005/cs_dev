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

    private void OnMapReady(object? sender, EventArgs e) => PushCurrentPageToMap();

    private void OnVehiclesPushed(object? sender, IReadOnlyList<VehicleRow> batch)
    {
        if (Diag.SkipMap) return;
        Map.UpdateMarkers(batch);
    }

    private void OnMapStatusChanged(object? sender, string text) => ViewModel.Monitor.MapHint = text;

    /// <summary>
    /// 过滤范围变化 → 地图跟随。
    /// 切范围时先清掉旧打点：否则旧组织的车还留在图上，
    /// 值班人员会误判成"车没被筛掉"，这是监控类界面最常见的错觉之一。
    /// 重新打点时用的是<b>当前 Feed</b>（已经按新范围/勾选重查过），
    /// 所以勾选若干车辆后，地图上只显示这几台，而不是全清空。
    /// </summary>
    private void OnScopeChanged(object? sender, EventArgs e)
    {
        Map.ClearMarkers();

        if (ViewModel.Monitor.SelectedVehicle is { Lon: not 0, Lat: not 0 } v)
            Map.CenterOn(v.Lon, v.Lat, 15);

        PushCurrentPageToMap();
    }

    /// <summary>仅聚焦某台车（点树上车辆行 / 点地图图钉走这条）：只居中，不清空其它打点。</summary>
    private void OnVehicleFocusRequested(object? sender, VehicleNode vehicle)
    {
        if (vehicle.Lon != 0 && vehicle.Lat != 0)
            Map.CenterOn(vehicle.Lon, vehicle.Lat, 15);
    }

    /// <summary>点击地图上的图钉 → 在树上勾选这台车并联动定位数据栏（同时地图聚焦）。</summary>
    private void OnMarkerClicked(object? sender, string vehicleIdText)
    {
        if (!uint.TryParse(vehicleIdText, out var id)) return;

        var vehicle = ViewModel.Monitor.Companies
            .SelectMany(c => c.DescendantsAndSelf())
            .OfType<VehicleNode>()
            .FirstOrDefault(v => v.Id == id);

        if (vehicle is null)
        {
            ViewModel.Monitor.MapHint = $"车辆 #{id} 不在当前展开的组织下，先展开对应组织";
            return;
        }

        // 让树上的复选框也同步勾上，保持 UI 与数据范围一致
        vehicle.IsChecked = true;
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
}

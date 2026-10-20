namespace Vms.Client.Models;

/// <summary>
/// 「定位数据」页签的一行。
///
/// ── 字段扩展点 ───────────────────────────────────────────────
/// 新增字段的步骤固定为三步，不需要动 XAML 之外的任何逻辑：
///   ① 在下方「业务字段」区加属性（需要实时刷新就照 <see cref="SpeedKph"/> 写 setter + OnPropertyChanged）；
///   ② 在 <see cref="CopyFrom"/> 里补一行赋值；
///   ③ 在 DataTabsPanel.xaml 的「定位数据」表头与行模板里各加一列（列宽写在 VmsTheme 的 Col* 令牌里）。
/// 若字段来自 native 库，先在 native 查询结构体里加好、再到 NativeModels/数据源映射层透出即可。
///
/// 当前这版字段是**从已有的车辆行派生**的（速度/位置等本来就随实时推送变化），
/// 这样在 native 侧还没有专门的定位记录表之前，页签已经能真跑起来。
/// ────────────────────────────────────────────────────────────
/// </summary>
public sealed class PositionRecord : ObservableObject
{
    private string _plate = string.Empty;
    private string _deviceCode = string.Empty;
    private string _gpsTime = string.Empty;
    private double _lon;
    private double _lat;
    private float _speedKph;
    private int _direction;
    private float _altitude;
    private double _mileageKm;
    private string _locateState = string.Empty;
    private string _address = string.Empty;
    private bool _online;
    private bool _accOn;
    private int _satellites;
    private int _signal;
    private bool _hasAlarm;
    private bool _isChecked;
    private bool _isColumnHighlighted;

    /// <summary>行选择框状态（数据表格最左侧那个勾选框）。<b>不参与任何业务筛选</b>。</summary>
    public bool IsChecked
    {
        get => _isChecked;
        set => SetProperty(ref _isChecked, value);
    }

    /// <summary>
    /// 本行是否处于"列高亮"状态。
    ///
    /// 需求：未勾选行选择框时，左键点某一列 → 该列在<b>所有行</b>里都要高亮成同一颜色，
    /// 让人一眼看出"当前正在操作的是哪一列"。
    /// 所以它挂在<b>行</b>上而不是单元格上：行模型记住"我这一行该显示高亮底色"，
    /// 至于高亮落在第几列，由 DataTabsPanel 统一算好后刷新全部可见行。
    /// </summary>
    public bool IsColumnHighlighted
    {
        get => _isColumnHighlighted;
        set => SetProperty(ref _isColumnHighlighted, value);
    }

    // ------------------------------ 标识 ------------------------------

    /// <summary>车辆 ID（与 <see cref="VehicleRow.Id"/> 同源，用于就地更新）。</summary>
    public uint VehicleId { get; init; }

    /// <summary>行序号（仅用于斑马纹/调试，不参与业务）。</summary>
    public int Seq { get; set; }

    // ---------------------------- 业务字段 ----------------------------

    public string Plate { get => _plate; set => SetProperty(ref _plate, value); }
    public string DeviceCode { get => _deviceCode; set => SetProperty(ref _deviceCode, value); }
    public string GpsTime { get => _gpsTime; set => SetProperty(ref _gpsTime, value); }
    public double Lon { get => _lon; set { if (SetProperty(ref _lon, value)) OnPropertyChanged(nameof(LonText)); } }
    public double Lat { get => _lat; set { if (SetProperty(ref _lat, value)) OnPropertyChanged(nameof(LatText)); } }

    public float SpeedKph
    {
        get => _speedKph;
        set { if (SetProperty(ref _speedKph, value)) OnPropertyChanged(nameof(SpeedText)); }
    }

    public int Direction
    {
        get => _direction;
        set { if (SetProperty(ref _direction, value)) OnPropertyChanged(nameof(DirectionText)); }
    }

    public float Altitude
    {
        get => _altitude;
        set { if (SetProperty(ref _altitude, value)) OnPropertyChanged(nameof(AltitudeText)); }
    }

    public double MileageKm
    {
        get => _mileageKm;
        set { if (SetProperty(ref _mileageKm, value)) OnPropertyChanged(nameof(MileageText)); }
    }

    /// <summary>定位状态：已定位 / 未定位 / 信号弱（对应终端上报的 GPS 状态位）。</summary>
    public string LocateState { get => _locateState; set => SetProperty(ref _locateState, value); }

    /// <summary>逆地理结果（"XX市XX区XX路"）。</summary>
    public string Address { get => _address; set => SetProperty(ref _address, value); }

    public bool Online
    {
        get => _online;
        set
        {
            if (!SetProperty(ref _online, value)) return;
            OnPropertyChanged(nameof(OnlineText));
            OnPropertyChanged(nameof(State));
        }
    }

    /// <summary>ACC 点火状态。离线车判断"是停车熄火还是掉线"的关键字段。</summary>
    public bool AccOn
    {
        get => _accOn;
        set { if (SetProperty(ref _accOn, value)) OnPropertyChanged(nameof(AccText)); }
    }

    public int Satellites
    {
        get => _satellites;
        set { if (SetProperty(ref _satellites, value)) OnPropertyChanged(nameof(SatellitesText)); }
    }

    public int Signal
    {
        get => _signal;
        set { if (SetProperty(ref _signal, value)) OnPropertyChanged(nameof(SignalText)); }
    }

    public bool HasAlarm
    {
        get => _hasAlarm;
        set { if (SetProperty(ref _hasAlarm, value)) OnPropertyChanged(nameof(State)); }
    }

    // ---------------------------- 展示派生 ----------------------------

    public string LonText => _lon == 0 ? "--" : _lon.ToString("F6");
    public string LatText => _lat == 0 ? "--" : _lat.ToString("F6");
    public string SpeedText => _online ? _speedKph.ToString("F1") : "--";
    public string DirectionText => _direction == 0 ? "--" : $"{_direction}°";
    public string AltitudeText => _altitude.ToString("F0");
    public string MileageText => _mileageKm.ToString("F0");
    public string OnlineText => _online ? "在线" : "离线";
    public string AccText => _accOn ? "开" : "关";
    public string SatellitesText => _satellites.ToString();
    public string SignalText => $"{_signal}%";

    public VehicleState State => _hasAlarm ? VehicleState.Alarm : _online ? VehicleState.Online : VehicleState.Offline;

    public void CopyFrom(PositionRecord src)
    {
        Plate = src.Plate;
        DeviceCode = src.DeviceCode;
        GpsTime = src.GpsTime;
        Lon = src.Lon;
        Lat = src.Lat;
        SpeedKph = src.SpeedKph;
        Direction = src.Direction;
        Altitude = src.Altitude;
        MileageKm = src.MileageKm;
        LocateState = src.LocateState;
        Address = src.Address;
        Online = src.Online;
        AccOn = src.AccOn;
        Satellites = src.Satellites;
        Signal = src.Signal;
        HasAlarm = src.HasAlarm;
    }
}

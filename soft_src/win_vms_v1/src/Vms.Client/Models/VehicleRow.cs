namespace Vms.Client.Models;

/// <summary>
/// 表格与地图共用的车辆行。作为 <c>ListView</c> 的 ItemSource 元素，
/// 必须实现 INotifyPropertyChanged —— 实时推送时只改变化的字段，
/// 界面按属性粒度刷新，3 万行也不会整表重画。
/// </summary>
public sealed class VehicleRow : ObservableObject
{
    private string _selfCode = string.Empty;
    private string _plate = string.Empty;
    private string _time = string.Empty;
    private float _speedKph;
    private string _location = string.Empty;
    private bool _online;
    private uint _offlineSeconds;
    private string _day = string.Empty;
    private string _issue = string.Empty;
    private AlarmFlags _alarm;
    private double _lon;
    private double _lat;

    public uint Id { get; init; }

    /// <summary>库表 d_vehicleinfo.vehicle_id（字符串主键）。按主键拉定位/视频时用它。</summary>
    public string VehicleId { get; init; } = string.Empty;

    /// <summary>库表 d_vehicleinfo.org_id（归属组织的字符串主键）。</summary>
    public string OrgId { get; init; } = string.Empty;

    public string SelfCode { get => _selfCode; set => SetProperty(ref _selfCode, value); }
    public string Plate { get => _plate; set => SetProperty(ref _plate, value); }
    public string Time { get => _time; set => SetProperty(ref _time, value); }
    public float SpeedKph
    {
        get => _speedKph;
        set { if (SetProperty(ref _speedKph, value)) OnPropertyChanged(nameof(SpeedText)); }
    }
    public string Location { get => _location; set => SetProperty(ref _location, value); }
    public bool Online
    {
        get => _online;
        set
        {
            if (!SetProperty(ref _online, value)) return;
            OnPropertyChanged(nameof(OnlineText));
            OnPropertyChanged(nameof(OnlineBrushKey));
        }
    }
    public uint OfflineSeconds
    {
        get => _offlineSeconds;
        set { if (SetProperty(ref _offlineSeconds, value)) OnPropertyChanged(nameof(OfflineDurationText)); }
    }
    public string Day { get => _day; set => SetProperty(ref _day, value); }
    public string Issue { get => _issue; set => SetProperty(ref _issue, value); }
    public AlarmFlags Alarm
    {
        get => _alarm;
        set { if (SetProperty(ref _alarm, value)) OnPropertyChanged(nameof(HasAlarm)); }
    }
    public double Lon { get => _lon; set => SetProperty(ref _lon, value); }
    public double Lat { get => _lat; set => SetProperty(ref _lat, value); }

    // --------------------------- 展示派生属性 ---------------------------

    public bool HasAlarm => _alarm != AlarmFlags.None;

    public string SpeedText => _speedKph <= 0.05f ? "--" : _speedKph.ToString("F0");
    public string OnlineText => _online ? "在线" : "离线";

    public string OfflineDurationText
    {
        get
        {
            if (_online || _offlineSeconds == 0) return "--";
            var t = TimeSpan.FromSeconds(_offlineSeconds);
            if (t.TotalDays >= 1) return $"{(int)t.TotalDays}天{t.Hours}小时";
            if (t.TotalHours >= 1) return $"{(int)t.TotalHours}小时{t.Minutes}分";
            return $"{Math.Max(1, (int)t.TotalMinutes)}分钟";
        }
    }

    /// <summary>用于 XAML 里按状态着色（在线/离线/报警三态）。</summary>
    public string OnlineBrushKey => _online ? "StatusOnlineBrush" : "StatusOfflineBrush";

    /// <summary>
    /// 用新到的推送数据**就地更新**本行（保留同一个对象实例）。
    /// 这样 ListView 只重画变化的单元格，而不是替换整行 —— 3 万行下差别是"流畅"和"卡死"。
    /// </summary>
    public void CopyFrom(VehicleRow src)
    {
        SelfCode = src.SelfCode;
        Plate = src.Plate;
        Time = src.Time;
        SpeedKph = src.SpeedKph;
        Location = src.Location;
        Online = src.Online;
        OfflineSeconds = src.OfflineSeconds;
        Day = src.Day;
        Issue = src.Issue;
        Alarm = src.Alarm;
        Lon = src.Lon;
        Lat = src.Lat;
    }
}

[Flags]
public enum AlarmFlags : uint
{
    None = 0,
    Overspeed = 1u << 0,
    Fatigue = 1u << 1,
    Offline = 1u << 2,
    Geofence = 1u << 3,
    GnssLost = 1u << 4,
}

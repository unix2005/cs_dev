namespace Vms.Client.Models;

/// <summary>
/// 车辆在界面上的三态。
/// 顺序即优先级：报警 &gt; 在线 &gt; 离线 —— 一台"在线的车正在报警"必须显示红色，
/// 否则值班人员会漏掉最该看的那条。
/// </summary>
public enum VehicleState
{
    Offline = 0,
    Online = 1,
    Alarm = 2,
}

/// <summary>
/// 树下挂的车辆叶子节点。只保留"树上要显示 + 选中后要联动"的少量字段，
/// 完整字段在右侧「定位数据 / 报警数据」页签里展开 —— 树上塞 20 列既看不清也拖慢渲染。
/// </summary>
public sealed class VehicleNode : OrgNodeBase
{
    private bool _online;
    private bool _hasAlarm;
    private float _speedKph;
    private string _location = string.Empty;
    private string _selfCode = string.Empty;
    private double _lon;
    private double _lat;

    /// <summary>车牌号（= <see cref="OrgNodeBase.Name"/>，单独暴露一个语义化名字给模板用）。</summary>
    public string Plate => Name;

    public string SelfCode { get => _selfCode; set => SetProperty(ref _selfCode, value); }

    public bool Online
    {
        get => _online;
        set
        {
            if (!SetProperty(ref _online, value)) return;
            OnPropertyChanged(nameof(State));
            OnPropertyChanged(nameof(StateText));
        }
    }

    public bool HasAlarm
    {
        get => _hasAlarm;
        set
        {
            if (!SetProperty(ref _hasAlarm, value)) return;
            OnPropertyChanged(nameof(State));
            OnPropertyChanged(nameof(StateText));
        }
    }

    public float SpeedKph
    {
        get => _speedKph;
        set { if (SetProperty(ref _speedKph, value)) OnPropertyChanged(nameof(DetailText)); }
    }

    public string Location
    {
        get => _location;
        set { if (SetProperty(ref _location, value)) OnPropertyChanged(nameof(DetailText)); }
    }

    /// <summary>GCJ-02 经度 / 纬度。选中车辆后地图要聚焦，所以树上也要带坐标。</summary>
    public double Lon { get => _lon; set => SetProperty(ref _lon, value); }
    public double Lat { get => _lat; set => SetProperty(ref _lat, value); }

    public override bool IsVehicle => true;

    /// <summary>树上该车辆是否被勾选（用于"在树上勾选若干车辆 → 定位数据栏只显示这些"）。
    /// 车辆是叶子，只承担 true/false 两态；但类型统一为 <c>bool?</c> 与组织节点的三态共用基类方法。
    /// 勾上/取消一台车后，应由调用方触发 <see cref="OrgNodeBase.RecomputeAncestors"/>，
    /// 让父组织随之变成"部分选择"(null) 或回满/回空 —— 这是三态树的关键一环。</summary>
    public new bool? IsChecked
    {
        get => base.IsChecked;
        set => base.IsChecked = value;
    }

    public override VehicleState State => _hasAlarm ? VehicleState.Alarm : _online ? VehicleState.Online : VehicleState.Offline;

    public string StateText => State switch
    {
        VehicleState.Alarm => "报警",
        VehicleState.Online => "在线",
        _ => "离线",
    };

    /// <summary>树上紧跟车牌的次要信息：速度 / 位置，一眼判断这车在不在跑。</summary>
    public string DetailText => _online
        ? (_speedKph > 0.5f ? $"{_speedKph:F0} km/h" : "静止")
        : (string.IsNullOrEmpty(_location) ? string.Empty : _location);

    /// <summary>从查询/推送行同步状态。实时推送命中树上已展开的车辆时调用。</summary>
    public void Apply(VehicleRow row)
    {
        SelfCode = row.SelfCode;
        Online = row.Online;
        HasAlarm = row.HasAlarm;
        SpeedKph = row.SpeedKph;
        Location = row.Location;
        Lon = row.Lon;
        Lat = row.Lat;
    }

    public static VehicleNode From(VehicleRow row)
    {
        var node = new VehicleNode { Id = row.Id, Name = row.Plate };
        node.Apply(row);
        return node;
    }

    /// <summary>懒加载占位："正在加载 N 台…"也是树上的一个合法状态。</summary>
    public static VehicleNode Placeholder(string text) => new() { Id = 0, Name = text };
}

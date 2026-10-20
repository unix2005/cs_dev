namespace Vms.Client.Models;

/// <summary>
/// 「报警数据」页签的一行。
///
/// ── 字段扩展点 ───────────────────────────────────────────────
/// 与 <see cref="PositionRecord"/> 同一套扩展方式：
///   ① 加属性 → ② 在 <see cref="CopyFrom"/> 补赋值 → ③ 在 DataTabsPanel.xaml 加列。
/// 差异点：报警还有"处理闭环"（处理人/处理时间/处理说明），
/// 这几个字段将来会由「违规报警」模块的写接口回填，因此这里先留成可写属性。
/// ────────────────────────────────────────────────────────────
/// </summary>
public sealed class AlarmRecord : ObservableObject
{
    private string _plate = string.Empty;
    private string _deviceCode = string.Empty;
    private string _alarmTime = string.Empty;
    private string _alarmType = string.Empty;
    private string _level = string.Empty;
    private float _speedKph;
    private string _address = string.Empty;
    private double _lon;
    private double _lat;
    private string _handleState = string.Empty;
    private string _handler = string.Empty;
    private string _handleTime = string.Empty;
    private string _handleNote = string.Empty;
    private bool _isChecked;
    private bool _isColumnHighlighted;

    /// <summary>行选择框状态（数据表格最左侧那个勾选框）。<b>不参与任何业务筛选</b>。</summary>
    public bool IsChecked
    {
        get => _isChecked;
        set => SetProperty(ref _isChecked, value);
    }

    /// <summary>
    /// 本行是否处于"列高亮"状态。语义见 <see cref="PositionRecord.IsColumnHighlighted"/>：
    /// 高亮的是"当前选中的那一列"，但状态挂在行上，由面板统一刷新所有可见行。
    /// </summary>
    public bool IsColumnHighlighted
    {
        get => _isColumnHighlighted;
        set => SetProperty(ref _isColumnHighlighted, value);
    }

    public uint Id { get; init; }
    public uint VehicleId { get; init; }

    /// <summary>
    /// 报警类型的原始枚举值（<c>AlarmFlags</c> 的数值）。
    /// 只用于去重/索引，不用于展示 —— 展示看 <see cref="AlarmType"/>。
    /// </summary>
    public uint AlarmCode { get; init; }

    // ---------------------------- 业务字段 ----------------------------

    public string Plate { get => _plate; set => SetProperty(ref _plate, value); }
    public string DeviceCode { get => _deviceCode; set => SetProperty(ref _deviceCode, value); }
    public string AlarmTime { get => _alarmTime; set => SetProperty(ref _alarmTime, value); }

    /// <summary>报警类型：超速 / 疲劳驾驶 / 越界 / 离线 / GNSS 丢失 / 急加速 / 急减速 …</summary>
    public string AlarmType { get => _alarmType; set => SetProperty(ref _alarmType, value); }

    /// <summary>报警等级：一级（严重）/ 二级 / 三级。</summary>
    public string Level
    {
        get => _level;
        set { if (SetProperty(ref _level, value)) OnPropertyChanged(nameof(IsSevere)); }
    }

    public float SpeedKph
    {
        get => _speedKph;
        set { if (SetProperty(ref _speedKph, value)) OnPropertyChanged(nameof(SpeedText)); }
    }

    public string Address { get => _address; set => SetProperty(ref _address, value); }
    public double Lon { get => _lon; set => SetProperty(ref _lon, value); }
    public double Lat { get => _lat; set => SetProperty(ref _lat, value); }

    /// <summary>处理状态：未处理 / 已处理 / 已忽略 / 误报。</summary>
    public string HandleState
    {
        get => _handleState;
        set
        {
            if (!SetProperty(ref _handleState, value)) return;
            OnPropertyChanged(nameof(IsHandled));
        }
    }

    public string Handler { get => _handler; set => SetProperty(ref _handler, value); }
    public string HandleTime { get => _handleTime; set => SetProperty(ref _handleTime, value); }
    public string HandleNote { get => _handleNote; set => SetProperty(ref _handleNote, value); }

    // ---------------------------- 展示派生 ----------------------------

    public string SpeedText => _speedKph <= 0.05f ? "--" : _speedKph.ToString("F1");
    public bool IsHandled => !string.IsNullOrEmpty(_handleState) && _handleState != "未处理";
    public bool IsSevere => _level.StartsWith('一');

    public void CopyFrom(AlarmRecord src)
    {
        Plate = src.Plate;
        DeviceCode = src.DeviceCode;
        AlarmTime = src.AlarmTime;
        AlarmType = src.AlarmType;
        Level = src.Level;
        SpeedKph = src.SpeedKph;
        Address = src.Address;
        Lon = src.Lon;
        Lat = src.Lat;
        HandleState = src.HandleState;
        Handler = src.Handler;
        HandleTime = src.HandleTime;
        HandleNote = src.HandleNote;
    }
}

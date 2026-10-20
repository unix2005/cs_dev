using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Media;
using Vms.Client.Models;

namespace Vms.Client.Converters;

/// <summary>bool → Visibility。Invert 为 true 时取反。</summary>
public sealed partial class BoolToVisibilityConverter : IValueConverter
{
    public bool Invert { get; set; }

    public object Convert(object value, Type targetType, object parameter, string language)
    {
        var flag = value is bool b && b;
        if (Invert) flag = !flag;
        return flag ? Visibility.Visible : Visibility.Collapsed;
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new NotSupportedException();
}

/// <summary>bool → Brush。用于"选中/未选中"、"在线/离线"这类二态着色。</summary>
public sealed partial class BoolToBrushConverter : IValueConverter
{
    public Brush? TrueBrush { get; set; }
    public Brush? FalseBrush { get; set; }

    public object? Convert(object value, Type targetType, object parameter, string language)
        => value is bool b && b ? TrueBrush : FalseBrush;

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new NotSupportedException();
}

/// <summary>
/// <see cref="VehicleState"/> → Brush。车辆三态着色的唯一入口：
/// 报警红 &gt; 在线蓝 &gt; 离线灰。
/// 用枚举而不是两个 bool 组合，是为了让"报警优先于在线"这条业务规则
/// 只写在模型里一处，XAML 侧不重复判断。
/// </summary>
public sealed partial class VehicleStateToBrushConverter : IValueConverter
{
    public Brush? OnlineBrush { get; set; }
    public Brush? OfflineBrush { get; set; }
    public Brush? AlarmBrush { get; set; }

    public object? Convert(object value, Type targetType, object parameter, string language)
        => value switch
        {
            VehicleState.Alarm => AlarmBrush,
            VehicleState.Online => OnlineBrush,
            _ => OfflineBrush,
        };

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new NotSupportedException();
}

/// <summary>报警位掩码 → 是否显示报警标记。</summary>
public sealed partial class AlarmToVisibilityConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        var flagged = value is not null && System.Convert.ToUInt32(value) != 0;
        return flagged ? Visibility.Visible : Visibility.Collapsed;
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new NotSupportedException();
}

/// <summary>车辆状态 → 是否显示"报警"角标（仅 Alarm 态显示）。</summary>
public sealed partial class StateToAlarmVisibilityConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language)
        => value is VehicleState.Alarm ? Visibility.Visible : Visibility.Collapsed;

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new NotSupportedException();
}

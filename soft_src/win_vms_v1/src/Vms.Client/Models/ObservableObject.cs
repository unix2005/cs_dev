using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Vms.Client.Models;

/// <summary>
/// 最小可用的 <see cref="INotifyPropertyChanged"/> 基类。
/// 项目里没有引入 CommunityToolkit.Mvvm —— 桌面监控端对 UI 刷新是"高频窄字段更新"，
/// 自己写一个 30 行的基类比背一个框架更可控，也少一份 NuGet 依赖。
/// </summary>
public abstract class ObservableObject : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

    protected bool SetProperty<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return false;
        field = value;
        OnPropertyChanged(propertyName);
        return true;
    }
}

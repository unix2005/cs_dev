using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace Vms.Client.Controls;

/// <summary>
/// 可拖拽分隔条（零依赖实现）。
///
/// 用法：把它放进需要调整的 <see cref="Grid"/>，并且**自带一个独立的列/行**，
/// 它会自动找左右的兄弟列（上下的兄弟行）来做此消彼长的调整：
/// <code>
/// &lt;Grid.ColumnDefinitions&gt;
///   &lt;ColumnDefinition Width="236" MinWidth="180" MaxWidth="400" /&gt;
///   &lt;ColumnDefinition Width="6" /&gt;                                  ← 分隔条自己占一列
///   &lt;ColumnDefinition Width="*" MinWidth="320" /&gt;
/// &lt;/Grid.ColumnDefinitions&gt;
/// &lt;controls:SplitterBar Grid.Column="1" /&gt;
/// </code>
///
/// 为什么用"相邻兄弟列"而不是暴露 ColumnDefinition 属性：
///   ColumnDefinition 不是 FrameworkElement，从 XAML 传进来要绕 x:Reference，
///   而且列一改结构就得同步改绑定。自动探测索引后，增删列都不需要动分隔条。
///
/// 交互约定：
///   · 拖拽     —— 改变前一个定义的大小（后一个是星号时由星号吸收，总宽不变）；
///   · 双击     —— 恢复到首次布局时的宽度，值班人员拖乱了能一键还原；
///   · 悬停     —— 细线变强调色，明确提示"这里能拖"。
/// </summary>
public sealed partial class SplitterBar : UserControl
{
    private static readonly Color IdleColor = Color.FromArgb(0xFF, 0xEA, 0xEA, 0xEA);
    private static readonly Color HoverColor = Color.FromArgb(0xFF, 0x00, 0x67, 0xC0);

    private Grid? _grid;
    private int _index;

    private bool _dragging;
    private double _startPointer;
    private double _startPrev;
    private double _startNext;
    private double _defaultPrev = double.NaN;

    public SplitterBar()
    {
        InitializeComponent();
        ApplyOrientation();

        Loaded += OnLoaded;
        Unloaded += OnUnloaded;

        Root.PointerEntered += (_, _) => SetLineColor(HoverColor);
        Root.PointerExited += (_, _) => { if (!_dragging) SetLineColor(IdleColor); };
        Root.PointerPressed += OnPointerPressed;
        Root.PointerMoved += OnPointerMoved;
        Root.PointerReleased += OnPointerReleased;
        Root.PointerCaptureLost += (_, _) => EndDrag();
        Root.DoubleTapped += OnDoubleTapped;
    }

    // ============================ 依赖属性 ============================

    /// <summary>
    /// <see cref="Orientation.Vertical"/>（默认）= 竖条，调整**列**宽；
    /// <see cref="Orientation.Horizontal"/> = 横条，调整**行**高。
    /// </summary>
    public static readonly DependencyProperty OrientationProperty = DependencyProperty.Register(
        nameof(Orientation), typeof(Orientation), typeof(SplitterBar),
        new PropertyMetadata(Orientation.Vertical, (d, _) => ((SplitterBar)d).ApplyOrientation()));

    public Orientation Orientation
    {
        get => (Orientation)GetValue(OrientationProperty);
        set => SetValue(OrientationProperty, value);
    }

    // ============================ 初始化 ============================

    private void ApplyOrientation()
    {
        var thickness = (double)Application.Current.Resources["SplitterThickness"];
        var vertical = Orientation == Orientation.Vertical;

        Width = vertical ? thickness : double.NaN;
        Height = vertical ? double.NaN : thickness;
        HorizontalAlignment = HorizontalAlignment.Stretch;
        VerticalAlignment = VerticalAlignment.Stretch;

        Line.Width = vertical ? 1 : double.NaN;
        Line.Height = vertical ? double.NaN : 1;
        Line.HorizontalAlignment = vertical ? HorizontalAlignment.Center : HorizontalAlignment.Stretch;
        Line.VerticalAlignment = vertical ? VerticalAlignment.Stretch : VerticalAlignment.Center;

        SetLineColor(IdleColor);

        // 光标必须由控件自己设（ProtectedCursor 是 protected），
        // 在 XAML 里没有可等价配置的属性。
        ProtectedCursor = InputSystemCursor.Create(
            vertical ? InputSystemCursorShape.SizeWestEast : InputSystemCursorShape.SizeNorthSouth);
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        _grid = Parent as Grid;
        if (_grid is null) return;

        _index = Orientation == Orientation.Vertical ? Grid.GetColumn(this) : Grid.GetRow(this);
        if (_index <= 0) return;

        _defaultPrev = Orientation == Orientation.Vertical
            ? _grid.ColumnDefinitions[_index - 1].ActualWidth
            : _grid.RowDefinitions[_index - 1].ActualHeight;
    }

    private void OnUnloaded(object sender, RoutedEventArgs e) => EndDrag();

    // ============================ 拖拽逻辑 ============================

    private void OnPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        if (_grid is null || _index <= 0) return;
        if (!Root.CapturePointer(e.Pointer)) return;

        _dragging = true;
        _startPointer = PointerPosition(e);
        _startPrev = PrevSize();
        _startNext = NextSize();
        SetLineColor(HoverColor);
        e.Handled = true;
    }

    private void OnPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        if (!_dragging || _grid is null) return;

        var delta = PointerPosition(e) - _startPointer;
        if (Math.Abs(delta) < 0.5) return;

        var (prevMin, prevMax, nextMin, nextMax) = Limits();

        // ① 先把前一个定义推到目标位置（并夹到它自己的 Min/Max）
        var newPrev = Math.Clamp(_startPrev + delta, prevMin, prevMax);
        var applied = newPrev - _startPrev;

        // ② 后一个定义若是星号（*），它天然吸收变化，不需要动
        if (NextIsStar())
        {
            SetPrev(newPrev);
            e.Handled = true;
            return;
        }

        // ③ 否则由后一个定义分摊；若它被 Min/Max 卡住，前一个也要相应退回，
        //    保证两者之和恒定 —— 否则拖到边界时整块内容会"挤掉"或"多出"一截空白。
        var newNext = Math.Clamp(_startNext - applied, nextMin, nextMax);
        var nextDelta = newNext - _startNext;
        var finalPrev = Math.Clamp(_startPrev - nextDelta, prevMin, prevMax);

        SetPrev(finalPrev);
        SetNext(_startNext + (_startPrev - finalPrev));
        e.Handled = true;
    }

    private void OnPointerReleased(object sender, PointerRoutedEventArgs e) => EndDrag();

    private void EndDrag()
    {
        if (!_dragging) return;
        _dragging = false;
        SetLineColor(IdleColor);
        Root.ReleasePointerCaptures();
    }

    private void OnDoubleTapped(object sender, DoubleTappedRoutedEventArgs e)
    {
        if (_grid is null || _index <= 0 || double.IsNaN(_defaultPrev)) return;
        var (prevMin, prevMax, _, _) = Limits();
        SetPrev(Math.Clamp(_defaultPrev, prevMin, prevMax));
        e.Handled = true;
    }

    // ============================ 取值辅助 ============================

    private double PointerPosition(PointerRoutedEventArgs e)
    {
        if (_grid is null) return 0;
        var p = e.GetCurrentPoint(_grid).Position;
        return Orientation == Orientation.Vertical ? p.X : p.Y;
    }

    private double PrevSize()
    {
        if (_grid is null) return 0;
        return Orientation == Orientation.Vertical
            ? _grid.ColumnDefinitions[_index - 1].ActualWidth
            : _grid.RowDefinitions[_index - 1].ActualHeight;
    }

    private double NextSize()
    {
        if (_grid is null) return 0;
        return Orientation == Orientation.Vertical
            ? _grid.ColumnDefinitions[_index + 1].ActualWidth
            : _grid.RowDefinitions[_index + 1].ActualHeight;
    }

    private bool NextIsStar()
    {
        if (_grid is null) return false;
        return Orientation == Orientation.Vertical
            ? _grid.ColumnDefinitions[_index + 1].Width.IsStar
            : _grid.RowDefinitions[_index + 1].Height.IsStar;
    }

    private void SetPrev(double value)
    {
        if (_grid is null) return;
        if (Orientation == Orientation.Vertical) _grid.ColumnDefinitions[_index - 1].Width = new GridLength(value);
        else _grid.RowDefinitions[_index - 1].Height = new GridLength(value);
    }

    private void SetNext(double value)
    {
        if (_grid is null) return;
        if (Orientation == Orientation.Vertical) _grid.ColumnDefinitions[_index + 1].Width = new GridLength(value);
        else _grid.RowDefinitions[_index + 1].Height = new GridLength(value);
    }

    /// <summary>
    /// 取相邻两个定义的夹取范围。
    /// ColumnDefinition.MinWidth/MaxWidth 的默认值是 0 / +∞，直接用会让列可以被拖成 0 宽度
    /// 或顶到屏幕外，所以这里给无约束的一侧兜一个业务下限（留 160px 给对面，避免拖没了找不回来）。
    /// </summary>
    private (double PrevMin, double PrevMax, double NextMin, double NextMax) Limits()
    {
        if (_grid is null) return (96, double.MaxValue, 96, double.MaxValue);

        var vertical = Orientation == Orientation.Vertical;
        var extent = vertical ? _grid.ActualWidth : _grid.ActualHeight;

        var prevMinRaw = vertical ? _grid.ColumnDefinitions[_index - 1].MinWidth : _grid.RowDefinitions[_index - 1].MinHeight;
        var prevMaxRaw = vertical ? _grid.ColumnDefinitions[_index - 1].MaxWidth : _grid.RowDefinitions[_index - 1].MaxHeight;
        var nextMinRaw = vertical ? _grid.ColumnDefinitions[_index + 1].MinWidth : _grid.RowDefinitions[_index + 1].MinHeight;
        var nextMaxRaw = vertical ? _grid.ColumnDefinitions[_index + 1].MaxWidth : _grid.RowDefinitions[_index + 1].MaxHeight;

        const double FallbackMin = 120;
        var prevMin = prevMinRaw > 0 ? prevMinRaw : FallbackMin;
        var nextMin = nextMinRaw > 0 ? nextMinRaw : FallbackMin;
        var prevMax = double.IsInfinity(prevMaxRaw) ? Math.Max(prevMin, extent - nextMin) : prevMaxRaw;
        var nextMax = double.IsInfinity(nextMaxRaw) ? Math.Max(nextMin, extent - prevMin) : nextMaxRaw;

        return (prevMin, prevMax, nextMin, nextMax);
    }

    private void SetLineColor(Color color)
        => Line.Background = new SolidColorBrush(color);
}

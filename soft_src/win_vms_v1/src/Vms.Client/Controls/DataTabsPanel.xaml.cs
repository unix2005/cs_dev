using System.Text;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Vms.Client.Models;
using Vms.Client.ViewModels;
using Windows.UI;
// ⚠ 不用 `using Clipboard = ...` 别名：文件顶部有 `using Windows.UI;`，它里面也含一个
//   Clipboard 静态类。别名与它同名的同时存在时，编译器仍会按外层 using 解析到 Windows.UI.Clipboard，
//   报错 CS0723「无法声明静态类型的变量」。这里改用完整类型名 + 短别名 DataPackage，一次性避开撞名。
using DataPackage = Windows.ApplicationModel.DataTransfer.DataPackage;
using WinrtClipboard = Windows.ApplicationModel.DataTransfer.Clipboard;

namespace Vms.Client.Controls;

/// <summary>右侧下半部分的数据页签（定位数据 / 报警数据）。</summary>
public sealed partial class DataTabsPanel : UserControl
{
    // 隔行底色取自主题资源（GridRowAltBackgroundBrush = #FAFAFA），不再硬编码 ——
    // 之前写死颜色看起来"没生效"，其实是和主题里的令牌各改各的，主题一调就对不上。
    private static readonly Brush EvenRowBrush = ResolveBrush("GridRowBackgroundBrush", "#FFFFFFFF");
    private static readonly Brush OddRowBrush = ResolveBrush("GridRowAltBackgroundBrush", "#FFFAFAFA");

    /// <summary>从 Application 资源取刷子；取不到就用字面量兜底（诊断设施不该成为崩溃源）。</summary>
    private static Brush ResolveBrush(string key, string fallback)
    {
        try
        {
            if (Application.Current?.Resources.TryGetValue(key, out var v) == true && v is Brush b)
                return b;
        }
        catch
        {
            // 落到下面兜底
        }
        return new SolidColorBrush(ParseColor(fallback));
    }

    /// <summary>#AARRGGBB → Color。用 uint 解析，避免依赖 System.Drawing（WinUI 3 无它）。</summary>
    private static Color ParseColor(string hex)
    {
        hex = hex.TrimStart('#');
        if (hex.Length != 8) return Microsoft.UI.Colors.White;
        uint v = uint.Parse(hex, System.Globalization.NumberStyles.HexNumber);
        return Color.FromArgb((byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8), (byte)v);
    }

    public DataTabsPanel()
    {
        ViewModel = ((App)Application.Current).ViewModel;
        InitializeComponent();

        // SelectorBar 的 SelectionChanged 不保证在初次加载时触发，
        // 显式跑一次，保证"当前页签 / 面板可见性 / 按钮可见性"三者初始一致。
        Loaded += (_, _) =>
        {
            ApplyTab(ResolveIndex(Tabs.SelectedItem));

            // 诊断：把平时需要人手操作才会走到的路径压出来（见 Diag 注释）
            if (Diagnostics.Diag.StartOnAlarmTab) Tabs.SelectedItem = AlarmTab;
            if (Diagnostics.Diag.AutoScroll) StartDiagnosticAutoScroll();
        };

        Unloaded += (_, _) =>
        {
            _diagScrollTimer?.Stop();
            _diagScrollTimer = null;
        };
    }

    public MainViewModel ViewModel { get; }

    private DispatcherQueueTimer? _diagScrollTimer;
    private int _diagScrollIndex;

    /// <summary>
    /// 诊断用：定时把定位列表往下滚，逼出"容器生成 → 触底预取 → 重建集合"这条链路。
    /// 这条链路正是之前 0xC0000005 的现场（预取在 XAML 本机回调栈里改了数据源）。
    /// </summary>
    private void StartDiagnosticAutoScroll()
    {
        _diagScrollTimer = DispatcherQueue.GetForCurrentThread().CreateTimer();
        _diagScrollTimer.Interval = TimeSpan.FromSeconds(2);
        _diagScrollTimer.Tick += (_, _) =>
        {
            var items = PositionRows.Items;
            if (items.Count == 0) return;

            _diagScrollIndex = Math.Min(_diagScrollIndex + 200, items.Count - 1);
            PositionRows.ScrollIntoView(items[_diagScrollIndex]);
        };
        _diagScrollTimer.Start();
    }

    // ============================== 页签切换 ==============================

    private void Tabs_SelectionChanged(SelectorBar sender, SelectorBarSelectionChangedEventArgs args)
        => ApplyTab(ResolveIndex(sender.SelectedItem));

    private static int ResolveIndex(SelectorBarItem? item)
        => item is not null && int.TryParse(item.Tag as string, out var i) ? i : 0;

    private void ApplyTab(int index)
    {
        if (PositionPanel is null || AlarmPanel is null) return;   // 初始化尚未完成时忽略

        ViewModel.Monitor.SelectedTabIndex = index;
        PositionPanel.Visibility = index == 0 ? Visibility.Visible : Visibility.Collapsed;
        AlarmPanel.Visibility = index == 1 ? Visibility.Visible : Visibility.Collapsed;
        LoadMoreButton.Visibility = index == 0 ? Visibility.Visible : Visibility.Collapsed;
        HintText.Text = index == 0 ? "定位数据：随实时推送就地刷新" : "报警数据：新报警自动插到最前";

        // 先把可见性切好，再让 VM 去重建集合 —— NotifyTabChanged 内部会排队，
        // 所以重建发生时报警面板已经真正可见了（对不可见列表做结构性变更才是危险的）。
        ViewModel.Monitor.NotifyTabChanged(index);
    }

    // ============================ 分页加载 ============================

    private async void LoadMore_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            await ViewModel.Monitor.Feed.LoadMoreAsync();
        }
        catch (Exception ex)
        {
            HintText.Text = $"加载下一页失败：{ex.Message}";
        }
    }

    /// <summary>
    /// ListView 虚拟化的核心回调：只在容器被创建/复用时触发。
    /// 这里做两件事：斑马纹着色（不动数据模板，省一层绑定开销）+ 触底预取下一页。
    ///
    /// ⚠ 本方法**绝不能** await 或在这里直接改数据源，原因见 <see cref="Prefetch"/>。
    /// </summary>
    private void PositionRows_ContainerContentChanging(ListViewBase sender, ContainerContentChangingEventArgs args)
    {
        if (args.InRecycleQueue) return;
        if (args.ItemContainer is ListViewItem container)
        {
            container.Background = args.ItemIndex % 2 == 0 ? EvenRowBrush : OddRowBrush;
            ApplyColumnHighlight(container, _positionHighlightColumn);
        }

        Prefetch(args.ItemIndex);
    }

    private void AlarmRows_ContainerContentChanging(ListViewBase sender, ContainerContentChangingEventArgs args)
    {
        if (args.InRecycleQueue) return;
        if (args.ItemContainer is ListViewItem container)
        {
            container.Background = args.ItemIndex % 2 == 0 ? EvenRowBrush : OddRowBrush;
            ApplyColumnHighlight(container, _alarmHighlightColumn);
        }

        // 报警页签不再翻页（它是滚动追加的集合），这里只做着色
    }

    /// <summary>
    /// 给容器里"当前高亮列"的单元格上高亮底色，其余恢复透明。
    ///
    /// 为什么在<b>容器刷新回调</b>里做、而不是用绑定：
    /// · 绑定的条件只能是"行.IsColumnHighlighted == true"，它只知道"这行参与高亮"，
    ///   不知道"参与的是第几列" —— 列号是<b>表级</b>状态（同一时刻全表只有一列高亮），
    ///   这个"多对一"的关系绑定表达不了。
    /// · ListView 虚拟化意味着容器会被回收复用，滚动后新容器必须重新上色；
    ///   容器刷新正好覆盖"创建/复用"这两个时机。
    ///
    /// ⚠ WinUI 3 的 <see cref="TextBlock"/> <b>没有</b> Background 属性（只有 Border/Panel 有），
    ///   所以模板里每个单元格都套了一层 <c>CellHighlight</c> Border，这里给它们上色。
    ///   列号取自每个 Border 的 <c>Tag</c>（模板里写死的列索引）。
    /// </summary>
    private static void ApplyColumnHighlight(ListViewItem container, int highlightColumn)
    {
        foreach (var border in FindDescendants<Border>(container))
        {
            if (border.Tag is not string s || !int.TryParse(s, out var col)) continue;
            border.Background = col == highlightColumn ? ColumnHighlightBrush : TransparentBrush;
        }
    }

    private static readonly Brush TransparentBrush = new SolidColorBrush(Microsoft.UI.Colors.Transparent);

    /// <summary>深度优先收集所有指定类型的可视元素。</summary>
    private static IEnumerable<T> FindDescendants<T>(DependencyObject root) where T : DependencyObject
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(root); i++)
        {
            var child = VisualTreeHelper.GetChild(root, i);
            if (child is T t) yield return t;
            foreach (var sub in FindDescendants<T>(child)) yield return sub;
        }
    }

    private bool _prefetchScheduled;

    /// <summary>
    /// 距离末尾 40 行时预取，用户滚到底时下一页通常已经到位（"无限滚动"手感）。
    ///
    /// ── 为什么这里只是"排队"，而不是就地 await 加载 ──────────────────
    /// 本方法的调用方是 ContainerContentChanging —— 它是 **XAML 本机在生成列表容器
    /// 的过程中同步回调托管代码** 的，也就是说此刻本机栈上正压着一轮集合枚举。
    /// 加载下一页会 Clear() + 重建 Positions，等于本机栈还没退回去就把脚下的地板抽掉：
    /// 结果是 Microsoft.UI.Xaml.dll 里 0xc0000005 访问违例，进程瞬间消失，
    /// 而且托管层任何 try/catch 都收不到通知（crash.log 一片空白）。
    /// 排到下一个 UI 节拍执行，等本机栈退干净了再改集合，才是安全的。
    /// ────────────────────────────────────────────────────────────
    /// </summary>
    private void Prefetch(int index)
    {
        var feed = ViewModel.Monitor.Feed;
        if (index < feed.Count - 40 || !feed.HasMore || feed.IsLoading) return;
        if (_prefetchScheduled) return;   // 一屏几十个容器会连续命中，只排一次
        _prefetchScheduled = true;

        DispatcherQueue.GetForCurrentThread().TryEnqueue(DispatcherQueuePriority.Low, () =>
        {
            _prefetchScheduled = false;
            _ = LoadMoreSafeAsync(feed);
        });
    }

    private async Task LoadMoreSafeAsync(VehicleFeed feed)
    {
        try
        {
            await feed.LoadMoreAsync();
        }
        catch (Exception ex)
        {
            HintText.Text = $"加载下一页失败：{ex.Message}";
        }
    }

    // ============================== 行交互（左键/右键/选择框）=============================

    /// <summary>当前高亮的列索引（-1 = 无高亮）。两张表各存一个。</summary>
    private int _positionHighlightColumn = -1;
    private int _alarmHighlightColumn = -1;

    /// <summary>列高亮底色。用淡强调色，既醒目又不至于盖住"报警红/在线蓝"的文字语义。</summary>
    private static readonly Brush ColumnHighlightBrush =
        new SolidColorBrush(Color.FromArgb(0xFF, 0xE8, 0xF1, 0xFB));   // = AccentSoftBrush

    /// <summary>行选择框变化：只更新提示文字里的"已选 N 行"，不碰数据范围。</summary>
    private void PositionRowCheck_Changed(object sender, RoutedEventArgs e)
        => UpdateSelectionHint();

    private void AlarmRowCheck_Changed(object sender, RoutedEventArgs e)
        => UpdateSelectionHint();

    /// <summary>底部提示条显示两个表各自勾选了几行（勾选框不参与业务，仅供批量操作预览）。</summary>
    private void UpdateSelectionHint()
    {
        int p = ViewModel.Monitor.Positions.Count(r => r.IsChecked);
        int a = ViewModel.Monitor.Alarms.Count(r => r.IsChecked);
        var parts = new List<string>();
        if (p > 0) parts.Add($"定位已选 {p} 行");
        if (a > 0) parts.Add($"报警已选 {a} 行");
        _selectionHint = parts.Count > 0 ? string.Join("  ·  ", parts) : string.Empty;
        if (HintText.Text == "定位数据：随实时推送就地刷新" || HintText.Text == "报警数据：新报警自动插到最前")
            HintText.Text = string.IsNullOrEmpty(_selectionHint) ? _tabHint : _selectionHint;
    }

    private string _selectionHint = string.Empty;
    private string _tabHint = "定位数据：随实时推送就地刷新";

    /// <summary>
    /// 左键点击定位数据行：<b>复制该单元格</b> + <b>高亮当前列</b>。
    ///
    /// 需求原话："没点选择框时，鼠标左键点击了数据行中的某列，这列要变颜色表示当前选择的列值"。
    /// 实现要点：
    ///   · 点在<b>选择框列</b>（第 0 列）→ 只让 CheckBox 自己切换，**不**改高亮列
    ///     （CheckBox 会吃掉这次点击，走到这里说明是它自己的处理）；
    ///   · 点其它列 → 用该 TextBlock 的 <c>Tag</c>（模板里写的列号）取列索引，
    ///     存到 <c>_positionHighlightColumn</c>，再刷新所有可见行的高亮；
    ///   · 复制的是被点单元格文本；点在行内空白处 → 复制整行。
    ///
    /// ⚠ 高亮状态挂在<b>行模型</b>（IsColumnHighlighted）上，但"哪一列高亮"是表级状态。
    ///   ListView 虚拟化决定了只有可见行才有容器，所以每次改列号都必须重刷全部可见行，
    ///   否则滚动后新生成的容器不知道当前高亮的是哪列。
    /// </summary>
    private void PositionRow_Tapped(object sender, TappedRoutedEventArgs e)
    {
        // 点在选择框上 → 不当作"选择列"
        if (IsCheckBox(e.OriginalSource as DependencyObject)) return;

        var tb = FindTextBlock(e.OriginalSource as DependencyObject);
        if (ColumnIndexOf(e.OriginalSource as DependencyObject) is { } col)
        {
            _positionHighlightColumn = col;
            SetHighlightColumn(col, PositionRows);
        }

        var text = tb?.Text;
        if (string.IsNullOrWhiteSpace(text))
        {
            // 点在行内空白处 → 复制整行
            if ((sender as FrameworkElement)?.DataContext is PositionRecord row)
                text = string.Join('\t',
                    row.Plate, row.DeviceCode, row.GpsTime, row.LonText, row.LatText,
                    row.SpeedText, row.DirectionText, row.AltitudeText, row.MileageText,
                    row.LocateState, row.OnlineText, row.Address);
        }
        if (!string.IsNullOrWhiteSpace(text) && CopyToClipboard(text) is { } err) HintText.Text = err;
    }

    /// <summary>左键点击报警数据行：同定位表（复制 + 列高亮）。</summary>
    private void AlarmRow_Tapped(object sender, TappedRoutedEventArgs e)
    {
        if (IsCheckBox(e.OriginalSource as DependencyObject)) return;

        var tb = FindTextBlock(e.OriginalSource as DependencyObject);
        if (ColumnIndexOf(e.OriginalSource as DependencyObject) is { } col)
        {
            _alarmHighlightColumn = col;
            SetHighlightColumn(col, AlarmRows);
        }

        var text = tb?.Text;
        if (string.IsNullOrWhiteSpace(text))
        {
            if ((sender as FrameworkElement)?.DataContext is AlarmRecord row)
                text = string.Join('\t',
                    row.Plate, row.DeviceCode, row.AlarmTime, row.AlarmType, row.Level,
                    row.SpeedText, row.Address, row.HandleState, row.Handler, row.HandleTime, row.HandleNote);
        }
        if (!string.IsNullOrWhiteSpace(text) && CopyToClipboard(text) is { } err) HintText.Text = err;
    }

    /// <summary>
    /// 切换"当前高亮列"，并刷新所有可见行的 <c>IsColumnHighlighted</c>。
    ///
    /// ⚠ 高亮的判定必须在**可视树层面**做，不能只看行模型的 bool：
    /// 行模型的 IsColumnHighlighted 只表达"这一行参与列高亮"，
    /// 真正"只有第 N 列着色"要靠模板里把高亮色画在对应 Grid.Column 上。
    /// 而 ListView 虚拟化决定了只有可见行才有容器 —— 所以改完列号必须
    /// <c>RequestContainerUpdate()</c> 让所有可见容器重建，否则滚动后新生成的
    /// 容器不知道当前高亮的是哪一列。
    ///
    /// 可见容器里按 Tag（= 列号）比对，命中才着高亮色。
    /// </summary>
    private static void SetHighlightColumn(int newCol, ListView list)
    {
        // ⚠ WinUI 3 的 ListView **没有** RequestContainerUpdate()（那是 UWP/WinUI 2 的 API）。
        //   改用"翻转行模型的 bool"来强制刷新：false → true 才会发 PropertyChanged，
        //   绑定随之把容器内容刷新一遍，ApplyColumnHighlight 再按新列号上色。
        foreach (var item in list.Items)
        {
            if (item?.GetType().GetProperty("IsColumnHighlighted") is not { } pi) continue;
            pi.SetValue(item, false);
            pi.SetValue(item, true);
        }
    }

    /// <summary>从点击源往上找最近那个带 <c>Tag="列号"</c> 的 Border，返回列索引。</summary>
    /// <remarks>
    /// 列号挂在 <b>Border</b> 上（因为 WinUI 3 的 TextBlock 没有 Background，
    /// 高亮底色只能画在 Border 上），而点击命中的原始来源通常是 Border 里的 TextBlock，
    /// 所以必须从 TextBlock 一路往上找 Border，不能只看 TextBlock.Tag。
    /// </remarks>
    private static int? ColumnIndexOf(DependencyObject? start)
    {
        for (var d = start; d is not null; d = VisualTreeHelper.GetParent(d))
            if (d is FrameworkElement fe && fe.Tag is string s && int.TryParse(s, out var col))
                return col;
        return null;
    }

    /// <summary>判断点击源是否落在 CheckBox 里（第 0 列的行选择框）。</summary>
    private static bool IsCheckBox(DependencyObject? start)
    {
        for (var d = start; d is not null; d = VisualTreeHelper.GetParent(d))
            if (d is CheckBox) return true;
        return false;
    }

    /// <summary>
    /// 把纯文本写进剪贴板。成功返回 null，失败返回错误描述（由调用方写进提示条）。
    ///
    /// 做成"返回错误"而不是"自己写提示"，是因为这是个 static 工具方法，
    /// 而提示条 <see cref="HintText"/> 是实例字段 —— 让它返回结果、调用方决定怎么提示，
    /// 既保住 static 的纯粹性，也不会让每个调用点重复写 try/catch。
    /// </summary>
    private static string? CopyToClipboard(string text)
    {
        try
        {
            // ⚠ WinUI 3（UWP）剪贴板 API 没有 SetText 便捷方法：
            //   必须先建 DataPackage → package.SetText → SetContent → Flush。
            //   Flush() 让内容在本进程退出后仍留在剪贴板。
            var package = new DataPackage
            {
                RequestedOperation = Windows.ApplicationModel.DataTransfer.DataPackageOperation.Copy,
            };
            package.SetText(text);
            WinrtClipboard.SetContent(package);
            WinrtClipboard.Flush();
            return null;
        }
        catch (Exception ex)
        {
            return $"复制失败：{ex.Message}";
        }
    }

    // ============================== 右键菜单 ==============================

    /// <summary>
    /// 右键菜单：复制（已实现）+ 5 个占位功能。
    ///
    /// 用 <see cref="MenuFlyout"/> 而不是 FloatingMenu：菜单项需要 disabled 占位、
    /// 分隔线，FloatingMenu 是面向触控手势的，鼠标右键场景下反而更别扭。
    /// 占位项一律 <c>IsEnabled="False"</c> —— 让用户一眼看出"还没做"，好过点了没反应。
    ///
    /// 复制语义：有勾选行 → 复制所有勾选行（多行用换行分隔，一次拿走一批）；
    /// 没有勾选 → 只复制光标所在那一行。
    /// </summary>
    private void PositionRow_RightTapped(object sender, RightTappedRoutedEventArgs e)
    {
        var menu = BuildRowMenu(
            // 该表里当前勾选的所有行（复选框语义：选中即参与批量操作）
            checkedRows: ViewModel.Monitor.Positions.Where(r => r.IsChecked).ToList(),
            // 光标所在行（没有勾选时的复制对象）
            currentRow: CurrentRecord<PositionRecord>(e.OriginalSource as DependencyObject),
            wholeRowOf: r => string.Join('\t', r.Plate, r.DeviceCode, r.GpsTime, r.LonText, r.LatText,
                                          r.SpeedText, r.DirectionText, r.AltitudeText, r.MileageText,
                                          r.LocateState, r.OnlineText, r.Address));

        menu.ShowAt(e.OriginalSource as FrameworkElement ?? (FrameworkElement)sender,
                    new FlyoutShowOptions { Position = e.GetPosition(e.OriginalSource as UIElement) });
    }

    private void AlarmRow_RightTapped(object sender, RightTappedRoutedEventArgs e)
    {
        var menu = BuildRowMenu(
            checkedRows: ViewModel.Monitor.Alarms.Where(r => r.IsChecked).ToList(),
            currentRow: CurrentRecord<AlarmRecord>(e.OriginalSource as DependencyObject),
            wholeRowOf: r => string.Join('\t', r.Plate, r.DeviceCode, r.AlarmTime, r.AlarmType, r.Level,
                                          r.SpeedText, r.Address, r.HandleState, r.Handler, r.HandleTime, r.HandleNote));

        menu.ShowAt(e.OriginalSource as FrameworkElement ?? (FrameworkElement)sender,
                    new FlyoutShowOptions { Position = e.GetPosition(e.OriginalSource as UIElement) });
    }

    /// <summary>构建统一的行右键菜单（只构建，不弹出）。</summary>
    private MenuFlyout BuildRowMenu<T>(List<T> checkedRows, T? currentRow, Func<T, string> wholeRowOf)
        where T : class
    {
        var menu = new MenuFlyout();

        // —— 复制（唯一已实现的功能）——
        var copyItem = new MenuFlyoutItem { Text = "复制", Icon = new SymbolIcon(Symbol.Copy) };
        copyItem.Click += (_, _) =>
        {
            if (checkedRows.Count > 0)
            {
                var err = CopyToClipboard(string.Join("\n", checkedRows.Select(wholeRowOf)));
                HintText.Text = err ?? $"已复制 {checkedRows.Count} 行（勾选行）";
            }
            else if (currentRow is not null)
            {
                var err = CopyToClipboard(wholeRowOf(currentRow));
                HintText.Text = err ?? $"已复制：{Trim(wholeRowOf(currentRow), 30)}";
            }
            else
            {
                HintText.Text = "没有可复制的行";
            }
        };
        menu.Items.Add(copyItem);

        menu.Items.Add(new MenuFlyoutSeparator());

        // —— 5 个占位功能（除复制外都先占位）——
        AddPlaceholder(menu, "轨迹回访", Symbol.Play);
        AddPlaceholder(menu, "添加到重点关注组", Symbol.Favorite);
        AddPlaceholder(menu, "打开视频", Symbol.Video);
        AddPlaceholder(menu, "查询车辆信息", Symbol.Find);
        AddPlaceholder(menu, "查询运单信息", Symbol.Document);

        return menu;
    }

    /// <summary>从点击源往上找 DataContext 是 T 的那一行（找不到返回 null）。</summary>
    private static T? CurrentRecord<T>(DependencyObject? start) where T : class
    {
        for (var d = start; d is not null; d = VisualTreeHelper.GetParent(d))
            if (d is FrameworkElement fe && fe.DataContext is T t) return t;
        return null;
    }

    /// <summary>加一个"尚未实现"的菜单项：禁用，让用户一眼看出还没做。</summary>
    private void AddPlaceholder(MenuFlyout menu, string text, Symbol icon)
    {
        var item = new MenuFlyoutItem
        {
            Text = text,
            Icon = new SymbolIcon(icon),
            IsEnabled = false,
        };
        // 无障碍：读屏用户能听到"该功能开发中"，而不只是"不可用"
        AutomationProperties.SetName(item, $"{text}（功能开发中）");
        menu.Items.Add(item);
    }

    private static string Trim(string s, int n)
        => s.Length <= n ? s : s[..n] + "…";

    /// <summary>从点击的原始来源往上找最近的那个 TextBlock（点在文字上或其容器上都命中）。</summary>
    private static TextBlock? FindTextBlock(DependencyObject? start)
    {
        for (var d = start; d is not null; d = VisualTreeHelper.GetParent(d))
            if (d is TextBlock tb) return tb;
        return null;
    }

    // ============================== 导出 ==============================

    /// <summary>
    /// 导出当前页签已加载的行为 CSV（UTF-8 BOM，Excel 直接打开不乱码）。
    /// 只导出已加载页是刻意的：3 万行全量导出应该走后端异步任务，
    /// 不该让前端在 UI 线程上拼一个几十兆的字符串。
    /// </summary>
    private void Export_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var dir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Vms.Client", "Export");
            Directory.CreateDirectory(dir);

            var alarmTab = ViewModel.Monitor.SelectedTabIndex == 1;
            var file = Path.Combine(dir, $"{(alarmTab ? "alarms" : "positions")}_{DateTime.Now:yyyyMMdd_HHmmss}.csv");

            var sb = new StringBuilder();
            if (alarmTab)
            {
                sb.AppendLine("车牌号,终端号,报警时间,报警类型,等级,报警速度,报警位置,处理状态,处理人,处理时间,处理说明");
                foreach (var r in ViewModel.Monitor.Alarms)
                {
                    sb.AppendLine(string.Join(',', Csv(r.Plate), Csv(r.DeviceCode), Csv(r.AlarmTime),
                        Csv(r.AlarmType), Csv(r.Level), Csv(r.SpeedText), Csv(r.Address),
                        Csv(r.HandleState), Csv(r.Handler), Csv(r.HandleTime), Csv(r.HandleNote)));
                }
            }
            else
            {
                sb.AppendLine("车牌号,终端号,定位时间,经度,纬度,速度,方向,海拔,里程,定位状态,在线,位置");
                foreach (var r in ViewModel.Monitor.Positions)
                {
                    sb.AppendLine(string.Join(',', Csv(r.Plate), Csv(r.DeviceCode), Csv(r.GpsTime),
                        Csv(r.LonText), Csv(r.LatText), Csv(r.SpeedText), Csv(r.DirectionText),
                        Csv(r.AltitudeText), Csv(r.MileageText), Csv(r.LocateState), Csv(r.OnlineText), Csv(r.Address)));
                }
            }

            File.WriteAllText(file, sb.ToString(), new UTF8Encoding(encoderShouldEmitUTF8Identifier: true));
            HintText.Text = $"已导出 → {file}";
        }
        catch (Exception ex)
        {
            HintText.Text = $"导出失败：{ex.Message}";
        }

        static string Csv(string value)
            => value.Contains(',') || value.Contains('"')
                ? "\"" + value.Replace("\"", "\"\"") + "\""
                : value;
    }
}

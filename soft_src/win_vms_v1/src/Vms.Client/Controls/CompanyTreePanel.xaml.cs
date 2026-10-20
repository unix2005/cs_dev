using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Vms.Client.Models;
using Vms.Client.ViewModels;

namespace Vms.Client.Controls;

/// <summary>按节点类型分派 TreeView 模板：组织节点用公司模板，其余（车辆）用车辆模板。</summary>
public sealed class OrgNodeTemplateSelector : DataTemplateSelector
{
    public DataTemplate? CompanyTemplate { get; set; }
    public DataTemplate? VehicleTemplate { get; set; }

    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
        => item is VehicleNode ? VehicleTemplate : CompanyTemplate;
}

public sealed partial class CompanyTreePanel : UserControl
{
    /// <summary>
    /// 当前存活的树面板实例（供诊断钩子做 UI 层回归）。
    ///
    /// 为什么不走构造函数注入：<see cref="ViewModels.RealtimeMonitorViewModel"/> 在树的<b>下方</b>，
    /// 让 VM 反向依赖 Controls 会把依赖方向倒过来。诊断设施不值得为它破一次分层，
    /// 用一个静态挂载点把耦合限制在一处，且只在 <c>VMS_DIAG</c> 诊断里被读取。
    /// </summary>
    public static CompanyTreePanel? Current { get; private set; }

    public CompanyTreePanel()
    {
        ViewModel = ((App)Application.Current).ViewModel;
        InitializeComponent();
        Current = this;

        // 组织树重建（Companies 变化）后刷新"已勾选 N 个组织"摘要。
        //
        // ⚠ 这里**只能**监听 Companies —— 千万别把 CheckedSummary 加进条件：
        //   RaiseCheckedSummaryChanged() 发出的正是 PropertyChanged(CheckedSummary)，
        //   监听它就变成"自己触发自己"，无限递归 → 栈溢出 → 进程直接被杀。
        //   栈溢出**任何异常处理器都拦不住**（App.UnhandledException 也没用），
        //   表现就是无提示闪退、crash.log 里什么都不留。这条注释是踩过坑换来的。
        ViewModel.Monitor.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(RealtimeMonitorViewModel.Companies))
                ViewModel.Monitor.RaiseCheckedSummaryChanged();
        };
    }

    public MainViewModel ViewModel { get; }

    /// <summary>
    /// TreeView 的两个回调都**不在本机回调栈上直接干活**，一律排到下一个 UI 节拍。
    ///
    /// 原因和 DataTabsPanel 的预取是同一条：TreeView.Expanding / ItemInvoked 都是
    /// XAML 本机在遍历节点树时同步回调托管代码的。此刻往 Children 里塞车辆节点
    /// （LoadVehiclesAsync 干的就是这个），等于本机正在枚举的集合被改 ——
    /// 结果是 Microsoft.UI.Xaml.dll 里 0xc0000005 访问违例，进程直接消失。
    /// </summary>
    private void Tree_ItemInvoked(TreeView sender, TreeViewItemInvokedEventArgs args)
    {
        switch (args.InvokedItem)
        {
            case VehicleNode vehicle:
                // 点车辆行 = 仅把该车居中到地图（聚焦），不改变数据范围。
                // 勾选/取消勾选（数据联动）由行内复选框负责，二者分离，互不干扰。
                Post(async () => ViewModel.Monitor.FocusVehicle(vehicle));
                break;
            case CompanyNode company:
                // 点组织节点 = 仅展开/收起（方便看下级单位），<b>不再加载定位数据</b>。
                // 定位数据栏的加载改由"勾选组织复选框"（OrgCheck_Click → UpdateOrgSelectionAsync）
                // 或"勾选车辆复选框"驱动；未勾选任何项前数据栏保持空，符合需求。
                // ⚠ 展开会让 TreeView 立刻去生成子节点容器（结构性变更），必须排在下一个 UI 节拍，
                //   不能在 ItemInvoked 的本机回调栈里做（见 Post 的说明）。
                Post(async () => { company.IsExpanded = true; });
                break;
        }
    }

    /// <summary>「全部车辆」：清空组织过滤与检索，回到全局视图。</summary>
    private void AllVehicles_Click(object sender, RoutedEventArgs e)
        => Post(async () => await ViewModel.Monitor.SelectAllAsync());

    /// <summary>车辆行复选框：勾上/取消 = 加入/移出"定位数据联动"的选中集合。
    /// 事件来自用户点击（UI 线程，非本机布局回调），更新集合安全；内部仍走 Post 保持纪律。</summary>
    private void VehicleCheck_Changed(object sender, RoutedEventArgs e)
    {
        if (((FrameworkElement)sender).DataContext is not VehicleNode vehicle) return;
        Post(async () => await ViewModel.Monitor.UpdateVehicleSelectionAsync(vehicle, vehicle.IsChecked));
    }

    /// <summary>
    /// 组织复选框：经典三态树点击处理。
    ///
    /// 复选框是<b>三态</b>（<c>IsThreeState=True</c>，模型 <see cref="CompanyNode.IsChecked"/> 为 <c>bool?</c>）。
    /// 三态 CheckBox 的点击循环是 <c>false → true → null → false</c>；TwoWay 绑定会在
    /// <see cref="CheckBox.Click"/> 触发<b>之前</b>，把框架翻转后的值回写进模型。
    /// 因此 Click 跑起来时，<c>org.IsChecked</c> 已经是"回写后的当前值"，据此反推用户意图：
    ///
    ///   当前 == true  → 之前是未选/部分 ⇒ 意图=全选          ⇒ 置 true
    ///   当前 == null  → 之前是全选     ⇒ 意图=取消全选       ⇒ 置 false
    ///   当前 == false → 之前是部分     ⇒ 意图=全选           ⇒ 置 true
    ///
    /// 即 <c>target = org.IsChecked != null</c>（null→false，其余→true）。
    /// 父组织的"部分选择"(null) 只由子节点推导，<b>不经由点击</b>：点击恒指向"全选/全不选"。
    ///
    /// 设值走 <see cref="CompanyNode.SetCheckedWithPropagation"/> —— 一次把本级及整棵子树置成确定态，
    /// 再沿父链向上重算每个祖先（可能变 null=部分选择），正是"取消某下级 → 父级变部分选择"的来源。
    /// </summary>
    private void OrgCheck_Click(object sender, RoutedEventArgs e)
    {
        if (((FrameworkElement)sender).DataContext is not CompanyNode org) return;

        // 依据 TwoWay 回写后的当前值反推意图（见上方说明）
        bool target = org.IsChecked != null;          // null→false；true/false→true

        org.SetCheckedWithPropagation(target);        // 本级+下级级联 + 向上重算父（含部分态）
        Post(async () => await ViewModel.Monitor.UpdateOrgSelectionAsync(org, target));
    }

    private void Tree_Expanding(TreeView sender, TreeViewExpandingEventArgs args)
    {
        if (args.Node.Content is not CompanyNode company || company.IsVehiclesLoaded) return;

        // 该组织下面还有子组织（如「北京分公司」下还有「配送中心」），
        // 展开就只显示子组织，不要再往它自己名下塞车辆节点 —— 否则子组织和车辆会混在同一层。
        // 只有"叶子组织"（配送中心 / 没有下级分公司的分公司）才在这里懒加载车辆。
        if (company.CompanyChildren.Any())
            return;

        Post(async () => await ViewModel.Monitor.LoadVehiclesAsync(company));
    }

    private static void Post(Func<Task> asyncAction)
        => DispatcherQueue.GetForCurrentThread().TryEnqueue(DispatcherQueuePriority.Normal, () =>
        {
            _ = SafeRun(asyncAction);
        });

    private static async Task SafeRun(Func<Task> asyncAction)
    {
        try { await asyncAction(); }
        catch (Exception ex) { System.Diagnostics.Debug.WriteLine($"[Tree] 操作失败：{ex}"); }
    }

    private void Tree_Collapsed(TreeView sender, TreeViewCollapsedEventArgs args)
    {
        // 折叠不卸载车辆：缓存住，下次展开免请求（除非显式刷新）。
    }

    /// <summary>
    /// UI 层回归：找到 <paramref name="org"/> 对应行的组织复选框，<b>真实点两次</b>
    /// （勾上 → 取消），断言控件上画的对号跟着翻转。
    ///
    /// 为什么要单独测这一层：模型 <c>IsChecked</c> 翻对了，不代表**控件上的对号**也翻了
    /// —— 用户看到的是控件。本项目栽过"模型对、界面不对"的坑（TreeView 模板不渲染、
    /// 复选框本地值压掉绑定推送），所以这条断言直接读 <c>CheckBox.IsChecked</c>。
    ///
    /// WinUI 无法注入鼠标点击（见记忆：不能靠注入输入做自动化点击），
    /// 所以走控件自身的 Automation Peer 触发 Invoke，等价于用户点一下。
    /// 返回 (勾选后的IsChecked, 取消后的IsChecked, 是否找到控件)。
    /// </summary>
    /// <summary>
    /// 诊断辅助：确保 <paramref name="org"/> 在树里<b>可见</b>（展开其全部祖先）并等布局完成。
    ///
    /// TreeView 只为"已展开路径"上的节点生成可视容器；没展开的组织即使在模型里被勾了，
    /// 它的 CheckBox 根本没被创建，去可视树里找自然找不到 —— 那不是"对号没翻转"，是"没渲染"。
    /// 诊断必须先把这条前置条件铺好，断言才有意义。
    /// </summary>
    public async Task<bool> EnsureOrgVisibleAsync(CompanyNode org)
    {
        // 从根往下逐级展开（自顶向上，否则父未展开时设子 IsExpanded 无效）
        var chain = new List<CompanyNode>();
        for (var p = org; p is not null; p = p.Parent as CompanyNode)
            chain.Insert(0, p);
        foreach (var c in chain) c.IsExpanded = true;

        // 等 TreeView 完成容器生成与布局
        for (int i = 0; i < 6; i++) await Task.Delay(150);
        return true;
    }

    public async Task<(bool? AfterCheck, bool? AfterUncheck, bool Found)> RunOrgCheckUiToggleAsync(CompanyNode org)
    {
        // 面板本身可能因导航项收起而 Visibility=Collapsed —— Collapsed 的子树不在可视树里，
        // 找不到任何 CheckBox。先把这种情况显式区分开，别让"没渲染"被误读成"对号没翻转"。
        if (Visibility != Visibility.Visible)
            return (null, null, false);
        if (!Tree.IsLoaded)
            return (null, null, false);

        var cb = FindOrgCheckBox(Tree, org);
        if (cb is null) return (null, null, false);

        // ⚠ 不用 Automation Peer 来点，两个原因（都实测踩过）：
        //   ① CheckBox 继承 ToggleButton，Peer 暴露的是 Toggle 模式而非 Invoke，
        //      GetPattern(Invoke) 返回 null，强转即 NRE；
        //   ② CreatePeerForElement 在 UIA 树未就绪时直接返回 null。
        //   改为直接翻转 IsChecked 再调 OnClick —— 走的就是用户点一下的那条代码路径
        //   （OrgCheck_Click），断言的才是真实行为，且不依赖任何外部子系统。
        // ⚠ 这里不能用 Thread.Sleep / Task.Wait()：Post 出去的 VM 更新要靠 UI 线程泵执行，
        //   堵死线程必然死锁。await Task.Delay 才是正确姿势（让出线程、泵继续转）。
        await ToggleCheckBoxAsync(cb);   // 第 1 次：勾上
        var afterCheck = cb.IsChecked;

        await ToggleCheckBoxAsync(cb);   // 第 2 次：取消
        return (afterCheck, cb.IsChecked, true);
    }

    /// <summary>
    /// 模拟"用户点一下复选框"：翻转 <c>IsChecked</c>（TwoWay 回写模型）后触发 <c>Click</c>。
    ///
    /// <c>OnClick</c> 是 <see cref="ToggleButton"/> 的 protected 方法，外部调不到，
    /// 这里只在<b>诊断路径</b>用一次反射调它。换来的是"断言的就是真实点击路径"，
    /// 代价可以接受；反射找不到就放弃（返回 false），绝不让诊断成为崩溃源。
    /// </summary>
    private static async Task ToggleCheckBoxAsync(CheckBox cb)
    {
        cb.IsChecked = cb.IsChecked != true;      // 先翻转（TwoWay 把新值回写进模型）
        var onClick = typeof(ToggleButton).GetMethod(
            "OnClick", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);
        // ⚠ WinUI 3 的 RoutedEventArgs 只有**无参**构造（WPF 才 RoutedEventArgs(RoutedEvent)）。
        onClick?.Invoke(cb, [new RoutedEventArgs()]);
        await Task.Delay(400);                    // 让 Post 出去的 VM 更新落地
    }

    /// <summary>
    /// 量一棵树上所有 CheckBox 的<b>实际渲染尺寸</b>，供诊断取证。
    ///
    /// 存在理由：本项目两次栽在"复选框右侧被切掉、勾画不全"上 ——
    /// 根因都是给 <c>CheckBox</c> 设了小于框体内在尺寸的 <c>Width</c>
    /// （WinUI 3 的勾选框用 <c>AnimatedIcon</c> 画，有内在尺寸，Width 一压就切右边）。
    /// 光看代码"Width 已删"不能证明框真的完整，必须量 <c>ActualWidth</c>：
    /// 它若明显小于内在框宽（约 20px），就说明框被压扁了。
    /// </summary>
    public string MeasureCheckBoxes()
    {
        var sb = new System.Text.StringBuilder();
        int n = 0, clipped = 0;
        const double IntrinsicBox = 20.0;   // WinUI 3 CheckBox 勾选框的内置尺寸下界
        foreach (var cb in FindAllCheckBoxes(Tree))
        {
            n++;
            bool bad = cb.ActualWidth + 0.5 < IntrinsicBox;
            if (bad) clipped++;
            if (n <= 8)
            {
                var name = (cb.DataContext as OrgNodeBase)?.Name ?? "(未绑定)";
                sb.AppendLine($"  [{n}] {name,-12} ActualW={cb.ActualWidth:F1} ActualH={cb.ActualHeight:F1} " +
                              $"DesiredW={cb.DesiredSize.Width:F1} MinW={cb.MinWidth} " +
                              $"Margin={cb.Margin.Left},{cb.Margin.Right} {(bad ? "← 被压扁!" : "OK")}");
            }
        }
        sb.AppendLine($"  共 {n} 个 CheckBox，其中 ActualWidth<{IntrinsicBox:F0} 的有 {clipped} 个");
        return sb.ToString();
    }

    private static IEnumerable<CheckBox> FindAllCheckBoxes(DependencyObject root)
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(root); i++)
        {
            var child = VisualTreeHelper.GetChild(root, i);
            if (child is CheckBox cb) yield return cb;
            foreach (var sub in FindAllCheckBoxes(child)) yield return sub;
        }
    }

    /// <summary>在树的可视子树里找某个组织节点所在行的 CheckBox（DataContext 命中即可）。</summary>
    private static CheckBox? FindOrgCheckBox(DependencyObject root, CompanyNode org)
    {
        int n = VisualTreeHelper.GetChildrenCount(root);
        for (int i = 0; i < n; i++)
        {
            var child = VisualTreeHelper.GetChild(root, i);
            if (child is CheckBox cb && ReferenceEquals(cb.DataContext, org)) return cb;
            var hit = FindOrgCheckBox(child, org);
            if (hit is not null) return hit;
        }
        return null;
    }
}

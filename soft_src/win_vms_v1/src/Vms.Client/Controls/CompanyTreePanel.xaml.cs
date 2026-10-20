using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Vms.Client.Diagnostics;
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

    /// <summary>
    /// 车辆行复选框：勾上/取消 = 加入/移出"定位数据联动"的选中集合。
    ///
    /// ⚠⚠ **必须挂 <c>Click</c>，绝不能挂 <c>Checked</c>/<c>Unchecked</c>**（本项目正是由此栽了一个
    /// "点了组织没对号、下级车辆也没选中"的坑，代价是一整轮排查）：
    ///   · <c>Checked</c>/<c>Unchecked</c> 在**值变化**时触发，**程序化赋值也会触发**；
    ///   · <c>Click</c> 只在**用户交互**（鼠标/键盘/OnClick）时触发，程序化设 <c>IsChecked</c> 不触发。
    /// 而"勾选组织 → 级联把子树车辆置勾选"正是程序化赋值（<c>SetTreeVehiclesChecked</c>）。
    /// 挂了 Checked/Unchecked 的后果：级联每置一台车就回调一次 ——
    ///   ① <c>RecomputeAncestors()</c> 按"车辆大多没勾"把父组织重算成 false/null，
    ///      **把用户刚勾上的对号当场冲掉**（叶子组织必现，因为只有叶子组织才挂车辆节点）；
    ///   ② <c>UpdateVehicleSelectionAsync</c> 把范围改成"按车辆 id"，把组织范围顶掉，数据栏随之清空。
    ///
    /// 事件来自用户点击（UI 线程，非本机布局回调），更新集合安全；内部仍走 Post 保持纪律。
    /// 勾选/取消一台车后，要沿父链重算祖先组织 —— 父组织随之变"部分选择"(null) 或回满/回空，
    /// 这是三态树"单独勾几台车 → 父组织显示半选"的关键。
    /// </summary>
    private void VehicleCheck_Click(object sender, RoutedEventArgs e)
    {
        if (((FrameworkElement)sender).DataContext is not VehicleNode vehicle) return;
        // TwoWay 绑定已在 Click 之前把控件翻转后的值回写进模型，直接读模型即可
        bool selected = vehicle.IsChecked == true;
        Diag.Log($"[VehicleCheck_Click] 车={vehicle.Name} selected={selected}");
        vehicle.RecomputeAncestors();                 // 父组织可能变为部分选择
        Post(async () => await ViewModel.Monitor.UpdateVehicleSelectionAsync(vehicle, selected));
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
        if (((FrameworkElement)sender).DataContext is not CompanyNode org)
        {
            // 这条打点专治"点了没反应"：DataContext 不是组织时 handler 会静默退出，
            // 而控件自身已经翻转过，肉眼看就是"点了没变化"，不落盘根本查不出来。
            Diag.Log($"[OrgCheck_Click] 提前退出：DataContext={((FrameworkElement)sender).DataContext?.GetType().Name ?? "null"}");
            return;
        }

        var cb = sender as CheckBox;
        Diag.Log($"[OrgCheck_Click] 进入 org={org.Name}(Id={org.Id}) " +
                 $"cb.IsChecked={Str(cb?.IsChecked)} 模型.IsChecked={Str(org.IsChecked)} " +
                 $"IsThreeState={cb?.IsThreeState}");

        // 依据 TwoWay 回写后的当前值反推意图（见上方说明）
        bool target = org.IsChecked != null;          // null→false；true/false→true

        org.SetCheckedWithPropagation(target);        // 本级+下级级联 + 向上重算父（含部分态）
        Diag.Log($"[OrgCheck_Click] target={target} 传播后 模型={Str(org.IsChecked)} cb={Str(cb?.IsChecked)}");

        // ⚠ 显式把控件对齐到推导出的确定态：三态 CheckBox 的点击循环会在 Click 之前把控件翻成 null，
        // 而 TwoWay 把 null 回写进模型后、模型再被设为 target 时，控件未必及时刷回 target
        // （实测表现为"取消勾选后控件停在半选 null"）。这里直接兜底，确保"用户看到的对号"=模型值。
        if (cb is not null && cb.IsChecked != target)
        {
            cb.IsChecked = target;
            Diag.Log($"[OrgCheck_Click] 控件对齐到 {target}");
        }

        Diag.Log($"[OrgCheck_Click] 结束 模型={Str(org.IsChecked)} cb={Str(cb?.IsChecked)}");
        Post(async () => await ViewModel.Monitor.UpdateOrgSelectionAsync(org, target));
    }

    /// <summary>把 <c>bool?</c> 打成可读串（null 要显式可见 —— 三态里 null 是"部分选择"）。</summary>
    private static string Str(bool? b) => b is null ? "null" : b.Value ? "True" : "False";

    // （沿继承链查方法的 FindMethodInHierarchy 见下方"真实输入注入"区）

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
    /// 模拟"用户点一下组织复选框"：先按 WinUI 三态 CheckBox 的真实点击循环翻转控件值
    /// （<c>false → true → null → false</c>，TwoWay 把新值回写进模型），再直接调用
    /// <see cref="OrgCheck_Click"/>（同一份点击逻辑，不经过 <c>ToggleButton.OnClick</c> 的二次翻转）。
    ///
    /// ⚠ 不能反射调 <c>OnClick</c>：OnClick 内部会再翻转一次 IsChecked，导致"双重翻转"把结果测反，
    /// 那不是用户真实点击路径（真实点击由框架先翻转、再 Raise Click，OnClick 不再翻）。
    /// 直接调 OrgCheck_Click 才等价于用户的真实点击行为，断言才可信。
    /// </summary>
    private async Task ToggleCheckBoxAsync(CheckBox cb)
    {
        cb.IsChecked = cb.IsChecked switch            // 复刻三态点击循环（框架在点击时先翻这一步）
        {
            false => true,
            true => null,
            null => false,
        };
        // 直接走点击处理逻辑（与用户真实点击同一条代码路径，无二次翻转）
        OrgCheck_Click(cb, new RoutedEventArgs());
        await Task.Delay(400);                    // 让 Post 出去的 VM 更新落地
    }

    /// <summary>
    /// <c>VMS_DIAG=orgclick</c>：用<b>框架自己的点击处理器</b>（反射调 <c>ToggleButton.OnClick</c>）
    /// 真实点某个组织的复选框三次，逐步记录"控件值 / 模型值 / 子树车辆勾选数 / 数据栏条数"。
    ///
    /// 为什么必须走 <c>OnClick</c> 而不是"手动翻转 + 调 handler"：
    /// 真实点击的唯一入口就是 <c>OnClick</c> —— 它负责翻转 <c>IsChecked</c>、把值回写给绑定源、
    /// 再 Raise <c>Click</c>。手动翻转会绕开"翻转 → 回写 → Click"这个真实时序，
    /// 测出来的结论与真机不符（本项目就是这么被误导过一轮：诊断全 PASS，用户实机点不动）。
    /// </summary>
    public async Task<string> RunOrgClickProbeAsync()
    {
        var sb = new System.Text.StringBuilder();
        var all = ViewModel.Monitor.Companies
                           .SelectMany(c => c.DescendantsAndSelf()).OfType<CompanyNode>().ToList();

        // 覆盖三类节点 —— 只测根节点会漏掉真实故障点（用户点的"一公司"是叶子组织）：
        //   ① 根（18714 台，走 CompanyId 整树）；
        //   ② 叶子组织且车辆 >256（862 台，同样走 CompanyId 整树，但**有父**）；
        //   ③ 叶子组织且车辆 ≤256（走 Ids 精确过滤，且会加载车辆到树上）；
        //   ④ 有下级组织的分公司（验证级联）。
        var targets = new List<CompanyNode>();
        CompanyNode? Pick(Func<CompanyNode, bool> f) => all.FirstOrDefault(f);
        var root = all.FirstOrDefault(c => c.Parent is null);
        var bigLeaf = Pick(c => !c.CompanyChildren.Any() && c.Count > VehicleQuery.MaxIdFilter);
        var smallLeaf = Pick(c => !c.CompanyChildren.Any() && c.Count > 0 && c.Count <= VehicleQuery.MaxIdFilter);
        var withKids = Pick(c => c.CompanyChildren.Any() && c.Parent is not null);
        foreach (var t in new[] { root, bigLeaf, smallLeaf, withKids })
            if (t is not null && !targets.Contains(t)) targets.Add(t);

        if (targets.Count == 0) { sb.AppendLine("树里没有组织节点，探针放弃"); return sb.ToString(); }

        var hwnd = System.Diagnostics.Process.GetCurrentProcess().MainWindowHandle;
        sb.AppendLine($"主窗口 hwnd=0x{hwnd.ToInt64():X}（0=取不到句柄）");
        if (hwnd != IntPtr.Zero)
        {
            ShowWindow(hwnd, 9);               // SW_RESTORE：最小化时先还原，否则前台设置无效
            sb.AppendLine($"SetForegroundWindow={SetForegroundWindow(hwnd)}");
        }
        await Task.Delay(300);

        foreach (var org in targets)
        {
            sb.AppendLine();
            sb.AppendLine($"########## 目标：{org.Name}(Id={org.Id}) 车辆总数={org.Count} " +
                          $"下级组织={org.CompanyChildren.Count()} 有父={org.Parent is not null}");
            await ProbeOneOrgAsync(sb, org);
        }

        // 收尾：复位后勾一个<b>叶子组织</b>并展开它，便于截图肉眼确认
        // "勾组织 → 对号出现 + 下级车辆全部勾上" 这两件事（用户实机反馈的两个点）。
        var demo = bigLeaf ?? root;
        if (demo is not null)
        {
            await ViewModel.Monitor.ResetTreeChecksAsync();
            await EnsureOrgVisibleAsync(demo);
            demo.SetCheckedWithPropagation(true);
            await ViewModel.Monitor.UpdateOrgSelectionAsync(demo, true);
            await EnsureOrgVisibleAsync(demo);
            int loaded = demo.DescendantsAndSelf().OfType<VehicleNode>().Count(v => v.Id != 0);
            int picked = demo.DescendantsAndSelf().OfType<VehicleNode>().Count(v => v.Id != 0 && v.IsChecked == true);
            sb.AppendLine();
            sb.AppendLine($"########## 收尾演示（截图用）：{demo.Name}(Id={demo.Id}) 已勾选，" +
                          $"展开后车辆应全部勾上：{picked}/{loaded}，数据栏 Feed.Count={ViewModel.Monitor.Feed.Count}");
        }
        return sb.ToString();
    }

    /// <summary>对单个组织做一次"复位 → 真实点击 → 记录"，把每一层的表现都摆出来。</summary>
    private async Task ProbeOneOrgAsync(System.Text.StringBuilder sb, CompanyNode org)
    {
        await EnsureOrgVisibleAsync(org);

        var cb = FindOrgCheckBox(Tree, org);
        sb.AppendLine($"找到行内复选框(Tag=OrgSelect)：{cb is not null}" +
                      (cb is null ? "" : $" IsThreeState={cb.IsThreeState} IsEnabled={cb.IsEnabled}"));
        if (cb is null) { sb.AppendLine("    找不到该行的复选框（未渲染？），跳过"); return; }

        // —— 第一组自检：绑定方向 ——
        // "点了没对号"有两类根因：① 点击循环顺序与我假设的不同；② 绑定压根没生效（方向不对）。
        // 先花两步把绑定排除掉，后面的点击结果才好解读。
        sb.AppendLine("--- 绑定自检 A（控件→模型，验 TwoWay 回写）");
        cb.IsChecked = true;
        await Task.Delay(150);
        sb.AppendLine($"    设 cb=True 后 模型={Str(org.IsChecked)}（True=回写正常，否则绑定没生效）");

        sb.AppendLine("--- 绑定自检 B（模型→控件，验推送方向）");
        org.SetSubtreeChecked(false);
        await Task.Delay(150);
        var afterFalse = cb.IsChecked;
        org.SetSubtreeChecked(true);
        await Task.Delay(150);
        sb.AppendLine($"    设模型=False 后 cb={Str(afterFalse)}；设模型=True 后 cb={Str(cb.IsChecked)}" +
                      $"（跟着变=推送正常）");

        // 复位到"未选"，让接下来的点击从干净初值开始
        await ViewModel.Monitor.ResetTreeChecksAsync();
        await Task.Delay(250);
        sb.AppendLine($"--- 复位后：cb={Str(cb.IsChecked)} 模型={Str(org.IsChecked)}");

        // —— 真实键盘输入（空格切换 CheckBox），走的就是用户输入那条路径 ——
        cb.Focus(FocusState.Programmatic);
        sb.AppendLine($"给复选框设焦点：FocusState={cb.FocusState}");
        sb.AppendLine($"--- 点击前：cb={Str(cb.IsChecked)} 模型={Str(org.IsChecked)}");
        PressSpace();                     // 真实键盘事件（keydown + keyup）
        await Task.Delay(800);            // 让 Post 出去的 VM 联动落地

        int checkedVehicles = org.DescendantsAndSelf().OfType<VehicleNode>()
                                 .Count(v => v.Id != 0 && v.IsChecked == true);
        int loadedVehicles = org.DescendantsAndSelf().OfType<VehicleNode>().Count(v => v.Id != 0);
        int kidsChecked = org.CompanyChildren.Count(c => c.IsChecked == true);
        var q = ViewModel.Monitor.Feed.Query;
        sb.AppendLine($"--- 点击后：cb={Str(cb.IsChecked)} 模型={Str(org.IsChecked)} " +
                      $"下级组织已勾={kidsChecked}/{org.CompanyChildren.Count()} " +
                      $"子树车辆(已加载/已勾)={loadedVehicles}/{checkedVehicles} " +
                      $"Feed.Count={ViewModel.Monitor.Feed.Count} Ids={q.Ids.Count} " +
                      $"CompanyId={q.CompanyId} NoResults={q.NoResults}");
    }

    // ---------------- 真实输入注入（仅诊断路径使用） ----------------

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, IntPtr dwExtraInfo);

    private const byte VkSpace = 0x20;
    private const uint KeyEventFKeyUp = 0x0002;

    /// <summary>发一个真实的空格键（按下 + 抬起）。空格是 CheckBox 的标准切换键，
    /// 走的是与鼠标点击同一条框架路径（OnToggle → 翻转 → 回写 → Raise Click）。</summary>
    private static void PressSpace()
    {
        keybd_event(VkSpace, 0, 0, IntPtr.Zero);
        keybd_event(VkSpace, 0, KeyEventFKeyUp, IntPtr.Zero);
    }

    /// <summary>保留：沿继承链逐级查找实例方法（含非公共）的反射助手。
    /// 当前 WinUI 3 的 C# 投影里并没有 <c>OnClick</c>（WinRT 不暴露 protected 成员），
    /// 所以真实点击只能靠输入注入，但这个方法留着做取证时的备选手段。
    ///
    /// <c>Type.GetMethod(name, NonPublic|Instance)</c> 只在<b>当前类型</b>里找非公共成员，
    /// 基类的 protected 方法永远搜不到 —— 必须用 DeclaredOnly 逐级向上。
    /// </summary>
    private static System.Reflection.MethodInfo? FindMethodInHierarchy(Type type, string name)
    {
        const System.Reflection.BindingFlags flags =
            System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public |
            System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.DeclaredOnly;
        for (var t = type; t is not null; t = t.BaseType)
        {
            var m = t.GetMethod(name, flags);
            if (m is not null) return m;
        }
        return null;
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

    /// <summary>在树的可视子树里找某个组织节点<b>所在行我们的</b>复选框。
    /// ⚠ 必须同时匹配 <c>DataContext</c> 与 <c>Tag="OrgSelect"</c>：TreeViewItem 自带一个隐藏的多选框
    /// （Margin=10,0、MinWidth=32、平时 Opacity=0），它的 <c>DataContext</c> 也继承自节点，
    /// 会先于我们的行内复选框被找到，导致测到的是内置框、根本没走 <see cref="OrgCheck_Click"/>。</summary>
    private static CheckBox? FindOrgCheckBox(DependencyObject root, CompanyNode org)
    {
        int n = VisualTreeHelper.GetChildrenCount(root);
        for (int i = 0; i < n; i++)
        {
            var child = VisualTreeHelper.GetChild(root, i);
            if (child is CheckBox cb && ReferenceEquals(cb.DataContext, org) &&
                (cb.Tag as string) == "OrgSelect") return cb;
            var hit = FindOrgCheckBox(child, org);
            if (hit is not null) return hit;
        }
        return null;
    }
}

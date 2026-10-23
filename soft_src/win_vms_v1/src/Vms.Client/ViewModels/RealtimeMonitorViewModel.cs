using System.Collections.ObjectModel;
using Microsoft.UI.Dispatching;
using Vms.Client.Diagnostics;
using Vms.Client.Interop;
using Vms.Client.Models;
using Vms.Client.Services;

namespace Vms.Client.ViewModels;

/// <summary>「实时监控」主页的视图模型：组织树 + 选中联动 + 地图打点 + 下方定位/报警数据页签。</summary>
public sealed class RealtimeMonitorViewModel : ObservableObject, IDisposable
{
    private readonly IVehicleDataSource _source;
    private readonly DispatcherQueue? _uiQueue;
    private readonly DispatcherQueueTimer? _statsTimer;

    private int _selectedTabIndex;
    private string _loadStateText = "尚未加载";
    private string _coordinateText = "经度:-- 纬度:--";
    private string _searchKeyword = string.Empty;
    private string _mapHint = "地图图层待接入合规地图服务（高德/腾讯/百度/天地图）";
    private uint _selectedCompanyId;
    private long _pushFrameCount;
    private double _lastLon;
    private double _lastLat;
    private string _treeSummary = "0 台";
    private string _scopeText = "当前范围：未选择（勾选车辆或组织机构以加载）";

    // —— 范围状态机 ——
    // 数据栏的"范围"由"选中集合"决定，优先级自上而下：
    //   1) 树上勾选的具体车辆（_selectedVehicleIds）—— 走 Ids 精确过滤，上限 256 台；
    //   2) 勾选的组织（_scopeCompanyId）—— 走该组织含下级的整棵子树过滤，可容纳任意规模；
    //   3) 检索关键词（_scopeKeyword）—— 关键词过滤；
    //   4) 全空（默认）—— NoResults，数据栏保持空、不加载任何车（需求：未勾选前不加载）。
    // 勾选组织时会把其子树车辆收进 _selectedVehicleIds（小组织）或直接用 CompanyId 整树（大组织）；
    // _orgAddedIds 记录每个组织勾选时贡献了哪些 id，便于取消勾选时精确回退。
    private uint _scopeCompanyId;
    private string _scopeCompanyName = string.Empty;
    private string _scopeKeyword = string.Empty;
    // v5：选中集合存的是**库表主键字符串** d_vehicleinfo.vehicle_id（"车牌-颜色"），
    // 不再是 uint32 行号 —— 行号只是本机 UI 序号，跨进程/跨请求不稳定，服务端也不认。
    private readonly HashSet<string> _selectedVehicleIds = new();
    private readonly Dictionary<uint, List<string>> _orgAddedIds = new();
    private VehicleNode? _lastToggledVehicle;

    /// <summary>懒加载车辆时单组织的上限。组织下可能有上万车，树里不可能全放，先给一个能看的量。</summary>
    private const int VehicleTreePageSize = 200;

    // 定位 / 报警记录的就地更新索引（避免每帧全量重建集合）
    private readonly Dictionary<uint, PositionRecord> _posIndex = new(2048);
    private readonly Dictionary<string, AlarmRecord> _alarmIndex = new(2048);

    // 待提交的报警（累积一小批再插集合，避免每帧都在 ListView 头部硬插）
    private readonly List<AlarmRecord> _alarmPending = new(64);
    private bool _alarmFlushScheduled;

    // 心跳计数：每 6 次（约 30 秒）写一次运行状态，供"进程悄悄没了"时事后取证
    private readonly DateTime _startedAt = DateTime.Now;
    private int _heartbeatTicks;

    public RealtimeMonitorViewModel(IVehicleDataSource source, DispatcherQueue? uiQueue)
    {
        _source = source;
        _uiQueue = uiQueue;
        Feed = new VehicleFeed(source, new VehicleQuery { Limit = 200 });

        // ⚠ StateChanged 里的动作必须排队（见 Post 的说明）：
        //   它会 Clear()+重建 Positions，而触发它的是 LoadMoreAsync ——
        //   后者最典型的调用点正是 ListView 的 ContainerContentChanging 回调。
        Feed.StateChanged += (_, _) => Post(() => { RefreshLoadState(); RebuildPositions(); });

        _source.VehiclesPushed += OnVehiclesPushed;
        // M5：报警推送（与轨迹独立）。无界面消费时同样丢弃本帧（见 OnVehiclesPushed 同款纪律）。
        _source.AlarmsPushed += OnAlarmsPushed;

        Positions = new ObservableCollection<PositionRecord>();
        Alarms = new ObservableCollection<AlarmRecord>();

        if (uiQueue is not null)
        {
            _statsTimer = uiQueue.CreateTimer();
            _statsTimer.Interval = TimeSpan.FromSeconds(5);
            _statsTimer.Tick += (_, _) => { RefreshLoadState(); WriteHeartbeat(); };
            _statsTimer.Start();
        }
    }

    /// <summary>
    /// 「结构性集合变更」的统一出口：排到下一个 UI 节拍再执行，而不是就地执行。
    ///
    /// ── 为什么必须有这一层（本项目踩过的最疼的坑）───────────────────────
    /// XAML 有相当多的事件是**本机代码在布局 / 命中测试 / 容器生成过程中同步回调托管代码**：
    ///   ListViewBase.ContainerContentChanging、TreeView.Expanding、SelectorBar.SelectionChanged …
    /// 在这些回调里 Clear() / Insert() 一个正在被 ItemsControl 枚举的集合，
    /// 等于本机栈还没退回时把脚下的地板抽掉，结果是：
    ///   Microsoft.UI.Xaml.dll 里 0xc0000005 访问违例 → 进程瞬间消失。
    /// 这种崩溃**托管层完全拦不住** —— try/catch、App.UnhandledException 都不会被通知，
    /// crash.log 一片空白，现场只剩"程序自己退出了"这一句描述。
    ///
    /// 排到下一个消息循环再改集合，那时 XAML 的回调栈已经退干净，改集合才是安全的。
    /// ────────────────────────────────────────────────────────────────
    /// </summary>
    private void Post(Action action)
    {
        if (_uiQueue is null)
        {
            action();
            return;
        }
        _uiQueue.TryEnqueue(DispatcherQueuePriority.Normal, () =>
        {
            try { action(); }
            catch (Exception ex) { System.Diagnostics.Debug.WriteLine($"[Monitor] 延期任务失败：{ex}"); }
        });
    }

    // ------------------------------- 绑定属性 -------------------------------

    public ObservableCollection<CompanyNode> Companies { get; } = [];
    public VehicleFeed Feed { get; }
    public ObservableCollection<PositionRecord> Positions { get; }
    public ObservableCollection<AlarmRecord> Alarms { get; }

    public int SelectedTabIndex
    {
        get => _selectedTabIndex;
        set => SetProperty(ref _selectedTabIndex, value);
    }

    public string LoadStateText { get => _loadStateText; private set => SetProperty(ref _loadStateText, value); }

    public bool IsLoading => Feed.IsLoading;

    /// <summary>地图下方的坐标读数（对应截图左下角「经度:113.572906 纬度:42.443101」）。</summary>
    public string CoordinateText { get => _coordinateText; private set => SetProperty(ref _coordinateText, value); }

    public string MapHint { get => _mapHint; set => SetProperty(ref _mapHint, value); }

    public string SearchKeyword { get => _searchKeyword; set => SetProperty(ref _searchKeyword, value); }

    public uint SelectedCompanyId { get => _selectedCompanyId; private set => SetProperty(ref _selectedCompanyId, value); }

    /// <summary>左侧树底部 / 顶部展示的"已加载车辆数"摘要。</summary>
    public string TreeSummary { get => _treeSummary; private set => SetProperty(ref _treeSummary, value); }

    /// <summary>右侧数据页签上方展示的当前过滤范围。</summary>
    public string ScopeText { get => _scopeText; private set => SetProperty(ref _scopeText, value); }

    public VehicleNode? SelectedVehicle { get; private set; }

    /// <summary>当前在树上勾选的车辆数（0 = 未勾选，数据栏走组织/全局范围）。</summary>
    public int SelectedVehicleCount => _selectedVehicleIds.Count;

    /// <summary>树底部"已选 N 台车 · 定位数据已联动"提示；未勾选时为空。</summary>
    public string SelectionSummary =>
        _selectedVehicleIds.Count == 0 ? string.Empty : $"已选 {_selectedVehicleIds.Count} 台车 · 定位数据已联动";

    /// <summary>
    /// 是否有界面正在消费 <see cref="Positions"/> / <see cref="Alarms"/>。
    /// 页面被 Frame 卸载（切到别的模块）后必须由页面置 false：
    /// 那两集合仍挂在已销毁的 ListView 上，继续往里 Insert/Clear 就是在操作
    /// 已经不存在的视觉树节点，同样会打出 0xc0000005。
    /// </summary>
    public bool IsViewActive { get; private set; } = true;

    /// <summary>页面卸载：停止向集合写数据。</summary>
    public void DeactivateView() => IsViewActive = false;

    /// <summary>页面重新加载：恢复写入并补齐这一段"缺席期间"的数据。</summary>
    public void ActivateView()
    {
        IsViewActive = true;
        RebuildAllDeferred();
    }

    /// <summary>报警页签当前是否在显示（默认显示的是定位页签，所以初始为 false）。</summary>
    private bool _alarmTabVisible;

    /// <summary>
    /// 数据页签切换（由 DataTabsPanel 告知）。
    ///
    /// 为什么 VM 需要知道这个：
    ///   报警集合绑在报警页签的 ListView 上，而该页签没被选中时是 Collapsed 的。
    ///   往一个不可见、没有有效布局状态的 ListView 持续做**结构性**变更（Insert/Remove），
    ///   是在跟 XAML 的布局状态较劲 —— 属于崩溃高发区（表现同样是 0xC0000005）。
    ///   所以策略是：不可见时只把报警攒进待提交队列，等用户真的切过去，一次性重建。
    /// </summary>
    public void NotifyTabChanged(int index) => Post(() =>
    {
        var visible = index == 1;
        if (visible == _alarmTabVisible) return;
        _alarmTabVisible = visible;

        // 切到报警页签：此刻它已可见，在"可见状态"下重建是安全的
        if (visible) RebuildAlarmsFromFeed();
    });

    /// <summary>已勾选（用于批量下发/导出的）组织节点数。</summary>
    public string CheckedSummary
    {
        get
        {
            var leaf = Companies.SelectMany(c => c.DescendantsAndSelf()).OfType<CompanyNode>()
                                .Where(n => !n.CompanyChildren.Any() && n.IsChecked == true)
                                .ToList();
            var vehicles = leaf.Sum(n => (long)n.Count);
            return leaf.Count == 0 ? "未勾选组织" : $"已勾选 {leaf.Count} 个组织，覆盖 {vehicles:N0} 台车";
        }
    }

    // ------------------------------- 初始化 --------------------------------

    public async Task InitializeAsync(CancellationToken ct = default)
    {
        var records = await _source.GetCompaniesAsync(ct);
        Companies.Clear();
        foreach (var root in CompanyTreeBuilder.Build(records))
            Companies.Add(root);

        // ⚠ 启动即空：未勾选任何车辆/组织前，定位数据栏不加载（需求：未勾选前不加载车辆）。
        // 之前的"启动先 ResetAsync 拉全量"会一上来就把 3 万台车灌进列表，已移除。
        RebuildAllDeferred();
        RaiseCheckedSummaryChanged();
        RaiseScopeChanged();
        WriteTreeSnapshot();

        // 诊断开关 VMS_DIAG=treetest：自动展开"北京分公司 → 第一个配送中心"并选中它。
        // 用于无人值守的 UI 回归取证（配合截图脚本），也用来验证"父组织过滤 = 整个子树"这条语义。
        if (Diag.TreeSmokeTest)
        {
            Post(() =>
            {
                var parent = Companies.SelectMany(c => c.CompanyChildren)
                                      .FirstOrDefault(c => c.CompanyChildren.Any());
                if (parent is null) return;
                parent.IsExpanded = true;
                var leaf = parent.CompanyChildren.FirstOrDefault();
                if (leaf is not null)
                {
                    leaf.IsExpanded = true;
                    _ = LoadVehiclesAsync(leaf);
                }
                _ = SelectCompanyAsync(parent.Id, parent.Name);
            });
        }

        // 诊断开关 VMS_DIAG=treecheck：自动展开一个叶子组织、加载车辆、勾选其中 2 台，
        // 断言 Feed 与定位数据栏（Positions）只含这 2 台，验证"树上勾选 → 数据联动"链路。
        // 用于无人值守回归取证（配合截图脚本），替代无法注入的鼠标点击。
        if (Diag.VehicleSelectionCheck)
        {
            Post(() => _ = RunVehicleSelectionCheckAsync());
        }

        // 诊断开关 VMS_DIAG=orgclick：用框架自己的点击处理器真实点三次组织复选框，
        // 逐步记录控件值/模型值/联动结果（写 diagtrace.log + orgclick.log）。
        if (Diag.OrgClickProbe)
        {
            Post(() => _ = RunOrgClickProbeAsync());
        }

        // 诊断开关 VMS_DIAG=tristate：找一个含≥2下级的组织，勾选其中 1 个下级，
        // 让该父组织变成"部分选择"(null)，并展开它（使其与已勾下级都可见），
        // 用于截图验证"父组织半选框"这一经典三态表现。仅改模型态，不联动数据栏，纯展示用。
        if (Diag.TriStateDemo)
        {
            Post(() =>
            {
                var parent = Companies.SelectMany(c => c.CompanyChildren)
                                      .FirstOrDefault(p => p.CompanyChildren.Count() >= 2);
                if (parent is null) return;
                parent.IsExpanded = true;                                  // 展开父，使下级可见
                var firstChild = parent.CompanyChildren.First();
                firstChild.SetCheckedWithPropagation(true);                // 勾 1 个下级 → 父变部分选择(null)
            });
        }
    }

    /// <summary>
    /// 把树上的组织勾选、车辆勾选、选中集与组织范围<b>全部复位到未选</b>。
    /// 供诊断探针在多个目标之间反复做"干净初值 → 点击 → 断言"。
    /// </summary>
    public async Task ResetTreeChecksAsync()
    {
        foreach (var c in Companies) c.SetSubtreeChecked(false);
        foreach (var v in Companies.SelectMany(c => c.DescendantsAndSelf()).OfType<VehicleNode>())
            v.IsChecked = false;
        _selectedVehicleIds.Clear();
        _orgAddedIds.Clear();
        _scopeCompanyId = 0;
        _scopeCompanyName = string.Empty;
        SelectedCompanyId = 0;
        OnPropertyChanged(nameof(SelectedVehicleCount));
        OnPropertyChanged(nameof(SelectionSummary));
        await ApplyScopeAsync(CancellationToken.None);
    }

    /// <summary>
    /// <c>VMS_DIAG=orgclick</c> 的无人值守探针：让 UI 层用<b>框架自己的点击处理器</b>连点三次
    /// 组织复选框，把每步的控件值 / 模型值 / 联动结果写到 <c>%LocalAppData%\Vms.Client\orgclick.log</c>
    /// （点击处理内部的打点同步写 diagtrace.log）。
    ///
    /// 目的：三态 CheckBox 的点击循环顺序与 TwoWay 回写时机都是运行期行为，读代码推不出来，
    /// 只能实测。用来回答"为什么真机点了没对号"这类问题。
    /// </summary>
    private async Task RunOrgClickProbeAsync()
    {
        try
        {
            var panel = Controls.CompanyTreePanel.Current;
            var text = panel is null
                ? "树面板尚未创建（CompanyTreePanel.Current 为 null）"
                : await panel.RunOrgClickProbeAsync();

            var dir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Vms.Client");
            Directory.CreateDirectory(dir);
            File.WriteAllText(Path.Combine(dir, "orgclick.log"), text,
                              new System.Text.UTF8Encoding(encoderShouldEmitUTF8Identifier: true));
        }
        catch (Exception ex)
        {
            Diag.Log($"[orgclick] 探针异常：{ex}");
        }
        finally
        {
            Diag.FlushTrace();
        }
    }

    /// <summary>
    /// <c>VMS_DIAG=treecheck</c> 的无人值守校验：在树上勾选 2 台车，
    /// 看定位数据栏（= Feed 过滤后的投影）是否只显示这 2 台。
    /// 这是"跟定位数据栏联动"这条需求的客观证据，结果写到 treecheck.log。
    /// </summary>
    private async Task RunVehicleSelectionCheckAsync()
    {
        try
        {
            var sb = new System.Text.StringBuilder();

            // 0) 初始：未勾选任何车辆/组织 → 定位数据栏必须为空（需求：未勾选前不加载车辆）
            bool initEmpty = Feed.Count == 0 && Positions.Count == 0 && _selectedVehicleIds.Count == 0;
            sb.AppendLine($"{(initEmpty ? "PASS" : "FAIL")} 初始未勾选 → 定位数据栏为空（不加载车辆）");

            // 1) 找一个叶子组织（配送中心：没有下级单位的末级单位，展开后直接挂车辆）
            var leaf = Companies.SelectMany(c => c.DescendantsAndSelf())
                                .OfType<CompanyNode>()
                                .FirstOrDefault(c => !c.CompanyChildren.Any());
            if (leaf is null) { WriteSelectionCheck("FAIL: 找不到叶子组织（树里没有配送中心节点）"); return; }

            // 2) 加载该组织下的车辆（懒加载路径，与用户手动展开一致）
            await LoadVehiclesAsync(leaf);

            // 3) 取前 2 台真实车辆（Id != 0，跳过"共 N 台"占位行）
            var vehicles = leaf.VehicleChildren.Where(v => v.Id != 0).Take(2).ToList();
            if (vehicles.Count < 2)
            {
                sb.AppendLine($"FAIL: 叶子组织「{leaf.Name}」只载到 {vehicles.Count} 台车（不足 2 台，无法验证多选）");
                WriteSelectionCheck(sb.ToString());
                return;
            }

            var v1 = vehicles[0];
            var v2 = vehicles[1];

            // 4) 勾选这 2 台（等价于用户点行内复选框），每次都会重设 Feed.Query.Ids 并重置 Feed
            await UpdateVehicleSelectionAsync(v1, true);
            await UpdateVehicleSelectionAsync(v2, true);

            // 5) 此刻已在 UI 线程（本方法是 Post 出的），直接重建 Positions（安全，非 XAML 布局回调内），
            //    让它与 Feed 同步，再断言数据栏只含这 2 台。
            RebuildPositions();

            // v5：Ids 里是字符串主键 vehicle_id
            var idsOk = Feed.Query.Ids.Count == 2
                        && Feed.Query.Ids.Contains(v1.VehicleId)
                        && Feed.Query.Ids.Contains(v2.VehicleId);
            var feedOk = Feed.Count == 2 && Feed.TotalCount == 2;
            var posOk = Positions.Count == 2;
            sb.AppendLine(
                $"{(idsOk && feedOk && posOk ? "PASS" : "FAIL")} 车辆勾选 → 定位数据栏联动\n" +
                $"  勾选车辆: {v1.Plate}(vehicleId={v1.VehicleId}), {v2.Plate}(vehicleId={v2.VehicleId})\n" +
                $"  Feed.Query.Ids 数量={Feed.Query.Ids.Count}  命中期望={idsOk}\n" +
                $"  Feed.Count={Feed.Count}  Feed.TotalCount={Feed.TotalCount}  ({(feedOk ? "OK" : "BAD")})\n" +
                $"  Positions.Count={Positions.Count}  ({(posOk ? "OK" : "BAD")})");

            // 6) 组织勾选 → 选中其全部车辆（先清空个人勾选，隔离测试）
            await SelectAllAsync();
            var (_, orgTotal) = await _source.QueryAsync(new VehicleQuery { CompanyId = leaf.Id, Limit = 1 }, default);
            await UpdateOrgSelectionAsync(leaf, true);
            RebuildPositions();
            bool orgSmall = orgTotal <= VehicleQuery.MaxIdFilter;
            bool orgIdsOk = orgSmall
                ? _selectedVehicleIds.Count == orgTotal
                : _scopeCompanyId == leaf.Id && _selectedVehicleIds.Count == 0;
            bool orgFeedOk = Feed.TotalCount == orgTotal &&
                             (orgSmall ? Positions.Count == orgTotal : Positions.Count > 0);
            sb.AppendLine(
                $"{(orgIdsOk && orgFeedOk ? "PASS" : "FAIL")} 组织勾选 → 选中其全部车辆{(orgSmall ? "(精确id)" : "(整树CompanyId)")}\n" +
                $"  组织: {leaf.Name}(id={leaf.Id})  子树车辆数={orgTotal}  分支={(orgSmall ? "小组织(ids≤256)" : "大组织(CompanyId整树)")}\n" +
                $"  _selectedVehicleIds 数量={_selectedVehicleIds.Count}  _scopeCompanyId={_scopeCompanyId}  命中期望={orgIdsOk}\n" +
                $"  Feed.TotalCount={Feed.TotalCount}  Positions.Count={Positions.Count}  ({(orgFeedOk ? "OK" : "BAD")})");

            // 7) 取消组织勾选 → 清空数据栏 + 取消其车辆勾选 + **组织对号本身复位**
            //
            // 三态下：取消一个被全选的组织 ⇒ 该组织及子树回 false，父组织（若仍有其它勾选项）
            // 会经 RecomputeAncestors 变成"部分选择"(null)。这里断言该组织本身已回 false、
            // 且树下车辆勾选已清空。
            await UpdateOrgSelectionAsync(leaf, false);
            RebuildPositions();
            bool leafVehiclesStillChecked = leaf.VehicleChildren.Any(v => v.Id != 0 && v.IsChecked == true);
            bool orgMarkCleared = leaf.IsChecked != true;
            bool orgClearOk = Feed.Count == 0 && Positions.Count == 0 &&
                              !leafVehiclesStillChecked && orgMarkCleared;
            sb.AppendLine(
                $"{(orgClearOk ? "PASS" : "FAIL")} 取消组织勾选 → 对号复位 + 清空数据栏 + 取消其车辆勾选\n" +
                $"  组织 IsChecked={leaf.IsChecked} (期望 False, {(orgMarkCleared ? "OK" : "BAD")})\n" +
                $"  Feed.Count={Feed.Count}  Positions.Count={Positions.Count}  " +
                $"树下车辆仍有勾选={leafVehiclesStillChecked}");

            // 8) 往返回纳：取消后必须还能重新勾上（防止"只能进不能出"的单向锁死）
            await UpdateOrgSelectionAsync(leaf, true);
            bool recheckOk = leaf.IsChecked == true && Feed.TotalCount == orgTotal;
            sb.AppendLine(
                $"{(recheckOk ? "PASS" : "FAIL")} 取消后再勾选 → 可重新勾上（往返回纳）\n" +
                $"  组织 IsChecked={leaf.IsChecked}  Feed.TotalCount={Feed.TotalCount} (期望 {orgTotal})  " +
                $"({(recheckOk ? "OK" : "BAD")})");

            // 8.5) 三态父级推导（本次需求的核心）：找一个含 ≥2 个下级组织的父组织，
            //      勾其中 1 个 → 父应变"部分选择"(null)；勾全部 → 父变 true；取消 1 个 → 回到 null。
            var parent = Companies.SelectMany(c => c.CompanyChildren)
                                  .FirstOrDefault(p => p.CompanyChildren.Count() >= 2);
            if (parent is null)
            {
                sb.AppendLine("SKIP 三态父级推导（找不到含≥2下级组织的父组织，跳过）");
            }
            else
            {
                var kids = parent.CompanyChildren.Take(2).ToList();
                var d = (bool b) => b ? "OK" : "BAD";
                var s = (bool? v) => v == true ? "True" : v == false ? "False" : "null";

                kids[0].SetCheckedWithPropagation(true);              // 勾第 1 个下级
                bool partialAfterOne = parent.IsChecked == null;
                string afterOne = s(parent.IsChecked);
                kids[1].SetCheckedWithPropagation(true);              // 勾第 2 个下级（全部勾上）
                bool allAfterTwo = parent.IsChecked == true;
                string afterTwo = s(parent.IsChecked);
                kids[0].SetCheckedWithPropagation(false);             // 取消第 1 个
                bool partialAfterUncheck = parent.IsChecked == null;
                string afterUncheck = s(parent.IsChecked);

                // 复位：把所有下级组织取消，父级随之回 false，避免污染后续/截图
                foreach (var k in parent.CompanyChildren) k.SetCheckedWithPropagation(false);

                bool triOk = partialAfterOne && allAfterTwo && partialAfterUncheck;
                sb.AppendLine(
                    $"{(triOk ? "PASS" : "FAIL")} 三态父级推导：勾1个下级→父部分选择(null)；勾全部→父true；取消1个→回null\n" +
                    $"  父组织={parent.Name} 下级数={parent.CompanyChildren.Count()}\n" +
                    $"  勾1个后 父IsChecked={afterOne}(期望 null, {d(partialAfterOne)})\n" +
                    $"  勾全部后 父IsChecked={afterTwo}(期望 True, {d(allAfterTwo)})\n" +
                    $"  取消1个后 父IsChecked={afterUncheck}(期望 null, {d(partialAfterUncheck)})");
            }

            // 9) UI 层：真实点两次组织复选框，断言**控件上的对号**真的翻转。
            //    模型 IsChecked 对 ≠ 用户看到的对号对，本条才是"取消勾选后对号消失"的直接证据。
            //    ⚠ 用**根组织**做：根节点一定在可视树里，不会因为"没展开→没渲染"而误判。
            var panel = Controls.CompanyTreePanel.Current;
            var rootOrg = Companies.FirstOrDefault();
            if (panel is null || rootOrg is null)
            {
                sb.AppendLine("SKIP UI 层对号翻转（树面板未实例化或无根组织，跳过）");
            }
            else
            {
                await panel.EnsureOrgVisibleAsync(rootOrg);
                var (afterCheck, afterUncheck, found) = await panel.RunOrgCheckUiToggleAsync(rootOrg);
                bool uiOk = found && afterCheck == true && afterUncheck == false;
                sb.AppendLine(
                    $"{(uiOk ? "PASS" : "FAIL")} UI 层：点击组织复选框 → 对号勾上后能取消\n" +
                    $"  组织={rootOrg.Name}  找到控件={found}\n" +
                    $"  勾选后 IsChecked={afterCheck?.ToString() ?? "null"} (期望 True)  " +
                    $"取消后 IsChecked={afterUncheck?.ToString() ?? "null"} (期望 False)");

                // 9b) 复选框**尺寸**取证：ActualWidth 若 < 20 说明框被 Width 压扁、右侧被切掉。
                //     这个坑编译期和运行期都不报错（框照样显示，只是缺一截），只能量尺寸。
                sb.AppendLine("INFO 树内复选框实际渲染尺寸（ActualWidth<20 即被压扁）:");
                sb.Append(panel.MeasureCheckBoxes());

                // 复位：测试把根组织取消勾选了，清干净以免影响后续/截图
                await UpdateOrgSelectionAsync(rootOrg, false);
                foreach (var r in Companies) r.SetSubtreeChecked(false);
            }

            WriteSelectionCheck(sb.ToString());
        }
        catch (Exception ex)
        {
            WriteSelectionCheck($"FAIL: 校验过程异常 {ex}");
        }
    }

    /// <summary>把 treecheck 结果写到 <c>%LocalAppData%\Vms.Client\treecheck.log</c>（覆盖写）。</summary>
    private void WriteSelectionCheck(string text)
    {
        try
        {
            var dir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Vms.Client");
            Directory.CreateDirectory(dir);
            File.WriteAllText(Path.Combine(dir, "treecheck.log"),
                $"{DateTime.Now:yyyy-MM-dd HH:mm:ss}\n{text}\n");
        }
        catch
        {
            // 诊断设施绝不许成为崩溃源
        }
    }

    /// <summary>
    /// 组织树装配结果快照（<c>%LocalAppData%\Vms.Client\tree.log</c>）。
    ///
    /// 存在意义：树是否有下级单位，取决于"数据源返回的组织记录 + 父子装配"两段，
    /// 而这两段都不在界面上留痕迹 —— 一旦树只显示根节点，光看界面无法判断是
    /// 数据里就没有下级，还是树控件没渲染出来。写一行快照就能立刻分清，
    /// 省掉一轮"来回加日志重编译"。
    /// </summary>
    private void WriteTreeSnapshot()
    {
        try
        {
            var dir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Vms.Client");
            Directory.CreateDirectory(dir);

            var text = new System.Text.StringBuilder();
            text.AppendLine($"# 组织树快照 {DateTime.Now:yyyy-MM-dd HH:mm:ss}  数据源={_source.BackendName}  根节点={Companies.Count}");
            foreach (var root in Companies)
            {
                text.AppendLine($"{root.Name}(id={root.Id}) 在线/总数={root.CountText} 下级组织={root.CompanyChildren.Count()} 已载车辆={root.VehicleChildren.Count()}");
                foreach (var c in root.CompanyChildren)
                    text.AppendLine($"    下级：{c.Name}(id={c.Id}) 在线/总数={c.CountText} 下级组织={c.CompanyChildren.Count()}");
            }
            File.WriteAllText(Path.Combine(dir, "tree.log"), text.ToString());
        }
        catch
        {
            // 诊断设施绝不许成为崩溃源
        }
    }

    /// <summary>
    /// 重建两个数据页签的集合 —— 一律排队执行。
    /// 这几个调用点（检索框回车、点组织、点车辆、启动初始化）全部源于 XAML 事件回调，
    /// 就地重建集合等于在本机回调栈里抽地板（见 <see cref="Post"/>）。
    /// </summary>
    private void RebuildAllDeferred() => Post(() =>
    {
        RebuildPositions();

        // 报警页签不可见时连重建都不做：那同样是对一个 Collapsed 的 ListView 做结构性变更。
        // 它显示出来的时候会由 NotifyTabChanged 触发一次重建，数据不会缺。
        if (_alarmTabVisible) RebuildAlarmsFromFeed();

        RefreshLoadState();
    });

    /// <summary>
    /// 组织树单击选中 → 切换公司过滤。
    /// 过滤范围是**该组织及其所有下级组织**（车辆实体挂在末级单位上，
    /// 只按 id 精确匹配的话"点北京分公司"会一台车都查不到）。
    /// 切组织会清空"树上勾选的具体车辆"（两种范围是互斥的视图）。
    /// </summary>
    public async Task SelectCompanyAsync(uint companyId, string? companyName = null, CancellationToken ct = default)
    {
        _scopeCompanyId = companyId;
        _scopeCompanyName = companyName ?? string.Empty;
        _scopeKeyword = string.Empty;
        SearchKeyword = string.Empty;
        SelectedCompanyId = companyId;
        _selectedVehicleIds.Clear();
        await ApplyScopeAsync(ct);
    }

    /// <summary>
    /// 组织树顶部「全部车辆」：清空组织过滤 + 检索词 + 车辆勾选，回到全局视图。
    /// （点过某个分公司之后必须有一条回到"全部"的路径，否则用户会被困在过滤态里。）
    /// </summary>
    public async Task SelectAllAsync(CancellationToken ct = default)
    {
        SearchKeyword = string.Empty;
        _scopeCompanyId = 0;
        _scopeCompanyName = string.Empty;
        _scopeKeyword = string.Empty;
        SelectedCompanyId = 0;
        _selectedVehicleIds.Clear();
        _orgAddedIds.Clear();
        foreach (var v in Companies.SelectMany(c => c.DescendantsAndSelf()).OfType<VehicleNode>().Where(v => v.Id != 0))
            v.IsChecked = false;
        foreach (var root in Companies)          // 从根清一次即可（SetSubtreeChecked 内部已递归）
            root.SetSubtreeChecked(false);
        await ApplyScopeAsync(ct);
    }

    /// <summary>检索框回车：按关键词过滤（显式查询，非"勾选"），并退出组织/车辆勾选态。</summary>
    public async Task ReloadAsync(CancellationToken ct = default)
    {
        _scopeKeyword = SearchKeyword;
        _selectedVehicleIds.Clear();
        _orgAddedIds.Clear();
        _scopeCompanyId = 0;
        _scopeCompanyName = string.Empty;
        SelectedCompanyId = 0;
        foreach (var root in Companies)
            root.SetSubtreeChecked(false);
        await ApplyScopeAsync(ct);
    }

    /// <summary>
    /// 树上勾选/取消勾选某台车（复选框触发）。
    /// 勾选集合非空时，定位/报警数据栏只显示这些车；清空后回退到组织/全局范围。
    /// 每次勾选变化都会把最后操作的那台车设为地图聚焦目标。
    /// </summary>
    /// <summary>
    /// 树上勾选/取消勾选某台车（复选框触发）。
    /// 勾选集合非空时，定位/报警数据栏只显示这些车；清空后回退到组织/关键词/空范围。
    /// 勾选具体车辆会退出"组织整树"范围（二者互斥），并复位组织复选框。
    /// 每次勾选变化都会把最后操作的那台车设为地图聚焦目标。
    /// </summary>
    public async Task UpdateVehicleSelectionAsync(VehicleNode vehicle, bool selected, CancellationToken ct = default)
    {
        if (selected)
        {
            // 勾选具体车辆 → 退出组织整树范围（CompanyId），改为按 id 联动
            if (_scopeCompanyId != 0)
            {
                _scopeCompanyId = 0;
                _scopeCompanyName = string.Empty;
                SelectedCompanyId = 0;
                UncheckAllOrgsExcept(null);
            }
            // 有主键才收进选中集；占位/未取到主键的行跳过（空串会被 native 端忽略）
            if (!string.IsNullOrEmpty(vehicle.VehicleId))
                _selectedVehicleIds.Add(vehicle.VehicleId);
        }
        else
        {
            _selectedVehicleIds.Remove(vehicle.VehicleId);
        }

        _lastToggledVehicle = vehicle;
        SelectedVehicle = vehicle;
        OnPropertyChanged(nameof(SelectedVehicleCount));
        OnPropertyChanged(nameof(SelectionSummary));

        // M5：增量订阅 —— 勾上一台车 = 订阅其轨迹/报警；取消 = 退订。
        // pushd 按车辆主键过滤轨迹、按组织子树过滤报警；这里只传车辆主键即可。
        if (!string.IsNullOrEmpty(vehicle.VehicleId))
            PushDelta(selected ? (int)NativePushOp.Add : (int)NativePushOp.Remove,
                      new[] { vehicle.VehicleId });

        await ApplyScopeAsync(ct);
    }

    /// <summary>
    /// 勾选/取消勾选某个组织（组织复选框触发）。
    ///
    /// 勾选 = 选中该组织<b>及其全部下级</b>的车辆（定位数据栏只显示这些）：
    ///   · 子树车辆数 ≤ 256 → 把每辆车的 id 收进 <see cref="_selectedVehicleIds"/>（数据栏走 Ids 精确过滤）；
    ///   · 子树过大（&gt; 256）或累计将超 256 → 改用 <see cref="_scopeCompanyId"/> 整棵子树过滤
    ///     （可容纳任意规模），此时选中集与勾选态让位给"单一组织范围"（互斥）。
    ///
    /// 取消勾选 = 把该组织贡献的 id 移除（数据栏回退），并取消该组织下可见车辆的勾选。
    /// </summary>
    public async Task UpdateOrgSelectionAsync(CompanyNode org, bool selectAll, CancellationToken ct = default)
    {
        if (selectAll)
        {
            // 先问总数，决定走"精确 id"还是"整棵子树 CompanyId"
            var (_, total) = await _source.QueryAsync(new VehicleQuery { CompanyId = org.Id, Limit = 1 }, ct);
            bool fitsIds = total > 0 && total <= VehicleQuery.MaxIdFilter
                           && _selectedVehicleIds.Count + total <= VehicleQuery.MaxIdFilter;

            if (fitsIds)
            {
                // 小组织：模型把本级及整棵子树置为全选（三态），数据层把每辆车 id 收进选中集
                org.SetCheckedWithPropagation(true);

                var (rows, _) = await _source.QueryAsync(new VehicleQuery { CompanyId = org.Id, Limit = (uint)total }, ct);
                // v5：取字符串主键；空主键（占位行）不能进过滤集，否则服务端匹配不到
                var ids = rows.Select(r => r.VehicleId).Where(s => !string.IsNullOrEmpty(s)).ToList();
                _orgAddedIds[org.Id] = ids;
                foreach (var id in ids) _selectedVehicleIds.Add(id);
                // M5：增量订阅（小组织 = 精确 id 集合）
                PushDelta((int)NativePushOp.Add, ids);

                // 让树下可见的车辆行也打上勾（先确保车辆已加载，叶子组织 ≤256 量可控）
                await EnsureVehiclesLoadedAsync(org, ct);
                SetTreeVehiclesChecked(org, ids, true);

                _scopeCompanyId = 0;
                _scopeCompanyName = string.Empty;
                SelectedCompanyId = 0;
            }
            else
            {
                // 大组织：整棵子树用 CompanyId 过滤；选中集与勾选态让位给单一组织范围
                _selectedVehicleIds.Clear();
                _orgAddedIds.Clear();
                UncheckAllTreeVehicles();
                UncheckAllOrgsExcept(org);
                SetTreeVehiclesCheckedAll(org, true);   // 树上已加载的下级车辆同步勾上（语义：整棵子树选中）
                _scopeCompanyId = org.Id;
                _scopeCompanyName = org.Name;
                SelectedCompanyId = org.Id;
                // M5：大组织走 CompanyId 整树过滤，但推送订阅按车辆主键。本地只持有已加载的车，
                // 订阅这些（服务端对本会话按 user 组织子树下发的报警仍覆盖全子树）。
                var loadedIds = org.DescendantsAndSelf().OfType<VehicleNode>()
                                    .Where(v => v.VehicleId.Length > 0).Select(v => v.VehicleId).ToList();
                PushDelta((int)NativePushOp.Add, loadedIds);
            }
        }
        else
        {
            // 取消勾选：清空该组织贡献的车辆选择，并把本级及整棵子树的对号撤掉（含父级回溯）。
            // 组织对号由 OrgCheck_Click 在点击时已走 SetCheckedWithPropagation 置 false；这里再兜一次底，
            // 覆盖"从数据栏/地图侧取消"等不经过复选框点击的路径，避免 UI 与数据范围脱节。
            org.SetCheckedWithPropagation(false);

            if (_orgAddedIds.TryGetValue(org.Id, out var ids))
            {
                foreach (var id in ids) _selectedVehicleIds.Remove(id);
                _orgAddedIds.Remove(org.Id);
                SetTreeVehiclesChecked(org, ids, false);
                // M5：退订（小组织精确 id 集合）
                PushDelta((int)NativePushOp.Remove, ids);
            }
            else if (_scopeCompanyId == org.Id)
            {
                _scopeCompanyId = 0;
                _scopeCompanyName = string.Empty;
                SelectedCompanyId = 0;
                UncheckAllTreeVehicles(org);
                // M5：退订已加载的车辆（大组织范围取消）
                var loadedIds = org.DescendantsAndSelf().OfType<VehicleNode>()
                                    .Where(v => v.VehicleId.Length > 0).Select(v => v.VehicleId).ToList();
                PushDelta((int)NativePushOp.Remove, loadedIds);
            }
        }

        OnPropertyChanged(nameof(SelectedVehicleCount));
        OnPropertyChanged(nameof(SelectionSummary));
        await ApplyScopeAsync(ct);
    }

    /// <summary>把 org 子树内、id 落在给定集合里的车辆行勾选/取消（仅影响已加载的行）。</summary>
    private void SetTreeVehiclesChecked(CompanyNode org, List<string> ids, bool value)
    {
        foreach (var v in org.DescendantsAndSelf().OfType<VehicleNode>()
                             .Where(v => v.VehicleId.Length > 0 && ids.Contains(v.VehicleId)))
            v.IsChecked = value;
    }

    /// <summary>
    /// 把 org 子树内<b>已加载</b>的车辆行<b>批量</b>置勾选/取消。
    ///
    /// ⚠ 批量赋值不会走用户点击那条路（车辆复选框挂的是 <c>Click</c>，程序化赋值不触发），
    /// 所以这里既不会重算父级、也不会改数据范围 —— 正是"组织级联勾选"想要的：只改树上的显示态。
    /// </summary>
    private void SetTreeVehiclesCheckedAll(CompanyNode org, bool value)
    {
        foreach (var v in org.DescendantsAndSelf().OfType<VehicleNode>().Where(v => v.Id != 0))
            v.IsChecked = value;
    }

    /// <summary>取消树上全部已加载车辆行的勾选；within 非空时只处理该组织子树。</summary>
    private void UncheckAllTreeVehicles(CompanyNode? within = null)
    {
        var roots = within is null ? (IEnumerable<OrgNodeBase>)Companies : new[] { within };
        foreach (var root in roots)
            foreach (var v in root.DescendantsAndSelf().OfType<VehicleNode>().Where(v => v.Id != 0))
                v.IsChecked = false;
    }

    /// <summary>
    /// 取消全部组织的勾选；<paramref name="keep"/> 非 null 时<b>连同其整棵子树</b>保持勾选。
    ///
    /// ⚠ 不能只判 <c>c != keep</c>：keep 的<b>下级</b>组织也是 c != keep，
    /// 会被一起取消掉，随后 <c>keep.IsChecked = true</c> 只级联一次，
    /// 中间还可能被 <see cref="CompanyNode.SetSubtreeChecked"/> 的级联覆盖，
    /// 最终出现"父勾了、子没勾"的错乱。必须按"是否在 keep 子树内"整段跳过。
    /// </summary>
    private void UncheckAllOrgsExcept(CompanyNode? keep)
    {
        var keepIds = keep is null
            ? new HashSet<uint>()
            : new HashSet<uint>(keep.DescendantsAndSelf().OfType<CompanyNode>().Select(c => c.Id));

        foreach (var c in Companies.SelectMany(r => r.DescendantsAndSelf()).OfType<CompanyNode>())
            if (!keepIds.Contains(c.Id))
                c.SetSubtreeChecked(false);   // 批量置位（不回溯），避免 O(n²)

        // keep 整棵子树置勾选（父在下级之后不会被误清），随后沿父链重算祖先 ——
        // keep 的兄弟组织都已被取消，祖先组织据此变成"部分选择"(null)，正是三态树应有的表现。
        if (keep is not null)
        {
            keep.SetSubtreeChecked(true);
            keep.RecomputeAncestors();
        }
    }

    /// <summary>确保 org 子树（含各级下级组织）的车辆已加载，便于勾选态在树上可见。</summary>
    private async Task EnsureVehiclesLoadedAsync(CompanyNode org, CancellationToken ct)
    {
        if (org.CompanyChildren.Any())
        {
            foreach (var c in org.CompanyChildren)
                await EnsureVehiclesLoadedAsync(c, ct);
        }
        else
        {
            await LoadVehiclesAsync(org);
        }
    }

    /// <summary>点击树上车辆行 / 地图图钉：仅把该车居中到地图，不改变数据范围（不清空其它打点）。</summary>
    public void FocusVehicle(VehicleNode vehicle)
    {
        SelectedVehicle = vehicle;
        RaiseVehicleFocus(vehicle);
    }

    /// <summary>
    /// 把当前的"范围"写进 Feed 查询并重置。
    /// 优先级：树上勾选的具体车辆 &gt; 组织/检索范围。
    /// 范围变化会让地图清空旧打点（防止旧组织的车被误认为没被筛掉），
    /// 再由页面按新 Feed 重新打点。
    /// </summary>
    /// <summary>
    /// 把当前的"选中集合"写进 Feed 查询并重置。
    /// 优先级：树上勾选的具体车辆（Ids）&gt; 勾选的组织（CompanyId 整树）&gt; 关键词 &gt; 全空（NoResults）。
    /// 全空时数据栏保持空、不加载任何车（需求：未勾选前不加载车辆）。
    /// </summary>
    private async Task ApplyScopeAsync(CancellationToken ct)
    {
        if (_selectedVehicleIds.Count > 0)
        {
            Feed.Query.NoResults = false;
            Feed.Query.Ids.Clear();
            foreach (var id in _selectedVehicleIds)
                Feed.Query.Ids.Add(id);
            Feed.Query.CompanyId = 0;
            Feed.Query.Keyword = string.Empty;
            // 选中的车可能跨多组织，确保一页装得下
            Feed.Query.Limit = (uint)Math.Max(200, _selectedVehicleIds.Count);
            ScopeText = $"当前范围：已选 {_selectedVehicleIds.Count} 台车（定位数据已联动）";
        }
        else if (_scopeCompanyId != 0)
        {
            Feed.Query.NoResults = false;
            Feed.Query.Ids.Clear();
            Feed.Query.CompanyId = _scopeCompanyId;
            Feed.Query.Keyword = string.Empty;
            Feed.Query.Limit = 200;
            var label = string.IsNullOrEmpty(_scopeCompanyName) ? $"组织 #{_scopeCompanyId}" : _scopeCompanyName;
            ScopeText = $"当前范围：{label}（含下级单位）";
        }
        else if (!string.IsNullOrEmpty(_scopeKeyword))
        {
            Feed.Query.NoResults = false;
            Feed.Query.Ids.Clear();
            Feed.Query.CompanyId = 0;
            Feed.Query.Keyword = _scopeKeyword;
            Feed.Query.Limit = 200;
            ScopeText = $"当前范围：关键词「{_scopeKeyword}」";
        }
        else
        {
            // 全空：不加载任何车（需求：未勾选任何车辆/组织前，定位数据栏保持空）
            Feed.Query.NoResults = true;
            Feed.Query.Ids.Clear();
            Feed.Query.CompanyId = 0;
            Feed.Query.Keyword = string.Empty;
            Feed.Query.Limit = 200;
            ScopeText = "当前范围：未选择（勾选车辆或组织机构以加载）";
        }

        // 退出"勾选态"时，把树上所有车辆复选框复位，避免 UI 勾选与数据范围对不上
        if (_selectedVehicleIds.Count == 0 && _scopeCompanyId == 0)
            foreach (var v in Companies.SelectMany(c => c.DescendantsAndSelf()).OfType<VehicleNode>().Where(v => v.Id != 0))
                v.IsChecked = false;

        await Feed.ResetAsync(ct);
        RebuildAllDeferred();
        RaiseScopeChanged();
    }

    /// <summary>懒加载某组织下的车辆（树展开时调用）。</summary>
    public async Task LoadVehiclesAsync(CompanyNode node)
    {
        if (node.IsVehiclesLoaded || node.IsLoadingVehicles) return;
        node.IsLoadingVehicles = true;
        try
        {
            var (rows, total) = await _source.QueryAsync(
                new VehicleQuery { CompanyId = node.Id, Limit = VehicleTreePageSize }, CancellationToken.None);

            // ⚠ 新加载出来的车辆要**继承所属组织的勾选态**（在加入集合之前设好，避免多余通知）：
            // 组织已被全选时再展开，挂在它下面的车必须也是勾上的 —— 否则用户"勾了组织 → 展开一看，
            // 下级车辆一个都没勾"，与"勾选组织 = 选中其整棵子树"的语义直接冲突。
            bool inheritChecked = node.IsChecked == true;

            foreach (var v in node.VehicleChildren.ToList())
                node.Children.Remove(v);
            foreach (var r in rows)
            {
                var vehicle = VehicleNode.From(r);
                vehicle.IsChecked = inheritChecked;
                node.AddVehicle(vehicle);
            }

            if (total > rows.Count)
                node.AddVehicle(VehicleNode.Placeholder(
                    $"共 {total:N0} 台 · 仅显示前 {rows.Count} 台（用检索可精确定位）"));

            node.IsVehiclesLoaded = true;
            node.RaiseVehicleCountChanged();
            RefreshTreeSummary();
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[Monitor] 加载车辆失败：{ex.Message}");
        }
        finally
        {
            node.IsLoadingVehicles = false;
        }
    }

    /// <summary>
    /// 通知"已勾选组织摘要"刷新。
    /// 带重入保护：即使将来有人在别处订阅 CheckedSummary 并回调本方法，
    /// 也只会空转一层返回，而不是无限递归到栈溢出（栈溢出任何 handler 都拦不住）。
    /// </summary>
    public void RaiseCheckedSummaryChanged()
    {
        if (_raisingCheckedSummary) return;
        _raisingCheckedSummary = true;
        try { OnPropertyChanged(nameof(CheckedSummary)); }
        finally { _raisingCheckedSummary = false; }
    }

    private bool _raisingCheckedSummary;

    /// <summary>
    /// 过滤范围变化（切组织 / 选车辆 / 检索）后触发。
    /// 页面据此把地图视野对准新范围 —— ViewModel 不该直接持有 MapHost，
    /// 用事件解耦，将来换成"独立地图窗口"也不用改 VM。
    /// </summary>
    public event EventHandler? ScopeChanged;

    /// <summary>
    /// 仅"聚焦某台车到地图中心"的请求（与范围变化区分开）。
    /// 点击树上车辆行 / 地图上的图钉走这条 —— 它<b>只居中、不清空</b>其它打点，
    /// 否则用户点一下车，地图上其它车全没了，体验很割裂。
    /// </summary>
    public event EventHandler<VehicleNode>? VehicleFocusRequested;

    private void RaiseScopeChanged() => ScopeChanged?.Invoke(this, EventArgs.Empty);

    private void RaiseVehicleFocus(VehicleNode vehicle) => VehicleFocusRequested?.Invoke(this, vehicle);

    // ------------------------------ 实时推送 -------------------------------

    private void OnVehiclesPushed(object? sender, IReadOnlyList<VehicleRow> batch)
    {
        // 没有界面在消费时直接丢弃本帧：数据照收（统计仍在跑），只是不往集合里写。
        if (!IsViewActive) return;

        _pushFrameCount++;
        var applied = Feed.ApplyPush(batch);

        foreach (var row in batch)
        {
            // 定位页签内就地更新（命中即改，不重建集合）
            if (!Diag.SkipPositionUpdate && _posIndex.TryGetValue(row.Id, out var pos))
                pos.CopyFrom(ToPositionRecord(row));

            // 报警页签：出现新报警才入队，避免同一条反复刷。
            // 注意这里只"排队"不直接 Insert —— 每帧在集合头部硬插会逼 ListView
            // 反复重排全部索引，攒一小批一起提交对 UI 友好得多。
            if (!Diag.SkipAlarmInsert && row.HasAlarm)
            {
                var key = AlarmKey(row.Id, row.Alarm);
                if (!_alarmIndex.ContainsKey(key))
                {
                    var rec = ToAlarmRecord(row);
                    _alarmIndex[key] = rec;
                    _alarmPending.Add(rec);
                }
            }
        }

        if (_alarmPending.Count > 0) ScheduleAlarmFlush();

        if (batch.Count > 0)
        {
            _lastLon = batch[0].Lon;
            _lastLat = batch[0].Lat;
            CoordinateText = $"经度:{_lastLon:F6} 纬度:{_lastLat:F6}";
        }

        MapHint = $"实时推送 #{_pushFrameCount}（本帧 {batch.Count} 台，命中可见行 {applied} 台）" +
                  (_source.IsNative ? " · 数据来自 native 库" : " · 数据来自内置模拟");
    }

    // M5：报警推送（与轨迹独立）。逐条去重入队，攒一小批再插集合，避免每帧硬插逼 ListView 重排。
    private void OnAlarmsPushed(object? sender, IReadOnlyList<AlarmRecord> batch)
    {
        if (!IsViewActive) return;

        foreach (var rec in batch)
        {
            var key = AlarmKey(rec.VehicleId, (AlarmFlags)rec.AlarmCode);
            if (!_alarmIndex.ContainsKey(key))
            {
                _alarmIndex[key] = rec;
                _alarmPending.Add(rec);
            }
        }
        if (_alarmPending.Count > 0) ScheduleAlarmFlush();
    }

    /// <summary>
    /// M5：向推送服务发增量订阅（车辆树勾选/取消时调用）。
    /// 只在 native 数据源下真实下发；mock / 空集合直接跳过。异常吞掉（订阅失败不应卡 UI）。
    /// </summary>
    private void PushDelta(int op, IEnumerable<string> ids)
    {
        if (!_source.IsNative) return;
        var list = ids.Where(s => !string.IsNullOrEmpty(s)).ToList();
        if (list.Count == 0) return;
        try { _source.SubscribeDelta(op, list); }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[Monitor] 推送增量订阅失败(op={op})：{ex}");
        }
    }

    // --------------------------- 集合重建（结构性变化） ---------------------------

    private void RebuildPositions()
    {
        _posIndex.Clear();
        Positions.Clear();
        foreach (var row in Feed)
        {
            var r = ToPositionRecord(row);
            _posIndex[row.Id] = r;
            Positions.Add(r);
        }
    }

    private void RebuildAlarmsFromFeed()
    {
        _alarmIndex.Clear();
        Alarms.Clear();
        foreach (var row in Feed.Where(r => r.HasAlarm))
        {
            var rec = ToAlarmRecord(row);
            _alarmIndex[AlarmKey(row.Id, row.Alarm)] = rec;
            Alarms.Add(rec);
        }
        TrimAlarms();

        // 待提交的比 Feed 里的更新，补到最前（不可见时 FlushAlarms 只会做裁剪）
        FlushAlarms();
    }

    /// <summary>把累积的报警一次性提交（新报警插到最前，符合值班习惯）。</summary>
    private void ScheduleAlarmFlush()
    {
        if (!_alarmTabVisible)
        {
            // 报警页签没在显示：只攒不写（见 NotifyTabChanged 的说明）
            TrimAlarmPending();
            return;
        }

        if (_alarmFlushScheduled) return;
        _alarmFlushScheduled = true;
        Post(FlushAlarms);
    }

    private void FlushAlarms()
    {
        _alarmFlushScheduled = false;
        if (_alarmPending.Count == 0) return;

        if (!_alarmTabVisible)
        {
            TrimAlarmPending();
            return;
        }

        for (var i = _alarmPending.Count - 1; i >= 0; i--)
        {
            var rec = _alarmPending[i];
            // 同步回索引：重建会清空 _alarmIndex，这里补回去才不会重复入列
            _alarmIndex[AlarmKey(rec.VehicleId, (AlarmFlags)rec.AlarmCode)] = rec;
            Alarms.Insert(0, rec);
        }
        _alarmPending.Clear();
        TrimAlarms();
    }

    /// <summary>待提交队列的上限（超出丢弃最旧的）。</summary>
    private void TrimAlarmPending()
    {
        while (_alarmPending.Count > 2000)
        {
            var dropped = _alarmPending[0];
            _alarmPending.RemoveAt(0);
            _alarmIndex.Remove(AlarmKey(dropped.VehicleId, (AlarmFlags)dropped.AlarmCode));
        }
    }

    private void TrimAlarms()
    {
        while (Alarms.Count > 2000)
        {
            var removed = Alarms[Alarms.Count - 1];
            Alarms.RemoveAt(Alarms.Count - 1);
            // ⚠ 这里的 key 必须和入队时同一个函数算出来。
            //   之前写成 $"{VehicleId}:{AlarmType}"（"123:超速"），入队时是 $"{Id}:{Alarm}"（"123:Overspeed"），
            //   两边对不上 → 索引只增不减，被裁掉的报警永远无法再次入列。已统一为 AlarmKey。
            _alarmIndex.Remove(AlarmKey(removed.VehicleId, (AlarmFlags)removed.AlarmCode));
        }
    }

    /// <summary>报警去重键。入队、裁剪、查找必须共用这一个，否则索引会静默泄漏。</summary>
    private static string AlarmKey(uint vehicleId, AlarmFlags alarm) => $"{vehicleId}:{(uint)alarm}";

    private void RefreshTreeSummary()
    {
        var count = Companies.SelectMany(c => c.DescendantsAndSelf()).OfType<VehicleNode>()
                              .Count(v => v.Id != 0);
        TreeSummary = $"{count:N0} 台";
    }

    // ---------------------------- 行 → 记录 投影 ----------------------------

    /// <summary>
    /// 车辆行 → 定位记录。
    /// 方向/海拔/里程/卫星数/信号/终端号这类原生库目前还没有专门字段，先用 Id 派生出
    /// **确定性** 的占位值（同一辆车永远同一组），保证页面能真跑、且不抖。
    /// 后续 native 提供定位记录表后，把这里换成真实字段透出即可。
    /// </summary>
    private static PositionRecord ToPositionRecord(VehicleRow row)
    {
        var h = Hash(row.Id);
        return new PositionRecord
        {
            VehicleId = row.Id,
            Plate = row.Plate,
            DeviceCode = $"T{row.Id % 900000 + 100000:D6}",
            GpsTime = row.Time,
            Lon = row.Lon,
            Lat = row.Lat,
            SpeedKph = row.Online ? row.SpeedKph : 0f,
            Direction = (int)(h % 360),
            Altitude = 800f + h % 600,
            MileageKm = (h % 9000) + 100,
            LocateState = row.Online ? "已定位" : "未定位",
            Address = row.Location,
            Online = row.Online,
            AccOn = row.Online && (h & 1) == 0,
            Satellites = row.Online ? (int)(3 + h % 9) : 0,
            Signal = row.Online ? (int)(40 + h % 60) : 0,
            HasAlarm = row.HasAlarm,
        };
    }

    private static AlarmRecord ToAlarmRecord(VehicleRow row)
    {
        var (type, level) = row.Alarm switch
        {
            AlarmFlags.Overspeed => ("超速", "一级"),
            AlarmFlags.Fatigue => ("疲劳驾驶", "一级"),
            AlarmFlags.Offline => ("离线超时", "三级"),
            AlarmFlags.Geofence => ("越界", "二级"),
            AlarmFlags.GnssLost => ("GNSS丢失", "三级"),
            _ => ("其他", "三级"),
        };

        return new AlarmRecord
        {
            Id = (uint)(row.Id * 31 + (uint)row.Alarm),
            VehicleId = row.Id,
            Plate = row.Plate,
            DeviceCode = $"T{row.Id % 900000 + 100000:D6}",
            AlarmCode = (uint)row.Alarm,   // 保留原始枚举值：去重键要用，展示层用 AlarmType 文案
            AlarmTime = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"),
            AlarmType = type,
            Level = level,
            SpeedKph = row.Online ? row.SpeedKph : 0f,
            Address = row.Location,
            Lon = row.Lon,
            Lat = row.Lat,
            HandleState = "未处理",
            Handler = "--",
        };
    }

    /// <summary>稳定的 32 位哈希（xorshift），只用于派生占位字段，不用于任何安全场景。</summary>
    private static uint Hash(uint id)
    {
        ulong s = id * 0x9E3779B97F4A7C15UL + 0x1234567UL;
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return (uint)(s & 0xFFFFFFFF);
    }

    private void RefreshLoadState()
    {
        LoadStateText = Feed.TotalCount == 0
            ? (Feed.IsLoading ? "正在加载…" : "无匹配车辆")
            : $"已加载 {Feed.Count:N0} / 共 {Feed.TotalCount:N0} 条" + (Feed.IsLoading ? "（加载中…）" : string.Empty);
        OnPropertyChanged(nameof(IsLoading));
    }

    /// <summary>
    /// 运行状态心跳（约每 30 秒一行，覆盖写同一个文件）。
    ///
    /// 存在意义：本机层的崩溃（访问违例 / 栈溢出）托管代码拦不住，crash.log 会是空的，
    /// 现场只剩"程序自己退出了"。有了这行心跳，至少能立刻看清死前最后一刻的状态：
    /// 内存是不是已经涨到离谱、报警索引是不是漏了、推送帧停在哪一帧。
    /// </summary>
    private void WriteHeartbeat()
    {
        if (++_heartbeatTicks % 6 != 0) return;

        try
        {
            var dir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Vms.Client");
            Directory.CreateDirectory(dir);

            var mem = GC.GetTotalMemory(false) / (1024 * 1024);
            File.WriteAllText(Path.Combine(dir, "health.log"),
                $"启动：{_startedAt:yyyy-MM-dd HH:mm:ss}  已运行：{DateTime.Now - _startedAt:hh\\:mm\\:ss}\n" +
                $"最后心跳：{DateTime.Now:yyyy-MM-dd HH:mm:ss}  " +
                $"推送帧={_pushFrameCount}  定位={Positions.Count}(idx {_posIndex.Count})  " +
                $"报警={Alarms.Count}(idx {_alarmIndex.Count}, 待提交 {_alarmPending.Count})  " +
                $"Feed={Feed.Count}/{Feed.TotalCount}  托管堆≈{mem}MB\n");
        }
        catch
        {
            // 心跳写失败绝不许再抛，否则诊断设施反而成了崩溃源
        }
    }

    public void Dispose()
    {
        _source.VehiclesPushed -= OnVehiclesPushed;
        _source.AlarmsPushed -= OnAlarmsPushed;
        _statsTimer?.Stop();
    }
}

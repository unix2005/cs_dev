using Microsoft.UI.Dispatching;
using Vms.Client.Models;

namespace Vms.Client.Services;

/// <summary>
/// 纯托管兜底数据源：当 <c>vms_core.dll</c> 缺失或 ABI 校验失败时启用，
/// 保证界面永远能打开（设计评审、UI 联调不被后端阻塞）。
///
/// 数据规模与真实系统一致（34554 台 / 18714 台归属班组），用于验证
/// 分页加载与实时刷新在真实数据量下是否还流畅 —— 用 50 条假数据测不出问题。
/// </summary>
internal sealed class MockVehicleDataSource : IVehicleDataSource
{
    private const uint TotalVehicles = 34554;
    private const uint TotalOnline = 10846;
    private const uint TotalOffline = 23708;
    private const uint AlarmUnhandled = 46343;

    private static readonly (uint Id, uint Parent, uint Online, uint Count, string Name)[] Seed =
    [
        ( 1, 0, 8960, 18714, "运输公司"),
        ( 2, 1,  293,   886, "塔运司"),
        ( 3, 1,  110,   464, "沙运司"),
        ( 4, 1,   89,   862, "一公司"),
        ( 5, 1, 1844,  2038, "长庆运输公司"),
        ( 6, 1,    0,   876, "华北运输公司"),
        ( 7, 1,   70,   450, "准噶尔分公司"),
        ( 8, 1,   87,   130, "冀东运输公司"),
        ( 9, 1,  316,   500, "黑龙江分公司"),
        (10, 1,  332,   615, "吉林分公司"),
        (11, 1,  269,   470, "辽宁分公司"),
        (12, 1,   93,   158, "大连分公司"),
        (13, 1,  210,   520, "内蒙古分公司"),
        (14, 1,  149,   398, "北京分公司"),
        (15, 1,  215,   272, "河北分公司"),
        (16, 1,   57,   107, "天津分公司"),
        (17, 1,  116,   167, "山西分公司"),
        (18, 1,  272,   344, "山东分公司"),
        (19, 1,   82,   135, "江苏分公司"),

        // —— 三级组织测试数据：省级分公司 → 配送中心 ——
        // 北京 / 河北 / 辽宁 这三个省级单位下面再挂配送中心（地市级网点）。
        // 它们有下级组织，因此展开时只显示子组织、不在此加载车辆（见 CompanyTreePanel.Tree_Expanding）；
        // 展开某个配送中心才会懒加载其名下车辆。
        (1401, 14,  80,   210, "北京朝阳配送中心"),
        (1402, 14,  69,   188, "北京海淀配送中心"),
        (1501, 15, 120,   160, "石家庄配送中心"),
        (1502, 15,  95,   112, "保定配送中心"),
        (1101, 11, 150,   250, "沈阳配送中心"),
        (1102, 11, 119,   220, "大连配送中心"),
    ];

    private static readonly string[] Cities =
    [
        "北京市朝阳区", "北京市海淀区", "天津市滨海新区", "石家庄市裕华区", "太原市小店区",
        "呼和浩特市赛罕区", "沈阳市铁西区", "长春市朝阳区", "哈尔滨市南岗区", "济南市历下区",
        "西安市雁塔区", "兰州市城关区", "保定市莲池区", "唐山市路北区", "青岛市市南区",
    ];

    private readonly DispatcherQueue? _uiQueue;
    private readonly uint[] _companyOf = new uint[TotalVehicles + 1];
    private readonly Dictionary<uint, HashSet<uint>> _subtree = [];
    private Timer? _timer;

    public MockVehicleDataSource(DispatcherQueue? uiQueue)
    {
        _uiQueue = uiQueue;
        BuildCompanyAssignment();
    }

    public string BackendName => "内置模拟数据（未加载 native 库）";
    public bool IsNative => false;

    public event EventHandler<IReadOnlyList<VehicleRow>>? VehiclesPushed;

    private void BuildCompanyAssignment()
    {
        // 1) 每棵子树的成员集合 —— 过滤语义是"该组织及其所有下级"，
        //    和 native 侧 company_in_subtree 保持一致（点父组织不该查出空列表）。
        foreach (var c in Seed)
        {
            var set = new HashSet<uint> { c.Id };
            bool grew = true;
            while (grew)
            {
                grew = false;
                foreach (var s in Seed)
                    if (s.Parent != 0 && set.Contains(s.Parent) && set.Add(s.Id)) grew = true;
            }
            _subtree[c.Id] = set;
        }

        // 2) 车辆归属：只分配给"叶子组织"（有下级的节点，其车辆实体在下级，
        //    父节点的 count 只是聚合值，避免父子查出两套重叠的车）。
        var parents = Seed.Where(s => s.Parent != 0).Select(s => s.Parent).ToHashSet();
        uint cursor = 0;
        foreach (var c in Seed.Skip(1))
        {
            if (parents.Contains(c.Id)) continue;
            for (uint i = 0; i < c.Count && cursor <= TotalVehicles; i++, cursor++)
                _companyOf[cursor] = c.Id;
        }
    }

    public Task<SystemStats> GetStatsAsync(CancellationToken ct = default)
        => Task.FromResult(new SystemStats(
            TotalVehicles, TotalOnline, TotalOffline, 0, AlarmUnhandled,
            (float)TotalOnline * 100f / TotalVehicles));

    public Task<IReadOnlyList<CompanyRecord>> GetCompaniesAsync(CancellationToken ct = default)
        => Task.FromResult<IReadOnlyList<CompanyRecord>>(
            // OrgId：Mock 也带上字符串主键（= 序号的字符串形式），让 UI 逻辑与真实数据源同形
            [.. Seed.Select(s => new CompanyRecord(s.Id, s.Parent, s.Online, s.Count, s.Name, s.Id.ToString()))]);

    public Task<(IReadOnlyList<VehicleRow> Rows, int Total)> QueryAsync(VehicleQuery query, CancellationToken ct = default)
        => Task.Run<(IReadOnlyList<VehicleRow>, int)>(() =>
        {
            // 强制空结果：未勾选任何车辆/组织时，定位数据栏保持空、不加载（需求：未勾选前不加载车辆）
            if (query.NoResults)
                return ([], 0);

            var rows = new List<VehicleRow>((int)query.Limit);
            var matched = 0;

            for (uint idx = 1; idx <= TotalVehicles; idx++)
            {
                ct.ThrowIfCancellationRequested();

                var row = Generate(idx);
                // v5：按字符串主键过滤（vehicle_id），不再是 uint32 行号
                if (query.Ids.Count > 0 && !query.Ids.Contains(row.VehicleId)) continue;
                // v5：Mock 的 org_id 就是公司序号的十进制串，解析回序号后复用同一套子树表
                if (!string.IsNullOrEmpty(query.OrgId))
                {
                    if (!uint.TryParse(query.OrgId, out var cid)) continue;         // 认不出 → 不属于
                    if (!(_subtree.TryGetValue(cid, out var os) && os.Contains(_companyOf[idx]))) continue;
                }
                if (query.CompanyId != 0 &&
                    !(_subtree.TryGetValue(query.CompanyId, out var scope) && scope.Contains(_companyOf[idx]))) continue;
                if (query.OnlineOnly && !row.Online) continue;
                if (query.AlarmOnly && row.Alarm == AlarmFlags.None) continue;
                if (!string.IsNullOrEmpty(query.Keyword) &&
                    !row.Plate.Contains(query.Keyword, StringComparison.OrdinalIgnoreCase) &&
                    !row.SelfCode.Contains(query.Keyword, StringComparison.OrdinalIgnoreCase)) continue;

                if (matched >= query.Offset && matched < query.Offset + query.Limit)
                    rows.Add(row);
                matched++;
            }
            return (rows, matched);
        }, ct);

    /// <summary>确定性生成（同一 index 永远同一台车），因此分页之间不会"跳车"。</summary>
    private static VehicleRow Generate(uint idx)
    {
        ulong s = idx * 0x9E3779B97F4A7C15UL + 0x1234567UL;
        uint Next(uint hi) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint)(s % hi); }

        string[] prefixes = ["京", "津", "冀", "晋", "蒙", "辽", "吉", "黑", "鲁", "苏", "陕", "甘"];
        var online = idx <= TotalOnline;
        var alarm = Next(100) < 12 ? (AlarmFlags)(1u << (int)Next(5)) : AlarmFlags.None;

        // v5：主键按 d_vehicleinfo.vehicle_id 的真实语义 —— **车牌号+颜色**（非数字），
        // 与 native Mock、真实库形状一致；否则 ids 过滤在 Mock 下能过、接真库就全废。
        var plate = $"{prefixes[Next((uint)prefixes.Length)]}{(char)('A' + Next(26))}{Next(90000) + 1000:D5}";

        return new VehicleRow
        {
            Id = idx,
            VehicleId = $"{plate}-{Next(5) + 1}",
            SelfCode = $"QC{idx % 100000:D5}",
            Plate = plate,
            Time = DateTime.Now.AddSeconds(online ? 0 : -Next(86400)).ToString("yyyy-MM-dd HH:mm:ss"),
            SpeedKph = online ? Next(96) : 0f,
            Location = Cities[Next((uint)Cities.Length)],
            Online = online,
            OfflineSeconds = online ? 0u : Next(86400 * 7) + 60,
            Day = DateTime.Now.ToString("yyyy-MM-dd"),
            Issue = alarm == AlarmFlags.None ? string.Empty : "报警",
            Alarm = alarm,
            Lon = 113.572906 + (Next(4000) - 2000) / 10000.0,
            Lat = 42.443101 + (Next(3000) - 1500) / 10000.0,
        };
    }

    public void StartRealtime()
    {
        _timer ??= new Timer(_ =>
        {
            var batch = new List<VehicleRow>(24);
            for (var i = 0; i < 24; i++)
            {
                // 偏向低位 index：让用户当前看到的首页确实是"活的"
                var idx = i % 3 == 0 ? Random.Shared.Next(1, (int)TotalVehicles) : Random.Shared.Next(1, 400);
                var row = Generate((uint)idx);
                if (row.Online) row.SpeedKph = Random.Shared.Next(0, 96);
                batch.Add(row);
            }

            if (_uiQueue is null) VehiclesPushed?.Invoke(this, batch);
            else _uiQueue.TryEnqueue(DispatcherQueuePriority.Normal, () => VehiclesPushed?.Invoke(this, batch));
        }, null, 800, 800);
    }

    public void StopRealtime()
    {
        _timer?.Dispose();
        _timer = null;
    }

    public void Dispose() => StopRealtime();
}

namespace Vms.Client.Models;

/// <summary>底部状态栏的全局统计（对应 <c>vms_stats</c>）。</summary>
public sealed record SystemStats(
    uint Total,
    uint Online,
    uint Offline,
    uint Excluded,
    uint AlarmUnhandled,
    float OnlineRate)
{
    public static readonly SystemStats Empty = new(0, 0, 0, 0, 0, 0f);

    public string TotalText => Total.ToString("N0");
    public string OnlineText => Online.ToString("N0");
    public string OfflineText => Offline.ToString("N0");
    public string ExcludedText => Excluded.ToString("N0");
    public string OnlineRateText => OnlineRate.ToString("F2") + "%";
    public string AlarmUnhandledText => AlarmUnhandled.ToString("N0");
}

/// <summary>
/// 组织机构扁平记录（native 返回形态），由服务层装配成树。
/// <paramref name="OrgId"/> 是库表 d_org_info.org_id 的字符串主键（可能是 UUID），
/// 与 <see cref="Id"/>（UI 内部 uint32 序号）不是一回事：向后台检索组织/车辆时用 OrgId。
/// </summary>
public readonly record struct CompanyRecord(uint Id, uint ParentId, uint Online, uint Count, string Name, string OrgId = "");

/// <summary>车辆列表查询条件（对应 <c>vms_query</c>）。</summary>
public sealed class VehicleQuery
{
    /// <summary>
    /// 组织过滤：<b>0 = 全部；非 0 = 该组织及其所有下级组织</b>（子树语义）。
    /// 车辆实体挂在最末级单位上，若按 id 精确匹配，"点北京分公司"会一台车都查不到。
    /// native 侧同名语义见 <c>vms_core.c: company_in_subtree</c>。
    /// </summary>
    public uint CompanyId { get; set; }
    public uint Offset { get; set; }
    public uint Limit { get; set; } = 200;
    public bool OnlineOnly { get; set; }
    public bool AlarmOnly { get; set; }
    public uint AlarmMask { get; set; } = 0xFFFF;
    public string Keyword { get; set; } = string.Empty;

    /// <summary>
    /// v5：组织过滤的<b>首选写法</b> —— 直接给库表主键 <c>d_org_info.org_id</c>。
    /// 非空时优先于 <see cref="CompanyId"/>：省掉"uint32 序号 → 字符串"的翻译，
    /// 也无需本机已加载组织树。子树语义不变（该组织及其所有下级）。
    /// </summary>
    public string OrgId { get; set; } = string.Empty;

    /// <summary>
    /// 车辆<b>主键</b>精确过滤：非空时<b>只</b>返回这些主键的车辆，
    /// 用于「在树上勾选若干车辆 → 定位数据栏只显示这些」。
    /// 为空（<see cref="Ids"/>.Count == 0）表示不过滤。
    /// <para>
    /// v5 起元素是<b>字符串</b>（<c>d_vehicleinfo.vehicle_id</c>，形如 <c>京A12345-1</c>），
    /// 直接填 <see cref="VehicleRow.VehicleId"/>，不再用 uint32 行号。
    /// </para>
    /// </summary>
    public List<string> Ids { get; set; } = new();

    /// <summary>
    /// 强制空结果（v2 新增，纯托管标志，不进 native 结构体）。
    /// 用于「未勾选任何车辆 / 组织机构」时让定位数据栏保持空、不加载：
    /// 置 true 后两个数据源都直接返回 0 行，避免"一上来就把 3 万台车灌进列表"。
    /// </summary>
    public bool NoResults { get; set; }

    /// <summary>
    /// Ids 精确过滤的硬上限（必须与 native 结构体 <c>vms_query.ids[128]</c> 一致）。
    /// 超过这个数量的"选中集合"不能走 Ids 过滤，须改用组织子树（CompanyId / OrgId）过滤。
    /// </summary>
    public const int MaxIdFilter = 128;

    public VehicleQuery Clone()
    {
        var copy = (VehicleQuery)MemberwiseClone();
        copy.Ids = new List<string>(Ids);   // 避免与源查询共享同一个列表
        return copy;
    }
}

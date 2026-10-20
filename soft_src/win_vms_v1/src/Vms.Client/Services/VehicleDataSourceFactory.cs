using Microsoft.UI.Dispatching;
using Vms.Client.Interop;
using Vms.Client.Models;

namespace Vms.Client.Services;

/// <summary>数据源装配结果：数据源本身 + 诊断信息（供状态栏展示 native 自检结论）。</summary>
public sealed record VehicleDataSourceBundle(IVehicleDataSource Source, NativeProbeResult Probe);

/// <summary>
/// 数据源工厂。策略：**优先 native，失败即降级并如实告知用户**。
/// 不静默吞掉 native 的失败 —— 否则"为什么我的 C 库没被调用"会查很久。
/// </summary>
public static class VehicleDataSourceFactory
{
    public static VehicleDataSourceBundle Create(DispatcherQueue? uiQueue)
    {
        var probe = VmsCoreClient.Probe();
        if (!probe.Success)
            return new VehicleDataSourceBundle(new MockVehicleDataSource(uiQueue), probe);

        try
        {
            var client = VmsCoreClient.Create(
                configJson: """{"app":"Vms.Client","ui":"winui3"}""",
                endpoint: Environment.GetEnvironmentVariable("VMS_ENDPOINT"));

            return new VehicleDataSourceBundle(
                new NativeVehicleDataSource(client, probe.Version ?? "unknown"),
                probe);
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[Vms] native 初始化失败，降级：{ex}");
            return new VehicleDataSourceBundle(
                new MockVehicleDataSource(uiQueue),
                NativeProbeResult.Fail($"native 初始化失败，已降级：{ex.Message}"));
        }
    }
}

/// <summary>把 native 返回的扁平组织记录装配成 <see cref="CompanyNode"/> 树。</summary>
public static class CompanyTreeBuilder
{
    public static IReadOnlyList<CompanyNode> Build(IReadOnlyList<CompanyRecord> flat)
    {
        var map = new Dictionary<uint, CompanyNode>(flat.Count);
        foreach (var c in flat)
            map[c.Id] = new CompanyNode { Id = c.Id, ParentId = c.ParentId, Name = c.Name, Online = c.Online, Count = c.Count };

        var roots = new List<CompanyNode>();
        foreach (var node in map.Values)
        {
            if (node.ParentId != 0 && map.TryGetValue(node.ParentId, out var parent))
                parent.AddChild(node);
            else
                roots.Add(node);
        }

        // 根节点计数 = 自身 + 后代累计？（此处保留后端给的原始值，不做二次计算：
        // 真实系统里"未分配车辆"不算在任何分公司下，前端擅自累加反而会和后端对不上）
        foreach (var root in roots)
            root.IsExpanded = true;

        return roots;
    }
}

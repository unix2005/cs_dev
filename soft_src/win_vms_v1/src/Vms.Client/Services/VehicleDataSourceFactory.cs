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
    /// <summary>
    /// 从 <b>XML 配置文件</b>装配登录参数（v7 起取代环境变量取参）。
    ///
    /// 路径优先级：显式传参 &gt; 环境变量 <c>VMS_CLIENT_CONF</c> &gt; 平台惯例
    /// （Windows %LOCALAPPDATA%\vms\vms_client.xml，由 native 给出）。
    /// 解析在 native 侧完成，键名与默认值三端共用一份。
    ///
    /// 返回 <c>(Options, Error)</c>：Error 非空即不可登录，且已是可直接展示给人的原因
    /// （区分"文件不存在"与"XML 语法错"，排查方向完全不同）。
    /// 配置里没有口令时也算失败 —— 口令不该落盘，应由登录框输入。
    /// </summary>
    public static (LoginOptions? Options, string? Error) TryCreateLoginOptionsFromConfig(
        string? configPath = null)
    {
        var r = ClientConfigService.Load(configPath);
        if (!r.Success)
            return (null, r.Error);

        if (r.Error is not null)
            return (null, $"配置不完整：{r.Error}");

        var c = r.Config!;

        // 口令不落盘是合规红线：配置里没有就走登录框，不偷偷尝试空口令。
        if (c.NeedsPassword)
            return (null, "配置未含口令（password 留空）—— 请由登录框输入，口令不得写入配置文件");

        return (new LoginOptions(
            Host: c.Host,
            Port: c.Port,
            User: c.User,
            Password: c.Password!,
            Otp: c.Otp,
            DeviceIdHex: c.DeviceIdHex,
            ServerPubKeyPath: c.PubKeyPath,
            KnockEndpoint: c.KnockEndpoint,
            SpaKeyPath: c.SpaKeyPath,
            Pbkdf2Iterations: c.Pbkdf2Iterations), null);
    }

    /// <summary>当前生效的配置摘要（供状态栏 / 诊断页展示；口令不出现在其中）。</summary>
    public static string? DescribeConfig(string? configPath = null)
    {
        var r = ClientConfigService.Load(configPath);
        return r.Success ? r.Config!.Summary : r.Error;
    }

    private static string? NullIfEmpty(string? s) => string.IsNullOrWhiteSpace(s) ? null : s;
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

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
    /// 从环境变量装配登录参数（M2：先把「状态栏显示真实数据」这条链路打通，
    /// 正式登录对话框 / 凭据保管留到 M3）。
    ///
    /// 必需：VMS_HOST / VMS_USER / VMS_PASS / VMS_PUBKEY
    /// 可选：VMS_PORT(默认18099) / VMS_OTP / VMS_DEVID_HEX / VMS_KNOCK / VMS_SPA_KEY / VMS_PBKDF2_ITER
    /// 必需项不全时返回 null —— 此时 native 仍返回内置 Mock，界面照常工作。
    ///
    /// 注意口令走环境变量只为联调方便，**生产环境不要这么做**（进程环境可被同权限进程读取），
    /// 应改为登录框输入 + Windows 凭据管理器保管。
    /// </summary>
    public static LoginOptions? TryCreateLoginOptionsFromEnvironment()
    {
        var host = Environment.GetEnvironmentVariable("VMS_HOST");
        var user = Environment.GetEnvironmentVariable("VMS_USER");
        var pass = Environment.GetEnvironmentVariable("VMS_PASS");
        var pub  = Environment.GetEnvironmentVariable("VMS_PUBKEY");

        if (string.IsNullOrWhiteSpace(host) || string.IsNullOrWhiteSpace(user)
            || string.IsNullOrWhiteSpace(pass) || string.IsNullOrWhiteSpace(pub))
            return null;

        var port = 18099;
        if (int.TryParse(Environment.GetEnvironmentVariable("VMS_PORT"), out var p) && p is > 0 and <= 65535)
            port = p;

        _ = int.TryParse(Environment.GetEnvironmentVariable("VMS_PBKDF2_ITER"), out var iter);

        return new LoginOptions(
            Host: host,
            Port: port,
            User: user,
            Password: pass,
            Otp: NullIfEmpty(Environment.GetEnvironmentVariable("VMS_OTP")),
            DeviceIdHex: NullIfEmpty(Environment.GetEnvironmentVariable("VMS_DEVID_HEX")),
            ServerPubKeyPath: pub,
            KnockEndpoint: NullIfEmpty(Environment.GetEnvironmentVariable("VMS_KNOCK")),
            SpaKeyPath: NullIfEmpty(Environment.GetEnvironmentVariable("VMS_SPA_KEY")),
            Pbkdf2Iterations: iter);
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

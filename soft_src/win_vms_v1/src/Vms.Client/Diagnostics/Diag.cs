using System.Text;

namespace Vms.Client.Diagnostics;

/// <summary>
/// 诊断开关：用环境变量 <c>VMS_DIAG</c>（逗号分隔，可组合）逐条打开/关闭特定路径，
/// 用于隔离定位本机层访问违例（0xC0000005）。这些崩溃托管层拦不住，
/// 只能靠"关掉某条路径看还崩不崩"来二分。
///
///   关功能（用于隔离变量）：
///     noalarm   不往报警集合里插
///     nopos     不做定位行的就地刷新
///     nomap     不初始化地图、不推打点
///   加压（用于把平时要人手操作才出现的路径压出来）：
///     alarmtab  启动就切到报警页签（让报警集合可见，走重建路径）
///     scroll    每 2 秒把定位列表往下滚一屏（触发容器生成 + 触底预取）
///     treetest  启动后自动展开一个"有下级单位的组织"及其第一个叶子单位并选中它
///               （树的多层渲染 / 父组织子树过滤 的无人值守回归取证）
///     treecheck 启动后自动展开一个叶子组织、加载车辆、勾选其中 2 台，
    ///               断言 Feed.Query.Ids / Feed.Count / Positions.Count 都只含这 2 台，
    ///               验证"树上勾选车辆 → 定位数据栏联动"链路（结果写 treecheck.log）。
    ///               用于无人值守回归取证（WinUI 无法用注入鼠标点击驱动）。
    ///     fontcheck 启动后实测各字体的渲染宽度，验证全局字体是否真的落到 Noto Sans SC
    ///               （结果写 fontcheck.log）。字体回退是运行期行为，不能靠读代码断定。
    ///
    /// 例：<c>set VMS_DIAG=alarmtab,scroll</c>
    /// </summary>
internal static class Diag
{
    private static readonly string[] Modes = (Environment.GetEnvironmentVariable("VMS_DIAG") ?? string.Empty)
        .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

    private static bool Has(string mode) => Array.Exists(Modes, m => string.Equals(m, mode, StringComparison.OrdinalIgnoreCase));

    public static bool SkipAlarmInsert => Has("noalarm");
    public static bool SkipPositionUpdate => Has("nopos");
    public static bool SkipMap => Has("nomap");

    public static bool StartOnAlarmTab => Has("alarmtab");
    public static bool AutoScroll => Has("scroll");
    public static bool TreeSmokeTest => Has("treetest");
    public static bool VehicleSelectionCheck => Has("treecheck");
    public static bool FontCheck => Has("fontcheck");
    /// <summary>三态演示：启动后让某个含≥2下级的组织进入"部分选择"(null)，便于截图看半选框。</summary>
    public static bool TriStateDemo => Has("tristate");

    /// <summary>
    /// 组织复选框点击探针：启动后用**框架自己的点击处理器**（反射调 <c>ToggleButton.OnClick</c>）
    /// 真实点某个组织复选框三次，逐步记录控件值/模型值/联动结果（写 diagtrace.log）。
    /// 存在的理由：三态 CheckBox 的"点击循环顺序"与"TwoWay 回写时机"都是运行期行为，
    /// 靠读代码推不出来（本项目已在"点了没反应/对号不显示"上栽过一轮，就是推理代替实测的代价）。
    /// </summary>
    public static bool OrgClickProbe => Has("orgclick");

    /// <summary>是否处于任意诊断模式。</summary>
    public static bool IsActive => Modes.Length > 0;

    private static readonly List<string> TraceLines = [];
    private static readonly object TraceLock = new();

    /// <summary>诊断打点。仅在 VMS_DIAG 非空时记录 —— 正常运行零开销。</summary>
    public static void Log(string line)
    {
        if (!IsActive) return;
        lock (TraceLock) TraceLines.Add($"[{DateTime.Now:HH:mm:ss.fff}] {line}");
    }

    /// <summary>把打点落盘到 <c>%LocalAppData%\Vms.Client\diagtrace.log</c>。</summary>
    public static void FlushTrace()
    {
        if (!IsActive) return;
        try
        {
            var dir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Vms.Client");
            Directory.CreateDirectory(dir);
            lock (TraceLock)
                File.WriteAllText(Path.Combine(dir, "diagtrace.log"),
                                  string.Join(Environment.NewLine, TraceLines),
                                  new UTF8Encoding(encoderShouldEmitUTF8Identifier: true));
        }
        catch
        {
            // 诊断设施绝不能成为崩溃源
        }
    }
}

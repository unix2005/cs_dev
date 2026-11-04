namespace Vms.Client.Models;

/// <summary>
/// M4：会话治理状态（托管镜像，对应 native 的 <c>vms_session_info</c>）。
/// 由 <see cref="Vms.Client.Interop.VmsCoreClient.GetSessionStatus"/> 与
/// <see cref="Vms.Client.Interop.VmsCoreClient.SessionStateChanged"/> 产出。
/// </summary>
/// <param name="State">见 <c>vms_session_state</c>：0 空闲 / 1 在线 / 2 重连中 / 3 重新登录中 / 4 已断开</param>
/// <param name="Attempts">当前连续失败次数（成功即归零）</param>
/// <param name="HbSent">已发出心跳次数</param>
/// <param name="HbFailed">心跳失败次数</param>
/// <param name="Reconnects">重建连接成功次数</param>
/// <param name="Relogins">自动重新登录成功次数</param>
/// <param name="Detail">最近一次事件描述（UTF-8）</param>
/// <param name="PushConnected">M5：推送长连接是否就绪（1 = 正在收轨迹/报警）</param>
/// <param name="PushSubs">M5：当前订阅的车辆数</param>
public sealed record SessionStatus(int State, uint Attempts, uint HbSent, uint HbFailed,
                                   uint Reconnects, uint Relogins, string Detail,
                                   uint PushConnected = 0, uint PushSubs = 0)
{
    public bool IsOnline  => State == 1;
    public bool IsDown    => State is 2 or 3 or 4;

    /// <summary>M5：推送通道真的连上（不是 mock 退化）。</summary>
    public bool IsPushConnected => PushConnected != 0;

    /// <summary>状态栏展示文案（运维视角，不用英文枚举名）。</summary>
    public string StateText => State switch
    {
        1 => "在线",
        2 => "连接中断，正在重连",
        3 => "票据失效，正在重新登录",
        4 => "已断开（需重新登录）",
        _ => "未连接",
    };

    public override string ToString()
        => $"{StateText}（心跳 {HbSent} 次 / 失败 {HbFailed} 次 / 重连 {Reconnects} 次 / 重登 {Relogins} 次）{Detail}";
}

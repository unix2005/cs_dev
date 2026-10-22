using System.Runtime.InteropServices;
using System.Text;

namespace Vms.Client.Interop;

/// <summary>
/// 与 <c>native/include/vms_api.h</c> 一一对应的托管结构体。
///
/// 铁律：
///  1. 字段顺序、类型宽度、对齐（Pack=8）必须与 C 头文件完全一致；
///  2. 结构体里 <b>只放 blittable / unmanaged 类型</b>（uint/float/double/fixed byte[]），
///     不要放 bool、string、class —— 一旦有托管引用字段，结构体就不可 blit，
///     托管数组与 C 数组之间只能逐元素 marshal，35k 行表格会直接被拖死；
///  3. 启动时用 <see cref="AbiGuard"/> 与本机 DLL 回报的 sizeof 交叉校验，不一致直接失败，
///     绝不允许"带着错误布局继续跑"（那是内存越界最难查的一类 bug）。
/// </summary>
internal enum NativeStatus
{
    Ok = 0,
    InvalidArg = -1,
    NoMemory = -2,
    NotFound = -3,
    BufferSmall = -4,
    NotConnected = -5,
    Timeout = -6,
    AbiMismatch = -7,
    Internal = -8,
}

/// <summary>对应头文件 <c>vms_struct_kind</c>，用于启动时的结构体尺寸自检。</summary>
internal enum NativeStructKind
{
    AbiInfo = 1,
    Stats = 2,
    Company = 3,
    Vehicle = 4,
    Query = 5,
    Credentials = 6,
    SessionInfo = 7,   // v6：M4 会话状态
    KeeperCfg = 8,     // v6：M4 会话治理参数
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct NativeAbiInfo
{
    public uint AbiVersion;
    public uint StructSize;
    public uint VerMajor;
    public uint VerMinor;
    public uint VerPatch;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct NativeStats
{
    public uint Total;
    public uint Online;
    public uint Offline;
    public uint Excluded;
    public uint AlarmUnhandled;
    public float OnlineRate;
    public uint Reserved;
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct NativeCompany
{
    public const int TextMax = 64;
    public const int KeyMax = 40;     // VMS_KEY_MAX：d_org_info.org_id CHAR(36)

    public uint Id;
    public uint ParentId;
    public uint Online;
    public uint Count;
    public uint ChildCount;
    public uint Reserved;
    public fixed byte Name[TextMax];
    public fixed byte OrgId[KeyMax];  // v4：库表字符串主键（可能是 UUID）
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct NativeVehicle
{
    public const int CodeMax = 24;
    public const int PlateMax = 16;
    public const int TextMax = 64;
    public const int TimeMax = 24;
    public const int IssueMax = 40;
    public const int KeyMax = 40;     // VMS_KEY_MAX

    public uint Id;
    public uint Online;
    public uint OfflineSeconds;
    public uint AlarmFlags;
    public float SpeedKph;
    public double Lon;
    public double Lat;
    public fixed byte SelfCode[CodeMax];
    public fixed byte Plate[PlateMax];
    public fixed byte Time[TimeMax];
    public fixed byte Location[TextMax];
    public fixed byte Day[TimeMax];
    public fixed byte Issue[IssueMax];
    // v4：库表字符串主键；后续按主键拉定位/视频时用这两个，不要用 uint Id（那只是 UI 行号）
    public fixed byte VehicleId[KeyMax];
    public fixed byte OrgId[KeyMax];
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct NativeQuery
{
    // 必须与 vms_api.h 的 VMS_QUERY_MAX_IDS 一致（v5: 256 -> 128）
    public const int QueryMaxIds = 128;
    public const int IdsBytes = QueryMaxIds * NativeVehicle.KeyMax;   // char[128][40]

    public uint CompanyId;
    public uint Offset;
    public uint Limit;
    public uint OnlineOnly;
    public uint AlarmOnly;
    public uint AlarmMask;
    public uint Reserved;
    public fixed byte Keyword[NativeVehicle.TextMax];
    // v5：库表字符串主键 d_vehicleinfo.vehicle_id（"车牌-颜色"），不再是 uint32 行号。
    // C# 不支持二维 fixed buffer，用一维字节块 + 手动步进（步长 = KeyMax）。
    public fixed byte Ids[IdsBytes];
    public uint IdCount;
    // v5：组织子树根，库表主键 d_org_info.org_id；非空时优先于 CompanyId
    public fixed byte OrgId[NativeVehicle.KeyMax];
}

/// <summary>
/// 登录凭据（对应 <c>vms_credentials</c>，ABI v3 新增）。
/// 字段顺序 / 长度 / Pack 必须与 vms_api.h 完全一致，启动自检会比对 sizeof。
/// 口令只在本次调用期间存在于本结构体（UTF-8 字节），**不得**落盘或写日志：
/// native 侧会本地派生 PBKDF2-SM3，外发的只有 32 字节派生值。
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct NativeCredentials
{
    public const int HostMax = 64;        // VMS_HOST_MAX
    public const int UserMax = 64;        // VMS_USER_MAX
    public const int PassMax = 64;        // VMS_PASS_MAX
    public const int OtpMax = 16;         // VMS_OTP_MAX
    public const int DevIdHexMax = 65;    // VMS_DEVID_HEX_MAX
    public const int PathMax = 260;       // VMS_PATH_MAX

    public fixed byte Host[HostMax];
    public ushort Port;
    public ushort Reserved16;             // 对齐填充，置 0
    public fixed byte User[UserMax];
    public fixed byte Password[PassMax];
    public fixed byte Otp[OtpMax];
    public fixed byte DevIdHex[DevIdHexMax];
    public fixed byte PubKeyPath[PathMax];  // 服务端 SM2 公钥 DER（必填，抗中间人）
    public fixed byte Knock[HostMax];       // "ip:port"；空 = 不敲门
    public fixed byte SpaKeyPath[PathMax];  // SPA 密钥文件（随 Knock 使用）
    public uint Pbkdf2Iter;                 // 0 = 用协议默认 100000
    public uint Reserved;
}

/// <summary>M4：会话状态（对应 <c>vms_session_state</c>）。</summary>
internal enum NativeSessionState
{
    Idle = 0,          // 未登录 / keeper 未启动
    Online = 1,        // 已认证，心跳正常
    Reconnecting = 2,  // 连接不可用，退避重连中
    Reauthing = 3,     // Token 失效，重新登录中
    Offline = 4,       // 连续失败超上限或缺凭据，需 UI 介入
}

/// <summary>
/// M4：会话状态与统计（对应 <c>vms_session_info</c>，ABI v6）。
/// 时间字段是 native 进程内的<b>单调毫秒</b>（GetTickCount64 / CLOCK_MONOTONIC），
/// 不是墙上时间 —— 只能用来算间隔，不要当日期显示。
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal unsafe struct NativeSessionInfo
{
    public const int DetailMax = 96;

    public int State;
    public uint Attempts;
    public uint HbSent;
    public uint HbFailed;
    public uint Reconnects;
    public uint Relogins;
    public ulong LastOkMs;
    public ulong NextHbMs;
    public ulong NextRetryMs;
    public fixed byte Detail[DetailMax];
}

/// <summary>M4：会话治理参数（对应 <c>vms_keeper_cfg</c>）。全 0 = 交给 native 取默认。</summary>
[StructLayout(LayoutKind.Sequential, Pack = 8)]
internal struct NativeKeeperCfg
{
    public uint HbIntervalSec;   // 0 = 60
    public uint BackoffBaseSec;  // 0 = 1（序列 1/2/4/8/16/30 封顶，带 ±20% 抖动）
    public uint BackoffMaxSec;   // 0 = 30
    public uint MaxRetry;        // 0 = 无限重连
    public uint TickMs;          // 0 = 500
    public uint Fault;           // 故障注入，生产恒 0
}

/// <summary>从 <c>fixed byte[]</c> 读 UTF-8 字符串的小工具，零分配读取路径。</summary>
internal static unsafe class Utf8
{
    public static string Read(byte* p, int maxBytes)
    {
        if (p is null || maxBytes <= 0) return string.Empty;
        var span = new ReadOnlySpan<byte>(p, maxBytes);
        int len = span.IndexOf((byte)0);
        if (len < 0) len = maxBytes;
        return len == 0 ? string.Empty : Encoding.UTF8.GetString(span[..len]);
    }

    public static void Write(byte* dst, int maxBytes, string? value)
    {
        if (dst is null || maxBytes <= 0) return;
        var span = new Span<byte>(dst, maxBytes);
        span.Clear();
        if (string.IsNullOrEmpty(value)) return;

        var bytes = Encoding.UTF8.GetBytes(value);
        int n = Math.Min(bytes.Length, maxBytes - 1);
        // 不要把多字节字符截成半个（否则 native 侧逆解码会拿到乱码）
        while (n > 0 && n < bytes.Length && (bytes[n] & 0xC0) == 0x80) n--;
        bytes.AsSpan(0, n).CopyTo(span);
    }
}

// 玩家设置：鼠标灵敏度、反转 Y、视野、音量、菜单配色、HUD 主题、键位存在 GameUserSettings.ini 的 [MechSettings] 段；
// （11-01 去掉了「按键提示」：HUD 早就不画按键提示了，这个开关没有作用；ini 里残留的 ShowHints 不读）
// 画质、分辨率、窗口模式、垂直同步、帧率上限走引擎的 UGameUserSettings（同一个 ini）。

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "MechTypes.h"
#include "MechRules.h"

class APlayerController;

/** 能在设置里改键的操作（设置页「按键」按这个顺序列）。手柄映射固定不改；Esc（菜单）、F1（光影面板）不在表里 */
enum class EMechKey : uint8
{
	MoveForward,
	MoveBack,
	MoveLeft,
	MoveRight,
	Jump,         // 跳跃 / 空中按住向上喷射 / 空中贴墙蹬墙跳
	Boost,        // 冲刺（按住；空中滑翔）
	QuickBoost,   // 快速闪避
	Stance,       // 切换射击 / 近战
	Fire,         // 开火 / 攻击
	Alt,          // 瞄准 / 格挡
	Shoulder,     // 肩部装备（11-01 战术装备并入肩部，C 键和这里的 Tactical 一项去掉；键位按名字存，老存档 ini 里多的 Key_Tactical 没人读，不影响别的键）
	Reload,
	FirstPerson,  // 第一人称切换
	Map,          // 按住看大地图（夺芯）
	Scoreboard,   // 按住看计分板
	ThrowCore,    // 夺芯：把带着的核心扔出去
	Ping,         // 标点（11-01，仿 Apex：点一下标点 / 敌人，双击标危险，按住出轮盘；默认鼠标中键）
	Count
};

namespace MechKeys
{
	constexpr int32 Num = static_cast<int32>(EMechKey::Count);
}

struct FMechSettings
{
	/** 鼠标灵敏度，和 CS2 一个意思：每个鼠标计数 = MouseSensitivity x 0.022 度（11-02；以前是 0.1-3 的倍率）。
	 *  ZoomSensitivityRatio 对应 zoom_sensitivity_ratio：狙击开镜时再乘这个倍率。 */
	float MouseSensitivity = 8.f;
	float ZoomSensitivityRatio = 0.3f;
	/** 11-02 用户：开镜灵敏度独立。右键瞄准、狙击开镜各一个独立的 CS2 数字（不再是主灵敏度的倍率，改主灵敏度不跟着变）。
	 *  旧设置迁移（SensVersion < 3）：瞄准 = 主灵敏度 x 0.6（以前写死的），开镜 = 主灵敏度 x ZoomSensitivityRatio。ZoomSensitivityRatio 只留着迁移用 */
	float AimSensitivity = 4.8f;
	float ScopeSensitivity = 2.4f;
	/** CS2 灵敏度 1 = 每计数多少度 */
	static constexpr float DegreesPerCount = 0.022f;
	/** 引擎里除了 InputYawScale 以外，每个鼠标计数进到 DoLook 时要乘的倍数 = 增强输入的 Mouse2D 轴灵敏度 0.07（实测 -MechLookCalib：注入 100 计数，DoLook 收到 7.0） */
	static constexpr float LookEngineExtra = 0.07f;
	bool bInvertY = false;
	float FieldOfView = 90.f;
	float MasterVolume = 1.f;
	/** 战斗界面告警音（被锁定、导弹来袭、低装甲……，UMechHudAlerts）的音量，再乘主音量。默认偏小：告警是提醒，不该盖过枪炮声 */
	float AlertVolume = 0.6f;
	/** 告警语音（10-31）：0 关、1 冷静女声、2 机械合成、3 企业客服；音量跟「告警音量」走 */
	int32 AlertVoice = 1;
	/** 告警语音是不是玩家手动选的（10-31 HUD 主题）：没选过 = 「跟随机体」，用本机框架制造商的主题默认
	 *  （新纪 冷静女声、维克托 机械合成、相位 企业客服，MechHudTheme）；ini 里没有这一项时，AlertVoice 不是默认的 1 就当选过 */
	bool bAlertVoiceSet = false;
	/** 音乐音量（11-01）：背景音乐（MechMusic，每帧读）和结算场景的配乐，再乘主音量 */
	float MusicVolume = 0.7f;
	/** 菜单配色（11-01，MechUITheme::Preset 的编号：0 青色、1 琥珀、2 冷白、3 赤红、4 荧绿） */
	int32 MenuPalette = 0;
	/** 战斗 HUD 主题（11-01）：-1 = 跟随机体（按本机框架的制造商，原来的做法），0–3 = 固定用那一套（EMechHudThemeId）。
	 *  命令行 -MechHudTheme= 仍然优先 */
	int32 HudTheme = -1;
	/** 第一人称（机体自己的眼睛）；游戏里按 V 切换也会记下来 */
	bool bFirstPerson = false;
	/** 动态模糊（10-30 用户要求做成可选；关掉时镜头后期的模糊量为 0） */
	bool bMotionBlur = true;
	/** 信息区（11-01，类似 CS2 右上角：击杀、队友用技能、拿插件、标点……）；关掉就不画 */
	bool bShowFeed = true;
	/** 房间：开打前要不要禁用阶段（房主上次的选择） */
	bool bDraftBans = true;
	/** 房间：对战规则（模式、团队淘汰局数、死斗个人 / 团队、目标击毁数、时间；房主上次的选择，ini 里 MatchMode / ElimRounds / DMSolo / DMTeamTarget / DMSoloTarget / DMTime） */
	FMechRules Rules;
	/** 测试场里最后应用的配装（下次进测试场还是这一套） */
	FMechLoadout Loadout;
	/** 玩家名称（空 = 显示「玩家 N」，单机显示「你」） */
	FString PlayerName;
	/** 机体涂装 */
	FMechPaint Paint;
	/** 键位（按 EMechKey 编号；读 ini 之前是默认键） */
	FKey Keys[MechKeys::Num];

	/** 进程里一份，第一次取的时候从 ini 读 */
	static FMechSettings& Get();
	void Load();
	void Save() const;
	/** 只存键位（改一个键就存） */
	void SaveKeys() const;
	/** 主音量（视野、灵敏度机体每帧自己读） */
	void ApplyAudio(const UObject* WorldContext) const;
};

namespace MechKeys
{
	FKey Default(EMechKey Action);
	/** 当前绑定的键（设置里的；没改过就是默认） */
	FKey Key(EMechKey Action);
	/** 设置页上的操作名 */
	FText Label(EMechKey Action);
	/** 键名（中文：鼠标左键、空格、左 Shift……） */
	FText KeyName(const FKey& Key);
	/** 能不能绑：键盘键和鼠标按键可以；Esc（菜单）、F1（光影面板）、手柄、鼠标移动 / 滚轮不行 */
	bool CanBind(const FKey& Key);
	/** 改键并存盘：这个键已经给了别的操作就和它互换，返回被换的那个操作（没换返回 Count）。
	 *  调用方再调 AMechCharacter::ApplyKeyBindings() 让输入映射跟上 */
	EMechKey Rebind(EMechKey Action, const FKey& Key);
	/** 全部恢复默认并存盘 */
	void ResetAll();
	/** 本机玩家这个操作的键现在按着 / 这一帧刚按下（HUD、第一人称这类不走增强输入、直接查键的地方用） */
	bool IsDown(const APlayerController* PC, EMechKey Action);
	bool JustPressed(const APlayerController* PC, EMechKey Action);
}

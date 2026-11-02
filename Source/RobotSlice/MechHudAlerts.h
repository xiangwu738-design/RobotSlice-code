// 战斗界面的战机式告警（10-31，用户：「给现在的 UI 增加一点沉浸感，比如类似于战机的警报」）。
// 玩家就是这台机器：HUD 是机体自己的系统界面，告警是它的「感觉」。规矩照 HUD：不写中文、不堆在准星周围，靠图形、颜色、声音。
//
// 做了七项（每项的触发条件、声音、图形见 MechHudAlerts.cpp 开头和 README「告警」）：
//  1 被锁定（RWR）：敌人导弹在锁 / 锁上（导弹就绪时）、狙击镜对着自己、充能步枪照着自己、火炮架着对着自己
//  2 导弹来袭：越近哔得越快；新的一批飞来时先响一声发射告警
//  3 低装甲主告警（Master Caution）：装甲 < 25% 屏幕四边暗红呼吸 + 低沉的两音循环（响三轮后降一半音量，< 12% 又加急）
//  4 系统告警：推进耗尽 / 恢复、失衡槽将满、被网住、被钩爪拖拽，各一个短提示音；仪表盘上一排告警灯
//  5 中 EMP：HUD 横向撕裂、闪断、读数乱跳、偏冷色，开头最狠、之后断断续续
//  6 受重击：整个 HUD 抖一下（重的再撕一道）
//  7 上电 / 断电：出生、重生时 HUD 从下往上逐块闪亮 + 告警灯自检；被击毁时 HUD 闪两下收成一条线熄灭
// 混音：循环通道同时只响一种（导弹来袭 > 锁定 > 跟踪 > 低装甲），短提示音可以叠加、各自限流；音量 = 设置里的「告警音量」× 主音量。
// 只有自己操控的机体出声（观战只看图形）。
//
// AMechHUD 在 DrawHUD 里各块画完后调 Draw（判断 + 声音 + 告警图形），最后调 PostProcess（对整帧做抖动 / 撕裂 / 上电 / 断电）。
// 画图借 AMechHUD 的小工具（friend）。测试：-MechAlertTest（MechAlertTest.cpp）。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MechHudAlerts.generated.h"

class AMechHUD;
class AMechCharacter;
class USoundBase;
class UAudioComponent;
class UMechHudAlerts;
struct FMechHudFrame;


/** 循环通道里响的告警（数值越大越重要，同时只响最重要的一种） */
enum class EMechAlertLoop : uint8
{
	None,
	/** 低装甲 */
	Caution,
	/** 被跟踪（锁定还没完成 / 狙击镜在附近扫） */
	Track,
	/** 被锁定 */
	Lock,
	/** 导弹来袭 */
	Missile
};

/** 仪表盘上的告警灯 */
enum class EMechAlertLamp : uint8
{
	Missile,   // MSL 导弹来袭
	Lock,      // LCK 被锁定（琥珀 = 跟踪，红 = 锁定）
	Armor,     // AP  低装甲
	Energy,    // EN  推进耗尽 / 过热
	Balance,   // BAL 失衡槽将满 / 失衡中
	System,    // SYS EMP、被网住、被拖拽
	Count
};

/** 告警语音（10-31，用户：「警报加一些机械配音吧」→ 英文短句，三种风格设置里切换）。数值越大越重要的在 MechHudAlerts.cpp 的表里。
 *  持续性的（导弹来袭、被锁定、狙击、装甲、推进、失衡、网、拖拽）状况持续期间按间隔重复，解除就停；一次性的（上电、EMP、被击毁）只说一次 */
enum class EMechVoice : uint8
{
	Online,     // SYSTEMS ONLINE（上电，一次）
	Missile,    // MISSILE INBOUND → MISSILE
	Lock,       // LOCKED（导弹锁定完成 / 充能步枪照着 / 火炮架着对着）
	Sniper,     // SNIPER（狙击开镜对着自己）
	ArmorLow,   // ARMOR LOW
	ArmorCrit,  // ARMOR CRITICAL
	Thrust,     // THRUST DEPLETED
	Balance,    // BALANCE CRITICAL
	Emp,        // SYSTEM FAILURE（EMP 刚中，一次）
	Net,        // RESTRAINED
	Drag,       // TETHERED
	Lost,       // UNIT LOST（被击毁，一次）
	Count
};

/** -MechAlertTest（测试场 /Game/Maps/L_Range?Range）：本机玩家那台挨个触发各项告警并截图到 Saved/Screenshots/Alerts，日志 [alert]，
 *  约 45 秒后退出。实现在 MechAlertTest.cpp，由 UMechHudAlerts::Draw 每帧调 */
namespace MechAlertTest
{
	void Tick(AMechHUD& Hud, UMechHudAlerts& Alerts, AMechCharacter* Me, float Dt);
	const TCHAR* LoopName(EMechAlertLoop Loop);
}

UCLASS()
class UMechHudAlerts : public UObject
{
	GENERATED_BODY()

public:
	UMechHudAlerts();

	/** DrawHUD 里各块 HUD 画完之后调：判断告警、放声音、画告警图形（威胁方向、告警灯、低装甲呼吸） */
	void Draw(AMechHUD& Hud, AMechCharacter* Mech);
	/** DrawHUD 最后调：对整帧做受击抖动、EMP 撕裂 / 闪断 / 读数乱跳、上电逐块亮起，被击毁时把最后一帧收成一条线 */
	void PostProcess(AMechHUD& Hud, FMechHudFrame& Frame, AMechCharacter* Mech);

	/** 设置页拖「告警音量」时按新音量响一下（限流） */
	static void PreviewVolume(const UObject* WorldContext);
	/** 设置页点告警语音风格时按那种风格说一句（1 冷静女声、2 机械合成、3 企业客服；0 关不出声） */
	static void PreviewVoice(const UObject* WorldContext, int32 Style);
	/** 现在用的语音风格：设置里的，命令行 -MechAlertVoice=N 覆盖（测试用） */
	static int32 VoiceStyle();

	// ---------------------------------------------------------------- 测试（-MechAlertTest）
	/** 现在循环通道响的是哪种 */
	EMechAlertLoop GetLoop() const { return Loop; }
	/** 重放一次上电（截图用） */
	void DebugPowerUp(AMechHUD& Hud);
	/** 每个声音响过几次（测试结束时汇总进日志，查「该响的响了」） */
	TMap<FString, int32> SoundCounts;
	/** 敌人枪线（胸口 → 摆姿势瞄准点）从自己身边擦过多远（厘米）；朝反方向返回很大的数（测试也用来记日志） */
	static float AimMiss(const AMechCharacter* Enemy, const AMechCharacter* Me);
	/** 告警灯现在亮着的（0 灭、1 琥珀、2 红） */
	uint8 GetLamp(EMechAlertLamp L) const { return LampLevel[static_cast<int32>(L)]; }

private:
	struct FThreat
	{
		TWeakObjectPtr<AActor> Source;
		FVector Pos = FVector::ZeroVector;
		/** 1 跟踪、2 锁定、3 导弹来袭、4 拖拽（钩爪拉自己的那台） */
		uint8 Level = 0;
		float Dist = 0.f;
	};

	/** 第一次用时按路径加载音效（没导入就是空的：告警只剩图形，不报错） */
	void EnsureSounds();
	bool bSoundsLoaded = false;

	// ---- 判断
	void Gather(AMechHUD& Hud, AMechCharacter* Mech, float Real, bool bMine);
	/** 敌人胸口到自己胸口之间没有墙（每台约 0.12 秒查一次） */
	bool HasLos(AMechCharacter* Enemy, const AMechCharacter* Me, float Real);
	void UpdateLoop(AMechHUD& Hud, float Real, bool bMine, float APFrac);
	void SetLamp(EMechAlertLamp L, uint8 Level, float Real);

	// ---- 声音
	/** 短提示音（可以叠加；同一种 MinGap 秒内只响一次） */
	void Cue(AMechHUD& Hud, USoundBase* Sound, const TCHAR* Name, float Volume, float Real, float MinGap = 1.f, float Pitch = 1.f);
	/** 循环通道响一下（先掐掉上一下） */
	void Beep(AMechHUD& Hud, USoundBase* Sound, const TCHAR* Name, float Volume, float Pitch = 1.f);
	void StopLoop();
	/** 说话时提示音压低一点（不和语音抢） */
	float Duck() const;

	// ---- 语音
	void EnsureVoice(int32 Style);
	/** 一次性的：Delay 秒后说（上电等上电声响完、断电等熄灭那一下） */
	void SayOnce(EMechVoice V, float Real, float Delay);
	/** 每帧：到点的一次性 + 持续性里最要紧那一条按间隔重复；高的打断低的 */
	void UpdateVoice(AMechHUD& Hud, float Real);
	void PlayVoice(AMechHUD& Hud, int32 V, bool bFull, float Real);
	bool IsSpeaking() const;
	void Count(const TCHAR* Name);

	// ---- 画
	void DrawThreats(AMechHUD& Hud, AMechCharacter* Mech, float Real);
	/** 屏幕边上朝 Ang（俯视方位角，0 = 正前方 = 屏幕上方，顺时针为正）的一段渐变光带 + 朝外的小箭头 */
	void EdgeBand(AMechHUD& Hud, float Ang, const FLinearColor& Color, float Thick, int32 Chevrons, float Real);
	void DrawLamps(AMechHUD& Hud, AMechCharacter* Mech, float Real);
	void DrawCaution(AMechHUD& Hud, float Real, float APFrac);
	/** 11-02 贴近会损毁的水面（石柱山水、荒野与废墟的湖）：准星下方远处一排向上的箭头 + PULL UP + 离水高度，配告警音 */
	void DrawWaterWarn(AMechHUD& Hud, AMechCharacter* Mech, float Real);

	// ---- 整帧效果
	void StartPowerUp(AMechHUD& Hud, float Real, bool bSound);
	void StartPowerDown(AMechHUD& Hud, float Real);
	void AppendCollapsed(AMechHUD& Hud, FMechHudFrame& F, float T);

	UPROPERTY()
	TObjectPtr<USoundBase> SndTrack;
	UPROPERTY()
	TObjectPtr<USoundBase> SndLock;
	UPROPERTY()
	TObjectPtr<USoundBase> SndMissile;
	UPROPERTY()
	TObjectPtr<USoundBase> SndLaunch;
	UPROPERTY()
	TObjectPtr<USoundBase> SndCaution;
	UPROPERTY()
	TObjectPtr<USoundBase> SndOverheat;
	UPROPERTY()
	TObjectPtr<USoundBase> SndThrustReady;
	UPROPERTY()
	TObjectPtr<USoundBase> SndStagger;
	UPROPERTY()
	TObjectPtr<USoundBase> SndNet;
	UPROPERTY()
	TObjectPtr<USoundBase> SndDrag;
	UPROPERTY()
	TObjectPtr<USoundBase> SndEmp;
	UPROPERTY()
	TObjectPtr<USoundBase> SndStatic;
	UPROPERTY()
	TObjectPtr<USoundBase> SndPowerDown;
	UPROPERTY()
	TObjectPtr<USoundBase> SndPowerUp;

	/** 循环通道正在响的那一下（自动销毁的音频组件；换告警时掐掉） */
	TWeakObjectPtr<UAudioComponent> LoopBeep;

	/** 语音：按 EMechVoice 编号的整句、重复句（没有重复句就用整句），当前加载的是哪种风格 */
	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> VoiceFull;
	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> VoiceRepeat;
	int32 LoadedVoiceStyle = -1;
	TWeakObjectPtr<UAudioComponent> VoiceComp;
	int32 VoiceNow = -1;
	int32 VoicePriority = 0;
	float VoiceEnd = 0.f;
	/** 这一帧哪些持续性状况在；这一段状况里整句说过没有（说过就改说重复句）；下次可以再说的时间 */
	bool VoiceActive[12] = {};
	bool VoiceSaid[12] = {};
	float VoiceNext[12] = {};
	/** 导弹来袭语音的重复间隔（按最近一发的距离，和哔声一起变密） */
	float MissileVoiceGap = 1.5f;
	struct FPendingVoice
	{
		int32 V = 0;
		float Due = 0.f;
	};
	TArray<FPendingVoice> PendingVoice;
	/** 这一帧锁定来源：狙击 / 别的（导弹锁定、充能、火炮） */
	bool bLockBySniper = false;
	bool bLockByOther = false;

	// ---- 状态（时间都用真实时间：击毁慢放、顿帧时 HUD 动画和告警节奏不跟着变慢）
	bool bLog = false;
	/** 这一帧看的是不是自己操控的机体（只有自己的出声） */
	bool bMineNow = false;
	float Dt = 0.f;
	TWeakObjectPtr<AMechCharacter> LastMech;
	bool bWasDead = false;
	float LastReal = 0.f;
	TArray<FThreat> Threats;
	int32 MaxMechLevel = 0;
	int32 MissileCount = 0;
	float ClosestMissile = 0.f;
	TSet<TWeakObjectPtr<AActor>> KnownMissiles;
	struct FLos
	{
		bool bClear = false;
		float Next = 0.f;
	};
	TMap<TWeakObjectPtr<AMechCharacter>, FLos> LosCache;

	EMechAlertLoop Loop = EMechAlertLoop::None;
	float NextBeep = 0.f;
	/** 循环通道最近一次响的时间（图形跟着这一下闪） */
	float LastBeep = -100.f;
	/** 低装甲：这一段低装甲已经响了几轮（三轮后降音量）；回到 30% 以上清零 */
	int32 CautionCycles = 0;
	float LastCautionBeep = -100.f;
	bool bLowArmor = false;

	// 系统状态（上一帧），判断「刚进入」
	bool bWasOverheat = false;
	bool bWasEmp = false;
	bool bWasNetted = false;
	bool bWasDragged = false;
	bool bStaggerArmed = true;
	TWeakObjectPtr<AMechCharacter> Puller;
	float LastHurtSeen = -100.f;
	TMap<FString, float> LastCue;

	// 告警灯：亮度级别、这一次亮起的时间（开头闪一会儿再常亮）；整排灯的透明度（都灭了一会儿后淡出）
	uint8 LampLevel[6] = {};   // 按 EMechAlertLamp 编号
	float LampSince[6] = {};
	float LampPanel = 0.f;
	float LampLastLit = -100.f;

	// 整帧效果
	float PowerUpStart = -100.f;
	float PowerDownStart = -100.f;
	float ShakeStart = -100.f;
	float ShakeAmp = 0.f;
	float ShakeLen = 0.3f;
	bool bShakeTear = false;
	float EmpStart = -100.f;
	float NextStatic = 0.f;
	/** 活着时每 0.1 秒存一份整帧（被击毁时拿它做「收成一条线」） */
	TSharedPtr<FMechHudFrame> Snap;
	float SnapTime = -100.f;
};

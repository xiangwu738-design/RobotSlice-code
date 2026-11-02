// 机体 AI：不用行为树，直接在 Tick 里按几条简单规则打——
// 选最近的敌人当目标；绕着目标横移、保持自己喜欢的距离（每台性格不同），时不时冲刺、跳起来喷射；
// 能看见就点射步枪，导弹锁上就齐射，目标失衡或贴得够近就冲上去砍；导弹来了或刚挨了一波就快速闪避。
// 撞墙就换横移方向或跳，跑出场地中心太远就往回绕。
// 高处逻辑（10-31，搁置中、默认关，-MechHighGround 才开）：有高处图（城区、相位电子园区，见 MechHighGround.h）的地图上，看不见目标时
// 按格子找路绕过去、交战时会喷射上天桥 / 高架 / 屋顶 / 基地台打一会儿再下来；没开或没有高处图的地图照旧。

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MechAIController.generated.h"

class AMechCharacter;

/** 人机强度一档的全部数值（11-01）：含义和四档的取值都在 MechAIController.cpp 开头的 MechAITier 表里 */
struct FMechAITierValues
{
	FVector2f React = FVector2f(0.8f, 1.f);
	FVector2f Lead = FVector2f(0.75f, 0.95f);
	float AimShake = 0.45f;
	float Spread = 2.2f;
	float AimLag = 0.f;
	float SpeedShake = 0.f;
	float ADS = 1.f;
	float BurstGap = 1.f;
	float SightDelay = 0.f;
	float SwingThink = 0.18f;
	float WhiffChance = 0.f;
	float WhiffFar = 0.9f;
	float NoLockChance = 0.f;
	float MeleeKeep = 500.f;
	FIntPoint Combo = FIntPoint(2, 4);
	FVector2f ComboRest = FVector2f(0.f, 0.f);
	float GuardScale = 1.f;
	FVector2f GuardLag = FVector2f(0.f, 0.f);
	float ParryScale = 1.f;
	float ParryJitter = 0.f;
	float HeavyPunish = 1.f;
	float DashSlash = 1.f;
	FVector2f Reengage = FVector2f(0.f, 0.f);
	float StanceGap = 1.f;
	float DodgeGap = 1.f;
	float DodgeHurt = 450.f;
	float BoostWill = 1.f;
	float JumpGap = 1.f;
	float ShoulderRate = 1.f;
	FVector2f MissileHold = FVector2f(0.f, 0.f);
	FVector2f IdleEvery = FVector2f(0.f, 0.f);
	FVector2f IdleLen = FVector2f(0.f, 0.f);
};

UCLASS()
class AMechAIController : public AAIController
{
	GENERATED_BODY()

public:
	AMechAIController();

	virtual void Tick(float DeltaSeconds) override;
	/** 测试（-MechFlightTest）：全部 AI 停脑子 */
	static bool bBrainOffForTest;

	/** 场地中心和半宽（厘米，水平矩形），AI 不跑出去；比中心高出 ArenaMaxHeight 就往回走（别待在屋顶上）。
	 *  有高处图的地图不用 ArenaMaxHeight：站上高处由高处逻辑管（待一会儿、从边上走下来） */
	FVector ArenaCenter = FVector::ZeroVector;
	FVector2D ArenaExtent = FVector2D(6000.f, 6000.f);
	float ArenaMaxHeight = 1200.f;

	/** 夺芯：导演（MechCoreDirector）每 0.5 秒派的任务；None 时照常打架 */
	enum class ECoreGoal : uint8 { None, Deliver, Rescue, Fetch, Intercept, Guard, Escort, Foundry };
	ECoreGoal CoreGoal = ECoreGoal::None;
	FVector CoreGoalLocation = FVector::ZeroVector;
	/** 拦截 / 护送 / 守站时跟着的那台（带芯者） */
	TWeakObjectPtr<AMechCharacter> CoreGoalMech;
	TWeakObjectPtr<class AMechCoreArena> CoreArena;
	/** 调试：当前任务、路径走到第几个点、下一个点在哪 */
	FString CoreDebug() const;

	/** 人机强度（11-01）：按档位套用数值（0 新兵、1 普通、2 老练、3 王牌）。bLegacy = 调整前的数值（保卫战的精英、-MechAITierLegacy）；
	 *  接管机体时按房间的 AILevel 自动调一次，-MechAITierTest 用它给每台单独定档 */
	void ApplyTier(int32 Level, bool bLegacy);
	int32 GetTierLevel() const { return TierLevel; }
	/** 测试（-MechAITierTest）：-1 自己决定拿枪还是拿刀，0 只用枪，1 只用刀 */
	int32 TestStance = -1;
	/** 测试（-MechAITierTest）：这台 AI 做了多少次决定 */
	struct FTierStats
	{
		int32 Dodges = 0;
		int32 Guards = 0;
		int32 ParryTries = 0;
		int32 Whiffs = 0;
		int32 NoLock = 0;
		int32 Idles = 0;
		int32 Shoulders = 0;
	};
	FTierStats TierStats;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	AMechCharacter* PickTarget(AMechCharacter* Self) const;
	bool HasLineOfSight(AMechCharacter* Self, AMechCharacter* Other) const;
	bool Blocked(AMechCharacter* Self, const FVector& Dir) const;

	/** 夺芯：沿路点图走向任务地点，算出这一帧的移动方向；返回 false = 已经到附近，交给原来的打法 */
	bool CoreMove(AMechCharacter* Self, float DeltaSeconds, FVector& OutMove, bool& bOutBoost);
	TArray<FVector> CorePath;
	TArray<uint8> CorePathKinds;
	int32 CorePathIndex = 0;
	float CoreRepath = 0.f;
	FVector CorePathGoal = FVector::ZeroVector;
	float CoreStuckClock = 0.f;
	FVector CoreStuckFrom = FVector::ZeroVector;

	// 性格
	float PreferredRange = 3800.f;
	float Aggression = 0.5f;
	float ReactionChance = 0.6f;

	// 状态
	float StrafeSign = 1.f;
	float StrafeTimer = 0.f;
	float RetargetTimer = 0.f;
	float BurstTimer = 0.f;
	bool bBurst = false;
	float BoostTimer = 0.f;
	float JumpTimer = 4.f;
	float JumpHold = 0.f;
	float DodgeTimer = 3.f;
	/** 被网住时下一次挣扎（快速闪避）的倒计时 */
	float StruggleTimer = 0.f;
	float SlashThinkTimer = 0.f;
	/** 姿态：近了拔刀、远了换枪，切换之间至少隔一会儿 */
	float StanceTimer = 2.f;
	/** 近战：这台 AI 一套连招打几下、格挡还要撑多久、上一次看到的对手招式（每招只判断一次要不要挡） */
	int32 ComboLen = 3;
	float GuardHold = 0.f;
	float LastEnemyMoveTime = -1.f;
	/** 双手刀卡弹反：还要等多久才按格挡（等到对方刀快落下） */
	float GuardDelay = 0.f;
	float LastAP = -1.f;
	float DamageWindow = 0.f;
	float AimNoiseSeed = 0.f;

	// ---------------------------------------------------------------- 人机强度（11-01）：当前这档的数值和低档才有的状态（发呆、砍空、缓一下……）
	FMechAITierValues Tier;
	int32 TierLevel = 1;
	/** 这台抽到的提前量（跟枪滞后在它上面减） */
	float BaseLead = 0.9f;
	float IdleClock = 0.f;
	float IdleLeft = 0.f;
	/** 看见目标多久了（看见 / 换目标后要反应一下才开火） */
	float SightClock = 0.f;
	TWeakObjectPtr<AMechCharacter> SightTarget;
	bool bBurstADS = true;
	/** 被弹开 / 被挡 / 被弹反后还要缓多久；上一次看到的反馈时刻 */
	float ReengageLeft = 0.f;
	float LastBlockFxAt = -1000.f;
	float LastClashFxAt = -1000.f;
	/** 上一帧在不在出刀、招式走到哪（招式时间倒回去 = 新的一刀） */
	bool bSwingWasActive = false;
	float SwingLastTime = 0.f;
	/** 一套连招打完还要歇多久 */
	bool bComboRestArmed = false;
	float ComboRestLeft = 0.f;
	/** 这一刀没对准：还要多久才恢复锁定 */
	float NoLockLeft = 0.f;
	TWeakObjectPtr<AMechCharacter> NoLockTarget;
	bool bDashRolled = false;
	bool bDashTake = true;
	float MissileWait = 0.f;
	float MissileHoldNow = 0.f;
	float TacticalGate = 0.f;
	bool bTacticalOn = true;

	// ---------------------------------------------------------------- 高处（10-31）：有高处图（UMechHighGround，城区 / 园区）的地图才用
	// 照常打（None）→ 看不见目标就按格子找路绕过去（Chase）；交战时挑一个看得见目标的高处喷射上去（GoUp），在上面横移着打（Perch），
	// 待够了 / 看不见了 / 对方钻到脚下就从边上走下来（GoDown）。10-31 搁置：默认关，命令行 -MechHighGround 才开（关着时 AI 和改动前一样）
	enum class EHigh : uint8 { None, Chase, GoUp, Perch, GoDown };
	EHigh HighState = EHigh::None;
	/** 这一帧的移动交给高处逻辑：改 InOutMove；返回 true = 正在走路径 / 站在高点上（原来的撞墙换向、捡插件舱、随机跳都不做） */
	bool HighTick(AMechCharacter* Self, AMechCharacter* Target, bool bSee, float Dist, float Range, bool bBlade, float DeltaSeconds, FVector& InOutMove,
		bool& bOutBoost);
	/** 沿 HighPath 走：0 = 走着，1 = 到了，2 = 走不通（卡住 / 超时） */
	int32 HighFollow(AMechCharacter* Self, float DeltaSeconds, FVector& OutMove, bool& bOutBoost);
	bool HighPathTo(AMechCharacter* Self, int32 Goal, EHigh NewState);
	void HighStop(AMechCharacter* Self, float Cooldown);
	/** 挑高点：返回候选节点（好的在前），最多 3 个 */
	void PickPerches(AMechCharacter* Self, AMechCharacter* Target, float Range, TArray<int32>& Out) const;
	class UMechHighGround* GetHigh();
	TWeakObjectPtr<class UMechHighGround> HighGround;
	TArray<FVector> HighPath;
	TArray<uint8> HighKinds;
	TArray<int32> HighNodes;
	int32 HighIndex = 0;
	int32 HighFrom = INDEX_NONE;
	int32 HighGoal = INDEX_NONE;
	/** 这一段走了 / 在高点待了多久，最多多久 */
	float HighClock = 0.f;
	float HighLimit = 0.f;
	float HighThink = 3.f;
	float HighCooldown = 0.f;
	/** 找路追人失败后多久内先照原来的走法 */
	float ChaseCooldown = 0.f;
	float HighRepath = 0.f;
	float HighUnseen = 0.f;
	float HighSeen = 0.f;
	float HighStuckClock = 0.f;
	float HighBest = 0.f;
	int32 HighStuckCount = 0;
	float HighHop = 0.f;
	bool bHighThrust = false;
	float PerchSide = 1.f;
	float PerchSideTimer = 0.f;
	FVector ChaseGoal = FVector::ZeroVector;
	/** 走不通过的边（起点 << 32 | 终点），找路时绕开 */
	TSet<uint64> HighBad;
	/** 性格：多爱占高点（0.25–0.9）、路线偏好（A* 噪声种子） */
	float HighLove = 0.5f;
	float RouteSeed = 1.f;

	// ---------------------------------------------------------------- 有会损毁的水的地图（11-02，MechAIWater.cpp）
	struct FWaterOrder
	{
		bool bActive = false;
		bool bCtrlBoost = false;
		bool bBoost = false;
		bool bCtrlThrust = false;
		bool bThrust = false;
		/** 这段时间别随机跳 */
		bool bNoJump = false;
		/** 发呆也不能停（快撞水了） */
		bool bUrgent = false;
	};
	/** 算完移动方向之后过这一道：改 InOutMove、决定推进。没有水的图返回 false，什么都不动 */
	bool WaterThink(AMechCharacter* Self, AMechCharacter* Target, bool bSee, float Dist, float Range, bool bBlade, float DeltaSeconds, FVector& InOutMove, FWaterOrder& Out);
	bool WaterSteerGround(class UMechWater* W, const FVector& Me, const FVector& Dir, float Look, FVector& Out) const;
	bool WaterAirFree(AMechCharacter* Self, class UMechWater* W, const FVector& Me, const FVector& Feet, const FVector& Dir, float Look, float Climb) const;
	bool WaterDodgeOk(AMechCharacter* Self, const FVector& Dir) const;
	bool WaterSwingOk(AMechCharacter* Self, AMechCharacter* Target) const;
	FVector WaterHome = FVector::ZeroVector;
	bool bWaterHasHome = false;
	bool bWaterHoming = false;
	bool bWaterThrust = false;
	float WaterHomeClock = 0.f;
	float WaterSteerClock = 0.f;
	FVector WaterSteerDir = FVector::ZeroVector;
	float WaterTakeoffCd = 0.f;
	float WaterThrustMin = 0.f;
	/** 飞回最近的落脚点还要多少能量（这一帧算的） */
	float WaterNeed = 0.f;
	/** 性格：巡航多高、打枪时比目标高多少（厘米） */
	float WaterCruise = 2500.f;
	float WaterZOffset = 300.f;
	/** 测试统计（日志 [wateraistat]，对局结束时打） */
	struct FWaterStat
	{
		int32 Rescues = 0;
		int32 Steers = 0;
		int32 Takeoffs = 0;
		int32 Homings = 0;
	};
	FWaterStat WaterStat;

	// ---------------------------------------------------------------- 统计（-MechAIStats，测试用）：每 0.5 秒采样，每 10 秒打一行累计 [aistat]
	void StatTick(AMechCharacter* Self, float DeltaSeconds, bool bIntent, bool bExcuse);
	struct FStat
	{
		float Alive = 0.f, High = 0.f, HighStand = 0.f;
		int32 Stuck = 0, Spin = 0, Fall = 0, GoUp = 0, Arrive = 0, Fail = 0, Chase = 0, ChaseFail = 0;
	};
	FStat Stat;
	float StatClock = 0.f;
	float StatPrint = 0.f;
	float StatPosPrint = 0.f;
	float StatLastSample = -100.f;
	TArray<FVector> StatPos;
	TArray<uint8> StatIntent;
	bool bStatStuck = false;
	bool bStatSpin = false;
	bool bStatFell = false;
	FVector StatStuckAt = FVector::ZeroVector;
	FVector StatSpinAt = FVector::ZeroVector;
};

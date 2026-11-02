#include "MechAIController.h"
#include "MechMapRules.h"
#include "MechLoop.h"
#include "MechDefense.h"
#include "MechGameState.h"
#include "MechRules.h"
#include "MechPluginPod.h"
#include "EngineUtils.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechCoreArena.h"
#include "MechHealthComponent.h"
#include "MechMissile.h"
#include "MechShoulderComponent.h"
#include "MechSmoke.h"
#include "MechTacticalComponent.h"
#include "MechHighGround.h"
#include "MechMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Misc/CommandLine.h"

namespace
{
	/** 高处逻辑（10-31，城区 AI 搁置中）：默认关，命令行 -MechHighGround 才开；关着时 AI 和改动前完全一样（见 docs/城区AI_搁置进度_20261031.md） */
	bool MechHighGroundOn()
	{
		static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("MechHighGround"));
		return bOn;
	}
}

// ---------------------------------------------------------------- 人机强度表（11-01）
// 用户：「人机的强度仍旧太高，瞄准过强，近战几乎不失误，四个档位也没有什么区别」「目前的四个档人机应该都是高水准，仍然可以保留这种强度到最高等级」。
// 想调哪档只改这张表（一行一个参数，四列对应四档）。王牌这一列 = 11-01 之前的王牌，所有「新加的毛病」都是关着的，不要削。
// 量化对比：-MechAITierTest（MechAITierTest.cpp），日志 [aitier]；加 -MechAITierLegacy 跑调整前的数值。
// 保卫战的精英 / 首领走 Legacy（调整前的四档 + 毛病全关），不受这张表影响。
// 11-02 定稿时量的（枪命中率 / 刀命中率，新兵 普通 老练 王牌）：调整前 47 65 89 91 / 75 80 70 75，现在 15 35 53 92 / 32 52 65 81。
namespace MechAITier
{
	struct FRange { float Min; float Max; };
	//                                 新兵            普通            老练            王牌（别动）
	// ---- 瞄准（枪）
	// 反应：闪避、格挡掷骰的几率（每台 AI 在范围里抽一个）
	const FRange React[4]        = {{0.10f, 0.25f}, {0.30f, 0.50f}, {0.55f, 0.75f}, {0.80f, 1.00f}};
	// 提前量准确度：1 = 按目标速度完整预判，0 = 瞄当前位置
	const FRange Lead[4]         = {{0.00f, 0.25f}, {0.20f, 0.55f}, {0.50f, 0.80f}, {0.75f, 0.95f}};
	// 手抖倍数（Combat->AIAimErrorScale）
	const float AimShake[4]      = {3.0f,           1.7f,           1.3f,           0.45f};
	// 枪散布倍数（Combat->SpreadScale）
	const float Spread[4]        = {8.5f,           6.0f,           4.2f,           2.2f};
	// 跟枪滞后（秒）：准星落后目标这么久（目标跑得越快偏得越多，新兵打高速目标大多打不中）
	const float AimLag[4]        = {0.22f,          0.095f,         0.06f,          0.f};
	// 目标每快 10 米/秒，手抖再乘 (1 + 这个)
	const float SpeedShake[4]    = {0.50f,          0.20f,          0.15f,          0.f};
	// 远距离开镜的几率（每轮点射掷一次；不开镜散布大）
	const float ADS[4]           = {0.2f,           0.25f,          0.5f,           1.f};
	// 点射之间的停顿倍数（越大开火越稀）
	const float BurstGap[4]      = {1.8f,           1.35f,          1.1f,           1.f};
	// 看见目标 / 换目标后多久才开火（秒）
	const float SightDelay[4]    = {0.8f,           0.45f,          0.2f,           0.f};
	// ---- 近战
	// 多久想一次要不要出刀（秒）
	const float SwingThink[4]    = {0.55f,          0.36f,          0.25f,          0.18f};
	// 够不着时冲动出刀的几率（每次想都掷）
	const float WhiffChance[4]   = {0.50f,          0.25f,          0.12f,          0.f};
	// 够不着多远还会冲动出刀（突进距离的倍数；0.9 = 不会）
	const float WhiffFar[4]      = {1.7f,           1.4f,           1.2f,           0.9f};
	// 一刀没对准的几率（这一刀不突进、不追踪，只往前送一小步）
	const float NoLockChance[4]  = {0.82f,          0.60f,          0.38f,          0.f};
	// 拿刀时想站多远（厘米）：低档站得远、够不着也出刀，砍空的多；王牌贴到 5 米
	const float MeleeKeep[4]     = {1100.f,         850.f,          650.f,          500.f};
	// 一套连招打几下（范围）
	const FIntPoint Combo[4]     = {{1, 2},         {2, 3},         {2, 4},         {2, 4}};
	// 一套连招打完歇多久才出下一套（秒）
	const FRange ComboRest[4]    = {{0.8f, 1.4f},   {0.5f, 0.9f},   {0.2f, 0.45f},  {0.f, 0.f}};
	// 格挡几率 = 反应 × 0.8 × 这个
	const float GuardScale[4]    = {0.35f,          0.6f,           0.85f,          1.f};
	// 看到对手出刀到按下格挡的反应时间（秒；王牌是同一帧）
	const FRange GuardLag[4]     = {{0.30f, 0.55f}, {0.18f, 0.35f}, {0.08f, 0.18f}, {0.f, 0.f}};
	// 双手刀卡点弹反几率 = 反应 × 0.55 × 这个
	const float ParryScale[4]    = {0.f,            0.2f,           0.55f,          1.f};
	// 卡点误差（秒，弹反窗口 0.2 秒；误差大了就按早 / 按晚）
	const float ParryJitter[4]   = {0.2f,           0.12f,          0.05f,          0.f};
	// 对手失衡时重斩补刀的几率
	const float HeavyPunish[4]   = {0.2f,           0.5f,           0.8f,           1.f};
	// 单手刀闪避斩抓住机会的几率
	const float DashSlash[4]     = {0.2f,           0.5f,           0.8f,           1.f};
	// 被弹开 / 被挡 / 被弹反后多久才再贴上去、再出刀（秒）
	const FRange Reengage[4]     = {{0.9f, 1.6f},   {0.5f, 1.0f},   {0.2f, 0.45f},  {0.f, 0.f}};
	// 换刀 / 换枪之间的间隔倍数
	const float StanceGap[4]     = {1.8f,           1.4f,           1.15f,          1.f};
	// ---- 机动
	// 闪避间隔倍数
	const float DodgeGap[4]      = {2.6f,           1.7f,           1.25f,          1.f};
	// 短时间挨多少伤害才想闪避
	const float DodgeHurt[4]     = {1100.f,         750.f,          550.f,          450.f};
	// 该冲刺的时候真冲的几率
	const float BoostWill[4]     = {0.55f,          0.75f,          0.9f,           1.f};
	// 跳起来喷射的间隔倍数
	const float JumpGap[4]       = {2.2f,           1.5f,           1.15f,          1.f};
	// ---- 肩部装备
	// 使用频率倍数
	const float ShoulderRate[4]  = {0.25f,          0.5f,           0.75f,          1.f};
	// 导弹锁上后多久才发（秒）
	const FRange MissileHold[4]  = {{1.5f, 4.f},    {0.6f, 2.f},    {0.1f, 0.7f},   {0.f, 0.f}};
	// ---- 发呆（不开火、不出刀、不闪避，移动只剩一点）
	// 隔多久愣一次（秒；0 = 不愣）
	const FRange IdleEvery[4]    = {{4.f, 8.f},     {7.f, 14.f},    {14.f, 26.f},   {0.f, 0.f}};
	// 一次愣多久（秒）
	const FRange IdleLen[4]      = {{0.6f, 1.3f},   {0.3f, 0.7f},   {0.12f, 0.3f},  {0.f, 0.f}};

	// 调整前的四档（保卫战精英、-MechAITierLegacy）：只有这四项分档，上面其余参数全按王牌那一列（= 关）
	const FRange LegacyReact[4]  = {{0.15f, 0.35f}, {0.4f, 0.8f},   {0.6f, 0.9f},   {0.8f, 1.f}};
	const FRange LegacyLead[4]   = {{0.05f, 0.25f}, {0.25f, 0.6f},  {0.5f, 0.8f},   {0.75f, 0.95f}};
	const float LegacyShake[4]   = {1.9f,           1.f,            0.7f,           0.45f};
	const float LegacySpread[4]  = {5.5f,           4.f,            3.f,            2.2f};

	static FVector2f V(const FRange& R)
	{
		return FVector2f(R.Min, R.Max);
	}

	static FMechAITierValues Make(int32 Level, bool bLegacy)
	{
		// 新加的毛病：Legacy 用王牌那一列（全关）
		const int32 N = bLegacy ? 3 : Level;
		FMechAITierValues T;
		T.React = V(bLegacy ? LegacyReact[Level] : React[N]);
		T.Lead = V(bLegacy ? LegacyLead[Level] : Lead[N]);
		T.AimShake = bLegacy ? LegacyShake[Level] : AimShake[N];
		T.Spread = bLegacy ? LegacySpread[Level] : Spread[N];
		T.AimLag = AimLag[N];
		T.SpeedShake = SpeedShake[N];
		T.ADS = ADS[N];
		T.BurstGap = BurstGap[N];
		T.SightDelay = SightDelay[N];
		T.SwingThink = SwingThink[N];
		T.WhiffChance = WhiffChance[N];
		T.WhiffFar = WhiffFar[N];
		T.NoLockChance = NoLockChance[N];
		T.MeleeKeep = MeleeKeep[N];
		T.Combo = Combo[N];
		T.ComboRest = V(ComboRest[N]);
		T.GuardScale = GuardScale[N];
		T.GuardLag = V(GuardLag[N]);
		T.ParryScale = ParryScale[N];
		T.ParryJitter = ParryJitter[N];
		T.HeavyPunish = HeavyPunish[N];
		T.DashSlash = DashSlash[N];
		T.Reengage = V(Reengage[N]);
		T.StanceGap = StanceGap[N];
		T.DodgeGap = DodgeGap[N];
		T.DodgeHurt = DodgeHurt[N];
		T.BoostWill = BoostWill[N];
		T.JumpGap = JumpGap[N];
		T.ShoulderRate = ShoulderRate[N];
		T.MissileHold = V(MissileHold[N]);
		T.IdleEvery = V(IdleEvery[N]);
		T.IdleLen = V(IdleLen[N]);
		return T;
	}
}

AMechAIController::AMechAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	bWantsPlayerState = false;
	bSetControlRotationFromPawnOrientation = false;  // 瞄准由自己每帧设
}

void AMechAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	// 每台 AI 性格不一样：有的喜欢远距离周旋，有的爱贴身砍
	PreferredRange = FMath::FRandRange(2600.f, 5200.f);
	Aggression = FMath::FRandRange(0.25f, 0.9f);
	StrafeSign = FMath::RandBool() ? 1.f : -1.f;
	AimNoiseSeed = FMath::FRandRange(0.f, 100.f);
	// 高处（10-31，默认关，-MechHighGround 才开）：多爱占高点、路线偏好（找路时每台 AI 的格子代价带自己的噪声，不会全走同一条街）；
	// 重生 / 换手时状态清掉。关着时不抽随机数，AI 的随机序列和改动前一模一样
	if (MechHighGroundOn())
	{
		HighLove = FMath::FRandRange(0.25f, 0.9f);
		RouteSeed = FMath::FRandRange(1.f, 60.f);
		HighThink = FMath::FRandRange(2.f, 5.f);
	}
	// 11-02 有水的地图（MechAIWater.cpp）：每台巡航高度不同，不要全挤在同一层
	{
		FRandomStream WaterRand(FCrc::StrCrc32(*InPawn->GetName()));
		WaterCruise = WaterRand.FRandRange(1800.f, 4200.f);
		WaterZOffset = WaterRand.FRandRange(-200.f, 1400.f);
		bWaterHoming = false;
		bWaterThrust = false;
		WaterStat = FWaterStat();
	}
	HighState = EHigh::None;
	HighPath.Reset();
	HighKinds.Reset();
	HighNodes.Reset();
	HighGoal = INDEX_NONE;
	HighCooldown = 0.f;
	bHighThrust = false;
	HighHop = 0.f;
	StatPos.Reset();
	StatIntent.Reset();
	// AI 强度（房间里房主定）：新兵 / 普通 / 老练 / 王牌，全部数值在文件开头的 MechAITier 表里。
	// 保卫战的精英 / 首领（11-01）：用调整前的数值，保持现状；房间里也不让选强度
	const AMechGameState* LevelGS = GetWorld()->GetGameState<AMechGameState>();
	const AMechCharacter* LevelMech = Cast<AMechCharacter>(InPawn);
	int32 Level = LevelMech && LevelMech->AILevelOverride >= 0 ? FMath::Clamp(LevelMech->AILevelOverride, 0, 3) : LevelGS ? FMath::Clamp(LevelGS->AILevel, 0, 3) : 1;  // 11-02 每台人机单独的强度
	FParse::Value(FCommandLine::Get(), TEXT("MechAILevel="), Level);  // 测试：全部人机定这一档（落水统计要分档看）
	static const bool bLegacyFlag = FParse::Param(FCommandLine::Get(), TEXT("MechAITierLegacy"));
	bool bLegacy = bLegacyFlag;
	for (TActorIterator<AMechDefenseDirector> It(GetWorld()); It && !bLegacy; ++It)
	{
		bLegacy = true;
	}
	ApplyTier(Level, bLegacy);
}

void AMechAIController::OnUnPossess()
{
	if (AMechCharacter* GlideMech = Cast<AMechCharacter>(GetPawn()))
	{
		GlideMech->GetMechMovement()->bGlideAI = false;  // 真人接管：飞行跟视线
	}
	if (const AMechCharacter* WaterMech = Cast<AMechCharacter>(GetPawn()); WaterMech && WaterStat.Takeoffs + WaterStat.Steers + WaterStat.Rescues > 0)
	{
		UE_LOG(LogTemp, Display, TEXT("[wateraistat] %s 起飞 %d 绕开 %d 回航 %d 紧急 %d（帧）"), *WaterMech->DisplayName, WaterStat.Takeoffs, WaterStat.Steers, WaterStat.Homings, WaterStat.Rescues);
	}
	// 11-01 人机强度：真人接管这台时把 AI 的枪法毛病还回去（默认值见 MechCombatComponent.h），不然接管的人带着 AI 的散布和负提前量
	if (AMechCharacter* Mech = Cast<AMechCharacter>(GetPawn()))
	{
		UMechCombatComponent* Combat = Mech->GetCombat();
		Combat->SpreadScale = 1.f;
		Combat->LeadAccuracy = 0.9f;
		Combat->AIAimErrorScale = 1.f;
		Combat->ClearForcedTarget();
		NoLockLeft = 0.f;
	}
	Super::OnUnPossess();
}

void AMechAIController::ApplyTier(int32 Level, bool bLegacy)
{
	Level = FMath::Clamp(Level, 0, 3);
	TierLevel = Level;
	Tier = MechAITier::Make(Level, bLegacy);
	ReactionChance = FMath::FRandRange(Tier.React.X, Tier.React.Y);
	ComboLen = FMath::RandRange(Tier.Combo.X, Tier.Combo.Y);
	BaseLead = FMath::FRandRange(Tier.Lead.X, Tier.Lead.Y);
	IdleClock = Tier.IdleEvery.Y > 0.f ? FMath::FRandRange(Tier.IdleEvery.X, Tier.IdleEvery.Y) : 0.f;
	IdleLeft = 0.f;
	MissileWait = 0.f;
	MissileHoldNow = FMath::FRandRange(Tier.MissileHold.X, Tier.MissileHold.Y);
	bBurstADS = true;
	bTacticalOn = true;
	if (AMechCharacter* Mech = Cast<AMechCharacter>(GetPawn()))
	{
		Mech->GetCombat()->SpreadScale = Tier.Spread;  // AI 打得散一点
		Mech->GetCombat()->LeadAccuracy = BaseLead;
		Mech->GetCombat()->AIAimErrorScale = Tier.AimShake;
		UE_LOG(LogTemp, Display, TEXT("[ai] %s 强度 %d%s 喜欢距离 %.0f 米 进攻性 %.2f 反应 %.2f 提前量 %.2f"), *Mech->DisplayName, Level, bLegacy ? TEXT("（调整前的数值）") : TEXT(""),
			PreferredRange / 100.f, Aggression, ReactionChance, BaseLead);
	}
}

AMechCharacter* AMechAIController::PickTarget(AMechCharacter* Self) const
{
	AMechCharacter* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		AMechCharacter* Other = *It;
		if (Other == Self || Other->GetTeam() == Self->GetTeam() || Other->GetHealth()->IsDead())
		{
			continue;
		}
		// 近的优先，看得见的再优先一点，残血的也优先一点
		float Score = FVector::Dist(MechLoop::Near(GetWorld(), Other->GetActorLocation(), Self->GetActorLocation()), Self->GetActorLocation());
		Score *= HasLineOfSight(Self, Other) ? 0.7f : 1.f;
		Score *= 0.6f + 0.4f * Other->GetHealth()->GetAPFraction();
		// 死斗刚重生、还在保护中的（打了不掉血）往后排
		if (Other->GetEffects() && Other->GetEffects()->HasEffect(MechRules::SpawnGuardId()))
		{
			Score *= 3.f;
		}
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Other;
		}
	}
	return Best;
}

bool AMechAIController::HasLineOfSight(AMechCharacter* Self, AMechCharacter* Other) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechAILOS), false, Self);
	Params.AddIgnoredActor(Other);
	const FVector From = Self->GetActorLocation() + FVector(0.f, 0.f, 80.f);
	const FVector To = MechLoop::Near(GetWorld(), Other->GetActorLocation(), From) + FVector(0.f, 0.f, 40.f);
	return !GetWorld()->LineTraceSingleByObjectType(Hit, From, To, FCollisionObjectQueryParams(ECC_WorldStatic), Params)
		&& !AMechSmoke::BlocksLine(GetWorld(), From, To);  // 隔着烟幕看不见
}

bool AMechAIController::Blocked(AMechCharacter* Self, const FVector& Dir) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechAIBlock), false, Self);
	const FVector From = Self->GetActorLocation();
	return GetWorld()->SweepSingleByObjectType(Hit, From, From + Dir * 450.f, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
		FCollisionShape::MakeSphere(60.f), Params);
}

bool AMechAIController::bBrainOffForTest = false;

void AMechAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bBrainOffForTest)
	{
		return;
	}
	AMechCharacter* Self = Cast<AMechCharacter>(GetPawn());
	if (!Self || Self->GetHealth()->IsDead())
	{
		return;
	}
	UMechCombatComponent* Combat = Self->GetCombat();
	if (!Self->CanAct())
	{
		Combat->SetTrigger(false);
		Combat->SetAltHeld(false);
		Self->SetBoostHeld(false);
		return;
	}

	// 被网住：连按快速闪避挣扎（反应快的 AI 按得勤）
	if (Self->IsNetted())
	{
		StruggleTimer -= DeltaSeconds;
		if (StruggleTimer <= 0.f)
		{
			StruggleTimer = FMath::FRandRange(0.16f, 0.4f) / FMath::Max(0.4f, ReactionChance + 0.3f);
			Self->QuickBoostToward(FMath::VRand().GetSafeNormal2D());
		}
	}

	// 发呆（11-01 人机强度）：低档隔一阵愣一下，不开火、不出刀、不闪避，移动也只剩一点；王牌不发呆
	if (IdleLeft > 0.f)
	{
		IdleLeft -= DeltaSeconds;
	}
	else if (Tier.IdleEvery.Y > 0.f)
	{
		IdleClock -= DeltaSeconds;
		if (IdleClock <= 0.f)
		{
			IdleClock = FMath::FRandRange(Tier.IdleEvery.X, Tier.IdleEvery.Y);
			IdleLeft = FMath::FRandRange(Tier.IdleLen.X, Tier.IdleLen.Y);
			++TierStats.Idles;
		}
	}
	const bool bIdle = IdleLeft > 0.f;

	// ---------------------------------------------------------------- 目标
	RetargetTimer -= DeltaSeconds;
	AMechCharacter* Target = Combat->GetTarget();
	// 这一刀没对准（低档）：砍下去之前战斗组件不锁目标（不突进、不追踪，只往前送一小步），脑子里还盯着它
	if (NoLockLeft > 0.f)
	{
		NoLockLeft -= DeltaSeconds;
		Target = NoLockTarget.Get();
		if (NoLockLeft <= 0.f && Target)
		{
			Combat->SetForcedTarget(Target);
		}
	}
	if ((RetargetTimer <= 0.f && NoLockLeft <= 0.f) || !Target || Target->GetHealth()->IsDead())
	{
		RetargetTimer = FMath::FRandRange(1.5f, 3.f);
		Target = PickTarget(Self);
		Combat->SetForcedTarget(Target);
		NoLockLeft = 0.f;
	}
	// 夺芯任务：沿路点走（照样瞄准、开枪），到了附近再交给原来的打法
	FVector CoreDir = FVector::ZeroVector;
	bool bCoreBoost = false;
	const bool bCoreMoving = CoreGoal != ECoreGoal::None && CoreMove(Self, DeltaSeconds, CoreDir, bCoreBoost);
	if (!Target)
	{
		Combat->SetTrigger(false);
		// 11-02 有水的地图：没有目标时（对手都在等重生）也不能在水面上方松手
		{
			FVector WaterMove = FVector::ZeroVector;
			FWaterOrder WaterIdle;
			if (WaterThink(Self, nullptr, false, 0.f, 0.f, false, DeltaSeconds, WaterMove, WaterIdle))
			{
				Self->SetBoostHeld(WaterIdle.bCtrlBoost && WaterIdle.bBoost);
				Self->SetJumpThrust(WaterIdle.bCtrlThrust && WaterIdle.bThrust);
				if (!WaterMove.IsNearlyZero())
				{
					SetControlRotation(FRotator(0.f, WaterMove.Rotation().Yaw, 0.f));
					Self->AddMovementInput(WaterMove, 1.f);
				}
				return;
			}
		}
		Self->SetBoostHeld(bCoreMoving && bCoreBoost && Self->GetEnergy() > 30.f);
		if (bCoreMoving && !CoreDir.IsNearlyZero())
		{
			SetControlRotation(FRotator(0.f, CoreDir.Rotation().Yaw, 0.f));
			Self->AddMovementInput(CoreDir, 1.f);
		}
		return;
	}

	const FVector Me = Self->GetActorLocation();
	const FVector ToTarget = MechLoop::Delta(GetWorld(), Me, Target->GetActorLocation());
	// 11-02 空战图（石柱山水）按直线距离算（对手在头顶 60 米不算贴脸）；别的图和原来一样按水平距离
	const bool bAirMap = MechMapRules::FlightFor(GetWorld()).bAirMap;
	const float Dist = bAirMap ? ToTarget.Size() : ToTarget.Size2D();
	const FVector Toward = ToTarget.GetSafeNormal2D();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward) * StrafeSign;
	const bool bSee = HasLineOfSight(Self, Target);

	// 瞄准：看向目标，带一点随时间变化的手抖
	const float T = GetWorld()->GetTimeSeconds() + AimNoiseSeed;
	FRotator Look = (MechLoop::Near(GetWorld(), Target->GetActorLocation(), Me) + FVector(0.f, 0.f, 30.f) - (Me + FVector(0.f, 0.f, 100.f))).Rotation();
	Look.Yaw += FMath::PerlinNoise1D(T * 0.7f) * 2.f;
	Look.Pitch += FMath::PerlinNoise1D(T * 0.9f + 50.f) * 1.5f;
	if (bCoreMoving && !bSee && !CoreDir.IsNearlyZero())
	{
		Look = FRotator(0.f, CoreDir.Rotation().Yaw, 0.f);  // 看不见敌人就看路
	}
	SetControlRotation(Look);

	// ---------------------------------------------------------------- 移动：绕目标横移，保持喜欢的距离
	StrafeTimer -= DeltaSeconds;
	if (StrafeTimer <= 0.f)
	{
		StrafeTimer = FMath::FRandRange(1.4f, 3.6f);
		StrafeSign = FMath::RandBool() ? 1.f : -1.f;
	}
	// ---------------------------------------------------------------- 姿态：近了（越好斗越早）或对手失衡就拔刀，拉开了就换枪
	const bool bBlade = Combat->GetStance() == EMechStance::Blade;
	const float MeleeEnter = 1500.f + Aggression * 900.f;
	StanceTimer -= DeltaSeconds;
	if (StanceTimer <= 0.f && Combat->GetSwitchProgress() < 0.f && !Combat->IsMeleeActive())
	{
		// 带芯拿不了刀；跑任务时（救人、捡芯、护送）别拔刀追人，贴脸了才砍
		const bool bTaskWalk = bCoreMoving && CoreGoal != ECoreGoal::Intercept && CoreGoal != ECoreGoal::Guard;
		bool bWantSword = bSee && !Self->IsCarryingCore() && (!bTaskWalk || Dist < 900.f) &&
			(Dist < MeleeEnter || (Target->GetHealth()->IsStaggered() && Dist < 3000.f));
		bool bWantGun = Dist > 3400.f || !bSee;
		if (TestStance >= 0)
		{
			// -MechAITierTest：枪、刀分开量
			bWantSword = TestStance == 1;
			bWantGun = TestStance == 0;
		}
		if ((!bBlade && bWantSword) || (bBlade && bWantGun))
		{
			Combat->SetTrigger(false);
			Combat->SetAltHeld(false);
			Combat->SwitchStance();
			StanceTimer = FMath::FRandRange(2.f, 3.5f) * Tier.StanceGap;
		}
	}
	const bool bWantBlade = bBlade;
	// 近距离枪（10-30：冲锋枪 20 米外掉伤害、霰弹枪靠近距离）：喜欢的距离压到这把枪的有效距离里，不然 AI 在 40 米外拿冲锋枪刮痧
	const EMechShooter HeldGun = Combat->GetGunType();
	const float KeepRange = HeldGun == EMechShooter::SMG ? 1800.f : HeldGun == EMechShooter::Shotgun ? 1000.f : PreferredRange;
	// 被弹开 / 被挡 / 被弹反（11-01 人机强度）：低档要缓一下才重新贴上去、重新出刀（这段时间站开一点、不冲刺）；王牌 Reengage = 0，马上接
	if (ReengageLeft > 0.f)
	{
		ReengageLeft -= DeltaSeconds;
	}
	if (Tier.Reengage.Y > 0.f)
	{
		const float WorldNow = GetWorld()->GetTimeSeconds();
		const float BlockAt = WorldNow - Combat->GetBlockFxAge();
		const float ClashAt = WorldNow - Combat->GetClashFxAge();
		const bool bNewBlock = BlockAt > LastBlockFxAt + 0.01f && !Combat->IsBlockFxDefender() && Combat->GetBlockFxAge() < 3.f;
		const bool bNewClash = ClashAt > LastClashFxAt + 0.01f && Combat->GetClashFxAge() < 3.f;
		LastBlockFxAt = BlockAt;
		LastClashFxAt = ClashAt;
		if (bNewBlock || bNewClash)
		{
			ReengageLeft = FMath::FRandRange(Tier.Reengage.X, Tier.Reengage.Y);
		}
	}
	const bool bReengaging = ReengageLeft > 0.f;
	const float Range = bBlade ? (bReengaging ? FMath::Max(900.f, Tier.MeleeKeep) : Tier.MeleeKeep) : FMath::Min(PreferredRange, KeepRange);
	float Radial = FMath::Clamp((Dist - Range) / 1500.f, -1.f, 1.f);
	FVector Move = Side + Toward * Radial * 1.2f;
	if (!bSee)
	{
		Move = Toward + Side * 0.4f;  // 看不见就往目标那边绕
	}
	FVector FromCenter = Me - ArenaCenter;
	if (MechLoop::IsActive(GetWorld()))
	{
		FromCenter.X = 0.f;  // 老街循环：沿街（X）没有边，只看两侧
	}
	if (FMath::Abs(FromCenter.X) > ArenaExtent.X || FMath::Abs(FromCenter.Y) > ArenaExtent.Y)
	{
		Move += -FromCenter.GetSafeNormal2D() * 1.5f;  // 出场地了，往回走
	}
	// 高处图（城区 / 园区，10-31）：看不见目标时找路、抢高点、在高点上横移、从高处下来都在 HighTick 里，这时下面的撞墙换向、
	// 捡插件舱、随机跳都不做（路径自己管喷射和跳）；没有高处图的地图（街道 / 自然 / 小巷 / 测试场 / 夺芯）HighTick 直接返回 false，照旧
	bool bHighBoost = false;
	const bool bHighMoving = !bCoreMoving && CoreGoal == ECoreGoal::None && HighTick(Self, Target, bSee, Dist, Range, bBlade, DeltaSeconds, Move, bHighBoost);
	if (!bHighMoving && !GetHigh() && !bAirMap && Me.Z > ArenaCenter.Z + ArenaMaxHeight && JumpHold <= 0.f)
	{
		Move += -FromCenter.GetSafeNormal2D();  // 跑到屋顶上了，往场地中间走（会掉下去）；有高处图的地图不用这条
	}
	Move = Move.GetSafeNormal2D();
	if (bHighMoving && !bSee && !Move.IsNearlyZero())
	{
		SetControlRotation(FRotator(0.f, Move.Rotation().Yaw, 0.f));  // 看不见敌人就看路
	}
	if (!bHighMoving && Blocked(Self, Move))
	{
		StrafeSign = -StrafeSign;
		Move = (FVector::CrossProduct(FVector::UpVector, Toward) * StrafeSign + Toward * 0.3f).GetSafeNormal2D();
		if (Blocked(Self, Move))
		{
			JumpTimer = 0.f;
		}
	}
	// 插件舱：60 米内有、没在近身缠斗、又是本队离它最近的那台（不让整队一起去），就绕过去拿（照样瞄准开枪）
	if (!bCoreMoving && !bHighMoving && (Dist > 1200.f || !bSee))
	{
		const AMechPluginPod* Pod = nullptr;
		float Best = 6000.f;
		for (TActorIterator<AMechPluginPod> It(GetWorld()); It; ++It)
		{
			const float D = FVector::Dist2D(It->GetActorLocation(), Me);
			if (D < Best)
			{
				Best = D;
				Pod = *It;
			}
		}
		bool bClosest = Pod != nullptr;
		for (TActorIterator<AMechCharacter> It(GetWorld()); It && bClosest; ++It)
		{
			bClosest = *It == Self || It->GetTeam() != Self->GetTeam() || It->GetHealth()->IsDead() ||
				FVector::Dist2D(It->GetActorLocation(), Pod->GetActorLocation()) >= Best;
		}
		if (bClosest)
		{
			Move = ((Pod->GetActorLocation() - Me).GetSafeNormal2D() + Side * 0.25f).GetSafeNormal2D();
		}
	}
	if (bCoreMoving)
	{
		Move = CoreDir;
	}
	// 11-02 有水的地图：别往水里走，空战图还要管飞多高、什么时候回去落地（MechAIWater.cpp）；别的图 WaterThink 直接返回
	FWaterOrder WaterOrder;
	WaterThink(Self, Target, bSee, Dist, Range, bBlade, DeltaSeconds, Move, WaterOrder);
	Self->AddMovementInput(Move, bIdle && !WaterOrder.bUrgent ? 0.35f : 1.f);
	StatTick(Self, DeltaSeconds, !Move.IsNearlyZero(),
		Self->IsNetted() || Self->IsEmped() || Self->GetHealth()->IsStaggered() || Combat->IsMeleeActive());

	// 冲刺：离得远就冲，近战前冲，其余时候偶尔冲一段（走高处路径时由路径定：喷射前不冲刺、站在高点上不冲刺）
	BoostTimer -= DeltaSeconds;
	if (WaterOrder.bCtrlBoost)
	{
		BoostTimer = 0.f;
		Self->SetBoostHeld(WaterOrder.bBoost);
	}
	else if (bHighMoving)
	{
		BoostTimer = 0.f;
		Self->SetBoostHeld(bHighBoost && Self->GetEnergy() > 30.f);
	}
	else if (BoostTimer <= 0.f)
	{
		BoostTimer = FMath::FRandRange(0.8f, 2.2f);
		const bool bBoost = bCoreMoving ? bCoreBoost
			: (FMath::FRand() < Tier.BoostWill && !bReengaging && (Dist > Range * 1.6f || bWantBlade || FMath::FRand() < 0.35f + 0.3f * Aggression));
		Self->SetBoostHeld(bBoost && Self->GetEnergy() > 30.f);
	}

	// 跳起来喷一会儿，换个高度打
	JumpTimer -= DeltaSeconds;
	if (WaterOrder.bCtrlThrust)
	{
		JumpHold = 0.f;
		Self->SetJumpThrust(WaterOrder.bThrust);
	}
	else if (bHighMoving)
	{
		JumpHold = 0.f;  // 喷射 / 跳由高处路径管（HighFollow），站在高点上也不乱跳
	}
	else if (JumpHold > 0.f)
	{
		JumpHold -= DeltaSeconds;
		if (JumpHold <= 0.f || Self->GetEnergy() < 20.f)
		{
			Self->SetJumpThrust(false);
			JumpHold = 0.f;
		}
	}
	else if (!bCoreMoving && JumpTimer <= 0.f && Self->GetEnergy() > 45.f && !WaterOrder.bNoJump)
	{
		JumpTimer = FMath::FRandRange(5.f, 11.f) * Tier.JumpGap;
		JumpHold = FMath::FRandRange(0.6f, 1.4f);
		Self->SetJumpThrust(true);
	}

	// ---------------------------------------------------------------- 闪避：导弹来了 / 刚挨了一波 / 偶尔随机
	const float AP = Self->GetHealth()->GetAP();
	if (LastAP >= 0.f && AP < LastAP)
	{
		DamageWindow += LastAP - AP;
	}
	LastAP = AP;
	DamageWindow = FMath::Max(0.f, DamageWindow - DeltaSeconds * 300.f);
	DodgeTimer -= DeltaSeconds;
	bool bDodge = false;
	for (TActorIterator<AMechMissile> It(GetWorld()); It; ++It)
	{
		if (It->GetTarget() == Self && MechLoop::Dist(GetWorld(), It->GetActorLocation(), Me) < 1800.f)
		{
			bDodge = true;
			break;
		}
	}
	bDodge = !bIdle && (bDodge || DamageWindow > Tier.DodgeHurt || DodgeTimer <= 0.f);
	if (bDodge && DodgeTimer <= 0.6f && !MechHighGroundOn())
	{
		// 原来的闪避（高处逻辑关着时一字不差）；11-01 间隔和触发伤害按强度
		if (FMath::FRand() < ReactionChance || DodgeTimer <= 0.f)
		{
			const FVector DodgeSide = FVector::CrossProduct(FVector::UpVector, Toward) * (FMath::RandBool() ? 1.f : -1.f);
			// 11-02 有水的地图：闪避不朝水里闪，那边不行就闪另一边，两边都不行就不闪（别的图 WaterDodgeOk 恒真，随机数序列不变）
			const FVector DodgeDir = WaterDodgeOk(Self, DodgeSide) ? DodgeSide : WaterDodgeOk(Self, -DodgeSide) ? -DodgeSide : FVector::ZeroVector;
			if (!DodgeDir.IsNearlyZero())
			{
				Self->QuickBoostToward(DodgeDir);
			}
			DamageWindow = 0.f;
			++TierStats.Dodges;
		}
		DodgeTimer = FMath::FRandRange(2.5f, 6.f) * Tier.DodgeGap;
	}
	else if (bDodge && DodgeTimer <= 0.6f)
	{
		FVector DodgeDir = FVector::CrossProduct(FVector::UpVector, Toward) * (FMath::RandBool() ? 1.f : -1.f);
		bool bDodgeOk = !bHighThrust;  // 贴墙往上喷的时候不闪（一闪就从墙边甩开、白喷）
		if (HighState == EHigh::Perch)
		{
			// 站在高点上：往还在同一片高处的那边闪；两边都是边沿就照闪（闪下去也行，落地照常打）
			UMechHighGround* HG = GetHigh();
			const int32 N = HG ? HG->FindNode(Me - FVector(0.f, 0.f, Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())) : INDEX_NONE;
			if (N != INDEX_NONE && !HG->StepStaysInZone(N, DodgeDir) && HG->StepStaysInZone(N, -DodgeDir))
			{
				DodgeDir = -DodgeDir;
			}
		}
		if (bDodgeOk && (FMath::FRand() < ReactionChance || DodgeTimer <= 0.f))
		{
			Self->QuickBoostToward(DodgeDir);
			DamageWindow = 0.f;
			++TierStats.Dodges;
		}
		DodgeTimer = FMath::FRandRange(2.5f, 6.f) * Tier.DodgeGap;
	}

	// ---------------------------------------------------------------- 武器
	BurstTimer -= DeltaSeconds;
	if (BurstTimer <= 0.f)
	{
		bBurst = !bBurst;
		BurstTimer = bBurst ? FMath::FRandRange(1.0f, 2.2f) : FMath::FRandRange(0.35f, 0.9f) * Tier.BurstGap;
		bBurstADS = FMath::FRand() < Tier.ADS;
	}
	// 看见目标 / 换了目标之后要反应一下才开火（11-01 人机强度；王牌 0 秒）
	if (SightTarget.Get() != Target)
	{
		SightTarget = Target;
		SightClock = 0.f;
	}
	SightClock = bSee ? SightClock + DeltaSeconds : 0.f;
	const bool bSightReady = SightClock >= Tier.SightDelay;
	const EMechShoulder ShoulderType = Self->GetLoadout().Shoulder;
	UMechShoulderComponent* Shoulder = Self->GetShoulder();
	if (MechIsTacticalShoulder(ShoulderType))
	{
		// 11-01 战术装备并入肩部，这四件不走下面的肩部武器分支：用不用、往哪用由组件自己看局面（被围、挨打、有人冲刀过来、目标在中距离），
		// 这里只把目标和最近挨的伤害给它
		// 11-01 人机强度：低档只有一部分时间在想这件装备（每秒掷一次）；王牌一直在想
		TacticalGate -= DeltaSeconds;
		if (TacticalGate <= 0.f)
		{
			TacticalGate = 1.f;
			bTacticalOn = FMath::FRand() < Tier.ShoulderRate;
		}
		UMechTacticalComponent* Tactical = Self->GetTactical();
		if (Tactical && bTacticalOn && !bIdle)
		{
			Tactical->AIThink(Target, bSee, DamageWindow, DeltaSeconds);
		}
	}
	else if (ShoulderType == EMechShoulder::Missiles)
	{
		if (Combat->GetMissileLock() >= 1.f && Combat->GetMissileReady() >= 1.f && bSee && !bIdle)
		{
			// 11-01 人机强度：低档锁上了也要愣一会儿才发；王牌马上发
			MissileWait += DeltaSeconds;
			if (MissileWait >= MissileHoldNow)
			{
				MissileWait = 0.f;
				MissileHoldNow = FMath::FRandRange(Tier.MissileHold.X, Tier.MissileHold.Y);
				Combat->FireMissiles();
				++TierStats.Shoulders;
			}
		}
	}
	else if (Shoulder && Shoulder->IsArtilleryHeld())
	{
		// 火炮：架好了就松开开炮；架的时候丢了视线就收炮（没架好松开 = 不开）
		if (Shoulder->GetArtilleryDeploy() >= 1.f || !bSee)
		{
			Shoulder->Released();
		}
	}
	else if (!bIdle && Shoulder && Shoulder->GetReady() >= 1.f && !Target->GetHealth()->IsDead() && FMath::FRand() < DeltaSeconds * (0.6f + Aggression) * Tier.ShoulderRate)
	{
		bool bUse = false;
		switch (ShoulderType)
		{
		case EMechShoulder::Grapple:  // 中远距离把对方拉过来（被网住的更好拉）
			bUse = bSee && Dist > 1500.f && Dist < UMechShoulderComponent::GrappleRange * 0.85f;
			break;
		case EMechShoulder::Net:  // 30 米内、对方没被网住时
			bUse = bSee && Dist > 500.f && Dist < 3000.f && !Target->IsNetted();
			break;
		case EMechShoulder::Artillery:  // 12 米到 120 米
			bUse = bSee && Dist > 1200.f && Dist < 12000.f;
			break;
		case EMechShoulder::Emp:  // 35 米内、对方还没中
			bUse = bSee && Dist < 3500.f && !Target->IsEmped();
			break;
		case EMechShoulder::Smoke:  // 挨打挨得狠或装甲见底：在自己和对方之间放烟
			bUse = bSee && (DamageWindow > 350.f || Self->GetHealth()->GetAPFraction() < 0.4f);
			break;
		case EMechShoulder::Shield:  // 导弹来了 / 正在挨打
			bUse = bSee && (bDodge || DamageWindow > 250.f);
			break;
		default:
			break;
		}
		if (bUse)
		{
			Shoulder->Pressed();
			++TierStats.Shoulders;
		}
	}
	if (!bBlade)
	{
		// 射击：点射（在这把枪的射程里），离得远就右键瞄准（散布小）
		const MechWeapons::FGun& G = Combat->GetGun();
		const float GunRange = G.ChargeTime > 0.f ? 22000.f : FMath::Min(22000.f, G.BulletSpeed * G.BulletLife * 0.9f);
		// 充能步枪：开始充了就一直按着直到打出去（点射节奏中途松开 = 取消，白充）
		const bool bKeepCharging = G.ChargeTime > 0.f && Combat->GetCharge() > 0.f && bSee;
		// 跟枪（11-01 人机强度）：准星落后目标 AimLag 秒（用负的提前量，不用给战斗组件加字段）、目标越快手越抖。
		// 没扣扳机时恢复原来的提前量（肩部装备的预判也读它，飞得慢的东西不该按子弹的飞行时间算滞后）
		if (Tier.AimLag > 0.f || Tier.SpeedShake > 0.f)
		{
			const bool bTrackAim = bBurst || bKeepCharging;
			const float Flight = FMath::Max(ToTarget.Size() / FMath::Max(G.BulletSpeed, 1.f), 0.02f);
			Combat->LeadAccuracy = bTrackAim ? BaseLead - Tier.AimLag / Flight : BaseLead;
			Combat->AIAimErrorScale = Tier.AimShake * (1.f + Tier.SpeedShake * Target->GetVelocity().Size() / 1000.f);
		}
		// 11-02 空战图：能量枪花的是飞行的能量，见底了先别开（不然自己把自己打下去）
		const bool bEnergyLow = bAirMap && G.EnergyPerShot > 0.f && Self->GetEnergy() < G.EnergyPerShot + 35.f;
		Combat->SetTrigger((bBurst || bKeepCharging) && bSee && Dist < GunRange && !bIdle && bSightReady && !bEnergyLow);
		Combat->SetAltHeld(bBurst && bSee && ((Dist > 3000.f && bBurstADS) || G.EnergyPerShot > 0.f));
		return;
	}

	// ---------------------------------------------------------------- 近战：对手出招冲过来就看反应挡一下，否则贴上去连招，对手失衡就重斩
	Combat->SetTrigger(false);
	UMechCombatComponent* Enemy = Target->GetCombat();
	// 投掷匕首不能格挡（10-30）：贴身时右键挥刀，远了照样投
	const bool bDagger = Combat->GetBlade().Knives > 0;
	if (Enemy->IsMeleeActive() && Dist < 1100.f && !bDagger && !bIdle)
	{
		const float MoveStart = GetWorld()->GetTimeSeconds() - Enemy->GetMeleeTime();
		if (FMath::Abs(MoveStart - LastEnemyMoveTime) > 0.05f)
		{
			LastEnemyMoveTime = MoveStart;
			if (FMath::FRand() < ReactionChance * 0.8f * Tier.GuardScale && Self->GetEnergy() > 25.f)
			{
				GuardHold = Enemy->GetMeleeDuration() - Enemy->GetMeleeTime() + 0.1f;
				// 弹反已去掉（11-02 化境）：按强度的反应时间格挡
				const bool bParry = false;
				GuardDelay = FMath::FRandRange(Tier.GuardLag.X, Tier.GuardLag.Y);
				++TierStats.Guards;
				TierStats.ParryTries += bParry ? 1 : 0;
			}
		}
	}
	if (GuardDelay > 0.f)
	{
		GuardDelay -= DeltaSeconds;  // 卡弹反的点：先别按
	}
	else
	{
		GuardHold -= DeltaSeconds;
	}
	Combat->SetAltHeld(GuardHold > 0.f && GuardDelay <= 0.f);
	SlashThinkTimer -= DeltaSeconds;
	// 单手刀：刚闪避完、对方在闪避斩够得着的距离里 → 立刻出轻击（远距离突进斩）；11-01 低档不一定抓得住（每次闪避掷一次）
	const bool bDashWindow = Combat->CanDashSlash();
	if (!bDashWindow)
	{
		bDashRolled = false;
	}
	else if (!bDashRolled)
	{
		bDashRolled = true;
		bDashTake = FMath::FRand() < Tier.DashSlash;
	}
	// 这一刀没对准（11-01 人机强度）：每一刀开始时掷（连招里接上的也算），没对准就不突进、不追踪，只往前送一小步，对方一动就砍空；王牌不掷
	const bool bSwingActive = Combat->IsMeleeActive();
	const float SwingTime = Combat->GetMeleeTime();
	if (bSwingActive && (!bSwingWasActive || SwingTime < SwingLastTime - 1.e-4f) && Tier.NoLockChance > 0.f && !bDagger && FMath::FRand() < Tier.NoLockChance)
	{
		NoLockTarget = Target;
		NoLockLeft = FMath::Max(Combat->GetMeleeStrikeAt() - SwingTime, 0.f) + 0.05f;
		Combat->SetForcedTarget(nullptr);
		++TierStats.NoLock;
	}
	bSwingWasActive = bSwingActive;
	SwingLastTime = SwingTime;
	// 一套连招打完歇一下（11-01 人机强度；王牌 ComboRest = 0，照旧马上接）：歇的时候不出刀，连招窗口过了就算断了
	if (Tier.ComboRest.Y > 0.f)
	{
		if (Combat->GetComboStep() >= ComboLen)
		{
			if (!bComboRestArmed)
			{
				bComboRestArmed = true;
				ComboRestLeft = FMath::FRandRange(Tier.ComboRest.X, Tier.ComboRest.Y);
			}
		}
		else if (!bSwingActive)
		{
			ComboRestLeft -= DeltaSeconds;
			if (ComboRestLeft <= 0.f)
			{
				bComboRestArmed = false;
			}
		}
	}
	const bool bCanSwing = GuardHold <= 0.f && !bIdle && !bReengaging && !bComboRestArmed && WaterSwingOk(Self, Target);
	if (bDashWindow && bDashTake && bSee && Dist < MechWeapons::DashSlashRange * 0.9f && !Combat->IsMeleeActive() && bCanSwing)
	{
		Combat->RequestLight();
		SlashThinkTimer = Tier.SwingThink;
	}
	if (SlashThinkTimer <= 0.f && bCanSwing)
	{
		SlashThinkTimer = Tier.SwingThink;
		const float BladeRange = Combat->GetBladeRange();
		if (bSee && Dist < BladeRange * 0.9f)
		{
			if (Target->GetHealth()->IsStaggered() && Combat->GetHeavyReady() >= 1.f && !Combat->IsMeleeActive() && FMath::FRand() < Tier.HeavyPunish)
			{
				Combat->RequestHeavy();
			}
			else if (bDagger && Dist < 1100.f)
			{
				Combat->RequestDaggerSlash();
			}
			else if (Combat->GetComboStep() < ComboLen)
			{
				Combat->RequestLight();
			}
		}
		else if (bSee && !bDagger && !Combat->IsMeleeActive() && Dist < BladeRange * Tier.WhiffFar && FMath::FRand() < Tier.WhiffChance)
		{
			// 够不着就出刀（11-01 人机强度）：低档判断不准距离，砍空了站在原地挨打；王牌 WhiffFar = 0.9，走不到这里
			Combat->RequestLight();
			++TierStats.Whiffs;
		}
	}
}

// ---------------------------------------------------------------- 夺芯：沿路点图走
bool AMechAIController::CoreMove(AMechCharacter* Self, float DeltaSeconds, FVector& OutMove, bool& bOutBoost)
{
	AMechCoreArena* Arena = CoreArena.Get();
	if (!Arena)
	{
		return false;
	}
	const FVector Me = Self->GetActorLocation();
	const FVector Feet = Me - FVector(0.f, 0.f, 130.f);
	FVector Goal = CoreGoalLocation;
	if (AMechCharacter* M = CoreGoalMech.Get(); M && (CoreGoal == ECoreGoal::Intercept || CoreGoal == ECoreGoal::Escort))
	{
		Goal = M->GetActorLocation() - FVector(0.f, 0.f, 130.f);
	}
	const bool bSameLayer = Arena->LayerOf(Me) == Arena->LayerOf(Goal);
	// 拦截、护送贴上去之后照常打；守站站到站边上就照常打
	const float Near = CoreGoal == ECoreGoal::Escort ? 1200.f : CoreGoal == ECoreGoal::Intercept ? 1500.f : CoreGoal == ECoreGoal::Guard ? 450.f : 0.f;
	if (Near > 0.f && bSameLayer && FVector::Dist2D(Me, Goal) < Near)
	{
		return false;
	}
	bOutBoost = FVector::Dist2D(Me, Goal) > 1500.f;
	// 交芯、救人、捡芯：走到点上站住
	if (bSameLayer && FVector::Dist2D(Me, Goal) < 140.f)
	{
		OutMove = FVector::ZeroVector;
		return true;
	}
	CoreRepath -= DeltaSeconds;
	if (CoreRepath <= 0.f || CorePath.Num() == 0 || FVector::Dist(Goal, CorePathGoal) > 600.f)
	{
		CoreRepath = 1.f;
		CorePathGoal = Goal;
		Arena->FindPath(Feet, Goal, CorePath, &CorePathKinds);
		CorePathIndex = 0;
		// 已经走过第一个路点（离第二个更近）就跳过
		if (CorePath.Num() >= 2 && CorePathKinds.IsValidIndex(1) && CorePathKinds[1] == AMechCoreArena::Walk &&
			Arena->LayerOf(Me) == Arena->LayerOf(CorePath[1]) && FVector::Dist2D(Me, CorePath[1]) < FVector::Dist2D(CorePath[0], CorePath[1]))
		{
			CorePathIndex = 1;
		}
	}
	if (CorePath.Num() == 0)
	{
		OutMove = (Goal - Me).GetSafeNormal2D();
		return true;
	}
	auto Reached = [&Feet](const FVector& P) { return FVector::Dist2D(Feet, P) < 260.f && FMath::Abs(Feet.Z - P.Z) < 250.f; };
	while (CorePathIndex < CorePath.Num() - 1 && Reached(CorePath[CorePathIndex]))
	{
		++CorePathIndex;
	}
	const FVector Next = CorePath[CorePathIndex];
	const uint8 Kind = CorePathKinds.IsValidIndex(CorePathIndex) ? CorePathKinds[CorePathIndex] : AMechCoreArena::Walk;
	if (Kind == AMechCoreArena::Lift && CorePathIndex > 0)
	{
		// 升降台：等平台到自己这一层再走上去，站在台子中间等它到另一层（到了 Reached 会往下一个路点走）
		const bool bDown = Next.Z < CorePath[CorePathIndex - 1].Z;
		const int32 Half = Next.X < Arena->Center().X ? 0 : 1;
		const float Depth = Arena->LiftDepth(Half);
		const FVector High = Arena->LiftCenter(Half, false);
		const FVector Low = Arena->LiftCenter(Half, true);
		const float PadZ = FMath::Lerp(High.Z, Low.Z, Depth);
		const float ToPad = FVector::Dist2D(Me, High);
		const bool bOnPad = ToPad < 330.f && FMath::Abs(Feet.Z - PadZ) < 150.f;
		if (bOnPad)
		{
			OutMove = ToPad > 120.f ? (High - Me).GetSafeNormal2D() : FVector::ZeroVector;
		}
		else
		{
			const bool bAtMyLevel = bDown ? Depth < 0.02f : Depth > 0.98f;
			const bool bCanDrop = bDown && Depth > 0.98f;  // 往下：平台已经在底下，直接跳下去
			OutMove = (bAtMyLevel || bCanDrop || ToPad > 700.f) ? (High - Me).GetSafeNormal2D() : FVector::ZeroVector;
		}
		bOutBoost = false;
		CoreStuckClock = 0.f;
		return true;
	}
	OutMove = (Next - Me).GetSafeNormal2D();
	// 卡住了（1.2 秒没走出 1.5 米）：侧移一下、跳一下、重新找路
	CoreStuckClock += DeltaSeconds;
	if (CoreStuckClock > 1.2f)
	{
		if (FVector::Dist2D(Me, CoreStuckFrom) < 150.f)
		{
			OutMove = (OutMove + FVector::CrossProduct(FVector::UpVector, OutMove) * (FMath::RandBool() ? 1.f : -1.f)).GetSafeNormal2D();
			if (!Self->IsCarryingCore() && Self->GetEnergy() > 30.f)
			{
				Self->SetJumpThrust(true);
				JumpHold = 0.5f;
			}
			CoreRepath = 0.f;
		}
		CoreStuckClock = 0.f;
		CoreStuckFrom = Me;
	}
	return true;
}

FString AMechAIController::CoreDebug() const
{
	const FVector O = CoreArena.IsValid() ? CoreArena->Center() : FVector::ZeroVector;
	const FVector Next = CorePath.IsValidIndex(CorePathIndex) ? CorePath[CorePathIndex] - O : FVector::ZeroVector;
	const uint8 Kind = CorePathKinds.IsValidIndex(CorePathIndex) ? CorePathKinds[CorePathIndex] : 0;
	return FString::Printf(TEXT("任务 %d 路点 %d/%d 下一个 (%.0f, %.0f, %.0f) 类型 %d"), static_cast<int32>(CoreGoal), CorePathIndex, CorePath.Num(), Next.X, Next.Y, Next.Z, Kind);
}

// ---------------------------------------------------------------- 高处（10-31）
// 用户 10-31：「AI 很少上屋顶和高架」——原来离场地中心高 12 米就往回走，随机跳上去的也会被拉下来；园区 AI 全挤在中间广场。
// 现在：高处图（MechHighGround）告诉 AI 哪些面能站、怎么过去；交战时挑看得见目标、比目标高 3 米以上的高点喷射上去打一会儿，
// 看不见目标时按格子找路（每台 AI 路线偏好不同），队友 12 米内互相让开一点。上去以后：只在同一片高处里横移（不会走下去），
// 待够了 / 看不见目标 2.5 秒 / 目标钻到脚下 / 拔刀了 → 找一条到街面的路（路径里就有从边上走下去的那一步）。
// 走路径卡住 1.6 秒就侧移 + 跳，连卡三次把那条边记成走不通、放弃一阵子，不会在高处原地打转。
// 10-31 搁置：默认关（不建高处图、AI 和改动前一样），-MechHighGround 才开；没跑完对比测试，进度见 docs/城区AI_搁置进度_20261031.md
UMechHighGround* AMechAIController::GetHigh()
{
	if (!MechHighGroundOn())
	{
		return nullptr;
	}
	if (!HighGround.IsValid())
	{
		HighGround = GetWorld()->GetSubsystem<UMechHighGround>();
	}
	UMechHighGround* HG = HighGround.Get();
	// 第一次调时开始分帧建图；没有 MechArena 标记的图永远是 nullptr（AI 照旧）
	if (!HG || !HG->Update() || CoreArena.IsValid())
	{
		return nullptr;
	}
	return HG;
}

bool AMechAIController::HighTick(AMechCharacter* Self, AMechCharacter* Target, bool bSee, float Dist, float Range, bool bBlade, float DeltaSeconds,
	FVector& InOutMove, bool& bOutBoost)
{
	UMechHighGround* HG = GetHigh();
	if (!HG)
	{
		return false;
	}
	const FVector Me = Self->GetActorLocation();
	const float HalfH = Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = Me - FVector(0.f, 0.f, HalfH);
	const FVector TFeet = MechLoop::Near(GetWorld(), Target->GetActorLocation(), Me) - FVector(0.f, 0.f, HalfH);
	const float MyH = Feet.Z - HG->FloorZ();
	const bool bGround = Self->GetCharacterMovement()->IsMovingOnGround();
	const FVector Toward = (TFeet - Feet).GetSafeNormal2D();
	// 目标比自己高出一截（近战够不着，要找路上去）
	const bool bTargetAbove = TFeet.Z - Feet.Z > 400.f;
	HighUnseen = bSee ? 0.f : HighUnseen + DeltaSeconds;
	HighSeen = bSee ? HighSeen + DeltaSeconds : 0.f;
	HighCooldown -= DeltaSeconds;
	ChaseCooldown -= DeltaSeconds;
	HighThink -= DeltaSeconds;
	HighClock += DeltaSeconds;

	if (HighState == EHigh::None)
	{
		// 站在高处（园区出生在基地台上、随机跳上去的、被打飞上去的）：看得见目标就当占着高点打一会儿；拔刀了 / 看不见 / 脚下不是一片高处就找路下去追
		if (bGround && MyH > UMechHighGround::HighMin - 100.f)
		{
			const int32 N = HG->FindNode(Feet);
			if (HighCooldown <= 0.f && N != INDEX_NONE && HG->NodeZone(N) >= 0 && bSee && !bBlade)
			{
				HighState = EHigh::Perch;
				HighGoal = N;
				HighClock = 0.f;
				HighLimit = FMath::FRandRange(3.f, 7.f) * (0.6f + HighLove);
				Self->SetJumpThrust(false);
				InOutMove = FVector::ZeroVector;
				return true;
			}
			// 看得见就往目标那边的街面下（GoDown 走完才算，不会一看见目标就停在边上）；看不见就直接找路追过去
			const int32 G = bSee ? INDEX_NONE : HG->FindNode(TFeet, 400.f, 200.f);
			const int32 Down = G != INDEX_NONE ? G : HG->NearestNode(FVector(Feet.X, Feet.Y, HG->FloorZ()) + Toward * 1500.f, 1500.f, true);
			if (ChaseCooldown <= 0.f)
			{
				if (Down != INDEX_NONE && HighPathTo(Self, Down, G != INDEX_NONE ? EHigh::Chase : EHigh::GoDown))
				{
					return true;
				}
				ChaseCooldown = 2.f;  // 找不到下去的路：先照原来的走法（会自己走下去），过两秒再试
			}
		}
		// 看不见目标一阵了（或者拔刀了、目标在够不着的高处）→ 按格子找路过去，代替原来「朝目标方向硬走、撞墙换向」
		if (ChaseCooldown <= 0.f && ((HighUnseen > 0.8f && Dist > 1500.f) || (bBlade && bTargetAbove && Dist < 3000.f)))
		{
			const int32 G = HG->FindNode(TFeet, 400.f, 200.f);
			const int32 Goal = G != INDEX_NONE ? G : HG->NearestNode(TFeet, 900.f);
			if (Goal != INDEX_NONE && HighPathTo(Self, Goal, EHigh::Chase))
			{
				return true;
			}
			ChaseCooldown = 1.5f;  // 找不到路：先照原来的走法，过一会儿再试
		}
		// 抢高点：在街面上、拿着枪、看得见目标又不太近，按性格有一定几率挑一个高点上去
		if (HighThink <= 0.f)
		{
			HighThink = FMath::FRandRange(3.f, 6.f);
			if (HighCooldown <= 0.f && !bBlade && bSee && Dist > 1800.f && MyH < UMechHighGround::HighMin && FMath::FRand() < HighLove)
			{
				TArray<int32> Cands;
				PickPerches(Self, Target, Range, Cands);
				for (int32 C : Cands)
				{
					if (HighPathTo(Self, C, EHigh::GoUp))
					{
						++Stat.GoUp;
						return true;
					}
				}
				HighCooldown = 3.f;
			}
		}
		// 队友别挤成一堆（园区 AI 以前全挤在中间广场）：12 米内的队友往外推一点
		for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
		{
			if (*It == Self || It->GetTeam() != Self->GetTeam() || It->GetHealth()->IsDead())
			{
				continue;
			}
			const FVector Away = -MechLoop::Delta(GetWorld(), Me, It->GetActorLocation());
			const float D = Away.Size2D();
			if (D > 1.f && D < 1200.f)
			{
				InOutMove += Away.GetSafeNormal2D() * (1.f - D / 1200.f) * 0.9f;
			}
		}
		return false;
	}

	if (HighState == EHigh::Perch)
	{
		// 被打下去 / 被钩爪拉走了：回去照常打
		if (bGround && MyH < UMechHighGround::HighMin - 150.f)
		{
			HighStop(Self, FMath::FRandRange(4.f, 8.f));
			return false;
		}
		const bool bUnder = Dist < 1300.f && TFeet.Z < Feet.Z - 400.f;  // 对方钻到脚下了，从上面打不着
		const bool bTooFar = Dist > FMath::Max(Range * 2.2f, 6000.f);
		if (HighClock > HighLimit || HighUnseen > 2.5f || bBlade || bUnder || bTooFar)
		{
			// 下去：往目标那边的街面上找个点，路径里自然有「从边上走下去」那一步
			const FVector Toward3 = FVector(Feet.X, Feet.Y, HG->FloorZ()) + Toward * FMath::Min(Dist * 0.5f, 1500.f);
			int32 G = HG->NearestNode(Toward3, 1200.f, true);
			if (G == INDEX_NONE)
			{
				G = HG->NearestNode(FVector(Feet.X, Feet.Y, HG->FloorZ()), 3000.f, true);
			}
			if (G != INDEX_NONE && HighPathTo(Self, G, EHigh::GoDown))
			{
				InOutMove = FVector::ZeroVector;
				return true;
			}
			HighStop(Self, 4.f);  // 找不到路：交回原来的打法（会自己走下去）
			return false;
		}
		// 在高点上：对着目标横移，只在同一片高处里走（下一格不是这片高处就掉头）；不冲刺、不乱跳
		PerchSideTimer -= DeltaSeconds;
		if (PerchSideTimer <= 0.f)
		{
			PerchSideTimer = FMath::FRandRange(1.2f, 3.f);
			PerchSide = FMath::RandBool() ? 1.f : -1.f;
		}
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward) * PerchSide;
		const int32 N = HG->FindNode(Feet, 250.f, 150.f);
		FVector Move = FVector::ZeroVector;
		if (N != INDEX_NONE)
		{
			if (HG->StepStaysInZone(N, Side))
			{
				Move = Side;
			}
			else if (HG->StepStaysInZone(N, -Side))
			{
				PerchSide = -PerchSide;
				Move = -Side;
			}
			else if (HG->StepStaysInZone(N, -Toward))
			{
				Move = -Toward * 0.5f;  // 横着没地方走：往后退半步（离边沿远一点）
			}
		}
		// 格子说能走，也别顶着护栏 / 机房原地蹭
		if (!Move.IsNearlyZero() && Blocked(Self, Move.GetSafeNormal2D()))
		{
			PerchSide = -PerchSide;
			Move = FVector::ZeroVector;
		}
		InOutMove = Move;
		bOutBoost = false;
		Self->SetJumpThrust(false);
		return true;
	}

	// 走路径：追人 / 上高点 / 下来
	if (HighState == EHigh::Chase && HighSeen > 0.4f && !(bBlade && bTargetAbove))
	{
		HighStop(Self, 0.f);  // 又看见了：回去照常横移着打
		return false;
	}
	if (HighState == EHigh::GoUp && bBlade)
	{
		HighStop(Self, 3.f);
		return false;
	}
	if (HighState == EHigh::Chase)
	{
		HighRepath -= DeltaSeconds;
		if (HighRepath <= 0.f || FVector::Dist(TFeet, ChaseGoal) > 800.f)
		{
			const int32 G = HG->FindNode(TFeet, 400.f, 200.f);
			const int32 Goal = G != INDEX_NONE ? G : HG->NearestNode(TFeet, 900.f);
			if (Goal == INDEX_NONE || !HighPathTo(Self, Goal, EHigh::Chase))
			{
				++Stat.ChaseFail;
				ChaseCooldown = 2.f;
				HighStop(Self, 0.f);
				return false;
			}
		}
	}
	const int32 R = HighFollow(Self, DeltaSeconds, InOutMove, bOutBoost);
	if (R == 1)
	{
		if (HighState == EHigh::GoUp)
		{
			++Stat.Arrive;
			HighState = EHigh::Perch;
			HighClock = 0.f;
			HighLimit = FMath::FRandRange(6.f, 12.f) * (0.6f + HighLove);
			Self->SetJumpThrust(false);
			InOutMove = FVector::ZeroVector;
			return true;
		}
		HighStop(Self, HighState == EHigh::GoDown ? FMath::FRandRange(5.f, 10.f) : 0.f);
		return false;
	}
	if (R == 2)
	{
		if (HighState == EHigh::Chase)
		{
			++Stat.ChaseFail;
			ChaseCooldown = FMath::FRandRange(2.f, 4.f);
			HighStop(Self, 0.f);
		}
		else
		{
			++Stat.Fail;
			HighStop(Self, FMath::FRandRange(3.f, 6.f));
		}
		return false;
	}
	return true;
}

bool AMechAIController::HighPathTo(AMechCharacter* Self, int32 Goal, EHigh NewState)
{
	UMechHighGround* HG = GetHigh();
	if (!HG || Goal == INDEX_NONE)
	{
		return false;
	}
	const FVector Feet = Self->GetActorLocation() - FVector(0.f, 0.f, Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	int32 From = HG->FindNode(Feet, 400.f, 150.f);
	if (From == INDEX_NONE)
	{
		From = HG->NearestNode(Feet, 600.f);
	}
	if (From == INDEX_NONE)
	{
		return false;
	}
	// 能喷多高：满能量喷到没电的高度打六折（地面型推进器不能喷，只能走坡道 / 跳台阶）
	const UMechMovementComponent* MC = Self->GetMechMovement();
	UMechHighGround::FAbility Ability;
	Ability.MaxClimb = MC->bCanThrust ? MC->MaxEnergy / FMath::Max(1.f, MC->ThrustDrain) * MC->MaxRiseSpeed * 0.6f : 0.f;
	float Cost = 0.f;
	TArray<FVector> Pts;
	TArray<uint8> Kinds;
	TArray<int32> PathNodes;
	if (!HG->FindPath(From, Goal, Ability, RouteSeed, &HighBad, Pts, Kinds, &PathNodes, &Cost))
	{
		return false;
	}
	// 抢高点：绕太远（比直线远 2.5 倍以上）就不去了
	if (NewState == EHigh::GoUp && Cost > FVector::Dist(Feet, HG->NodeFeet(Goal)) * 2.5f + 3000.f)
	{
		return false;
	}
	if (NewState == EHigh::Chase && HighState != EHigh::Chase)
	{
		++Stat.Chase;
	}
	if (NewState != HighState)
	{
		HighStuckCount = 0;  // 追人时每 2 秒按目标位置重找一次路：卡住次数接着数，不然连卡三次放弃永远凑不够
	}
	HighPath = MoveTemp(Pts);
	HighKinds = MoveTemp(Kinds);
	HighNodes = MoveTemp(PathNodes);
	HighState = NewState;
	HighIndex = 0;
	HighFrom = From;
	HighGoal = Goal;
	HighClock = 0.f;
	HighLimit = 8.f + Cost / 450.f;  // 代价按厘米算，走路约 6.8 米/秒，留足余量
	HighStuckClock = 0.f;
	HighBest = TNumericLimits<float>::Max();
	HighRepath = 2.f;
	ChaseGoal = HG->NodeFeet(Goal);
	return true;
}

void AMechAIController::HighStop(AMechCharacter* Self, float Cooldown)
{
	HighState = EHigh::None;
	HighPath.Reset();
	HighKinds.Reset();
	HighNodes.Reset();
	HighIndex = 0;
	HighGoal = INDEX_NONE;
	HighCooldown = FMath::Max(HighCooldown, Cooldown);
	HighThink = FMath::Max(HighThink, 2.f);
	if (bHighThrust || HighHop > 0.f)
	{
		Self->SetJumpThrust(false);
	}
	bHighThrust = false;
	HighHop = 0.f;
}

int32 AMechAIController::HighFollow(AMechCharacter* Self, float DeltaSeconds, FVector& OutMove, bool& bOutBoost)
{
	UMechHighGround* HG = GetHigh();
	const FVector Me = Self->GetActorLocation();
	const FVector Feet = Me - FVector(0.f, 0.f, Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const bool bGround = Self->GetCharacterMovement()->IsMovingOnGround();
	auto Reached = [&Feet, this](int32 i)
	{
		const FVector& P = HighPath[i];
		const uint8 K = HighKinds[i];
		const float D2 = FVector::Dist2D(Feet, P);
		if (K == UMechHighGround::Climb || K == UMechHighGround::Hop)
		{
			return D2 < 220.f && Feet.Z > P.Z - 60.f && Feet.Z < P.Z + 300.f;
		}
		if (K == UMechHighGround::Drop)
		{
			return D2 < 260.f && Feet.Z < P.Z + 200.f;  // 落到下面那层附近就算
		}
		return D2 < 200.f && FMath::Abs(Feet.Z - P.Z) < 150.f;
	};
	// 后面几个点里已经到了哪个就跳到它后面（往前看着走时会抄近路，不一定正好经过中间的点）
	for (int32 k = FMath::Min(HighPath.Num(), HighIndex + 4) - 1; k >= HighIndex; --k)
	{
		if (Reached(k))
		{
			HighIndex = k + 1;
			HighStuckClock = 0.f;
			HighStuckCount = 0;
			break;
		}
	}
	if (HighIndex >= HighPath.Num())
	{
		Self->SetJumpThrust(false);
		bHighThrust = false;
		return 1;
	}
	if (HighClock > HighLimit || !HG)
	{
		return 2;
	}
	const uint8 K = HighKinds[HighIndex];
	// 被打下来 / 撞偏了（离下一个点水平 12 米以上，或者掉到它下面 4 米）：从当前位置重新找路
	{
		const FVector& P = HighPath[HighIndex];
		if (bGround && (FVector::Dist2D(Feet, P) > 1200.f || (K != UMechHighGround::Climb && Feet.Z < P.Z - 400.f)) && HighRepath <= 1.f)
		{
			HighRepath = 2.f;
			const EHigh Keep = HighState;
			const int32 Goal = HighGoal;
			if (!HighPathTo(Self, Goal, Keep))
			{
				return 2;
			}
			return 0;
		}
		HighRepath -= HighState == EHigh::Chase ? 0.f : DeltaSeconds;  // 追人在 HighTick 里按目标位置重找
	}
	// 往前看几个点：同一高度、直线走得过去就直接朝远的点走（3 米一格的折线走起来一顿一顿的）
	int32 Aim = HighIndex;
	if (K == UMechHighGround::Walk)
	{
		for (int32 k = HighIndex + 1; k < FMath::Min(HighPath.Num(), HighIndex + 4); ++k)
		{
			if (HighKinds[k] != UMechHighGround::Walk || FMath::Abs(HighPath[k].Z - Feet.Z) > 80.f)
			{
				break;
			}
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MechAIHighAim), true, Self);
			if (GetWorld()->LineTraceSingleByObjectType(Hit, Me, FVector(HighPath[k].X, HighPath[k].Y, Me.Z), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{
				break;
			}
			Aim = k;
		}
	}
	const FVector P = HighPath[Aim];
	FVector Dir = (P - Me).GetSafeNormal2D();
	const float D2 = FVector::Dist2D(Me, P);
	bool bThrust = false;
	bOutBoost = D2 > 1500.f && K != UMechHighGround::Climb;
	if (K == UMechHighGround::Climb)
	{
		bOutBoost = false;
		if (Feet.Z < P.Z - 40.f)
		{
			const UMechMovementComponent* MC = Self->GetMechMovement();
			const float Need = (P.Z - Feet.Z) / FMath::Max(100.f, MC->MaxRiseSpeed) * MC->ThrustDrain + 15.f;
			if (bGround && (Self->GetEnergy() < Need || Self->IsOverheated()))
			{
				// 能量不够喷上去：在墙根等着回能（照样瞄准开枪），不算卡住
				Self->SetJumpThrust(false);
				bHighThrust = false;
				HighStuckClock = 0.f;
				OutMove = D2 > 500.f ? Dir : FVector::ZeroVector;
				return 0;
			}
			bThrust = !bGround || D2 < 500.f;  // 走到墙根再起跳，贴着墙往上喷（横向照样往目标推，到了高度自然翻上去）
		}
		else if (!bGround && Feet.Z < P.Z + 80.f)
		{
			bThrust = true;  // 刚过边沿，再喷一点免得脚被台沿挂住
		}
	}
	else if (K == UMechHighGround::Hop && bGround && D2 < 450.f && HighHop <= 0.f)
	{
		HighHop = 0.35f;  // 跳过矮护栏 / 上台阶：按 0.35 秒（只起跳，不怎么喷）
	}
	HighHop -= DeltaSeconds;
	// 卡住：1.6 秒没往前走 1 米（剩下的点数 × 100 米 + 到正在朝的那个点的距离，点数一少就算进展）→ 侧移 + 跳；
	// 连卡三次 = 这条边走不通，记下来放弃
	const float Prog = (HighPath.Num() - Aim) * 10000.f + FVector::Dist(Feet, P);
	if (Prog < HighBest - 100.f)
	{
		HighBest = Prog;
		HighStuckClock = 0.f;
	}
	else
	{
		HighStuckClock += DeltaSeconds;
	}
	if (HighStuckClock > 1.6f)
	{
		HighStuckClock = 0.f;
		HighBest = Prog;
		if (++HighStuckCount >= 3)
		{
			const int32 A = HighIndex > 0 ? HighNodes[HighIndex - 1] : HighFrom;
			HighBad.Add((static_cast<uint64>(A) << 32) | static_cast<uint32>(HighNodes[HighIndex]));
			UE_LOG(LogTemp, Display, TEXT("[ai] %s 高处路径走不通（状态 %d，第 %d/%d 段，方式 %d），记下这条边"), *Self->DisplayName, static_cast<int32>(HighState),
				HighIndex, HighPath.Num(), K);
			return 2;
		}
		Dir = (Dir + FVector::CrossProduct(FVector::UpVector, Dir) * (FMath::RandBool() ? 1.f : -1.f)).GetSafeNormal2D();
		if (K != UMechHighGround::Climb)
		{
			HighHop = 0.35f;
		}
	}
	const bool bWant = bThrust || HighHop > 0.f;
	Self->SetJumpThrust(bWant);
	bHighThrust = bThrust;
	OutMove = Dir;
	return 0;
}

void AMechAIController::PickPerches(AMechCharacter* Self, AMechCharacter* Target, float Range, TArray<int32>& Out) const
{
	const UMechHighGround* HG = HighGround.Get();
	if (!HG || !HG->IsReady() || HG->PerchNodes().Num() == 0)
	{
		return;
	}
	const TArray<int32>& Perches = HG->PerchNodes();
	const float HalfH = Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = Self->GetActorLocation() - FVector(0.f, 0.f, HalfH);
	const FVector TFeet = MechLoop::Near(GetWorld(), Target->GetActorLocation(), Feet) - FVector(0.f, 0.f, HalfH);
	const FVector TChest = MechLoop::Near(GetWorld(), Target->GetActorLocation(), Feet) + FVector(0.f, 0.f, 40.f);
	// 队友已经占着 / 正要去的高点：别扎堆
	TArray<FVector, TInlineAllocator<4>> Claimed;
	for (TActorIterator<AMechAIController> It(GetWorld()); It; ++It)
	{
		const AMechCharacter* Mate = Cast<AMechCharacter>(It->GetPawn());
		if (*It != this && Mate && Mate->GetTeam() == Self->GetTeam() && It->HighGoal != INDEX_NONE &&
			(It->HighState == EHigh::GoUp || It->HighState == EHigh::Perch) && HG->IsValidNode(It->HighGoal))
		{
			Claimed.Add(HG->NodeFeet(It->HighGoal));
		}
	}
	struct FCand
	{
		float Score;
		int32 Node;
	};
	TArray<FCand> Cands;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechAIPerchLOS), false, Self);
	Params.AddIgnoredActor(Target);
	for (int32 k = 0; k < 60; ++k)
	{
		const int32 N = Perches[FMath::RandHelper(Perches.Num())];
		const FVector F = HG->NodeFeet(N);
		const float DSelf = FVector::Dist(F, Feet);
		const float DT = FVector::Dist2D(F, TFeet);
		const float Up = F.Z - TFeet.Z;
		if (DSelf > 6000.f || DT < 1200.f || DT > FMath::Max(Range * 1.8f, 4500.f) || Up < 300.f)
		{
			continue;
		}
		// 站上去看得见目标（眼睛高 2.3 米）
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByObjectType(Hit, F + FVector(0.f, 0.f, 230.f), TChest, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			continue;
		}
		// 离喜欢的距离近、离自己近、比目标高得多的好；再加点随机，每次不全挑同一个
		float Score = FMath::Abs(DT - Range) / 1000.f + DSelf / 2000.f - FMath::Min(Up, 1500.f) / 800.f + FMath::FRand() * 0.6f;
		for (const FVector& C : Claimed)
		{
			Score += FVector::Dist(C, F) < 1500.f ? 3.f : 0.f;
		}
		Cands.Add({Score, N});
	}
	Cands.Sort([](const FCand& A, const FCand& B) { return A.Score < B.Score; });
	for (int32 i = 0; i < FMath::Min(3, Cands.Num()); ++i)
	{
		Out.Add(Cands[i].Node);
	}
}

// ---------------------------------------------------------------- 统计（-MechAIStats）
// 只在有高处图的地图上统计（要街面高度）。高处 = 脚比街面高 5 米以上（含喷在空中）；站在高处 = 同时脚踩着地。
// 卡住 = 连续 3 秒一直在给移动输入、却没离开 1 米（被网住 / EMP / 失衡 / 出招中不算）；
// 打转 = 6 秒里走了 12 米以上、却一直在 3 米半径里来回。每 10 秒打一行累计，每 2.5 秒打一行位置（[aipos]，区域分布用）。
void AMechAIController::StatTick(AMechCharacter* Self, float DeltaSeconds, bool bIntent, bool bExcuse)
{
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("MechAIStats"));
	if (!bOn)
	{
		return;
	}
	if (!HighGround.IsValid())
	{
		HighGround = GetWorld()->GetSubsystem<UMechHighGround>();
	}
	UMechHighGround* HG = HighGround.Get();
	if (!HG || !HG->Update())
	{
		return;
	}
	StatClock += DeltaSeconds;
	if (StatClock < 0.5f)
	{
		return;
	}
	StatClock = FMath::Min(StatClock - 0.5f, 0.5f);
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - StatLastSample > 1.2f)
	{
		StatPos.Reset();  // 阵亡等重生、回合间：窗口重新开始
		StatIntent.Reset();
		bStatStuck = false;
		bStatSpin = false;
	}
	StatLastSample = Now;
	const FVector Feet = Self->GetActorLocation() - FVector(0.f, 0.f, Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const float H = Feet.Z - HG->FloorZ();
	Stat.Alive += 0.5f;
	if (H >= UMechHighGround::HighMin)
	{
		Stat.High += 0.5f;
		Stat.HighStand += Self->GetCharacterMovement()->IsMovingOnGround() ? 0.5f : 0.f;
	}
	if (H < -800.f && !bStatFell)
	{
		bStatFell = true;
		++Stat.Fall;
		UE_LOG(LogTemp, Warning, TEXT("[aistat] %s 掉到街面以下 %.0f 米 位置 %s"), *Self->DisplayName, -H / 100.f, *Feet.ToString());
	}
	else if (H > -200.f)
	{
		bStatFell = false;
	}
	StatPos.Add(Feet);
	StatIntent.Add(bIntent && !bExcuse ? 1 : 0);
	if (StatPos.Num() > 12)
	{
		StatPos.RemoveAt(0);
		StatIntent.RemoveAt(0);
	}
	if (StatPos.Num() >= 6)
	{
		const int32 S0 = StatPos.Num() - 6;
		bool bAll = true;
		float MaxD = 0.f;
		for (int32 i = S0; i < StatPos.Num(); ++i)
		{
			bAll = bAll && StatIntent[i] != 0;
			MaxD = FMath::Max(MaxD, FVector::Dist(StatPos[i], StatPos[S0]));
		}
		if (bAll && MaxD < 100.f && !bStatStuck)
		{
			bStatStuck = true;
			StatStuckAt = Feet;
			++Stat.Stuck;
			UE_LOG(LogTemp, Display, TEXT("[aistat] %s 卡住：位置 (%.0f, %.0f) 离街面 %.1f 米，高处状态 %d"), *Self->DisplayName, Feet.X / 100.f, Feet.Y / 100.f, H / 100.f,
				static_cast<int32>(HighState));
		}
		if (bStatStuck && FVector::Dist(Feet, StatStuckAt) > 300.f)
		{
			bStatStuck = false;
		}
	}
	if (StatPos.Num() >= 12)
	{
		FVector Mean = FVector::ZeroVector;
		float Path = 0.f;
		for (int32 i = 0; i < StatPos.Num(); ++i)
		{
			Mean += StatPos[i] / StatPos.Num();
			Path += i > 0 ? FVector::Dist(StatPos[i], StatPos[i - 1]) : 0.f;
		}
		float Radius = 0.f;
		for (const FVector& P : StatPos)
		{
			Radius = FMath::Max(Radius, FVector::Dist(P, Mean));
		}
		if (Path > 1200.f && Radius < 300.f && !bStatSpin)
		{
			bStatSpin = true;
			StatSpinAt = Feet;
			++Stat.Spin;
			UE_LOG(LogTemp, Display, TEXT("[aistat] %s 原地打转：6 秒走了 %.0f 米、半径 %.1f 米，位置 (%.0f, %.0f) 离街面 %.1f 米，高处状态 %d"), *Self->DisplayName,
				Path / 100.f, Radius / 100.f, Feet.X / 100.f, Feet.Y / 100.f, H / 100.f, static_cast<int32>(HighState));
		}
		if (bStatSpin && FVector::Dist(Feet, StatSpinAt) > 600.f)
		{
			bStatSpin = false;
		}
	}
	StatPosPrint += 0.5f;
	if (StatPosPrint >= 2.5f)
	{
		StatPosPrint = 0.f;
		UE_LOG(LogTemp, Display, TEXT("[aipos] %s %.0f %.0f %.1f %d"), *Self->DisplayName, Feet.X / 100.f, Feet.Y / 100.f, H / 100.f, static_cast<int32>(HighState));
	}
	StatPrint += 0.5f;
	if (StatPrint >= 10.f)
	{
		StatPrint = 0.f;
		UE_LOG(LogTemp, Display, TEXT("[aistat] %s 累计：存活 %.0f 秒 高处 %.0f 秒（站着 %.0f）卡住 %d 打转 %d 掉出 %d 去高点 %d 到达 %d 放弃 %d 找路追 %d 追丢 %d 高处逻辑 %s"),
			*Self->DisplayName, Stat.Alive, Stat.High, Stat.HighStand, Stat.Stuck, Stat.Spin, Stat.Fall, Stat.GoUp, Stat.Arrive, Stat.Fail, Stat.Chase, Stat.ChaseFail,
			GetHigh() ? TEXT("开") : TEXT("关"));
	}
}

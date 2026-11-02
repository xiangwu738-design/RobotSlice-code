// 保卫战的肉鸽成长（11-01 可玩小切片，用户：「之前那个只增强数值的我不太满意」「我想是每一把武器都有路径」「路径不一定要明确」）。
// 设计见 docs/保卫战_机制词条方案.md。这一版：步枪、大剑、导弹三件武器每件两条路径（每条 3 步），框架每家一条路径（每条 2 步），共 24 张卡。
//
// 不锁路径：每张卡只要求上一步已经拿了，同一件武器两条线可以混着拿，靠等级数自然限制；界面不显示「选路径」。
//
// 对战绝对不能受影响，所以：
//   · 状态都在 UMechRunComponent 里，只有保卫战的调度器（AMechDefenseDirector::PlayerTick）才给玩家控制器加这个组件；
//   · 机体上只存一个指针（AMechCharacter::GetRun），对战里永远是空，钩子第一句就是判空，空了什么都不做；
//   · 特效的组播放在调度器上（对战里根本没有调度器），不给战斗组件加 RPC。
//
// 放在玩家控制器上（不放机体）：阵亡重生时旧机体被销毁（AMechGameMode::SpawnRangeMech），等级和卡要留下来；
// 玩家控制器只复制给本人，经验、等级、手上的卡正好是「服务器算，复制给本人」。别人的机体在本机上 GetRun() 是空：规则都在服务器算，表现走组播。
//
// 钩子都是一两行，规则都写在 MechRun.cpp（UMechRunComponent 是战斗组件的 friend）；数值都在下面 MechRun 命名空间这一处，试玩后在这里调。
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "MechLoadout.h"
#include "MechTypes.h"
#include "MechRun.generated.h"

class AMechCharacter;
class AMechDefenseDirector;
class AMechGrunt;
class AMechMissile;
class APlayerController;
class UMechCombatComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class UStaticMesh;
class UStaticMeshComponent;
class USoundBase;
struct FMechBullet;
enum class EMechDefenseKill : uint8;  // MechScore.h

/** 24 张卡（同一条路径的步数连续排，上一步 = 编号减一；存成 Owned 的位，只能往后加） */
enum class EMechRunCard : uint8
{
	Flood1, Flood2, Flood3,     // 步枪 洪流
	Burst1, Burst2, Burst3,     // 步枪 点射
	Rift1, Rift2, Rift3,        // 大剑 地裂
	Charge1, Charge2, Charge3,  // 大剑 蓄力
	Swarm1, Swarm2, Swarm3,     // 导弹 蜂群
	Heavy1, Heavy2, Heavy3,     // 导弹 重弹
	Repair1, Repair2,           // 新纪 抢修
	Supply1, Supply2,           // 维克托 补给
	Air1, Air2,                 // 相位 空战
	Num
};

/** 卡属于哪一件（出卡时看你带没带） */
enum class EMechRunItem : uint8
{
	Rifle,
	GreatSword,
	Missiles,
	FrameNewEra,
	FrameVictor,
	FramePhase,
	Num
};

/** 组播给各端的特效（AMechDefenseDirector::MulticastRunFx → AMechRunFx::Play） */
enum class EMechRunFxKind : uint8
{
	Wave,     // 地裂冲击波：At 起点，Vec 起点到终点，A 长度，B 半宽，C 速度
	Slash,    // 蓄满的横贯一刀：同上
	Strip,    // 地裂路径的持续伤害：At 起点，Vec 起点到终点，A 半宽，B 持续
	Fire,     // 重弹火场：At 落点，A 半径，B 持续
	Ring,     // 地面冲击环：At，A 半径，B 持续，C 颜色档（0 橙，1 金，2 青）
	LevelUp   // 升级：At 机体位置
};

namespace MechRun
{
	// ---------------------------------------------------------------- 等级和经验（docs/保卫战_成长系统方案.md 第二节）
	/** 击杀经验（下标 EMechDefenseKill：自爆、机枪、炮车、迫击炮、精英、首领） */
	constexpr int32 XpKill[6] = {3, 5, 20, 20, 60, 200};
	/** 第 N 波打完人人加：10 + 5 × N */
	constexpr int32 XpWaveBase = 10;
	constexpr int32 XpWavePer = 5;
	/** 第 N 级升到 N+1 级要：30 + 15 × N */
	constexpr int32 XpLevelBase = 30;
	constexpr int32 XpLevelPer = 15;
	/** 每升一级：装甲上限、所有伤害（升级时装甲回满） */
	constexpr float LevelArmor = 0.03f;
	constexpr float LevelDamage = 0.02f;

	// ---------------------------------------------------------------- 步枪 洪流
	/** 3 满层时分出的追踪弹：伤害是这一发的几成、找多远的别的敌人、速度、每秒最多拐多少度 */
	constexpr float HomingDamage = 0.5f;
	constexpr float HomingRange = 3000.f;
	constexpr float HomingSpeed = 11000.f;
	constexpr float HomingTurn = 900.f;
	constexpr float HomingLife = 0.9f;
	// ---- 步枪 点射
	/** 1 三连发：一组内两发的间隔、组间间隔（一轮 0.41 秒 3 发 = 每秒 7.3 发，比连射的 9.1 发少）、单发伤害（DPS 和连射差不多，但吃准头） */
	constexpr int32 BurstCount = 3;
	constexpr float BurstGap = 0.055f;
	constexpr float BurstRest = 0.3f;
	constexpr float BurstDamage = 1.25f;
	/** 一组子弹多久没打全就不等了 */
	constexpr float BurstGroupLife = 1.5f;
	/** 3 引爆：半径、伤害是这一发的几倍、冲击 */
	constexpr float DetonateRadius = 550.f;
	constexpr float DetonateDamage = 3.f;
	constexpr float DetonateImpact = 200.f;

	// ---------------------------------------------------------------- 大剑 地裂
	/** 1 冲击波：长度、半宽、速度、伤害是这一刀的几成、冲击、上下能打到多高（低飞的自爆无人机打得到，高处的机枪无人机打不到） */
	constexpr float WaveLength = 2000.f;
	constexpr float WaveHalfWidth = 260.f;
	constexpr float WaveSpeed = 4500.f;
	constexpr float WaveDamage = 0.6f;
	constexpr float WaveImpact = 300.f;
	constexpr float WaveHalfHeight = 700.f;
	/** 2 三道扇形：左右各偏几度 */
	constexpr float WaveFan = 24.f;
	/** 3 路径留持续伤害：多久、每秒伤害、半宽、几秒结算一次 */
	constexpr float StripTime = 4.f;
	constexpr float StripDps = 320.f;
	constexpr float StripHalfWidth = 220.f;
	constexpr float ZoneTick = 0.25f;
	// ---- 大剑 蓄力
	/** 1 蓄力：蓄满要几秒、蓄满时判定范围 / 伤害的倍数、按住时停在重斩起手的第几成（不改动画的简单版：招式时间停住） */
	constexpr float ChargeFull = 3.f;
	constexpr float ChargeReach = 2.6f;
	constexpr float ChargeDamage = 2.f;
	constexpr float ChargeHoldPose = 0.55f;
	/** 2 蓄力时受到的伤害（不吃失衡、不被打断照大剑霸体的规则） */
	constexpr float ChargeDamageTaken = 0.5f;
	/** 3 横贯：长度（按场地 200 米多一点的刷怪距离，从平台上差不多能扫到一半场子）、半宽、速度、伤害是这一刀的几倍、冲击 */
	constexpr float SlashLength = 12000.f;
	constexpr float SlashHalfWidth = 420.f;
	constexpr float SlashSpeed = 16000.f;
	constexpr float SlashDamage = 1.5f;
	constexpr float SlashImpact = 900.f;

	// ---------------------------------------------------------------- 导弹 蜂群
	/** 1 锁杂兵：准星多大角度内能锁上、放宽到多少还保持、多远（锁定时间照导弹的 0.8 秒 × 框架倍数） */
	constexpr float LockCone = 13.f;
	constexpr float LockKeep = 24.f;
	constexpr float LockRange = 26000.f;
	/** 2 分裂：几枚、伤害、半径（相对母弹）、找多远的敌人、画面大小 */
	constexpr int32 SplitCount = 3;
	constexpr float SplitDamage = 0.4f;
	constexpr float SplitRadius = 0.75f;
	constexpr float SplitRange = 3500.f;
	constexpr float SplitScale = 0.6f;
	/** 3 全锁：视野多大角度（半角）、最多打多少枚（一轮本来 4 枚，敌人多就多打） */
	constexpr float AllCone = 45.f;
	constexpr int32 AllMax = 12;
	// ---- 导弹 重弹
	/** 1 大导弹：伤害（一轮的总伤害 ×）、半径 ×、冲击 ×、画面大小 */
	constexpr float BigDamage = 1.2f;
	constexpr float BigRadius = 1.5f;
	constexpr float BigImpact = 3.f;
	constexpr float BigScale = 2.6f;
	/** 2 范围翻倍、打出失衡：失衡倍数（机甲一下打满），杂兵没有失衡条，改成瘫几秒 */
	constexpr float ShockRadius = 2.f;
	constexpr float ShockStagger = 10.f;
	constexpr float ShockStun = 2.5f;
	/** 3 火场：持续、半径（相对爆炸半径）、每秒伤害 */
	constexpr float FireTime = 3.f;
	constexpr float FireRadius = 0.8f;
	constexpr float FireDps = 420.f;

	// ---------------------------------------------------------------- 框架
	/** 新纪 抢修 1：挨打时也修，速度是平时的几成；2：修到几成 */
	constexpr float RepairHurtRate = 0.5f;
	constexpr float RepairFullCap = 1.f;
	/** 维克托 补给 1：击杀补几分之一弹匣；2：击杀减肩部冷却几秒 */
	constexpr float SupplyAmmo = 1.f / 3.f;
	constexpr float SupplyCooldown = 2.f;
	/** 相位 空战 1：离地 / 着地的伤害倍数；2：空中击杀回一次闪避的能量（按这台的闪避耗能算） */
	constexpr float AirDamage = 1.4f;
	constexpr float GroundDamage = 0.8f;

	/** 一张卡的文字（命令行代号、卡名、哪件、路径名、一句规则、第几步、这条路径共几步、属于哪件） */
	struct FCard
	{
		const TCHAR* Code;
		const TCHAR* Name;
		const TCHAR* ItemName;
		const TCHAR* PathName;
		const TCHAR* Rule;
		int32 Step;
		int32 Steps;
		EMechRunItem Item;
	};
	const FCard& Card(EMechRunCard C);
	constexpr int32 NumCards = static_cast<int32>(EMechRunCard::Num);
	/** 按命令行代号（flood1、charge3……）或卡名（洪流·穿透）找；找不到返回 Num */
	EMechRunCard FindCard(const FString& Code);
}

/** 测试和日志用的计数（-MechRunTest 逐张卡看它们；只在服务器上记） */
struct FMechRunDebug
{
	int32 PierceHits = 0;       // 穿透打中了几个（第 2 个起算）
	int32 MaxPierce = 0;        // 一发子弹最多打中几个杂兵
	int32 HomingSpawned = 0;
	int32 HomingHits = 0;
	int32 BurstShots = 0;
	int32 BurstGroups = 0;
	int32 BurstRefunds = 0;
	int32 Detonations = 0;
	int32 Waves = 0;
	int32 WaveHits = 0;
	int32 SlashHits = 0;
	int32 ZonesSpawned = 0;
	int32 ZoneHits = 0;
	float LastCharge = 0.f;     // 上一刀蓄了几成
	float LastReach = 0.f;      // 上一刀的判定半径
	int32 LastStrikeHits = 0;
	int32 SalvoMissiles = 0;
	int32 SalvoTargets = 0;
	int32 Splits = 0;
	float LastMissileRadius = 0.f;
	float LastMissileDamage = 0.f;
	int32 Stunned = 0;
	int32 AmmoGiven = 0;
	float CooldownGiven = 0.f;
	float EnergyGiven = 0.f;
	int32 Kills = 0;
};

UCLASS()
class UMechRunComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMechRunComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 这个控制器的组件（对战里没有） */
	static UMechRunComponent* Of(const APlayerController* PC);

	// ---------------------------------------------------------------- 状态（复制给本人）
	int32 GetLevel() const { return Level; }
	int32 GetXP() const { return XP; }
	int32 GetNextXP() const { return MechRun::XpLevelBase + MechRun::XpLevelPer * Level; }
	int32 GetPendingPicks() const { return PendingPicks; }
	const TArray<uint8>& GetOffer() const { return Offer; }
	uint32 GetOwned() const { return Owned; }
	bool Has(EMechRunCard C) const { return (Owned >> static_cast<uint32>(C)) & 1u; }
	/** 拿了这张卡、而且这件东西现在带着（换了配装，卡留着不生效，换回来还在） */
	bool Active(EMechRunCard C) const;
	/** 卡属于的这件现在带没带 */
	bool ItemEquipped(EMechRunItem Item) const;
	/** 服务器算的导弹锁定（HUD 画）：锁着哪个杂兵、进度 0–1 */
	AActor* GetLockActor() const { return LockActor; }
	float GetLockProgress() const { return LockProgress; }
	/** 刚选的卡和时间（HUD 闪一下卡名，本机记） */
	int32 GetLastPicked() const { return LastPicked; }
	float GetLastPickedTime() const { return LastPickedTime; }
	/** 客户端：画一发追踪弹（调度器组播过来；服务器那份是真的，这里只是画面） */
	static void AddVisualHoming(AMechCharacter* Mech, const FVector& From, const FVector& Dir, AActor* Target);
	/** 视野内能全锁的敌人（蜂群 3；服务器发射时、HUD 画框都调） */
	void CollectAllInView(const FVector& ViewLoc, const FVector& ViewDir, TArray<AActor*>& Out) const;

	// ---------------------------------------------------------------- 操作
	/** 本机：数字键 1 / 2 / 3（AMechPlayerController::PlayerTick 里调，菜单开着时不调） */
	void LocalInput(APlayerController* PC);
	UFUNCTION(Server, Reliable)
	void ServerPick(int32 Slot);
	/** 蓄力开始 / 松手（本机操控端调，服务器结算） */
	UFUNCTION(Server, Reliable)
	void ServerChargeBegin(AMechCharacter* InTarget);
	UFUNCTION(Server, Reliable)
	void ServerChargeEnd();
	/** 控制台 mech.Run.*（客户端也能用；服务器上直接生效） */
	UFUNCTION(Server, Reliable)
	void ServerCheat(const FString& What, int32 Number);

	// ---------------------------------------------------------------- 服务器：调度器调
	void AddXP(int32 Amount);
	/** 这台打死了敌人（Kind 见 MechScore.h，Victim 可能已经在销毁） */
	void OnKill(EMechDefenseKill Kind, AActor* Victim);
	void OnWaveEnd(int32 Wave);

	// ---------------------------------------------------------------- 钩子（都在服务器；只有带这个组件的机体才会走到）
	/** 所有伤害的倍数：等级 + 空战（UMechHealthComponent::ApplyHit、AMechGrunt::TakeDamage 调） */
	float OutgoingScale() const;
	/** 现场抢修（UMechHealthComponent::TickComponent 调）：改开始修的条件、速度、上限 */
	void TuneRepair(bool& bTime, float& Rate, float& Cap) const;

	// ---- 步枪
	bool KeepsStreak() const { return Active(EMechRunCard::Flood2); }
	bool IsBurstMode() const { return Active(EMechRunCard::Burst1); }
	bool IsBurstGoing() const { return BurstLeft > 0; }
	void BeforeBurstShot();
	void AfterBurstShot();
	/** 子弹刚打出去（服务器那份）：点射的组号和伤害 */
	void TagBullet(FMechBullet& B);
	/** 子弹打中杂兵：杂兵也算连中（洪流要它），返回这一发的伤害；顺带算追踪弹、点射 */
	float OnBulletHitGrunt(AActor* Grunt, FMechBullet& B, float Damage, const FVector& At);
	void OnBulletHitMech(AMechCharacter* Victim, FMechBullet& B, const FVector& At);
	/** 一发子弹穿过了一个杂兵（第二个起算穿透） */
	void NotePierce(const FMechBullet& B);

	// ---- 大剑
	/** 这一下左键按住要不要走蓄力（本机操控端调） */
	bool WantsChargeHold() const;
	/** 这一刀开始结算：判定范围和伤害的倍数 */
	void OnStrikeBegin(EMechMelee Move, float& ReachScale, float& DamageScale);
	/** 这一刀结算完：放冲击波、横贯 */
	void OnStrikeEnd(EMechMelee Move, float BaseDamage, float Reach, int32 Hits);

	// ---- 导弹
	/** 按下发射：这一轮打几枚、打谁 */
	void OnFireMissiles(int32& Pending, bool& bLocked);
	/** 这一枚（第 Index 枚）的目标和威力 */
	void ModifyMissile(int32 Index, AActor*& OutRunTarget, AMechCharacter*& OutMechTarget, float& Damage, float& Impact, float& Radius, float& Scale, float& Stagger);
	/** 导弹炸了：分裂、瘫痪杂兵、火场 */
	void OnMissileExplode(AMechMissile* Missile, const FVector& At);

	// ---------------------------------------------------------------- 测试（-MechRunTest，MechRunTest.cpp）
	FMechRunDebug Debug;
	void DebugReset(uint32 InCards, int32 InLevel = 1);
	void DebugClearZones();
	void DebugSetAmmo(int32 Ammo);
	void DebugSetMissileTimer(float Seconds);
	void DebugSetStreak(int32 N);
	/** 弹药满、冷却清零、出招打断 */
	void DebugResetCombat();
	/** 马上重抽一次卡 */
	void DebugForceOffer();
	void DebugGive(uint32 Bits) { Owned |= Bits; }
	int32 DebugZoneCount() const { return Zones.Num(); }
	int32 DebugWaveCount() const { return Waves.Num(); }
	/** 对一个敌人造成伤害（Attacker = 这台机体，算经验、算击杀）；地裂、火场、引爆、测试都走这里 */
	void HitEnemy(AActor* Enemy, float Damage, float Impact, const FVector& At, EMechHitKind Kind, float Stagger = -1.f);

private:
	UPROPERTY(Replicated)
	int32 Level = 1;
	UPROPERTY(Replicated)
	int32 XP = 0;
	UPROPERTY(ReplicatedUsing = OnRep_Owned)
	uint32 Owned = 0;
	UPROPERTY(Replicated)
	int32 PendingPicks = 0;
	/** 现在手上这一次的卡（最多 3 张，EMechRunCard） */
	UPROPERTY(Replicated)
	TArray<uint8> Offer;
	UPROPERTY(Replicated)
	TObjectPtr<AActor> LockActor;
	UPROPERTY(Replicated)
	float LockProgress = 0.f;
	UFUNCTION()
	void OnRep_Owned(uint32 OldOwned);
	void NotePicked(uint32 NewBits);

	float XpScale = 1.f;
	/** 经验倍率读过命令行了（构造时命令行还不一定对：只在服务器读一次） */
	bool bXpParsed = false;
	int32 LastPicked = -1;
	float LastPickedTime = -100.f;
	float LastPressTime = -100.f;
	int32 SeenLevel = 1;

	TWeakObjectPtr<AMechCharacter> BoundMech;
	AMechCharacter* GetMech() const { return BoundMech.Get(); }
	UMechCombatComponent* GetCombat() const;
	AMechDefenseDirector* GetDirector() const;
	mutable TWeakObjectPtr<AMechDefenseDirector> CachedDirector;
	void Bind(AMechCharacter* Mech);
	/** 重生时恢复配装：AMechGameMode::SpawnRangeMech 生成的新机体是默认配装（配装页只在进图时发一次），保卫战第一版就有这个问题，死一次武器就变了 */
	FMechLoadout SavedLoadout;
	bool bHaveLoadout = false;

	void ServerTick(AMechCharacter* Mech, float DeltaTime);
	void BuildOffer();
	bool Eligible(EMechRunCard C) const;
	void UpdateLock(AMechCharacter* Mech, float DeltaTime);

	// ---- 点射
	int32 BurstLeft = 0;
	int32 NextGroup = 1;
	int32 CurrentGroup = 0;
	struct FBurstGroup
	{
		int32 Id = 0;
		int32 Hits = 0;
		float Born = 0.f;
		bool bSameTarget = true;
		TWeakObjectPtr<AActor> Victim;
		float ShotDamage = 0.f;
	};
	TArray<FBurstGroup> Groups;
	void CountGroupHit(FMechBullet& B, AActor* Victim, const FVector& At);
	void Detonate(const FVector& At, float ShotDamage);

	// ---- 追踪弹（打中时记下，下一次 Tick 再加：加子弹会让 UpdateBullets 循环里的引用失效）
	struct FPendingHoming
	{
		FVector From;
		TWeakObjectPtr<AActor> Target;
		float Damage;
		float Impact;
	};
	TArray<FPendingHoming> PendingHoming;
	void AfterBulletHit(AActor* Victim, FMechBullet& B, const FVector& At);
	AActor* FindOtherEnemy(const AActor* Except, const FVector& At, float Range) const;

	// ---- 冲击波和地面持续伤害（服务器算）
	struct FWave
	{
		FVector From = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		float Length = 0.f;
		float Speed = 0.f;
		float HalfWidth = 0.f;
		float Damage = 0.f;
		float Impact = 0.f;
		float Done = 0.f;
		bool bSlash = false;
		bool bStrip = false;
		/** 同一刀放出来的算一组（三道扇形在根部是重叠的，贴脸不该一下吃三道）：打过的敌人这一组只算一次 */
		int32 Volley = 0;
	};
	TArray<FWave> Waves;
	int32 CurrentVolley = 0;
	TMap<int32, TArray<TWeakObjectPtr<AActor>>> VolleyHits;
	struct FZone
	{
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		float HalfWidth = 0.f;
		float Dps = 0.f;
		float EndAt = 0.f;
		float Clock = 0.f;
	};
	TArray<FZone> Zones;
	void AddWave(const FVector& From, const FVector& Dir, float Length, float HalfWidth, float Speed, float Damage, float Impact, bool bSlash, bool bStrip);
	void AddZone(const FVector& A, const FVector& B, float HalfWidth, float Dps, float Seconds);
	void UpdateWaves(float DeltaTime);
	void UpdateZones(float DeltaTime);
	/** 范围里的敌人（机甲 + 杂兵，去重） */
	void EnemiesInBox(const FVector& Center, const FVector& HalfExtent, TArray<AActor*>& Out) const;
	float GroundAt(const FVector& P) const;

	// ---- 导弹
	TArray<TWeakObjectPtr<AActor>> SalvoTargets;
};

/** 肉鸽卡的画面（各端本地、不复制）：地裂冲击波、横贯一刀、火场、蓄力光圈。第一次用时生成一个，放在世界里 */
UCLASS(NotPlaceable)
class AMechRunFx : public AActor
{
	GENERATED_BODY()

public:
	AMechRunFx();
	virtual void Tick(float DeltaTime) override;
	static AMechRunFx* Get(UWorld* World);
	void Play(EMechRunFxKind Kind, const FVector& At, const FVector& Vec, float A, float B, float C);
	/** 蓄力中（战斗组件在各端每帧调）：脚下一圈按蓄力涨到判定范围，机体隔一会儿闪一下，蓄满变金 */
	void ChargeTick(AMechCharacter* Mech, float Level, float Reach);
	/** 测试：放过几次特效（联机测试看客户端收到没有） */
	int32 DebugPlayed = 0;

private:
	/** 地上的面片（M_ShockRing）和一条光（M_MechBeam），用完留着复用 */
	struct FPatch
	{
		TObjectPtr<UStaticMeshComponent> Comp;
		TObjectPtr<UMaterialInstanceDynamic> MID;
		float Born = -100.f;
		float Life = 0.f;
		float Strength = 1.f;
		float Width = 0.08f;
		float Style = 0.f;
		bool bShrink = false;
		/** 这片地上的火舌（发光锥体，不用 Course_05 的火：那个自带很强的灯，几团叠在一起整个画面白粉一片，又没法从外面关） */
		TArray<TObjectPtr<UStaticMeshComponent>> Tongues;
		TObjectPtr<UMaterialInstanceDynamic> TongueMID;
		float TongueHeight = 300.f;
		float TongueWidth = 1.f;
		float NextSpark = 0.f;
		bool bFree = true;
	};
	TArray<FPatch> Patches;
	FPatch& NewPatch(UStaticMesh* Mesh, UMaterialInterface* Mat);
	void AddTongue(FPatch& P, const FVector& At);
	UStaticMeshComponent* MakeGlowMesh(UStaticMesh* Mesh);

	struct FCrest
	{
		FVector From;
		FVector Dir;
		float Length;
		float Speed;
		float HalfWidth;
		float Done = 0.f;
		float LastBurst = 0.f;
		bool bSlash = false;
		TObjectPtr<UStaticMeshComponent> Comp;
		TObjectPtr<UMaterialInstanceDynamic> MID;
	};
	TArray<FCrest> Crests;

	struct FBeam
	{
		TObjectPtr<UStaticMeshComponent> Comp;
		TObjectPtr<UMaterialInstanceDynamic> MID;
		float Born;
		float Life;
		float Radius;
		float Length;
		float Strength = 3.f;
		/** 沿路长出来（地裂的裂缝：跟着浪头走）：起点、方向、速度；0 = 一出来就是全长（横贯） */
		FVector From = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		float Grow = 0.f;
	};
	TArray<FBeam> Beams;

	struct FChargeVis
	{
		TWeakObjectPtr<AMechCharacter> Mech;
		TObjectPtr<UStaticMeshComponent> Comp;
		TObjectPtr<UMaterialInstanceDynamic> MID;
		float LastTick = -100.f;
		float FlashClock = 0.f;
		bool bFull = false;
	};
	TArray<FChargeVis> Charges;

	float GroundAt(const FVector& P) const;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaneMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> RingMat;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BeamMat;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> EarthFX;
	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> SparkFX;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> EmberFX;
	UPROPERTY()
	TObjectPtr<USoundBase> WaveSound;
	UPROPERTY()
	TObjectPtr<USoundBase> SlashSound;
	UPROPERTY()
	TObjectPtr<USoundBase> ChargeFullSound;
	UPROPERTY()
	TObjectPtr<USoundBase> LevelSound;
};

/** -MechRunTest（MechRunTest.cpp）：每张卡放一遍对应动作，日志 [run] 记效果，最后汇总。由组件在服务器的本机玩家上每帧调 */
namespace MechRunTest
{
	void Tick(UMechRunComponent* Run, AMechCharacter* Me, float DeltaTime);
	/** -MechRunNetTest=Host|Client（tools/run_roguelike_test.sh net）：本机双开，客户端升级、按键选卡、开枪、重斩、蓄力、放导弹，房主看服务器上的结果 */
	void NetTick(UMechRunComponent* Run, bool bHost, float DeltaTime);
}

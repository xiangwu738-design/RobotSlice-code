// 肩部装备里紧急脱离、弹射升空、震荡波、地雷这四件的实现。10-31 做的时候是独立的第六类配装「战术装备」（C 键）；
// 11-01 用户「战术装备其实是肩部装备」，并进肩部列表、按 E 用（AMechCharacter::MissilePressed 分派，无人机机身见 AMechCharacter::ShoulderMeshes）。
// 预测和网络代码都在这个组件里，没有重写，只改入口：GetType() = 肩部配装，不是这四件时 Pressed / AIThink 什么都不做。
// 按一下用一次，各自冷却。设计、数值、反制见 docs/战术装备_设计.md（键位、配装类那部分以这里和 README 为准）。
//  紧急脱离（移动）：朝按着的方向（没按方向 = 往后）0.38 秒冲出 22 米。不看能量、过热也能用——被围攻时最常见的死法是能量见底跑不掉，
//                  所以它不走能量条；代价是长冷却、路线笔直好预判，而且冲完能量清零（10-31 用户定，免得脱离完马上接着飞）。
//                  空中用保持高度；顺手松开自己的钩爪、挣脱别人的钩爪拉拽。
//  弹射升空（移动）：原地往上弹 16 米（带一点按键方向的水平位移），之后照常滑翔 / 下落。脱离地面近身围攻，代价是在空中很显眼。
//  震荡波（攻击）：蓄力 0.3 秒（机体发橙光、地上亮出 9 米范围圈），然后以自己为中心 9 米内的敌人被震开（走现有的冲击击退）、
//                  打断正在出的刀、受伤害和一点失衡。近身被围时把人推开，蓄力那一下就是给对手的提示（能闪开）。
//  地雷（攻击）：抛向准星（最远 15 米），落地 1 秒后待命（灯慢闪），敌人进 3.5 米内亮出爆炸范围、0.35 秒后炸。最多同时 2 颗，
//                  会被子弹打掉、被震荡波扫掉。本体在 AMechMine。
// 能不能用：活着、回合进行中、没失衡、没被网住；移动型另外在 EMP 中（推进器失灵）、带核心（夺芯）时不能用——这两样都是对手的反制手段。
// 联机：
//  移动型走角色移动组件的根运动（和钩爪拉自己同一个做法）：操控的那一端先算好终点（取整到厘米）和时长（毫秒），本地马上开始，
//  再发给服务器；服务器核一遍后用同一个终点、同一个时长施加同样的根运动（FRootMotionSource_MoveToForce，Matches 比的就是名字、
//  优先级、时长、终点），两边对得上就不会被拉回。移动包的自定义标志位已经用完，不能像快速闪避那样随移动包发；根运动的存档 / 重放引擎自己管。
//  攻击型：服务器结算；操控的那一端先播蓄力 / 抛出的画面和声音、冷却先走，服务器再组播给其他人（和肩部武器一样）。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MechTypes.h"
#include "MechTacticalComponent.generated.h"

class AActor;
class AMechCharacter;
class AMechMine;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraSystem;
class USoundBase;
class USoundAttenuation;

/** 地上的范围圈（震荡波蓄力 / 爆开、地雷引爆预警）：一圈首尾相连的细圆柱（ISM），材质是充能步枪光束那个叠加发光材质 M_MechBeam
 *  （不受光、自带白天曝光补偿，参数 Color / Intensity）。圈上每个点往下找地面贴上去，台阶、坡上也贴得住；粗细按离本机镜头的距离补偿。
 *  不用 EMP 的范围贴花材质 M_EmpField：那个材质 10-30 又改成了很淡的斑驳底色（EMP 的主角换成了闪电球），预警圈要清楚，自己画 */
namespace MechRangeRing
{
	/** 在 Owner 上建一个圈（平时没有实例），OutMID 是它的动态材质；网格没加载到返回空 */
	UInstancedStaticMeshComponent* Create(AActor* Owner, FName Name, UStaticMesh* Cylinder, UMaterialInterface* Material, UMaterialInstanceDynamic*& OutMID);
	/** 在 Center 周围半径 Radius 摆一圈并显示；Intensity 亮度、Color 颜色；bDashed 时隔一段空一段（预警用，和炸开时扩散的实线圈分开） */
	void Update(UInstancedStaticMeshComponent* Comp, UMaterialInstanceDynamic* MID, const FVector& Center, float Radius, float Intensity, const FLinearColor& Color, bool bDashed);
	void Hide(UInstancedStaticMeshComponent* Comp);
}

UCLASS(ClassGroup=(Mech))
class UMechTacticalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMechTacticalComponent();

	// ---------------------------------------------------------------- 操作（玩家按键、AI、测试都走这里）
	/** 按下 E（肩部配装是这四件时，AMechCharacter::MissilePressed 分派过来）：按配装用一次（冷却好了、能用的时候）。
	 *  DirOverride 非零时按这个水平方向（AI 用；玩家的紧急脱离 / 弹射升空按移动输入的方向）；
	 *  PointOverride 非零时地雷扔向这个点（AI 用；玩家扔向准星） */
	void Pressed(const FVector& DirOverride = FVector::ZeroVector, const FVector& PointOverride = FVector::ZeroVector);
	/** 机体换配装后调：冷却清零、正在蓄的震荡波取消（配装不是这四件时组件就停在这个状态） */
	void OnLoadoutChanged();
	/** 用到的特效（开局预热用） */
	void GetEffectSystems(TArray<UNiagaraSystem*>& Out) const;
	/** AI（服务器）：控制器每帧调一次，这里自己看局面决定用不用、往哪用（被围、挨打、有人冲刀过来、目标在中距离……）。
	 *  Target / bSeeTarget 是控制器选的目标和能不能看见，DamageWindow 是最近一小段时间挨的伤害（控制器在算） */
	void AIThink(AMechCharacter* Target, bool bSeeTarget, float DamageWindow, float DeltaSeconds);

	// ---------------------------------------------------------------- 状态（HUD、AI、测试）
	/** 就是肩部配装（MechIsTacticalShoulder 为 false 时这个组件不工作） */
	EMechShoulder GetType() const;
	static bool IsMobility(EMechShoulder Type) { return Type == EMechShoulder::Burst || Type == EMechShoulder::Launch; }
	/** 各装备的冷却（秒，没算插件效果；选配界面的属性条也用） */
	static float BaseCooldown(EMechShoulder Type);
	/** 同厂加成后的数值（11-01，MechWeapons::Own*）：紧急脱离距离、弹射高度、震荡波半径、同时几颗地雷 */
	float BurstDist() const;
	float LaunchRise() const;
	float ShockReach() const;
	int32 MineLimit() const;
	/** 就绪程度 0–1、还要冷却几秒 */
	float GetReady() const;
	float GetCooldown() const { return Cooldown; }
	/** 服务器：缩短冷却（保卫战 维克托 补给 2） */
	void RunReduceCooldown(float Seconds) { Cooldown = FMath::Max(0.f, Cooldown - Seconds); }
	/** 现在能不能用（不看冷却）：活着、回合进行中、没失衡、没被网住；移动型另外不能在 EMP 中、带核心时用 */
	bool CanUse() const;
	/** 震荡波蓄力进度 0–1（没在蓄为 -1；各端按本地画面算） */
	float GetCharge() const;
	/** 最近一次用的本地时间（HUD 闪一下）、冷却刚转好的本地时间（HUD 亮一下） */
	float GetLastUseTime() const { return LastUseTime; }
	float GetReadySince() const { return ReadySince; }
	/** 测试统计（服务器）：冲出 / 弹起的次数、震荡波打中几台、地雷放了几颗 */
	int32 DebugDashes = 0;
	int32 DebugShockHits = 0;
	int32 DebugMines = 0;

	// ---------------------------------------------------------------- 数值
	/** 紧急脱离：距离、冲出去的速度（决定时长）、到了以后留多少速度、冷却 */
	static constexpr float BurstDistance = 2200.f;
	static constexpr float BurstSpeed = 5800.f;
	static constexpr float BurstFinishSpeed = 1300.f;
	static constexpr float BurstCooldown = 12.f;
	/** 弹射升空：高度、顺着按键方向的水平位移、用时、到顶留多少速度（往上再飘一点）、冷却 */
	static constexpr float LaunchHeight = 1600.f;
	static constexpr float LaunchDrift = 400.f;
	static constexpr float LaunchDuration = 0.5f;
	static constexpr float LaunchFinishSpeed = 500.f;
	static constexpr float LaunchCooldown = 14.f;
	/** 移动型撞墙 / 顶到天花板后最少还要走多远才算数（不够就不用、不进冷却） */
	static constexpr float DashMinDistance = 300.f;
	/** 震荡波：蓄力时间、半径、伤害（中心，边上六成）、冲击（击退按它）、失衡倍数（冲击 × 这个才积累失衡）、冷却 */
	static constexpr float ShockCharge = 0.3f;
	static constexpr float ShockRadius = 900.f;
	static constexpr float ShockDamage = 400.f;
	static constexpr float ShockImpact = 850.f;
	static constexpr float ShockStagger = 0.5f;
	static constexpr float ShockCooldown = 15.f;
	/** 地雷：最远扔多远、冷却、同时最多几颗（其余数值在 AMechMine） */
	static constexpr float MineRange = 1500.f;
	static constexpr float MineCooldown = 10.f;
	static constexpr int32 MaxMines = 2;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	AMechCharacter* GetMech() const;
	float ServerNow() const;
	/** 冷却按插件效果缩短（「导弹冷却」那项对肩部武器都算，这里也算，和肩部武器一致） */
	float ScaledCooldown(float Base) const;
	/** 服务器认不认这次的冷却（客户端的冷却先走完，服务器那边晚半个来回才开始算，留一点余量） */
	bool CooldownReadyOnServer() const { return Cooldown <= 0.3f; }

	// ---- 移动型（紧急脱离 / 弹射升空）
	/** 算终点（取整到厘米，两端一致）和时长（毫秒）；OutDir 是水平方向（动作、特效用）。贴墙走不出去 / 头顶有东西弹不起来返回 false */
	bool PlanDash(EMechShoulder Type, const FVector& DirOverride, FVector& OutTarget, uint16& OutMs, FVector& OutDir) const;
	/** 两端都调：施加根运动；顺手松开自己的钩爪（拉自己那段）、挣脱别人钩爪的拉拽 */
	void ApplyDash(EMechShoulder Type, const FVector& Target, uint16 Ms);

	UFUNCTION(Server, Reliable)
	void ServerDash(uint8 Type, FVector_NetQuantize Target, uint16 Ms, uint8 Stamp, FVector_NetQuantizeNormal Dir);

	/** 服务器不认这一下（冷却没好、被网住……）：操控的客户端撤掉自己预测的根运动 */
	UFUNCTION(Client, Reliable)
	void ClientCancelDash(uint8 Stamp);

	uint8 DashStamp = 0;
	/** 操控的客户端预测紧急脱离前的能量 / 过热（用完会清零；服务器不认时 ClientCancelDash 还回去） */
	float PreDashEnergy = 0.f;
	bool bPreDashOverheated = false;

	// ---- 震荡波
	/** 服务器：开始蓄力（ShockAt 之后炸） */
	void StartShockAuthority();
	/** 服务器：炸开，结算范围里的敌人和地雷 */
	void ShockBlast();

	UFUNCTION(Server, Reliable)
	void ServerShock();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShockBlast(FVector_NetQuantize At);

	/** 服务器上震荡波炸开的世界时间（没在蓄为 -1） */
	float ShockAt = -1.f;

	// ---- 地雷
	/** 扔的起点（机体前上方）和初速度（抛物线落到瞄的点；水平最远 MineRange） */
	void PlanMineThrow(const FVector& PointOverride, FVector& OutOrigin, FVector& OutVelocity) const;
	void ThrowMineAuthority(const FVector& Origin, const FVector& Velocity);

	UFUNCTION(Server, Reliable)
	void ServerThrowMine(FVector_NetQuantize Origin, FVector_NetQuantize10 Velocity);

	/** 这台放出去的地雷（服务器，超出 MaxMines 时最早那颗碎掉） */
	TArray<TWeakObjectPtr<AMechMine>> Mines;

	// ---- 共用：开始用的画面和声音（操控的那一端按下时先播，服务器 / 其他人按组播播）
	/** Type = EMechShoulder（这四件之一），Dir = 水平方向（冲出去的方向 / 扔的方向） */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastUseFX(uint8 Type, FVector_NetQuantizeNormal Dir);
	void PlayUseFX(EMechShoulder Type, const FVector& Dir);
	/** 服务器认了这一下之后：服务器自己的画面（这台不是本机操控的才播，本机操控的按下时已经播过）+ 组播给其他人 */
	void BroadcastUse(EMechShoulder Type, const FVector& Dir);
	/** 震荡波炸开的画面和声音（各端） */
	void PlayShockBlast(const FVector& At);
	void PlayAt(USoundBase* Sound, const FVector& At, float Volume, float Pitch) const;

	UPROPERTY(Replicated)
	float Cooldown = 0.f;

	/** 冷却总长（就绪程度用） */
	UPROPERTY(Replicated)
	float CooldownLength = 1.f;

	float LastUseTime = -100.f;
	float ReadySince = -100.f;
	bool bWasReady = true;
	/** 推进器猛喷到什么时候（本地时间，各端自己播；冲刺 / 弹射的整段都开着） */
	float ThrustFxUntil = -1.f;
	/** 震荡波蓄力开始的本地时间（画面：机体发光、范围圈） */
	float ChargeStart = -100.f;
	/** 震荡波炸开的本地时间、地点（扩散的范围圈） */
	float BlastStart = -100.f;
	FVector BlastAt = FVector::ZeroVector;
	/** AI 每隔一会儿想一次（别每帧掷骰子） */
	float AIClock = 0.f;

	// ---------------------------------------------------------------- 表现（都用工程里已有的素材占位，要换的列在设计文档里）
	/** 震荡波的范围圈（MechRangeRing） */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Ring;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> RingMID;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> RingMaterial;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> DustFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> GroundBlastFX;

	UPROPERTY()
	TObjectPtr<USoundBase> BoostSound;

	UPROPERTY()
	TObjectPtr<USoundBase> PopSound;

	UPROPERTY()
	TObjectPtr<USoundBase> ChargeSound;

	UPROPERTY()
	TObjectPtr<USoundBase> BlastSound;

	UPROPERTY()
	TObjectPtr<USoundAttenuation> Attenuation;
};

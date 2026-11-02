// 机体武器与锁定。手上的武器分两种姿态，Q 切换（拔刀 / 收刀约 0.6 秒，期间不能攻击）：
//  射击：配装选的枪（实体弹，飞行有时间，靠提前量和闪避决定命中），右键按住瞄准（散布小、视野窄、慢走）。
//        每把枪的特色（过热和距离衰减、充能激光、耗能量和开镜吸附、吸血和压刀、连中加成、双持）见 MechWeapons。
//  近战：配装选的刀，左键点按轻击（最多四连，第一下冲向锁定目标），左键按住重斩（冲得更远、冲击大），右键按住格挡
//        （正面挨打伤害和冲击大减，每挡一下扣能量，能量不够就破防）。投掷匕首例外：左键投一把，按住扇形投三把。
// 肩部导弹（锁定后四发齐射追踪）两种姿态都能用。玩家和 AI 走同一套接口。
// 锁定：玩家取镜头中心锥形范围内最近的敌人，AI 由控制器指定；导弹锁定要瞄住目标一段时间。
// 联机：开火、导弹、挥刀都由服务器结算；客户端只发意图，表现由组播在各端各自播放。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MechTypes.h"
#include "MechWeapons.h"
#include "MechCombatComponent.generated.h"

class AMechCharacter;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;
class USoundAttenuation;
class UMaterialInterface;
class UMechMeleeFx;
class UMechRunComponent;

/** -MechTTKTest（测试场 L_Range?Range）：本机玩家那台由脚本把每把枪在 10 / 25 / 45 米、每把刀贴身各打木桩 10 秒，
 *  日志 [ttk] 记每秒伤害（直击加成折回去的基础值）、刀把中型框架打进失衡要几秒，打完退出。实现在 MechCombatTest.cpp */
namespace MechCombatTest
{
	void Tick(AMechCharacter* Me, float DeltaTime);
	/** -MechDuelTest：刀剑机制、霰弹压刀、三维突进、狙击吸附、手枪冷却、充能步枪逐项和木桩对打一遍（MechDuelTest.cpp） */
	void DuelTick(AMechCharacter* Me, float DeltaTime);
	/** -MechMeleeFeelTest：七把刀各从 13 米外侧身出轻击（远距离突进 + 转身）接四连、再重斩，侧面逐帧截图 + 每帧骨骼角度跳变 /
	 *  转向角速度 / 速度写 CSV（MechMeleeFeelTest.cpp，10-30 刀剑手感） */
	void MeleeFeelTick(AMechCharacter* Me, float DeltaTime);
}

/** 伤害飘字（只在打中别人的那个玩家自己这一端）：命中点、实际伤害、部位、出现的时间 */
struct FMechDamagePopup
{
	FVector At = FVector::ZeroVector;
	float Damage = 0.f;
	float Time = 0.f;
	EMechHitPart Part = EMechHitPart::Torso;
	EMechHitKind Kind = EMechHitKind::Bullet;
	bool bDirect = false;
	bool bKill = false;
};

/** 飞行中的子弹 / 飞刀（各端各自模拟；只有服务器上的会结算伤害） */
struct FMechBullet
{
	FVector Pos;
	FVector Vel;
	float Life = 0.f;
	float Traveled = 0.f;
	bool bLive = false;
	float Damage = 0.f;
	float Impact = 0.f;
	/** 曳光粗细倍数 */
	float Width = 1.f;
	/** 飞刀（画成匕首、按刀结算失衡） */
	bool bKnife = false;
	/** 飞刀追踪的目标、每秒最多拐多少度；画出来的大小倍数（强力匕首大一号） */
	TWeakObjectPtr<class AMechCharacter> Homing;
	float Turn = 0.f;
	float Scale = 1.f;
	// ---- 保卫战肉鸽（11-01，MechRun.h）：对战里全是默认值
	/** 洪流 1：穿过杂兵继续飞；已经穿过的杂兵 */
	bool bPierce = false;
	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<4>> Pierced;
	/** 点射：属于哪一组、这一发算过了没有 */
	int32 RunGroup = 0;
	bool bGroupCounted = false;
	/** 洪流 3 的追踪弹：追谁（可能是杂兵，所以不用上面的 Homing）；不再分裂、不算连中 */
	TWeakObjectPtr<AActor> RunHoming;
	bool bRunExtra = false;
};

UCLASS(ClassGroup=(Mech), meta=(BlueprintSpawnableComponent))
class UMechCombatComponent : public UActorComponent
{
	GENERATED_BODY()

	/** 保卫战肉鸽（11-01）：卡的规则都写在 MechRun.cpp，这里只留一两行钩子，它要读写弹药、连中、子弹这些状态 */
	friend class UMechRunComponent;

public:
	UMechCombatComponent();

	// ---------------------------------------------------------------- 操作（玩家输入和 AI 都调这些）
	void SetTrigger(bool bHeld);
	/** 配装：换射击 / 近战武器（弹药、热量、充能、匕首数都重置）；各端都要调 */
	void SetWeapons(EMechShooter InGun, EMechMeleeWeapon InBlade);
	/** 快速闪避时调用（服务器和操控者本地都会调）：双持手枪自动装满弹匣（有冷却）、单手刀记下闪避时刻（闪避斩） */
	void OnQuickBoost();
	void FireMissiles();
	void Reload();
	/** 模型查看：弹匣满的时候也换一次弹（看换弹动作） */
	void DebugReload()
	{
		Ammo = 0;
		Reload();
	}
	/** 模型查看：弹匣装满、热量清零（一直开火看姿势时不换弹） */
	void DebugRefill()
	{
		Ammo = FMath::Max(Ammo, GetMagazine());
		Heat = 0.f;
		OverheatTimer = 0.f;
	}
	/** Q：射击 / 近战切换 */
	void SwitchStance();
	/** 结算场景（MechResultsStage）本机生成的展示机体：直接换到这个姿态，不播拔刀 / 收刀 */
	void SetStanceNow(EMechStance InStance)
	{
		Stance = SwitchFrom = InStance;
		SwitchTime = -1.f;
	}
	/** 右键：射击姿态下瞄准，近战姿态下格挡 */
	void SetAltHeld(bool bHeld);
	/** 左键：近战姿态下点按轻击、按住重斩（射击姿态下左键走 SetTrigger） */
	void MeleePressed();
	void MeleeReleased();
	/** AI 用：直接出招 */
	void RequestLight();
	void RequestHeavy();
	/** 投掷匕首的右键：近身挥刀（匕首不能格挡；左键仍是按住投掷） */
	void RequestDaggerSlash();
	/** 旧接口：近战姿态下出一记轻击 */
	void Slash() { RequestLight(); }

	/** AI 用：直接指定目标（玩家由镜头自动锁定） */
	void SetForcedTarget(AMechCharacter* InTarget);
	/** 11-02 真人接管 AI 的机体时调：不再由控制器指定目标，回到玩家的自动锁定（以前 bForcedTarget 设上后永远不复位） */
	void ClearForcedTarget() { bForcedTarget = false; Target = nullptr; MissileLock = 0.f; }

	// ---------------------------------------------------------------- 状态（HUD、AI、角色用）
	AMechCharacter* GetTarget() const { return Target; }
	float GetMissileLock() const { return MissileLock; }
	/** 告警用（10-31，HUD 战机式告警）：这台对它的目标（GetTarget）的导弹锁定进度 0–1，服务器算好复制给所有人。
	 *  只在肩部是导弹、导弹就绪（冷却完、没在齐射）时非零——冷却中的锁定构不成威胁，不然 AI 一直锁着人就一直响。
	 *  MissileLock 本身只在服务器和操控那一端准，被锁的那一端要看这个 */
	float GetLockThreat() const { return RepLockThreat / 255.f; }
	int32 GetAmmo() const { return Ammo; }
	/** 弹匣容量：武器的弹匣 × 框架的弹匣倍数（维克托重工 +20%，四舍五入）；0 = 这把枪不用弹匣 */
	int32 GetMagazine() const { return GetGun().Magazine > 0 ? FMath::Max(1, FMath::RoundToInt(GetGun().Magazine * FrameMagScale)) : 0; }
	bool IsReloading() const { return ReloadTimer > 0.f; }
	float GetReloadProgress() const { return 1.f - ReloadTimer / FMath::Max(GetGun().ReloadTime, 0.01f); }
	// ---- 当前武器和它的特色状态
	EMechShooter GetGunType() const { return GunType; }
	EMechMeleeWeapon GetBladeType() const { return BladeType; }
	const MechWeapons::FGun& GetGun() const { return MechWeapons::Gun(GunType); }
	const MechWeapons::FBlade& GetBlade() const { return MechWeapons::Blade(BladeType); }
	/** 冲锋枪热量 0–1，过热时为真 */
	float GetHeat() const { return Heat; }
	bool IsOverheated() const { return OverheatTimer > 0.f; }
	/** 充能步枪充能 0–1（复制给所有人：别人也要画出这台的充能激光） */
	float GetCharge() const { return Charge; }
	/** 双持手枪：最近一次重斩砍中、弹匣补满的时间（HUD「装满」图形；各端按组播记） */
	float GetLastRefillTime() const { return LastRefillTime; }
	/** 最近一次重斩砍中敌人的时间（HUD 准星处的重击命中图形；各端按组播记） */
	float GetLastHeavyHitTime() const { return LastHeavyHitTime; }
	/** 化境（双手刀）：下一刀的伤害倍数（首击 x1.4，每打中一下 -0.1，最低 x0.7，停手 2.5 秒恢复）；服务器上才准 */
	float GetHuajingMult() const;
	/** 单手刀：刚快速闪避过，这时出轻击就是闪避斩 */
	bool CanDashSlash() const;
	/** 大剑霸体重斩中（服务器结算伤害时查） */
	bool IsSuperArmor() const;
	/** 同厂加成（11-01）：这一类配装和框架同一家造的就返回 Scale，不是返回 1 */
	float OwnScale(int32 Category, float Scale) const;
	/** 一轮打几枚导弹（维克托框架配维克托导弹多一枚）；肉鸽的重弹、全锁会改这一轮的枚数 */
	int32 SalvoSize() const;
	/** 肉鸽 蓄力（MechRun.h）：0 没在蓄，1 蓄（没有霸体），2 蓄 + 霸体减伤；所有人都知道（画蓄力光效） */
	uint8 GetRunChargeHold() const { return RunChargeHold; }
	/** 服务器：打断正在出的刀（失衡、被弹反、霰弹打中出刀中的人），各端一起收招 */
	void InterruptMelee();
	/** 格挡反馈（HUD 准星旁的提示图形）：最近一次刀被挡 / 弹反 / 破防过去多久、哪一种（0 挡住、1 弹反、2 破防）、
	 *  自己是挡的一方还是被挡的一方；各端都有（组播里记在双方的组件上） */
	float GetBlockFxAge() const { return GetWorld()->GetTimeSeconds() - BlockFxTime; }
	int32 GetBlockFxKind() const { return BlockFxKind; }
	bool IsBlockFxDefender() const { return bBlockFxDefender; }
	/** 拼刀反馈（HUD 准星处的图形，11-01）：最近一次拼刀过去多久，各端都有（组播里记在双方的组件上） */
	float GetClashFxAge() const { return GetWorld()->GetTimeSeconds() - ClashFxTime; }
	/** 测试（-MechDuelTest）：这台拼过几次刀（各端按组播记）；现在是不是正被拼刀弹开（服务器和操控者本地） */
	int32 DebugClashes = 0;
	bool IsClashKnocked() const { return ClashKnockClock >= 0.f; }
	/** 移动组件每个子步调（弹开中）：按曲线算这一小段的水平速度。放在子步里，一帧很长（卡顿、截图）时不会按开头最快的速度走完一整帧 */
	void ClashKnockSubstep(float Dt, FVector& InOutVelocity);
	/** 测试（-MechTTKTest）：服务器上这台打出去的伤害累计、其中「直击」（打失衡中的目标）折回不加成的伤害 */
	float DebugDamage = 0.f;
	float DebugBaseDamage = 0.f;
	/** 步枪连中层数 */
	int32 GetStreak() const { return Streak; }
	/** 投掷匕首：手上还有几把、下一把回来的进度 0–1 */
	int32 GetKnives() const { return Knives; }
	/** 投掷匕首：自上一把强力匕首后已经投了几把普通的（满 4 把，下一把就是强力匕首） */
	int32 GetKnifeStreak() const { return KnifeStreak; }
	float GetKnifeProgress() const;
	/** 双持手枪：最近一发是哪只手开的（0 右、1 左） */
	int32 GetLastShotHand() const { return ShotHand; }
	/** 最近一次霰弹枪吸血回复的时间（HUD 提示）、最近一次投出匕首的时间（动作用） */
	float GetLastHealTime() const { return LastHealTime; }
	float GetLastThrowTime() const { return LastThrowTime; }
	float GetMissileReady() const { return 1.f - MissileTimer / MissileCooldown; }
	/** 导弹还要冷却几秒 */
	float GetMissileTimer() const { return MissileTimer; }
	// ---- 姿态
	EMechStance GetStance() const { return Stance; }
	/** 切换进度 0–1，没在切换为 -1 */
	float GetSwitchProgress() const;
	/** 刀是不是在手上（切换过半就算换过去了；武器挂点、动作用） */
	bool IsBladeInHand() const;
	bool IsAiming() const { return bAiming; }
	bool IsGuarding() const { return bGuarding; }
	// ---- 近战
	EMechMelee GetMeleeMove() const { return MeleeMove; }
	/** 当前招式已过去的时间、命中时刻、整招时长（动作对齐用；已按武器快慢换算） */
	float GetMeleeTime() const { return MeleeTime; }
	float GetMeleeStrikeAt() const;
	float GetMeleeDuration() const;
	/** 当前这一刀可以接下一招的时刻（这之前机体不吃移动输入） */
	float GetMeleeCancel() const;
	bool IsMeleeActive() const { return MeleeMove != EMechMelee::None; }
	/** 近战特效（各端自己画；可能为空） */
	UMechMeleeFx* GetMeleeFx() const { return MeleeFx; }
	/** 出刀到可取消时刻之前（服务器和操控者本地）：速度和朝向整个由这里按突进曲线定，移动组件不再刹车、不再自己转向（10-30 刀剑手感） */
	bool IsMeleeDriving() const;
	/** 连招第几下（0 = 没在连招） */
	int32 GetComboStep() const;
	float GetHeavyReady() const { return 1.f - HeavyTimer / HeavyCooldown; }
	/** 左键按住多久了（重斩蓄力提示），没按为 0 */
	float GetMeleeHoldFraction() const;
	/** 格挡开始 / 结束后过去的时间、最近一次挡住攻击后过去的时间（动作用） */
	float GetGuardTime() const { return GuardTime; }
	float GetSinceGuardEnd() const { return SinceGuardEnd; }
	float GetSinceBlock() const { return SinceBlock; }
	bool IsGuardBroken() const { return GuardBreakTimer > 0.f; }
	/** 服务器上，受击方用：正面格挡住就把伤害和冲击减掉，返回是否挡住（钝器砍在格挡上直接破防，返回 false） */
	bool TryBlock(const FVector& From, float& Damage, float& Impact);
	/** 旧接口（HUD / AI）：近战能不能出手、在不在出招、冲刺距离 */
	float GetBladeReady() const { return (Stance == EMechStance::Blade && SwitchTime < 0.f && MeleeMove == EMechMelee::None) ? 1.f : 0.f; }
	bool IsSlashing() const { return IsMeleeActive(); }
	/** 最近在战斗（开火、挥刀、导弹）：机体要转向瞄准方向 */
	bool IsEngaged() const;
	/** 别的武器（钩爪、捕获网）发射时调：算进「最近在战斗」 */
	void NoteEngaged() { LastEngagedTime = GetWorld()->GetTimeSeconds(); }
	/** 右臂（枪）应该指的方向 */
	FVector GetAimDirection() const { return AimDir; }
	/** 摆姿势用的瞄准方向（上身、枪臂、瞄上瞄下）：从胸口指向 GetPoseAimPoint，不从枪口算，免得和姿势互相影响打摆子 */
	FVector GetPoseAimDirection() const { return PoseAimDir; }
	/** 摆姿势用的瞄准点（世界）：子弹真正要去的地方——玩家是准星点再按辅助瞄准拉向目标提前量，AI 是带手抖的提前量点；
	 *  别人的机体在客户端上用服务器同步来的点 */
	FVector GetPoseAimPoint() const { return PoseAimPoint; }
	/** 摆姿势瞄准方向的起点（胸口，世界） */
	FVector GetPoseAimPivot() const;
	/** 非零时枪臂按这个方向瞄（模型查看模式朝机体正前方，免得绕到前面看时枪掉头指镜头） */
	FVector AimOverride = FVector::ZeroVector;
	/** 最近一次命中敌人的时间和是否直击（HUD 命中提示） */
	float GetLastHitTime() const { return LastHitTime; }
	bool WasLastHitDirect() const { return bLastHitDirect; }
	bool WasLastHitKill() const { return bLastHitKill; }
	EMechHitKind GetLastHitKind() const { return LastHitKind; }
	/** 服务器：告诉攻击者的客户端打中了（HUD 命中提示、伤害飘字、命中确认音、击毁慢放）；导弹爆炸也走这里 */
	void ConfirmHit(AMechCharacter* Victim, bool bDirect, EMechHitKind Kind, float Damage, const FVector& At, EMechHitPart Part = EMechHitPart::Torso);
	/** 伤害飘字（HUD 画，1.2 秒后自己清掉） */
	const TArray<FMechDamagePopup>& GetDamagePopups() const { return Popups; }
	/** 测试用：服务器上子弹 / 飞刀打中各部位的次数（躯干、头、四肢） */
	int32 DebugPartHits[3] = {0, 0, 0};
	/** AI 用：近战姿态下多近开始出手（投掷匕首远很多） */
	float GetBladeRange() const { return GetBlade().Knives > 0 ? 4500.f : LightLungeRange; }
	float GetLockRange() const { return LockRange; }
	/** 用到的特效（开局预热用） */
	void GetEffectSystems(TArray<UNiagaraSystem*>& Out) const;
	/** 最近一次开枪 / 发射导弹的时间（本地，各端都有）：后坐、导弹巢抬起的动作用 */
	float GetLastShotTime() const { return LastShotTime; }
	float GetLastMissileTime() const { return LastMissileTime; }

	/** 最近一次因为烟幕锁不上的敌人（HUD 在它身上画干扰框），0.3 秒内有效 */
	AMechCharacter* GetJammedTarget() const { return GetWorld()->GetTimeSeconds() - JamTime < 0.3f ? JammedTarget.Get() : nullptr; }

	/** 准星指向的世界点（没锁定时用；玩家取镜头视线，AI 取机体正前方） */
	FVector GetFreeAimPoint() const;

	/** 服务器通知攻击者：打中了（只发给控制这台机体的那一端） */
	UFUNCTION(Client, Unreliable)
	void ClientHitConfirm(bool bDirect, bool bKill, EMechHitKind Kind, float Damage, FVector_NetQuantize At, EMechHitPart Part);

	/** 散布倍数（武器自己的散布再乘这个）；AI 设大一点 */
	UPROPERTY(EditAnywhere, Category="Mech|Rifle")
	float SpreadScale = 1.f;

	/** 框架制造商特色（10-31，AMechCharacter::ApplyLoadout 按 MechCatalog::FrameSpec 设，各端一样）：散布、弹匣、导弹锁定时间的倍数 */
	float FrameSpreadScale = 1.f;
	float FrameMagScale = 1.f;
	float FrameLockScale = 1.f;

	/** 提前量准确度：1 = 按目标当前速度完整预判，0 = 直接瞄当前位置 */
	UPROPERTY(EditAnywhere, Category="Mech|Rifle")
	float LeadAccuracy = 0.9f;
	/** AI 瞄准晃动的倍数（AI 强度定） */
	float AIAimErrorScale = 1.f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ---------------------------------------------------------------- 数值（枪和刀的在 MechWeapons，按配装取）
	UPROPERTY(EditAnywhere, Category="Mech|Missile")
	int32 MissilesPerSalvo = 4;

	UPROPERTY(EditAnywhere, Category="Mech|Missile")
	float MissileCooldown = 8.f;

	UPROPERTY(EditAnywhere, Category="Mech|Missile")
	float MissileDamage = 180.f;  // 10-30 玩家说导弹弱：170 → 180，再加 AMechMissile 里离体表 1.2 米内满额（以前近炸只吃到 0.71–0.76），每发实际 126 → 约 178（+40%）

	UPROPERTY(EditAnywhere, Category="Mech|Missile")
	float MissileImpact = 180.f;

	UPROPERTY(EditAnywhere, Category="Mech|Missile")
	float MissileRadius = 380.f;

	/** 瞄住目标多久完成导弹锁定 */
	UPROPERTY(EditAnywhere, Category="Mech|Missile")
	float MissileLockTime = 0.8f;

	/** 轻击第一下、重斩能冲过去砍的最远距离 */
	UPROPERTY(EditAnywhere, Category="Mech|Blade")
	float LightLungeRange = 1400.f;

	UPROPERTY(EditAnywhere, Category="Mech|Blade")
	float HeavyLungeRange = 2000.f;

	UPROPERTY(EditAnywhere, Category="Mech|Blade")
	float HeavyCooldown = 3.f;

	/** 左键按住多久算重斩 */
	UPROPERTY(EditAnywhere, Category="Mech|Blade")
	float HeavyHold = 0.22f;

	/** 格挡：挡住时伤害、冲击乘的系数，正面多大角度内算挡住（度），每挡一下扣的能量（基础 + 冲击 / 40） */
	UPROPERTY(EditAnywhere, Category="Mech|Blade")
	float GuardDamageScale = 0.25f;

	UPROPERTY(EditAnywhere, Category="Mech|Blade")
	float GuardImpactScale = 0.2f;

	UPROPERTY(EditAnywhere, Category="Mech|Blade")
	float GuardAngle = 75.f;

	UPROPERTY(EditAnywhere, Category="Mech|Blade")
	float GuardEnergyCost = 6.f;

	/** 拔刀 / 收刀要多久 */
	UPROPERTY(EditAnywhere, Category="Mech|Blade")
	float SwitchDuration = 0.6f;
	/** 这一次切换的时长：刀→枪快（打断后摇用），枪→刀稍慢 */
	float SwitchLen = 0.45f;
	/** 结束当前招式（正常收招、切枪打断、闪避打断都走这里） */
	void EndMelee();

	UPROPERTY(EditAnywhere, Category="Mech|Lock")
	float LockRange = 26000.f;

	/** 镜头中心多大角度内能锁上（度）；已锁定的目标放宽到 LockKeepCone */
	UPROPERTY(EditAnywhere, Category="Mech|Lock")
	float LockCone = 13.f;

	UPROPERTY(EditAnywhere, Category="Mech|Lock")
	float LockKeepCone = 24.f;

private:
	AMechCharacter* GetMech() const;
	bool CanAct() const;
	bool IsLocallyControlled() const;

	void UpdateLock(float DeltaTime);
	void UpdateGun(float DeltaTime);
	void UpdateKnives(float DeltaTime);
	/** 投掷匕首：扇形投出 Count 把 */
	void ThrowKnivesAuthority(int32 Count);
	void UpdateMissiles(float DeltaTime);
	void UpdateMelee(float DeltaTime);
	void UpdateStance(float DeltaTime);
	bool CanMelee() const;
	/** 轻击连招：空手出第一下（刚收招接着连），出招中排队到可取消时刻接下一下 */
	void LightCombo();
	void StartMove(EMechMelee Move);
	void UpdateBullets(float DeltaTime);
	void UpdateTracers();

	/** 枪口位置；Hand 0 = 右手那把，1 = 左手那把（双持） */
	FVector GetMuzzle(int32 Hand = 0) const;
	FVector ComputeAimPoint(const FVector& From, float Speed) const;
	/** 开火 / 投掷的方向：AI 打锁定目标（带提前量）；玩家打准星，只在准星贴近目标时轻微帮一下（MechWeapons::AimAssist*）。
	 *  bNoAssist：充能步枪、狙击枪开镜时不这样帮（狙击枪另有 ScopeMagnetDir） */
	FVector AssistedAimDir(const FVector& From, float Speed, bool bNoAssist) const;
	/** 狙击枪开镜吸附：Free 这条射线离哪台敌人的身体（按子弹飞到时的位置）最近、又在吸附角内，就改成指向它身上离射线最近的那一点 */
	FVector ScopeMagnetDir(const FVector& From, const FVector& Free, float Speed) const;
	/** 开一枪（霰弹一次多颗） */
	void FireShotAuthority(float InCharge);
	// ---- 充能步枪
	/** 服务器：打出光束（命中扫描）；Power 是这一发的威力（充满 1，提前松开按 MechWeapons::ChargeShotScale） */
	void FireBeamAuthority(float Power);
	/** 服务器：充能激光每 LaserTick 秒结算一次照到的敌人 */
	void LaserDamageAuthority();
	/** 充能激光现在的起点（本机看到的枪口）、方向（指向摆姿势的瞄准点——就是子弹要去的地方，别人的机体用服务器同步来的点） */
	FVector LaserDirFrom(const FVector& From) const;
	/** 激光 / 光束的画面（各端各自画，拉长的圆柱，粗细按离本机镜头的距离补偿，远处也看得见） */
	void UpdateBeams(float DeltaTime);
	void EnsureBeamMeshes();
	/** 当前招式（已按武器快慢、伤害、冲击换算） */
	struct FMove
	{
		float StrikeAt = 0.f;
		float Cancel = 0.f;
		float Duration = 0.f;
		float Damage = 0.f;
		float Impact = 0.f;
		float Step = 0.f;
	};
	FMove CurrentMove(EMechMelee Move) const;
	/** 这一招能冲多远（突进距离）：第一下轻击、重斩、闪避斩远，其余贴身 */
	float LungeRange(EMechMelee Move) const;
	/** 出刀时的速度和朝向（服务器和操控者本地各算一份）：突进有很短的加速段和砍到前的减速段、砍下后带着惯性收住，
	 *  转向是连续的快速转身（有角加速度上限、快对准时缓出） */
	void DriveMelee(const FMove& S, float Reach, float DeltaTime);
	/** 朝 WantYaw 转（度/秒上限 MaxRate），角速度 MeleeYawRate 有加速度上限 */
	void TurnToward(float WantYaw, float MaxRate, float DeltaTime, float Accel = 16000.f);
	void LaunchOneMissile();
	void MeleeStrikeAuthority();
	/** 服务器（11-01 拼刀，这台的刀砍下、够得着 Victim 时）：算不算拼刀，算就结算（两刀作废、弹开）并返回 true */
	bool TryBladeClash(AMechCharacter* Victim);
	/** 服务器（拼刀、格挡弹刀共用）：这台和 Victim 的刀都作废，弹开（匕首和别的刀拼只弹匕首方）。bGuard = Victim 是举着格挡挨的这一刀 */
	void SettleClash(AMechCharacter* Victim, bool bGuard);
	/** 拼刀弹开（服务器和操控者本地）：每帧按曲线设水平速度，僵直结束交还移动组件 */
	void UpdateClashKnock(float DeltaTime);
	float ClashKnockSpeed(float Time) const;
	/** 拼刀的场面（火花、闪光、声音）：Mid 是服务器算的两人中点，各端按自己看到的刀找交点 */
	void PlayClashFx(const FVector& Mid, AMechCharacter* Other);
	/** 打中处一盏不投影的点光闪一下（夜里很显眼），一台机体一盏，复用 */
	void FlashLight(const FVector& At, float Intensity, const FLinearColor& Color);
	UPROPERTY()
	TObjectPtr<class UPointLightComponent> ImpactLight;
	float ImpactLightLevel = 0.f;
	float ConfirmSoundTime = -100.f;
	/** 子弹打中机体的金属声：一秒最多十几下（冲锋枪连发每发都响太吵） */
	float LastMetalHitSound = -100.f;
	void AddBullet(const FVector& Origin, const FVector& Dir, bool bLive, float Speed, float Life, float Damage, float Impact, float Width, bool bKnife);
	void PlayAt(USoundBase* Sound, const FVector& At, float Volume = 1.f, float PitchJitter = 0.08f) const;

	UFUNCTION(Server, Reliable)
	void ServerSetTrigger(bool bHeld);

	UFUNCTION(Server, Reliable)
	void ServerFireMissiles(AMechCharacter* InTarget, bool bLocked);

	UFUNCTION(Server, Reliable)
	void ServerRequestMelee(AMechCharacter* InTarget, bool bHeavy);

	UFUNCTION(Server, Reliable)
	void ServerRequestDaggerSlash(AMechCharacter* InTarget);

	UFUNCTION(Server, Reliable)
	void ServerSwitchStance();

	UFUNCTION(Server, Reliable)
	void ServerSetAlt(bool bHeld);

	UFUNCTION(Server, Reliable)
	void ServerReload();

	UFUNCTION(Server, Unreliable)
	void ServerSetAim(AMechCharacter* InTarget, FVector_NetQuantize AimPoint);

	/** 一发（霰弹是一组）：各颗方向由随机种子和散布（0.1 度为单位）在各端算出来，不用逐颗发 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShot(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir, uint8 Hand, uint8 ChargeByte, uint8 SpreadTenths, int32 Seed);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastKnives(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir, uint8 Count, bool bPower, AMechCharacter* Homing);

	/** 霰弹枪吸血：这一帧回了多少（一发的几颗弹丸合在一起发一次） */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastLifesteal(float Amount);

	/** 投掷匕首近距离击退（11-01）：和普通击退一样，服务器和操控被打那台的客户端各推一次 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastKnifePush(AMechCharacter* Victim, FVector_NetQuantize100 Push);

	/** 充能步枪的光束：服务器上的枪口和打到的点（各端从自己看到的枪口画到这个点）、这一发的威力（0–255 对应 0–1，提前松开的弱一发细一些暗一些） */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBeam(FVector_NetQuantize Origin, FVector_NetQuantize End, bool bHitMech, uint8 PowerByte);

	/** 一发导弹出膛：At 是判定用的发射管位置，Tube 是第几根发射管（各端按自己看到的无人机画发射焰） */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastMissileLaunch(FVector_NetQuantize At, uint8 Tube);

	/** 出招：方向（三维，向着目标突进时可能朝上 / 朝下）、出招快慢（百分比，快刀连斩层数）、标志（闪避斩、弹反加成） */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastMeleeStart(EMechMelee Move, FVector_NetQuantizeNormal Dir, uint8 SpeedPct, uint8 Flags);

	/** 打断出招（各端一起收招，操控者本地不再往前冲） */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastInterrupt();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastStanceSwitch(EMechStance NewStance);

	/** 挡住：Kind 0 普通格挡、1 弹反、2 破防；By 是砍过来的那台（子弹打在格挡上时为空：只有火花和声音，不顿帧） */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBlocked(FVector_NetQuantize At, uint8 Kind, AMechCharacter* By);

	/** 刀砍中：bHeavy = 重斩（重击，反馈加一档）；bRefill = 这一下给双持手枪补满了弹匣（「装满」图形 + 上膛声） */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBladeImpact(FVector_NetQuantize At, float Impact, bool bHeavy, bool bRefill, AMechCharacter* Victim);

	/** 拼刀（两台各发一次）：Other 是对面那台，Flags 见 cpp 的 ClashFlag*。可靠：要带走弹开和收招的状态，掉了操控者那边就被位置纠正拉回去；又不常发 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastClash(FVector_NetQuantize At, AMechCharacter* Other, uint8 Flags);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastReload();

	// ---------------------------------------------------------------- 状态
	UPROPERTY(Replicated)
	TObjectPtr<AMechCharacter> Target;

	bool bForcedTarget = false;
	TWeakObjectPtr<AMechCharacter> JammedTarget;
	float JamTime = -100.f;
	bool bTrigger = false;
	/** 客户端：上次告诉服务器的扳机状态（不一致就再发，按下时还没拿到连接也不会丢） */
	bool bTriggerSent = false;
	float LastShotTime = -100.f;
	float LastMissileTime = -100.f;
	float MissileLock = 0.f;
	/** 导弹锁定威胁（0–255，16 档，少发包），见 GetLockThreat */
	UPROPERTY(Replicated)
	uint8 RepLockThreat = 0;
	UPROPERTY(Replicated)
	int32 Ammo = 36;
	/** 当前武器（配装复制下来后各端自己设） */
	EMechShooter GunType = EMechShooter::Rifle;
	EMechMeleeWeapon BladeType = EMechMeleeWeapon::Katana;
	// ---- 枪的特色
	UPROPERTY(Replicated)
	float Heat = 0.f;
	UPROPERTY(Replicated)
	float OverheatTimer = 0.f;
	UPROPERTY(Replicated)
	float Charge = 0.f;
	bool bCharging = false;
	/** 充能激光结算伤害的计时（服务器） */
	float LaserTickTimer = 0.f;
	/** 本机上一帧的充能（判断刚开始充能，放充能声） */
	float PrevVisualCharge = 0.f;
	float LaserSparkTimer = 0.f;
	/** 最近一道光束：打出的时间、落点（各端） */
	float BeamTime = -100.f;
	FVector BeamTo = FVector::ZeroVector;
	/** 霰弹枪吸血：这一帧攒下的回复量（Tick 里合成一次组播） */
	float PendingLifesteal = 0.f;
	/** 光束这一发的威力（充能步枪提前松开时小于 1：光束细一些、暗一些） */
	float BeamPower = 1.f;
	/** 双持手枪重斩补满、重斩砍中的时间（HUD 用） */
	float LastRefillTime = -100.f;
	float LastHeavyHitTime = -100.f;
	/** 这一刀已经补过弹匣了（一刀砍中好几台也只补一次；MeleeStrikeAuthority 结束时清掉） */
	bool bRefilledThisSwing = false;
	/** 这一刀已经砍中过人（服务器）：刀过去了，窗口内也不再算拼刀 */
	bool bSwingLanded = false;
	/** 拼刀：HUD 反馈的时间；和匕首拼上没被弹开时这一招改到这个时刻可取消（小于 0 = 不改）；弹开的方向、距离、走了多久（小于 0 = 没在弹开） */
	float ClashFxTime = -100.f;
	float ClashCancelAt = -1.f;
	FVector ClashKnockDir = FVector::ZeroVector;
	float ClashKnockDist = 0.f;
	float ClashKnockClock = -1.f;
	/** 这一帧移动组件已经走过的子步时间（战斗组件的 Tick 在移动之后，每帧清零） */
	float ClashKnockAccum = 0.f;
	/** 服务器：还有多久恢复对这台操控者的位置校验（见 MechWeapons::ClashNetTrust） */
	float ClashTrustTimer = 0.f;
	/** 弹开从哪儿开始（日志量走了多远，服务器和操控者本地各记） */
	FVector ClashKnockFrom = FVector::ZeroVector;
	/** 最近一次快速闪避的时间（单手刀闪避斩；服务器和操控者本地各记） */
	float LastDodgeTime = -100.f;
	// ---- 刀的机制
	/** 化境（服务器）：连续打中的次数、上一次打中人的时刻、这一刀开始时定的倍数 */
	int32 HuajingHits = 0;
	double HuajingLastHit = -100.0;
	float HuajingSwingMult = 1.f;
	/** 这一招的出招快慢倍数（快刀连斩层数）和标志（MoveFlag*），出招时由服务器定、组播给各端 */
	float MoveTimeScale = 1.f;
	uint8 MoveFlags = 0;
	/** 被砍的一方：这一刀是钝器（打在格挡上破防）、是谁砍的（格挡反馈要让双方都有）——砍的一方在调 ReceiveHit 前后设 / 清 */
	bool bIncomingGuardBreak = false;
	TWeakObjectPtr<AMechCharacter> IncomingAttacker;
	/** TryBlock 刚挡住了这一刀（砍的一方据此不再放命中特效） */
	bool bLastIncomingBlocked = false;
	/** 服务器：格挡弹刀后一直按着右键——僵直结束格挡自己重新举起时不算「刚按下」，不白送一个弹反窗口，松开重按才算 */
	bool bNoParryUntilRelease = false;
	/** 格挡反馈（HUD）：时间、种类、自己是不是挡的一方 */
	float BlockFxTime = -100.f;
	uint8 BlockFxKind = 0;
	bool bBlockFxDefender = false;
	UPROPERTY(Replicated)
	int32 Streak = 0;
	TWeakObjectPtr<AMechCharacter> StreakTarget;
	// ---- 保卫战肉鸽（11-01，MechRun.h）：对战里都不变
	/** 这台开的枪有什么规则（第 0 位：穿透杂兵）：各端画子弹要知道 */
	UPROPERTY(Replicated)
	uint8 RunShotFlags = 0;
	/** 蓄力中：停在重斩起手（见 GetRunChargeHold） */
	UPROPERTY(Replicated)
	uint8 RunChargeHold = 0;
	/** 服务器：下一次重斩要停住蓄力；蓄力开始的时间；这一刀蓄了几成（松手时定，砍下去用掉） */
	bool bRunChargeWanted = false;
	float RunChargeT0 = 0.f;
	float RunChargeLevel = 0.f;
	/** 操控端：这次按住走的是蓄力（松手要告诉服务器） */
	bool bRunChargePressed = false;
	/** 各端：蓄了多久（画光效） */
	float RunChargeClock = 0.f;
	/** 这一轮导弹的枚数（0 = 照常） */
	int32 RunSalvo = 0;
	/** 连中的目标（可能是杂兵） */
	TWeakObjectPtr<AActor> RunStreakActor;
	/** 不算肉鸽卡的一轮枚数 */
	int32 BaseSalvo() const;
	float StreakTime = -100.f;
	int32 ShotHand = 0;
	float LastHealTime = -100.f;
	float LastThrowTime = -100.f;
	/** 两次投掷之间的最短间隔 */
	float ThrowCooldown = 0.f;
	// ---- 投掷匕首
	UPROPERTY(Replicated)
	int32 Knives = 0;
	UPROPERTY(Replicated)
	int32 KnifeStreak = 0;
	UPROPERTY(Replicated)
	float KnifeTimer = 0.f;
	float FireTimer = 0.f;
	UPROPERTY(Replicated)
	float ReloadTimer = 0.f;
	UPROPERTY(Replicated)
	float MissileTimer = 0.f;
	int32 PendingMissiles = 0;
	float MissileSalvoTimer = 0.f;
	bool bSalvoLocked = false;
	// ---- 姿态
	UPROPERTY(Replicated)
	EMechStance Stance = EMechStance::Gun;
	EMechStance SwitchFrom = EMechStance::Gun;
	float SwitchTime = -1.f;
	/** 右键按住（本地输入；服务器从 RPC 收到） */
	bool bAltHeld = false;
	UPROPERTY(Replicated)
	bool bAiming = false;
	UPROPERTY(Replicated)
	bool bGuarding = false;
	float GuardTime = 0.f;
	float SinceGuardEnd = 10.f;
	float SinceBlock = 10.f;
	float GuardBreakTimer = 0.f;
	// ---- 近战
	EMechMelee MeleeMove = EMechMelee::None;
	float MeleeTime = 0.f;
	bool bMeleeHitDone = false;
	/** 连招里按了下一下：到可取消时刻接上 */
	bool bQueuedLight = false;
	/** 上一招刚结束，在这段时间里再按轻击会接着连 */
	float ComboWindow = 0.f;
	EMechMelee LastLight = EMechMelee::None;
	UPROPERTY(Replicated)
	float HeavyTimer = 0.f;
	/** 本地：左键按住的时长，过了 HeavyHold 就出重斩 */
	bool bMeleeHeld = false;
	bool bHeavyFired = false;
	float MeleeHold = 0.f;
	FVector LungeDir = FVector::ForwardVector;
	/** 出刀转身的角速度（度/秒，带符号）；这一招第一次设突进速度时的招式时间（速度曲线从这里算起） */
	float MeleeYawRate = 0.f;
	float MeleeDriveT0 = 0.f;
	float LastEngagedTime = -100.f;
	float LastHitTime = -100.f;
	bool bLastHitDirect = false;
	bool bLastHitKill = false;
	EMechHitKind LastHitKind = EMechHitKind::Bullet;
	/** 轻击已经砍到时按住转重斩：到可取消时刻接重斩 */
	bool bQueuedHeavy = false;
	FVector AimDir = FVector::ForwardVector;
	FVector PoseAimDir = FVector::ForwardVector;
	FVector PoseAimPoint = FVector::ZeroVector;
	FVector RemoteAimPoint = FVector::ZeroVector;
	/** 服务器算好的摆姿势瞄准点，同步给别的客户端（操控者自己不要） */
	UPROPERTY(Replicated)
	FVector_NetQuantize RepAimPoint = FVector::ZeroVector;
	float MuzzleFlash = 0.f;
	TArray<FMechBullet> Bullets;
	TArray<FMechDamagePopup> Popups;

	// ---------------------------------------------------------------- 表现
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Tracers;

	/** 飞行中的匕首 */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> KnifeMeshes;

	/** 飞刀的尾迹（10-31 近战特效）：普通的冷白、强力匕首的橙色粗一号（各一组拉长的发光圆柱，同曳光） */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> KnifeStreaks;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> PowerStreaks;

	/** 近战特效（刀光、刀刃热光、刀痕、冲击环；MechMeleeFx.h）。专用服务器上没有；-MechMeleeFxOld 时也没有（用以前的粒子刀光） */
	UPROPERTY()
	TObjectPtr<UMechMeleeFx> MeleeFx;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> MuzzleLight;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> BladeTrail;

	/** 双刀：左手那把的刀光 */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> BladeTrail2;

	UPROPERTY()
	TObjectPtr<UStaticMesh> KnifeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> TracerMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> TracerMaterial;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> MuzzleFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> HitMechFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> HitWorldFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> BladeTrailFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> BladeHitFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> MissileLaunchFX;

	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> RifleSounds;

	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> MetalHitSounds;

	UPROPERTY()
	TObjectPtr<USoundBase> GroundHitSound;

	/** 命中确认（只在开枪 / 挥刀的玩家自己耳边）：低沉的一声闷响，击毁时更重 */
	UPROPERTY()
	TObjectPtr<USoundBase> ThumpSound;

	UPROPERTY()
	TObjectPtr<USoundBase> ReloadSound;

	UPROPERTY()
	TObjectPtr<USoundBase> MissileLaunchSound;

	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> SwingSounds;

	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> BladeHitSounds;

	UPROPERTY()
	TObjectPtr<USoundAttenuation> Attenuation;

	// ---- 10-30 加的：格挡 / 弹反 / 破防的声音（音效全集里挑的，tools/ue_import_combat_fx.py 导入），充能步枪的声音和光束
	UPROPERTY()
	TObjectPtr<USoundBase> GuardBlockSound;

	UPROPERTY()
	TObjectPtr<USoundBase> ParryTingSound;

	UPROPERTY()
	TObjectPtr<USoundBase> ParryRingSound;

	UPROPERTY()
	TObjectPtr<USoundBase> GuardBreakSound;

	UPROPERTY()
	TObjectPtr<USoundBase> ChargeSound;

	UPROPERTY()
	TObjectPtr<USoundBase> BeamSound;

	/** 重斩砍中垫的一声厚重的低频（小爆炸声压低音调） */
	UPROPERTY()
	TObjectPtr<USoundBase> HeavyHitSound;

	/** 光束材质（叠加、不受光、白天补偿，参数 Color / Intensity）；没导入时退回曳光材质 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BeamMaterial;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> LaserMeshes;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> BeamCoreMeshes;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> BeamGlowMeshes;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> LaserMID;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> BeamCoreMID;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> BeamGlowMID;
};

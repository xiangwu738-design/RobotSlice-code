// 肩部装备「地雷」的本体（10-31 做的时候叫战术装备）。和烟雾弹 / EMP 弹一样：服务器生成，复制出生位置、初速度、出生时间，各端按同一条抛物线自己算位置；
// 只有服务器判落地、判触发、结算。
//  飞行 → 落地（贴在地面上，灯慢闪，ArmTime 秒后待命）→ 待命（灯短促一闪一闪）→ 敌人进 TriggerRadius → 引爆倒计时（灯快闪、嘀嘀响、
//  地上亮出爆炸范围圈）→ FuseTime 秒后炸：BlastRadius 内的敌人受伤害（中心最高、边上四成）和冲击（击退走机体现有的受击击退）。
// 反制（PvP 可读性）：灯和队伍色谁都看得见；能被子弹打掉（MaxHP，敌方子弹才算）、被敌人的震荡波扫掉；
// 引爆前 0.35 秒有范围圈和嘀嘀声，快速闪避能出圈；离地 3.5 米以上飞过去不会触发。
// 打得中：根组件是一个小球，物体类型 WorldDynamic、只做查询、对所有通道忽略——子弹是按物体类型查的（会打中它），
// 机体走路、镜头、AI 视线、导弹都按通道查（穿过去，不挡路）。子弹打中后战斗组件按「MechShootable」标签把伤害交给 TakeDamage。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechMine.generated.h"

class AMechCharacter;
class USphereComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UPointLightComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraSystem;
class USoundBase;
class USoundAttenuation;

UENUM()
enum class EMechMinePhase : uint8
{
	/** 抛出去还在飞 */
	Flying,
	/** 落地了（落地 ArmTime 秒后才待命） */
	Landed,
	/** 有敌人进圈，FuseTime 秒后炸 */
	Triggered,
	/** 炸了 / 碎了，等着删 */
	Done
};

UCLASS()
class AMechMine : public AActor
{
	GENERATED_BODY()

public:
	AMechMine();

	/** 服务器上、FinishSpawning 之前：谁扔的、从哪扔、初速度 */
	void Init(AMechCharacter* InOwner, const FVector& Origin, const FVector& Velocity);
	/** 服务器：不炸、直接碎掉（被打掉、被震荡波扫掉、超时、主人不在了、回合结束）；bEffects 时冒火花响一声 */
	void Break(bool bEffects);

	AMechCharacter* GetOwnerMech() const { return OwnerMech; }
	int32 GetTeam() const { return Team; }
	/** 还在场上（飞行 / 落地 / 引爆倒计时）；炸了或碎了为假 */
	bool IsActive() const { return Phase != EMechMinePhase::Done; }
	/** 待命中（落地满 ArmTime 秒，会被触发） */
	bool IsArmed() const;
	/** 扔出去的服务器时间（同一台放满时先碎最早那颗） */
	float GetLaunchTime() const { return LaunchTime; }

	void GetEffectSystems(TArray<UNiagaraSystem*>& Out) const;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 被打（服务器；子弹由战斗组件按 MechShootable 标签转过来）：自己人打不掉，敌人打满 MaxHP 就碎 */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

	// ---------------------------------------------------------------- 数值
	/** 飞行重力（厘米/秒²；扔的初速度按它算） */
	static constexpr float Gravity = 1400.f;
	/** 落地后多久待命 */
	static constexpr float ArmTime = 1.f;
	/** 敌人离雷多近触发（到机体胶囊表面的距离） */
	static constexpr float TriggerRadius = 350.f;
	/** 触发后多久炸 */
	static constexpr float FuseTime = 0.35f;
	/** 爆炸半径、中心伤害（边上四成）、冲击 */
	static constexpr float BlastRadius = 500.f;
	static constexpr float BlastDamage = 900.f;
	static constexpr float BlastImpact = 700.f;
	/** 落地后最多留多久（秒） */
	static constexpr float LifeTime = 40.f;
	/** 被敌人子弹打多少就碎（步枪两发、狙击一发） */
	static constexpr float MaxHP = 150.f;

protected:
	virtual void BeginPlay() override;

private:
	float ServerNow() const;
	FVector PosAt(float T) const;
	FVector VelAt(float T) const;
	/** 服务器：落在 At（地面法线 Normal） */
	void Land(const FVector& At, const FVector& Normal);
	/** 服务器：炸 */
	void Explode();
	/** 敌人到这一点的距离（到胶囊中线再减半径） */
	static float DistToMech(const AMechCharacter* M, const FVector& P);
	bool HasLineTo(const AMechCharacter* M, const FVector& From) const;
	/** 灯（闪法按阶段）、引爆预警圈、落地后摆到位置（各端） */
	void UpdateVisuals();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastExplode(FVector_NetQuantize At);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastBreak(FVector_NetQuantize At);

	UFUNCTION()
	void OnRep_Phase();

	UPROPERTY(Replicated)
	TObjectPtr<AMechCharacter> OwnerMech;

	UPROPERTY(Replicated)
	int32 Team = 0;

	UPROPERTY(Replicated)
	FVector_NetQuantize LaunchPos = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FVector_NetQuantize10 LaunchVel = FVector::ZeroVector;

	UPROPERTY(Replicated)
	float LaunchTime = 0.f;

	UPROPERTY(ReplicatedUsing=OnRep_Phase)
	EMechMinePhase Phase = EMechMinePhase::Flying;

	/** 这个阶段开始的服务器时间 */
	UPROPERTY(Replicated)
	float PhaseTime = 0.f;

	/** 落地的位置和地面法线 */
	UPROPERTY(Replicated)
	FVector_NetQuantize RestPos = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FVector_NetQuantizeNormal RestNormal = FVector::UpVector;

	float PrevT = 0.f;
	float HP = MaxHP;
	/** 本地：飞行时画面上已经碰到地面（等服务器的落地位置），先停在这 */
	bool bVisualStuck = false;
	/** 本地：引爆倒计时嘀嘀声上一次响的时间、待命那一下响过没有 */
	float LastBeep = -100.f;
	bool bArmBeeped = false;
	/** 本地：爆炸范围圈开始扩散的时间（炸开后的画面） */
	float BlastStart = -100.f;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> HitBox;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Body;

	/** 雷顶上的小灯（叠加发光的小球，队伍色，白天也看得见） */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Beacon;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BeaconMID;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BodyGlowMID;

	/** 引爆预警 / 爆炸的范围圈（MechRangeRing：一圈细圆柱，光束材质） */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Ring;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> RingMID;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> RingMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BeaconMaterial;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> BlastFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> SparkFX;

	UPROPERTY()
	TObjectPtr<USoundBase> BeepSound;

	UPROPERTY()
	TObjectPtr<USoundBase> BlastSound;

	UPROPERTY()
	TObjectPtr<USoundBase> BreakSound;

	UPROPERTY()
	TObjectPtr<USoundAttenuation> Attenuation;
};

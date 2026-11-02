// 保卫战的杂兵（10-31 用户：「大部分敌人的攻击方式应该非常简单，敌方机甲更多作为首领登场」）。每种只有一招，看得懂、躲得开：
//   自爆无人机：直线冲向最近的人（附近没人就冲指挥所），贴身后停住、脚下出红圈，1 秒后爆炸；红圈期间打掉它就不炸
//   机枪无人机：在目标 30–50 米外悬停、绕圈，隔 3 秒打一串 5 发慢速曳光弹（看得见，侧闪能躲）
//   履带炮车：地面慢慢开向指挥所（35 米内有人就打人），停下来炮管指着目标、拉一道红色瞄准线 1.5 秒，再打一发慢炮弹（落地小范围爆炸）
//   迫击炮：在远处不动，隔 7–9 秒往指挥所附近（或指挥所旁的人）砸一发，落点先出 2 秒红圈
// 轻量：没有骨骼动画、没有角色移动，一个碰撞球 + 几块占位模型 + 一个状态机。服务器算，位置按 15 次/秒同步，客户端平滑追过去。
// 被打：碰撞球照地雷的做法（物体类型 WorldDynamic、只查询），子弹按「MechShootable」标签转给 TakeDamage；刀、导弹在战斗组件 / 导弹里补了判定。
// 炮车的碰撞球挡机体（不然机体从车里穿过去）；无人机不挡。子弹、炮弹、红圈由 AMechDefenseDirector 统一管。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechGrunt.generated.h"

class AMechCharacter;
class AMechDefenseDirector;
class UMaterialInstanceDynamic;
class UNiagaraSystem;
class USphereComponent;
class UStaticMeshComponent;
class USoundBase;

UENUM()
enum class EMechGrunt : uint8
{
	Kamikaze,  // 自爆无人机
	Gunner,    // 机枪无人机
	Tank,      // 履带炮车
	Mortar     // 迫击炮
};

UCLASS()
class AMechGrunt : public AActor
{
	GENERATED_BODY()

public:
	AMechGrunt();
	virtual void Tick(float DeltaTime) override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 服务器：生成后马上调（SpawnActorDeferred 和 FinishSpawning 之间） */
	void Init(EMechGrunt InKind, AMechDefenseDirector* InDirector, float HPScale);
	EMechGrunt GetKind() const { return Kind; }
	bool IsAlive() const { return !bDead; }
	float GetHPFraction() const { return MaxHP > 0.f ? HP / MaxHP : 0.f; }
	/** 测试用：直接打死（不算谁打的） */
	void DebugKill();
	/** 肉鸽 重弹 2（11-01）：杂兵没有失衡条，瘫痪几秒：不动、不开火，红灯快闪（服务器） */
	void RunStun(float Seconds);
	/** 测试日志：离目标多远、目标是谁、状态、离地多高 */
	FString DebugInfo() const;

	/** 每种的血量（乘波次 / 人数倍率前） */
	static float BaseHP(EMechGrunt K);
	static const TCHAR* KindName(EMechGrunt K);

private:
	UPROPERTY(ReplicatedUsing = OnRep_Kind)
	EMechGrunt Kind = EMechGrunt::Kamikaze;
	UFUNCTION()
	void OnRep_Kind();
	/** 按种类摆占位模型、碰撞（两端都调） */
	void BuildVisuals();
	bool bBuilt = false;

	// ---- 同步给客户端的状态
	UPROPERTY(Replicated)
	FVector_NetQuantize RepPos;
	UPROPERTY(Replicated)
	FVector_NetQuantize RepVel;
	UPROPERTY(Replicated)
	float RepYaw = 0.f;
	/** 炮塔 / 炮管朝向（炮车、迫击炮） */
	UPROPERTY(Replicated)
	float RepAimYaw = 0.f;
	UPROPERTY(Replicated)
	float HP = 100.f;
	UPROPERTY(Replicated)
	float MaxHP = 100.f;
	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;
	UFUNCTION()
	void OnRep_Dead();
	/** 0 平常，1 自爆无人机红圈倒数中 / 炮车瞄准中 */
	UPROPERTY(Replicated)
	uint8 RepState = 0;
	/** 炮车瞄准线的终点 */
	UPROPERTY(Replicated)
	FVector_NetQuantize RepAimPoint;

	// ---- 服务器行为
	TWeakObjectPtr<AMechDefenseDirector> Director;
	TWeakObjectPtr<AActor> Target;
	float RetargetClock = 0.f;
	float StateClock = 0.f;
	float FireClock = 0.f;
	int32 BurstLeft = 0;
	float BurstClock = 0.f;
	float OrbitAngle = 0.f;
	float GroundZ = 0.f;
	float GroundClock = 0.f;
	float AvoidClock = 0.f;
	float DetourTime = 0.f;
	float DetourSign = 1.f;
	FVector StuckFrom = FVector::ZeroVector;
	float StuckClock = 0.f;
	float RunStunTimer = 0.f;
	FVector Vel = FVector::ZeroVector;
	void ServerTick(float DeltaTime);
	void PickTarget();
	FVector TargetPoint() const;
	float GroundAt(const FVector& P) const;
	void TickKamikaze(float DeltaTime);
	void TickGunner(float DeltaTime);
	void TickTank(float DeltaTime);
	void TickMortar(float DeltaTime);
	/** 死（bKiller 为空 = 自爆 / 测试打死） */
	void Die(AActor* Killer, bool bExploded);

	// ---- 客户端
	void ClientSmooth(float DeltaTime);
	void UpdateVisualState(float DeltaTime);
	float BlinkClock = 0.f;
	/** 红圈开始涨的时间（只用于画面；服务器的倒数在 StateClock，别混用——第一版混用了，自爆无人机永远不炸） */
	float RingStart = 0.f;

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastDeathFX(FVector_NetQuantize At, bool bBig);

	// ---- 组件 / 资源
	UPROPERTY()
	TObjectPtr<USphereComponent> HitBox;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Turret;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Barrel;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Eye;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Laser;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Ring;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> EyeMID;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> RingMID;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaneMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> MissileMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> PodMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> MetalMat;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMat;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> RingMat;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> ExplosionFX;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> SmallExplosionFX;
	UPROPERTY()
	TObjectPtr<USoundBase> ExplosionSound;
};

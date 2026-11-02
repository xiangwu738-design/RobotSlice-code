// 肩部导弹：先向上斜飞一小段，然后加速追踪锁定的目标（转向能力有限，横向快速闪避能甩掉）。
// 撞到东西、贴近目标或飞太久就爆炸，范围内的敌方机体按距离吃伤害和冲击。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechMissile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class UPointLightComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;
class USoundAttenuation;
class AMechCharacter;

UCLASS()
class AMechMissile : public AActor
{
	GENERATED_BODY()

public:
	AMechMissile();

	/** 发射前设置（服务器上） */
	void Init(AMechCharacter* InShooter, AMechCharacter* InTarget, float InDamage, float InImpact, float InRadius);

	/** 生成后给初速度 */
	void Launch(const FVector& Velocity);

	AMechCharacter* GetTarget() const { return Target; }
	// ---- 保卫战肉鸽（11-01，MechRun.h）：发射时由 UMechRunComponent 设；对战里全是默认值，什么都不做
	/** 追一个杂兵（Target 只能是机体） */
	TWeakObjectPtr<AActor> RunTarget;
	/** 爆炸打机甲的失衡倍数（< 0 = 照常）：重弹 2 */
	float RunStagger = -1.f;
	/** 分裂出来的小导弹（不再分裂） */
	bool bRunChild = false;
	/** 这么多秒后自爆（0 = 照常）：小导弹没目标时别飞 7 秒 */
	float RunLife = 0.f;
	float GetDamage() const { return Damage; }
	float GetImpact() const { return Impact; }
	float GetRadius() const { return Radius; }
	int32 GetTeam() const { return Team; }
	/** 已经炸了（还要 0.3 秒才销毁；HUD 告警不再把它算作来袭） */
	bool IsExploded() const { return bExploded; }
	/** 用到的特效（开局预热用） */
	void GetEffectSystems(TArray<UNiagaraSystem*>& Out) const;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& Sweep);

	void Explode(const FVector& At);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastExplodeFX(FVector_NetQuantize At);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProjectileMovementComponent> Movement;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Glow;

	/** 尾部喷焰（发光锥体） */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Flame;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> ExplosionFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> DustFX;

	UPROPERTY()
	TObjectPtr<USoundBase> ExplosionSound;

	UPROPERTY()
	TObjectPtr<USoundAttenuation> Attenuation;

	UPROPERTY(Replicated)
	TObjectPtr<AMechCharacter> Target;

	UPROPERTY()
	TObjectPtr<AMechCharacter> Shooter;

	float Damage = 240.f;
	float Impact = 220.f;
	float Radius = 380.f;
	int32 Team = 0;
	float Age = 0.f;
	bool bExploded = false;
	/** 刚出现时画面上从本机看到的无人机发射管出（判定位置是服务器按无人机理想位置算的），0.2 秒内并回判定位置：
	 *  模型、尾焰、灯整体偏这么多（世界坐标），按 UMechShoulderComponent::LaunchVisualFade 收掉 */
	FVector VisOffset = FVector::ZeroVector;
	float VisAge = 0.f;
	FVector BaseRel[3];
	void ApplyVisOffset();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

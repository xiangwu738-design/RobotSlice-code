// 捕获网（肩部武器，UMechShoulderComponent 发射）：服务器生成，复制出生位置、初速度、出生时间，各端按同一条抛物线自己算位置
// （不靠位置同步，飞起来不会一顿一顿）。飞出去 0.3 秒内从一团张开到全尺寸；服务器每帧看扫过的范围里有没有敌方机体（按张开的半径算，
// 网大，比子弹好中），有就罩住；撞到地形就掉在那，过一会儿消失。
// 罩住后换成裹住机体的网，跟着对方走，网上通电（火花、结点闪），对方挣脱后破网消失。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechNet.generated.h"

class AMechCharacter;
class UStaticMeshComponent;
class UStaticMesh;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UNiagaraSystem;
class USoundBase;
class USoundAttenuation;

UENUM()
enum class EMechNetState : uint8
{
	Flying,
	/** 没罩住人，掉在地上 */
	Dropped,
	/** 罩住了一台机体 */
	Wrapped,
	/** 对方挣脱（或被击毁），破网 */
	Broken
};

UCLASS()
class AMechNet : public AActor
{
	GENERATED_BODY()

public:
	AMechNet();

	/** 服务器上、FinishSpawning 之前：谁发射的、从哪、初速度 */
	void Init(AMechCharacter* InShooter, const FVector& Origin, const FVector& Velocity);
	/** 服务器：网住的人挣脱了 */
	void Break();

	AMechCharacter* GetVictim() const { return Victim; }
	EMechNetState GetState() const { return State; }
	void GetEffectSystems(TArray<UNiagaraSystem*>& Out) const;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 张开后的半径（厘米）；扫中机体时用它的八成 */
	static constexpr float OpenRadius = 220.f;
	/** 最多飞多久（之后不再能罩人，掉下去） */
	static constexpr float MaxFlight = 1.1f;

protected:
	virtual void BeginPlay() override;

private:
	float ServerNow() const;
	FVector PosAt(float T) const;
	FVector VelAt(float T) const;
	/** 张开程度 0–1 */
	static float OpenAt(float T);
	void Catch(AMechCharacter* Mech, const FVector& At);
	void Drop(const FVector& At, const FVector& Normal);
	/** 进入一个状态的表现（服务器直接调，客户端在复制回来时调） */
	void EnterState();

	UFUNCTION()
	void OnRep_State();

	UPROPERTY(ReplicatedUsing=OnRep_State)
	EMechNetState State = EMechNetState::Flying;

	UPROPERTY(Replicated)
	FVector_NetQuantize LaunchPos = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FVector_NetQuantize10 LaunchVel = FVector::ZeroVector;

	UPROPERTY(Replicated)
	float LaunchTime = 0.f;

	UPROPERTY(Replicated)
	int32 Team = 0;

	UPROPERTY(Replicated)
	TObjectPtr<AMechCharacter> Shooter;

	UPROPERTY(Replicated)
	TObjectPtr<AMechCharacter> Victim;

	/** 掉在哪、地面朝向、掉下时张开了多少 */
	UPROPERTY(Replicated)
	FVector_NetQuantize DropPos = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FVector_NetQuantizeNormal DropNormal = FVector::UpVector;

	UPROPERTY(Replicated)
	float DropOpen = 1.f;

	float PrevT = 0.f;
	/** 刚出现时画面上从本机看到的无人机发射口出（判定弹道起点是无人机理想位置），0.2 秒内并回判定弹道 */
	FVector VisOffset = FVector::ZeroVector;
	/** 本地：进入当前状态的时间 */
	float StateStart = 0.f;
	float SparkTimer = 0.f;
	bool bEntered = false;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UAudioComponent> ShockAudio;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GlowMID;

	UPROPERTY()
	TObjectPtr<UStaticMesh> OpenMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> WrapMesh;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> SparkFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> CatchFX;

	UPROPERTY()
	TObjectPtr<USoundBase> FlySound;

	UPROPERTY()
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY()
	TObjectPtr<USoundBase> BreakSound;

	UPROPERTY()
	TObjectPtr<USoundAttenuation> Attenuation;
};

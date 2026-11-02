// 彩蛋的无人机 / 机械狗（11-02，总说明见 MechEaster.h）。
// 一个 actor 一台：根是打得中的球（照保卫战杂兵：WorldDynamic、只查询，标签 MechShootable + MechGrunt，子弹 / 刀 / 导弹爆炸按 ApplyDamage 交给 TakeDamage），
// 画面挂在 Visual 下面（循环地图上画在离镜头最近的那一份），涂装按复制下来的种子各端自己刷（同一个种子各端一样）。
// 服务器算行为，位置按 15 次/秒同步、客户端平滑追；老街的狗群不同步位置：起跑时间、起点、方向复制一次，各端按服务器时间自己算（越跑越快也不卡）。
// 无人机：旋翼网格挂在机身插槽上，代码每帧转；机身随速度前倾。
// 机械狗：有动作片段（A_PDog_Idle/Walk/Run/Sit）就播片段；没有就用可摆姿势的网格按骨头名摆一个简单的步态（四条腿对角同步、头歪、尾巴天线摇）。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechEaster.h"
#include "MechEasterBot.generated.h"

class AMechEasterDirector;
class UAnimSequence;
class UMaterialInstanceDynamic;
class UMeshComponent;
class UPoseableMeshComponent;
class USceneComponent;
class USkeletalMeshComponent;
class USphereComponent;
class UStaticMeshComponent;

UCLASS(NotPlaceable)
class AMechEasterBot : public AActor
{
	GENERATED_BODY()

public:
	AMechEasterBot();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 服务器：生成后马上调（SpawnActorDeferred 和 FinishSpawning 之间） */
	void InitBot(EMechEasterKind InKind, EMechEasterRole InRole, EMechEasterWeapon InWeapon, int32 InSeed, AMechEasterDirector* InDirector);
	/** 服务器：狗群的跑法（起跑的服务器时间、起点（脚底）、水平方向、左右摆动的相位） */
	void InitStampede(float StartTime, const FVector& Origin, const FVector& Dir, float WeavePhase);
	/** 服务器：无人机在场多久（之后飞走） */
	void SetLife(float Seconds) { LifeLeft = Seconds; }

	EMechEasterKind GetKind() const { return Kind; }
	EMechEasterRole GetBotRole() const { return BotRole; }
	bool IsAlive() const { return !bDead && !bGone; }
	bool IsHostile() const { return BotRole != EMechEasterRole::Friendly; }
	float GetHPFraction() const { return MaxHP > 0.f ? HP / MaxHP : 0.f; }
	/** HUD：画面上的位置（循环地图上是离镜头最近的那一份） */
	FVector GetVisualCenter() const;
	/** 狗群：现在的速度（厘米/秒） */
	float StampedeSpeed(float Now) const;
	/** 测试 / 清场：直接拿掉（不放爆炸） */
	void Remove();
	FString DebugInfo() const;

private:
	// ---- 复制
	UPROPERTY(ReplicatedUsing = OnRep_Kind)
	EMechEasterKind Kind = EMechEasterKind::Drone;
	/** 不叫 Role：AActor 已经有一个（网络角色） */
	UPROPERTY(Replicated)
	EMechEasterRole BotRole = EMechEasterRole::Hostile;
	UPROPERTY(Replicated)
	EMechEasterWeapon Weapon = EMechEasterWeapon::Gun;
	UPROPERTY(Replicated)
	int32 Seed = 0;
	UFUNCTION()
	void OnRep_Kind();

	UPROPERTY(Replicated)
	FVector_NetQuantize RepPos;
	UPROPERTY(Replicated)
	FVector_NetQuantize RepVel;
	UPROPERTY(Replicated)
	float RepYaw = 0.f;
	/** 0 站着、1 走、2 跑、3 蹲 / 坐、4 滑滑梯、5 爬梯子 */
	UPROPERTY(Replicated)
	uint8 RepAnim = 0;
	UPROPERTY(Replicated)
	float HP = 100.f;
	UPROPERTY(Replicated)
	float MaxHP = 100.f;
	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;
	UFUNCTION()
	void OnRep_Dead();

	// 狗群（只在开跑时复制一次）
	UPROPERTY(Replicated)
	float RunStart = 0.f;
	UPROPERTY(Replicated)
	FVector_NetQuantize RunOrigin;
	UPROPERTY(Replicated)
	FVector_NetQuantizeNormal RunDir;
	UPROPERTY(Replicated)
	float RunWeave = 0.f;

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastDeathFX(FVector_NetQuantize At);

	// ---- 组件
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> HitBox;
	/** 循环地图：±周期 处各一个一样的球（打中它 = 打中这台） */
	UPROPERTY()
	TArray<TObjectPtr<USphereComponent>> MirrorBoxes;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Visual;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Rotors;
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> DogAnimMesh;
	UPROPERTY()
	TObjectPtr<UPoseableMeshComponent> DogPoseMesh;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Placeholder;
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> LiveryMIDs;
	UPROPERTY()
	TObjectPtr<UAnimSequence> AnimIdle;
	UPROPERTY()
	TObjectPtr<UAnimSequence> AnimWalk;
	UPROPERTY()
	TObjectPtr<UAnimSequence> AnimRun;
	UPROPERTY()
	TObjectPtr<UAnimSequence> AnimSit;

	bool bBuilt = false;
	void BuildVisuals();
	void BuildDrone();
	void BuildDog();
	void BuildPlaceholder();
	void ApplyLivery(UMeshComponent* C);
	void SetupMirrors();
	/** 画面：旋翼、前倾、腿、涂装发光（各端） */
	void UpdateVisuals(float DeltaTime);
	void UpdateDogPose(float DeltaTime, float Speed);
	void PlayDogAnim(uint8 Anim, float Speed);
	uint8 PlayingAnim = 255;
	float VisClock = 0.f;
	float GaitPhase = 0.f;
	FVector VisVel = FVector::ZeroVector;
	/** 可摆姿势网格：每根骨头参考姿势的组件空间旋转（按名字） */
	TMap<FName, FQuat> RefRot;
	bool bGone = false;

	// ---- 行为（服务器）
	TWeakObjectPtr<AMechEasterDirector> Director;
	TWeakObjectPtr<AActor> Target;
	FVector Vel = FVector::ZeroVector;
	float LifeLeft = 0.f;
	float RetargetClock = 0.f;
	float FireClock = 0.f;
	int32 BurstLeft = 0;
	float BurstClock = 0.f;
	float OrbitAngle = 0.f;
	float OrbitRadius = 3000.f;
	float OrbitHeight = 1500.f;
	float GroundClock = 0.f;
	float Ground = 0.f;
	float AvoidClock = 0.f;
	float Lift = 0.f;
	float NetClock = 0.f;
	void ServerTick(float DeltaTime);
	void TickDrone(float DeltaTime);
	void TickFriendlyDog(float DeltaTime);
	void TickStampede(float DeltaTime);
	void ClientSmooth(float DeltaTime);
	void Die(AActor* Killer);

	// 苏联的狗：一个小状态机
	enum class EDog : uint8 { Idle, Walk, Sit, ToSlide, Climb, Slide, ToSeesaw, Seesaw, Flee };
	EDog DogState = EDog::Idle;
	float DogClock = 0.f;
	FVector DogGoal = FVector::ZeroVector;
	int32 DogSpot = -1;
	int32 DogSlot = 0;
	/** 走到地方以后蹲下 */
	bool bThenSit = false;
	FVector FleeFrom = FVector::ZeroVector;
	void DogPickNext();
	/** 朝 Goal 走（贴地）；到了返回 true */
	bool DogWalkTo(const FVector& Goal, float Speed, float DeltaTime);
	bool DogLineClear(const FVector& From, const FVector& To) const;
	void ReleaseSpot();

	// 狗群
	/** 这一刻（服务器时间）的位置（脚底，没按循环挪过的连续坐标） */
	FVector StampedePos(float Now) const;
	float LastStampedeT = -1.f;
	TMap<TWeakObjectPtr<AActor>, float> LastBump;
};

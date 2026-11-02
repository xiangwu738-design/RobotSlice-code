// 无人机群（10-31 保卫战）：第一版只给压力测试用（-MechStressTest），量「很多架轻量敌人」的开销；以后正式的无人机敌人从这里长出来。
// 轻量的意思：没有骨骼动画、没有角色移动组件，每架只是一个位置 + 速度；一个群一个 Actor、一个实例化网格，统一更新、统一画。
// 行为：绕着目标在各自的高度和半径上盘旋，慢慢收拢；每架每 0.25 秒（错开）往前方打一条射线避障；每 1.6–2.4 秒朝目标「开一枪」
// （射线 + 枪口焰），不造成伤害。联机同步还没做（以后由服务器算、按群压缩同步）。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechDroneSwarm.generated.h"

class UInstancedStaticMeshComponent;
class UNiagaraSystem;
class UStaticMesh;

UCLASS()
class AMechDroneSwarm : public AActor
{
	GENERATED_BODY()

public:
	AMechDroneSwarm();
	virtual void Tick(float DeltaTime) override;

	/** 增减到 N 架（新的从目标外 60–90 米、离地 15–30 米处出现） */
	void SetCount(int32 N);
	void SetTarget(AActor* InTarget) { Target = InTarget; }
	int32 Num() const { return Drones.Num(); }
	/** 这一秒开了几枪（测试日志用） */
	int32 ShotsLastSecond = 0;

private:
	struct FDrone
	{
		FVector Pos = FVector::ZeroVector;
		FVector Vel = FVector::ZeroVector;
		float Angle = 0.f;        // 盘旋角
		float Radius = 3000.f;    // 盘旋半径（慢慢收拢）
		float Height = 1500.f;    // 离目标的高度
		float FireClock = 0.f;
		float TraceClock = 0.f;
		float Lift = 0.f;         // 避障时往上抬
	};
	TArray<FDrone> Drones;
	int32 ShotCounter = 0;
	float ShotWindow = 0.f;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Mesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> DroneMesh;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> MuzzleFX;
	TWeakObjectPtr<AActor> Target;
};

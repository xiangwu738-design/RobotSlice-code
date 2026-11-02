#include "MechDroneSwarm.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

AMechDroneSwarm::AMechDroneSwarm()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Drones"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);  // 小东西、数量多：不投影子
	Mesh->bAffectDistanceFieldLighting = false;
	Mesh->bAffectDynamicIndirectLighting = false;
	Mesh->SetUsingAbsoluteLocation(true);
	Mesh->SetUsingAbsoluteRotation(true);
	Mesh->SetUsingAbsoluteScale(true);
	Mesh->SetHasPerInstancePrevTransforms(true);
	// 先借导弹巢的模型当机身（正式的无人机模型以后做）
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Body(TEXT("/Game/Weapons/Meshes/SM_MissilePod"));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Muzzle(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Muzzle"));
	DroneMesh = Body.Object;
	MuzzleFX = Muzzle.Object;
	Mesh->SetStaticMesh(DroneMesh);
}

void AMechDroneSwarm::SetCount(int32 N)
{
	const FVector Center = Target.IsValid() ? Target->GetActorLocation() : GetActorLocation();
	while (Drones.Num() > N)
	{
		Drones.Pop();
	}
	while (Drones.Num() < N)
	{
		FDrone D;
		D.Angle = FMath::FRandRange(0.f, 2.f * PI);
		D.Radius = FMath::FRandRange(2500.f, 6000.f);
		D.Height = FMath::FRandRange(1200.f, 3000.f);
		const float R0 = FMath::FRandRange(6000.f, 9000.f);
		D.Pos = Center + FVector(FMath::Cos(D.Angle) * R0, FMath::Sin(D.Angle) * R0, FMath::FRandRange(1500.f, 3000.f));
		D.FireClock = FMath::FRandRange(0.f, 2.f);
		D.TraceClock = FMath::FRandRange(0.f, 0.25f);
		Drones.Add(D);
	}
}

void AMechDroneSwarm::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	const FVector Center = Target.IsValid() ? Target->GetActorLocation() : GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechDrone), false, Target.Get());
	TArray<FTransform> Xf;
	Xf.Reserve(Drones.Num());
	for (FDrone& D : Drones)
	{
		// 盘旋：角速度和半径相关（外圈慢），半径慢慢收到 25 米
		D.Angle += DeltaTime * 1800.f / FMath::Max(D.Radius, 500.f) * 0.6f;
		D.Radius = FMath::Max(2500.f, D.Radius - DeltaTime * 150.f);
		const FVector Want = Center + FVector(FMath::Cos(D.Angle) * D.Radius, FMath::Sin(D.Angle) * D.Radius, D.Height + D.Lift);
		const FVector Accel = ((Want - D.Pos) * 2.5f - D.Vel * 1.6f).GetClampedToMaxSize(4000.f);
		D.Vel = (D.Vel + Accel * DeltaTime).GetClampedToMaxSize(2600.f);
		D.Pos += D.Vel * DeltaTime;
		// 避障：每 0.25 秒往前 15 米打一条射线，挡住就往上抬
		D.TraceClock -= DeltaTime;
		if (D.TraceClock <= 0.f)
		{
			D.TraceClock += 0.25f;
			FHitResult Hit;
			const bool bBlocked = World->LineTraceSingleByChannel(Hit, D.Pos, D.Pos + D.Vel.GetSafeNormal() * 1500.f, ECC_Visibility, Params);
			D.Lift = bBlocked ? FMath::Min(D.Lift + 600.f, 3000.f) : FMath::Max(D.Lift - 150.f, 0.f);
		}
		// 开火：射线 + 枪口焰（不造成伤害）
		D.FireClock -= DeltaTime;
		if (D.FireClock <= 0.f)
		{
			D.FireClock = FMath::FRandRange(1.6f, 2.4f);
			const FVector Aim = (Center + FVector(0.f, 0.f, 100.f) - D.Pos).GetSafeNormal();
			FHitResult Hit;
			World->LineTraceSingleByChannel(Hit, D.Pos, D.Pos + Aim * 12000.f, ECC_Visibility, Params);
			if (MuzzleFX)
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, MuzzleFX, D.Pos + Aim * 80.f, Aim.Rotation(), FVector(0.35f), true, true, ENCPoolMethod::AutoRelease);
			}
			++ShotCounter;
		}
		const FRotator Rot = (D.Vel.SizeSquared() > 100.f ? D.Vel : (Center - D.Pos)).Rotation();
		Xf.Add(FTransform(Rot, D.Pos, FVector(1.f)));
	}
	ShotWindow += DeltaTime;
	if (ShotWindow >= 1.f)
	{
		ShotsLastSecond = ShotCounter;
		ShotCounter = 0;
		ShotWindow = 0.f;
	}
	// 每帧整批重摆（实例编号会变，上一帧位置给成和这一帧一样，同曳光）
	Mesh->ClearInstances();
	if (Xf.Num())
	{
		Mesh->AddInstances(Xf, false, true);
		Mesh->BatchUpdateInstancesTransforms(0, Xf, Xf, true, true, true);
	}
}

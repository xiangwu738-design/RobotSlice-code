#include "MechElevator.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/ConstructorHelpers.h"

AMechElevator::AMechElevator()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;  // 先挪平台，再算站在上面的机体
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Platform = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Platform"));
	Platform->SetupAttachment(Root);
	Platform->SetMobility(EComponentMobility::Movable);
	Platform->SetCollisionProfileName(TEXT("BlockAll"));
	Platform->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
	Platform->SetStaticMesh(Cube.Object);
}

void AMechElevator::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// 方块网格 100 厘米见方、中心在原点：缩放成平台大小，顶面对齐 Actor 的位置
	Platform->SetRelativeScale3D(Size / 100.f);
	if (PlatformMaterial)
	{
		Platform->SetMaterial(0, PlatformMaterial);
	}
	ApplyLift(0.f);
}

void AMechElevator::BeginPlay()
{
	Super::BeginPlay();
	ApplyLift(LiftAt(0.f));
}

float AMechElevator::LiftAt(float Time) const
{
	const float Cycle = 2.f * (MoveTime + StopTime);
	if (Cycle <= 0.f || MoveTime <= 0.f)
	{
		return 0.f;
	}
	const float T = FMath::Fmod(FMath::Max(0.f, Time + Phase), Cycle);
	// 底下停 → 升 → 顶上停 → 降
	if (T < StopTime)
	{
		return 0.f;
	}
	if (T < StopTime + MoveTime)
	{
		return FMath::SmoothStep(0.f, 1.f, (T - StopTime) / MoveTime);
	}
	if (T < 2.f * StopTime + MoveTime)
	{
		return 1.f;
	}
	return 1.f - FMath::SmoothStep(0.f, 1.f, (T - 2.f * StopTime - MoveTime) / MoveTime);
}

void AMechElevator::ApplyLift(float L)
{
	Lift01 = L;
	Platform->SetRelativeLocation(FVector(0.f, 0.f, L * Travel - Size.Z * 0.5f));
}

void AMechElevator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// 按服务器时间：联机各端同一时刻在同一高度（客户端的服务器时间有估算误差，几十毫秒内）
	const AGameStateBase* GS = GetWorld()->GetGameState();
	ApplyLift(LiftAt(GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds()));
}

#include "MechPluginPod.h"

#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MechCharacter.h"
#include "MechEffectComponent.h"
#include "MechFeed.h"
#include "MechHealthComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMechPluginPod::AMechPluginPod()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);
	Trigger->InitSphereRadius(330.f);
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetGenerateOverlapEvents(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Core = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Core"));
	Core->SetupAttachment(Trigger);
	Core->SetStaticMesh(Cube.Object);
	Core->SetRelativeScale3D(FVector(0.75f));
	Core->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Core->SetCastShadow(false);
	Ring = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ring"));
	Ring->SetupAttachment(Trigger);
	Ring->SetStaticMesh(Cylinder.Object);
	Ring->SetRelativeLocation(FVector(0.f, 0.f, -180.f));
	Ring->SetRelativeScale3D(FVector(2.6f, 2.6f, 0.04f));
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring->SetCastShadow(false);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Trigger);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(600.f);
	Light->SetAttenuationRadius(1400.f);
	Light->SetCastShadows(false);
}

void AMechPluginPod::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Robot/Materials/M_RobotMaster")))
	{
		GlowMID = UMaterialInstanceDynamic::Create(Master, this);
		Core->SetMaterial(0, GlowMID);
		Ring->SetMaterial(0, GlowMID);
	}
	ApplyLook();
	if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMechPluginPod::OnTouch);
	}
}

void AMechPluginPod::OnRep_Plugin()
{
	ApplyLook();
}

void AMechPluginPod::ApplyLook()
{
	const MechEffects::FDef* Def = MechEffects::Find(Plugin);
	const FLinearColor C = Def ? Def->Color : FLinearColor(0.36f, 0.79f, 0.65f);
	if (GlowMID)
	{
		GlowMID->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.02f, 0.02f, 0.025f));
		GlowMID->SetVectorParameterValue(TEXT("Emissive"), C);
		GlowMID->SetScalarParameterValue(TEXT("EmissiveStrength"), 12.f);
	}
	Light->SetLightColor(C);
}

void AMechPluginPod::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	Core->SetRelativeRotation(FRotator(35.f * FMath::Sin(Clock * 0.7f), Clock * 70.f, 45.f));
	Core->SetRelativeLocation(FVector(0.f, 0.f, 25.f * FMath::Sin(Clock * 1.8f)));
	Ring->SetRelativeScale3D(FVector(2.6f + 0.15f * FMath::Sin(Clock * 3.f), 2.6f + 0.15f * FMath::Sin(Clock * 3.f), 0.04f));
}

void AMechPluginPod::OnTouch(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	AMechCharacter* Mech = Cast<AMechCharacter>(Other);
	if (bTaken || !Mech || !Mech->GetEffects() || Mech->GetHealth()->IsDead() || Mech->IsTrainingDummy())
	{
		return;
	}
	bTaken = true;
	Mech->GetEffects()->AddEffect(Plugin, PluginTime, 1);
	UE_LOG(LogTemp, Display, TEXT("[plugin] %s 拿到 %s"), *Mech->DisplayName, *Plugin.ToString());
	MechFeed::OnPlugin(Mech, Plugin);  // 11-01 信息区：队友拿到插件（只发给同队，自己那台不发）
	Destroy();
}

void AMechPluginPod::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechPluginPod, Plugin);
}

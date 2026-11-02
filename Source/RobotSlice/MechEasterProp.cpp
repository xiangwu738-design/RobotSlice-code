// 彩蛋的机关：压力板、门、按钮（11-02，说明见 MechEasterProp.h）

#include "MechEasterProp.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MechCharacter.h"
#include "MechEaster.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

AMechEasterProp::AMechEasterProp()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	// 打得中的盒子（门、按钮用；压力板不能打）：照保卫战杂兵，WorldDynamic、只查询，子弹按 MechShootable 标签把伤害交给 TakeDamage
	HitBox = CreateDefaultSubobject<UBoxComponent>(TEXT("HitBox"));
	RootComponent = HitBox;
	HitBox->InitBoxExtent(FVector(20.f, 100.f, 150.f));
	HitBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HitBox->SetCollisionObjectType(ECC_WorldDynamic);
	HitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitBox->SetGenerateOverlapEvents(false);
}

void AMechEasterProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechEasterProp, Kind);
	DOREPLIFETIME(AMechEasterProp, bDone);
	DOREPLIFETIME(AMechEasterProp, bArmed);
	DOREPLIFETIME(AMechEasterProp, bPlaceholder);
	DOREPLIFETIME(AMechEasterProp, Size);
	DOREPLIFETIME(AMechEasterProp, LinkedTag);
}

void AMechEasterProp::InitProp(uint8 InKind, const FVector& InSize, bool bInPlaceholder, FName InLinkedTag, AMechEasterDirector* InDirector)
{
	Kind = InKind;
	Size = InSize;
	bPlaceholder = bInPlaceholder;
	LinkedTag = InLinkedTag;
	Director = InDirector;
	HP = MechEaster::DoorHP;
}

void AMechEasterProp::BeginPlay()
{
	Super::BeginPlay();
	Build();
}

void AMechEasterProp::Build()
{
	if (bBuilt)
	{
		return;
	}
	bBuilt = true;
	HitBox->SetBoxExtent(FVector(Size) * 0.5f);
	if (Kind != MechEasterProp::Plate)
	{
		Tags.AddUnique(TEXT("MechShootable"));
		Tags.AddUnique(TEXT("MechGrunt"));  // 刀、导弹爆炸也算（它们按这个标签找能打的东西）
	}
	if (bPlaceholder)
	{
		// 占位：引擎自带的方块 / 圆柱 + 机体的深色漆、曳光弹的发光材质（和保卫战杂兵的占位一样）
		UStaticMesh* Cube = MechEaster::Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube"));
		UStaticMesh* Cylinder = MechEaster::Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder"));
		UMaterialInterface* Paint = MechEaster::Load<UMaterialInterface>(TEXT("/Game/Robot/Materials/MI_Robot_PaintDark"));
		UMaterialInterface* GlowMat = MechEaster::Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/MI_Tracer"));
		auto Part = [this](UStaticMesh* M, UMaterialInterface* Mat) {
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
			C->SetStaticMesh(M);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetupAttachment(HitBox);
			if (Mat)
			{
				C->SetMaterial(0, Mat);
			}
			C->RegisterComponent();
			return C;
		};
		const FVector S = FVector(Size) / 100.f;  // 引擎方块 / 圆柱都是 1 米
		if (Kind == MechEasterProp::Plate)
		{
			// 一块扁圆盘，下面一圈细细的发光边（藏在塔顶 / 山顶，不显眼）
			Base = Part(Cylinder, Paint);
			Base->SetRelativeScale3D(FVector(S.X, S.Y, FMath::Max(S.Z, 0.04f)));
			Glow = Part(Cylinder, GlowMat);
			Glow->SetRelativeLocation(FVector(0.f, 0.f, -2.f));
			Glow->SetRelativeScale3D(FVector(S.X * 1.06f, S.Y * 1.06f, 0.02f));
			Glow->SetCastShadow(false);
		}
		else if (Kind == MechEasterProp::Door)
		{
			Base = Part(Cube, Paint);
			Base->SetRelativeScale3D(S);
		}
		else
		{
			// 按钮：一块底板 + 朝外的红色圆钮（圆柱的轴转到 actor 的 X = 朝街）
			Base = Part(Cube, Paint);
			Base->SetRelativeScale3D(FVector(S.X * 0.5f, S.Y * 1.3f, S.Z * 1.3f));
			Glow = Part(Cylinder, GlowMat);
			Glow->SetRelativeLocation(FVector(Size.X * 0.4f, 0.f, 0.f));
			Glow->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
			Glow->SetRelativeScale3D(FVector(S.Y, S.Z, S.X));
			Glow->SetCastShadow(false);
		}
		if (Glow && GlowMat)
		{
			GlowMID = UMaterialInstanceDynamic::Create(GlowMat, this);
			Glow->SetMaterial(0, GlowMID);
		}
	}
	Refresh();
}

AActor* AMechEasterProp::FindLinked() const
{
	if (LinkedTag.IsNone())
	{
		return nullptr;
	}
	AActor* Best = nullptr;
	float BestD = 2000.f;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (*It == this || !It->ActorHasTag(LinkedTag))
		{
			continue;
		}
		const float D = FVector::Dist(It->GetActorLocation(), GetActorLocation());
		if (D < BestD)
		{
			BestD = D;
			Best = *It;
		}
	}
	return Best;
}

void AMechEasterProp::OnRep_State()
{
	if (HasActorBegunPlay())
	{
		Build();
		Refresh();
	}
}

void AMechEasterProp::Refresh()
{
	if (!bBuilt)
	{
		return;
	}
	bool bShootable = false;
	bool bShow = true;
	if (Kind == MechEasterProp::Door)
	{
		bShootable = !bDone;
		bShow = !bDone;
		if (bDone)
		{
			// 地图里的那扇门（不复制的东西，各端自己藏）
			if (AActor* Linked = FindLinked())
			{
				Linked->SetActorHiddenInGame(true);
				Linked->SetActorEnableCollision(false);
			}
		}
	}
	else if (Kind == MechEasterProp::Button)
	{
		bShootable = bArmed && !bDone;
		bShow = bArmed;
	}
	else if (Base)
	{
		// 压力板：踩下去沉 4 厘米
		Base->SetRelativeLocation(FVector(0.f, 0.f, bDone ? -4.f : 0.f));
	}
	HitBox->SetCollisionEnabled(bShootable ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	if (Base)
	{
		Base->SetVisibility(bShow);
	}
	if (Glow)
	{
		Glow->SetVisibility(bShow);
	}
}

void AMechEasterProp::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Clock += DeltaTime;
	if (!GlowMID)
	{
		return;
	}
	// 压力板：没踩时淡淡的青色呼吸，踩下后红色；按钮：红色快闪，按下后变绿
	FLinearColor C(0.1f, 0.8f, 1.f);
	float Strength = 4.f + 2.f * FMath::Sin(Clock * 1.5f);
	if (Kind == MechEasterProp::Plate && bDone)
	{
		C = FLinearColor(1.f, 0.08f, 0.04f);
		Strength = 30.f;
	}
	else if (Kind == MechEasterProp::Button)
	{
		C = bDone ? FLinearColor(0.2f, 1.f, 0.3f) : FLinearColor(1.f, 0.06f, 0.03f);
		Strength = bDone ? 20.f : 25.f + 20.f * FMath::Sin(Clock * 9.f);
	}
	GlowMID->SetVectorParameterValue(TEXT("Emissive"), C);
	GlowMID->SetScalarParameterValue(TEXT("EmissiveStrength"), Strength);
}

float AMechEasterProp::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || bDone || DamageAmount <= 0.f)
	{
		return 0.f;
	}
	AActor* By = Cast<AMechCharacter>(DamageCauser);
	if (!By && EventInstigator)
	{
		By = EventInstigator->GetPawn();
	}
	if (Kind == MechEasterProp::Door)
	{
		HP -= DamageAmount;
		if (HP <= 0.f)
		{
			SetDone(By);
		}
		return DamageAmount;
	}
	if (Kind == MechEasterProp::Button && bArmed)
	{
		SetDone(By);
		return DamageAmount;
	}
	return 0.f;
}

void AMechEasterProp::SetDone(AActor* By)
{
	if (!HasAuthority() || bDone)
	{
		return;
	}
	bDone = true;
	DoneTime = GetWorld()->GetTimeSeconds();
	ForceNetUpdate();
	Refresh();
	if (Kind == MechEasterProp::Door)
	{
		MulticastBreakFX(GetActorLocation());
		if (AMechEasterDirector* D = Director.Get())
		{
			D->OnDoorBroken(By);
		}
	}
	else if (Kind == MechEasterProp::Button)
	{
		if (AMechEasterDirector* D = Director.Get())
		{
			D->OnButtonShot(By);
		}
	}
}

void AMechEasterProp::SetArmed(bool bOn)
{
	if (!HasAuthority() || bArmed == bOn)
	{
		return;
	}
	bArmed = bOn;
	ForceNetUpdate();
	Refresh();
}

void AMechEasterProp::MulticastBreakFX_Implementation(FVector_NetQuantize At)
{
	if (UNiagaraSystem* FX = MechEaster::Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01")))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, FX, At, FRotator::ZeroRotator, FVector(1.6f), true, true, ENCPoolMethod::AutoRelease);
	}
	if (USoundBase* Snd = MechEaster::Load<USoundBase>(TEXT("/Game/Audio/Combat/S_explosion_small")))
	{
		UGameplayStatics::PlaySoundAtLocation(this, Snd, At, FRotator::ZeroRotator, 0.6f, 1.1f);
	}
}

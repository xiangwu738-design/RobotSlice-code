#include "MechLoop.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "MechCharacter.h"

namespace MechLoop
{
	namespace
	{
		UMechLoopSubsystem* Sub(const UWorld* World)
		{
			return World ? World->GetSubsystem<UMechLoopSubsystem>() : nullptr;
		}
	}

	bool IsActive(const UWorld* World)
	{
		UMechLoopSubsystem* S = Sub(World);
		return S && S->IsLoop();
	}

	float Period(const UWorld* World)
	{
		UMechLoopSubsystem* S = Sub(World);
		return S && S->IsLoop() ? S->GetPeriod() : 0.f;
	}

	float Seam(const UWorld* World)
	{
		UMechLoopSubsystem* S = Sub(World);
		return S && S->IsLoop() ? S->GetSeam() : 0.f;
	}

	FVector Near(const UWorld* World, const FVector& Target, const FVector& Ref)
	{
		UMechLoopSubsystem* S = Sub(World);
		if (!S || !S->IsLoop())
		{
			return Target;
		}
		const double P = S->GetPeriod();
		const double K = FMath::RoundToDouble((Target.X - Ref.X) / P);
		return FVector(Target.X - K * P, Target.Y, Target.Z);
	}

	FVector Delta(const UWorld* World, const FVector& From, const FVector& To)
	{
		return Near(World, To, From) - From;
	}

	float Dist(const UWorld* World, const FVector& A, const FVector& B)
	{
		return float(Delta(World, A, B).Size());
	}

	float DistSquared(const UWorld* World, const FVector& A, const FVector& B)
	{
		return float(Delta(World, A, B).SizeSquared());
	}

	float WrapShift(const UWorld* World, float X, float Margin)
	{
		UMechLoopSubsystem* S = Sub(World);
		if (!S || !S->IsLoop())
		{
			return 0.f;
		}
		const float P = S->GetPeriod();
		const float Rel = X - S->GetCenter();
		const float Limit = S->GetSeam() + Margin;
		if (FMath::Abs(Rel) <= Limit)
		{
			return 0.f;
		}
		// 挪整周期，挪完落回 [-接缝, 接缝] 附近
		return -P * FMath::RoundToFloat(Rel / P);
	}

	FVector ViewLocation(const UWorld* World)
	{
		if (World)
		{
			if (const APlayerController* PC = World->GetFirstPlayerController(); PC && PC->IsLocalController() && PC->PlayerCameraManager)
			{
				return PC->PlayerCameraManager->GetCameraLocation();
			}
		}
		return FVector::ZeroVector;
	}

	bool LogOn()
	{
		static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("MechLoopLog"));
		return bOn;
	}

	bool CutOn()
	{
		static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("MechLoopNoCut"));
		return !bOff;
	}
}

// ---------------------------------------------------------------- 子系统
void UMechLoopSubsystem::Scan()
{
	if (bScanned)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World || !World->AreActorsInitialized())
	{
		return;  // 地图里的点还没全加载：先当不循环，过一会再查
	}
	bScanned = true;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->ActorHasTag(TEXT("OldStreetBounds")))
		{
			continue;
		}
		float P = 0.f;
		float S = 0.f;
		for (const FName& T : It->Tags)
		{
			const FString Str = T.ToString();
			if (Str.StartsWith(TEXT("PeriodX=")))
			{
				P = FCString::Atof(*Str.Mid(8));
			}
			else if (Str.StartsWith(TEXT("SeamX=")))
			{
				S = FCString::Atof(*Str.Mid(6));
			}
		}
		if (P > 1000.f)
		{
			bLoop = true;
			Period = P;
			SeamX = S > 0.f ? S : P * 0.5f;
			CenterX = It->GetActorLocation().X;
			UE_LOG(LogTemp, Display, TEXT("[loop] 沿 X 循环：周期 %.0f 米，接缝 ±%.0f 米，中心 X = %.0f 米"), Period / 100.f, SeamX / 100.f, CenterX / 100.f);
		}
		break;
	}
}

void UMechLoopSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Scan();
	if (bLoop && InWorld.GetNetMode() != NM_DedicatedServer)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		InWorld.SpawnActor<AMechLoopGhosts>(AMechLoopGhosts::StaticClass(), FTransform::Identity, Params);
	}
}

// ---------------------------------------------------------------- 镜像画面
AMechLoopGhosts::AMechLoopGhosts()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;  // 机体的骨骼、手里的武器都摆完以后再抄
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

bool AMechLoopGhosts::IsMirrored(const UPrimitiveComponent* C)
{
	// 骨骼网格和静态网格（武器、喷焰、护盾……）；实例化网格是子弹、电弧这类特效，不抄
	if (!C || C->IsA<UInstancedStaticMeshComponent>())
	{
		return false;
	}
	if (const USkeletalMeshComponent* Sk = Cast<USkeletalMeshComponent>(C))
	{
		return Sk->GetSkeletalMeshAsset() != nullptr;
	}
	if (const UStaticMeshComponent* St = Cast<UStaticMeshComponent>(C))
	{
		return St->GetStaticMesh() != nullptr;
	}
	return false;
}

UPrimitiveComponent* AMechLoopGhosts::MakeCopy(UPrimitiveComponent* Source)
{
	UPrimitiveComponent* Copy = nullptr;
	if (USkeletalMeshComponent* Sk = Cast<USkeletalMeshComponent>(Source))
	{
		USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(this);
		C->SetSkeletalMeshAsset(Sk->GetSkeletalMeshAsset());
		// 包围盒按网格资源算（跟随姿势时引擎按本体那份算会算到本体的位置上去，镜像会被错误剔除）
		C->bComponentUseFixedSkelBounds = true;
		C->SetBoundsScale(1.6f);
		Copy = C;
		// 本体离开画面时也要更新骨骼（镜像还在画面里）
		Sk->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
	else if (UStaticMeshComponent* St = Cast<UStaticMeshComponent>(Source))
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(St->GetStaticMesh());
		Copy = C;
	}
	if (!Copy)
	{
		return nullptr;
	}
	Copy->SetUsingAbsoluteLocation(true);
	Copy->SetUsingAbsoluteRotation(true);
	Copy->SetUsingAbsoluteScale(true);
	Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Copy->SetGenerateOverlapEvents(false);
	Copy->SetCastShadow(Source->CastShadow);
	Copy->bAffectDistanceFieldLighting = false;
	Copy->SetupAttachment(GetRootComponent());
	Copy->RegisterComponent();
	if (USkeletalMeshComponent* C = Cast<USkeletalMeshComponent>(Copy))
	{
		C->SetLeaderPoseComponent(Cast<USkeletalMeshComponent>(Source), true);
	}
	AllCopies.Add(Copy);
	return Copy;
}

void AMechLoopGhosts::DestroyPart(FPart& Part)
{
	for (UPrimitiveComponent* C : Part.Copies)
	{
		if (IsValid(C))
		{
			AllCopies.Remove(C);
			C->DestroyComponent();
		}
	}
	Part.Copies.Reset();
}

void AMechLoopGhosts::RescanParts(FMechSet& Set)
{
	AActor* Mech = Set.Mech.Get();
	TArray<UPrimitiveComponent*> Prims;
	if (Mech)
	{
		Mech->GetComponents<UPrimitiveComponent>(Prims);
	}
	// 去掉已经没有的
	for (int32 i = Set.Parts.Num() - 1; i >= 0; --i)
	{
		UPrimitiveComponent* Src = Set.Parts[i].Source.Get();
		if (!Src || !Prims.Contains(Src) || !IsMirrored(Src))
		{
			DestroyPart(Set.Parts[i]);
			Set.Parts.RemoveAtSwap(i);
		}
	}
	// 加新的
	for (UPrimitiveComponent* P : Prims)
	{
		if (!IsMirrored(P) || Set.Parts.ContainsByPredicate([P](const FPart& Part) { return Part.Source.Get() == P; }))
		{
			continue;
		}
		FPart& Part = Set.Parts.AddDefaulted_GetRef();
		Part.Source = P;
		for (int32 k = 0; k < Offsets.Num(); ++k)
		{
			Part.Copies.Add(MakeCopy(P));
		}
	}
}

void AMechLoopGhosts::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!MechLoop::IsActive(World))
	{
		return;
	}
	if (Offsets.Num() == 0)
	{
		const float P = MechLoop::Period(World);
		for (int32 r = 1; r <= MechLoop::GhostRings; ++r)
		{
			Offsets.Add(P * r);
			Offsets.Add(-P * r);
		}
	}
	// 每秒找一次新出生的机体
	FindClock -= DeltaTime;
	if (FindClock <= 0.f)
	{
		FindClock = 1.f;
		for (TActorIterator<AMechCharacter> It(World); It; ++It)
		{
			if (!Sets.ContainsByPredicate([&It](const FMechSet& S) { return S.Mech.Get() == *It; }))
			{
				FMechSet& S = Sets.AddDefaulted_GetRef();
				S.Mech = *It;
			}
		}
	}
	const FVector View = MechLoop::ViewLocation(World);
	for (int32 s = Sets.Num() - 1; s >= 0; --s)
	{
		FMechSet& Set = Sets[s];
		AActor* Mech = Set.Mech.Get();
		if (!IsValid(Mech))
		{
			for (FPart& Part : Set.Parts)
			{
				DestroyPart(Part);
			}
			Sets.RemoveAtSwap(s);
			continue;
		}
		// 武器、护盾之类的组件是开局后才建的：半秒对一次
		Set.RescanClock -= DeltaTime;
		if (Set.RescanClock <= 0.f)
		{
			Set.RescanClock = 0.5f;
			RescanParts(Set);
		}
		for (FPart& Part : Set.Parts)
		{
			UPrimitiveComponent* Src = Part.Source.Get();
			if (!Src)
			{
				continue;
			}
			const bool bShown = Src->IsRegistered() && Src->ShouldRender();  // 自己可见、没在游戏里隐藏、机体没被藏起来
			const FTransform Xf = Src->GetComponentTransform();
			for (int32 k = 0; k < Part.Copies.Num() && k < Offsets.Num(); ++k)
			{
				UPrimitiveComponent* C = Part.Copies[k];
				if (!IsValid(C))
				{
					continue;
				}
				const FVector At = Xf.GetLocation() + FVector(Offsets[k], 0.f, 0.f);
				const bool bVisible = bShown && FMath::Abs(At.X - View.X) < MechLoop::GhostMaxDist;
				if (C->IsVisible() != bVisible)
				{
					C->SetVisibility(bVisible);
				}
				if (!bVisible)
				{
					continue;
				}
				// 换了网格 / 材质（换配装、受击闪白、队伍色）就跟着换
				if (USkeletalMeshComponent* Sk = Cast<USkeletalMeshComponent>(C))
				{
					USkeletalMeshComponent* SrcSk = CastChecked<USkeletalMeshComponent>(Src);
					if (Sk->GetSkeletalMeshAsset() != SrcSk->GetSkeletalMeshAsset())
					{
						Sk->SetSkeletalMeshAsset(SrcSk->GetSkeletalMeshAsset());
						Sk->SetLeaderPoseComponent(SrcSk, true);
					}
				}
				else if (UStaticMeshComponent* St = Cast<UStaticMeshComponent>(C))
				{
					UStaticMeshComponent* SrcSt = CastChecked<UStaticMeshComponent>(Src);
					if (St->GetStaticMesh() != SrcSt->GetStaticMesh())
					{
						St->SetStaticMesh(SrcSt->GetStaticMesh());
					}
				}
				const int32 NumMats = Src->GetNumMaterials();
				for (int32 m = 0; m < NumMats; ++m)
				{
					UMaterialInterface* Mat = Src->GetMaterial(m);
					if (C->GetMaterial(m) != Mat)
					{
						C->SetMaterial(m, Mat);
					}
				}
				C->SetWorldTransform(FTransform(Xf.GetRotation(), At, Xf.GetScale3D()));
			}
		}
	}
}

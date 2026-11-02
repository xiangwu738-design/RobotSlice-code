#include "MechSmoke.h"
#include "MechLoop.h"
#include "EngineUtils.h"
#include "MechCharacter.h"
#include "MechHealthComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "RHI.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	template <typename T>
	T* Load(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<T> Finder(Path);
		return Finder.Object;
	}
	/** 场上所有烟（BlocksLine 每帧要查好多次，不用遍历全部 Actor） */
	TArray<TWeakObjectPtr<AMechSmoke>> GSmokes;
	const int32 NumPuffs = 34;
	const int32 NumGlints = 46;
	/** 烟片的边长（厘米）：平面按最大尺寸放，涨开靠材质里缩放贴图（Grow） */
	const float PuffSize = 900.f;
	const float GlintSize = 9.f;
	/** 球心离地多高（BlocksLine 用） */
	const float CloudHeight = 250.f;
}

AMechSmoke::AMechSmoke()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	PlaneMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Plane"));
	PuffMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_SmokePuff"));
	GlintMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_ChaffGlint"));
	BurstSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_smoke_burst"));
	HissSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_smoke_hiss"));
	Attenuation = Load<USoundAttenuation>(TEXT("/Game/Audio/SA_Combat"));
}

void AMechSmoke::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMechSmoke, SpawnTime, COND_InitialOnly);
}

float AMechSmoke::ServerNow() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

float AMechSmoke::Age() const
{
	return ServerNow() - SpawnTime;
}

float AMechSmoke::GetRadius() const
{
	const float A = Age();
	const float Grow = 1.f - FMath::Exp(-A * 3.f);
	const float Fade = FMath::Clamp((Duration - A) / 1.5f, 0.f, 1.f);
	return Radius * FMath::Min(Grow, Fade);
}

UStaticMeshComponent* AMechSmoke::MakePlane(const FVector& At, float Size, UMaterialInstanceDynamic* MID)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(PlaneMesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->bAffectDistanceFieldLighting = false;
	C->bAffectDynamicIndirectLighting = false;
	C->SetupAttachment(RootComponent);
	C->SetRelativeLocation(At);
	C->SetRelativeScale3D(FVector(Size / 100.f));
	if (MID)
	{
		C->SetMaterial(0, MID);
	}
	C->RegisterComponent();
	return C;
}

void AMechSmoke::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		SpawnTime = ServerNow();
		SetLifeSpan(Duration + 0.5f);
	}
	GSmokes.Add(this);
	UE_LOG(LogTemp, Display, TEXT("[smoke] 烟片材质 %s 编译中或出错 %d，箔片材质 %s 编译中或出错 %d"), PuffMaterial ? *PuffMaterial->GetPathName() : TEXT("无"),
		PuffMaterial && PuffMaterial->GetMaterial()->IsCompilingOrHadCompileError(GMaxRHIShaderPlatform) ? 1 : 0, GlintMaterial ? *GlintMaterial->GetPathName() : TEXT("无"),
		GlintMaterial && GlintMaterial->GetMaterial()->IsCompilingOrHadCompileError(GMaxRHIShaderPlatform) ? 1 : 0);

	// 烟片：中间几片最先冒出来、最大最浓，外圈的晚一点、往外滚；高度偏向贴地
	FRandomStream Rand(GetTypeHash(GetActorLocation().GridSnap(10.f)));
	for (int32 i = 0; i < NumPuffs; ++i)
	{
		FPuff P;
		const float Ring = i < 4 ? Rand.FRandRange(0.f, 0.2f) : FMath::Sqrt(Rand.FRandRange(0.05f, 1.f));
		const float A = Rand.FRandRange(0.f, 2.f * PI);
		const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
		// 高度偏向贴地（机体在 0–3.5 米这一层，烟要先把这一层盖住）
		P.Start = Dir * Ring * Radius * 0.62f + FVector(0.f, 0.f, 50.f + FMath::Pow(Rand.FRand(), 2.2f) * 420.f * (1.f - 0.4f * Ring));
		P.Drift = Dir * Rand.FRandRange(120.f, 260.f) + FVector(0.f, 0.f, Rand.FRandRange(10.f, 40.f));
		P.Delay = i < 4 ? Rand.FRandRange(0.f, 0.08f) : Ring * 0.55f + Rand.FRandRange(0.f, 0.2f);
		P.Rot0 = Rand.FRandRange(0.f, 2.f * PI);
		P.Spin = Rand.FRandRange(-0.25f, 0.25f);
		P.Frame0 = Rand.FRandRange(26.f, 36.f);  // 序列帧后半段：烟团已经长大、蓬松（前面是刚冒出来的小团）
		P.Opacity = Rand.FRandRange(0.85f, 1.f);
		P.FadeAt = Duration - Rand.FRandRange(1.6f, 2.6f);
		P.Rate = Rand.FRandRange(2.f, 3.f);
		Puffs.Add(P);
		const float Size = PuffSize * (i < 4 ? 1.1f : Rand.FRandRange(0.75f, 1.f));
		UMaterialInstanceDynamic* MID = PuffMaterial ? UMaterialInstanceDynamic::Create(PuffMaterial, this) : nullptr;
		if (MID)
		{
			// 浓一点：材质的不透明度修好以后（以前整片一个透明度 = 方板），按贴图浓度算的烟偏薄，烟中间的机体看得一清二楚
			MID->SetScalarParameterValue(TEXT("Contrast"), 2.2f);
		}
		PuffComps.Add(MakePlane(P.Start, Size, MID));
		PuffMIDs.Add(MID);
		PuffSizes.Add(Size);
	}
	// 干扰箔片：小亮点，在烟里慢慢往下飘、一闪一闪
	for (int32 i = 0; i < NumGlints; ++i)
	{
		FPuff G;
		const float A = Rand.FRandRange(0.f, 2.f * PI);
		G.Start = FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * FMath::Sqrt(Rand.FRand()) * Radius * 0.75f + FVector(0.f, 0.f, Rand.FRandRange(80.f, 520.f));
		G.Drift = FVector(Rand.FRandRange(-20.f, 20.f), Rand.FRandRange(-20.f, 20.f), -Rand.FRandRange(10.f, 25.f));
		G.Delay = Rand.FRandRange(0.2f, 0.9f);
		G.Rot0 = Rand.FRandRange(0.f, 2.f * PI);
		G.Rate = Rand.FRandRange(2.f, 5.f);
		G.FadeAt = Duration - Rand.FRandRange(1.5f, 2.5f);
		Glints.Add(G);
		UMaterialInstanceDynamic* MID = GlintMaterial ? UMaterialInstanceDynamic::Create(GlintMaterial, this) : nullptr;
		GlintComps.Add(MakePlane(G.Start, GlintSize, MID));
		GlintMIDs.Add(MID);
	}
	Animate();
	UGameplayStatics::PlaySoundAtLocation(this, BurstSound, GetActorLocation(), FRotator::ZeroRotator, 1.f, 1.f, 0.f, Attenuation);
	UGameplayStatics::PlaySoundAtLocation(this, HissSound, GetActorLocation(), FRotator::ZeroRotator, 0.7f, 0.9f, 0.f, Attenuation);
}

void AMechSmoke::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GSmokes.RemoveAll([this](const TWeakObjectPtr<AMechSmoke>& S) { return !S.IsValid() || S.Get() == this; });
	Super::EndPlay(EndPlayReason);
}

void AMechSmoke::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Animate();
	if (HasAuthority())
	{
		// 烟里的敌人持续掉血：每 0.5 秒结算一次（服务器），算给放烟的人，同队不伤
		DamageAccum += DeltaSeconds;
		if (DamageAccum >= 0.5f)
		{
			const float Dmg = DamagePerSecond * DamageAccum;
			DamageAccum = 0.f;
			AMechCharacter* Thrower = Cast<AMechCharacter>(GetInstigator());
			const float R = GetRadius();
			if (Thrower && R > 10.f)
			{
				const FVector C = GetActorLocation() + FVector(0.f, 0.f, CloudHeight);
				for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
				{
					AMechCharacter* M = *It;
					if (M == Thrower || M->GetTeam() == Thrower->GetTeam() || M->GetHealth()->IsDead() || MechLoop::DistSquared(GetWorld(), M->GetActorLocation(), C) > R * R)
					{
						continue;
					}
					M->ReceiveHit(Dmg, 0.f, Thrower, M->GetActorLocation(), EMechHitKind::Bullet);
				}
			}
		}
	}
}

void AMechSmoke::Animate()
{
	const float A = Age();
	// 平面对着本机镜头（法线 +Z 朝镜头，X 跟镜头右方向；服务器没有画面就不转）
	FQuat Face = FQuat::Identity;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const bool bFace = PC && PC->PlayerCameraManager;
	if (bFace)
	{
		const FRotator CamRot = PC->PlayerCameraManager->GetCameraRotation();
		Face = FRotationMatrix::MakeFromZX(-CamRot.Vector(), FRotationMatrix(CamRot).GetScaledAxis(EAxis::Y)).ToQuat();
	}
	for (int32 i = 0; i < Puffs.Num(); ++i)
	{
		const FPuff& P = Puffs[i];
		UMaterialInstanceDynamic* MID = PuffMIDs[i];
		const float T = A - P.Delay;
		if (!MID || !PuffComps[i])
		{
			continue;
		}
		if (T <= 0.f)
		{
			MID->SetScalarParameterValue(TEXT("Opacity"), 0.f);
			continue;
		}
		// 先猛地涌出来再慢下来；往外、往上飘；慢慢转、慢慢换帧；最后变淡
		const float Out = 1.f - FMath::Exp(-T * 1.3f);
		PuffComps[i]->SetRelativeLocationAndRotation(P.Start + P.Drift * Out + FVector(0.f, 0.f, T * 6.f), Face);
		const float Fade = 1.f - FMath::SmoothStep(P.FadeAt, Duration, A);
		MID->SetScalarParameterValue(TEXT("Grow"), FMath::Lerp(0.45f, 1.f, 1.f - FMath::Exp(-T * 2.6f)) * (1.f + 0.12f * (1.f - Fade)));
		MID->SetScalarParameterValue(TEXT("Rot"), P.Rot0 + P.Spin * T);
		MID->SetScalarParameterValue(TEXT("Frame"), FMath::Min(P.Frame0 + T * P.Rate, 62.9f));  // 不绕回开头的小团
		MID->SetScalarParameterValue(TEXT("Opacity"), P.Opacity * FMath::SmoothStep(0.f, 0.25f, T) * Fade);
	}
	for (int32 i = 0; i < Glints.Num(); ++i)
	{
		const FPuff& G = Glints[i];
		UMaterialInstanceDynamic* MID = GlintMIDs[i];
		const float T = A - G.Delay;
		if (!MID || !GlintComps[i])
		{
			continue;
		}
		GlintComps[i]->SetRelativeLocationAndRotation(G.Start + G.Drift * FMath::Max(T, 0.f) + FVector(FMath::Sin(T * 1.7f + G.Rot0), FMath::Cos(T * 1.3f + G.Rot0), 0.f) * 12.f, Face);
		// 箔片翻转时偶尔反一下光：大部分时间暗，短促地亮一下
		const float Twinkle = FMath::Pow(FMath::Max(0.f, FMath::Sin(T * G.Rate * 2.f + G.Rot0 * 3.f)), 24.f);
		const float Fade = 1.f - FMath::SmoothStep(G.FadeAt, Duration, A);
		MID->SetScalarParameterValue(TEXT("Glint"), T > 0.f ? (0.04f + Twinkle) * Fade : 0.f);
	}
}

bool AMechSmoke::IsInside(const UWorld* World, const FVector& P)
{
	return BlocksLine(World, P, P);
}

bool AMechSmoke::BlocksLine(const UWorld* World, const FVector& A, const FVector& B)
{
	for (const TWeakObjectPtr<AMechSmoke>& Weak : GSmokes)
	{
		const AMechSmoke* S = Weak.Get();
		if (!S || S->GetWorld() != World)
		{
			continue;
		}
		const float R = S->GetRadius();
		if (R <= 10.f)
		{
			continue;
		}
		const FVector C = MechLoop::Near(World, S->GetActorLocation() + FVector(0.f, 0.f, CloudHeight), (A + B) * 0.5f);  // 老街循环
		if (FVector::DistSquared(FMath::ClosestPointOnSegment(C, A, B), C) < R * R)
		{
			return true;
		}
	}
	return false;
}

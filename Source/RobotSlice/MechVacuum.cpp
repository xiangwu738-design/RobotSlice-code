#include "MechVacuum.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/UObjectIterator.h"

namespace
{
	bool VacuumLog()
	{
		static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("MechVacuumLog"));
		return bOn;
	}
}

bool UMechVacuum::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("MechVacuumOff"));
	// 原开发图和新蓝穹基地共用真空声音规则；空气范围由各图 MoonAir 标记决定。
	return !bOff && (World->GetMapName().EndsWith(TEXT("L_Moon")) || World->GetMapName().EndsWith(TEXT("L_MoonDome")));
}

void UMechVacuum::Deinitialize()
{
	Apply(false);  // 衰减是共享资源：离开地图一定还原
	Super::Deinitialize();
}

void UMechVacuum::ScanDomes()
{
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	bScanned = true;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->ActorHasTag(TEXT("MoonAir")))
		{
			continue;
		}
		FDome D;
		D.Center = It->GetActorLocation();
		for (const FName& T : It->Tags)
		{
			const FString S = T.ToString();
			if (S.StartsWith(TEXT("Radius=")))
			{
				D.Radius = FCString::Atof(*S.Mid(7));
			}
			else if (S.StartsWith(TEXT("Height=")))
			{
				D.Height = FCString::Atof(*S.Mid(7));
			}
		}
		Domes.Add(D);
	}
	if (Domes.Num() == 0)
	{
		// 默认：原点的大穹顶。11-02 实测：中心往下打第一下打中的是穹顶顶上的结构（35.8 米），不是地面（玻璃没有碰撞，但顶上的骨架有）。
		// 所以中心那一下当顶，地面取穹顶外墙外 15 米处的地面，顶高 = 两者之差
		FDome D;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MechVacuumFloor), false);
		const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
		FHitResult Top, Ground;
		const bool bTop = World->LineTraceSingleByObjectType(Top, FVector(0.f, 0.f, 50000.f), FVector(0.f, 0.f, -50000.f), Objects, Params);
		const float Out = D.Radius + 1500.f;
		const bool bGround = World->LineTraceSingleByObjectType(Ground, FVector(Out, 0.f, 50000.f), FVector(Out, 0.f, -50000.f), Objects, Params);
		if (bGround)
		{
			D.Center = FVector(0.f, 0.f, Ground.ImpactPoint.Z);
			if (bTop && Top.ImpactPoint.Z - Ground.ImpactPoint.Z > 900.f)
			{
				D.Height = Top.ImpactPoint.Z - Ground.ImpactPoint.Z + 200.f;
			}
		}
		else if (bTop)
		{
			D.Center = FVector(0.f, 0.f, Top.ImpactPoint.Z - D.Height);
		}
		Domes.Add(D);
	}
	for (const FDome& D : Domes)
	{
		UE_LOG(LogTemp, Display, TEXT("[vacuum] 有空气的穹顶：中心 %s，半径 %.0f 米，顶高 %.0f 米"), *D.Center.ToCompactString(), D.Radius / 100.f, D.Height / 100.f);
	}
}

bool UMechVacuum::HasAir(const FVector& P)
{
	if (!bScanned)
	{
		ScanDomes();
	}
	for (const FDome& D : Domes)
	{
		const float R = FVector::Dist2D(P, D.Center);
		if (R >= D.Radius)
		{
			continue;
		}
		// 半球顶：中心最高，往外按圆弧降到墙边（墙边至少 9 米）
		const float Top = D.Center.Z + FMath::Max(900.f, D.Height * FMath::Sqrt(FMath::Max(0.f, 1.f - FMath::Square(R / D.Radius))));
		if (P.Z < Top && P.Z > D.Center.Z - 1000.f)
		{
			return true;
		}
	}
	return false;
}

void UMechVacuum::MuffleNew()
{
	for (TObjectIterator<USoundAttenuation> It; It; ++It)
	{
		USoundAttenuation* A = *It;
		if (!A || A->HasAnyFlags(RF_ClassDefaultObject) || Original.Contains(A))
		{
			continue;
		}
		Original.Add(A, A->Attenuation);
		FSoundAttenuationSettings& S = A->Attenuation;
		if (!S.bAttenuate)
		{
			continue;  // 不随距离衰减的（当 2D 用的）不动
		}
		S.AttenuationShape = EAttenuationShape::Sphere;
		S.AttenuationShapeExtents = FVector(NearRadius, 0.f, 0.f);
		S.FalloffDistance = NearFalloff;
		// 越远越闷：贴身的还清楚一点，到 NearRadius 已经很闷
		S.bAttenuateWithLPF = true;
		S.LPFRadiusMin = 0.f;
		S.LPFRadiusMax = NearRadius;
		S.LPFFrequencyAtMin = 4000.f;
		S.LPFFrequencyAtMax = 900.f;
	}
}

void UMechVacuum::Apply(bool bOn)
{
	if (bOn)
	{
		MuffleNew();
		return;
	}
	for (TPair<TWeakObjectPtr<USoundAttenuation>, FSoundAttenuationSettings>& P : Original)
	{
		if (USoundAttenuation* A = P.Key.Get())
		{
			A->Attenuation = P.Value;
		}
	}
	Original.Reset();
}

void UMechVacuum::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	FVector Ear, Front, Right;
	PC->GetAudioListenerPosition(Ear, Front, Right);
	const bool bNow = !HasAir(Ear);
	if (bNow != bVacuum)
	{
		bVacuum = bNow;
		Apply(bVacuum);
		RescanClock = 2.f;
		if (VacuumLog())
		{
			UE_LOG(LogTemp, Display, TEXT("[vacuum] 听者 %s %s（改了 %d 个衰减设置）"), *Ear.ToCompactString(), bVacuum ? TEXT("出了穹顶：真空，远处听不见") : TEXT("进了穹顶：有空气"),
				Original.Num());
		}
	}
	else if (bVacuum)
	{
		// 后加载的声音（刚换的配装、刚出现的特效）也一起改
		RescanClock -= DeltaTime;
		if (RescanClock <= 0.f)
		{
			RescanClock = 2.f;
			MuffleNew();
		}
	}
}

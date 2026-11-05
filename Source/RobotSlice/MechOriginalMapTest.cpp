// -MechOriginalMapTest：在两张新图上做无渲染检查，覆盖实际场景碰撞、真空与循环子系统。
#include "MechVacuum.h"
#include "MechLoop.h"
#include "MechMaps.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"

namespace MechOriginalMapChecks
{
	bool Run(float DeltaSeconds)
	{
		// Ticker 的 DeltaSeconds 是当前帧时长，不是 .5 秒回调间隔；NullRHI 下累加会等很久。
		static const double StartedAt = FPlatformTime::Seconds();
		const double Elapsed = FPlatformTime::Seconds() - StartedAt;
		UWorld* World = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* Candidate = Context.World();
				if (Candidate && Candidate->IsGameWorld() && Candidate->HasBegunPlay() && Candidate->GetFirstPlayerController()
					&& Candidate->GetFirstPlayerController()->GetPawn()) { World = Candidate; break; }
			}
		}
		if (!World || Elapsed < 2.f)
		{
			if (Elapsed < 60.f) return true;
			UE_LOG(LogTemp, Error, TEXT("[originalmaps] game world unavailable"));
			FPlatformMisc::RequestExitWithStatus(false, 1); return false;
		}
		int32 Checks = 0, Failures = 0;
		FString Details;
		auto Verify = [&](bool Passed, const FString& Detail) {
			++Checks;
			if (!Passed) { ++Failures; Details += Detail + TEXT("; "); UE_LOG(LogTemp, Error, TEXT("[originalmaps] %s"), *Detail); }
		};
		Verify(MechMaps::Num() == 11, TEXT("expected 9 references plus 2 new maps"));
		Verify(MechMaps::FindByMapName(World->GetMapName()) >= 9, TEXT("new map menu identity"));
		const bool bMoon = World->GetMapName().EndsWith(TEXT("L_MoonDome"));
		FCollisionQueryParams Query(SCENE_QUERY_STAT(OriginalMapChecks), false);
		Query.AddIgnoredActor(World->GetFirstPlayerController()->GetPawn());
		if (bMoon)
		{
			Verify(FMath::IsNearlyEqual(World->GetGravityZ(), -327.f), TEXT("moon gravity"));
			UMechVacuum* Vacuum = World->GetSubsystem<UMechVacuum>();
			Verify(Vacuum != nullptr, TEXT("vacuum subsystem created"));
			AActor* Shield = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It) if (It->ActorHasTag(TEXT("MoonShield"))) Shield = *It;
			Verify(Shield != nullptr, TEXT("shield actor exists"));
			if (Shield)
			{
				const FVector Center = Shield->GetActorLocation();
				if (Vacuum)
				{
					Verify(Vacuum->HasAir(Center + FVector(0, 0, 300)), TEXT("air inside"));
					Verify(!Vacuum->HasAir(Center + FVector(0, 0, 4000)), TEXT("vacuum above dome"));
					Verify(!Vacuum->HasAir(Center + FVector(6500, 0, 300)), TEXT("vacuum outside rim"));
					Verify(Vacuum->HasAir(Center + FVector(6100, 0, 500)), TEXT("air in low rim skirt"));
				}
				FCollisionObjectQueryParams Objects;
				Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic); Objects.AddObjectTypesToQuery(ECC_Pawn);
				for (int32 Side = 0; Side < 4; ++Side)
				{
					const float Angle = Side * PI * .5f;
					const FVector Axis(FMath::Cos(Angle), FMath::Sin(Angle), 0);
					const FVector Inside = Center + Axis * 4800.f + FVector(0, 0, 1500);
					const FVector Outside = Center + Axis * 7500.f + FVector(0, 0, 1500);
					for (int32 Direction = 0; Direction < 2; ++Direction)
					{
						const FVector Start = Direction ? Outside : Inside, End = Direction ? Inside : Outside;
						FHitResult Hit;
						Verify(World->LineTraceSingleByObjectType(Hit, Start, End, Objects, Query) && Hit.GetActor() == Shield,
							FString::Printf(TEXT("hitscan blocked side=%d direction=%d hit=%s"), Side, Direction, *GetNameSafe(Hit.GetActor())));
						Verify(World->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, Objects, FCollisionShape::MakeSphere(18.f), Query) && Hit.GetActor() == Shield,
							FString::Printf(TEXT("projectile blocked side=%d direction=%d"), Side, Direction));
						Verify(!World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(85.f, 220.f), Query),
							FString::Printf(TEXT("robot can cross side=%d direction=%d hit=%s"), Side, Direction, *GetNameSafe(Hit.GetActor())));
					}
				}
			}
		}
		else
		{
			Verify(World->GetMapName().EndsWith(TEXT("L_MillenniumStreet")), TEXT("expected millennium map"));
			Verify(MechLoop::IsActive(World), TEXT("loop subsystem active"));
			Verify(FMath::IsNearlyEqual(MechLoop::Period(World), 10000.f), TEXT("100m period"));
			Verify(FMath::IsNearlyEqual(MechLoop::Seam(World), 5000.f), TEXT("50m seam"));
			Verify(FMath::IsNearlyEqual(MechLoop::WrapShift(World, 5200.f, 0.f), -10000.f), TEXT("forward seam wraps"));
			Verify(FMath::IsNearlyEqual(MechLoop::WrapShift(World, -5200.f, 0.f), 10000.f), TEXT("backward seam wraps"));
			Verify(FMath::IsNearlyEqual(MechLoop::Dist(World, FVector(4900,0,300), FVector(-4900,0,300)), 200.f), TEXT("combat distance crosses seam"));
			// 接缝两侧的真实地面必须连续，避免传送后跌落。
			FCollisionObjectQueryParams GroundObjects(ECC_WorldStatic);
			for (float X : {-5100.f, -4900.f, 4900.f, 5100.f})
			{
				FHitResult Ground;
				Verify(World->LineTraceSingleByObjectType(Ground, FVector(X,200,400), FVector(X,200,-200), GroundObjects, Query)
					&& FMath::Abs(Ground.ImpactPoint.Z) < 8.f, FString::Printf(TEXT("seam floor X=%.0f"), X));
			}
		}
		const FString Folder = FPaths::ProjectSavedDir() / TEXT("Automation");
		IFileManager::Get().MakeDirectory(*Folder, true);
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"checks\":%d,\"failures\":%d,\"detail\":\"%s\"}"), Checks, Failures, *Details),
			*(Folder / (bMoon ? TEXT("MoonDomeRuntime.json") : TEXT("MillenniumStreetRuntime.json"))));
		UE_LOG(LogTemp, Display, TEXT("[originalmaps] %s checks=%d failures=%d"), Failures ? TEXT("FAIL") : TEXT("PASS"), Checks, Failures);
		FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0); return false;
	}
	FDelayedAutoRegisterHelper Register(EDelayedRegisterRunPhase::EndOfEngineInit, [] {
		if (FParse::Param(FCommandLine::Get(), TEXT("MechOriginalMapTest")))
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Run), .5f);
	});
}

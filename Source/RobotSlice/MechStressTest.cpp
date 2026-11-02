// -MechStressTest（10-31 保卫战第 0 步：压力测试）。用测试图跑：
//   UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Defense?Range" -game -MechStressTest -RenderOffscreen -ForceRes -ResX=2880 -ResY=1620 -windowed
// 测试场模式进图（玩家一台，木桩去掉），玩家不死、站着不动，镜头一直朝着敌人堆；按档位刷敌方 AI 机甲（一起冲玩家打）和无人机群（AMechDroneSwarm），
// 每档 14 秒：前 6 秒等刷完、稳定（新机体第一次出场有加载卡顿），后 8 秒量帧时间（平均、最慢 1%、游戏线程 / 渲染线程 / GPU）。
// 显存不在游戏里量（引擎的贴图统计不含网格、渲染目标），跑的时候另开 nvidia-smi 每 2 秒记一次，按时间对上档位。日志 [stress]，最后一行汇总，跑完退出。
#include "MechGameMode.h"

#include "EngineUtils.h"
#include "MechAIController.h"
#include "MechCharacter.h"
#include "MechDroneSwarm.h"
#include "MechHealthComponent.h"
#include "MechLoadout.h"
#include "MechWeapons.h"
#include "Misc/CommandLine.h"
#include "RHI.h"
#include "RenderTimer.h"

namespace MechStressLocal
{
	struct FStep
	{
		int32 Mechs;
		int32 Drones;
	};
	// 先单独加机甲，再在 6 台机甲上加无人机，最后两档组合
	const FStep Steps[] = {{0, 0}, {6, 0}, {10, 0}, {14, 0}, {6, 20}, {6, 50}, {6, 100}, {10, 50}, {14, 100}};
	constexpr float StepTime = 14.f;
	constexpr float MeasureFrom = 6.f;
	constexpr float StartDelay = 4.f;

	struct FResult
	{
		float AvgMs = 0.f;
		float P99Ms = 0.f;
		float WorstMs = 0.f;
		float GT = 0.f;
		float RT = 0.f;
		float GPU = 0.f;
		int32 Alive = 0;
	};
	TArray<float> Frames;
	double SumGT = 0.0;
	double SumRT = 0.0;
	double SumGPU = 0.0;
	FResult Results[UE_ARRAY_COUNT(Steps)];
	TArray<TWeakObjectPtr<AMechCharacter>> Enemies;
	TWeakObjectPtr<AMechDroneSwarm> Swarm;
	int32 SpawnSerial = 0;

	FVector GroundAt(UWorld* World, const FVector& P)
	{
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(MechStressGround), false);
		if (World->LineTraceSingleByObjectType(Hit, P + FVector(0.f, 0.f, 20000.f), P - FVector(0.f, 0.f, 20000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q))
		{
			return Hit.ImpactPoint;
		}
		return P;
	}
}

void AMechPlayerController::StressTestTick(float DeltaTime)
{
	using namespace MechStressLocal;
	AMechCharacter* Me = Cast<AMechCharacter>(GetPawn());
	if (!Me || !HasAuthority())
	{
		return;
	}
	UWorld* World = GetWorld();
	const float T0 = StressTestClock;
	StressTestClock += DeltaTime;
	const float T = StressTestClock;
	if (T0 <= 0.f)
	{
		// 开场：去掉测试场的木桩，玩家不死
		for (TActorIterator<AMechCharacter> It(World); It; ++It)
		{
			if (It->IsTrainingDummy())
			{
				It->Destroy();
			}
		}
		Me->GetHealth()->bNoDeath = true;
		UE_LOG(LogTemp, Display, TEXT("[stress] 开始：%d 档，每档 %.0f 秒（后 %.0f 秒量）"), (int32)UE_ARRAY_COUNT(Steps), StepTime, StepTime - MeasureFrom);
	}
	if (T < StartDelay)
	{
		return;
	}
	const int32 NumSteps = UE_ARRAY_COUNT(Steps);
	const float L = T - StartDelay;
	const int32 Index = FMath::FloorToInt(L / StepTime);
	const float InStep = L - Index * StepTime;
	const float InStepPrev = (T0 - StartDelay) - Index * StepTime;
	if (Index >= NumSteps)
	{
		if (Index == NumSteps && InStepPrev < 0.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[stress] 汇总（毫秒；帧率 = 1000 / 平均）："));
			for (int32 i = 0; i < NumSteps; ++i)
			{
				const FResult& R = Results[i];
				UE_LOG(LogTemp, Display, TEXT("[stress]   机甲 %2d 无人机 %3d：%5.1f 帧/秒 平均 %.1f 最慢1%% %.1f 最慢 %.0f | 游戏线程 %.1f 渲染线程 %.1f GPU %.1f | 存活 %d"),
					Steps[i].Mechs, Steps[i].Drones, R.AvgMs > 0.f ? 1000.f / R.AvgMs : 0.f, R.AvgMs, R.P99Ms, R.WorstMs, R.GT, R.RT, R.GPU, R.Alive);
			}
			ConsoleCommand(TEXT("quit"));
		}
		return;
	}
	const FStep& S = Steps[Index];
	// 档位开始：增减机甲和无人机
	if (InStepPrev < 0.f)
	{
		Enemies.RemoveAll([](const TWeakObjectPtr<AMechCharacter>& E) { return !E.IsValid(); });
		while (Enemies.Num() > S.Mechs)
		{
			AMechCharacter* E = Enemies.Pop().Get();
			if (AController* C = E->GetController())
			{
				C->Destroy();
			}
			E->Destroy();
		}
		while (Enemies.Num() < S.Mechs)
		{
			// 玩家周围 45–90 米一圈，各种框架和武器轮着来（显存要算上不同的机体和武器模型）
			const float Ang = FMath::FRandRange(0.f, 2.f * PI);
			const float Dist = FMath::FRandRange(4500.f, 9000.f);
			const FVector P = GroundAt(World, Me->GetActorLocation() + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * Dist);
			const FTransform At(FRotator(0.f, FMath::RadiansToDegrees(Ang) + 180.f, 0.f), P + FVector(0.f, 0.f, 96.f * 1.35f + 30.f));
			AMechCharacter* E = World->SpawnActorDeferred<AMechCharacter>(AMechCharacter::StaticClass(), At, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
			if (!E)
			{
				break;
			}
			E->SetTeam(1);
			E->DisplayName = FString::Printf(TEXT("敌 %d"), ++SpawnSerial);
			E->FinishSpawning(At);
			FMechLoadout Lo = E->GetLoadout();
			Lo.Frame = static_cast<EMechFrame>(SpawnSerial % MechNumFrames);
			Lo.Shooter = static_cast<EMechShooter>(SpawnSerial % MechWeapons::NumGuns());
			Lo.Melee = static_cast<EMechMeleeWeapon>((SpawnSerial / 2) % MechWeapons::NumBlades());
			E->SetLoadout(Lo, true);
			if (AMechAIController* AI = Cast<AMechAIController>(E->GetController()))
			{
				AI->ArenaCenter = Me->GetActorLocation();
				AI->ArenaExtent = FVector2D(15000.f, 15000.f);
			}
			Enemies.Add(E);
		}
		if (S.Drones > 0 && !Swarm.IsValid())
		{
			Swarm = World->SpawnActor<AMechDroneSwarm>(AMechDroneSwarm::StaticClass(), Me->GetActorTransform());
			Swarm->SetTarget(Me);
		}
		if (Swarm.IsValid())
		{
			Swarm->SetCount(S.Drones);
		}
		Frames.Reset();
		SumGT = SumRT = SumGPU = 0.0;
		UE_LOG(LogTemp, Display, TEXT("[stress] 第 %d 档开始（%s）：机甲 %d 无人机 %d"), Index + 1, *FDateTime::Now().ToString(TEXT("%H:%M:%S")), S.Mechs, S.Drones);
	}
	// 镜头朝敌人堆（机甲 + 无人机的中心），屏幕上东西越多越接近最坏情况
	FVector Sum = FVector::ZeroVector;
	int32 N = 0;
	for (const TWeakObjectPtr<AMechCharacter>& E : Enemies)
	{
		if (E.IsValid())
		{
			Sum += E->GetActorLocation();
			++N;
		}
	}
	if (N > 0)
	{
		const FRotator Want = ((Sum / N) - Me->GetActorLocation()).Rotation();
		SetControlRotation(FMath::RInterpTo(GetControlRotation(), FRotator(FMath::Clamp(Want.Pitch, -20.f, 10.f), Want.Yaw, 0.f), DeltaTime, 2.f));
	}
	if (InStep >= MeasureFrom)
	{
		Frames.Add(DeltaTime * 1000.f);
		SumGT += FPlatformTime::ToMilliseconds(GGameThreadTime);
		SumRT += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		SumGPU += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	}
	// 档位最后一帧：记结果
	if (InStep + DeltaTime >= StepTime && Frames.Num() > 0)
	{
		FResult& R = Results[Index];
		TArray<float> Sorted = Frames;
		Sorted.Sort();
		float Total = 0.f;
		for (float F : Sorted)
		{
			Total += F;
		}
		const int32 M = Sorted.Num();
		R.AvgMs = Total / M;
		R.P99Ms = Sorted[FMath::Clamp(FMath::FloorToInt(M * 0.99f), 0, M - 1)];
		R.WorstMs = Sorted.Last();
		R.GT = SumGT / M;
		R.RT = SumRT / M;
		R.GPU = SumGPU / M;
		R.Alive = N;
		UE_LOG(LogTemp, Display, TEXT("[stress] 第 %d 档（机甲 %d 无人机 %d）：%.1f 帧/秒 平均 %.1f 最慢1%% %.1f 最慢 %.0f 毫秒 | 游戏线程 %.1f 渲染线程 %.1f GPU %.1f | 存活机甲 %d 无人机这一秒开火 %d"),
			Index + 1, S.Mechs, S.Drones, 1000.f / R.AvgMs, R.AvgMs, R.P99Ms, R.WorstMs, R.GT, R.RT, R.GPU, N, Swarm.IsValid() ? Swarm->ShotsLastSecond : 0);
		Frames.Reset();
	}
}

// 画面问题复现测试（说明见 MechVisualTest.h）。用法（都要 -RenderOffscreen，测试场必须带 ?Range）：
// UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Range?Range" -game -MechFallTest -MechAnimLog -benchmark -fps=30 -RenderOffscreen -ForceRes -ResX=1280 -ResY=720 -windowed
// UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Range?Range" -game -MechTracerTest -RenderOffscreen -ForceRes -ResX=1600 -ResY=900 -windowed

#include "MechVisualTest.h"

#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechMovementComponent.h"

namespace MechVisualTestState  // 不用匿名命名空间：合并编译时会和别的文件里同名的局部变量撞
{
	float Clock = 0.f;
	bool bSetup = false;
	bool bQuit = false;
	// 下落测试
	int32 Scene = -1;
	float EventAt = -1.f;   // 这一段「开始下落」的时刻（段内秒数），-1 = 还没发生
	bool bWasFalling = false;
	float LogClock = 0.f;
	// 曳光测试
	int32 Phase = -1;
	int32 OffKind = 0;
	TMap<FString, int32> SavedCVars;
}

void FMechVisualTest::Tick(AMechCharacter* Me, float DeltaSeconds)
{
	static const bool bFall = FParse::Param(FCommandLine::Get(), TEXT("MechFallTest"));
	static const bool bTracer = FParse::Param(FCommandLine::Get(), TEXT("MechTracerTest"));
	if ((!bFall && !bTracer) || !Me->IsLocallyControlled() || !Me->IsPlayerControlled() || Me->IsTrainingDummy())
	{
		return;
	}
	if (bFall)
	{
		FallTick(Me, DeltaSeconds);
	}
	else
	{
		TracerTick(Me, DeltaSeconds);
	}
}

namespace
{
	/** 两腿的几何量：左右腿各三段（大腿、小腿、脚）两两最近距离、膝 / 脚左右间距（负 = 交叉）、膝弯曲、大腿抬起角、小腿离躯干最近距离（世界厘米） */
	FString LegMetrics(const AMechCharacter* Me, float& OutMinGap)
	{
		const USkeletalMeshComponent* M = Me->GetMesh();
		auto B = [M](const TCHAR* N) { return M->GetBoneLocation(FName(N)); };
		const FVector Leg[2][4] = {
			{B(TEXT("thigh_l")), B(TEXT("calf_l")), B(TEXT("foot_l")), B(TEXT("ball_l"))},
			{B(TEXT("thigh_r")), B(TEXT("calf_r")), B(TEXT("foot_r")), B(TEXT("ball_r"))},
		};
		const FVector Pelvis = B(TEXT("pelvis"));
		const FVector Chest = B(TEXT("spine_05"));
		static const TCHAR* SegName[3] = {TEXT("大腿"), TEXT("小腿"), TEXT("脚")};
		float Best = TNumericLimits<float>::Max();
		int32 BestA = 0, BestB = 0;
		for (int32 a = 0; a < 3; ++a)
		{
			for (int32 b = 0; b < 3; ++b)
			{
				FVector PA, PB;
				FMath::SegmentDistToSegmentSafe(Leg[0][a], Leg[0][a + 1], Leg[1][b], Leg[1][b + 1], PA, PB);
				const float D = FVector::Dist(PA, PB);
				if (D < Best)
				{
					Best = D;
					BestA = a;
					BestB = b;
				}
			}
		}
		OutMinGap = Best;
		const FVector Left = -Me->GetActorRightVector();
		const float KneeGap = (Leg[0][1] - Leg[1][1]) | Left;
		const float FootGap = (Leg[0][2] - Leg[1][2]) | Left;
		auto Angle = [](const FVector& U, const FVector& V) {
			return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(U.GetSafeNormal() | V.GetSafeNormal(), -1.f, 1.f)));
		};
		const FVector SpineDown = Pelvis - Chest;
		float CalfTorso[2];
		for (int32 i = 0; i < 2; ++i)
		{
			FVector PA, PB;
			FMath::SegmentDistToSegmentSafe(Leg[i][1], Leg[i][2], Pelvis, Chest, PA, PB);
			CalfTorso[i] = FVector::Dist(PA, PB);
		}
		return FString::Printf(TEXT("两腿最近 %.1f（左%s-右%s） 膝间距 %.1f 脚间距 %.1f 膝弯 左%.0f 右%.0f 大腿抬 左%.0f 右%.0f 小腿离躯干 左%.1f 右%.1f"),
			Best, SegName[BestA], SegName[BestB], KneeGap, FootGap,
			Angle(Leg[0][1] - Leg[0][0], Leg[0][2] - Leg[0][1]), Angle(Leg[1][1] - Leg[1][0], Leg[1][2] - Leg[1][1]),
			Angle(Leg[0][1] - Leg[0][0], SpineDown), Angle(Leg[1][1] - Leg[1][0], SpineDown), CalfTorso[0], CalfTorso[1]);
	}
}

void FMechVisualTest::FallTick(AMechCharacter* Me, float Dt)
{
	using namespace MechVisualTestState;
	APlayerController* PC = Cast<APlayerController>(Me->GetController());
	if (!PC || bQuit)
	{
		return;
	}
	UMechMovementComponent* Move = Me->GetMechMovement();
	static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("MechAnimOldFall"));
	if (!bSetup)
	{
		bSetup = true;
		// 腿部特写：镜头在机体右前方、略低，不跟随延迟、不做碰撞
		Me->GetCameraBoom()->bEnableCameraLag = false;
		Me->GetCameraBoom()->bEnableCameraRotationLag = false;
		Me->GetCameraBoom()->bDoCollisionTest = false;
		Me->GetCameraBoom()->TargetArmLength = 420.f;
		Me->GetCameraBoom()->SocketOffset = Me->BaseSocketOffset = FVector::ZeroVector;
		Me->GetCameraBoom()->TargetOffset = FVector(0.f, 0.f, -60.f);
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		// 配装固定：均衡型推进器（能喷射、空中冲刺；测试场存着的可能是地面型）、步枪
		FMechLoadout Want = Me->GetLoadout();
		Want.Booster = EMechBooster::Balanced;
		Want.Shooter = EMechShooter::Rifle;
		Me->SetLoadout(Want, true);
		UE_LOG(LogTemp, Display, TEXT("[falltest] 开始（%s）"), bOld ? TEXT("旧混法") : TEXT("新混法"));
	}
	Clock += Dt;
	if (Clock < 2.f)
	{
		return;  // 等木桩生成、开局稳定
	}
	// 每段：起点（厘米，脚下）、朝向、做什么（0 站立起跳 1 跑步起跳 2 冲刺起跳 3 跑下高台 4 冲刺跑下高台 5 喷射结束 6 滑翔结束）。
	// 都沿 -Y 走：测试场 x = 0 这条线上没有掩体和木桩；高台 (35, 24) 米、12 米见方、顶面 10 米，中间站着一个木桩，从 x = 31 米往 -Y 跑下去
	struct FScene
	{
		const TCHAR* Name;
		FVector Start;
		int32 Kind;
	};
	static const FScene Scenes[] = {
		{TEXT("A_站立起跳"), FVector(0.f, 0.f, 0.f), 0},
		{TEXT("B_跑步起跳"), FVector(0.f, 3000.f, 0.f), 1},
		{TEXT("C_冲刺起跳"), FVector(0.f, 4500.f, 0.f), 2},
		{TEXT("D_跑下高台"), FVector(3100.f, 2350.f, 1000.f), 3},
		{TEXT("E_冲刺跑下高台"), FVector(3100.f, 2600.f, 1000.f), 4},
		{TEXT("F_喷射结束"), FVector(0.f, -2000.f, 0.f), 5},
		{TEXT("G_滑翔结束"), FVector(0.f, -4000.f, 0.f), 6},
	};
	constexpr float Slot = 4.5f;
	const int32 Index = FMath::FloorToInt((Clock - 2.f) / Slot);
	const float L = Clock - 2.f - Index * Slot;
	const float L0 = L - Dt;
	auto Crossed = [&](float At) { return L0 < At && L >= At; };
	if (Index >= UE_ARRAY_COUNT(Scenes))
	{
		bQuit = true;
		UE_LOG(LogTemp, Display, TEXT("[falltest] 结束"));
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	const FScene& S = Scenes[Index];
	const FVector Dir(0.f, -1.f, 0.f);
	if (Index != Scene)
	{
		Scene = Index;
		EventAt = -1.f;
		Me->JumpReleased();
		Move->bWantsBoost = false;
		Move->StopMovementImmediately();
		Move->Energy = Move->MaxEnergy;
		Me->SetActorLocation(S.Start + FVector(0.f, 0.f, Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
		Me->SetActorRotation(Dir.Rotation());
	}
	bool bMove = false;
	bool bBoost = false;
	switch (S.Kind)
	{
	case 0:
		if (Crossed(1.f))
		{
			Me->JumpPressed();
			EventAt = 1.f;
		}
		if (Crossed(1.08f))
		{
			Me->JumpReleased();
		}
		break;
	case 1:
	case 2:
		bMove = L > 0.2f && L < 2.4f;
		bBoost = S.Kind == 2 && bMove;
		if (Crossed(1.4f))
		{
			Me->JumpPressed();
			EventAt = 1.4f;
		}
		if (Crossed(1.48f))
		{
			Me->JumpReleased();
		}
		break;
	case 3:
	case 4:
		bMove = L > 0.3f && L < 2.6f;
		bBoost = S.Kind == 4 && bMove;
		if (EventAt < 0.f && L > 0.35f && Move->IsFalling() && !bWasFalling)
		{
			EventAt = L;  // 走出台边、刚离地
		}
		break;
	case 5:
		if (Crossed(0.4f))
		{
			Me->JumpPressed();
		}
		if (Crossed(1.5f))
		{
			Me->JumpReleased();
			EventAt = 1.5f;  // 松开喷射，开始往下掉
		}
		break;
	case 6:
		if (Crossed(0.3f))
		{
			Me->JumpPressed();
		}
		if (Crossed(0.75f))
		{
			Me->JumpReleased();
		}
		bMove = bBoost = L > 0.75f && L < 1.9f;
		if (Crossed(1.9f))
		{
			EventAt = 1.9f;  // 松开冲刺和方向，滑翔结束开始下落
		}
		break;
	default:
		break;
	}
	if (bMove)
	{
		Me->AddMovementInput(Dir, 1.f);
	}
	Move->bWantsBoost = bBoost;
	bWasFalling = Move->IsFalling();
	// 镜头在机体右前方（跑下高台那两段在左前方，右边是高台）；枪照常朝机体正前方端着——不然上身会拧向身后的镜头方向（实际游戏里镜头在背后）
	PC->SetControlRotation(FRotator(-4.f, Me->GetActorRotation().Yaw + (S.Kind == 3 || S.Kind == 4 ? 145.f : 215.f), 0.f));
	Me->GetCombat()->AimOverride = Me->GetActorForwardVector();

	// 记录：事件前后每帧一行，其余每 0.1 秒一行；事件后 0–0.2 秒每帧截图，之后 0.3 / 0.45 / 0.6 / 0.8 秒各一张
	const float Since = EventAt >= 0.f ? L - EventAt : -1.f;
	const bool bNear = EventAt >= 0.f && Since >= 0.f && Since <= 1.f;
	LogClock += Dt;
	if (bNear || LogClock >= 0.1f)
	{
		LogClock = 0.f;
		float Gap = 0.f;
		const FString Legs = LegMetrics(Me, Gap);
		const FVector V = Me->GetVelocity();
		UE_LOG(LogTemp, Display, TEXT("[falltest] %s 段内 %.3f 事件后 %.3f 空中 %d 冲刺 %d 喷射 %d 速度 水平 %.0f 上 %.0f | %s"), S.Name, L, Since,
			Move->IsFalling() ? 1 : 0, Me->IsBoosting() ? 1 : 0, Me->IsThrustingUp() ? 1 : 0, V.Size2D(), V.Z, *Legs);
	}
	if (EventAt >= 0.f)
	{
		const float Since0 = L0 - EventAt;
		bool bShot = Since >= 0.f && Since <= 0.2f;
		for (const float At : {0.3f, 0.45f, 0.6f, 0.8f})
		{
			bShot |= Since0 < At && Since >= At;
		}
		if (bShot)
		{
			static const bool bNoLimit = FParse::Param(FCommandLine::Get(), TEXT("MechNoLegLimit"));
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Fall") /
				FString::Printf(TEXT("%s%s_%03d.png"), bNoLimit ? TEXT("无限位_") : bOld ? TEXT("旧_") : TEXT(""), S.Name, FMath::RoundToInt(Since * 1000.f));
			FScreenshotRequest::RequestScreenshot(Path, false, false);
		}
	}
}

void FMechVisualTest::TracerTick(AMechCharacter* Me, float Dt)
{
	using namespace MechVisualTestState;
	APlayerController* PC = Cast<APlayerController>(Me->GetController());
	if (!PC || bQuit)
	{
		return;
	}
	UMechCombatComponent* Combat = Me->GetCombat();
	static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("MechTracerOld"));
	if (!bSetup)
	{
		bSetup = true;
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;  // 准星、读数会挡住曳光
		}
		UE_LOG(LogTemp, Display, TEXT("[tracertest] 开始（%s）"), bOld ? TEXT("旧曳光") : TEXT("新曳光"));
	}
	Clock += Dt;
	if (Clock < 4.f)
	{
		return;  // 等木桩生成、开局 Lumen 稳定
	}
	// 每段 4 秒：0.5 秒后开火，边开火边左右转镜头（±12 度、1.6 秒一个来回）并横移（1 秒换向）；第 1.3 / 2.0 / 2.7 / 3.4 秒各截一张。
	// 关掉的东西：0 不关 1 动态模糊 2 TSR（时域抗锯齿 / 放大）3 Lumen 漫反射和反射 4 粒子（枪口焰、命中火花）5 曳光 / 光束本身
	struct FPhase
	{
		const TCHAR* Name;
		EMechShooter Gun;
		int32 Off;
		bool bMove;
	};
	static const FPhase Phases[] = {
		{TEXT("步枪_1基线"), EMechShooter::Rifle, 0, true},
		{TEXT("步枪_2关动态模糊"), EMechShooter::Rifle, 1, true},
		{TEXT("步枪_3关TSR"), EMechShooter::Rifle, 2, true},
		{TEXT("步枪_4关Lumen"), EMechShooter::Rifle, 3, true},
		{TEXT("步枪_5关粒子"), EMechShooter::Rifle, 4, true},
		{TEXT("步枪_6关曳光"), EMechShooter::Rifle, 5, true},
		{TEXT("步枪_7不动"), EMechShooter::Rifle, 0, false},
		{TEXT("冲锋枪_1基线"), EMechShooter::SMG, 0, true},
		{TEXT("冲锋枪_2关动态模糊"), EMechShooter::SMG, 1, true},
		{TEXT("充能_1基线"), EMechShooter::Marksman, 0, true},
		{TEXT("充能_2关动态模糊"), EMechShooter::Marksman, 1, true},
		{TEXT("充能_3关TSR"), EMechShooter::Marksman, 2, true},
		{TEXT("充能_4关光束"), EMechShooter::Marksman, 5, true},
	};
	constexpr float Slot = 4.f;
	const int32 Index = FMath::FloorToInt((Clock - 4.f) / Slot);
	const float L = Clock - 4.f - Index * Slot;
	const float L0 = L - Dt;
	auto Crossed = [&](float At) { return L0 < At && L >= At; };
	auto SetCVar = [](const TCHAR* Name, int32 Value) {
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (!SavedCVars.Contains(Name))
			{
				SavedCVars.Add(Name, V->GetInt());
			}
			V->Set(Value, ECVF_SetByConsole);
		}
	};
	auto RestoreCVar = [](const TCHAR* Name) {
		if (const int32* Was = SavedCVars.Find(Name))
		{
			if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				V->Set(*Was, ECVF_SetByConsole);
			}
		}
	};
	auto HideTracers = [Me](bool bHide) {
		TInlineComponentArray<UInstancedStaticMeshComponent*> Comps(Me);
		for (UInstancedStaticMeshComponent* C : Comps)
		{
			const FName N = C->GetFName();
			if (N == TEXT("Tracers") || N == TEXT("ChargeLaser") || N == TEXT("ChargeBeamCore") || N == TEXT("ChargeBeamGlow"))
			{
				C->SetHiddenInGame(bHide);
			}
		}
	};
	auto SetOff = [&](int32 Kind, bool bOff) {
		switch (Kind)
		{
		case 1: bOff ? SetCVar(TEXT("r.MotionBlurQuality"), 0) : RestoreCVar(TEXT("r.MotionBlurQuality")); break;
		case 2: bOff ? SetCVar(TEXT("r.AntiAliasingMethod"), 0) : RestoreCVar(TEXT("r.AntiAliasingMethod")); break;
		case 3:
			bOff ? SetCVar(TEXT("r.Lumen.DiffuseIndirect.Allow"), 0) : RestoreCVar(TEXT("r.Lumen.DiffuseIndirect.Allow"));
			bOff ? SetCVar(TEXT("r.Lumen.Reflections.Allow"), 0) : RestoreCVar(TEXT("r.Lumen.Reflections.Allow"));
			break;
		case 4: PC->ConsoleCommand(bOff ? TEXT("ShowFlag.Particles 0") : TEXT("ShowFlag.Particles 2")); break;  // 2 = 恢复默认
		case 5: HideTracers(bOff); break;
		default: break;
		}
	};
	if (Index >= UE_ARRAY_COUNT(Phases))
	{
		SetOff(OffKind, false);
		Combat->SetTrigger(false);
		bQuit = true;
		UE_LOG(LogTemp, Display, TEXT("[tracertest] 结束"));
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	const FPhase& P = Phases[Index];
	if (Index != Phase)
	{
		Phase = Index;
		SetOff(OffKind, false);
		OffKind = P.Off;
		SetOff(OffKind, true);
		Combat->SetTrigger(false);
		if (Me->GetLoadout().Shooter != P.Gun)
		{
			FMechLoadout Want = Me->GetLoadout();
			Want.Shooter = P.Gun;
			Me->SetLoadout(Want, true);
		}
		Me->SetActorLocation(FVector(0.f, 0.f, Me->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogTemp, Display, TEXT("[tracertest] %s"), P.Name);
	}
	if (Combat->GetStance() != EMechStance::Gun && Combat->GetSwitchProgress() < 0.f)
	{
		Combat->SwitchStance();
	}
	// 对着 25 米那个木桩（25, -4 米）的胸口，左右扫
	AMechCharacter* Dummy = nullptr;
	for (TActorIterator<AMechCharacter> It(Me->GetWorld()); It; ++It)
	{
		if (It->IsTrainingDummy() && (!Dummy || FVector::Dist2D(It->GetActorLocation(), FVector(2500.f, -400.f, 0.f)) < FVector::Dist2D(Dummy->GetActorLocation(), FVector(2500.f, -400.f, 0.f))))
		{
			Dummy = *It;
		}
	}
	if (!Dummy || !PC->PlayerCameraManager)
	{
		return;
	}
	FRotator Aim = (Dummy->GetActorLocation() + FVector(0.f, 0.f, 60.f) - PC->PlayerCameraManager->GetCameraLocation()).Rotation();
	if (P.bMove)
	{
		Aim.Yaw += 12.f * FMath::Sin(L * UE_TWO_PI / 1.6f);
		Me->AddMovementInput(Me->GetActorRightVector(), FMath::Fmod(L, 2.f) < 1.f ? 1.f : -1.f);
	}
	PC->SetControlRotation(Aim);
	Combat->SetTrigger(L > 0.5f && L < 3.9f);
	for (const float At : {1.3f, 2.f, 2.7f, 3.4f})
	{
		if (Crossed(At))
		{
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Tracer") /
				FString::Printf(TEXT("%s%s_%.1f.png"), bOld ? TEXT("旧_") : TEXT(""), P.Name, At);
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			UE_LOG(LogTemp, Display, TEXT("[tracertest] %s 第 %.1f 秒 帧时间 %.1f 毫秒"), P.Name, At, Dt * 1000.f);
		}
	}
}

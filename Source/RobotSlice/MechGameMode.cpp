#include "MechGameMode.h"
#include "MechMapRules.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "MechComms.h"
#include "MechLoadout.h"
#include "MechMaps.h"
#include "MechRoom.h"
#include "MechShoulderComponent.h"
#include "MechGrenade.h"
#include "MechSmoke.h"
#include "MechEffectComponent.h"
#include "MechPluginPod.h"
#include "GameFramework/PlayerState.h"
#include "MechItemStudio.h"
#include "MechMatchIntro.h"
#include "MechResultsStage.h"
#include "MechDraftUI.h"
#include "MechDraft.h"
#include "MechWeapons.h"
#include "MechMeshComponent.h"
#include "MechMovementComponent.h"
#include "MechSettings.h"
#include "Engine/TargetPoint.h"
#include "Widgets/SWeakWidget.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "InputMappingContext.h"
#include "LightingBlueprintLibrary.h"
#include "LightingDirector.h"
#include "Misc/CommandLine.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"
#include "MechAIController.h"
#include "MechCharacter.h"
#include "MechDefense.h"
#include "MechRun.h"
#include "MechCombatComponent.h"
#include "MechCoreArena.h"
#include "MechCoreDirector.h"
#include "MechGameState.h"
#include "MechHealthComponent.h"
#include "MechHUD.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "MechMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "MechMissile.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"

AMechPlayerController::AMechPlayerController()
{
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> DefaultIMC(TEXT("/Game/Input/IMC_Default"));
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> MouseLookIMC(TEXT("/Game/Input/IMC_MouseLook"));
	if (DefaultIMC.Succeeded())
	{
		DefaultMappingContexts.Add(DefaultIMC.Object);
	}
	if (MouseLookIMC.Succeeded())
	{
		MobileExcludedMappingContexts.Add(MouseLookIMC.Object);
	}
	// 11-01 标点和信息区（MechComms.h）：本机看标点键、发标点；服务器按它挑人发信息区的行（MechComms::Get 按类型找，不另存指针）
	CreateDefaultSubobject<UMechCommsComponent>(TEXT("Comms"));
}

void AMechPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		// 菜单开局前把输入切回游戏（界面模式会让视口忽略游戏输入，换图后还留着）
		SetInputMode(FInputModeGameOnly());
		// 启动脚本用 -windowed -ResX/-ResY 开窗口时，把设置也改成窗口 + 这个大小：否则设置里存的是无边框，
		// 一按「应用并保存」就切成无边框、按桌面 4K 渲染
		int32 ResX = 0, ResY = 0;
		UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		if (GUS && FParse::Param(FCommandLine::Get(), TEXT("windowed")) && FParse::Value(FCommandLine::Get(), TEXT("ResX="), ResX) &&
			FParse::Value(FCommandLine::Get(), TEXT("ResY="), ResY))
		{
			GUS->SetFullscreenMode(EWindowMode::Windowed);
			GUS->SetScreenResolution(FIntPoint(ResX, ResY));
		}
		// 窗口比屏幕还大（第一次启动默认 2880×1620 是按 4K 屏定的，1080p 屏放不下）：缩到屏幕高度的四分之三（16:9）并存下来
		// 离屏测试（-RenderOffscreen）没有真屏幕，引擎把窗口大小当成桌面大小，会误缩，跳过
		if (GUS && GUS->GetFullscreenMode() == EWindowMode::Windowed && !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")))
		{
			const FIntPoint Desk = GUS->GetDesktopResolution();
			const FIntPoint Cur = GUS->GetScreenResolution();
			if (Desk.X > 0 && Desk.Y > 0 && (Cur.X > Desk.X * 0.95f || Cur.Y > Desk.Y * 0.9f))
			{
				int32 FitY = FMath::RoundToInt(Desk.Y * 0.75f);
				FitY = FMath::Min(FitY, FMath::RoundToInt(Desk.X * 0.9f * 9.f / 16.f));
				const FIntPoint Fit(FMath::RoundToInt(FitY * 16.f / 9.f), FitY);
				UE_LOG(LogTemp, Display, TEXT("[ui] 窗口 %dx%d 比屏幕 %dx%d 大，改成 %dx%d"), Cur.X, Cur.Y, Desk.X, Desk.Y, Fit.X, Fit.Y);
				GUS->SetScreenResolution(Fit);
				GUS->ApplyResolutionSettings(false);
				GUS->SaveSettings();
			}
		}
		SetShowMouseCursor(false);
		FMechSettings::Get().ApplyAudio(this);
		bPerf = FParse::Param(FCommandLine::Get(), TEXT("MechPerf"));
		GetWorldTimerManager().SetTimer(WarmUpTimer, FTimerDelegate::CreateUObject(this, &AMechPlayerController::WarmUpEffects), 0.5f, false);
	}
}

void AMechPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MenuContainer.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MenuContainer.ToSharedRef());
	}
	Menu.Reset();
	MenuContainer.Reset();
	// 11-01 房间 ↔ 对战图换图时房间 / 选配整屏也收掉：引擎换图不清视口里直接加的 Slate 控件，留着会盖在新图上直到旧控制器被回收
	if (DraftContainer.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(DraftContainer.ToSharedRef());
	}
	DraftScreen.Reset();
	DraftContainer.Reset();
	FWorldDelegates::OnWorldPostActorTick.Remove(JitterHandle);
	if (bPerf)
	{
		PerfSummary();
		bPerf = false;
	}
	Super::EndPlay(EndPlayReason);
}

void AMechPlayerController::WarmUpEffects()
{
	TArray<UNiagaraSystem*> Systems;
	GetDefault<AMechCharacter>()->GetEffectSystems(Systems);
	GetDefault<AMechMissile>()->GetEffectSystems(Systems);
	FVector At = PlayerCameraManager ? PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	At.Z -= 5000.f;
	int32 Count = 0;
	for (UNiagaraSystem* S : Systems)
	{
		if (S && UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), S, At, FRotator::ZeroRotator, FVector(1.f), true, true, ENCPoolMethod::AutoRelease, false))
		{
			++Count;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[perf] 预热特效 %d 个"), Count);
}

void AMechPlayerController::PerfTick(float DeltaTime)
{
	PerfAge += DeltaTime;
	if (PerfAge < 5.f)  // 刚进图的加载不算
	{
		return;
	}
	const float Ms = DeltaTime * 1000.f;
	PerfWindow.Add(Ms);
	PerfAll.Add(Ms);
	PerfGT += FPlatformTime::ToMilliseconds(GGameThreadTime);
	PerfRT += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	PerfGPU += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	if (Ms > 50.f)
	{
		++PerfHitches;
		UE_LOG(LogTemp, Display, TEXT("[perf] 卡顿一帧 %.0f 毫秒（第 %.1f 秒）"), Ms, PerfAge);
	}
	PerfClock += DeltaTime;
	if (PerfClock >= 2.f)
	{
		const int32 N = PerfWindow.Num();
		float Sum = 0.f, Worst = 0.f;
		for (float F : PerfWindow)
		{
			Sum += F;
			Worst = FMath::Max(Worst, F);
		}
		const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
		UE_LOG(LogTemp, Display, TEXT("[perf] %.0f 帧/秒 平均 %.1f 最慢 %.0f 毫秒 | 游戏线程 %.1f 渲染线程 %.1f GPU %.1f 毫秒 | 阶段 %d"), N / PerfClock, Sum / N, Worst,
			PerfGT / N, PerfRT / N, PerfGPU / N, GS ? (int32)GS->Phase : -1);
		PerfWindow.Reset();
		PerfGT = PerfRT = PerfGPU = 0.0;
		PerfClock = 0.f;
	}
}

void AMechPlayerController::JitterTick(UWorld* World, ELevelTick TickType, float DeltaTime)
{
	if (World != GetWorld() || DeltaTime <= 0.f)
	{
		return;
	}
	for (TActorIterator<AMechCharacter> It(World); It; ++It)
	{
		AMechCharacter* Mech = *It;
		if (Mech->GetHealth()->IsDead())
		{
			JitterTracks.Remove(Mech);
			continue;
		}
		FJitterTrack& T = JitterTracks.FindOrAdd(Mech);
		if (!T.Flame.IsValid())
		{
			TInlineComponentArray<USceneComponent*> Comps(Mech);
			for (USceneComponent* C : Comps)
			{
				if (C->GetFName() == TEXT("ThrusterLeft"))
				{
					T.Flame = C;
				}
			}
		}
		const UMechMeshComponent* M = Mech->GetMechMesh();
		constexpr int32 NP = FJitterTrack::NumParts;
		const USceneComponent* Parts[NP] = {M, M->Props[0], M->Props[1], M->Props[2], M->Props[3], M->Props[4], Mech->GetClawMesh(), T.Flame.Get()};
		FVector Now[NP];
		bool bShown[NP];
		for (int32 k = 0; k < NP; ++k)
		{
			Now[k] = Parts[k] ? Parts[k]->GetComponentLocation() : FVector::ZeroVector;
			bShown[k] = Parts[k] && Parts[k]->IsVisible();
		}
		bShown[0] = true;
		bShown[7] = Parts[7] != nullptr;  // 喷焰不喷时隐藏，但位置照样跟着网格，一直记
		const float Yaw = M->GetComponentRotation().Yaw;
		// 挂件脱离网格：帧末道具在网格局部的位置和摆完那一刻差多少（摆完以后网格又被挪 / 转了、或者这一帧骨骼没刷新时，按世界坐标摆、挂在胶囊上的道具会留在原地）
		if (M->PlacedFrame != 0)
		{
			if (M->PlacedFrame != GFrameCounter)
			{
				++T.Stale;
			}
			const float Scale = M->GetComponentScale().X;
			for (int32 p = 0; p < UMechMeshComponent::NumProps; ++p)
			{
				if (bShown[p + 1] && p != 2)
				{
					T.Bind[p].Add(FVector::Dist(M->GetComponentTransform().InverseTransformPosition(Now[p + 1]), M->PlacedLocal[p]) * Scale);
				}
			}
		}
		// 肩部挂件是浮空无人机（10-30）：本来就有意滞后，不算「脱离网格」，改记它离这一帧理想点多远（弹簧拖了多少，上限 80 厘米）
		if (bShown[3])
		{
			T.Bind[2].Add(FVector::Dist(Mech->GetShoulder()->GetDroneLocation(), Mech->GetShoulder()->GetDroneIdealVisual()));
		}
		if (bShown[6] && Mech->GetShoulder()->GetGrappleState() == EMechGrapple::Idle)
		{
			T.Bind[5].Add(FVector::Dist(Now[6], Mech->GetShoulder()->GetClawRestWorld()));
		}
		if (T.N >= 2)
		{
			// 中间那帧（上一帧）偏离前后两帧按时间插值的位置多少（三帧都显示着才算：刚显示 / 刚隐藏时位置本来就不连续）
			const float A = T.Dt / FMath::Max(T.Dt + DeltaTime, 1e-4f);
			for (int32 k = 0; k < NP; ++k)
			{
				if (bShown[k] && T.bShown[0][k] && T.bShown[1][k])
				{
					T.Dev[k].Add(FVector::Dist(T.P[1][k], FMath::Lerp(T.P[0][k], Now[k], A)));
				}
			}
			const float Y0 = T.Yaw[0];
			const float Y2 = Y0 + FMath::FindDeltaAngleDegrees(Y0, Yaw);
			const float Y1 = Y0 + FMath::FindDeltaAngleDegrees(Y0, T.Yaw[1]);
			T.Dev[8].Add(FMath::Abs(Y1 - FMath::Lerp(Y0, Y2, A)));
			const FVector Want = Mech->GetCapsuleComponent()->GetComponentTransform().TransformPosition(Mech->GetBaseTranslationOffset());
			const float Off = FVector::Dist(Now[0], Want);
			T.OffsetSum += Off;
			T.OffsetMax = FMath::Max(T.OffsetMax, Off);
			T.SpeedSum += Mech->GetVelocity().Size();
		}
		// 10-31 联机细查：逐帧加速度（二阶差分）、回跳、动画用的速度跳变、瞄准方向、本机镜头（第三行 [jitter]）
		{
			const FVector AV = Mech->GetMechMovement()->GetVisualVelocity();
			const FVector AimNow = M->AimOffset.GetSafeNormal();
			const bool bMine = Mech->IsLocallyControlled() && Mech->IsPlayerControlled() && PlayerCameraManager;
			const FVector CamNow = bMine ? PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
			if (T.N >= 2)
			{
				const float D0 = FMath::Max(T.Dt, 1e-4f);
				const float D1 = FMath::Max(DeltaTime, 1e-4f);
				const float Dm = 0.5f * (D0 + D1);
				auto Accel = [D0, D1, Dm](const FVector& P0, const FVector& P1, const FVector& P2) { return float(((P2 - P1) / D1 - (P1 - P0) / D0).Size() / Dm); };
				T.Acc[0].Add(Accel(T.P[0][0], T.P[1][0], Now[0]) / 100.f);
				const float YA0 = T.Yaw[0];
				const float YA1 = YA0 + FMath::FindDeltaAngleDegrees(YA0, T.Yaw[1]);
				const float YA2 = YA1 + FMath::FindDeltaAngleDegrees(YA1, Yaw);
				T.Acc[1].Add(FMath::Abs((YA2 - YA1) / D1 - (YA1 - YA0) / D0) / Dm);
				const FVector S0 = (T.P[1][0] - T.P[0][0]) * FVector(1.f, 1.f, 0.f);
				const FVector S1 = (Now[0] - T.P[1][0]) * FVector(1.f, 1.f, 0.f);
				if (S0.SizeSquared() > 0.25f && S1.SizeSquared() > 0.25f && (S0 | S1) < 0.f)
				{
					++T.Reverse;
				}
				if (!T.Aim[0].IsZero() && !T.Aim[1].IsZero() && !AimNow.IsZero())
				{
					T.Acc[3].Add(FMath::RadiansToDegrees(Accel(T.Aim[0], T.Aim[1], AimNow)));
				}
				if (bMine && !T.Cam[0].IsZero())
				{
					T.Acc[4].Add(Accel(T.Cam[0], T.Cam[1], CamNow) / 100.f);
				}
			}
			if (T.N >= 1)
			{
				T.Acc[2].Add(FVector::Dist(AV, T.AnimV) / 100.f);
			}
			T.AnimV = AV;
			T.Aim[0] = T.Aim[1];
			T.Aim[1] = AimNow;
			T.Cam[0] = T.Cam[1];
			T.Cam[1] = CamNow;
			if (T.Corrections0 < 0)
			{
				T.Corrections0 = Mech->GetMechMovement()->CorrectionCount;
			}
		}
		// 端着枪交战时：枪管（枪模型 +X）偏离「枪口 → 姿势瞄准点」多少；瞄准方向偏离画面上机体朝向多少（客户端上别人的机体以前一直拧向世界 +X）
		const UMechCombatComponent* Combat = Mech->GetCombat();
		const bool bTwoHand = M->GunAimWeight > 0.99f;
		const bool bForearm = M->ArmPointWeight[1] > 0.99f && M->ArmWeight[1] > 0.99f;
		if (Combat->IsEngaged() && !Combat->IsBladeInHand() && !Combat->IsReloading() && M->Props[1] && !M->AimOffset.IsNearlyZero() && (bTwoHand || bForearm))
		{
			const FVector Point = M->VisualPivot() + M->AimOffset;
			const FVector Muzzle = M->GetPropPoint(1, Combat->GetGun().Muzzle);
			const float Err = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(M->Props[1]->GetForwardVector() | (Point - Muzzle).GetSafeNormal(), -1.f, 1.f)));
			T.Dev[9].Add(Err);
			// 偏得多时记一下当时的情况（每台每秒最多一行）：两手连线（右手 → 左手）对没对准、瞄准点多远多高、枪是哪把
			static TMap<TWeakObjectPtr<AMechCharacter>, double> LastNote;
			double& Last = LastNote.FindOrAdd(Mech);
			if (Err > 15.f && World->GetTimeSeconds() - Last > 1.0)
			{
				Last = World->GetTimeSeconds();
				const FVector HandR = M->GetBoneLocation(TEXT("hand_r"));
				const FVector Hands = (M->GetBoneLocation(TEXT("hand_l")) - HandR).GetSafeNormal();
				const float HandErr = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Hands | (Point - HandR).GetSafeNormal(), -1.f, 1.f)));
				const float ForeErr = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp((HandR - M->GetBoneLocation(TEXT("lowerarm_r"))).GetSafeNormal() | (Point - HandR).GetSafeNormal(), -1.f, 1.f)));
				UE_LOG(LogTemp, Display, TEXT("[jitter] %s 枪管偏 %.0f 度：%s %s，两手连线偏 %.0f 度，右前臂偏 %.0f 度，瞄准点 %.0f 米 俯仰 %.0f 度，枪重 %.2f 臂重 %.2f/%.2f，速度 %.0f%s"),
					*Mech->DisplayName, Err, Combat->GetGun().Name, bTwoHand ? TEXT("双手端") : TEXT("前臂"), HandErr, ForeErr, M->AimOffset.Size() / 100.f,
					M->AimOffset.Rotation().Pitch, M->GunAimWeight, M->ArmWeight[1], M->ArmPointWeight[1], Mech->GetVelocity().Size(),
					Mech->GetMechMovement()->IsFalling() ? TEXT(" 空中") : TEXT(""));
			}
			T.Dev[10].Add(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(M->VisualActorQuat().GetForwardVector().GetSafeNormal2D() | M->AimOffset.GetSafeNormal2D(), -1.f, 1.f))));
		}
		for (int32 k = 0; k < NP; ++k)
		{
			T.P[0][k] = T.P[1][k];
			T.P[1][k] = Now[k];
			T.bShown[0][k] = T.bShown[1][k];
			T.bShown[1][k] = bShown[k];
		}
		T.Yaw[0] = T.Yaw[1];
		T.Yaw[1] = Yaw;
		T.Dt = DeltaTime;
		++T.N;
	}
	JitterClock += DeltaTime;
	if (JitterClock < 5.f)
	{
		return;
	}
	JitterClock = 0.f;
	for (auto& Pair : JitterTracks)
	{
		AMechCharacter* Mech = Pair.Key.Get();
		FJitterTrack& T = Pair.Value;
		const int32 Frames = T.Dev[0].Num();
		if (!Mech || Frames < 20)
		{
			continue;
		}
		auto Pct = [](TArray<float>& V, float Q) {
			if (V.Num() == 0)
			{
				return -1.f;
			}
			V.Sort();
			return V[FMath::Clamp(FMath::FloorToInt(V.Num() * Q), 0, V.Num() - 1)];
		};
		const TCHAR* Who = Mech->IsLocallyControlled() && Mech->IsPlayerControlled() ? TEXT("本机") : Mech->GetLocalRole() == ROLE_SimulatedProxy ? TEXT("模拟")
			: Mech->GetRemoteRole() == ROLE_AutonomousProxy ? TEXT("房主看客户端") : TEXT("服务器AI");
		// 第一行格式和以前一样（枪 = 右手枪、挂件 = 背后挂件），新旧日志能直接对比
		UE_LOG(LogTemp, Display, TEXT("[jitter] %s（%s）%d 帧 速度 %.0f | 网格 中位 %.2f / 95%% %.2f / 最大 %.1f 厘米，朝向 95%% %.2f 度 | 枪 95%% %.2f，挂件 95%% %.2f，喷焰 95%% %.2f 厘米 | 网格偏离胶囊 平均 %.1f 最大 %.1f 厘米"),
			*Mech->DisplayName, Who, Frames, T.SpeedSum / Frames, Pct(T.Dev[0], 0.5f), Pct(T.Dev[0], 0.95f), Pct(T.Dev[0], 1.f), Pct(T.Dev[8], 0.95f),
			Pct(T.Dev[2], 0.95f), Pct(T.Dev[3], 0.95f), Pct(T.Dev[7], 0.95f), T.OffsetSum / Frames, T.OffsetMax);
		// 第二行：所有挂件的抖动 95% 分位，和「脱离网格」95% 分位 / 最大（-1 = 这几秒没显示过），骨骼没刷新的帧数。
		// 「无人机」那一列：抖动 = 浮空无人机本身一帧帧顺不顺；「离理想点」= 弹簧拖了多少（有意的滞后，不是分家）
		UE_LOG(LogTemp, Display, TEXT("[jitter] %s（%s）挂件抖动 95%%：刀 %.1f 枪 %.1f 无人机 %.1f 左枪 %.1f 左刀 %.1f 爪 %.1f 厘米 | 脱离网格 95%%/最大：刀 %.1f/%.1f 枪 %.1f/%.1f 无人机离理想点 %.1f/%.1f 左枪 %.1f/%.1f 左刀 %.1f/%.1f 爪 %.1f/%.1f 厘米 | 骨骼没刷新 %d 帧"),
			*Mech->DisplayName, Who, Pct(T.Dev[1], 0.95f), Pct(T.Dev[2], 0.95f), Pct(T.Dev[3], 0.95f), Pct(T.Dev[4], 0.95f), Pct(T.Dev[5], 0.95f), Pct(T.Dev[6], 0.95f),
			Pct(T.Bind[0], 0.95f), Pct(T.Bind[0], 1.f), Pct(T.Bind[1], 0.95f), Pct(T.Bind[1], 1.f), Pct(T.Bind[2], 0.95f), Pct(T.Bind[2], 1.f),
			Pct(T.Bind[3], 0.95f), Pct(T.Bind[3], 1.f), Pct(T.Bind[4], 0.95f), Pct(T.Bind[4], 1.f), Pct(T.Bind[5], 0.95f), Pct(T.Bind[5], 1.f), T.Stale);
		if (T.Dev[9].Num() > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("[jitter] %s（%s）端枪交战 %d 帧：枪管偏离瞄准点 中位 %.1f / 95%% %.1f 度，瞄准方向偏离机体朝向 中位 %.0f / 95%% %.0f 度"),
				*Mech->DisplayName, Who, T.Dev[9].Num(), Pct(T.Dev[9], 0.5f), Pct(T.Dev[9], 0.95f), Pct(T.Dev[10], 0.5f), Pct(T.Dev[10], 0.95f));
		}
		// 第三行（10-31 联机细查）：网格位置 / 朝向的逐帧加速度、回跳帧数、动画用的速度每帧跳多少、瞄准方向的加速度；
		// 收到新位置的间隔和扎堆、旧做法每次被拉回多少（外推误差）；网格现在是哪种平滑、回放比最新位置晚多少；自己的机体再加镜头加速度和纠正次数
		{
			UMechMovementComponent* MM = Mech->GetMechMovement();
			UMechMovementComponent::FNetStats& NS = MM->NetStats;
			FString Mine;
			if (Mech->IsLocallyControlled() && Mech->IsPlayerControlled())
			{
				Mine = FString::Printf(TEXT(" | 本机镜头加速度 95%%/最大 %.0f/%.0f 米/秒²，这几秒被服务器纠正 %d 次"), Pct(T.Acc[4], 0.95f), Pct(T.Acc[4], 1.f),
					MM->CorrectionCount - FMath::Max(T.Corrections0, 0));
			}
			UE_LOG(LogTemp, Display, TEXT("[jitter] %s（%s）联机：加速度 95%%/最大 网格 %.0f/%.0f 米/秒²、朝向 %.0f/%.0f 度/秒²，回跳 %d 帧 | 动画速度每帧变化 95%%/最大 %.2f/%.2f 米/秒 | 瞄准方向 95%%/最大 %.0f/%.0f 度/秒² | 收到新位置 %d 个，间隔 中位/95%%/最大 %.0f/%.0f/%.0f 毫秒，同帧多个 %d 帧，外推被拉回 95%%/最大 %.1f/%.1f 厘米 | %s，平均晚 %.0f 毫秒，在动时断粮 %d 帧%s"),
				*Mech->DisplayName, Who, Pct(T.Acc[0], 0.95f), Pct(T.Acc[0], 1.f), Pct(T.Acc[1], 0.95f), Pct(T.Acc[1], 1.f), T.Reverse, Pct(T.Acc[2], 0.95f),
				Pct(T.Acc[2], 1.f), Pct(T.Acc[3], 0.95f), Pct(T.Acc[3], 1.f), NS.Samples, Pct(NS.Gaps, 0.5f), Pct(NS.Gaps, 0.95f), Pct(NS.Gaps, 1.f), NS.BunchFrames,
				Pct(NS.Errors, 0.95f), Pct(NS.Errors, 1.f), MM->IsInterpActive() ? TEXT("按时间回放") : TEXT("追最新包"),
				NS.Frames > 0 ? float(NS.DelaySum / NS.Frames * 1000.0) : -1.f, NS.StarveFrames, *Mine);
			NS.Samples = NS.BunchFrames = NS.StarveFrames = NS.Frames = 0;
			NS.DelaySum = 0.0;
			NS.Gaps.Reset();
			NS.Errors.Reset();
			T.Corrections0 = MM->CorrectionCount;
			for (TArray<float>& D : T.Acc)
			{
				D.Reset();
			}
			T.Reverse = 0;
		}
		for (TArray<float>& D : T.Dev)
		{
			D.Reset();
		}
		for (TArray<float>& D : T.Bind)
		{
			D.Reset();
		}
		T.Stale = 0;
		T.OffsetSum = T.OffsetMax = T.SpeedSum = 0.f;
	}
}

void AMechPlayerController::PerfSummary()
{
	if (PerfAll.Num() < 10)
	{
		return;
	}
	TArray<float> Sorted = PerfAll;
	Sorted.Sort();
	float Sum = 0.f;
	for (float F : Sorted)
	{
		Sum += F;
	}
	const float P99 = Sorted[FMath::Min(Sorted.Num() - 1, (int32)(Sorted.Num() * 0.99f))];
	UE_LOG(LogTemp, Display, TEXT("[perf] 汇总：%d 帧，平均 %.1f 帧/秒（%.1f 毫秒），最慢 1%% 的帧 %.0f 毫秒，最慢一帧 %.0f 毫秒，超过 50 毫秒 %d 次"), Sorted.Num(),
		1000.f * Sorted.Num() / Sum, Sum / Sorted.Num(), P99, Sorted.Last(), PerfHitches);
}

void AMechPlayerController::PlayerTick(float DeltaTime)
{
	MechMapRules::UpdateBoosterLock(GetWorld());  // 11-02 石林：配装界面锁定专用推进器（房间里选了这张图也算）
	Super::PlayerTick(DeltaTime);
	if (bPerf)
	{
		PerfTick(DeltaTime);
	}
	if (FightClock < -0.5f)
	{
		bNetTest = FParse::Param(FCommandLine::Get(), TEXT("MechNetTest"));
		bPilot = bNetTest || FParse::Param(FCommandLine::Get(), TEXT("MechInputTest"));
		bPilotShots = FParse::Param(FCommandLine::Get(), TEXT("MechPilotShots"));
		FParse::Value(FCommandLine::Get(), TEXT("MechRounds="), RoundsWanted);
		FightClock = -0.25f;
		if (IsLocalController() && FParse::Param(FCommandLine::Get(), TEXT("MechJitterLog")) && !JitterHandle.IsValid())
		{
			JitterHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &AMechPlayerController::JitterTick);
		}
	}
	if (IsLocalController())
	{
		MenuTick();
		UpdateRenderScale(DeltaTime);
		DeathCamTick(DeltaTime);
		if (IsRangeMode() && FParse::Param(FCommandLine::Get(), TEXT("MechRangeTest")))
		{
			RangeTestTick(DeltaTime);
		}
		if (IsRangeMode() && FParse::Param(FCommandLine::Get(), TEXT("MechWeaponTest")))
		{
			WeaponTestTick(DeltaTime);
		}
		if (IsRangeMode() && FParse::Param(FCommandLine::Get(), TEXT("MechShoulderTest")))
		{
			ShoulderTestTick(DeltaTime);
		}
		if (IsRangeMode() && FParse::Param(FCommandLine::Get(), TEXT("MechFrameTraitTest")))
		{
			FrameTraitTestTick(DeltaTime);
		}
		if (IsRangeMode() && FParse::Param(FCommandLine::Get(), TEXT("MechTacticalTest")))
		{
			TacticalTestTick(DeltaTime);
		}
		if (IsRangeMode() && FParse::Param(FCommandLine::Get(), TEXT("MechStressTest")))
		{
			StressTestTick(DeltaTime);
		}
		if (IsRangeMode() && FParse::Param(FCommandLine::Get(), TEXT("MechDefenseTest")))
		{
			DefenseTestTick(DeltaTime);
		}
		// -MechInfiniteEnergy（11-01 用户：「这个版本请给我无限能量以便飞行」，看大地图用）：自己这台的推进能量一直是满的
		static const bool bInfiniteEnergy = FParse::Param(FCommandLine::Get(), TEXT("MechInfiniteEnergy"));
		if (bInfiniteEnergy)
		{
			if (AMechCharacter* Self = Cast<AMechCharacter>(GetPawn()))
			{
				Self->GetMechMovement()->Energy = Self->GetMechMovement()->MaxEnergy;
			}
		}
		// 测试：-MechQuitAfter=秒数，到点正常退出（联机测「掉线后 AI 接管」用）
		static float QuitAfter = -1.f;
		static const bool bHasQuit = FParse::Value(FCommandLine::Get(), TEXT("MechQuitAfter="), QuitAfter);
		if (bHasQuit && QuitAfter > 0.f && GetWorld()->GetTimeSeconds() > QuitAfter)
		{
			const AMechCharacter* Mine = Cast<AMechCharacter>(GetPawn());
			UE_LOG(LogTemp, Display, TEXT("[pilot] 到点退出（-MechQuitAfter）：自己机体 %s，收到服务器移动纠正 %d 次"),
				Mine ? *Mine->DisplayName : TEXT("没有"), Mine ? Mine->GetMechMovement()->CorrectionCount : -1);
			QuitAfter = -1.f;
			ConsoleCommand(TEXT("quit"));
		}
	}
	if (bPilot && IsLocalController())
	{
		PilotTick(DeltaTime);
	}
}

// ---------------------------------------------------------------- 菜单
void AMechPlayerController::MenuTick()
{
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	if (const AMechCharacter* Mine = Cast<AMechCharacter>(GetPawn()))
	{
		MenuTeam = Mine->GetTeam();
		MenuName = Mine->DisplayName;
	}
	if (!GS || bPilot)
	{
		return;
	}
	if (!bSentProfile && IsLocalController() && PlayerState)
	{
		SendProfile();
	}
	DraftScreenTick();
	if (GS->bRangeMode && !bSentRangeLoadout && GetPawn())
	{
		bSentRangeLoadout = true;
		FMechLoadout RangeLoadout = FMechSettings::Get().Loadout;
		// 11-01 肉鸽测试.bat：默认步枪 + 大剑 + 导弹（只有这一版的卡；Esc 里还能换）
		if (FParse::Param(FCommandLine::Get(), TEXT("MechRunLoadout")))
		{
			RangeLoadout.Shooter = EMechShooter::Rifle;
			RangeLoadout.Melee = EMechMeleeWeapon::GreatSword;
			RangeLoadout.Shoulder = EMechShoulder::Missiles;
		}
		ServerApplyLoadout(RangeLoadout);
	}
	// 11-01 肉鸽成长：数字键 1 / 2 / 3 选卡（只有保卫战才有这个组件；1 2 3 别处都没占，模型查看模式不走到这里）
	if (GS->bRangeMode && !IsMenuOpen())
	{
		if (UMechRunComponent* Run = UMechRunComponent::Of(this))
		{
			Run->LocalInput(this);
		}
	}
	if (GS->bMenuMode)
	{
		if (!IsMenuOpen())
		{
			OpenMenu(EMechMenuPage::Main);
		}
		if (GetWorld()->URL.HasOption(TEXT("MenuShots")))
		{
			MenuShotsTick(GetWorld()->GetDeltaSeconds());
		}
		return;
	}
	// 中途加入、没分到机体时看一台还活着的机体（优先自己队）
	if (!GetPawn() && GS->Phase != EMechRoundPhase::Waiting && GetViewTarget() == this)
	{
		AMechCharacter* Watch = nullptr;
		for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->GetHealth()->IsDead() && (!Watch || It->GetTeam() == MenuTeam))
			{
				Watch = *It;
			}
		}
		if (Watch)
		{
			SetViewTarget(Watch);
		}
	}
	// 整场结束弹结算，下一局开始时收起
	if (GS->Phase != MenuSeenPhase)
	{
		if (GS->Phase == EMechRoundPhase::MatchOver)
		{
			// 先播实时结算场景（MechResultsStage，播完 / 按键后它自己开结算表）；不播就照旧马上弹表
			if (!AMechResultsStage::BeginFor(this))
			{
				OpenMenu(EMechMenuPage::Results);
			}
		}
		else if (MenuSeenPhase == EMechRoundPhase::MatchOver && IsMenuOpen() && Menu->GetPage() == EMechMenuPage::Results)
		{
			CloseMenu();
		}
		MenuSeenPhase = GS->Phase;
	}
	if (!IsMenuOpen() && !AMechResultsStage::IsActive(GetWorld()) && WasInputKeyJustPressed(EKeys::Escape))  // 结算场景里 Esc = 跳到结算表
	{
		OpenMenu(EMechMenuPage::Pause);
	}
}

void AMechPlayerController::MenuShotsTick(float DeltaTime)
{
	// 11-01 「开始对战 / 联机对战」两页删了（地图在房间里选），只剩「加入」页
	static const EMechMenuPage Pages[] = {EMechMenuPage::Main, EMechMenuPage::Join, EMechMenuPage::Settings, EMechMenuPage::Pause, EMechMenuPage::Results};
	static const TCHAR* Names[] = {TEXT("1_主菜单"), TEXT("2_加入"), TEXT("4_设置"), TEXT("5_暂停"), TEXT("6_结算空表")};
	const float T0 = MenuShotClock;
	MenuShotClock += DeltaTime;
	const float Start = 6.f;  // 等灯光、贴图流送稳定
	const float Per = 1.6f;
	const int32 Index = FMath::FloorToInt((MenuShotClock - Start) / Per);
	if (MenuShotClock < Start || !Menu.IsValid())
	{
		return;
	}
	if (Index >= UE_ARRAY_COUNT(Pages))
	{
		ConsoleCommand(TEXT("quit"));
		return;
	}
	const float Local = MenuShotClock - Start - Index * Per;
	const float PrevLocal = T0 - Start - Index * Per;
	if (PrevLocal < 0.f && Local >= 0.f)
	{
		Menu->SetPage(Pages[Index]);
	}
	if (PrevLocal < 1.f && Local >= 1.f)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Menu") / FString::Printf(TEXT("%s.png"), Names[Index]), true, false);
	}
}

void AMechPlayerController::UpdateRenderScale(float DeltaTime)
{
	RenderScaleClock -= DeltaTime;
	if (RenderScaleClock > 0.f || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	RenderScaleClock = 0.5f;
	UGameUserSettings* GUS = GEngine->GetGameUserSettings();
	float Norm = 0.f, Chosen = 0.f, Min = 0.f, Max = 0.f;
	if (GUS)
	{
		GUS->GetResolutionScaleInformationEx(Norm, Chosen, Min, Max);
	}
	float Percent = Chosen;
	if (Percent <= 0.f)
	{
		FVector2D Size = FVector2D::ZeroVector;
		GEngine->GameViewport->GetViewportSize(Size);
		Percent = Size.Y > 0.f ? FMath::Clamp(810.f / Size.Y * 100.f, 50.f, 100.f) : 100.f;
	}
	// 用控制台命令设（和 -ExecCmds 同一条路）：直接 Set(…, ECVF_SetByCode) 实测值改了但画面没降分辨率
	static IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	if (ScreenPercentage && !FMath::IsNearlyEqual(ScreenPercentage->GetFloat(), Percent, 0.5f) && !FParse::Param(FCommandLine::Get(), TEXT("MechKeepScreenPercentage")))
	{
		ConsoleCommand(FString::Printf(TEXT("r.ScreenPercentage %.0f"), Percent));
		UE_LOG(LogTemp, Display, TEXT("[ui] 3D 渲染精度 %.0f%%%s"), Percent, Chosen <= 0.f ? TEXT("（自动）") : TEXT(""));
	}
	// 光线追踪：游戏开始后关一次（全局光照改用软件追踪，画面差别很小）。50% 精度下开着约 59 帧、关掉约 85 帧
	static bool bRayTracingOff = false;
	if (!bRayTracingOff && !FParse::Param(FCommandLine::Get(), TEXT("MechRayTracing")))
	{
		bRayTracingOff = true;
		ConsoleCommand(TEXT("r.RayTracing.Enable 0"));
		UE_LOG(LogTemp, Display, TEXT("[ui] 关闭光线追踪（-MechRayTracing 保留）"));
	}
}

void AMechPlayerController::OpenMenu(EMechMenuPage Page)
{
	if (!IsLocalController() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	if (!Menu.IsValid())
	{
		SAssignNew(Menu, SMechMenu).Owner(this).Page(Page);
		MenuContainer = SNew(SWeakWidget).PossiblyNullContent(Menu);
		GEngine->GameViewport->AddViewportWidgetContent(MenuContainer.ToSharedRef(), 50);
	}
	else
	{
		Menu->SetPage(Page);
	}
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Menu);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	SetShowMouseCursor(true);
	FlushPressedKeys();
	if (Page == EMechMenuPage::Pause && GetNetMode() == NM_Standalone)
	{
		SetPause(true);
	}
}

void AMechPlayerController::CloseMenu()
{
	if (MenuContainer.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MenuContainer.ToSharedRef());
	}
	Menu.Reset();
	MenuContainer.Reset();
	if (IsPaused())
	{
		SetPause(false);
	}
	if (DraftScreen.IsValid())
	{
		// 房间 / 选配界面还开着：鼠标还给它
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(DraftScreen);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		SetShowMouseCursor(true);
		return;
	}
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
}

namespace MechMenuNet
{
	/** 菜单里开房 / 加入 / 换图之前，先取消还在连接中的加入（世界上下文里挂着的 PendingNetGame）。
	 *  不取消的话，引擎带着这个挂起的客户端连接去开房：新世界没生成游戏模式（日志「Login failed: No game mode set」），
	 *  接着「Couldn't spawn player」直接致命退出（UnrealEngine.cpp:16649）。10-29、10-30 打包版两次崩溃都是
	 *  「先加入一个还没开房的 IP、停在连接中，再点开房」；用户的说法是「上一把不是房主开房会闪退」 */
	void CancelPendingJoin(const UObject* Ctx)
	{
		UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
		FWorldContext* WC = GEngine && World ? GEngine->GetWorldContextFromWorld(World) : nullptr;
		if (WC && WC->PendingNetGame)
		{
			UE_LOG(LogTemp, Display, TEXT("[menu] 取消还在连接中的加入，再换图 / 开房"));
			GEngine->CancelPending(World);   // 取 FWorldContext& 的那个重载是 protected，编不过；这个按世界找到上下文再取消，效果一样
		}
	}
}

void AMechPlayerController::MenuOpenRoom()
{
	MechMenuNet::CancelPendingJoin(this);
	// 11-01 用户：「只单独保留开房，地图在界面里面选择」——以前「开始对战 / 联机开房」先在菜单里选好地图再直接进对战图，
	// 现在一律先进房间图（总是 listen，朋友随时能进；单人就是没人进来的房间），地图、模式、规则都在房间里由房主选。
	// 换图要用的东西见 MechRoom.h（地图表照旧在 MechMaps.cpp）
	if (UMechRoomSubsystem* Sub = UMechRoomSubsystem::Get(this))
	{
		Sub->BeginRoom();
	}
	SetInputMode(FInputModeGameOnly());
	UGameplayStatics::OpenLevel(this, FName(MechRoom::RoomMapPath()), true, TEXT("Room?listen"));
}

void AMechPlayerController::MenuJoin(const FString& Address)
{
	MechMenuNet::CancelPendingJoin(this);  // 连着一个没响应的地址又去加入别的：先撤掉旧的
	if (UMechRoomSubsystem* Sub = UMechRoomSubsystem::Get(this))
	{
		Sub->EndRoom();  // 去别人的房：自己不再是房主
	}
	SetInputMode(FInputModeGameOnly());
	ClientTravel(Address.IsEmpty() ? FString(TEXT("127.0.0.1")) : Address, TRAVEL_Absolute);
}

void AMechPlayerController::MenuBackToRoom()
{
	// 11-01 结算后回房间：只有房主能带大家回（服务器换图，客户端自动跟着）；非房主的按钮是灰的
	AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>();
	if (GM && GM->IsHost(this))
	{
		GM->ReturnToRoom();
	}
}

bool AMechPlayerController::CanToggleFriendlyFire() const
{
	const AMechGameState* GS = GetWorld() ? GetWorld()->GetGameState<AMechGameState>() : nullptr;
	return GS && GS->bRoomMatch && GS->bRangeMode && MechRoom::IsTraining(GS->Room) && CanRestartMatch();
}

bool AMechPlayerController::IsFriendlyFire() const
{
	const AMechGameState* GS = GetWorld() ? GetWorld()->GetGameState<AMechGameState>() : nullptr;
	return GS && GS->bFriendlyFire;
}

void AMechPlayerController::MenuSetFriendlyFire(bool bOn)
{
	AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>();
	if (GM && GM->IsHost(this))
	{
		GM->SetTrainingFriendlyFire(bOn);
	}
}

bool AMechPlayerController::IsRoomMatch() const
{
	const AMechGameState* GS = GetWorld() ? GetWorld()->GetGameState<AMechGameState>() : nullptr;
	return GS && GS->bRoomMatch;
}

void AMechPlayerController::MenuOpenViewer()
{
	MechMenuNet::CancelPendingJoin(this);
	SetInputMode(FInputModeGameOnly());
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Maps/L_Street")), true, TEXT("Viewer"));
}

void AMechPlayerController::MenuBackToMain()
{
	MechMenuNet::CancelPendingJoin(this);
	if (UMechRoomSubsystem* Sub = UMechRoomSubsystem::Get(this))
	{
		Sub->EndRoom();  // 房主回主菜单 = 房间解散（客户端断开后回到自己的主菜单）
	}
	SetInputMode(FInputModeGameOnly());
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Maps/L_Street")));
}

void AMechPlayerController::MenuResume()
{
	CloseMenu();
}

void AMechPlayerController::MenuRestart()
{
	if (AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>())
	{
		GM->RestartMatchNow();
	}
}

void AMechPlayerController::RangeTestTick(float DeltaTime)
{
	const float T0 = RangeTestClock;
	RangeTestClock += DeltaTime;
	auto At = [&](float S) { return T0 < S && RangeTestClock >= S; };
	auto Shot = [this](const TCHAR* Name) {
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Range") / FString::Printf(TEXT("%s.png"), Name), true, false);
	};
	AMechCharacter* Me = Cast<AMechCharacter>(GetPawn());
	if (!Me)
	{
		return;
	}
	AMechCharacter* Dummy = nullptr;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsTrainingDummy() && (!Dummy || FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist(Dummy->GetActorLocation(), Me->GetActorLocation())))
		{
			Dummy = *It;
		}
	}
	auto Log = [&](const TCHAR* Tag) {
		const UMechMovementComponent* M = Me->GetMechMovement();
		UE_LOG(LogTemp, Display, TEXT("[rangetest] %s：%s | 装甲 %.0f（%.0f%%）行走 %.0f 冲刺 %.0f 闪避 %.0f 耗能 %.0f 能喷射 %d 能滑翔 %d"), Tag,
			*MechLoadout::Describe(Me->GetLoadout()), Me->GetHealth()->GetAP(), Me->GetHealth()->GetAPFraction() * 100.f, M->WalkSpeed, M->BoostSpeed,
			M->QuickBoostSpeed, M->QuickBoostCost, M->bCanThrust ? 1 : 0, M->bCanAirBoost ? 1 : 0);
	};
	if (At(4.f))
	{
		Shot(TEXT("00_全景"));
		Log(TEXT("开局"));
	}
	if (Dummy && RangeTestClock > 4.5f && RangeTestClock < 10.f)
	{
		SetControlRotation((Dummy->GetActorLocation() + FVector(0.f, 0.f, -60.f) - PlayerCameraManager->GetCameraLocation()).Rotation());
		Me->GetCombat()->SetTrigger(RangeTestClock > 5.f);
	}
	if (At(10.f))
	{
		Me->GetCombat()->SetTrigger(false);
		Shot(TEXT("01_打木桩"));
		if (Dummy)
		{
			UE_LOG(LogTemp, Display, TEXT("[rangetest] %s 读数：上一击 %.0f 这一轮 %.0f 每秒 %.0f 装甲 %.0f%%"), *Dummy->DisplayName, Dummy->GetDummyLastHit(),
				Dummy->GetDummyStreakDamage(), Dummy->GetDummyDPS(), Dummy->GetHealth()->GetAPFraction() * 100.f);
		}
	}
	if (At(11.f))
	{
		OpenMenu(EMechMenuPage::Loadout);
	}
	if (At(12.5f))
	{
		Shot(TEXT("02_配装页"));
	}
	if (At(13.5f))
	{
		FMechLoadout L;
		L.Frame = EMechFrame::Heavy;
		L.Booster = EMechBooster::Ground;
		L.Shoulder = EMechShoulder::Artillery;
		MenuApplyLoadout(L);
		CloseMenu();
	}
	if (At(15.f))
	{
		Log(TEXT("重型 + 地面型"));
		FMechLoadout L;
		L.Frame = EMechFrame::Light;
		L.Booster = EMechBooster::Flight;
		MenuApplyLoadout(L);
	}
	if (At(16.5f))
	{
		Log(TEXT("轻型 + 飞行型"));
		Shot(TEXT("03_换装后"));
		FMechSettings::Get().Loadout = FMechLoadout();  // 测完把存的配装改回默认
		FMechSettings::Get().Save();
	}
	if (At(18.f))
	{
		ConsoleCommand(TEXT("quit"));
	}
}

void AMechPlayerController::WeaponTestTick(float DeltaTime)
{
	// 每把 10 秒：换装、站到木桩前、开火 / 出招 4.5 秒、记读数，再空 4 秒等木桩回满（木桩停手 4 秒才复位）
	const float Slot = 10.f;
	const float Start = 3.f;
	const float T0 = WeaponTestClock;
	WeaponTestClock += DeltaTime;
	AMechCharacter* Me = Cast<AMechCharacter>(GetPawn());
	if (!Me || WeaponTestClock < Start)
	{
		return;
	}
	const int32 NumGuns = MechWeapons::NumGuns();
	const int32 NumBlades = MechWeapons::NumBlades();
	// -MechWeaponTestBlades：只轮 7 把刀（10-31 看近战特效）
	static const bool bBladesOnly = FParse::Param(FCommandLine::Get(), TEXT("MechWeaponTestBlades"));
	const int32 SlotIndex = FMath::FloorToInt((WeaponTestClock - Start) / Slot);
	const int32 Index = SlotIndex + (bBladesOnly ? NumGuns : 0);
	const float L0 = T0 - Start - SlotIndex * Slot;  // 这一把开始后过了多久（上一帧）
	const float L = WeaponTestClock - Start - SlotIndex * Slot;
	auto At = [&](float S) { return L0 < S && L >= S; };
	if (Index >= NumGuns + NumBlades)
	{
		if (L0 < 0.5f && L >= 0.5f)
		{
			ConsoleCommand(TEXT("quit"));
		}
		return;
	}
	const bool bGun = Index < NumGuns;
	UMechCombatComponent* Combat = Me->GetCombat();
	AMechCharacter* Dummy = nullptr;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsTrainingDummy() && (!Dummy || FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist(Dummy->GetActorLocation(), Me->GetActorLocation())))
		{
			Dummy = *It;
		}
	}
	if (!Dummy)
	{
		return;
	}
	const FString Name = bGun ? MechWeapons::Gun(static_cast<EMechShooter>(Index)).Name : MechWeapons::Blade(static_cast<EMechMeleeWeapon>(Index - NumGuns)).Name;
	auto Shot = [&](const TCHAR* Tag) {
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Weapons") /
			FString::Printf(TEXT("%02d_%s_%s.png"), Index, *Name, Tag), true, false);
	};
	if (At(0.f) || (SlotIndex == 0 && L0 <= 0.f && L > 0.f))
	{
		FMechLoadout Want = Me->GetLoadout();
		if (bGun)
		{
			Want.Shooter = static_cast<EMechShooter>(Index);
		}
		else
		{
			Want.Melee = static_cast<EMechMeleeWeapon>(Index - NumGuns);
		}
		ServerApplyLoadout(Want);
		if (!bGun && Combat->GetStance() == EMechStance::Gun)
		{
			Combat->SwitchStance();
		}
		// 站到木桩正前方：枪 18 米，刀 5 米，匕首 15 米
		const float Dist = bGun ? 1800.f : (MechWeapons::Blade(Want.Melee).Knives > 0 ? 1500.f : 500.f);
		const FVector Away = Dummy->GetActorForwardVector().GetSafeNormal2D();
		Me->SetActorLocation(Dummy->GetActorLocation() + Away * Dist + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
		Me->SetActorRotation(FRotator(0.f, (-Away).Rotation().Yaw, 0.f));
	}
	// 一直对着木桩：精准步枪、狙击枪瞄头（看分部位伤害），其他瞄胸口
	const bool bAimHead = bGun && Combat->GetGun().Spread * Combat->GetGun().AimSpread < 0.1f;
	const FVector AimAt = bAimHead ? Dummy->GetMechMesh()->GetBoneLocation(TEXT("head")) + FVector(0.f, 0.f, 14.f) : Dummy->GetMechMesh()->GetBoneLocation(TEXT("spine_03"));
	FRotator AimRot = (AimAt - PlayerCameraManager->GetCameraLocation()).Rotation();
	if (bGun && L > 3.2f && L < 4.f)
	{
		AimRot.Pitch += 20.f;  // 抬高瞄一下：枪口要跟着准星往上
	}
	SetControlRotation(AimRot);
	if (bGun && At(3.9f))
	{
		const UStaticMeshComponent* GunMesh = Me->GetMechMesh()->Props[1] ? Cast<UStaticMeshComponent>(Me->GetMechMesh()->Props[1].Get()) : nullptr;
		if (GunMesh)
		{
			const float Deg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(GunMesh->GetForwardVector() | Combat->GetAimDirection(), -1.f, 1.f)));
			UE_LOG(LogTemp, Display, TEXT("[weapontest] %s 抬高 20 度后：枪管偏离瞄准方向 %.1f 度（瞄准俯仰 %.1f 度）"), *Name, Deg, Combat->GetAimDirection().Rotation().Pitch);
		}
	}
	if (At(0.1f))
	{
		Combat->DebugPartHits[0] = Combat->DebugPartHits[1] = Combat->DebugPartHits[2] = 0;
	}
	if (bGun)
	{
		const MechWeapons::FGun& G = Combat->GetGun();
		const bool bFiring = L > 0.6f && L < 5.f;
		if (G.ChargeTime > 0.f)
		{
			Combat->SetTrigger(bFiring && FMath::Fmod(L - 0.6f, 1.3f) < 1.f);  // 按住 1 秒充满、松开
		}
		else
		{
			Combat->SetTrigger(bFiring);
		}
		Combat->SetAltHeld(bFiring && (G.EnergyPerShot > 0.f || G.ChargeTime > 0.f));  // 狙击枪、精准步枪开镜（打准星，瞄头）
		if (At(2.6f))
		{
			Shot(TEXT("开火"));
			// 枪管（模型 +X）和瞄准方向差多少度：枪口有没有跟着准星走
			const UStaticMeshComponent* GunMesh = Me->GetMechMesh()->Props[1] ? Cast<UStaticMeshComponent>(Me->GetMechMesh()->Props[1].Get()) : nullptr;
			if (GunMesh)
			{
				const float Deg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(GunMesh->GetForwardVector() | Combat->GetAimDirection(), -1.f, 1.f)));
				UE_LOG(LogTemp, Display, TEXT("[weapontest] %s 枪管偏离瞄准方向 %.1f 度（瞄准俯仰 %.1f 度）"), *Name, Deg, Combat->GetAimDirection().Rotation().Pitch);
			}
		}
		if (G.bDual && At(4.2f))
		{
			UE_LOG(LogTemp, Display, TEXT("[weapontest] %s 闪避前弹药 %d"), *Name, Combat->GetAmmo());
			Me->QuickBoostToward(Me->GetActorRightVector());
		}
		if (G.bDual && At(4.6f))
		{
			UE_LOG(LogTemp, Display, TEXT("[weapontest] %s 闪避后弹药 %d"), *Name, Combat->GetAmmo());
		}
		if (At(5.3f))
		{
			UE_LOG(LogTemp, Display, TEXT("[weapontest] %s：上一击 %.0f 这一轮 %.0f 每秒 %.0f | 弹药 %d 热量 %.2f 过热 %d 能量 %.0f 连中 %d | 部位 躯干 %d 头 %d 四肢 %d"),
				*Name, Dummy->GetDummyLastHit(), Dummy->GetDummyStreakDamage(), Dummy->GetDummyDPS(), Combat->GetAmmo(), Combat->GetHeat(),
				Combat->IsOverheated() ? 1 : 0, Me->GetMechMovement()->Energy, Combat->GetStreak(), Combat->DebugPartHits[0], Combat->DebugPartHits[1],
				Combat->DebugPartHits[2]);
		}
		return;
	}
	// 刀：四段轻击、停一下、一记重斩（匕首：点按投一把 ×4，再按住扇形投）
	const MechWeapons::FBlade& B = Combat->GetBlade();
	if (B.Knives > 0)
	{
		// 10-30 起匕首要按住 0.4 秒走满一圈才投：按住 0.5 秒投一把，连投 5 把（第 5 把是强力匕首），各拍一张飞行中的尾迹
		for (int32 k = 0; k < 5; ++k)
		{
			const float Press = 1.f + k * 0.75f;
			if (At(Press))
			{
				Combat->MeleePressed();
			}
			if (At(Press + 0.5f))
			{
				Combat->MeleeReleased();
			}
		}
		if (At(1.f + 0.5f + 0.09f))
		{
			Shot(TEXT("飞刀"));
		}
		if (At(1.f + 4 * 0.75f + 0.5f + 0.09f))
		{
			Shot(TEXT("强力飞刀"));
		}
		if (At(7.f))
		{
			UE_LOG(LogTemp, Display, TEXT("[weapontest] %s：上一击 %.0f 这一轮 %.0f | 匕首 %d"), *Name, Dummy->GetDummyLastHit(), Dummy->GetDummyStreakDamage(), Combat->GetKnives());
		}
		return;
	}
	for (int32 k = 0; k < 4; ++k)
	{
		const float Press = 1.f + k * 0.42f * B.TimeScale;
		if (At(Press))
		{
			Combat->MeleePressed();
		}
		if (At(Press + 0.05f))
		{
			Combat->MeleeReleased();
		}
	}
	if (At(1.f + 0.3f * B.TimeScale))
	{
		Shot(TEXT("轻击"));
	}
	const float HeavyAt = B.Knives > 0 ? 3.6f : 3.f + 0.8f * B.TimeScale;
	if (At(HeavyAt))
	{
		Combat->MeleePressed();
	}
	if (At(HeavyAt + 0.3f))
	{
		Combat->MeleeReleased();
	}
	if (At(HeavyAt + 0.22f + (B.Knives > 0 ? 0.1f : 0.38f * B.TimeScale)))
	{
		Shot(B.Knives > 0 ? TEXT("扇形") : TEXT("重斩"));
	}
	if (B.Knives == 0 && At(HeavyAt + 0.95f + 0.38f * B.TimeScale))
	{
		Shot(TEXT("刀痕"));  // 10-31 近战特效：重斩后半秒多，木桩身上的刀痕在冷却
	}
	// 只测刀时（-MechWeaponTestBlades）从侧面拍第四段轻击、重斩的刀光（11-01 刀光第二版：第三人称时刀光常在机体身后，看不全）：
	// 临时换到一台摄像机，拍完换回机体；-MechTrailOld 跑一遍就是同机位同时刻的第一版刀光
	auto SideCam = [&](bool bOn) {
		if (bOn)
		{
			const FVector Chest = Me->GetMechMesh()->GetBoneLocation(TEXT("spine_03"));
			const FVector Mid = (Chest + Dummy->GetMechMesh()->GetBoneLocation(TEXT("spine_03"))) * 0.5f;
			const FVector CamAt = Mid + Me->GetActorRightVector() * 750.f - Me->GetActorForwardVector() * 150.f + FVector(0.f, 0.f, 120.f);
			ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(CamAt, (Mid - CamAt).Rotation());
			if (Cam)
			{
				Cam->GetCameraComponent()->SetFieldOfView(55.f);
				SetViewTarget(Cam);
				WeaponTestCam = Cam;
			}
		}
		else
		{
			SetViewTarget(Me);
			if (WeaponTestCam)
			{
				WeaponTestCam->Destroy();
				WeaponTestCam = nullptr;
			}
		}
	};
	if (bBladesOnly && B.Knives == 0)
	{
		// 重斩侧看用第二记重斩（第一记的第三人称截图时刻不变）；镜头提前切好：离线渲染帧率低，切镜头和截图挤在同一帧就还是机体视角
		const float Light4At = 1.f + 3.f * 0.42f * B.TimeScale + 0.22f * B.TimeScale;
		const float Heavy2At = HeavyAt + 2.2f;
		// 重斩出刀早晚每把刀不一样：连拍四张（出招后 0.25 / 0.4 / 0.55 / 0.7 秒）
		const float Heavy2ShotAt = Heavy2At + 0.25f;
		for (int32 k = 1; k < 4; ++k)
		{
			if (At(Heavy2ShotAt + 0.15f * k))
			{
				Shot(*FString::Printf(TEXT("重斩侧看%d"), k + 1));
			}
		}
		if (At(Heavy2At))
		{
			Combat->MeleePressed();
		}
		if (At(Heavy2At + 0.3f))
		{
			Combat->MeleeReleased();
		}
		if (At(Light4At - 0.12f) || At(Heavy2At - 0.15f))
		{
			SideCam(true);
		}
		if (At(Light4At))
		{
			Shot(TEXT("轻击侧看"));
		}
		if (At(Heavy2ShotAt))
		{
			Shot(TEXT("重斩侧看1"));
		}
		if (At(Light4At + 0.12f) || At(Heavy2ShotAt + 0.6f))
		{
			SideCam(false);
		}
	}
	// 只测刀时（-MechWeaponTestBlades）再从木桩侧前方近处拍一张刀痕：临时换到一台摄像机，拍完换回机体
	if (bBladesOnly && B.Knives == 0)
	{
		const float CloseAt = HeavyAt + 0.75f + 0.38f * B.TimeScale;
		if (At(CloseAt))
		{
			const FVector Toward = (Me->GetActorLocation() - Dummy->GetActorLocation()).GetSafeNormal2D();
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward);
			const FVector Look = Dummy->GetMechMesh()->GetBoneLocation(TEXT("spine_03"));
			const FVector CamAt = Look + Toward * 170.f + Side * 230.f + FVector(0.f, 0.f, 40.f);
			ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(CamAt, (Look - CamAt).Rotation());
			if (Cam)
			{
				Cam->GetCameraComponent()->SetFieldOfView(50.f);
				SetViewTarget(Cam);
				WeaponTestCam = Cam;
			}
		}
		if (At(CloseAt + 0.08f))
		{
			Shot(TEXT("刀痕近看"));
		}
		if (At(CloseAt + 0.2f))
		{
			SetViewTarget(Me);
			if (WeaponTestCam)
			{
				WeaponTestCam->Destroy();
				WeaponTestCam = nullptr;
			}
		}
	}
	if (At(7.f))
	{
		UE_LOG(LogTemp, Display, TEXT("[weapontest] %s：上一击 %.0f 这一轮 %.0f 每秒 %.0f | 失衡 %.0f%% 匕首 %d"), *Name, Dummy->GetDummyLastHit(),
			Dummy->GetDummyStreakDamage(), Dummy->GetDummyDPS(), Dummy->GetHealth()->GetStaggerFraction() * 100.f, Combat->GetKnives());
	}
}

void AMechPlayerController::MenuOpenRange()
{
	MechMenuNet::CancelPendingJoin(this);
	SetInputMode(FInputModeGameOnly());
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Maps/L_Range")), true, TEXT("Range"));
}

void AMechPlayerController::MenuApplyLoadout(const FMechLoadout& Loadout)
{
	FMechSettings::Get().Loadout = Loadout;
	FMechSettings::Get().Save();
	ServerApplyLoadout(Loadout);
}

void AMechPlayerController::ShoulderTestTick(float DeltaTime)
{
	// 钩爪拉木桩（25 米）→ 钩爪抓前方 30 米的地面拉自己 → 捕获网罩木桩（20 米），每段截图、记位置
	AMechCharacter* Me = Cast<AMechCharacter>(GetPawn());
	if (!Me)
	{
		return;
	}
	AMechCharacter* Dummy = nullptr;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsTrainingDummy())
		{
			Dummy = *It;
			break;
		}
	}
	if (!Dummy)
	{
		return;
	}
	// 时间轴从机体和木桩都在了才开始走；-MechShoulderFrom=秒 跳到那一段（比如 45.5 只测 EMP 以后，配 -MechHour=21.5 看夜里），
	// -MechShotDir=子目录 截图换个目录（夜里那一轮不覆盖白天的）
	static bool bFromApplied = false;
	if (!bFromApplied)
	{
		bFromApplied = true;
		FParse::Value(FCommandLine::Get(), TEXT("MechShoulderFrom="), ShoulderTestClock);
	}
	const float T0 = ShoulderTestClock;
	ShoulderTestClock += DeltaTime;
	const float T = ShoulderTestClock;
	auto At = [&](float S) { return T0 < S && T >= S; };
	UMechShoulderComponent* Sh = Me->GetShoulder();
	static FString ShotDir = TEXT("Shoulder");
	static bool bShotDir = FParse::Value(FCommandLine::Get(), TEXT("MechShotDir="), ShotDir);
	(void)bShotDir;
	auto Shot = [&](const TCHAR* Tag) {
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots") / ShotDir / FString::Printf(TEXT("%s.png"), Tag), true, false);
	};
	auto Log = [&](const TCHAR* Tag) {
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] %s：自己 %s 木桩 %s 相距 %.0f | 木桩被网住 %d 装甲 %.0f | 钩爪状态 %d 冷却 %.1f | 钩住 %d 抓地形 %d 网中 %d"), Tag,
			*Me->GetActorLocation().ToCompactString(), *Dummy->GetActorLocation().ToCompactString(), FVector::Dist(Me->GetActorLocation(), Dummy->GetActorLocation()),
			Dummy->IsNetted() ? 1 : 0, Dummy->GetHealth()->GetAP(), static_cast<int32>(Sh->GetGrappleState()), Sh->GetCooldown(), Sh->DebugHooks, Sh->DebugLatches,
			Sh->DebugNets);
	};
	const FVector Away = Dummy->GetActorForwardVector().GetSafeNormal2D();
	auto StandAt = [&](float Dist) {
		Me->SetActorLocation(Dummy->GetActorLocation() + Away * Dist + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
		Me->SetActorRotation(FRotator(0.f, (-Away).Rotation().Yaw, 0.f));
	};
	auto Equip = [&](EMechShoulder S) {
		FMechLoadout L = Me->GetLoadout();
		L.Shoulder = S;
		ServerApplyLoadout(L);
	};
	auto AimAtDummy = [&]() { SetControlRotation((Dummy->GetMechMesh()->GetBoneLocation(TEXT("spine_03")) - PlayerCameraManager->GetCameraLocation()).Rotation()); };

	if (GetNetMode() == NM_Client)
	{
		// 联机客户端（不能自己瞬移）：原地抓右前方 22 米的地面拉自己两次，再钩一次木桩；看服务器纠正了几次位置（预测和服务器对得上就几乎不纠正）
		static int32 Corrections0 = 0;
		static FVector From = FVector::ZeroVector;
		const UMechMovementComponent* Move = Me->GetMechMovement();
		auto Report = [&](const TCHAR* Tag) {
			UE_LOG(LogTemp, Display, TEXT("[shouldernet] %s：走了 %.0f 厘米，这段纠正 %d 次（累计 %d），钩爪状态 %d"), Tag, FVector::Dist(From, Me->GetActorLocation()),
				Move->CorrectionCount - Corrections0, Move->CorrectionCount, static_cast<int32>(Sh->GetGrappleState()));
		};
		auto GroundShot = [&]() {
			const FVector Dir = FRotator(0.f, Me->GetActorRotation().Yaw + 40.f, 0.f).Vector();
			const FVector Ground = Me->GetActorLocation() + Dir * 2200.f - FVector(0.f, 0.f, Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			SetControlRotation((Ground - PlayerCameraManager->GetCameraLocation()).Rotation());
		};
		if (At(5.f))
		{
			Equip(EMechShoulder::Grapple);
		}
		for (const float Fire : {7.f, 15.f})
		{
			if (T > Fire - 0.6f && T < Fire)
			{
				GroundShot();
			}
			if (At(Fire))
			{
				Corrections0 = Move->CorrectionCount;
				From = Me->GetActorLocation();
				Me->MissilePressedForTest();
			}
			if (At(Fire + 2.5f))
			{
				Report(TEXT("抓地面拉自己"));
			}
		}
		if (T > 22.4f && T < 25.f)
		{
			AimAtDummy();
		}
		if (At(23.f))
		{
			Corrections0 = Move->CorrectionCount;
			From = Dummy->GetActorLocation();
			Me->MissilePressedForTest();
		}
		if (At(25.f))
		{
			UE_LOG(LogTemp, Display, TEXT("[shouldernet] 钩木桩：木桩移动了 %.0f 厘米，自己这段纠正 %d 次"), FVector::Dist(From, Dummy->GetActorLocation()),
				Move->CorrectionCount - Corrections0);
		}
		if (At(27.f))
		{
			ConsoleCommand(TEXT("quit"));
		}
		return;
	}

	// 0 预热：在面前 8 米炸一个没有射手的 EMP，让范围贴花的材质先编译好（第一次用到时现编译，那几帧画不出来，后面 EMP 那段的截图会空）
	if (At(1.f))
	{
		const FVector Drop = Me->GetActorLocation() + Me->GetActorForwardVector() * 800.f + FVector(0.f, 0.f, 100.f);
		const FTransform Where(FRotator::ZeroRotator, Drop);
		if (AMechGrenade* G = GetWorld()->SpawnActorDeferred<AMechGrenade>(AMechGrenade::StaticClass(), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			G->Init(nullptr, EMechGrenadeKind::Emp, Drop, FVector(0.f, 0.f, -500.f), 980.f);
			G->FinishSpawning(Where);
		}
	}
	if (At(1.35f))
	{
		Shot(TEXT("00_EMP预热"));
	}
	// 1 钩爪拉木桩
	if (At(3.f))
	{
		Equip(EMechShoulder::Grapple);
		StandAt(2500.f);
	}
	if (T > 3.f && T < 7.f)
	{
		AimAtDummy();
	}
	if (At(3.6f))
	{
		Shot(TEXT("01_钩爪就绪"));
		Log(TEXT("钩爪拉木桩 前"));
	}
	if (At(4.f))
	{
		Me->MissilePressedForTest();
	}
	if (At(4.15f))
	{
		Shot(TEXT("02_钩爪飞出"));
	}
	if (At(4.45f))
	{
		Shot(TEXT("03_钩住拉过来"));
		Log(TEXT("钩爪拉木桩 中"));
	}
	if (At(5.6f))
	{
		Shot(TEXT("04_拉到身前"));
		Log(TEXT("钩爪拉木桩 后"));
	}
	// 2 钩爪抓前方 30 米的地面，把自己拉过去（背对木桩）
	if (At(12.f))
	{
		Equip(EMechShoulder::Grapple);  // 换装清冷却
		StandAt(2000.f);
		Me->SetActorRotation(FRotator(0.f, Away.Rotation().Yaw, 0.f));
	}
	if (T > 12.f && T < 15.f)
	{
		const FVector Ground = Me->GetActorLocation() + Away * 3000.f - FVector(0.f, 0.f, Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		SetControlRotation((Ground - PlayerCameraManager->GetCameraLocation()).Rotation());
	}
	if (At(12.8f))
	{
		Log(TEXT("钩爪抓地面 前"));
		Me->MissilePressedForTest();
	}
	if (At(13.3f))
	{
		Shot(TEXT("05_抓地面拉自己"));
		Log(TEXT("钩爪抓地面 中"));
	}
	if (At(13.7f))
	{
		Shot(TEXT("05b_左手抓着无人机飞"));  // 10-30 第二轮：自拉时左手抓住无人机、被它拉过去
	}
	if (At(14.6f))
	{
		Shot(TEXT("06_拉到了"));
		Log(TEXT("钩爪抓地面 后"));
	}
	// 3 捕获网
	if (At(17.f))
	{
		Equip(EMechShoulder::Net);
		StandAt(2000.f);
	}
	if (T > 17.f && T < 21.5f)
	{
		AimAtDummy();
	}
	if (At(17.6f))
	{
		Shot(TEXT("07_网就绪"));
	}
	if (At(18.f))
	{
		Log(TEXT("网 前"));
		Me->MissilePressedForTest();
	}
	if (At(18.2f))
	{
		Shot(TEXT("08_网飞出张开"));
	}
	if (At(18.75f))
	{
		Shot(TEXT("09_网罩住"));
		Log(TEXT("网 中"));
	}
	if (At(18.95f))
	{
		StandAt(750.f);  // 走近看罩住的网
	}
	if (At(19.4f))
	{
		Shot(TEXT("10_网通电_近"));
	}
	if (At(21.f))
	{
		Shot(TEXT("11_网挣脱后"));
		Log(TEXT("网 后"));
	}
	// 4 网打空（打天上），看飞到头掉下来
	if (At(22.f))
	{
		Equip(EMechShoulder::Net);
		SetControlRotation(FRotator(8.f, Away.Rotation().Yaw + 60.f, 0.f));
	}
	if (At(22.5f))
	{
		Me->MissilePressedForTest();
	}
	if (At(23.2f))
	{
		Shot(TEXT("12_网打空"));
	}
	if (At(24.6f))
	{
		Shot(TEXT("13_网掉地"));
	}
	// 5 火炮：35 米外按住 E 架炮、架好后松开开炮
	if (At(26.f))
	{
		Equip(EMechShoulder::Artillery);
		StandAt(3500.f);
	}
	if (T > 26.f && T < 29.f)
	{
		AimAtDummy();
	}
	if (At(26.45f))
	{
		Shot(TEXT("14a_火炮无人机就绪"));
	}
	if (At(26.6f))
	{
		Log(TEXT("火炮 前"));
		Me->MissilePressedForTest();
	}
	if (At(27.3f))
	{
		Shot(TEXT("14_火炮架好开镜"));
	}
	if (At(27.4f))
	{
		Me->MissileReleasedForTest();
	}
	if (At(27.45f))
	{
		Shot(TEXT("14c_开炮瞬间"));
	}
	if (At(27.62f))
	{
		Shot(TEXT("15_炮弹炸开"));
	}
	if (At(28.6f))
	{
		Log(TEXT("火炮 后"));
		Shot(TEXT("16_火炮之后"));
	}
	// 6 护盾：木桩对着自己开枪，开盾 3 秒内不掉装甲，盾没了才掉
	if (At(31.f))
	{
		Equip(EMechShoulder::Shield);
		StandAt(1500.f);
	}
	if (T > 31.f && T < 38.f)
	{
		AimAtDummy();
	}
	if (At(31.5f))
	{
		Dummy->GetCombat()->SetForcedTarget(Me);
		Dummy->GetCombat()->SetTrigger(true);
	}
	if (At(32.f))
	{
		Me->MissilePressedForTest();
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] 开盾前装甲 %.0f"), Me->GetHealth()->GetAP());
	}
	if (At(33.f))
	{
		Shot(TEXT("17_护盾挡子弹"));
	}
	if (At(34.9f))
	{
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] 护盾快结束：装甲 %.0f 挡下 %d 次 盾还剩 %.2f"), Me->GetHealth()->GetAP(), Sh->DebugBlocks, Sh->GetShieldLeft());
	}
	if (At(36.5f))
	{
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] 盾没了 1.5 秒：装甲 %.0f"), Me->GetHealth()->GetAP());
		Dummy->GetCombat()->SetTrigger(false);
	}
	// 7 烟雾弹：丢在木桩脚下，看锁定是不是丢了
	if (At(39.f))
	{
		Equip(EMechShoulder::Smoke);
		StandAt(2000.f);
	}
	if (T > 39.f && T < 45.f)
	{
		AimAtDummy();
	}
	if (At(39.45f))
	{
		Shot(TEXT("18a_烟雾弹无人机就绪"));
	}
	if (At(39.6f))
	{
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] 烟雾弹前锁定 %s"), Me->GetCombat()->GetTarget() ? *Me->GetCombat()->GetTarget()->DisplayName : TEXT("无"));
		Me->MissilePressedForTest();
	}
	if (At(39.7f))
	{
		Shot(TEXT("18c_烟雾弹出膛"));
	}
	if (At(43.5f))
	{
		Shot(TEXT("18b_烟幕涨开"));
	}
	if (At(41.5f))
	{
		Shot(TEXT("18_烟幕"));
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] 烟雾里锁定 %s"), Me->GetCombat()->GetTarget() ? *Me->GetCombat()->GetTarget()->DisplayName : TEXT("无"));
	}
	// 8 EMP：丢到木桩身上（烟雾弹的烟有 13 秒，会挡住 EMP，先清掉）
	if (At(46.f))
	{
		for (TActorIterator<AMechSmoke> It(GetWorld()); It; ++It)
		{
			It->Destroy();
		}
		Equip(EMechShoulder::Emp);
		StandAt(2200.f);
	}
	if (T > 46.f && T < 50.f)
	{
		AimAtDummy();
	}
	if (At(46.45f))
	{
		Shot(TEXT("19_0_EMP无人机就绪"));
	}
	if (At(46.6f))
	{
		Me->MissilePressedForTest();
	}
	if (At(46.7f))
	{
		Shot(TEXT("19_1_EMP出膛"));
	}
	// EMP 炸开（约 47.75 秒，无人机在左肩外，飞得比以前略久）：炸开前后、扩到范围边（+0.3 秒）、外缘环和网格停着（+0.6 秒），再走近看木桩身上的电弧
	if (At(47.65f))
	{
		Shot(TEXT("19_EMP"));
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] EMP 后：木桩中 EMP %d 能量 %.0f 装甲 %.0f"), Dummy->IsEmped() ? 1 : 0, Dummy->GetMechMovement()->Energy,
			Dummy->GetHealth()->GetAP());
	}
	// 10-30 第三版闪电球：电光球（约 +0.15 秒）、炸散打地（约 +0.4 秒）、残余细电弧（约 +0.7 秒）
	if (At(47.9f))
	{
		Shot(TEXT("19a_EMP电光球"));
	}
	if (At(48.15f))
	{
		Shot(TEXT("19b_EMP炸散"));
	}
	if (At(48.45f))
	{
		Shot(TEXT("19b2_EMP残余"));
	}
	if (At(48.6f))
	{
		StandAt(650.f);
	}
	if (T > 48.6f && T < 51.5f)
	{
		AimAtDummy();
	}
	if (At(48.9f))
	{
		Shot(TEXT("20_EMP电弧_近"));
	}
	if (At(49.6f))
	{
		Shot(TEXT("21_EMP电弧_近2"));
	}
	if (At(51.f))
	{
		Shot(TEXT("22_EMP快结束"));
	}
	if (At(52.f))
	{
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] EMP 过去：木桩中 EMP %d"), Dummy->IsEmped() ? 1 : 0);
		// 再炸一个，这次炸在自己身后 2.5 米（镜头在肩后 5.6 米，自己丢的落不到镜头边上）：镜头在范围里，看会不会整屏泛白。
		// 没有射手（不然弹会从木桩的无人机那边「飞过来」并到判定位置），只看画面
		Equip(EMechShoulder::Emp);
		StandAt(900.f);
	}
	if (T > 52.f && T < 52.95f)
	{
		AimAtDummy();
	}
	if (At(52.15f))
	{
		const FVector Drop = Me->GetActorLocation() - Me->GetActorForwardVector() * 250.f + FVector(0.f, 0.f, 120.f);
		const FTransform Where(FRotator::ZeroRotator, Drop);
		if (AMechGrenade* G = GetWorld()->SpawnActorDeferred<AMechGrenade>(AMechGrenade::StaticClass(), Where, Dummy, Dummy, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			G->Init(nullptr, EMechGrenadeKind::Emp, Drop, FVector(0.f, 0.f, -500.f), 980.f);
			G->FinishSpawning(Where);
		}
	}
	if (At(52.62f))
	{
		Shot(TEXT("19c_EMP镜头在范围里"));
	}
	if (At(52.88f))
	{
		Shot(TEXT("19d_EMP镜头在范围里2"));
	}
	// 9 导弹：30 米外锁定木桩打一轮，看一轮实际掉多少装甲（[missile] 伤害 每发一行，带新旧系数）
	static float MissileAP0 = 0.f;
	if (At(53.f))
	{
		Equip(EMechShoulder::Missiles);
		StandAt(3000.f);
	}
	if (T > 53.f && T < 57.f)
	{
		AimAtDummy();
	}
	if (At(54.6f))
	{
		Shot(TEXT("23a_导弹无人机就绪"));
	}
	if (At(55.05f))
	{
		Shot(TEXT("23b_导弹出膛"));
	}
	if (At(54.8f))
	{
		MissileAP0 = Dummy->GetHealth()->GetAP();
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] 导弹 锁定 %.2f 木桩装甲 %.0f"), Me->GetCombat()->GetMissileLock(), MissileAP0);
		Me->MissilePressedForTest();
	}
	if (At(55.0f))
	{
		Me->MissileReleasedForTest();
	}
	if (At(55.9f))
	{
		Shot(TEXT("23_导弹"));
	}
	if (At(57.5f))
	{
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] 导弹一轮：木桩掉装甲 %.0f"), MissileAP0 - Dummy->GetHealth()->GetAP());
	}
	// 10 钩爪救队友：另一个木桩临时划到自己队、先罩上网，按住右键（射击姿态瞄准）对准它按 E，应该把它拉过来并解开网；
	// 再松开右键对它按一次，不应该勾到它
	static TWeakObjectPtr<AMechCharacter> PalW;
	AMechCharacter* Pal = PalW.Get();
	if (At(58.f))
	{
		for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
		{
			if (It->IsTrainingDummy() && *It != Dummy)
			{
				Pal = *It;
				break;
			}
		}
		PalW = Pal;
		if (!Pal)
		{
			UE_LOG(LogTemp, Display, TEXT("[shouldertest] 救队友：测试场只有一个木桩，跳过"));
		}
		else
		{
			Pal->SetTeam(Me->GetTeam());
			Equip(EMechShoulder::Grapple);
			const FVector PAway = Pal->GetActorForwardVector().GetSafeNormal2D();
			Me->SetActorLocation(Pal->GetActorLocation() + PAway * 2500.f + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
			Me->SetActorRotation(FRotator(0.f, (-PAway).Rotation().Yaw, 0.f));
		}
	}
	auto AimAtPal = [&]() { SetControlRotation((Pal->GetMechMesh()->GetBoneLocation(TEXT("spine_03")) - PlayerCameraManager->GetCameraLocation()).Rotation()); };
	if (Pal && T > 58.f && T < 66.f)
	{
		AimAtPal();
	}
	if (Pal && At(59.f))
	{
		Pal->GetShoulder()->ApplyNet(nullptr, nullptr);
		Me->GetCombat()->SetAltHeld(true);
	}
	if (Pal && At(59.6f))
	{
		// 木桩停手 4 秒、离原位 50 厘米以上会瞬移回去：先记一下「刚挨过打」，拉的过程看得完整
		Pal->ReceiveHit(1.f, 0.f, nullptr, Pal->GetActorLocation(), EMechHitKind::Bullet);
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] 救队友 前：按住右键 %d 准星提示 %d 锁定队友 %s，队友被网住 %d，相距 %.0f"), Sh->IsAllyMode() ? 1 : 0, Sh->GetAimHint(),
			Sh->GetAllyAim() ? *Sh->GetAllyAim()->DisplayName : TEXT("无"), Pal->IsNetted() ? 1 : 0, FVector::Dist(Me->GetActorLocation(), Pal->GetActorLocation()));
		Shot(TEXT("24_救队友_锁定"));
	}
	if (Pal && At(60.f))
	{
		Me->MissilePressedForTest();
	}
	if (Pal && At(60.5f))
	{
		Shot(TEXT("25_救队友_拉"));
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] 救队友 中：救了 %d 次，队友被网住 %d，装甲 %.0f，相距 %.0f"), Sh->DebugRescues, Pal->IsNetted() ? 1 : 0,
			Pal->GetHealth()->GetAP(), FVector::Dist(Me->GetActorLocation(), Pal->GetActorLocation()));
	}
	if (Pal && At(61.3f))
	{
		Shot(TEXT("26_救队友_到身前"));
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] 救队友 后：相距 %.0f"), FVector::Dist(Me->GetActorLocation(), Pal->GetActorLocation()));
		Me->GetCombat()->SetAltHeld(false);
	}
	if (Pal && At(62.f))
	{
		Equip(EMechShoulder::Grapple);  // 换装清冷却
		const FVector PAway = Pal->GetActorForwardVector().GetSafeNormal2D();
		Me->SetActorLocation(Pal->GetActorLocation() + PAway * 2000.f + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
		Me->SetActorRotation(FRotator(0.f, (-PAway).Rotation().Yaw, 0.f));
	}
	if (Pal && At(63.f))
	{
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] 不按右键对队友：准星提示 %d（应该不是 3）"), Sh->GetAimHint());
		Me->MissilePressedForTest();
	}
	if (Pal && At(64.f))
	{
		UE_LOG(LogTemp, Display, TEXT("[shouldertest] 不按右键对队友 后：救了 %d 次（应该还是 1），钩住敌人 %d"), Sh->DebugRescues, Sh->DebugHooks);
	}
	if (At(Pal ? 66.f : 58.5f))
	{
		if (Pal)
		{
			Pal->SetTeam(Dummy->GetTeam());
		}
		FMechSettings::Get().Loadout = FMechLoadout();
		FMechSettings::Get().Save();
		ConsoleCommand(TEXT("quit"));
	}
}

bool AMechPlayerController::IsRangeMode() const
{
	const AMechGameState* GS = GetWorld() ? GetWorld()->GetGameState<AMechGameState>() : nullptr;
	return GS && GS->bRangeMode;
}

void AMechPlayerController::ServerApplyLoadout_Implementation(const FMechLoadout& Loadout)
{
	if (AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>())
	{
		GM->ApplyRangeLoadout(this, Loadout);
	}
}

void AMechPlayerController::MenuQuit()
{
	ConsoleCommand(TEXT("quit"));
}

bool AMechPlayerController::CanRestartMatch() const
{
	return GetWorld() && GetWorld()->GetAuthGameMode() != nullptr;
}

void AMechPlayerController::Key(const FKey& InKey, bool bDown)
{
	if (bDown == Down.Contains(InKey))
	{
		return;
	}
	if (bDown)
	{
		Down.Add(InKey);
	}
	else
	{
		Down.Remove(InKey);
	}
	// 和真实按键走同一条路：玩家控制器 → 增强输入 → 按键映射 → 机体的输入绑定
	const FInputKeyEventArgs Args(nullptr, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), InKey, bDown ? IE_Pressed : IE_Released, 1.f, false,
		FPlatformTime::Cycles64());
	InputKey(Args);
}

void AMechPlayerController::Check(const FString& Name, bool bOk)
{
	if (bPilotShots)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		const FString Base = FString::Printf(TEXT("%02d_%s"), ++ShotCount, *FPaths::MakeValidFileName(Name.Left(14)));
		PendingShots.Emplace(Now, Base + TEXT("_a"));
		PendingShots.Emplace(Now + 0.35f, Base + TEXT("_b"));
	}
	(bOk ? Passed : Failed).Add(Name);
	UE_LOG(LogTemp, Display, TEXT("[pilot] %s %s"), bOk ? TEXT("通过") : TEXT("失败"), *Name);
}

void AMechPlayerController::PilotTick(float DeltaTime)
{
	for (int32 i = PendingShots.Num() - 1; i >= 0; --i)
	{
		if (GetWorld()->GetTimeSeconds() >= PendingShots[i].Key)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Pilot") / (PendingShots[i].Value + TEXT(".png")), true, false);
			PendingShots.RemoveAt(i);
			break;  // 一帧只截一张
		}
	}
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	AMechCharacter* Me = Cast<AMechCharacter>(GetPawn());
	const bool bFighting = GS && Me && GS->Phase == EMechRoundPhase::Fight && !Me->GetHealth()->IsDead();
	if (FightClock >= 0.f)
	{
		TotalClock += DeltaTime;
	}
	// 结束：单机检查完就退；联机打满 75 秒或整场结束就退
	if ((!bNetTest && Step >= 100) || (bNetTest && TotalClock > 75.f) || (GS && GS->Phase == EMechRoundPhase::MatchOver && FightClock >= 0.f))
	{
		const UMechMovementComponent* M = Me ? Me->GetMechMovement() : nullptr;
		UE_LOG(LogTemp, Display, TEXT("[pilot] 结束：%s 通过 %d 失败 %d，收到服务器移动纠正 %d 次，自己 AP %.0f"), HasAuthority() ? TEXT("主机") : TEXT("客户端"),
			Passed.Num(), Failed.Num(), M ? M->CorrectionCount : -1, Me ? Me->GetHealth()->GetAP() : -1.f);
		ConsoleCommand(TEXT("quit"));
		bPilot = false;
		return;
	}
	if (!bFighting)
	{
		for (const FKey& K : Down.Array())
		{
			Key(K, false);
		}
		return;
	}
	if (GS->Round != CheckRound)
	{
		if (CheckRound >= 0)
		{
			UE_LOG(LogTemp, Display, TEXT("[pilot] 第 %d 回合：新机体，从头再查一遍按键"), GS->Round);
			Step = 0;
			StepClock = 0.f;
			Retries = 0;
		}
		CheckRound = GS->Round;
	}
	if (FightClock < 0.f)
	{
		FightClock = 0.f;
	}
	FightClock += DeltaTime;
	StepClock += DeltaTime;
	UMechCombatComponent* Combat = Me->GetCombat();
	UMechMovementComponent* Move = Me->GetMechMovement();

	// 镜头对准最近的敌人（鼠标视角不在测试范围内）
	AMechCharacter* Enemy = nullptr;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetTeam() != Me->GetTeam() && !It->GetHealth()->IsDead() &&
			(!Enemy || FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist(Enemy->GetActorLocation(), Me->GetActorLocation())))
		{
			Enemy = *It;
		}
	}
	const float Dist = Enemy ? FVector::Dist2D(Enemy->GetActorLocation(), Me->GetActorLocation()) : 1e9f;
	if (Enemy)
	{
		SetControlRotation(FMath::RInterpTo(GetControlRotation(), (Enemy->GetActorLocation() - Me->GetActorLocation() - FVector(0.f, 0.f, 150.f)).Rotation(),
			DeltaTime, 8.f));
	}
	auto Next = [&]() { ++Step; StepClock = 0.f; Peak = 0.f; StartZ = Me->GetActorLocation().Z; bSawThrust = false; bStaggeredDuring = false; };
	// 被打失衡时机体本来就不能动、不能攻击：这段时间的检查不算，重来（最多 3 次）
	const bool bStaggered = Me->GetHealth()->IsStaggered();
	bStaggeredDuring |= bStaggered;
	const bool bReady = Me->CanAct() && !bStaggered;
	// 场地检查：闪避前方、跳跃头顶要有空间（撞墙、钻到高架下面不算按键失灵）；等 6 秒还不行就照按，日志里记下挡着的东西
	auto Blocked = [&](const FVector& Dir, float Length, FString& What) {
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PilotClearance), false, Me);
		const FVector From = Me->GetActorLocation();
		if (GetWorld()->SweepSingleByChannel(Hit, From, From + Dir * Length, FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(60.f), Params))
		{
			What = FString::Printf(TEXT("%s 在 %.1f 米处"), *GetNameSafe(Hit.GetActor()), Hit.Distance / 100.f);
			return true;
		}
		return false;
	};
	FString Obstacle;
	auto Verify = [&](const FString& Name, bool bOk, int32 RetryStep) {
		if (!bOk && bStaggeredDuring && Retries < 3)
		{
			++Retries;
			UE_LOG(LogTemp, Display, TEXT("[pilot]   %s：期间被打失衡，重试"), *Name);
			Step = RetryStep;
			StepClock = 0.f;
			return;
		}
		Check(Name, bOk);
		Retries = 0;
		Next();
	};

	// 按键都按设置里的键位注入（玩家改过键也照样能测；闪避默认 F）
	auto K = [](EMechKey Action) { return MechKeys::Key(Action); };
	switch (Step)
	{
	case 0:  // 左键按住开火
		if (FightClock > 1.f && bReady) { Key(K(EMechKey::Fire), true); Next(); }
		break;
	case 1:
		if (StepClock > 1.6f) { Key(K(EMechKey::Fire), false); Verify(TEXT("左键 步枪开火"), Combat->GetAmmo() < Combat->GetMagazine() || Combat->IsReloading(), 0); }
		break;
	case 2:  // R 换弹（弹匣满的话先打两发）
		if (StepClock > 0.2f && bReady) { Key(K(EMechKey::Reload), true); Next(); }
		break;
	case 3:
		if (StepClock > 0.4f) { Key(K(EMechKey::Reload), false); Verify(TEXT("R 换弹"), Combat->IsReloading() || Combat->GetAmmo() == Combat->GetMagazine(), 2); }
		break;
	case 4:  // W + Shift 冲刺
		if (bReady) { Key(K(EMechKey::MoveForward), true); Key(K(EMechKey::Boost), true); Next(); }
		break;
	case 5:
		Peak = FMath::Max(Peak, Move->Velocity.Size2D());
		if (StepClock > 1.4f) { Key(K(EMechKey::Boost), false); Verify(FString::Printf(TEXT("W+Shift 冲刺（最高 %.0f km/h）"), Peak * 0.036f), Peak > 1000.f, 4); }
		break;
	case 6:  // 快速闪避（默认 F，按设置里的键）：挑前 / 右 / 左 / 后里 9 米内没墙的方向，按住那个方向键再按闪避键（贴着墙闪避本来就冲不出去）
		if (StepClock > 0.5f && bReady && DodgeKey == EKeys::Invalid)
		{
			const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
			const TPair<FKey, FVector> Dirs[] = {{K(EMechKey::MoveForward), Yaw.Vector()}, {K(EMechKey::MoveRight), Yaw.RotateVector(FVector::RightVector)},
				{K(EMechKey::MoveLeft), -Yaw.RotateVector(FVector::RightVector)}, {K(EMechKey::MoveBack), -Yaw.Vector()}};
			for (const auto& D : Dirs)
			{
				FString What;
				if (!Blocked(D.Value, 900.f, What))
				{
					DodgeKey = D.Key;
					break;
				}
				Obstacle += (Obstacle.IsEmpty() ? TEXT("") : TEXT("；")) + D.Key.ToString() + TEXT(" ") + What;
			}
			if (DodgeKey == EKeys::Invalid && StepClock > 6.f)
			{
				UE_LOG(LogTemp, Display, TEXT("[pilot]   闪避：四周都被挡（%s），照按"), *Obstacle);
				DodgeKey = K(EMechKey::MoveForward);
			}
			if (DodgeKey != EKeys::Invalid)
			{
				Key(K(EMechKey::MoveForward), false);
				Key(DodgeKey, true);
				DodgeClock = StepClock;
			}
		}
		if (DodgeKey != EKeys::Invalid && StepClock > DodgeClock + 0.2f)
		{
			Key(K(EMechKey::QuickBoost), true);
			Next();
		}
		break;
	case 7:
		Peak = FMath::Max(Peak, Move->Velocity.Size2D());
		if (StepClock > 0.3f)
		{
			Key(K(EMechKey::QuickBoost), false);
			Key(DodgeKey, false);
			Key(K(EMechKey::MoveForward), true);
			const FString Name = FString::Printf(TEXT("%s 快速闪避（%s 方向，瞬间 %.0f km/h）"), *MechKeys::KeyName(K(EMechKey::QuickBoost)).ToString(), *DodgeKey.ToString(), Peak * 0.036f);
			DodgeKey = EKeys::Invalid;
			Verify(Name, Peak > 1900.f, 6);
		}
		break;
	case 8:  // 空格：起跳，按住喷射（落地、能动、头顶 6 米内没东西时再按）
		if (StepClock > 0.5f && !Move->IsFalling() && bReady && (!Blocked(FVector::UpVector, 600.f, Obstacle) || StepClock > 6.f))
		{
			if (!Obstacle.IsEmpty())
			{
				UE_LOG(LogTemp, Display, TEXT("[pilot]   跳跃：头顶一直被挡（%s），照按"), *Obstacle);
			}
			Key(K(EMechKey::Jump), true);
			Next();
		}
		break;
	case 9:
		Peak = FMath::Max(Peak, Me->GetActorLocation().Z - StartZ);
		bSawThrust |= Me->IsThrustingUp();
		if (StepClock > 2.f)
		{
			Key(K(EMechKey::Jump), false);
			Key(K(EMechKey::MoveForward), false);
			// 头顶有高架会挡住，所以以“喷射确实启动了”为准，升高多少只做记录
			Verify(FString::Printf(TEXT("空格 跳跃+喷射（升高 %.1f 米）"), Peak / 100.f), bSawThrust && Peak > 150.f, 8);
		}
		break;
	case 10:  // E 导弹：边往目标走边等锁定
		Key(K(EMechKey::MoveForward), Dist > 2500.f);
		if (Combat->GetMissileLock() >= 1.f && Combat->GetMissileReady() >= 1.f && bReady) { Key(K(EMechKey::Shoulder), true); Next(); }
		else if (StepClock > 15.f) { Check(TEXT("E 导弹（15 秒内没锁上目标）"), false); Key(K(EMechKey::MoveForward), false); Step = 12; StepClock = 0.f; }
		break;
	case 11:
		Key(K(EMechKey::Shoulder), false);
		if (StepClock > 0.5f) { Key(K(EMechKey::MoveForward), false); Verify(TEXT("E 导弹"), Combat->GetMissileReady() < 1.f, 10); }
		break;
	case 12:  // 射击姿态：右键按住瞄准
		if (StepClock > 0.3f && bReady) { Key(K(EMechKey::Alt), true); Next(); }
		break;
	case 13:
		bSawThrust |= Combat->IsAiming();
		if (StepClock > 0.5f) { Key(K(EMechKey::Alt), false); Verify(TEXT("右键 瞄准"), bSawThrust, 12); }
		break;
	case 14:  // Q 拔刀切近战
		if (StepClock > 0.3f && bReady && !Combat->IsMeleeActive()) { Key(K(EMechKey::Stance), true); Next(); }
		break;
	case 15:
		Key(K(EMechKey::Stance), false);
		if (StepClock > 0.9f) { Verify(TEXT("Q 切近战"), Combat->GetStance() == EMechStance::Blade, 14); }
		break;
	case 16:  // 冲到敌人跟前，左键点一下轻击
		Key(K(EMechKey::MoveForward), true);
		Key(K(EMechKey::Boost), true);
		if (Dist < 1300.f && bReady && !Combat->IsMeleeActive()) { Key(K(EMechKey::Fire), true); Next(); }
		else if (StepClock > 20.f) { Check(TEXT("左键 轻击（20 秒内没接近目标）"), false); Step = 19; StepClock = 0.f; }
		break;
	case 17:
		if (StepClock > 0.08f) { Key(K(EMechKey::Fire), false); }
		bSawThrust |= Combat->IsMeleeActive() && Combat->GetComboStep() > 0;
		if (StepClock > 0.5f) { Verify(TEXT("左键 轻击"), bSawThrust, 16); }
		break;
	case 18:  // 左键按住出重斩
		if (StepClock > 0.3f && bReady && !Combat->IsMeleeActive() && Combat->GetHeavyReady() >= 1.f) { Key(K(EMechKey::Fire), true); Next(); }
		else if (StepClock > 8.f) { Check(TEXT("左键按住 重斩（8 秒内没准备好）"), false); Step = 20; StepClock = 0.f; }
		break;
	case 19:
		bSawThrust |= Combat->GetMeleeMove() == EMechMelee::Heavy;
		if (StepClock > 0.6f) { Key(K(EMechKey::Fire), false); Verify(TEXT("左键按住 重斩"), bSawThrust, 18); }
		break;
	case 20:  // 右键按住格挡
		Key(K(EMechKey::MoveForward), false);
		Key(K(EMechKey::Boost), false);
		if (StepClock > 0.3f && bReady && !Combat->IsMeleeActive()) { Key(K(EMechKey::Alt), true); Next(); }
		break;
	case 21:
		bSawThrust |= Combat->IsGuarding();
		if (StepClock > 0.5f) { Key(K(EMechKey::Alt), false); Verify(TEXT("右键 格挡"), bSawThrust, 20); }
		break;
	case 22:  // Q 收刀切回射击
		if (StepClock > 0.3f && bReady && !Combat->IsMeleeActive()) { Key(K(EMechKey::Stance), true); Next(); }
		break;
	case 23:
		Key(K(EMechKey::Stance), false);
		if (StepClock > 0.9f) { Verify(TEXT("Q 切回射击"), Combat->GetStance() == EMechStance::Gun, 22); }
		break;
	case 24:
		Key(K(EMechKey::MoveForward), false);
		Key(K(EMechKey::Boost), false);
		++RoundsChecked;
		UE_LOG(LogTemp, Display, TEXT("[pilot] 第 %d 回合按键检查完成：累计通过 %d 项，失败 %d 项%s"), GS->Round, Passed.Num(), Failed.Num(),
			Failed.Num() ? *(TEXT("（") + FString::Join(Failed, TEXT("、")) + TEXT("）")) : TEXT(""));
		Next();
		break;
	default:  // 继续自动打：射击姿态左右横移、点射、锁上放导弹
	{
		const float T = FightClock;
		Key(FMath::Fmod(T, 4.f) < 2.f ? K(EMechKey::MoveLeft) : K(EMechKey::MoveRight), true);
		Key(FMath::Fmod(T, 4.f) < 2.f ? K(EMechKey::MoveRight) : K(EMechKey::MoveLeft), false);
		Key(K(EMechKey::MoveForward), Dist > 3500.f);
		Key(K(EMechKey::Boost), Dist > 5000.f);
		Key(K(EMechKey::Fire), FMath::Fmod(T, 2.2f) < 1.5f);
		Key(K(EMechKey::Shoulder), Combat->GetMissileLock() >= 1.f && Combat->GetMissileReady() >= 1.f && !Down.Contains(K(EMechKey::Shoulder)));
		if (!bNetTest && Step >= 25 && StepClock > 3.f && RoundsChecked >= RoundsWanted)
		{
			Step = 100;
		}
		break;
	}
	}
}

AMechGameMode::AMechGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AMechCharacter::StaticClass();
	PlayerControllerClass = AMechPlayerController::StaticClass();
	HUDClass = AMechHUD::StaticClass();
	GameStateClass = AMechGameState::StaticClass();
}

AMechGameState* AMechGameMode::GetMechGameState() const
{
	return GetGameState<AMechGameState>();
}

void AMechGameMode::StartPlay()
{
	Super::StartPlay();
	MechMapRules::UpdateBoosterLock(GetWorld());

	// 默认夜晚 + 雾：霓虹和路灯有体积光晕（路面积水是材质自带的，不靠天气）。测试时可用 -MechHour=20.5 -MechWeather=Fog 覆盖
	Hour = StartHour;
	WeatherName = StartWeather;
	if (GetWorld()->GetMapName().EndsWith(TEXT("L_Street")))
	{
		WeatherName = TEXT("LightRain");  // 街道图开阔、灯多，雾天整条街发白，小雨更像赛博朋克
	}
	else if (GetWorld()->GetMapName().Contains(TEXT("TechPark")))
	{
		// 相位电子园区（K051）：晴天下午 15 点，太阳从西南偏西约 40° 斜照，投影和明暗分区清楚。
		// 10-31 外观整修前是阴天（Overcast，太阳只剩 12%）：用户说整张图偏灰偏平、看不出投影。白色大面发白的问题改在材质 / 后期里
		// （地图里优先级 200 的后期体积压曝光 + 局部曝光）；云和雾是地图里 LightingDirector 的 TuningSettings（tools/ue_techpark_dress.py）
		Hour = 15.f;
		WeatherName = TEXT("Clear");
	}
	else if (GetWorld()->GetMapName().EndsWith(TEXT("L_Pillars")))
	{
		// 石柱山水（11-02）：上午的太阳，石柱立面明暗分明，水面亮
		Hour = 10.5f;
		WeatherName = TEXT("Clear");
	}
	else if (GetWorld()->GetMapName().EndsWith(TEXT("L_City")))
	{
		// 城区（K059 街道 + K005 楼）：阴天上午的漫射光，冷灰调；和夜晚街道、傍晚白塔园区区分开（雾天整条街会发白，不用）
		Hour = 11.f;
		WeatherName = TEXT("Overcast");
	}
	bDefenseMode = UGameplayStatics::HasOption(OptionsString, TEXT("Defense")) || FParse::Param(FCommandLine::Get(), TEXT("MechDefense"));
	bRangeMode = UGameplayStatics::HasOption(OptionsString, TEXT("Range")) || FParse::Param(FCommandLine::Get(), TEXT("MechRange")) || bDefenseMode;
	if (bRangeMode)
	{
		Hour = 13.f;  // 测试场：白天晴天，看得清
		WeatherName = TEXT("Clear");
	}
	{
		FString CoreArg;
		bCoreMode = !bRangeMode && (UGameplayStatics::HasOption(OptionsString, TEXT("Core")) || FParse::Param(FCommandLine::Get(), TEXT("MechCore")) ||
			FParse::Value(FCommandLine::Get(), TEXT("MechCore="), CoreArg));
	}
	if (bCoreMode)
	{
		Hour = 13.f;  // 夺芯灰盒：白天晴天
		WeatherName = TEXT("Clear");
	}
	FParse::Value(FCommandLine::Get(), TEXT("MechHour="), Hour);
	FParse::Value(FCommandLine::Get(), TEXT("MechWeather="), WeatherName);
	ApplyLighting();
	GetWorld()->Exec(GetWorld(), TEXT("DisableAllScreenMessages"));
	GAreScreenMessagesEnabled = false;  // 上面那条走 World 的命令不一定生效（贴图池超了的红字还在），直接关掉引擎的屏幕调试提示
	GetWorld()->Exec(GetWorld(), TEXT("r.Shadow.Virtual.AllowScreenOverflowMessages 0"));
	GetWorldTimerManager().SetTimer(LightingTimer, FTimerDelegate::CreateUObject(this, &AMechGameMode::ApplyLighting), 0.6f, false);

	// -MechResultsTest=Elim|DMTeam|DMSolo|Core：假数据直接进结算场景（MechResultsStage），截完图退出，不开对战
	if (AMechResultsStage::SetupTest(this))
	{
		return;
	}

	// 成图、演示、特效挑选模式只要一台机体站在出生点，不开对战
	bVfxGallery = FParse::Param(FCommandLine::Get(), TEXT("MechVfxGallery")) || FString(FCommandLine::Get()).Contains(TEXT("-MechVfxGallery="));
	if (FParse::Param(FCommandLine::Get(), TEXT("MechBeauty")) || FParse::Param(FCommandLine::Get(), TEXT("MechDemo")) ||
		FParse::Param(FCommandLine::Get(), TEXT("MechAnimTest")) || FParse::Param(FCommandLine::Get(), TEXT("MechViewer")) ||
		FParse::Param(FCommandLine::Get(), TEXT("MechViewerShots")) || FString(FCommandLine::Get()).Contains(TEXT("-MechViewerShots=")) || bVfxGallery ||
		UGameplayStatics::HasOption(OptionsString, TEXT("Viewer")))
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			AGameModeBase::RestartPlayer(PC);
		}
		return;
	}
	// 11-01 从房间开的这一场（房主在房间里点了开始，MechRoom.h）：跳过房间、等房间里的人进图、结算后回房间
	if (UMechRoomSubsystem* Sub = UMechRoomSubsystem::Get(this);
		Sub && Sub->bActive && Sub->bMatchPending && !UGameplayStatics::HasOption(OptionsString, TEXT("Room")))
	{
		Sub->bMatchPending = false;
		bFromRoom = true;
	}
	if (bRangeMode)
	{
		if (AMechGameState* GS = GetMechGameState())
		{
			GS->bRangeMode = true;
			if (const UMechRoomSubsystem* Sub = bFromRoom ? UMechRoomSubsystem::Get(this) : nullptr)
			{
				GS->Room = Sub->Settings;  // 保卫战：打完回房间（RoomDefenseTick）；测试场那套自由出生，不用等人
				GS->bRoomMatch = true;
				GS->bFriendlyFire = MechRoom::IsTraining(Sub->Settings) && Sub->Settings.bFriendlyFire;  // 11-02 训练场的友伤，SpawnRange 按这个分队伍号
			}
		}
		ComputeArena();
		SpawnRange();
		if (bDefenseMode)
		{
			// 保卫战：波次、杂兵、指挥所都在调度器里（MechDefense.h）
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			GetWorld()->SpawnActor<AMechDefenseDirector>(AMechDefenseDirector::StaticClass(), RangeStart, P);
		}
		return;
	}
	// 11-01 房间图（主菜单「开房」、回房间、联机_开房.bat）：只有房间界面。要在主菜单判断之前：开房失败（端口被占）时是单机，别当成主菜单
	if (UGameplayStatics::HasOption(OptionsString, TEXT("Room")))
	{
		SetupRoom();
		return;
	}
	// 主菜单：直接启动的单机（地址里没有 ?Match，命令行没有 -Mech 开头的测试 / 启动参数）
	bMenuMode = GetNetMode() == NM_Standalone && !UGameplayStatics::HasOption(OptionsString, TEXT("Match")) &&
		!FString(FCommandLine::Get()).Contains(TEXT("-Mech")) && !bCoreMode;
	if (bMenuMode)
	{
		if (AMechGameState* GS = GetMechGameState())
		{
			GS->bMenuMode = true;
		}
		SpawnShowcase();
		return;
	}
	FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));  // 每场 AI 性格不同（默认种子固定，先生成的蓝队每场抽到一样的性格）
	FParse::Value(FCommandLine::Get(), TEXT("MechTeam="), TeamSize);
	TeamSize = UGameplayStatics::GetIntOption(OptionsString, TEXT("Team"), TeamSize);
	TeamSize = FMath::Clamp(TeamSize, 1, 4);
	bAutoBattle = FParse::Param(FCommandLine::Get(), TEXT("MechAutoBattle"));
	bPlayerTest = FParse::Param(FCommandLine::Get(), TEXT("MechPlayerTest"));
	if (bAutoBattle)
	{
		MatchOverTime = 8.f;  // 结算页截一张图就退出
	}
	MatchOverTime += AMechResultsStage::ExtraMatchOverTime(GetWorld());  // 先播实时结算场景，之后结算表照旧停这么久
	if (bCoreMode)
	{
		SetupCoreMode();
	}
	// 对战规则：正常开局从房主上次的选择起（设置里存的），主菜单 / 开房的地址带 ?Mode=…；
	// 测试开关（-Mech 开头，-MechMatch 除外）从默认规则起，免得被设置里存的影响，再按命令行 -MechMode= / -MechDMSolo 等覆盖
	{
		FString Cmd = FCommandLine::Get();
		Cmd.ReplaceInline(TEXT("-MechMatch"), TEXT(""));
		Rules = Cmd.Contains(TEXT("-Mech")) ? FMechRules() : FMechSettings::Get().Rules;
		MechRules::ApplyOptions(Rules, OptionsString, FCommandLine::Get());
	}
	// 11-01 从房间开的：规则、每队台数、AI 强度、禁用开关全按房主在房间里选的（UMechRoomSubsystem 跨换图带过来）
	if (const UMechRoomSubsystem* Sub = bFromRoom ? UMechRoomSubsystem::Get(this) : nullptr)
	{
		Rules = Sub->Settings.Rules;
		bCustomRoster = true;  // 11-02 两队多少台按房间的人机名单（BuildRoster），地址里不再带台数
		if (AMechGameState* GS = GetMechGameState())
		{
			ApplyRoomToState(Sub->Settings);
			GS->bRoomMatch = true;
			GS->RoomExpected = Sub->Expected.Num();
		}
	}
	ElimTimeLimit = RoundTimeLimit;
	TeamCount[0] = TeamCount[1] = TeamSize;
	if (AMechGameState* GS = GetMechGameState())
	{
		GS->TeamSize = TeamSize;
		GS->TeamCount[0] = GS->TeamCount[1] = TeamSize;
	}
	ApplyRules();
	ComputeArena();
	FParse::Value(FCommandLine::Get(), TEXT("MechWaitPlayers="), WaitPlayers);
	{
		// 房间和选配：正常开局都走；自动对战只走选配（全 AI，顺便截选配界面）；按键 / 联机 / 性能测试直接开打
		const FString Cmd = FCommandLine::Get();
		const bool bScripted = bPlayerTest || WaitPlayers > 1 || Cmd.Contains(TEXT("-MechInputTest")) || Cmd.Contains(TEXT("-MechNetTest")) ||
			Cmd.Contains(TEXT("-MechPerf"));
		// 11-01 从房间开的：房间已经在房间图里开过了，这里不再进房间，直接禁用 / 选配
		bUseLobby = !bAutoBattle && !bScripted && !bFromRoom && !FParse::Param(FCommandLine::Get(), TEXT("MechNoLobby"));
		bUseDraft = (bUseLobby || bAutoBattle || bFromRoom) && !bScripted && !FParse::Param(FCommandLine::Get(), TEXT("MechNoDraft"));
		bUsePlugins = !FParse::Param(FCommandLine::Get(), TEXT("MechNoPlugins"));
	}
	if (WaitPlayers <= 1 && !bFromRoom)
	{
		bRoundsStarted = true;
		GetWorldTimerManager().SetTimer(StartTimer, FTimerDelegate::CreateUObject(this, &AMechGameMode::StartMatchFlow), 0.8f, false);
	}
	else if (bFromRoom)
	{
		// 11-01 从房间开的：非无缝换图后客户端要各自重连、载入地图（大图、隔海的朋友要几十秒），等房间里的人都进来再开选配，
		// 不然先到的房主一个人开了选配，后到的人没有位置。最多等 90 秒（房主也可以在等待页点「不等了」），没到的人进来后接管 AI
		if (AMechGameState* GS = GetMechGameState())
		{
			GS->PhaseEndTime = GetWorld()->GetTimeSeconds() + 90.f;
		}
		UE_LOG(LogTemp, Display, TEXT("[room] 从房间开局：%s，等 %d 名玩家进图"), *MechRoom::Describe(GetMechGameState()->Room), GetMechGameState()->RoomExpected);
	}
}

void AMechGameMode::ApplyLighting()
{
	ELightingWeatherType Weather = ELightingWeatherType::Rain;
	const int64 Value = StaticEnum<ELightingWeatherType>()->GetValueByNameString(WeatherName);
	if (Value != INDEX_NONE)
	{
		Weather = static_cast<ELightingWeatherType>(Value);
	}
	ULightingBlueprintLibrary::SetTimeOfDay(this, Hour);
	ULightingBlueprintLibrary::SetWeather(this, Weather, 0.f);
	if (ALightingDirector* Director = ULightingBlueprintLibrary::GetLightingDirector(this))
	{
		Director->FreezeTime(bFreezeTime);
		// 插件文档里的控制台命令，作为保险
		GetWorld()->Exec(GetWorld(), *FString::Printf(TEXT("lighting.Time %.2f"), Hour));
		GetWorld()->Exec(GetWorld(), *FString::Printf(TEXT("lighting.Weather %s 0"), *WeatherName));
	}
	UE_LOG(LogTemp, Display, TEXT("[lighting] 时间 %.2f 天气 %s"), Hour, *WeatherName);
}

// ---------------------------------------------------------------- 场地和出生点
void AMechGameMode::ComputeArena()
{
	const FString Map = GetWorld()->GetMapName();
	if (bCoreMode && CoreArena)
	{
		for (int32 Team = 0; Team < 2; ++Team)
		{
			SpawnBase[Team] = CoreArena->SpawnLocation(Team);
			SpawnYaw[Team] = CoreArena->SpawnYaw(Team);
		}
		ArenaCenter = CoreArena->Center();
		ArenaExtent = CoreArena->Extent();
		return;
	}
	// 新地图可以自己摆两队出生区：带「TeamSpawn0」「TeamSpawn1」标签的 actor（朝向 = 出生朝向）；场地范围包住出生区和 DMSpawn 点再放宽 25 米
	const AActor* TaggedSpawn[2] = {nullptr, nullptr};
	FBox TaggedBox(ForceInit);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		for (int32 Team = 0; Team < 2; ++Team)
		{
			if (!TaggedSpawn[Team] && It->ActorHasTag(Team == 0 ? FName(TEXT("TeamSpawn0")) : FName(TEXT("TeamSpawn1"))))
			{
				TaggedSpawn[Team] = *It;
				TaggedBox += It->GetActorLocation();
			}
		}
		if (It->ActorHasTag(TEXT("DMSpawn")))
		{
			TaggedBox += It->GetActorLocation();
		}
	}
	if (TaggedSpawn[0] && TaggedSpawn[1])
	{
		for (int32 Team = 0; Team < 2; ++Team)
		{
			SpawnBase[Team] = TaggedSpawn[Team]->GetActorLocation();
			SpawnYaw[Team] = TaggedSpawn[Team]->GetActorRotation().Yaw;
		}
		const FVector Mid = (SpawnBase[0] + SpawnBase[1]) * 0.5f;
		const FVector Ext = TaggedBox.ExpandBy(2500.f).GetExtent();
		const FVector Off = TaggedBox.ExpandBy(2500.f).GetCenter() - Mid;
		ArenaExtent = FVector2D(FMath::Max(4000.f, Ext.X + FMath::Abs(Off.X)), FMath::Max(4000.f, Ext.Y + FMath::Abs(Off.Y)));
	}
	else if (Map.EndsWith(TEXT("L_Street")))
	{
		// 街道沿 Y 轴，路中线 X≈9 米，两队各守一头，相距约 80 米
		SpawnBase[0] = FVector(900.f, 10200.f, 0.f);
		SpawnBase[1] = FVector(900.f, 2400.f, 0.f);
		SpawnYaw[0] = -90.f;
		SpawnYaw[1] = 90.f;
		ArenaExtent = FVector2D(2200.f, 5000.f);
	}
	else if (Map.Contains(TEXT("Alley")))
	{
		SpawnBase[0] = FVector(-5200.f, 0.f, 0.f);
		SpawnBase[1] = FVector(3600.f, 0.f, 0.f);
		SpawnYaw[0] = 0.f;
		SpawnYaw[1] = 180.f;
		ArenaExtent = FVector2D(5000.f, 800.f);
	}
	else
	{
		// 其他地图：蓝队在出生点，红队在出生点正前方 90 米
		FVector Base = FVector::ZeroVector;
		float Yaw = 0.f;
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			Base = It->GetActorLocation();
			Yaw = It->GetActorRotation().Yaw;
			break;
		}
		SpawnBase[0] = Base;
		SpawnYaw[0] = Yaw;
		SpawnBase[1] = Base + FRotator(0.f, Yaw, 0.f).Vector() * 9000.f;
		SpawnYaw[1] = Yaw + 180.f;
		ArenaExtent = FVector2D(7500.f, 7500.f);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("MechSwapSides")))
	{
		// 测试用：两队换边，检查胜负偏向是出生点造成的还是代码造成的
		Swap(SpawnBase[0], SpawnBase[1]);
		Swap(SpawnYaw[0], SpawnYaw[1]);
	}
	ArenaCenter = (SpawnBase[0] + SpawnBase[1]) * 0.5f;
	if (Map.EndsWith(TEXT("L_Street")))
	{
		ArenaCenter.X = 500.f;  // 路面在 X≈9 米，两侧人行道和高架下面都算场地
	}
	// 地图里有带「MechArena」标签的点（标签 HalfX= / HalfY= 厘米）：场地中心和范围按它定，和地图的隐形墙一致
	// （相位电子园区：出生点推算的框比墙大，中心高度也会落在出生的高台上；小地图、AI 回场、掉出判定都按这里）
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag(TEXT("MechArena")))
		{
			continue;
		}
		ArenaCenter = It->GetActorLocation();
		for (const FName& T : It->Tags)
		{
			const FString S = T.ToString();
			if (S.StartsWith(TEXT("HalfX=")))
			{
				ArenaExtent.X = FCString::Atof(*S.Mid(6));
			}
			else if (S.StartsWith(TEXT("HalfY=")))
			{
				ArenaExtent.Y = FCString::Atof(*S.Mid(6));
			}
		}
		break;
	}
	// 地图系统（HUD 小地图 / 大地图）按这个框拍场地俯视图；客户端没有 GameMode，放进 GameState 复制过去
	if (AMechGameState* GS = GetMechGameState())
	{
		GS->MapCenter = ArenaCenter;
		GS->MapExtent = ArenaExtent;
		GS->MapSpawn[0] = SpawnBase[0];
		GS->MapSpawn[1] = SpawnBase[1];
	}
	// 11-02 有会损毁的水的地图：给 AI 建地面格子（MechMapRules.h；没有 WaterKill 标记的图什么都不做）
	if (UMechWater* Water = GetWorld()->GetSubsystem<UMechWater>())
	{
		Water->BuildGrid(ArenaCenter, ArenaExtent);
	}
}

namespace
{
	/** 出生落地：从 P 上方 Up 往下到 P 下方 Down 找静态地面；起点在墙 / 楼里、坡太陡都算没找到 */
	bool SpawnGround(const UWorld* World, const FVector& P, float Up, float Down, FVector& Out)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MechSpawn), false);
		if (!World->LineTraceSingleByObjectType(Hit, P + FVector(0.f, 0.f, Up), P - FVector(0.f, 0.f, Down), FCollisionObjectQueryParams(ECC_WorldStatic), Params) ||
			Hit.bStartPenetrating || Hit.ImpactNormal.Z < 0.6f)
		{
			return false;
		}
		Out = Hit.ImpactPoint;
		return true;
	}
}

FVector AMechGameMode::SpawnBaseGround(int32 Team) const
{
	// 出生区基准地面：从基准点上方 3 米往下找（10-31 以前从 30 米高往下，街道两头头顶是高架 / 楼，红 1 落到 29 米高的楼顶、
	// 另两台落到 5 米高的高架上）；3 米内找不到（基准点本身埋在地下等）再按老办法从 30 米高往下
	Team = FMath::Clamp(Team, 0, 1);
	FVector Feet;
	if (SpawnGround(GetWorld(), SpawnBase[Team], 300.f, 3000.f, Feet) || SpawnGround(GetWorld(), SpawnBase[Team], 3000.f, 6000.f, Feet))
	{
		return Feet;
	}
	return SpawnBase[Team];
}

FTransform AMechGameMode::TeamSpawn(int32 Team, int32 Index) const
{
	Team = FMath::Clamp(Team, 0, 1);
	const FRotator Rot(0.f, SpawnYaw[Team], 0.f);
	const FVector Right = FRotationMatrix(Rot).GetUnitAxis(EAxis::Y);
	const FVector Forward = Rot.Vector();
	const float Half = 96.f * 1.35f;
	const FVector BaseFeet = SpawnBaseGround(Team);
	// 这一台沿左右排开 7 米。落点要和出生区地面在同一层：高差超过 3 米（车顶、高架、楼顶）或胶囊卡墙，
	// 就依次试：左右收一半、往后挪 6 米、往前 5 米、往后 12 米；都不行退回基准点（按序号往后错开 3 米，免得叠在一起）
	const float Lateral = (Index - (TeamCount[Team] - 1) * 0.5f) * 700.f;  // 11-02 两队台数可以不一样
	const FVector Tries[] = {
		Right * Lateral,
		Right * Lateral * 0.5f,
		Right * Lateral - Forward * 600.f,
		Right * Lateral * 0.5f - Forward * 600.f,
		Right * Lateral + Forward * 500.f,
		Right * Lateral - Forward * 1200.f,
	};
	for (const FVector& T : Tries)
	{
		FVector Feet;
		if (SpawnGround(GetWorld(), BaseFeet + T, 300.f, 1500.f, Feet) && FMath::Abs(Feet.Z - BaseFeet.Z) <= 300.f && IsClearSpot(Feet))
		{
			return FTransform(Rot, Feet + FVector(0.f, 0.f, Half + 15.f));
		}
	}
	return FTransform(Rot, BaseFeet - Forward * (300.f * Index) + FVector(0.f, 0.f, Half + 15.f));
}

// ---------------------------------------------------------------- 回合流程
void AMechGameMode::BeginRound()
{
	AMechGameState* GS = GetMechGameState();
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	// 真人先放开旧机体（下面整批重新生成）。自动对战时真人都观战
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* P = It->Get();
		if (P && P->GetPawn())
		{
			P->UnPossess();
		}
	}
	// 11-02 谁在哪个位置看阵容（RefreshRoster）：掉线的位置交给人机，新来的顶掉本队一个人机；空位不再自动补，人机名单是房主定的
	RefreshRoster();
	PublishChallenge();
	if (GS->ChallengeTeam >= 0 && GS->Round == 0)
	{
		GS->MulticastFeed(FString::Printf(TEXT("挑战倍数：%s 装甲 ×%.2f、伤害 ×%.2f"), GS->ChallengeTeam == 0 ? TEXT("蓝队") : TEXT("红队"), GS->ChallengeArmor, GS->ChallengeDamage), GS->ChallengeTeam);
	}
	for (AMechCharacter* Old : Mechs)
	{
		if (!Old)
		{
			continue;
		}
		if (AController* C = Old->GetController())
		{
			if (!C->IsA<APlayerController>())
			{
				C->Destroy();
			}
		}
		Old->Destroy();
	}
	Mechs.Reset();
	ClearPluginPods();
	NextPodTime = -1.f;
	DMDowned.Reset();
	DMRecentSpawns.Reset();
	DMGuardBlocks = DMGuardBreaks = 0;
	GS->Respawns.Reset();
	const bool bSolo = IsSoloDM();
	TArray<FTransform> SoloStarts;
	if (IsDM())
	{
		BuildDMSpawns();
		if (bSolo)
		{
			SoloStarts = PickSpreadStarts(Roster.Num());  // 个人死斗：开局每台一个尽量互相远的点
		}
	}

	++GS->Round;
	GS->Phase = EMechRoundPhase::Countdown;
	GS->PhaseEndTime = GetWorld()->GetTimeSeconds() + CountdownTime;
	GS->RoundWinner = -1;

	// 两队交替生成、每回合随机哪队先：同一帧里先更新的（AI 先算、刀先出）总是同一队的话，AI 对打会一直偏向那队
	const int32 FirstTeam = FMath::RandBool() ? 0 : 1;
	TArray<TPair<int32, int32>> SpawnOrder;
	for (int32 k = 0; k < TeamSize; ++k)
	{
		for (const int32 T : {FirstTeam, 1 - FirstTeam})
		{
			if (RosterAt(T, k))
			{
				SpawnOrder.Emplace(T, k);
			}
		}
	}
	for (int32 Slot = 0; Slot < SpawnOrder.Num(); ++Slot)
	{
		const int32 Team = SpawnOrder[Slot].Key;
		const int32 i = SpawnOrder[Slot].Value;
		const FMechRosterSlot* Seat = RosterAt(Team, i);
		APlayerController* Human = Seat->PlayerId >= 0 ? FindPlayer(Seat->PlayerId) : nullptr;
		const bool bPlayer = Human != nullptr;
		const FTransform At = bSolo && SoloStarts.IsValidIndex(Slot) ? SoloStarts[Slot] : TeamSpawn(Team, i);
		const float Yaw = At.Rotator().Yaw;
		AMechCharacter* Mech = GetWorld()->SpawnActorDeferred<AMechCharacter>(AMechCharacter::StaticClass(), At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Mech)
		{
			continue;
		}
		// 个人死斗：队伍号各不相同（Team 只是选配时的分组），「不同队 = 敌人」的判断各处照旧，人人为敌
		Mech->SetTeam(bSolo ? SoloTeam(Team, i) : Team);
		Mech->DisplayName = bPlayer ? HumanName(Human) : SlotName(Team, i);
		Mech->AILevelOverride = bPlayer ? -1 : Seat->AILevel;  // 11-02 每台人机单独的强度（FinishSpawning 时 AI 控制器接管，OnPossess 读）
		if (bPlayer)
		{
			Mech->AutoPossessAI = EAutoPossessAI::Disabled;
		}
		Mech->FinishSpawning(At);
		if (const FMechDraftSlot* Picked = GS->Draft.bDone ? GS->Draft.FindSlot(Team, i) : nullptr)
		{
			Mech->SetLoadout(Picked->Loadout, true);
		}
		else if (const int32 Forced = MechDraft::ForcedFrame(Team * TeamSize + i); Forced >= 0)
		{
			// 测试 -MechForceFrame=258（10-31 阵营机体）：没走选配的测试（-MechPerf、按键测试）每台按位轮流用这几种框架
			FMechLoadout L = Mech->GetLoadout();
			L.Frame = static_cast<EMechFrame>(Forced);
			Mech->SetLoadout(L, true);
		}
		if (const AMechPlayerController* HumanPC = bPlayer ? Cast<AMechPlayerController>(Human) : nullptr)
		{
			Mech->SetPaint(HumanPC->ProfilePaint);
		}
		if (bPlayer)
		{
			Human->Possess(Mech);
			Human->ClientSetRotation(FRotator(-8.f, Yaw, 0.f));
		}
		if (AMechAIController* AI = Cast<AMechAIController>(Mech->GetController()))
		{
			AI->ArenaCenter = ArenaCenter;
			AI->ArenaExtent = ArenaExtent;
			AI->SetControlRotation(FRotator(0.f, Yaw, 0.f));
		}
		Mech->GetHealth()->OnDied.AddWeakLambda(this, [this, Mech](AActor* Killer) { OnMechDied(Mech, Killer); });
		Mech->GetCharacterMovement()->DisableMovement();
		Mechs.Add(Mech);
		Stats.FindOrAdd(Mech->DisplayName).Team = Mech->GetTeam();
		// 出生高度检查：脚下比本队出生区地面高 / 低多少（超过 3 米说明落到了高处）
		const float Dz = (Mech->GetActorLocation().Z - Mech->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) - SpawnBaseGround(Team).Z;
		UE_LOG(LogTemp, Display, TEXT("[spawn] %s（%s第 %d 位）出生 %s，脚下比出生区地面 %+.2f 米%s"), *Mech->DisplayName, Team == 0 ? TEXT("蓝队") : TEXT("红队"), i + 1,
			*Mech->GetActorLocation().ToCompactString(), Dz / 100.f, !bSolo && FMath::Abs(Dz) > 300.f ? TEXT("（高差超过 3 米！）") : TEXT(""));
	}
	if (GS->Round == 1)
	{
		// 开场解说 + 运镜（MechMatchIntro）：整场第一回合机体生成后插在倒计时前面，倒计时往后推运镜时长（不播 = 0）
		GS->PhaseEndTime += AMechMatchIntro::BeginForMatch(this, bUseLobby || bFromRoom);  // 11-01 从房间开的也是正常流程
	}
	if (bAutoBattle && PC && Mechs.Num())
	{
		// 测试：-MechWatchTeam=1 镜头跟红队（查胜负偏向是不是镜头跟着哪队造成的）
		int32 Watch = 0;
		FParse::Value(FCommandLine::Get(), TEXT("MechWatchTeam="), Watch);
		AMechCharacter* View = Mechs[0];
		for (AMechCharacter* M : Mechs)
		{
			if (M && M->GetTeam() == Watch)
			{
				View = M;
				break;
			}
		}
		PC->SetViewTarget(View);
	}
	if (bCoreMode && CoreDirector)
	{
		CoreDirector->ResetMatch();
		GS->bOvertime = false;
	}
	UE_LOG(LogTemp, Display, TEXT("[battle] 第 %d 回合 %dv%d 开始倒计时，比分 蓝 %d : %d 红（%s先生成）%s"), GS->Round, TeamCount[0], TeamCount[1], GS->TeamScore[0], GS->TeamScore[1],
		FirstTeam == 0 ? TEXT("蓝队") : TEXT("红队"), *MechRules::Describe(Rules, bCoreMode));
	if (IsDM())
	{
		UE_LOG(LogTemp, Display, TEXT("[dm] 开局：%s，%d 台，重生点候选 %d 个（%s）"), *MechRules::Describe(Rules, false), Mechs.Num(), DMSpawns.Num(),
			bDMSpawnsFromMap ? TEXT("地图里的 DMSpawn") : TEXT("出生区 + 自动找的空地"));
	}
}

void AMechGameMode::StartFight()
{
	AMechGameState* GS = GetMechGameState();
	GS->Phase = EMechRoundPhase::Fight;
	FightStartTime = GetWorld()->GetTimeSeconds();
	ClearPluginPods();
	NextPodTime = FightStartTime + (bCoreMode ? 30.f : 35.f);
	GS->PhaseEndTime = FightStartTime + RoundTimeLimit;
	if (bCoreMode && CoreDirector)
	{
		CoreDirector->BeginFight();
	}
	for (AMechCharacter* Mech : Mechs)
	{
		if (Mech)
		{
			Mech->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
	}
	GS->MulticastFeed(FString::Printf(TEXT("第 %d 回合 开始"), GS->Round), -1);
}

void AMechGameMode::EndRound(int32 Winner)
{
	AMechGameState* GS = GetMechGameState();
	if (GS->Phase != EMechRoundPhase::Fight)
	{
		return;
	}
	GS->RoundWinner = Winner;
	if (Winner >= 0)
	{
		++GS->TeamScore[Winner];
	}
	GS->Phase = EMechRoundPhase::RoundOver;
	GS->PhaseEndTime = GetWorld()->GetTimeSeconds() + RoundOverTime;
	UE_LOG(LogTemp, Display, TEXT("[battle] 第 %d 回合结束：%s，用时 %.0f 秒，比分 蓝 %d : %d 红"), GS->Round,
		Winner < 0 ? TEXT("平局") : Winner == 0 ? TEXT("蓝队胜") : TEXT("红队胜"), GetWorld()->GetTimeSeconds() - FightStartTime,
		GS->TeamScore[0], GS->TeamScore[1]);
	if (bAutoBattle)
	{
		PendingEventShot = 0.6f;
	}
}

int32 AMechGameMode::AliveCount(int32 Team) const
{
	int32 N = 0;
	for (const AMechCharacter* Mech : Mechs)
	{
		N += (Mech && Mech->GetTeam() == Team && !Mech->GetHealth()->IsDead()) ? 1 : 0;
	}
	return N;
}

float AMechGameMode::TeamAPFraction(int32 Team) const
{
	float Sum = 0.f;
	for (const AMechCharacter* Mech : Mechs)
	{
		Sum += (Mech && Mech->GetTeam() == Team) ? Mech->GetHealth()->GetAPFraction() : 0.f;
	}
	return Sum;
}

void AMechGameMode::OnMechDied(AMechCharacter* Victim, AActor* Killer)
{
	AMechGameState* GS = GetMechGameState();
	AMechCharacter* KillerMech = Cast<AMechCharacter>(Killer);
	if (!KillerMech)
	{
		if (const AController* KillerController = Cast<AController>(Killer))
		{
			KillerMech = Cast<AMechCharacter>(KillerController->GetPawn());
		}
	}
	// 只有打死敌人才算击毁（环境 / 掉出地图 / 自己 / 队友都不算，也不扣分）
	const bool bCredit = KillerMech && KillerMech != Victim && KillerMech->GetTeam() != Victim->GetTeam();
	const FString KillerName = KillerMech ? KillerMech->DisplayName : TEXT("场地");
	GS->MulticastFeed(FString::Printf(TEXT("%s 击毁了 %s"), *KillerName, *Victim->DisplayName), KillerMech ? KillerMech->GetTeam() : -1);
	if (bCredit)
	{
		++Stats.FindOrAdd(KillerMech->DisplayName).Kills;
	}
	++Stats.FindOrAdd(Victim->DisplayName).Deaths;
	// 11-01 积分制（MechScore.h）：击毁分；夺芯里这台正带着核心 = 截下了这一分，另加（核心到 CoreDirector->OnMechDied 才掉，这里还查得到）；
	// 窗口内对它有过伤害 / 失衡 / 控制的其他人记助攻（掉出地图这类没有击毁者的也给，是他们把它逼死的）
	if (bCredit)
	{
		FMechStats& KillerStats = Stats.FindOrAdd(KillerMech->DisplayName);
		KillerStats.Points[static_cast<int32>(EMechScorePart::Kill)] += MechScore::Kill;
		if (bCoreMode && CoreDirector && CoreDirector->CoreCarriedBy(Victim))
		{
			++KillerStats.CarrierKills;
			KillerStats.Points[static_cast<int32>(EMechScorePart::Carrier)] += MechScore::CarrierKill;
		}
	}
	AwardAssists(Victim->DisplayName, Victim->GetTeam(), bCredit ? KillerMech->DisplayName : FString());
	UE_LOG(LogTemp, Display, TEXT("[battle] %s 击毁了 %s（开打后 %.1f 秒）"), *KillerName, *Victim->DisplayName,
		GetWorld()->GetTimeSeconds() - FightStartTime);
	if (bAutoBattle)
	{
		PendingEventShot = 0.35f;
		// 观战的那台被击毁了：过一会儿切到还活着的队友（没有就切到敌人）
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC && PC->GetViewTarget() == Victim)
		{
			const int32 VictimTeam = Victim->GetTeam();
			FTimerHandle H;
			GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, PC, VictimTeam]() {
				AMechCharacter* Next = nullptr;
				for (AMechCharacter* M : Mechs)
				{
					if (M && !M->GetHealth()->IsDead() && (!Next || M->GetTeam() == VictimTeam))
					{
						Next = M;
					}
				}
				if (Next)
				{
					PC->SetViewTargetWithBlend(Next, 0.6f);
				}
			}), 1.6f, false);
		}
	}
	if (bCoreMode)
	{
		// 夺芯不按全灭算：掉芯、等救或重生（规则在 CoreDirector）
		if (CoreDirector)
		{
			CoreDirector->OnMechDied(Victim, Killer);
		}
		return;
	}
	if (IsDM())
	{
		// 死斗：计分、排重生（不按全灭算）
		DMOnDied(Victim, bCredit ? KillerMech : nullptr);
		return;
	}
	if (AliveCount(Victim->GetTeam()) == 0)
	{
		EndRound(1 - Victim->GetTeam());
	}
}

// ---------------------------------------------------------------- 真人加入 / 掉线（AI 补位）
FString AMechGameMode::HumanName(const APlayerController* PC)
{
	const AMechPlayerController* MPC = Cast<AMechPlayerController>(PC);
	if (MPC && !MPC->ProfileName.IsEmpty())
	{
		return MPC->ProfileName;
	}
	if (GetNumPlayers() <= 1)
	{
		return TEXT("你");
	}
	return FString::Printf(TEXT("玩家%d"), MPC ? MPC->PlayerNumber : 0);
}

void AMechGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	AMechPlayerController* PC = Cast<AMechPlayerController>(NewPlayer);
	if (!PC)
	{
		return;
	}
	PC->PlayerNumber = ++JoinCounter;
	int32 Count[2] = {0, 0};
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AMechPlayerController* Other = Cast<AMechPlayerController>(It->Get());
		if (Other && Other != PC && Other->AssignedTeam >= 0 && Other->AssignedTeam <= 1)
		{
			++Count[Other->AssignedTeam];
		}
	}
	PC->AssignedTeam = Count[0] <= Count[1] ? 0 : 1;
	if (bRangeMode)
	{
		PC->AssignedTeam = 0;
		// 11-01：测试场 / 保卫战房主先进图，名字是单机的叫法「你」；有人加入后改回「玩家 N」，不然别人的信息区、头顶名字都写「你」
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Other = It->Get();
			AMechCharacter* OtherMech = Other && Other != PC ? Cast<AMechCharacter>(Other->GetPawn()) : nullptr;
			if (OtherMech && OtherMech->DisplayName == TEXT("你"))
			{
				OtherMech->DisplayName = HumanName(Other);
			}
		}
		SpawnRangeMech(PC);
		return;
	}
	const AMechGameState* GS = GetMechGameState();
	UE_LOG(LogTemp, Display, TEXT("[battle] %s 加入，分到%s队"), *HumanName(PC), PC->AssignedTeam == 0 ? TEXT("蓝") : TEXT("红"));
	if (GS && GS->Phase == EMechRoundPhase::Lobby)
	{
		RefreshLobby();
	}
	if (bRoundsStarted && !bAutoBattle && !bMenuMode && GS && (GS->Phase == EMechRoundPhase::Countdown || GS->Phase == EMechRoundPhase::Fight))
	{
		if (!TakeOverAI(PC))
		{
			UE_LOG(LogTemp, Display, TEXT("[battle] %s 队里没有能接管的 AI 机体，等下一回合"), *HumanName(PC));
		}
	}
}

// ---------------------------------------------------------------- 测试场
void AMechGameMode::SpawnRange()
{
	RangeStart = FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, 300.f));
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		RangeStart = It->GetActorTransform();
		break;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		SpawnRangeMech(It->Get());
	}
	if (bDefenseMode)
	{
		UE_LOG(LogTemp, Display, TEXT("[range] 保卫战：不摆木桩"));
		return;
	}
	TArray<FVector> Spots;
	for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("Dummy")))
		{
			Spots.Add(It->GetActorLocation());
		}
	}
	if (Spots.IsEmpty())  // 地图里没摆木桩点：在出生点前方 10 / 25 / 50 米摆一排
	{
		for (const float D : {1000.f, 2500.f, 5000.f})
		{
			Spots.Add(RangeStart.GetLocation() + RangeStart.GetRotation().GetForwardVector() * D);
		}
	}
	int32 N = 0;
	for (FVector At : Spots)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MechDummy), false);
		if (GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0.f, 0.f, 200.f), At - FVector(0.f, 0.f, 3000.f),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			At = Hit.ImpactPoint;
		}
		const float Yaw = (RangeStart.GetLocation() - At).GetSafeNormal2D().Rotation().Yaw;  // 面朝出生点
		const FTransform T(FRotator(0.f, Yaw, 0.f), At + FVector(0.f, 0.f, 96.f * 1.35f + 5.f));
		AMechCharacter* Dummy = GetWorld()->SpawnActorDeferred<AMechCharacter>(AMechCharacter::StaticClass(), T, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Dummy)
		{
			continue;
		}
		Dummy->AutoPossessAI = EAutoPossessAI::Disabled;
		Dummy->SetTeam(1);
		Dummy->DisplayName = FString::Printf(TEXT("木桩 %d"), ++N);
		Dummy->FinishSpawning(T);
		Dummy->MakeTrainingDummy();
		Mechs.Add(Dummy);
	}
	UE_LOG(LogTemp, Display, TEXT("[range] 测试场：木桩 %d 个"), N);
}

void AMechGameMode::SpawnRangeMech(APlayerController* PC)
{
	if (!PC)
	{
		return;
	}
	if (APawn* Old = PC->GetPawn())
	{
		PC->UnPossess();
		Mechs.Remove(Cast<AMechCharacter>(Old));
		Old->Destroy();
	}
	const int32 Index = (RangeSpawnCounter++) % 8;  // 11-02 一排 8 个位置轮流（重生的人不和别人叠在一起）；第一个还是中间
	const FVector Right = RangeStart.GetRotation().GetRightVector();
	const FTransform T(RangeStart.Rotator(), RangeStart.GetLocation() + Right * (Index % 2 ? 1.f : -1.f) * ((Index + 1) / 2) * 500.f +
		FVector(0.f, 0.f, 96.f * 1.35f - 80.f));
	AMechCharacter* Mech = GetWorld()->SpawnActorDeferred<AMechCharacter>(AMechCharacter::StaticClass(), T, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Mech)
	{
		return;
	}
	Mech->AutoPossessAI = EAutoPossessAI::Disabled;
	Mech->SetTeam(RangeTeamFor(PC));
	Mech->DisplayName = HumanName(PC);
	Mech->FinishSpawning(T);
	PC->Possess(Mech);
	PC->ClientSetRotation(FRotator(-6.f, RangeStart.Rotator().Yaw, 0.f));
	Mechs.Add(Mech);
	if (!bDefenseMode)  // 保卫战的重生归调度器管
	{
		Mech->GetHealth()->OnDied.AddWeakLambda(this, [this, Mech](AActor* Killer) { RangeOnDied(Mech, Killer); });
	}
}

// ---------------------------------------------------------------- 11-02 训练场：友伤、阵亡重生
int32 AMechGameMode::RangeTeamFor(APlayerController* PC)
{
	// 用户要「开启友伤」：不去改散在四十来处的敌我判断（`GetTeam() != Mech->GetTeam()`，好些在别人正在改的文件里），
	// 而是和个人死斗一样给每个玩家一个单独的队伍号：子弹、刀、肩部、地雷、锁定、AI、HUD 的判断都不用动，也碰不到其他模式；木桩还是 1 队
	const AMechGameState* GS = GetMechGameState();
	AMechPlayerController* MPC = Cast<AMechPlayerController>(PC);
	if (!GS || !GS->bFriendlyFire || !MPC)
	{
		return 0;
	}
	if (MPC->TrainingTeam < 0)
	{
		MPC->TrainingTeam = NextTrainingTeam++;
	}
	return MPC->TrainingTeam;
}

void AMechGameMode::SetTrainingFriendlyFire(bool bOn)
{
	AMechGameState* GS = GetMechGameState();
	if (!GS || !bRangeMode || bDefenseMode || GS->bFriendlyFire == bOn)
	{
		return;
	}
	GS->bFriendlyFire = bOn;
	GS->Room.bFriendlyFire = bOn;
	if (UMechRoomSubsystem* Sub = UMechRoomSubsystem::Get(this))
	{
		Sub->Settings.bFriendlyFire = bOn;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (AMechCharacter* Mech = PC ? Cast<AMechCharacter>(PC->GetPawn()) : nullptr)
		{
			Mech->SetTeam(RangeTeamFor(PC));
		}
	}
	GS->MulticastFeed(bOn ? TEXT("房主打开了友伤：玩家之间能互相打") : TEXT("房主关闭了友伤"), -1);
	UE_LOG(LogTemp, Display, TEXT("[range] 友伤 %s"), bOn ? TEXT("开") : TEXT("关"));
}

void AMechGameMode::RangeOnDied(AMechCharacter* Mech, AActor* Killer)
{
	AMechGameState* GS = GetMechGameState();
	if (!Mech || !GS)
	{
		return;
	}
	APlayerController* PC = Cast<APlayerController>(Mech->GetController());
	if (!PC)
	{
		return;
	}
	const AController* KillerController = Cast<AController>(Killer);
	const AMechCharacter* By = KillerController ? Cast<AMechCharacter>(KillerController->GetPawn()) : Cast<AMechCharacter>(Killer);
	if (By && By != Mech)
	{
		GS->MulticastFeed(FString::Printf(TEXT("%s 击毁了 %s"), *By->DisplayName, *Mech->DisplayName), By->GetTeam());
	}
	FRangeDown& D = RangeDowned.AddDefaulted_GetRef();
	D.PC = PC;
	D.Loadout = Mech->GetLoadout();
	D.Paint = Mech->GetPaint();
	D.RespawnAt = GetWorld()->GetTimeSeconds() + MechRules::RespawnDelay;
	FMechRespawn& R = GS->Respawns.AddDefaulted_GetRef();  // HUD 的重生读条
	R.Mech = Mech;
	R.DiedAt = GetWorld()->GetTimeSeconds();
	R.RespawnAt = D.RespawnAt;
	UE_LOG(LogTemp, Display, TEXT("[range] %s 阵亡，%.0f 秒后重生"), *Mech->DisplayName, MechRules::RespawnDelay);
}

void AMechGameMode::RangeRespawnTick()
{
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 i = RangeDowned.Num() - 1; i >= 0; --i)
	{
		if (Now < RangeDowned[i].RespawnAt)
		{
			continue;
		}
		const FRangeDown D = RangeDowned[i];
		RangeDowned.RemoveAt(i);
		APlayerController* PC = D.PC.Get();
		if (!PC)
		{
			continue;
		}
		if (AMechGameState* GS = GetMechGameState())
		{
			const APawn* OldPawn = PC->GetPawn();
			GS->Respawns.RemoveAll([OldPawn](const FMechRespawn& R) { return R.Mech == OldPawn; });
		}
		SpawnRangeMech(PC);
		if (AMechCharacter* Mech = Cast<AMechCharacter>(PC->GetPawn()))
		{
			Mech->SetLoadout(D.Loadout, true);
			Mech->SetPaint(D.Paint);
			UE_LOG(LogTemp, Display, TEXT("[range] %s 重生"), *Mech->DisplayName);
		}
	}
}

void AMechGameMode::ApplyRangeLoadout(APlayerController* PC, const FMechLoadout& Wanted)
{
	AMechCharacter* Mech = PC ? Cast<AMechCharacter>(PC->GetPawn()) : nullptr;
	if (!bRangeMode || !Mech)
	{
		return;
	}
	FMechLoadout L = Mech->GetLoadout();
	for (int32 c = 0; c < MechLoadout::NumCategories; ++c)
	{
		const int32 Want = MechLoadout::Get(Wanted, c);
		if (MechLoadout::Item(c, Want).bReady)
		{
			MechLoadout::Set(L, c, Want);
		}
	}
	Mech->SetLoadout(L, true);
	UE_LOG(LogTemp, Display, TEXT("[range] %s 换装：%s"), *Mech->DisplayName, *MechLoadout::Describe(L));
}

void AMechGameMode::RangeTick(float DeltaSeconds)
{
	RangeRespawnTick();
	WaterTick();
	// 掉下去（或飞出场地太远）就送回出生点
	for (AMechCharacter* Mech : Mechs)
	{
		if (Mech && !Mech->IsTrainingDummy() && Mech->GetActorLocation().Z < RangeStart.GetLocation().Z - 3000.f)
		{
			Mech->GetCharacterMovement()->StopMovementImmediately();
			Mech->TeleportTo(RangeStart.GetLocation(), RangeStart.Rotator());
		}
	}
	// 11-01 积分制：保卫战走测试场这条路，不经过对战的 Tick，统计也每秒抄一次给 Tab 计分板 / 结束画面的 MVP
	if (bDefenseMode)
	{
		PublishClock -= DeltaSeconds;
		if (PublishClock <= 0.f)
		{
			PublishClock = 1.f;
			PublishStats();
		}
	}
}

bool AMechGameMode::TakeOverAI(AMechPlayerController* PC)
{
	AMechGameState* GS = GetMechGameState();
	for (AMechCharacter* Mech : Mechs)
	{
		// 个人死斗没有队伍：接管哪台 AI 都行
		if (!Mech || (!IsSoloDM() && Mech->GetTeam() != PC->AssignedTeam) || Mech->GetHealth()->IsDead())
		{
			continue;
		}
		AMechAIController* AI = Cast<AMechAIController>(Mech->GetController());
		if (!AI)
		{
			continue;
		}
		const FString Old = Mech->DisplayName;
		AI->UnPossess();
		AI->Destroy();
		if (PC->GetPawn())
		{
			PC->UnPossess();
		}
		PC->Possess(Mech);
		PC->ClientSetRotation(FRotator(-8.f, Mech->GetActorRotation().Yaw, 0.f));
		Mech->DisplayName = HumanName(PC);
		Stats.FindOrAdd(Mech->DisplayName).Team = Mech->GetTeam();
		if (GS)
		{
			GS->MulticastFeed(FString::Printf(TEXT("%s 加入，接管 %s"), *Mech->DisplayName, *Old), Mech->GetTeam());
		}
		UE_LOG(LogTemp, Display, TEXT("[battle] %s 接管 AI 机体 %s"), *Mech->DisplayName, *Old);
		return true;
	}
	return false;
}

void AMechGameMode::HandToAI(AMechCharacter* Mech)
{
	Mech->SpawnDefaultController();
	if (AMechAIController* AI = Cast<AMechAIController>(Mech->GetController()))
	{
		AI->ArenaCenter = ArenaCenter;
		AI->ArenaExtent = ArenaExtent;
	}
}

void AMechGameMode::Logout(AController* Exiting)
{
	HandOverLeaving(Exiting);  // 一般在 PawnLeavingGame 里已经交过了，这里兜底
	Super::Logout(Exiting);
	if (const AMechGameState* LogoutGS = GetMechGameState(); LogoutGS && LogoutGS->Phase == EMechRoundPhase::Lobby)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &AMechGameMode::RefreshLobby);
	}
}

bool AMechGameMode::HandOverLeaving(AController* Exiting)
{
	// 掉线的人机体还活着：交给 AI 接着打（不然控制器销毁时机体也跟着删掉，这一队白白少一台）
	AMechCharacter* Mech = Exiting ? Cast<AMechCharacter>(Exiting->GetPawn()) : nullptr;
	if (Mech && !bMenuMode && Mechs.Contains(Mech) && !Mech->GetHealth()->IsDead())
	{
		const FString Old = Mech->DisplayName;
		Exiting->UnPossess();
		HandToAI(Mech);
		Mech->DisplayName = Old + TEXT("（AI）");
		Stats.FindOrAdd(Mech->DisplayName).Team = Mech->GetTeam();
		if (AMechGameState* GS = GetMechGameState())
		{
			GS->MulticastFeed(FString::Printf(TEXT("%s 掉线，AI 接管"), *Old), Mech->GetTeam());
		}
		UE_LOG(LogTemp, Display, TEXT("[battle] %s 掉线，机体交给 AI"), *Old);
		return true;
	}
	return false;
}

void AMechPlayerController::PawnLeavingGame()
{
	AMechGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AMechGameMode>() : nullptr;
	if (GM && GM->HandOverLeaving(this))
	{
		return;  // 机体已经交给 AI，不删
	}
	Super::PawnLeavingGame();
}

AMechCharacter* AMechGameMode::LastHitterOf(const AMechCharacter* Victim) const
{
	const TMap<FString, FMechAssistMark>* Marks = Victim ? AssistMarks.Find(Victim->DisplayName) : nullptr;
	if (!Marks)
	{
		return nullptr;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const FString* Best = nullptr;
	float BestAt = Now - MechScore::AssistWindow;
	for (const auto& Pair : *Marks)
	{
		const float At = FMath::Max(Pair.Value.DamageTime, Pair.Value.ControlTime);
		if (At >= BestAt)
		{
			BestAt = At;
			Best = &Pair.Key;
		}
	}
	if (!Best)
	{
		return nullptr;
	}
	for (AMechCharacter* M : Mechs)
	{
		if (M && M != Victim && M->DisplayName == *Best)
		{
			return M;
		}
	}
	return nullptr;
}

void AMechGameMode::WaterTick()
{
	UMechWater* Water = GetWorld()->GetSubsystem<UMechWater>();
	if (!Water || !Water->HasWater())
	{
		return;
	}
	const TArray<TObjectPtr<AMechCharacter>> Snapshot = Mechs;
	for (AMechCharacter* Mech : Snapshot)
	{
		if (!Mech || Mech->GetHealth()->IsDead() || Mech->IsTrainingDummy() || !Water->IsDrowned(Mech))
		{
			continue;
		}
		AMechCharacter* By = LastHitterOf(Mech);
		UE_LOG(LogTemp, Warning, TEXT("[water] %s%s 落水损毁：位置 %s 速度 %s 能量 %.0f%s 飞行 %d 失衡 %d 网 %d EMP %d 出刀 %d 冲刺键 %d，击毁算给 %s"), *Mech->DisplayName, Mech->IsPlayerControlled() ? TEXT("（玩家）") : TEXT("（AI）"),
			*Mech->GetActorLocation().ToCompactString(), *Mech->GetVelocity().ToCompactString(), Mech->GetEnergy(), Mech->IsOverheated() ? TEXT("（过热）") : TEXT(""), Mech->GetMechMovement()->bGliding ? 1 : 0,
			Mech->GetHealth()->IsStaggered() ? 1 : 0, Mech->IsNetted() ? 1 : 0, Mech->IsEmped() ? 1 : 0, Mech->GetCombat()->IsMeleeActive() ? 1 : 0, Mech->GetMechMovement()->bWantsBoost ? 1 : 0, By ? *By->DisplayName : TEXT("场地"));
		if (Mech->GetEffects())
		{
			Mech->GetEffects()->RemoveEffect(MechRules::SpawnGuardId());  // 重生保护挡不住水
		}
		Mech->GetHealth()->ApplyHit(1.e7f, 0.f, By, Mech->GetActorLocation(), 0.f);
	}
	static const bool bWaterTest = FParse::Param(FCommandLine::Get(), TEXT("MechWaterTest"));
	if (bWaterTest)
	{
		WaterTestTick(GetWorld()->GetDeltaSeconds());
	}
	static const bool bFlightTest = FParse::Param(FCommandLine::Get(), TEXT("MechFlightTest"));
	if (bFlightTest)
	{
		FlightTestTick(GetWorld()->GetDeltaSeconds());
	}
}

void AMechGameMode::RecordHit(AMechCharacter* Attacker, AMechCharacter* Victim, float Damage, EMechHitKind Kind, bool bBreak)
{
	// 11-01 积分制（MechScore.h）：只算打敌人的（自己炸到自己、木桩不算）——伤害分、对首领的伤害、打出失衡，都记成助攻的贡献。
	// 先记受害者再拿攻击者的引用（FindOrAdd 加新行会让前面拿到的引用失效）
	if (Attacker && Victim && Attacker != Victim && Attacker->GetTeam() != Victim->GetTeam() && !Victim->IsTrainingDummy())
	{
		Stats.FindOrAdd(Victim->DisplayName).Team = Victim->GetTeam();
		FMechStats& S = Stats.FindOrAdd(Attacker->DisplayName);
		S.Team = Attacker->GetTeam();
		S.ScoreDamage += Damage;
		if (bDefenseMode && Victim->ActorHasTag(TEXT("MechBoss")))
		{
			S.BossDamage += Damage;
		}
		if (bBreak)
		{
			++S.Breaks;
			S.Points[static_cast<int32>(EMechScorePart::Break)] += MechScore::Break;
		}
		NoteContribution(Attacker, Victim, Damage, bBreak);
	}
	if (Attacker)
	{
		FMechStats& S = Stats.FindOrAdd(Attacker->DisplayName);
		S.Dealt += Damage;
		++S.Hits[static_cast<int32>(Kind)];
		// 死斗重生保护：自己打中别人就提前结束（不让带着无敌去抢人头）
		if (Damage > 0.f && Attacker != Victim && Attacker->GetEffects() && Attacker->GetEffects()->HasEffect(MechRules::SpawnGuardId()))
		{
			Attacker->GetEffects()->RemoveEffect(MechRules::SpawnGuardId());
			++DMGuardBreaks;
		}
		if (Victim && Victim->GetEffects() && Victim->GetEffects()->HasEffect(MechRules::SpawnGuardId()))
		{
			++DMGuardBlocks;  // 打在重生保护上（不掉装甲）
		}
	}
	if (Victim)
	{
		Stats.FindOrAdd(Victim->DisplayName).Taken += Damage;
	}
}

void AMechGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bVfxGallery)
	{
		VfxGalleryTick(DeltaSeconds);
		return;
	}
	if (bMenuMode || bRoomMode)
	{
		if (bMenuMode)
		{
			ShowcaseTick(DeltaSeconds);
		}
		return;  // 房间图：没有回合，房间的事都在 RPC 里（LobbySetRoom / LobbySetTeam / LobbyStart）
	}
	if (bRangeMode)
	{
		RangeTick(DeltaSeconds);
		RoomDefenseTick();
		return;
	}
	AMechGameState* GS = GetMechGameState();
	if (GS && !bRoundsStarted && WaitPlayers > 1 && GetNumPlayers() >= WaitPlayers)
	{
		bRoundsStarted = true;
		UE_LOG(LogTemp, Display, TEXT("[battle] %d 名玩家到齐，开始"), GetNumPlayers());
		GetWorldTimerManager().SetTimer(StartTimer, FTimerDelegate::CreateUObject(this, &AMechGameMode::StartMatchFlow), 2.f, false);
	}
	RoomWaitTick();
	if (!GS || GS->Phase == EMechRoundPhase::Waiting)
	{
		return;
	}
	MatchClock += DeltaSeconds;
	PublishClock -= DeltaSeconds;
	if (PublishClock <= 0.f)
	{
		PublishClock = 1.f;
		PublishStats();
	}
	const float Remaining = GS->GetPhaseRemaining();
	switch (GS->Phase)
	{
	case EMechRoundPhase::Countdown:
		if (Remaining <= 0.f)
		{
			StartFight();
		}
		break;
	case EMechRoundPhase::Fight:
		PluginTick();
		WaterTick();
		if (GS->Phase != EMechRoundPhase::Fight)
		{
			break;  // 最后一台落水，这一局刚结束
		}
		if (IsDM())
		{
			DMTick(DeltaSeconds);
			break;
		}
		// 掉出地图（穿过地面的缝、从场地边缘掉下去）：送回本队出生点，不算击毁；位置记下来，回头补地图
		for (AMechCharacter* Mech : Mechs)
		{
			if (Mech && !Mech->GetHealth()->IsDead() && Mech->GetActorLocation().Z < ArenaCenter.Z - 3000.f)
			{
				UE_LOG(LogTemp, Warning, TEXT("[battle] %s 掉出地图：位置 %s 速度 %s，送回出生点"), *Mech->DisplayName, *Mech->GetActorLocation().ToString(),
					*Mech->GetVelocity().ToString());
				Mech->GetCharacterMovement()->StopMovementImmediately();
				Mech->TeleportTo(TeamSpawn(Mech->GetTeam(), 0).GetLocation(), Mech->GetActorRotation());
			}
		}
		if (bCoreMode)
		{
			// 夺芯到时：分高的赢；同分加时 3 分钟，下一分定胜负，还没人得分算平局
			if (Remaining <= 0.f)
			{
				if (GS->bOvertime)
				{
					EndCoreMatch(-1);
				}
				else if (GS->TeamScore[0] != GS->TeamScore[1])
				{
					EndCoreMatch(GS->TeamScore[0] > GS->TeamScore[1] ? 0 : 1);
				}
				else
				{
					GS->bOvertime = true;
					GS->PhaseEndTime = GetWorld()->GetTimeSeconds() + 180.f;
					GS->MulticastFeed(TEXT("时间到，同分加时：下一分定胜负"), -1);
					UE_LOG(LogTemp, Display, TEXT("[core] 时间到 %d : %d 同分，加时"), GS->TeamScore[0], GS->TeamScore[1]);
				}
			}
			break;
		}
		if (Remaining <= 0.f)
		{
			const float A = TeamAPFraction(0);
			const float B = TeamAPFraction(1);
			EndRound(FMath::IsNearlyEqual(A, B, 0.01f) ? -1 : (A > B ? 0 : 1));
		}
		break;
	case EMechRoundPhase::RoundOver:
		if (Remaining <= 0.f)
		{
			const int32 Winner = GS->TeamScore[0] >= RoundsToWin ? 0 : GS->TeamScore[1] >= RoundsToWin ? 1 : -1;
			if (Winner >= 0)
			{
				GS->MatchWinner = Winner;
				GS->Phase = EMechRoundPhase::MatchOver;
				GS->PhaseEndTime = GetWorld()->GetTimeSeconds() + MatchOverTime;
				UE_LOG(LogTemp, Display, TEXT("[battle] 比赛结束：%s，比分 蓝 %d : %d 红"), Winner == 0 ? TEXT("蓝队胜") : TEXT("红队胜"),
					GS->TeamScore[0], GS->TeamScore[1]);
				LogStats();
				PublishStats();
				if (bAutoBattle)
				{
					PendingEventShot = 1.f;
				}
			}
			else
			{
				BeginRound();
			}
		}
		break;
	case EMechRoundPhase::Draft:
		DraftTick();
		break;
	case EMechRoundPhase::MatchOver:
		if (Remaining <= 0.f)
		{
			if (bAutoBattle)
			{
				GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("quit"));
				return;
			}
			if (bFromRoom)
			{
				ReturnToRoom();  // 11-01 从房间开的：结算表到时自动回房间（以前是自动再来一局）
			}
			else
			{
				RestartMatchNow();
			}
		}
		break;
	default:
		break;
	}
	if (bAutoBattle)
	{
		AutoBattleTick(DeltaSeconds);
	}
	if (bPlayerTest)
	{
		PlayerTestTick(DeltaSeconds);
	}
}

void AMechGameMode::RestartMatchNow()
{
	AMechGameState* GS = GetMechGameState();
	if (!GS || GS->Phase != EMechRoundPhase::MatchOver)
	{
		return;
	}
	GS->TeamScore[0] = GS->TeamScore[1] = 0;
	GS->Round = 0;
	GS->MatchWinner = -1;
	bRosterBuilt = false;  // 11-02 再来一局重新排阵容
	GS->WinnerName.Reset();
	GS->Respawns.Reset();
	DMDowned.Reset();
	Stats.Reset();
	PublishStats();
	StartMatchFlow();
}

void AMechGameMode::PublishStats()
{
	AMechGameState* GS = GetMechGameState();
	if (!GS)
	{
		return;
	}
	// 结算场景（MechResultsStage）要每台的配装和涂装：场上还在的机体随时记下来（结束时有的已经销毁 / 在重生，用最后记下的）
	for (const AMechCharacter* Mech : Mechs)
	{
		if (FMechStats* St = Mech ? Stats.Find(Mech->DisplayName) : nullptr)
		{
			St->Loadout = Mech->GetLoadout();
			St->Paint = Mech->GetPaint();
			St->bHuman |= Mech->IsPlayerControlled();
		}
	}
	GS->Results.Reset();
	for (const auto& Pair : Stats)
	{
		if (bDefenseMode && Pair.Value.Team != 0)
		{
			continue;  // 11-01：保卫战只列玩家（敌方精英 / 首领机甲挨打、打人也会进统计）
		}
		FMechResultRow& Row = GS->Results.AddDefaulted_GetRef();
		FillScore(Row, Pair.Value);  // 11-01 积分制：Score、Parts、助攻等计数（MechScore.cpp）
		Row.Name = Pair.Key;
		Row.Team = Pair.Value.Team;
		Row.Dealt = FMath::RoundToInt(Pair.Value.Dealt);
		Row.Taken = FMath::RoundToInt(Pair.Value.Taken);
		Row.Kills = Pair.Value.Kills;
		Row.Deaths = Pair.Value.Deaths;
		Row.Bullets = Pair.Value.Hits[0];
		Row.Missiles = Pair.Value.Hits[1];
		Row.Blades = Pair.Value.Hits[2];
		Row.Cores = Pair.Value.Cores;
		Row.Loadout = Pair.Value.Loadout;
		Row.Paint = Pair.Value.Paint;
		Row.bHuman = Pair.Value.bHuman;
	}
	// 进入「比赛结束」的服务器时间（三处结束流程设完阶段都马上调这里）：各端结算场景按它同步开始
	if (GS->Phase == EMechRoundPhase::MatchOver)
	{
		if (GS->MatchEndTime < 0.f)
		{
			GS->MatchEndTime = GetWorld()->GetTimeSeconds();
		}
	}
	else
	{
		GS->MatchEndTime = -1.f;
	}
}

// ---------------------------------------------------------------- 主菜单背景
void AMechGameMode::SpawnShowcase()
{
	FVector At = FVector::ZeroVector;
	float Yaw = 0.f;
	if (GetWorld()->GetMapName().EndsWith(TEXT("L_Street")))
	{
		At = FVector(900.f, 6200.f, 0.f);  // 路中间的空地（出生点挨着墙）
		Yaw = 90.f;  // 面朝 +Y，镜头在它前面看向 -Y：背景是红方那头的霓虹街
	}
	else
	{
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			At = It->GetActorLocation();
			Yaw = It->GetActorRotation().Yaw;
			break;
		}
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechShowcase), false);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0.f, 0.f, 2000.f), At - FVector(0.f, 0.f, 6000.f),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		At = Hit.ImpactPoint;
	}
	const FTransform Spawn(FRotator(0.f, Yaw, 0.f), At + FVector(0.f, 0.f, 96.f * 1.35f + 5.f));
	Showcase = GetWorld()->SpawnActorDeferred<AMechCharacter>(AMechCharacter::StaticClass(), Spawn, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (Showcase)
	{
		Showcase->AutoPossessAI = EAutoPossessAI::Disabled;
		Showcase->SetTeam(0);
		Showcase->DisplayName = TEXT("HX-07");
		Showcase->FinishSpawning(Spawn);
		Showcase->SetLoadout(FMechSettings::Get().Loadout, true);  // 10-31：显示自己上次配的那台框架（九种框架外形各不相同）
		Showcase->SetPaint(FMechSettings::Get().Paint);  // 自己的涂装
		Showcase->SetShowcasePose(true);
	}
	ShowcaseCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Spawn);
	if (ShowcaseCamera)
	{
		ShowcaseCamera->GetCameraComponent()->SetFieldOfView(45.f);
		ShowcaseCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
		auto MakeLight = [this](const TCHAR* Name, const FLinearColor& Color, float Intensity) {
			UPointLightComponent* L = NewObject<UPointLightComponent>(ShowcaseCamera, Name);
			L->SetMobility(EComponentMobility::Movable);
			L->SetCastShadows(false);
			L->SetAttenuationRadius(900.f);
			L->SetSourceRadius(60.f);
			L->SetLightColor(Color);
			L->SetIntensity(Intensity);
			L->RegisterComponent();
			return L;
		};
		ShowcaseKey = MakeLight(TEXT("ShowcaseKey"), FLinearColor(1.f, 0.82f, 0.66f), 9000.f);
		ShowcaseRim = MakeLight(TEXT("ShowcaseRim"), FLinearColor(0.35f, 0.75f, 1.f), 16000.f);
	}
	ShowcaseTick(0.f);
}

void AMechGameMode::ShowcaseTick(float DeltaSeconds)
{
	ShowcaseClock += DeltaSeconds;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!Showcase || !ShowcaseCamera || !PC)
	{
		return;
	}
	// 镜头在机体前方偏右 9 米、略低（仰一点），左右慢慢摆，背景是沿街远处；看向机体左边一点，机体就落在画面右半边（左边是菜单面板）
	const FVector Chest = Showcase->GetActorLocation() + FVector(0.f, 0.f, 20.f);
	const float Around = Showcase->GetActorRotation().Yaw + 14.f + 12.f * FMath::Sin(ShowcaseClock * 0.12f);
	const FVector Eye = Chest + FRotator(0.f, Around, 0.f).Vector() * 900.f - FVector(0.f, 0.f, 60.f);
	const FVector Right = FRotationMatrix((Chest - Eye).Rotation()).GetUnitAxis(EAxis::Y);
	const FVector Aim = Chest - Right * 125.f;
	ShowcaseCamera->SetActorLocationAndRotation(Eye, (Aim - Eye).Rotation());
	if (ShowcaseKey && ShowcaseRim)
	{
		const float Yaw = Showcase->GetActorRotation().Yaw;
		ShowcaseKey->SetWorldLocation(Chest + FRotator(0.f, Yaw + 50.f, 0.f).Vector() * 380.f + FVector(0.f, 0.f, 220.f));
		ShowcaseRim->SetWorldLocation(Chest + FRotator(0.f, Yaw + 160.f, 0.f).Vector() * 300.f + FVector(0.f, 0.f, 160.f));
	}
	if (PC->GetViewTarget() != ShowcaseCamera)
	{
		PC->SetViewTarget(ShowcaseCamera);
	}
	// 设置里的配装换了框架：展示机体跟着换
	if (Showcase->GetLoadout().Frame != FMechSettings::Get().Loadout.Frame)
	{
		Showcase->SetLoadout(FMechSettings::Get().Loadout, true);
		Showcase->SetShowcasePose(true);
	}
	// 设置里改了涂装：展示机体跟着换
	const FMechPaint& Want = FMechSettings::Get().Paint;
	const FMechPaint& Has = Showcase->GetPaint();
	if (Want.Primary != Has.Primary || Want.Secondary != Has.Secondary || Want.Accent != Has.Accent)
	{
		Showcase->SetPaint(Want);
	}
}

void AMechGameMode::VfxGalleryTick(float DeltaSeconds)
{
	static const TCHAR* Systems[] = {
		TEXT("/Game/AAAVFX/Course_04/Particles/Ex_01_Hit_01"), TEXT("/Game/AAAVFX/Course_04/Particles/Ex_01_Hit_02"),
		TEXT("/Game/AAAVFX/Course_04/Particles/Ex_01_Hit_03"), TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"),
		TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_02"), TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_03"),
		TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Muzzle"), TEXT("/Game/AAAVFX/Course_04/Particles/Ex_02_Exp_01"),
		TEXT("/Game/AAAVFX/Course_04/Particles/Ex_02_Exp_02"), TEXT("/Game/AAAVFX/Course_04/Particles/Ex_04_GrenadeExp_01"),
		TEXT("/Game/AAAVFX/Course_04/Particles/Ex_05_Exp_01"), TEXT("/Game/AAAVFX/Course_04/Particles/Ex_06_Exp_01"),
		TEXT("/Game/AAAVFX/Course_04/Particles/Ex_06_Exp_02"), TEXT("/Game/AAAVFX/Course_01/Particles/Ex_07_Fire_Explosion/Exp_01"),
		TEXT("/Game/AAAVFX/Course_01/Particles/Ex_02_Earth_Explosion/Exp_01"), TEXT("/Game/AAAVFX/Course_00/Particle/Ex_03_Shockwave/ShockWave_01"),
		TEXT("/Game/AAAVFX/Course_00/Particle/Ex_02_Projectile/Projectile_Exp_01"), TEXT("/Game/AAAVFX/Course_03/Particles/Ex_01_SpriteExp"),
		TEXT("/Game/AAAVFX/Course_04/Particles/Ex_06_AreaExp_01"), TEXT("/Game/AAAVFX/Course_00/Particle/Ex_01_SpriteExp/SpriteExp_01"),
	};
	// -MechVfxGallery=Shoulder：肩部武器（烟雾弹、EMP、抓钩、捕获网、火炮、导弹拖尾）能用上的候选，持续类特效要多播一会儿
	static const TCHAR* Shoulder[] = {
		TEXT("/Game/AAAVFX/Course_05/Particles/Ex_02_SmokePlume"), TEXT("/Game/AAAVFX/Course_8/Particles/Ex_06_Tornado"),
		TEXT("/Game/AAAVFX/Course_01/Particles/Ex_04_Thunder_Explosion/Exp_01"), TEXT("/Game/AAAVFX/Course_01/Particles/Ex_04_Thunder_Explosion/Thunder_01"),
		TEXT("/Game/AAAVFX/Course_01/Particles/Ex_04_Thunder_Explosion/Thunder_02"), TEXT("/Game/AAAVFX/Course_01/Particles/Ex_04_Thunder_Explosion/Thunder_03"),
		TEXT("/Game/AAAVFX/Course_8/Particles/Ex_07_Shockwave_01"), TEXT("/Game/AAAVFX/Course_8/Particles/Ex_07_Shockwave_02"),
		TEXT("/Game/AAAVFX/Course_8/Particles/Ex_05_Beam"), TEXT("/Game/AAAVFX/Course_00/Particle/Ex_04_Ultimate/Ultimate_Chain_01"),
		TEXT("/Game/AAAVFX/Course_07/Particles/Ex_05_Chain_S_01"), TEXT("/Game/AAAVFX/Course_07/Particles/Ex_05_ChainExp_01"),
		TEXT("/Game/AAAVFX/Course_07/Particles/Ex_01_Shield"), TEXT("/Game/AAAVFX/Course_02/Particle/Ex_02_MagicShield"),
		TEXT("/Game/AAAVFX/Course_07/Particles/Ex_03_Stealth_S"), TEXT("/Game/AAAVFX/Course_03/Particles/Ex_04_GravityExp"),
		TEXT("/Game/AAAVFX/Course_04/Particles/Ex_04_GrenadeTrail"), TEXT("/Game/AAAVFX/Course_07/Particles/Ex_02_Trail"),
		TEXT("/Game/AAAVFX/Course_05/Particles/Ex_03_Explosion"), TEXT("/Game/AAAVFX/Course_06/Particles/Ex_01_Gunshot"),
	};
	static const bool bShoulder = FString(FCommandLine::Get()).Contains(TEXT("-MechVfxGallery=Shoulder"));
	const TCHAR* const* List = bShoulder ? Shoulder : Systems;
	const int32 Num = bShoulder ? UE_ARRAY_COUNT(Shoulder) : UE_ARRAY_COUNT(Systems);
	const float Per = bShoulder ? 4.f : 2.2f;
	const float T0 = GalleryClock;
	GalleryClock += DeltaSeconds;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || GalleryClock < 3.f)
	{
		return;
	}
	const int32 Index = FMath::FloorToInt((GalleryClock - 3.f) / Per);
	const float Local = GalleryClock - 3.f - Index * Per;
	const float PrevLocal = T0 - 3.f - Index * Per;
	if (Index >= Num)
	{
		PC->ConsoleCommand(TEXT("quit"));
		bVfxGallery = false;
		return;
	}
	if (Index != GalleryIndex)
	{
		GalleryIndex = Index;
		PC->SetControlRotation(FRotator(-6.f, Pawn->GetActorRotation().Yaw, 0.f));
		if (UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, List[Index]))
		{
			const FVector At = Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 900.f + FVector(0.f, 0.f, 60.f);
			// 持续类特效（烟柱、龙卷）会一直放：换下一个前把上一个删掉，不然后面全被它挡住
			if (GalleryFx)
			{
				GalleryFx->DestroyComponent();
			}
			GalleryFx = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Sys, At, FRotator::ZeroRotator, FVector(1.f));
		}
	}
	static const float Quick[] = {0.25f, 0.8f};
	static const float Slow[] = {0.3f, 1.2f, 3.f};
	const TArrayView<const float> Times = bShoulder ? TArrayView<const float>(Slow) : TArrayView<const float>(Quick);
	for (int32 k = 0; k < Times.Num(); ++k)
	{
		const float At = Times[k];
		if (PrevLocal < At && Local >= At)
		{
			// 同名的（雷电组里的 Exp_01 和别的包重名）带上上一级目录名
			const FString Name = FPaths::GetBaseFilename(FPaths::GetPath(List[Index])) + TEXT("_") + FPaths::GetBaseFilename(List[Index]);
			const FString Path = FPaths::ProjectSavedDir() / (bShoulder ? TEXT("Screenshots/VfxShoulder") : TEXT("Screenshots/Vfx"))
				/ FString::Printf(TEXT("%02d_%s_%c.png"), Index, *Name, TCHAR('a' + k));
			FScreenshotRequest::RequestScreenshot(Path, false, false);
		}
	}
}

void AMechGameMode::PlayerTestTick(float DeltaSeconds)
{
	AMechGameState* GS = GetMechGameState();
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	AMechCharacter* Me = PC ? Cast<AMechCharacter>(PC->GetPawn()) : nullptr;
	if (GS->Phase == EMechRoundPhase::RoundOver || GS->Phase == EMechRoundPhase::MatchOver)
	{
		PlayerTestClock += DeltaSeconds;
		if (PlayerTestClock > 3.f)
		{
			LogStats();
			PC->ConsoleCommand(TEXT("quit"));
			bPlayerTest = false;
		}
		return;
	}
	if (!Me || GS->Phase != EMechRoundPhase::Fight || Me->GetHealth()->IsDead())
	{
		return;
	}
	AMechCharacter* Enemy = nullptr;
	for (AMechCharacter* M : Mechs)
	{
		if (M && M->GetTeam() != Me->GetTeam() && !M->GetHealth()->IsDead() &&
			(!Enemy || FVector::Dist(M->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist(Enemy->GetActorLocation(), Me->GetActorLocation())))
		{
			Enemy = M;
		}
	}
	if (!Enemy)
	{
		return;
	}
	// 镜头（控制旋转）对着敌人，锁定由战斗组件按镜头中心自己判断
	const FVector To = Enemy->GetActorLocation() - Me->GetActorLocation();
	PC->SetControlRotation(FMath::RInterpTo(PC->GetControlRotation(), (To + FVector(0.f, 0.f, -150.f)).Rotation(), DeltaSeconds, 8.f));
	const float T = GetWorld()->GetTimeSeconds();
	Me->AddMovementInput(FVector::CrossProduct(FVector::UpVector, To.GetSafeNormal2D()) * (FMath::Fmod(T, 6.f) < 3.f ? 1.f : -1.f), 1.f);
	Me->AddMovementInput(To.GetSafeNormal2D(), To.Size2D() > 3000.f ? 0.8f : -0.3f);
	UMechCombatComponent* Combat = Me->GetCombat();
	Combat->SetTrigger(FMath::Fmod(T, 2.4f) < 1.6f);
	if (Combat->GetMissileLock() >= 1.f && Combat->GetMissileReady() >= 1.f)
	{
		Combat->FireMissiles();
	}
	if (To.Size2D() < 1300.f && Combat->GetBladeReady() >= 1.f)
	{
		Combat->Slash();
	}
	AutoShotTimer -= DeltaSeconds;
	if (AutoShotTimer <= 0.f)
	{
		AutoShotTimer = 2.5f;
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Player") / FString::Printf(TEXT("p_%02d.png"), AutoShotIndex++);
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		UE_LOG(LogTemp, Display, TEXT("[player] 截图 %d 目标 %s 锁定 %.2f AP %.0f 敌 AP %.0f"), AutoShotIndex - 1,
			Combat->GetTarget() ? *Combat->GetTarget()->DisplayName : TEXT("无"), Combat->GetMissileLock(), Me->GetHealth()->GetAP(),
			Enemy->GetHealth()->GetAP());
	}
}

void AMechGameMode::AutoBattleTick(float DeltaSeconds)
{
	AMechGameState* GS = GetMechGameState();
	auto Shot = [&](const TCHAR* Tag) {
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Battle") /
			FString::Printf(TEXT("r%d_%02d_%s.png"), GS->Round, AutoShotIndex++, Tag);
		FScreenshotRequest::RequestScreenshot(Path, true, false);
	};
	if (PendingEventShot >= 0.f)
	{
		PendingEventShot -= DeltaSeconds;
		if (PendingEventShot < 0.f)
		{
			Shot(TEXT("event"));
		}
	}
	if (GS->Phase == EMechRoundPhase::Fight)
	{
		AutoShotTimer -= DeltaSeconds;
		if (AutoShotTimer <= 0.f)
		{
			AutoShotTimer = 4.f;
			Shot(TEXT("fight"));
		}
	}
	// 保险：一场打太久就停（死斗按它的时间限制再多给 5 分钟）
	if (MatchClock > FMath::Max(900.f, (IsDM() ? RoundTimeLimit : 0.f) + 300.f))
	{
		UE_LOG(LogTemp, Warning, TEXT("[battle] 超过 15 分钟没打完，强制结束"));
		LogStats();
		MatchClock = -1e9f;
		GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("quit"));
	}
}

void AMechGameMode::LogStats() const
{
	for (const auto& Pair : Stats)
	{
		const FMechStats& S = Pair.Value;
		UE_LOG(LogTemp, Display, TEXT("[battle]   %s（%s）输出 %.0f 承受 %.0f 击毁 %d 阵亡 %d 命中：子弹 %d 导弹 %d 刀 %d"), *Pair.Key,
			S.Team == 0 ? TEXT("蓝队") : S.Team == 1 ? TEXT("红队") : TEXT("个人"), S.Dealt, S.Taken, S.Kills, S.Deaths, S.Hits[0], S.Hits[1], S.Hits[2]);
	}
	LogScores();  // 11-01 积分制：每人积分分项、MVP 和理由（MechScore.cpp）
}

// ---------------------------------------------------------------- 夺芯
void AMechGameMode::SetupCoreMode()
{
	FString Which = UGameplayStatics::ParseOption(OptionsString, TEXT("Core"));
	FParse::Value(FCommandLine::Get(), TEXT("MechCore="), Which);
	const bool bFactory = Which.Equals(TEXT("Factory"), ESearchCase::IgnoreCase);
	// 默认在测试场上空 300 米现搭灰盒（不改任何地图资产）；地图里摆了带「CoreArena」标签的点（做好美术的夺芯地图）就搭在那个点上，
	// 灰盒只留碰撞和机关，外观用地图自己的（MechCoreArena 按「CollisionOnly」标签处理）
	FTransform At(FRotator::ZeroRotator, FVector(0.f, 0.f, 30000.f));
	bool bOnMap = false;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("CoreArena")))
		{
			At = FTransform(FRotator(0.f, It->GetActorRotation().Yaw, 0.f), It->GetActorLocation());
			bOnMap = true;
			break;
		}
	}
	CoreArena = GetWorld()->SpawnActorDeferred<AMechCoreArena>(AMechCoreArena::StaticClass(), At);
	if (bOnMap)
	{
		CoreArena->Tags.Add(TEXT("CollisionOnly"));
	}
	CoreArena->Layout = bFactory ? 0 : 1;
	CoreArena->bSwapSides = FParse::Param(FCommandLine::Get(), TEXT("MechSwapSides"));
	CoreArena->FinishSpawning(At);
	CoreDirector = GetWorld()->SpawnActor<AMechCoreDirector>(AMechCoreDirector::StaticClass(), At);
	CoreDirector->Setup(this, CoreArena);
	RoundsToWin = 1;
	RoundTimeLimit = 600.f;
	FParse::Value(FCommandLine::Get(), TEXT("MechCoreTime="), RoundTimeLimit);
	int32 Target = 5;
	FParse::Value(FCommandLine::Get(), TEXT("MechCoreTarget="), Target);
	if (AMechGameState* GS = GetMechGameState())
	{
		GS->bCoreMode = true;
		GS->CoreTarget = Target;
	}
	UE_LOG(LogTemp, Display, TEXT("[core] 夺芯模式：%s，先到 %d 分，限时 %.0f 秒"), bFactory ? TEXT("装配厂") : TEXT("峡谷"), Target, RoundTimeLimit);
}

AMechCharacter* AMechGameMode::RespawnMech(AMechCharacter* Old, AController* Controller)
{
	if (!Old)
	{
		return nullptr;
	}
	const int32 RespawnTeam = FMath::Clamp(Old->GetTeam(), 0, 1);
	return RespawnMechAt(Old, Controller, TeamSpawn(RespawnTeam, FMath::RandRange(0, FMath::Max(0, TeamCount[RespawnTeam] - 1))));
}

AMechCharacter* AMechGameMode::RespawnMechAt(AMechCharacter* Old, AController* Controller, const FTransform& At)
{
	AMechGameState* GS = GetMechGameState();
	if (!Old || !GS || GS->Phase != EMechRoundPhase::Fight)
	{
		return nullptr;
	}
	const int32 Team = Old->GetTeam();
	const float Yaw = At.Rotator().Yaw;
	AMechCharacter* Mech = GetWorld()->SpawnActorDeferred<AMechCharacter>(AMechCharacter::StaticClass(), At, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Mech)
	{
		return nullptr;
	}
	Mech->SetTeam(Team);
	Mech->DisplayName = Old->DisplayName;
	Mech->AILevelOverride = Old->AILevelOverride;  // 11-02 重生后还是这台人机的强度（Possess 时读）
	Mech->AutoPossessAI = EAutoPossessAI::Disabled;
	Mech->FinishSpawning(At);
	Mech->SetLoadout(Old->GetLoadout(), true);
	Mech->SetPaint(Old->GetPaint());
	AController* C = Controller ? Controller : Old->GetController();
	if (C)
	{
		if (C->GetPawn())
		{
			C->UnPossess();
		}
		C->Possess(Mech);
		if (APlayerController* PC = Cast<APlayerController>(C))
		{
			PC->ClientSetRotation(FRotator(-8.f, Yaw, 0.f));
		}
	}
	else
	{
		Mech->SpawnDefaultController();
	}
	if (AMechAIController* AI = Cast<AMechAIController>(Mech->GetController()))
	{
		AI->ArenaCenter = ArenaCenter;
		AI->ArenaExtent = ArenaExtent;
		AI->SetControlRotation(FRotator(0.f, Yaw, 0.f));
	}
	Mech->GetHealth()->OnDied.AddWeakLambda(this, [this, Mech](AActor* Killer) { OnMechDied(Mech, Killer); });
	Mechs.Remove(Old);
	Mechs.Add(Mech);
	if (bAutoBattle)
	{
		APlayerController* Viewer = GetWorld()->GetFirstPlayerController();
		if (Viewer && Viewer->GetViewTarget() == Old)
		{
			Viewer->SetViewTarget(Mech);
		}
	}
	Old->Destroy();
	UE_LOG(LogTemp, Display, TEXT("[%s] %s 重生 @ %s"), bCoreMode ? TEXT("core") : TEXT("dm"), *Mech->DisplayName, *At.GetLocation().ToCompactString());
	return Mech;
}

void AMechGameMode::RecordCoreScore(AMechCharacter* Carrier)
{
	if (Carrier)
	{
		FMechStats& S = Stats.FindOrAdd(Carrier->DisplayName);
		++S.Cores;
		S.Points[static_cast<int32>(EMechScorePart::Core)] += MechScore::CoreDeliver;  // 11-01 积分制：交芯 = 两个击毁
	}
}

void AMechGameMode::EndCoreMatch(int32 Winner)
{
	AMechGameState* GS = GetMechGameState();
	if (!GS || GS->Phase != EMechRoundPhase::Fight)
	{
		return;
	}
	if (CoreDirector)
	{
		CoreDirector->StopMatch();
		CoreDirector->LogSummary();
	}
	GS->RoundWinner = Winner;
	GS->MatchWinner = Winner;
	GS->Phase = EMechRoundPhase::MatchOver;
	GS->PhaseEndTime = GetWorld()->GetTimeSeconds() + MatchOverTime;
	UE_LOG(LogTemp, Display, TEXT("[battle] 比赛结束：%s，比分 蓝 %d : %d 红"), Winner < 0 ? TEXT("平局") : Winner == 0 ? TEXT("蓝队胜") : TEXT("红队胜"),
		GS->TeamScore[0], GS->TeamScore[1]);
	LogStats();
	PublishStats();
	if (bAutoBattle)
	{
		PendingEventShot = 1.f;
	}
}

// ---------------------------------------------------------------- 阵亡视角：停在阵亡点（鼠标绕着看）/ 观战队友（左键下一个，右键回阵亡点）
void AMechPlayerController::DeathCamTick(float DeltaTime)
{
	AMechCharacter* Me = Cast<AMechCharacter>(GetPawn());
	if (IsMenuOpen() || AMechResultsStage::IsActive(GetWorld()))  // 结算场景接管了镜头
	{
		return;
	}
	if (!Me || !Me->GetHealth()->IsDead())
	{
		if (bDeathCam)
		{
			bDeathCam = false;
			Spectated = nullptr;
			if (Me && GetViewTarget() == DeathCam)
			{
				SetViewTarget(Me);
			}
		}
		return;
	}
	if (!bDeathCam)
	{
		bDeathCam = true;
		DeathPoint = Me->GetActorLocation();
		DeathOrbit = FRotator(-22.f, GetControlRotation().Yaw, 0.f);
		Spectated = nullptr;
	}
	if (!DeathCam)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		DeathCam = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(DeathPoint), Params);
		DeathCam->GetCameraComponent()->bConstrainAspectRatio = false;
		DeathCam->GetCameraComponent()->SetFieldOfView(90.f);
	}
	if (MechKeys::JustPressed(this, EMechKey::Fire))
	{
		TArray<AMechCharacter*> Mates;
		for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
		{
			if (*It != Me && It->GetTeam() == Me->GetTeam() && !It->GetHealth()->IsDead() && !It->IsTrainingDummy())
			{
				Mates.Add(*It);
			}
		}
		Mates.Sort([](const AMechCharacter& A, const AMechCharacter& B) { return A.DisplayName < B.DisplayName; });
		if (Mates.Num())
		{
			const int32 Index = Mates.IndexOfByKey(Spectated.Get());
			Spectated = Mates[(Index + 1) % Mates.Num()];
		}
	}
	if (MechKeys::JustPressed(this, EMechKey::Alt) || (Spectated.IsValid() && Spectated->GetHealth()->IsDead()))
	{
		Spectated = nullptr;
	}
	float DX = 0.f, DY = 0.f;
	GetInputMouseDelta(DX, DY);
	DeathOrbit.Yaw += DX * 0.4f;
	DeathOrbit.Pitch = FMath::Clamp(DeathOrbit.Pitch + DY * 0.4f, -70.f, 15.f);
	const AMechCharacter* Target = Spectated.Get();
	const FVector Pivot = Target ? Target->GetActorLocation() + FVector(0.f, 0.f, 180.f) : DeathPoint + FVector(0.f, 0.f, 120.f);
	FVector At = Pivot - DeathOrbit.Vector() * (Target ? 700.f : 950.f);
	// 镜头别穿墙
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MechDeathCam), false, Me);
	if (Target)
	{
		Query.AddIgnoredActor(Target);
	}
	if (GetWorld()->SweepSingleByChannel(Hit, Pivot, At, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(25.f), Query))
	{
		At = Hit.Location;
	}
	DeathCam->SetActorLocationAndRotation(At, DeathOrbit);
	if (GetViewTarget() != DeathCam)
	{
		SetViewTarget(DeathCam);
	}
}

// ---------------------------------------------------------------- 房间 → 禁用 / 选配 → 回合
void AMechGameMode::StartMatchFlow()
{
	if (bUseLobby)
	{
		EnterLobby();
	}
	else if (bUseDraft)
	{
		StartDraft();
	}
	else
	{
		BeginRound();
	}
}

bool AMechGameMode::IsHost(const APlayerController* PC) const
{
	return PC && PC->IsLocalController() && GetNetMode() != NM_DedicatedServer;
}

AMechPlayerController* AMechGameMode::FindPlayer(int32 PlayerId) const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AMechPlayerController* PC = Cast<AMechPlayerController>(It->Get());
		if (PC && PC->PlayerState && PC->PlayerState->GetPlayerId() == PlayerId)
		{
			return PC;
		}
	}
	return nullptr;
}

void AMechGameMode::GatherHumans(TArray<APlayerController*> (&Humans)[2])
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* P = It->Get();
		if (!P || bAutoBattle)
		{
			continue;
		}
		AMechPlayerController* MPC = Cast<AMechPlayerController>(P);
		int32 Team = MPC ? MPC->AssignedTeam : -1;
		if (Team < 0 || Team > 1)
		{
			Team = Humans[0].Num() <= Humans[1].Num() ? 0 : 1;
			if (MPC)
			{
				MPC->AssignedTeam = Team;
			}
		}
		Humans[Team].Add(P);
	}
}

void AMechGameMode::EnterLobby()
{
	AMechGameState* GS = GetMechGameState();
	if (!GS)
	{
		return;
	}
	GS->Phase = EMechRoundPhase::Lobby;
	GS->PhaseEndTime = 0.f;
	GS->Draft = FMechDraft();
	// 11-01 对战图里的老式房间（直接用 ?Match 进图）：房间的选择也放 GameState::Room 同步给所有人，地图就是这张、不能换。
	// 禁用开关按房主这台设置里存的（以前是各人界面按各自的设置显示，客户端看到的不一定是房主的）
	{
		FMechRoomSettings S = GS->Room;
		S.Map = FMath::Max(0, MechMaps::FindByMapName(GetWorld()->GetMapName()));
		S.bDefense = false;
		S.AILevel = GS->AILevel;
		if (!bLegacyLobbyInit)
		{
			// 11-02 名单起手：每队 TeamSize 台，真人占的位置之外都是人机（之后房主随便加减）
			TArray<APlayerController*> LobbyHumans[2];
			GatherHumans(LobbyHumans);
			S.AIs.Reset();
			for (int32 T = 0; T < 2; ++T)
			{
				for (int32 n = LobbyHumans[T].Num(); n < TeamSize; ++n)
				{
					FMechRoomAI A;
					A.Team = T;
					A.Level = GS->AILevel;
					S.AIs.Add(A);
				}
			}
		}
		S.bBans = bLegacyLobbyInit ? GS->bDraftBans : FMechSettings::Get().bDraftBans;  // 「再来一局」回到这里时保留上一局的
		S.Rules = Rules;
		ApplyRoomToState(S);
		bLegacyLobbyInit = true;
	}
	RefreshLobby();
	UE_LOG(LogTemp, Display, TEXT("[lobby] 进房间：每队 %d 台，AI 强度 %d"), TeamSize, GS->AILevel);
}

void AMechGameMode::RefreshLobby()
{
	AMechGameState* GS = GetMechGameState();
	if (!GS)
	{
		return;
	}
	GS->Lobby.Reset();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AMechPlayerController* PC = Cast<AMechPlayerController>(It->Get());
		if (!PC)
		{
			continue;
		}
		FMechLobbyPlayer& E = GS->Lobby.AddDefaulted_GetRef();
		E.PlayerId = PC->PlayerState ? PC->PlayerState->GetPlayerId() : -1;
		E.Name = HumanName(PC);
		E.Team = FMath::Clamp(PC->AssignedTeam, 0, 1);
		E.bHost = IsHost(PC);
		E.Paint = PC->ProfilePaint;
	}
	PublishChallenge();  // 11-02 真人进出、换队，人数变了推荐倍数也变
}

void AMechGameMode::SetProfile(AMechPlayerController* PC, const FString& Name, const FMechPaint& Paint)
{
	if (!PC)
	{
		return;
	}
	FString Clean = Name.TrimStartAndEnd().Left(16);
	Clean.ReplaceInline(TEXT(" 击毁了 "), TEXT(" "));  // HUD 的击毁通报按这个词拆名字
	PC->ProfileName = Clean;
	PC->ProfilePaint = Paint;
	if (PC->PlayerState)
	{
		PC->PlayerState->SetPlayerName(HumanName(PC));
	}
	if (AMechCharacter* Mine = Cast<AMechCharacter>(PC->GetPawn()))
	{
		Mine->SetPaint(Paint);
	}
	AMechGameState* GS = GetMechGameState();
	if (GS && GS->Phase == EMechRoundPhase::Lobby)
	{
		RefreshLobby();
	}
	UE_LOG(LogTemp, Display, TEXT("[lobby] 玩家资料：%s 涂装 %s / %s / %s"), *HumanName(PC), *Paint.Primary.ToHex(), *Paint.Secondary.ToHex(), *Paint.Accent.ToHex());
}

void AMechGameMode::LobbySetTeam(AMechPlayerController* PC, int32 Team)
{
	AMechGameState* GS = GetMechGameState();
	if (!PC || !GS || GS->Phase != EMechRoundPhase::Lobby || Team < 0 || Team > 1)
	{
		return;
	}
	// 一队的真人不超过场上总台数上限
	int32 Count = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AMechPlayerController* Other = Cast<AMechPlayerController>(It->Get());
		Count += (Other && Other != PC && Other->AssignedTeam == Team) ? 1 : 0;
	}
	if (Count >= MechRoom::MaxMechs)
	{
		return;
	}
	PC->AssignedTeam = Team;
	if (UMechRoomSubsystem* Sub = bRoomMode ? UMechRoomSubsystem::Get(this) : nullptr; Sub && !PC->RoomToken.IsEmpty())
	{
		Sub->Members.FindOrAdd(PC->RoomToken).Team = Team;  // 11-01 换图回来还在这队
	}
	RefreshLobby();
}

void AMechGameMode::LobbyStart(AMechPlayerController* PC)
{
	AMechGameState* GS = GetMechGameState();
	if (!GS || !IsHost(PC) || bRoomTravelling)
	{
		return;
	}
	// 11-01 从房间开的场，等人进图时房主点「不等了」：没到的人进来后接管 AI
	if (bFromRoom && !bRoundsStarted && GS->Phase == EMechRoundPhase::Waiting)
	{
		bRoundsStarted = true;
		UE_LOG(LogTemp, Display, TEXT("[room] 房主不等了：%d / %d 名玩家已进图，开始"), GS->RoomArrived, GS->RoomExpected);
		GetWorldTimerManager().SetTimer(StartTimer, FTimerDelegate::CreateUObject(this, &AMechGameMode::StartMatchFlow), 0.5f, false);
		return;
	}
	if (GS->Phase != EMechRoundPhase::Lobby)
	{
		return;
	}
	if (!MechRoom::StartProblem(GS->Room, *GS).IsEmpty())
	{
		return;  // 11-02 某队没人（界面上开始按钮是灰的，这里再挡一下：真开局一队全空就直接判输了）
	}
	FMechRoomSettings S = GS->Room;  // 房主在房间里的选择（LobbySetRoom 每改一次已经写进 GameState）
	TrimRoomAIs(S);
	MechRoom::Save(S);  // 记住这次的选择，下次开房按这个起（服务器 = 房主这台；测试开关下不存）
	if (bRoomMode)
	{
		// 11-01 房间图：记下每人的队伍和开始时在房间里的人，换到选好的地图（非无缝，客户端自动跟着重连；对战图开局从子系统读这些）
		UMechRoomSubsystem* Sub = UMechRoomSubsystem::Get(this);
		if (!Sub)
		{
			return;
		}
		Sub->Settings = S;
		Sub->Expected.Reset();
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			const AMechPlayerController* P = Cast<AMechPlayerController>(It->Get());
			if (P && !P->RoomToken.IsEmpty())
			{
				Sub->Expected.AddUnique(P->RoomToken);
				Sub->Members.FindOrAdd(P->RoomToken).Team = FMath::Clamp(P->AssignedTeam, 0, 1);
			}
		}
		Sub->bMatchPending = true;
		bRoomTravelling = true;
		GS->bRoomTravelling = true;
		const FString Url = MechRoom::MatchURL(S);
		UE_LOG(LogTemp, Display, TEXT("[room] 房主开始：%s，%d 名玩家，换图 %s"), *MechRoom::Describe(S), Sub->Expected.Num(), *Url);
		GetWorld()->ServerTravel(Url, true);  // 绝对地址：相对换图会把房间图的 ?Room 带过去
		return;
	}
	// 对战图里的老式房间（直接用 ?Match 进对战图的测试 / 启动脚本）：就地开选配
	ApplyRoomToState(S);
	bCustomRoster = true;
	bRosterBuilt = false;
	Rules = S.Rules;
	ApplyRules();
	UE_LOG(LogTemp, Display, TEXT("[lobby] 房主开始：%s，人机 %d 台，默认强度 %d，禁用阶段 %s"), *MechRules::Describe(Rules, bCoreMode), S.AIs.Num(), GS->AILevel,
		GS->bDraftBans && !IsSoloDM() ? TEXT("开") : TEXT("关"));
	if (bUseDraft)
	{
		StartDraft();
	}
	else
	{
		BeginRound();
	}
}

// ---------------------------------------------------------------- 11-01 房间（MechRoom.h）
void AMechGameMode::ApplyRoomToState(const FMechRoomSettings& S)
{
	AMechGameState* GS = GetMechGameState();
	if (!GS)
	{
		return;
	}
	GS->Room = S;
	GS->Rules = S.Rules;
	GS->AILevel = S.AILevel;
	GS->bDraftBans = S.bBans;
}

void AMechGameMode::LobbySetRoom(AMechPlayerController* PC, const FMechRoomSettings& InRoom)
{
	AMechGameState* GS = GetMechGameState();
	if (!GS || GS->Phase != EMechRoundPhase::Lobby || !IsHost(PC) || bRoomTravelling)
	{
		return;  // 只有房主能改（别人发来的不认）；开始以后不能改
	}
	FMechRoomSettings S = InRoom;
	if (!bRoomMode)
	{
		// 对战图里的老式房间：地图已经定了，模式、规则、人机照旧能改
		S.Map = GS->Room.Map;
		S.bDefense = false;
	}
	MechRoom::Sanitize(S);
	TrimRoomAIs(S);
	ApplyRoomToState(S);
	PublishChallenge();
	if (bRoomMode)
	{
		if (UMechRoomSubsystem* Sub = UMechRoomSubsystem::Get(this))
		{
			Sub->Settings = S;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[room] 房主改选择：%s，人机 %d 台，禁用阶段 %s"), *MechRoom::Describe(S), S.AIs.Num(), S.bBans ? TEXT("开") : TEXT("关"));
}

// ---------------------------------------------------------------- 11-02 阵容
const AMechGameMode::FMechRosterSlot* AMechGameMode::RosterAt(int32 Team, int32 Index) const
{
	return Roster.FindByPredicate([Team, Index](const FMechRosterSlot& S) { return S.Team == Team && S.Index == Index; });
}

void AMechGameMode::PublishRosterCounts()
{
	TeamCount[0] = TeamCount[1] = 0;
	for (const FMechRosterSlot& S : Roster)
	{
		++TeamCount[FMath::Clamp(S.Team, 0, 1)];
	}
	TeamSize = FMath::Max(TeamCount[0], TeamCount[1]);
	if (AMechGameState* GS = GetMechGameState())
	{
		GS->TeamSize = TeamSize;
		GS->TeamCount[0] = TeamCount[0];
		GS->TeamCount[1] = TeamCount[1];
	}
}

void AMechGameMode::PublishChallenge()
{
	AMechGameState* GS = GetMechGameState();
	if (!GS)
	{
		return;
	}
	// 测试 -MechChallengeK=2.6（量挑战倍数，和 -MechTeamCounts 一起用）：人少那队装甲、伤害都乘这个
	float TestK = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("MechChallengeK="), TestK))
	{
		GS->ChallengeTeam = TeamCount[0] == TeamCount[1] ? -1 : TeamCount[0] < TeamCount[1] ? 0 : 1;
		GS->ChallengeArmor = GS->ChallengeDamage = TestK;
		return;
	}
	const bool bLobby = GS->Phase == EMechRoundPhase::Lobby;
	const int32 C0 = bLobby ? MechRoom::CountOf(GS->Room, *GS, 0) : TeamCount[0];
	const int32 C1 = bLobby ? MechRoom::CountOf(GS->Room, *GS, 1) : TeamCount[1];
	MechRoom::EffectiveChallenge(GS->Room, MechRoom::IsSolo(GS->Room, GS->bRoomMode, GS->bCoreMode), C0, C1, GS->ChallengeTeam, GS->ChallengeArmor, GS->ChallengeDamage);
}

void AMechGameMode::TrimRoomAIs(FMechRoomSettings& S)
{
	const int32 Humans = GetNumPlayers();
	while (S.AIs.Num() > 0 && S.AIs.Num() + Humans > MechRoom::MaxMechs)
	{
		S.AIs.Pop();
	}
}

void AMechGameMode::BuildRoster()
{
	AMechGameState* GS = GetMechGameState();
	TArray<APlayerController*> Humans[2];
	GatherHumans(Humans);
	auto IdOf = [](const APlayerController* P) { return P && P->PlayerState ? P->PlayerState->GetPlayerId() : -1; };
	Roster.Reset();
	if (bCustomRoster && GS && IsSoloDM())
	{
		// 个人战没有队伍：真人和人机排成一列，Team 只是选配的两个分组（轮流分，选配还是 A B B A 的顺序）；队伍号各不相同（SoloTeam）
		TArray<FMechRosterSlot> All;
		for (int32 T = 0; T < 2; ++T)
		{
			for (const APlayerController* P : Humans[T])
			{
				FMechRosterSlot& S = All.AddDefaulted_GetRef();
				S.PlayerId = IdOf(P);
			}
		}
		for (const FMechRoomAI& A : GS->Room.AIs)
		{
			FMechRosterSlot& S = All.AddDefaulted_GetRef();
			S.AILevel = A.Level;
		}
		for (int32 k = 0; k < All.Num(); ++k)
		{
			All[k].Team = k % 2;
			All[k].Index = k / 2;
		}
		Roster = MoveTemp(All);
	}
	else if (bCustomRoster && GS)
	{
		// 一队 = 这队的真人 + 房主给这队加的人机，两队可以不一样（1v5 也行）
		for (int32 T = 0; T < 2; ++T)
		{
			int32 Index = 0;
			for (const APlayerController* P : Humans[T])
			{
				FMechRosterSlot& S = Roster.AddDefaulted_GetRef();
				S.Team = T;
				S.Index = Index++;
				S.PlayerId = IdOf(P);
			}
			for (const FMechRoomAI& A : GS->Room.AIs)
			{
				if (A.Team == T)
				{
					FMechRosterSlot& S = Roster.AddDefaulted_GetRef();
					S.Team = T;
					S.Index = Index++;
					S.AILevel = A.Level;
				}
			}
		}
	}
	else
	{
		// 没走房间的（自动对战、按键 / 联机测试、直接开打）：每队 TeamSize 台，空位 AI 补；一队的真人比这多就都加到装得下（最多 4 台）
		const int32 MostHumans = FMath::Max(Humans[0].Num(), Humans[1].Num());
		const int32 Size = MostHumans > TeamSize ? FMath::Min(MostHumans, 4) : TeamSize;
		if (Size != TeamSize)
		{
			UE_LOG(LogTemp, Display, TEXT("[battle] 真人多，每队加到 %d 台"), Size);
		}
		// 测试 -MechTeamCounts=1,5（量挑战倍数）：自动对战两队台数不一样
		int32 Sizes[2] = {Size, Size};
		FString CountsArg, CountA, CountB;
		if (FParse::Value(FCommandLine::Get(), TEXT("MechTeamCounts="), CountsArg) && CountsArg.Split(TEXT(","), &CountA, &CountB))
		{
			Sizes[0] = FCString::Atoi(*CountA);
			Sizes[1] = FCString::Atoi(*CountB);
		}
		for (int32 T = 0; T < 2; ++T)
		{
			for (int32 i = 0; i < Sizes[T]; ++i)
			{
				FMechRosterSlot& S = Roster.AddDefaulted_GetRef();
				S.Team = T;
				S.Index = i;
				S.PlayerId = Humans[T].IsValidIndex(i) ? IdOf(Humans[T][i]) : -1;
			}
		}
	}
	bRosterBuilt = true;
	PublishRosterCounts();
	UE_LOG(LogTemp, Display, TEXT("[room] 阵容：蓝 %d 台 红 %d 台（真人 %d）"), TeamCount[0], TeamCount[1], Humans[0].Num() + Humans[1].Num());
}

void AMechGameMode::RefreshRoster()
{
	if (!bRosterBuilt)
	{
		BuildRoster();
		return;
	}
	// 走了的真人：这个位置交给人机
	for (FMechRosterSlot& S : Roster)
	{
		if (S.PlayerId >= 0 && !FindPlayer(S.PlayerId))
		{
			UE_LOG(LogTemp, Display, TEXT("[room] 阵容：%d 队第 %d 位的真人走了，交给人机"), S.Team, S.Index);
			S.PlayerId = -1;
			S.AILevel = -1;
		}
	}
	// 新来的真人（选配之后才进来、对局中接管了人机的）：顶掉本队一个人机的位置；本队没有人机就多排一个
	if (!bAutoBattle)
	{
		TArray<APlayerController*> Humans[2];
		GatherHumans(Humans);
		const bool bSolo = IsSoloDM();
		for (int32 T = 0; T < 2; ++T)
		{
			for (const APlayerController* P : Humans[T])
			{
				const int32 Id = P && P->PlayerState ? P->PlayerState->GetPlayerId() : -1;
				if (Id < 0 || Roster.ContainsByPredicate([Id](const FMechRosterSlot& S) { return S.PlayerId == Id; }))
				{
					continue;
				}
				if (FMechRosterSlot* Free = Roster.FindByPredicate([T, bSolo](const FMechRosterSlot& S) { return S.PlayerId < 0 && (bSolo || S.Team == T); }))
				{
					Free->PlayerId = Id;
					continue;
				}
				auto Append = [this](int32 Team, int32 PlayerId) {
					int32 Index = 0;
					for (const FMechRosterSlot& S : Roster)
					{
						Index += S.Team == Team ? 1 : 0;
					}
					FMechRosterSlot& S = Roster.AddDefaulted_GetRef();
					S.Team = Team;
					S.Index = Index;
					S.PlayerId = PlayerId;
				};
				Append(T, Id);
				if (!bCustomRoster)
				{
					Append(1 - T, -1);  // 没走房间的：两队还是一样多（以前的规矩）
				}
			}
		}
	}
	PublishRosterCounts();
}

void AMechGameMode::SetupRoom()
{
	bRoomMode = true;
	UMechRoomSubsystem* Sub = UMechRoomSubsystem::Get(this);
	if (Sub && !Sub->bActive)
	{
		Sub->BeginRoom();  // 直接用 ?Room 地址启动（联机_开房.bat、测试）
	}
	if (Sub)
	{
		Sub->bMatchPending = false;
		MechRoom::Sanitize(Sub->Settings);
	}
	AMechGameState* GS = GetMechGameState();
	if (!GS)
	{
		return;
	}
	GS->bRoomMode = true;
	ApplyRoomToState(Sub ? Sub->Settings : MechRoom::LoadSaved());
	GS->Phase = EMechRoundPhase::Lobby;
	GS->PhaseEndTime = 0.f;
	// 已经连着的人（房主自己）先按加入时分的队伍列上；换图回来的人在 RoomHello 里按令牌恢复队伍
	RefreshLobby();
	UE_LOG(LogTemp, Display, TEXT("[room] 进房间图：%s，网络 %s"), *MechRoom::Describe(GS->Room),
		GetNetMode() == NM_ListenServer ? TEXT("开房（端口 7777）") : TEXT("单机（没开成房，朋友连不进来）"));
}

void AMechGameMode::RoomHello(AMechPlayerController* PC, const FString& Token)
{
	if (!PC || Token.IsEmpty())
	{
		return;
	}
	PC->RoomToken = Token.Left(64);
	UMechRoomSubsystem* Sub = UMechRoomSubsystem::Get(this);
	if (!Sub || !Sub->bActive)
	{
		return;  // 不是开房流程（直接进对战图的测试等）：只记下令牌
	}
	if (UMechRoomSubsystem::FMember* M = Sub->Members.Find(PC->RoomToken))
	{
		// 换图前就在：恢复队伍和编号。已经接管了机体的（超时开打后才进来的）队伍照机体的来
		const AMechCharacter* Mine = Cast<AMechCharacter>(PC->GetPawn());
		if (Mine && !bRangeMode && Mine->GetTeam() >= 0 && Mine->GetTeam() <= 1)  // 个人死斗的队伍号从 2 起，那时不按机体改；保卫战、训练场的机体都在 0 队，也不按机体改（回房间后还在原来那队）
		{
			M->Team = Mine->GetTeam();
		}
		PC->AssignedTeam = M->Team;
		PC->PlayerNumber = M->Number;
	}
	else
	{
		UMechRoomSubsystem::FMember& New = Sub->Members.Add(PC->RoomToken);
		New.Team = FMath::Clamp(PC->AssignedTeam, 0, 1);
		New.Number = ++Sub->NextNumber;
		PC->PlayerNumber = New.Number;
	}
	if (PC->PlayerState)
	{
		PC->PlayerState->SetPlayerName(HumanName(PC));
	}
	UE_LOG(LogTemp, Display, TEXT("[room] %s 报到（%s队）"), *HumanName(PC), PC->AssignedTeam == 0 ? TEXT("蓝") : TEXT("红"));
	const AMechGameState* GS = GetMechGameState();
	if (GS && GS->Phase == EMechRoundPhase::Lobby)
	{
		RefreshLobby();
	}
}

void AMechGameMode::RoomWaitTick()
{
	AMechGameState* GS = GetMechGameState();
	if (!bFromRoom || bRoundsStarted || !GS)
	{
		return;
	}
	const UMechRoomSubsystem* Sub = UMechRoomSubsystem::Get(this);
	int32 Arrived = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AMechPlayerController* P = Cast<AMechPlayerController>(It->Get());
		Arrived += P && Sub && Sub->Expected.Contains(P->RoomToken) ? 1 : 0;
	}
	GS->RoomArrived = Arrived;
	const bool bAll = Arrived >= GS->RoomExpected;
	if (bAll || GetWorld()->GetTimeSeconds() >= GS->PhaseEndTime)
	{
		bRoundsStarted = true;
		UE_LOG(LogTemp, Display, TEXT("[room] %s：%d / %d 名玩家已进图，开始"), bAll ? TEXT("到齐") : TEXT("等到时间了"), Arrived, GS->RoomExpected);
		GetWorldTimerManager().SetTimer(StartTimer, FTimerDelegate::CreateUObject(this, &AMechGameMode::StartMatchFlow), bAll ? 1.f : 0.2f, false);
	}
}

void AMechGameMode::RoomDefenseTick()
{
	// 11-01 保卫战没有结算页：守住 / 指挥所被摧毁后停 12 秒（HUD 上照旧写着「守住了」/「指挥所被摧毁」）再回房间
	if (!bFromRoom || !bDefenseMode || bRoomTravelling)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (DefenseEndAt < 0.f)
	{
		for (TActorIterator<AMechDefenseDirector> It(GetWorld()); It; ++It)
		{
			if (It->Phase == EMechDefensePhase::Won || It->Phase == EMechDefensePhase::Lost)
			{
				DefenseEndAt = Now + 12.f;
				if (AMechGameState* GS = GetMechGameState())
				{
					GS->MulticastFeed(TEXT("12 秒后回房间"), -1);
				}
				UE_LOG(LogTemp, Display, TEXT("[room] 保卫战%s，12 秒后回房间"), It->Phase == EMechDefensePhase::Won ? TEXT("守住了") : TEXT("失败"));
			}
			break;
		}
		return;
	}
	if (Now >= DefenseEndAt)
	{
		ReturnToRoom();
	}
}

void AMechGameMode::ReturnToRoom()
{
	UMechRoomSubsystem* Sub = UMechRoomSubsystem::Get(this);
	if (!bFromRoom || bRoomTravelling || !Sub)
	{
		return;
	}
	// 记下现在每人的队伍（中途进来的人也算进房间）；地图、规则就是开始时那份，没动过
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AMechPlayerController* P = Cast<AMechPlayerController>(It->Get());
		if (P && !P->RoomToken.IsEmpty() && P->AssignedTeam >= 0 && P->AssignedTeam <= 1)
		{
			Sub->Members.FindOrAdd(P->RoomToken).Team = P->AssignedTeam;
		}
	}
	Sub->bMatchPending = false;
	bRoomTravelling = true;
	if (AMechGameState* GS = GetMechGameState())
	{
		GS->bRoomTravelling = true;
	}
	UE_LOG(LogTemp, Display, TEXT("[room] 回房间：%s"), *MechRoom::RoomURL());
	GetWorld()->ServerTravel(MechRoom::RoomURL(), true);
}

void AMechGameMode::StartDraft()
{
	AMechGameState* GS = GetMechGameState();
	if (!GS)
	{
		return;
	}
	RefreshRoster();  // 11-02 谁在哪个位置、两队各几台都看阵容
	DraftRand.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
	FMechDraft D;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		for (int32 i = 0; i < TeamCount[Team]; ++i)
		{
			const FMechRosterSlot* Seat = RosterAt(Team, i);
			APlayerController* H = Seat && Seat->PlayerId >= 0 ? FindPlayer(Seat->PlayerId) : nullptr;
			FMechDraftSlot S;
			S.Team = Team;
			S.Index = i;
			S.PlayerId = H && H->PlayerState ? H->PlayerState->GetPlayerId() : -1;
			S.Name = H ? HumanName(H) : SlotName(Team, i);
			D.Slots.Add(S);
		}
	}
	for (FMechDraftSlot& S : D.Slots)
	{
		S.Loadout = MechDraft::Sanitize(D, S.Team, FMechLoadout());
	}
	// 禁用：A B A B（每队的队长：第一个真人，没有就第一台）；选配：蛇形 A B B A A B …
	const int32 First = DraftRand.RandBool() ? 0 : 1;
	auto Captain = [&D](int32 Team) {
		int32 Any = INDEX_NONE;
		for (int32 s = 0; s < D.Slots.Num(); ++s)
		{
			if (D.Slots[s].Team == Team)
			{
				if (D.Slots[s].PlayerId >= 0)
				{
					return s;
				}
				Any = Any == INDEX_NONE ? s : Any;
			}
		}
		return Any;
	};
	// 房主关掉禁用阶段：不排禁用轮，直接选配；个人死斗没有「对面」，不禁
	const bool bBans = GS->bDraftBans && !IsSoloDM();
	for (int32 k = 0; k < (bBans ? MechDraft::BansPerTeam * 2 : 0); ++k)
	{
		FMechDraftTurn T;
		T.Team = k % 2 == 0 ? First : 1 - First;
		T.Slot = Captain(T.Team);
		T.bBan = true;
		D.Order.Add(T);
	}
	int32 Picked[2] = {0, 0};
	for (int32 k = 0; k < TeamCount[0] + TeamCount[1]; ++k)
	{
		FMechDraftTurn T;
		T.Team = ((k + 1) / 2) % 2 == 0 ? First : 1 - First;
		if (Picked[T.Team] >= TeamCount[T.Team])
		{
			T.Team = 1 - T.Team;  // 11-02 两队台数不一样：这队选完了，后面都是另一队
		}
		const int32 Want = Picked[T.Team]++;
		T.Slot = D.Slots.IndexOfByPredicate([&](const FMechDraftSlot& S) { return S.Team == T.Team && S.Index == Want; });
		T.bBan = false;
		D.Order.Add(T);
	}
	GS->Draft = D;
	GS->Phase = EMechRoundPhase::Draft;
	DraftShotTurn = -1;
	BeginDraftTurn();
	UE_LOG(LogTemp, Display, TEXT("[draft] 开始：蓝 %d 台 红 %d 台，%s队先%s"), TeamCount[0], TeamCount[1], First == 0 ? TEXT("蓝") : TEXT("红"), bBans ? TEXT("禁") : TEXT("选"));
}

void AMechGameMode::BeginDraftTurn()
{
	AMechGameState* GS = GetMechGameState();
	const FMechDraftTurn* T = GS ? GS->Draft.Current() : nullptr;
	if (!T)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	DraftTurnStart = Now;
	GS->Draft.TurnEnd = Now + (T->bBan ? MechDraft::BanTime : MechDraft::PickTime);
	GS->PhaseEndTime = GS->Draft.TurnEnd;
	DraftAIDelay = bAutoBattle ? 0.9f : DraftRand.FRandRange(1.4f, 2.8f);
}

void AMechGameMode::NextDraftTurn()
{
	AMechGameState* GS = GetMechGameState();
	FMechDraft& D = GS->Draft;
	++D.Turn;
	if (D.Turn >= D.Order.Num())
	{
		D.bDone = true;
		GS->PhaseEndTime = GetWorld()->GetTimeSeconds() + (bAutoBattle ? 2.5f : MechDraft::ReviewTime);
		for (const FMechDraftSlot& S : D.Slots)
		{
			UE_LOG(LogTemp, Display, TEXT("[draft] %s：%s"), *S.Name, *MechLoadout::Describe(S.Loadout));
		}
		return;
	}
	BeginDraftTurn();
}

void AMechGameMode::DraftTick()
{
	AMechGameState* GS = GetMechGameState();
	FMechDraft& D = GS->Draft;
	const float Now = GetWorld()->GetTimeSeconds();
	// 自动对战：每一轮开始后截一张选配界面（Saved/Screenshots/Draft）
	if (bAutoBattle && DraftShotTurn != D.Turn && Now - DraftTurnStart > 0.6f)
	{
		DraftShotTurn = D.Turn;
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Draft") / FString::Printf(TEXT("d_%02d.png"), D.Turn), true, false);
	}
	if (D.bDone)
	{
		if (Now >= GS->PhaseEndTime)
		{
			BeginRound();
		}
		return;
	}
	const FMechDraftTurn* T = D.Current();
	if (!T || !D.Slots.IsValidIndex(T->Slot))
	{
		return;
	}
	const FMechDraftSlot& S = D.Slots[T->Slot];
	const bool bHuman = S.PlayerId >= 0 && FindPlayer(S.PlayerId) != nullptr;
	if (Now < D.TurnEnd && (bHuman || Now - DraftTurnStart < DraftAIDelay))
	{
		return;
	}
	// AI 到点自己选；真人超时：禁用替他随机禁一件，选配用他已经挑好的（没挑的类别用能用的第一件）
	if (T->bBan)
	{
		const FMechDraftBan B = MechDraft::AutoBan(D, T->Team, DraftRand);
		D.Bans.Add(B);
		UE_LOG(LogTemp, Display, TEXT("[draft] %s队禁 %s · %s"), T->Team == 0 ? TEXT("蓝") : TEXT("红"), MechLoadout::CategoryName(B.Category), MechLoadout::Item(B.Category, B.Index).Name);
	}
	else
	{
		FMechDraftSlot& Slot = D.Slots[T->Slot];
		Slot.Loadout = bHuman ? MechDraft::Sanitize(D, Slot.Team, Slot.Loadout) : MechDraft::AutoPick(D, T->Slot, DraftRand);
		Slot.bLocked = true;
	}
	NextDraftTurn();
}

void AMechGameMode::DraftBan(AMechPlayerController* PC, int32 Category, int32 Index)
{
	AMechGameState* GS = GetMechGameState();
	if (!PC || !PC->PlayerState || !GS || GS->Phase != EMechRoundPhase::Draft)
	{
		return;
	}
	FMechDraft& D = GS->Draft;
	const FMechDraftTurn* T = D.Current();
	const int32 Mine = D.FindPlayer(PC->PlayerState->GetPlayerId());
	if (!T || !T->bBan || !D.Slots.IsValidIndex(Mine) || D.Slots[Mine].Team != T->Team || !MechDraft::IsBannable(Category) ||
		Index < 0 || Index >= MechLoadout::NumItems(Category) || !MechLoadout::Item(Category, Index).bReady || MechDraft::HasBanned(D, T->Team, Category, Index))
	{
		return;
	}
	FMechDraftBan B;
	B.Team = T->Team;
	B.Category = static_cast<uint8>(Category);
	B.Index = static_cast<uint8>(Index);
	D.Bans.Add(B);
	UE_LOG(LogTemp, Display, TEXT("[draft] %s 禁 %s · %s"), *D.Slots[Mine].Name, MechLoadout::CategoryName(Category), MechLoadout::Item(Category, Index).Name);
	NextDraftTurn();
}

void AMechGameMode::DraftPick(AMechPlayerController* PC, const FMechLoadout& Loadout, bool bLock)
{
	AMechGameState* GS = GetMechGameState();
	if (!PC || !PC->PlayerState || !GS || GS->Phase != EMechRoundPhase::Draft)
	{
		return;
	}
	FMechDraft& D = GS->Draft;
	const FMechDraftTurn* T = D.Current();
	const int32 Mine = D.FindPlayer(PC->PlayerState->GetPlayerId());
	if (!T || T->bBan || Mine != T->Slot)
	{
		return;
	}
	FMechDraftSlot& Slot = D.Slots[Mine];
	Slot.Loadout = MechDraft::Sanitize(D, Slot.Team, Loadout);
	if (bLock)
	{
		Slot.bLocked = true;
		UE_LOG(LogTemp, Display, TEXT("[draft] %s 定下：%s"), *Slot.Name, *MechLoadout::Describe(Slot.Loadout));
		NextDraftTurn();
	}
}

// ---------------------------------------------------------------- 控制器：房间 / 选配的按钮和资料
void AMechPlayerController::SendProfile()
{
	if (!bSentRoomHello)
	{
		// 11-01 先报房间令牌（服务器按它恢复换图前的队伍），再发名字涂装：同一个控制器上的可靠 RPC 按顺序到
		if (const UMechRoomSubsystem* Sub = UMechRoomSubsystem::Get(this))
		{
			ServerRoomHello(Sub->Token);
		}
		bSentRoomHello = true;
	}
	const FMechSettings& S = FMechSettings::Get();
	ServerSetProfile(S.PlayerName, S.Paint);
	bSentProfile = true;
}

void AMechPlayerController::LobbySetTeam(int32 Team)
{
	ServerLobbyTeam(Team);
}

void AMechPlayerController::LobbyStart()
{
	ServerLobbyStart();
}

void AMechPlayerController::LobbySetRoom(const FMechRoomSettings& InRoom)
{
	ServerLobbyRoom(InRoom);
}

void AMechPlayerController::ServerLobbyRoom_Implementation(const FMechRoomSettings& InRoom)
{
	if (AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>())
	{
		GM->LobbySetRoom(this, InRoom);
	}
}

void AMechPlayerController::ServerRoomHello_Implementation(const FString& Token)
{
	if (AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>())
	{
		GM->RoomHello(this, Token);
	}
}

void AMechPlayerController::DraftBan(int32 Category, int32 Index)
{
	ServerDraftBan(static_cast<uint8>(Category), static_cast<uint8>(Index));
}

void AMechPlayerController::DraftPick(const FMechLoadout& Loadout, bool bLock)
{
	ServerDraftPick(Loadout, bLock);
}

void AMechPlayerController::ServerSetProfile_Implementation(const FString& Name, const FMechPaint& Paint)
{
	if (AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>())
	{
		GM->SetProfile(this, Name, Paint);
	}
}

void AMechPlayerController::ServerLobbyTeam_Implementation(int32 Team)
{
	if (AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>())
	{
		GM->LobbySetTeam(this, Team);
	}
}

void AMechPlayerController::ServerLobbyStart_Implementation()
{
	if (AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>())
	{
		GM->LobbyStart(this);
	}
}

void AMechPlayerController::ServerDraftBan_Implementation(uint8 Category, uint8 Index)
{
	if (AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>())
	{
		GM->DraftBan(this, Category, Index);
	}
}

void AMechPlayerController::ServerDraftPick_Implementation(const FMechLoadout& Loadout, bool bLock)
{
	if (AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>())
	{
		GM->DraftPick(this, Loadout, bLock);
	}
}

AMechItemStudio* AMechPlayerController::GetStudio()
{
	if (!Studio && IsLocalController())
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Studio = GetWorld()->SpawnActor<AMechItemStudio>(AMechItemStudio::StaticClass(), FTransform(AMechItemStudio::StudioLocation), P);
	}
	return Studio;
}

void AMechPlayerController::DraftScreenTick()
{
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	// 11-01 从房间开的场，进图后等人到齐那段（Waiting）也开着：整屏显示「等待玩家进图 N / M」（保卫战不等人）
	const bool bWant = GS && !GS->bMenuMode && (GS->Phase == EMechRoundPhase::Draft || GS->Phase == EMechRoundPhase::Lobby ||
		(GS->bRoomMatch && !GS->bRangeMode && GS->Phase == EMechRoundPhase::Waiting));
	if (bWant && !DraftScreen.IsValid())
	{
		OpenDraftScreen();
	}
	else if (!bWant && DraftScreen.IsValid())
	{
		CloseDraftScreen();
	}
	static const bool bLobbyTest = FParse::Param(FCommandLine::Get(), TEXT("MechLobbyTest"));
	if (bLobbyTest)
	{
		LobbyTestTick(GetWorld()->GetDeltaSeconds());
	}
}

void AMechPlayerController::LobbyTestTick(float DeltaTime)
{
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	if (!GS || !PlayerState)
	{
		return;
	}
	auto Shot = [](const FString& Name) {
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Lobby") / Name, true, false);
	};
	LobbyTestClock += DeltaTime;
	if (GS->Phase == EMechRoundPhase::Lobby)
	{
		if (LobbyTestStep == 0 && LobbyTestClock > 2.5f)
		{
			Shot(TEXT("lobby.png"));
			LobbyTestStep = 1;
		}
		else if (LobbyTestStep == 1 && LobbyTestClock > 3.5f)
		{
			FMechRoomSettings Room = GS->Room;  // 11-01 房主的选择先同步（LobbySetRoom），开始只是一个按钮
			Room.AILevel = 2;
			Room.AIs.Reset();  // 11-02 3v3：自己这队 2 台人机，对面 3 台
			for (const int32 T : {0, 0, 1, 1, 1})
			{
				FMechRoomAI A;
				A.Team = T;
				A.Level = 2;
				Room.AIs.Add(A);
			}
			Room.bBans = !FParse::Param(FCommandLine::Get(), TEXT("MechLobbyNoBans"));  // -MechLobbyNoBans 测关掉禁用阶段
			LobbySetRoom(Room);
			LobbyStart();
			LobbyTestStep = 2;
		}
		return;
	}
	if (GS->Phase == EMechRoundPhase::Fight)
	{
		const float Before = LobbyTestTurnClock;
		LobbyTestTurnClock += DeltaTime;
		if (FMath::FloorToInt(Before / 8.f) != FMath::FloorToInt(LobbyTestTurnClock / 8.f))
		{
			Shot(FString::Printf(TEXT("f_%02d.png"), FMath::FloorToInt(LobbyTestTurnClock / 8.f)));
		}
		return;
	}
	if (GS->Phase != EMechRoundPhase::Draft)
	{
		return;
	}
	const FMechDraft& D = GS->Draft;
	if (D.Turn != LobbyTestTurn)
	{
		LobbyTestTurn = D.Turn;
		LobbyTestTurnClock = 0.f;
	}
	const float Before = LobbyTestTurnClock;
	LobbyTestTurnClock += DeltaTime;
	if (Before < 1.2f && LobbyTestTurnClock >= 1.2f)
	{
		Shot(FString::Printf(TEXT("d_%02d.png"), D.Turn));
	}
	const FMechDraftTurn* T = D.Current();
	const int32 Mine = D.FindPlayer(PlayerState->GetPlayerId());
	if (!T || !D.Slots.IsValidIndex(Mine) || Before >= 1.8f || LobbyTestTurnClock < 1.8f)
	{
		return;
	}
	const int32 Team = D.Slots[Mine].Team;
	if (T->bBan && T->Team == Team)
	{
		for (int32 i = 0; i < MechLoadout::NumItems(MechLoadout::Shooter); ++i)
		{
			if (!MechDraft::HasBanned(D, Team, MechLoadout::Shooter, i))
			{
				DraftBan(MechLoadout::Shooter, i);
				break;
			}
		}
	}
	else if (!T->bBan && T->Slot == Mine)
	{
		FMechLoadout L;
		L.Shooter = EMechShooter::Shotgun;
		L.Melee = static_cast<EMechMeleeWeapon>(FMath::Min(6, MechLoadout::NumItems(MechLoadout::Melee) - 1));
		DraftPick(MechDraft::Sanitize(D, Team, L), true);
	}
}

void AMechPlayerController::OpenDraftScreen()
{
	if (!IsLocalController() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	SAssignNew(DraftScreen, SMechDraftScreen).Owner(this);
	DraftContainer = SNew(SWeakWidget).PossiblyNullContent(DraftScreen);
	GEngine->GameViewport->AddViewportWidgetContent(DraftContainer.ToSharedRef(), 40);  // 暂停菜单是 50，盖在它上面
	if (!IsMenuOpen())
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(DraftScreen);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		SetShowMouseCursor(true);
		FlushPressedKeys();
	}
}

void AMechPlayerController::CloseDraftScreen()
{
	if (DraftContainer.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(DraftContainer.ToSharedRef());
	}
	DraftScreen.Reset();
	DraftContainer.Reset();
	if (Studio)
	{
		Studio->Destroy();
		Studio = nullptr;
	}
	if (!IsMenuOpen())
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
	}
}

// ---------------------------------------------------------------- 插件舱
void AMechGameMode::ClearPluginPods()
{
	for (const TWeakObjectPtr<AMechPluginPod>& P : Pods)
	{
		if (P.IsValid())
		{
			P->Destroy();
		}
	}
	Pods.Reset();
}

void AMechGameMode::PluginTick()
{
	if (!bUsePlugins || NextPodTime < 0.f)
	{
		return;
	}
	Pods.RemoveAll([](const TWeakObjectPtr<AMechPluginPod>& P) { return !P.IsValid(); });
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextPodTime)
	{
		return;
	}
	NextPodTime = Now + (bCoreMode ? 50.f : 45.f);
	const TArray<FName> Ids = MechEffects::PluginIds();
	if (Pods.Num() >= (bCoreMode ? 2 : 1) || Ids.IsEmpty())
	{
		return;
	}
	// 死斗：场地中央；夺芯：铸造台左右两侧 12 米轮流（谷底中间，两队一样远）
	FVector At = ArenaCenter;
	if (bCoreMode && CoreArena)
	{
		At = CoreArena->FoundryLocation() + CoreArena->GetActorRightVector() * (PodSide++ % 2 == 0 ? 1200.f : -1200.f);
	}
	// 11-02 有水的图：场地中央可能是水，放到离中央最近的落脚点（不然舱子贴着水面，AI 去拿就下水了）
	if (const UMechWater* Water = GetWorld()->GetSubsystem<UMechWater>(); Water && Water->IsGridReady())
	{
		FVector Land;
		if (Water->FindLanding(FVector(At.X, At.Y, 1.e6f), 2.e6f, 60000.f, Land))
		{
			At = Land;
		}
	}
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MechPluginPod), false);
	for (AMechCharacter* M : Mechs)
	{
		Query.AddIgnoredActor(M);
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 3000.f), At - FVector(0.f, 0.f, 8000.f), ECC_Visibility, Query))
	{
		At = Hit.ImpactPoint;
	}
	At.Z += 250.f;
	const FTransform Where(At);
	AMechPluginPod* Pod = GetWorld()->SpawnActorDeferred<AMechPluginPod>(AMechPluginPod::StaticClass(), Where, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pod)
	{
		return;
	}
	Pod->Plugin = Ids[FMath::RandRange(0, Ids.Num() - 1)];
	Pod->FinishSpawning(Where);
	Pods.Add(Pod);
	UE_LOG(LogTemp, Display, TEXT("[plugin] 刷出插件舱 %s @ %s"), *Pod->Plugin.ToString(), *At.ToCompactString());
}

// ---------------------------------------------------------------- 对战规则 / 死斗
namespace
{
	/** 机体胶囊半高（和 TeamSpawn 一致：96 × 1.35） */
	constexpr float MechHalfHeight = 96.f * 1.35f;

	/** 在 P 附近找地面（从 P 上方 5 米往下 25 米，只看静态场景）：地面要够平，起点不能在墙里 */
	bool FindGround(const UWorld* World, const FVector& P, FVector& Out)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MechDMGround), false);
		if (!World->LineTraceSingleByObjectType(Hit, P + FVector(0.f, 0.f, 500.f), P - FVector(0.f, 0.f, 2500.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params) ||
			Hit.bStartPenetrating || Hit.ImpactNormal.Z < 0.7f)
		{
			return false;
		}
		Out = Hit.ImpactPoint;
		return true;
	}
}

void AMechGameMode::ApplyRules()
{
	MechRules::Sanitize(Rules);
	AMechGameState* GS = GetMechGameState();
	if (!bCoreMode && !bRangeMode)
	{
		// 夺芯的局数、限时在 SetupCoreMode 里定，这里不动
		if (Rules.IsDeathmatch())
		{
			RoundsToWin = 1;
			RoundTimeLimit = Rules.TimeLimit;
		}
		else
		{
			RoundsToWin = Rules.RoundsToWin;
			RoundTimeLimit = ElimTimeLimit;
		}
	}
	if (GS)
	{
		GS->Rules = Rules;
		GS->RoundsToWin = RoundsToWin;
	}
	UE_LOG(LogTemp, Display, TEXT("[rules] %s（先赢 %d 局，每局限时 %.0f 秒）"), *MechRules::Describe(Rules, bCoreMode), RoundsToWin, RoundTimeLimit);
}

FString AMechGameMode::SlotName(int32 Team, int32 Index) const
{
	if (IsSoloDM())
	{
		return FString::Printf(TEXT("机体%d"), Index * 2 + Team + 1);
	}
	return FString::Printf(TEXT("%s%d"), Team == 0 ? TEXT("蓝") : TEXT("红"), Index + 1);
}

void AMechGameMode::DMOnDied(AMechCharacter* Victim, AMechCharacter* Killer)
{
	AMechGameState* GS = GetMechGameState();
	if (!GS || GS->Phase != EMechRoundPhase::Fight || !Victim)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	// 点数就是击毁数：击毁一台 +1（个人算自己，团队算队伍），不算助攻；环境 / 掉出地图（Killer 为空）谁都不加分、也不扣分
	int32 KillerScore = 0;
	if (Killer)
	{
		if (IsSoloDM())
		{
			KillerScore = Stats.FindOrAdd(Killer->DisplayName).Kills;  // OnMechDied 里已经 +1
		}
		else
		{
			KillerScore = ++GS->TeamScore[FMath::Clamp(Killer->GetTeam(), 0, 1)];
		}
	}
	PublishStats();  // 计分板、个人比分马上更新（平时每秒一次）
	const FString Score = IsSoloDM() ? (Killer ? FString::Printf(TEXT("%s %d 击毁"), *Killer->DisplayName, KillerScore) : FString(TEXT("不计分")))
		: FString::Printf(TEXT("蓝 %d : %d 红"), GS->TeamScore[0], GS->TeamScore[1]);
	const FString By = Killer ? Killer->DisplayName : FString(TEXT("环境"));
	UE_LOG(LogTemp, Display, TEXT("[dm] %.1f 秒：%s 被 %s 击毁，%s（目标 %d）"), Now - FightStartTime, *Victim->DisplayName, *By, *Score, Rules.KillTarget());
	if (Killer && KillerScore >= Rules.KillTarget())
	{
		EndDMMatch(IsSoloDM() ? Killer->GetTeam() : FMath::Clamp(Killer->GetTeam(), 0, 1), IsSoloDM() ? Killer->DisplayName : FString());
		return;
	}
	// 排重生：阵亡视角照旧（停在阵亡点 / 观战队友），到点在离敌人远的点换一台新的（配装、涂装照旧，插件效果清空）
	FDMDown& D = DMDowned.AddDefaulted_GetRef();
	D.Mech = Victim;
	D.Controller = Victim->GetController();
	D.RespawnAt = Now + MechRules::RespawnDelay;
	FMechRespawn& R = GS->Respawns.AddDefaulted_GetRef();
	R.Mech = Victim;
	R.DiedAt = Now;
	R.RespawnAt = D.RespawnAt;
}

void AMechGameMode::DMTick(float DeltaSeconds)
{
	AMechGameState* GS = GetMechGameState();
	if (!GS)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	// 掉出地图（从场地边缘掉下去、穿过地面的缝）：算一次阵亡，谁都不加分，照常重生
	const TArray<TObjectPtr<AMechCharacter>> Snapshot = Mechs;
	for (AMechCharacter* Mech : Snapshot)
	{
		if (Mech && !Mech->GetHealth()->IsDead() && Mech->GetActorLocation().Z < ArenaCenter.Z - 3000.f)
		{
			UE_LOG(LogTemp, Warning, TEXT("[dm] %s 掉出地图：位置 %s 速度 %s，算阵亡"), *Mech->DisplayName, *Mech->GetActorLocation().ToString(), *Mech->GetVelocity().ToString());
			if (Mech->GetEffects())
			{
				Mech->GetEffects()->RemoveEffect(MechRules::SpawnGuardId());
			}
			Mech->GetCharacterMovement()->StopMovementImmediately();
			Mech->GetCharacterMovement()->DisableMovement();
			Mech->GetHealth()->ApplyHit(1.e7f, 0.f, nullptr, Mech->GetActorLocation(), 0.f);
			if (GS->Phase != EMechRoundPhase::Fight)
			{
				return;
			}
		}
	}
	// 到点重生（重生保护 SpawnGuardTime 秒，自己打中人就提前结束，见 RecordHit）
	for (int32 i = DMDowned.Num() - 1; i >= 0; --i)
	{
		if (!DMDowned[i].Mech.IsValid())
		{
			DMDowned.RemoveAt(i);
			continue;
		}
		if (Now < DMDowned[i].RespawnAt)
		{
			continue;
		}
		const FDMDown D = DMDowned[i];
		DMDowned.RemoveAt(i);
		AMechCharacter* Old = D.Mech.Get();
		GS->Respawns.RemoveAll([Old](const FMechRespawn& R) { return R.Mech == Old; });
		const FTransform At = PickDMSpawn(Old);
		DMRecentSpawns.RemoveAll([Now](const TPair<FVector, float>& S) { return Now - S.Value > 6.f; });
		DMRecentSpawns.Add({At.GetLocation() - FVector(0.f, 0.f, MechHalfHeight + 15.f), Now});
		if (AMechCharacter* Fresh = RespawnMechAt(Old, D.Controller.Get(), At))
		{
			Fresh->GetEffects()->AddEffect(MechRules::SpawnGuardId(), MechRules::SpawnGuardTime);
			float Nearest = 1.e9f;
			for (const AMechCharacter* M : Mechs)
			{
				if (M && M != Fresh && !M->GetHealth()->IsDead() && M->GetTeam() != Fresh->GetTeam())
				{
					Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist(M->GetActorLocation(), Fresh->GetActorLocation())));
				}
			}
			UE_LOG(LogTemp, Display, TEXT("[dm]   离最近的活着的敌人 %.0f 米，保护 %.0f 秒"), Nearest / 100.f, MechRules::SpawnGuardTime);
		}
	}
	GS->Respawns.RemoveAll([](const FMechRespawn& R) { return !IsValid(R.Mech.Get()); });
	DMLearnClock -= DeltaSeconds;
	if (DMLearnClock <= 0.f)
	{
		DMLearnClock = 2.f;
		LearnDMSpawns();
	}
	// 时间到：击毁多的一方 / 一人赢，同分平局
	if (GS->GetPhaseRemaining() <= 0.f)
	{
		if (IsSoloDM())
		{
			FString Best;
			int32 BestKills = -1;
			int32 BestTeam = -1;
			bool bTie = false;
			for (const auto& Pair : Stats)
			{
				if (Pair.Value.Kills > BestKills)
				{
					Best = Pair.Key;
					BestKills = Pair.Value.Kills;
					BestTeam = Pair.Value.Team;
					bTie = false;
				}
				else if (Pair.Value.Kills == BestKills)
				{
					bTie = true;
				}
			}
			EndDMMatch(bTie ? -1 : BestTeam, bTie ? FString() : Best);
		}
		else
		{
			EndDMMatch(GS->TeamScore[0] == GS->TeamScore[1] ? -1 : (GS->TeamScore[0] > GS->TeamScore[1] ? 0 : 1), FString());
		}
	}
}

void AMechGameMode::EndDMMatch(int32 WinnerTeam, const FString& Winner)
{
	AMechGameState* GS = GetMechGameState();
	if (!GS || GS->Phase != EMechRoundPhase::Fight)
	{
		return;
	}
	const bool bTimeUp = GS->GetPhaseRemaining() <= 0.f;
	GS->RoundWinner = WinnerTeam;
	GS->MatchWinner = WinnerTeam;
	GS->WinnerName = Winner;
	GS->Phase = EMechRoundPhase::MatchOver;
	GS->PhaseEndTime = GetWorld()->GetTimeSeconds() + MatchOverTime;
	DMDowned.Reset();
	GS->Respawns.Reset();
	ClearPluginPods();
	NextPodTime = -1.f;
	FString What;
	if (IsSoloDM())
	{
		What = Winner.IsEmpty() ? FString(TEXT("平局")) : FString::Printf(TEXT("%s 胜（%d 击毁）"), *Winner, Stats.FindRef(Winner).Kills);
	}
	else
	{
		What = WinnerTeam < 0 ? FString(TEXT("平局")) : FString(WinnerTeam == 0 ? TEXT("蓝队胜") : TEXT("红队胜"));
	}
	UE_LOG(LogTemp, Display, TEXT("[battle] 比赛结束：%s，比分 蓝 %d : %d 红"), *What, GS->TeamScore[0], GS->TeamScore[1]);
	UE_LOG(LogTemp, Display, TEXT("[dm] 结束（%s）：%s，%s，开打后 %.0f 秒，重生点候选 %d + 记下的 %d，重生保护挡下命中 %d 次、开火提前结束 %d 次"),
		bTimeUp ? TEXT("时间到") : TEXT("到达目标"), *MechRules::Describe(Rules, false), *What, GetWorld()->GetTimeSeconds() - FightStartTime, DMSpawns.Num(),
		DMLearned.Num(), DMGuardBlocks, DMGuardBreaks);
	LogStats();
	PublishStats();
	if (bAutoBattle)
	{
		PendingEventShot = 1.f;
	}
}

bool AMechGameMode::IsClearSpot(const FVector& Feet) const
{
	// 胶囊略缩一点、抬高 30 厘米（坡地不算卡住），只查静态场景（机体之间的距离另外判）
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechDMClear), false);
	return !GetWorld()->OverlapAnyTestByObjectType(Feet + FVector(0.f, 0.f, MechHalfHeight + 30.f), FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
		FCollisionShape::MakeCapsule(50.f, 120.f), Params);
}

FTransform AMechGameMode::SpawnAtFeet(const FVector& Feet) const
{
	// 朝场地中间
	const FVector To = ArenaCenter - Feet;
	const float Yaw = To.SizeSquared2D() > 1.f ? To.Rotation().Yaw : 0.f;
	return FTransform(FRotator(0.f, Yaw, 0.f), Feet + FVector(0.f, 0.f, MechHalfHeight + 15.f));
}

void AMechGameMode::BuildDMSpawns()
{
	DMSpawns.Reset();
	bDMSpawnsFromMap = false;
	UWorld* World = GetWorld();
	// 1. 地图里摆的：带「DMSpawn」标签的 actor（TargetPoint / PlayerStart 都行），落到脚下地面
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(TEXT("DMSpawn")))
		{
			FVector Feet = It->GetActorLocation();
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MechDMSpawn), false);
			if (World->LineTraceSingleByObjectType(Hit, Feet + FVector(0.f, 0.f, 200.f), Feet - FVector(0.f, 0.f, 3000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{
				Feet = Hit.ImpactPoint;
			}
			DMSpawns.Add(Feet);
		}
	}
	if (DMSpawns.Num() >= 4)
	{
		bDMSpawnsFromMap = true;
		return;
	}
	DMSpawns.Reset();
	// 2. 两队出生区（每队 4 个位置）：TeamSpawn 从 30 米高往下找地面，排开的第 3、4 台在街道图上会落到高架 / 楼顶上 →
	//    从出生区中心的高度往下重新找地面，离出生区地面高差 3.5 米以上的（车顶、屋顶）不要
	for (int32 Team = 0; Team < 2; ++Team)
	{
		FVector BaseFeet;
		if (!FindGround(World, SpawnBase[Team], BaseFeet))
		{
			BaseFeet = SpawnBase[Team];
		}
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector P = TeamSpawn(Team, i).GetLocation();
			FVector Feet;
			if (FindGround(World, FVector(P.X, P.Y, BaseFeet.Z), Feet) && FMath::Abs(Feet.Z - BaseFeet.Z) < 350.f && IsClearSpot(Feet))
			{
				DMSpawns.Add(Feet);
			}
		}
	}
	// 3. 两队出生区之间自动找空地：沿连线取 5 处，每处往两边偏 0 / 12 / 24 / 36 米；地面要平、胶囊放得下、
	//    和连线上那一点之间没有墙（同一片开阔地，避开封闭的房子里、高架上面）
	const FVector A = SpawnBase[0];
	const FVector B = SpawnBase[1];
	const FVector Along = (B - A).GetSafeNormal2D();
	const FVector Side(-Along.Y, Along.X, 0.f);
	for (const float F : {0.2f, 0.35f, 0.5f, 0.65f, 0.8f})
	{
		const FVector OnLine = FMath::Lerp(A, B, F);
		FVector LineFeet;
		if (!FindGround(World, OnLine, LineFeet))
		{
			continue;
		}
		for (const float Off : {0.f, 1200.f, -1200.f, 2400.f, -2400.f, 3600.f, -3600.f})
		{
			const FVector P = OnLine + Side * Off;
			if (FMath::Abs(P.X - ArenaCenter.X) > ArenaExtent.X || FMath::Abs(P.Y - ArenaCenter.Y) > ArenaExtent.Y)
			{
				continue;
			}
			FVector Feet;
			if (!FindGround(World, P, Feet) || FMath::Abs(Feet.Z - LineFeet.Z) > 350.f || !IsClearSpot(Feet))
			{
				continue;
			}
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MechDMReach), false);
			if (Off != 0.f && World->LineTraceSingleByObjectType(Hit, Feet + FVector(0.f, 0.f, 150.f), LineFeet + FVector(0.f, 0.f, 150.f),
				FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{
				continue;
			}
			if (DMSpawns.ContainsByPredicate([&Feet](const FVector& S) { return FVector::Dist(S, Feet) < 900.f; }))
			{
				continue;
			}
			DMSpawns.Add(Feet);
		}
	}
}

void AMechGameMode::LearnDMSpawns()
{
	// 没摆 DMSpawn 的地图：机体在地面上走过、离已有候选 15 米以上的点记下来（最多 64 个），之后重生也能用（不在屋顶上、不出场地）
	if (bDMSpawnsFromMap || DMLearned.Num() >= 64)
	{
		return;
	}
	for (const AMechCharacter* M : Mechs)
	{
		if (!M || M->GetHealth()->IsDead() || M->IsTrainingDummy() || !M->GetCharacterMovement()->IsMovingOnGround())
		{
			continue;
		}
		const FVector Feet = M->GetActorLocation() - FVector(0.f, 0.f, M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		if (FMath::Abs(Feet.X - ArenaCenter.X) > ArenaExtent.X * 0.95f || FMath::Abs(Feet.Y - ArenaCenter.Y) > ArenaExtent.Y * 0.95f ||
			Feet.Z > ArenaCenter.Z + 1200.f || Feet.Z < ArenaCenter.Z - 1500.f)
		{
			continue;
		}
		auto Near = [&Feet](const FVector& S) { return FVector::Dist(S, Feet) < 1500.f; };
		if (DMSpawns.ContainsByPredicate(Near) || DMLearned.ContainsByPredicate(Near) || !IsClearSpot(Feet))
		{
			continue;
		}
		DMLearned.Add(Feet);
		if (DMLearned.Num() >= 64)
		{
			break;
		}
	}
}

FTransform AMechGameMode::PickDMSpawn(const AMechCharacter* For) const
{
	const int32 Team = For ? For->GetTeam() : 0;
	TArray<FVector> Cands = DMSpawns;
	Cands.Append(DMLearned);
	struct FScored
	{
		FVector Feet;
		float Score;
	};
	TArray<FScored> Scored;
	const float Now = GetWorld()->GetTimeSeconds();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechDMSight), false);
	for (const FVector& C : Cands)
	{
		float MinEnemy = 1.e9f;
		float MinAlly = 1.e9f;
		float MinAny = 1.e9f;
		bool bSeen = false;
		for (const AMechCharacter* M : Mechs)
		{
			if (!M || M == For || M->GetHealth()->IsDead() || M->IsTrainingDummy())
			{
				continue;
			}
			const float D = FVector::Dist(C, M->GetActorLocation());
			MinAny = FMath::Min(MinAny, D);
			if (M->GetTeam() == Team)
			{
				MinAlly = FMath::Min(MinAlly, D);
				continue;
			}
			MinEnemy = FMath::Min(MinEnemy, D);
			FHitResult Hit;
			if (!bSeen && D < 9000.f && !GetWorld()->LineTraceSingleByObjectType(Hit, C + FVector(0.f, 0.f, 180.f), M->GetActorLocation() + FVector(0.f, 0.f, 80.f),
				FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{
				bSeen = true;  // 有敌人看得见这一点
			}
		}
		if (MinAny < 500.f)
		{
			continue;  // 别叠在别的机体身上
		}
		// 离最近的敌人越远越好（80 米封顶），被看得见的打对折；团队死斗离队友 40 米内加一点（和队友一起重生）
		float Score = FMath::Min(MinEnemy, 8000.f) * (bSeen ? 0.45f : 1.f);
		if (!IsSoloDM() && MinAlly < 4000.f)
		{
			Score += 800.f;
		}
		// 6 秒内刚有机体在 15 米内重生过：再往后排（免得两台前后脚在同一处冒出来，刚起身就对上）
		if (DMRecentSpawns.ContainsByPredicate([&C, Now](const TPair<FVector, float>& S) { return Now - S.Value < 6.f && FVector::Dist(S.Key, C) < 1500.f; }))
		{
			Score *= 0.3f;
		}
		Scored.Add({C, Score + FMath::FRand() * 300.f});
	}
	Scored.Sort([](const FScored& X, const FScored& Y) { return X.Score > Y.Score; });
	TArray<FVector> Top;
	for (const FScored& S : Scored)
	{
		if (IsClearSpot(S.Feet))
		{
			Top.Add(S.Feet);
			if (Top.Num() >= 3)
			{
				break;
			}
		}
	}
	if (Top.Num())
	{
		return SpawnAtFeet(Top[FMath::RandRange(0, Top.Num() - 1)]);
	}
	return TeamSpawn(FMath::Clamp(Team, 0, 1), FMath::RandRange(0, FMath::Max(0, TeamCount[FMath::Clamp(Team, 0, 1)] - 1)));
}

TArray<FTransform> AMechGameMode::PickSpreadStarts(int32 Count) const
{
	// 最远点采样：第一台随机，之后每台挑离已选的点最远的候选
	TArray<FVector> Cands;
	for (const FVector& C : DMSpawns)
	{
		if (IsClearSpot(C))
		{
			Cands.Add(C);
		}
	}
	TArray<FTransform> Out;
	TArray<FVector> Chosen;
	while (Out.Num() < Count)
	{
		if (Cands.IsEmpty())
		{
			// 候选不够：按两队出生区轮流排
			const int32 k = Out.Num();
			Out.Add(TeamSpawn(k % 2, (k / 2) % 4));
			continue;
		}
		int32 Pick = 0;
		if (Chosen.IsEmpty())
		{
			Pick = FMath::RandRange(0, Cands.Num() - 1);
		}
		else
		{
			float Best = -1.f;
			for (int32 c = 0; c < Cands.Num(); ++c)
			{
				float Closest = 1.e9f;
				for (const FVector& S : Chosen)
				{
					Closest = FMath::Min(Closest, static_cast<float>(FVector::Dist(S, Cands[c])));
				}
				if (Closest > Best)
				{
					Best = Closest;
					Pick = c;
				}
			}
		}
		Chosen.Add(Cands[Pick]);
		Out.Add(SpawnAtFeet(Cands[Pick]));
		Cands.RemoveAtSwap(Pick);
	}
	return Out;
}

// ================================================================ 测试：先加入一个没开房的地址，停在连接中，再开房（-MechHostAfterJoinTest）
// 复现 10-29 / 10-30 打包版「开房闪退」：修之前第二步直接致命退出（Couldn't spawn player）；修好后能进自己开的房，日志 [hostjoin] 通过，自动退出
namespace MechHostAfterJoinTest
{
	double Start = 0.0;
	int32 Step = 0;

	bool Tick(float)
	{
		UWorld* World = nullptr;
		for (const FWorldContext& WC : GEngine->GetWorldContexts())
		{
			if (WC.WorldType == EWorldType::Game && WC.World())
			{
				World = WC.World();
			}
		}
		AMechPlayerController* PC = World ? Cast<AMechPlayerController>(World->GetFirstPlayerController()) : nullptr;
		const double Now = FPlatformTime::Seconds();
		if (Start == 0.0)
		{
			Start = Now;
		}
		if (Now - Start > 90.0)
		{
			UE_LOG(LogTemp, Display, TEXT("[hostjoin] 失败：90 秒内没进到自己开的房（第 %d 步）"), Step);
			FPlatformMisc::RequestExit(false);
			return false;
		}
		if (!World || !PC)
		{
			return true;
		}
		const bool bHosted = World->URL.HasOption(TEXT("listen")) && World->URL.HasOption(TEXT("Room"));  // 11-01 开房 = 进房间图
		if (Step == 0 && !bHosted && Now - Start > 4.0)
		{
			UE_LOG(LogTemp, Display, TEXT("[hostjoin] 第 1 步：加入 127.0.0.1:7790（没人开房，会一直停在连接中）"));
			PC->MenuJoin(TEXT("127.0.0.1:7790"));
			Step = 1;
			Start = Now - 4.0;
		}
		else if (Step == 1 && Now - Start > 7.0)
		{
			const FWorldContext* WC = GEngine->GetWorldContextFromWorld(World);
			UE_LOG(LogTemp, Display, TEXT("[hostjoin] 第 2 步：挂着的连接 %s，现在开房"), WC && WC->PendingNetGame ? TEXT("还在") : TEXT("没有了"));
			PC->MenuOpenRoom();
			Step = 2;
		}
		else if (Step == 2 && bHosted && World->GetAuthGameMode() && World->GetNetMode() == NM_ListenServer)
		{
			UE_LOG(LogTemp, Display, TEXT("[hostjoin] 通过：进了自己开的房（%s），游戏模式 %s，本机玩家 %s"),
				*World->GetMapName(), *World->GetAuthGameMode()->GetClass()->GetName(), *PC->GetName());
			Step = 3;
			Start = Now;
		}
		else if (Step == 3 && Now - Start > 3.0)
		{
			FPlatformMisc::RequestExit(false);
			return false;
		}
		return true;
	}

	// 全局对象构造里不能读命令行（打包版 main 之前执行会崩），引擎初始化完再注册
	FDelayedAutoRegisterHelper Register(EDelayedRegisterRunPhase::EndOfEngineInit, []
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("MechHostAfterJoinTest")))
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.25f);
		}
	});
}

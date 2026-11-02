// -MechTacticalTest（测试场 "/Game/Maps/L_Range?Range"）：肩部的紧急脱离、弹射升空、震荡波、地雷（10-31 的战术装备，11-01 并入肩部）逐项和木桩过一遍
// （AMechPlayerController::TacticalTestTick，声明在 MechGameMode.h）。都是把肩部配装换成那一件，走按 E 的路（MissilePressedForTest）。
//  1 紧急脱离：离木桩 15 米面朝它，不按方向按 E → 应往后冲出约 22 米（检查 > 15 米）
//  2 弹射升空：原地按 E → 应升高约 16 米（检查 > 10 米；测试场头顶是空的）
//  3 震荡波：离木桩 6 米按 E → 0.3 秒后木桩掉装甲、被震开（检查掉了装甲、挪了 > 3 米）
//  4 地雷：离木桩 12 米瞄它脚下按 E → 落地、1 秒后待命、木桩在圈里触发、0.35 秒后炸（检查木桩掉了装甲）
//  5 地雷被打掉：往木桩旁边 9 米扔一颗（不会触发），待命后先用自己当伤害来源打它（自己人打不掉），再用木桩当来源打（应碎掉）
//  另外：紧急脱离冲完能量清零（用之前满能量、冲完 0 且过热）；四件 HUD 读的肩部冷却对得上；最后查配装五类、肩部 11 件都能禁、E 键（共 8 项）
// 截图在 Saved/Screenshots/Tactical，日志 [tacticaltest]（最后一行汇总通过几项），约 30 秒退出。
// 作为联机客户端跑（房主 "/Game/Maps/L_Range?Range?listen" -MechNoLobby，客户端 127.0.0.1 -MechTacticalTest -MechCorrectionLog）：
// 原地两次紧急脱离、一次弹射升空，记每次之后 2 秒内服务器纠正了几次位置 [tacticalnet]（预测和服务器对得上应接近 0）。

#include "MechGameMode.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MechCharacter.h"
#include "MechDraft.h"
#include "MechHealthComponent.h"
#include "MechLoadout.h"
#include "MechMeshComponent.h"
#include "MechMine.h"
#include "MechMovementComponent.h"
#include "MechSettings.h"
#include "MechTacticalComponent.h"

void AMechPlayerController::TacticalTestTick(float DeltaTime)
{
	AMechCharacter* Me = Cast<AMechCharacter>(GetPawn());
	if (!Me || !Me->GetTactical())
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
		return;  // 时间轴从机体和木桩都生成了才开始走
	}
	const float T0 = TacticalTestClock;
	TacticalTestClock += DeltaTime;
	const float T = TacticalTestClock;
	auto At = [&](float S) { return T0 < S && T >= S; };
	UMechTacticalComponent* Tac = Me->GetTactical();
	const UMechMovementComponent* Move = Me->GetMechMovement();
	auto Equip = [&](EMechShoulder Type) {
		// 换装会把冷却清零（OnLoadoutChanged），每段开头换一次
		FMechLoadout L = Me->GetLoadout();
		L.Shoulder = Type;
		ServerApplyLoadout(L);
	};
	static FVector From = FVector::ZeroVector;
	static FVector DummyFrom = FVector::ZeroVector;
	static float DummyAP0 = 0.f;
	static int32 Corrections0 = 0;

	if (GetNetMode() == NM_Client)
	{
		// 联机客户端（不能自己瞬移）：原地按，看服务器纠正了几次（预测的根运动和服务器的对得上就几乎不纠正）
		auto Report = [&](const TCHAR* Tag) {
			UE_LOG(LogTemp, Display, TEXT("[tacticalnet] %s：走了 %.0f 厘米（升高 %.0f），这段纠正 %d 次（累计 %d），冷却 %.1f"), Tag, FVector::Dist2D(From, Me->GetActorLocation()),
				Me->GetActorLocation().Z - From.Z, Move->CorrectionCount - Corrections0, Move->CorrectionCount, Tac->GetCooldown());
		};
		auto Press = [&]() {
			Corrections0 = Move->CorrectionCount;
			From = Me->GetActorLocation();
			Me->MissilePressedForTest();
		};
		if (At(4.f))
		{
			Equip(EMechShoulder::Burst);
		}
		if (At(6.f))
		{
			Press();
		}
		if (At(8.f))
		{
			Report(TEXT("紧急脱离（不按方向 = 往后）"));
		}
		if (At(9.f))
		{
			Equip(EMechShoulder::Launch);
		}
		if (At(11.f))
		{
			Press();
		}
		if (At(14.f))
		{
			Report(TEXT("弹射升空（落地后）"));
		}
		if (At(15.f))
		{
			Equip(EMechShoulder::Burst);
		}
		if (At(17.f))
		{
			Press();
		}
		if (At(19.f))
		{
			Report(TEXT("紧急脱离 第二次"));
		}
		if (At(21.f))
		{
			ConsoleCommand(TEXT("quit"));
		}
		return;
	}

	auto Shot = [](const TCHAR* Tag) {
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Tactical") / FString::Printf(TEXT("%s.png"), Tag), true, false);
	};
	static int32 TacPassed = 0;
	static int32 TacFailed = 0;
	auto Verdict = [&](const TCHAR* Name, bool bOk, const FString& Detail) {
		if (bOk)
		{
			++TacPassed;
		}
		else
		{
			++TacFailed;
		}
		UE_LOG(LogTemp, Display, TEXT("[tacticaltest] %s %s：%s"), bOk ? TEXT("通过") : TEXT("没通过"), Name, *Detail);
	};
	const FVector Away = Dummy->GetActorForwardVector().GetSafeNormal2D();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Away).GetSafeNormal();
	const float DummyHalf = Dummy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector DummyFeet = Dummy->GetActorLocation() - FVector(0.f, 0.f, DummyHalf);
	auto StandAt = [&](float Dist) {
		Me->SetActorLocation(Dummy->GetActorLocation() + Away * Dist + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
		Me->SetActorRotation(FRotator(0.f, (-Away).Rotation().Yaw, 0.f));
	};
	auto AimAt = [&](const FVector& P) {
		if (PlayerCameraManager)
		{
			SetControlRotation((P - PlayerCameraManager->GetCameraLocation()).Rotation());
		}
	};

	// 1 紧急脱离：面朝木桩，不按方向 → 往后
	if (At(2.f))
	{
		Equip(EMechShoulder::Burst);
		StandAt(1500.f);
	}
	if (T > 2.f && T < 4.5f)
	{
		AimAt(Dummy->GetActorLocation());
	}
	if (At(3.f))
	{
		From = Me->GetActorLocation();
		Shot(TEXT("01_脱离前"));
	}
	static float EnergyBefore = 0.f;
	if (At(3.2f))
	{
		EnergyBefore = Move->Energy;
		Me->MissilePressedForTest();
	}
	if (At(3.35f))
	{
		Shot(TEXT("02_脱离中"));
	}
	if (At(3.9f))
	{
		Shot(TEXT("03_脱离后"));
		const float Moved = FVector::Dist2D(From, Me->GetActorLocation());
		const float Back = FVector::DotProduct(Me->GetActorLocation() - From, Away);
		Verdict(TEXT("紧急脱离"), Moved > 1500.f && Back > 1000.f && Tac->GetCooldown() > 0.f,
			FString::Printf(TEXT("水平走了 %.0f 厘米（往后 %.0f），冷却 %.1f 秒，地面 %d"), Moved, Back, Tac->GetCooldown(), Move->IsFalling() ? 0 : 1));
		// 10-31 用户定：冲完能量清零（冲刺约 0.38 秒在 3.58 秒结束，之后要等 0.6 秒才开始回能，所以 3.9 秒时应该还是 0、过热）
		Verdict(TEXT("紧急脱离后能量清零"), EnergyBefore > 50.f && Move->Energy < 1.f && Move->bOverheated,
			FString::Printf(TEXT("用之前能量 %.0f，冲完 %.1f，过热 %d"), EnergyBefore, Move->Energy, Move->bOverheated ? 1 : 0));
		// HUD 和信息区读的是肩部冷却（GetShoulderReady / GetShoulderTimer）：要和组件自己的冷却对得上；肩上要有一架无人机（借现成模型）
		const UStaticMeshComponent* Drone = Me->GetMechMesh() ? Cast<UStaticMeshComponent>(Me->GetMechMesh()->Props[2].Get()) : nullptr;
		Verdict(TEXT("肩部冷却和无人机"), FMath::IsNearlyEqual(Me->GetShoulderTimer(), Tac->GetCooldown()) && Me->GetShoulderReady() < 1.f && Drone && Drone->GetStaticMesh() && Drone->IsVisible(),
			FString::Printf(TEXT("肩部冷却 %.1f，组件冷却 %.1f，就绪 %.2f，机身 %s"), Me->GetShoulderTimer(), Tac->GetCooldown(), Me->GetShoulderReady(),
				Drone && Drone->GetStaticMesh() ? *Drone->GetStaticMesh()->GetName() : TEXT("没有")));
	}

	// 2 弹射升空
	if (At(6.f))
	{
		Equip(EMechShoulder::Launch);
		StandAt(1500.f);
	}
	if (At(7.f))
	{
		From = Me->GetActorLocation();
		Me->MissilePressedForTest();
	}
	if (At(7.25f))
	{
		Shot(TEXT("04_弹射中"));
	}
	if (At(7.55f))
	{
		Shot(TEXT("05_弹射到顶"));
		const float Rise = Me->GetActorLocation().Z - From.Z;
		Verdict(TEXT("弹射升空"), Rise > 1000.f, FString::Printf(TEXT("升高 %.0f 厘米，空中 %d"), Rise, Move->IsFalling() ? 1 : 0));
	}

	// 3 震荡波：离木桩 6 米
	if (At(11.f))
	{
		Equip(EMechShoulder::Shockwave);
		StandAt(600.f);
		// 测试场木桩没控制器、移动模式是「无」，击退推不动它（和钩爪测试一样先打开，免得把「木桩不会动」误判成震不开）
		UMechMovementComponent* DM = Dummy->GetMechMovement();
		DM->bRunPhysicsWithNoController = true;
		if (DM->MovementMode == MOVE_None)
		{
			DM->SetDefaultMovementMode();
		}
	}
	if (T > 11.f && T < 13.5f)
	{
		AimAt(Dummy->GetActorLocation());
	}
	if (At(12.f))
	{
		DummyFrom = Dummy->GetActorLocation();
		DummyAP0 = Dummy->GetHealth()->GetAP();
		Me->MissilePressedForTest();
	}
	if (At(12.15f))
	{
		Shot(TEXT("06_震荡波蓄力"));
	}
	if (At(12.36f))
	{
		Shot(TEXT("07_震荡波炸开"));
	}
	if (At(13.f))
	{
		Shot(TEXT("08_震开"));
		const float Pushed = FVector::Dist2D(DummyFrom, Dummy->GetActorLocation());
		const float Lost = DummyAP0 - Dummy->GetHealth()->GetAP();
		Verdict(TEXT("震荡波"), Lost > 100.f && Pushed > 300.f && Tac->DebugShockHits > 0,
			FString::Printf(TEXT("木桩掉装甲 %.0f、被推开 %.0f 厘米，震到 %d 台"), Lost, Pushed, Tac->DebugShockHits));
	}

	// 4 地雷：离木桩 12 米，瞄它脚下扔（等木桩停手 4 秒回满、回原位）
	if (At(19.f))
	{
		Equip(EMechShoulder::Mine);
		StandAt(1200.f);
	}
	if (T > 19.f && T < 20.5f)
	{
		AimAt(DummyFeet);
	}
	if (At(20.f))
	{
		DummyAP0 = Dummy->GetHealth()->GetAP();
		Me->MissilePressedForTest();
	}
	if (At(20.3f))
	{
		Shot(TEXT("09_地雷飞出"));
	}
	if (At(21.4f))
	{
		Shot(TEXT("10_地雷待命前"));
	}
	if (At(21.9f))
	{
		Shot(TEXT("11_引爆倒计时"));
	}
	if (At(22.3f))
	{
		Shot(TEXT("12_地雷爆炸"));
	}
	if (At(23.f))
	{
		const float Lost = DummyAP0 - Dummy->GetHealth()->GetAP();
		Verdict(TEXT("地雷炸木桩"), Lost > 100.f && Tac->DebugMines > 0, FString::Printf(TEXT("木桩掉装甲 %.0f，扔出 %d 颗"), Lost, Tac->DebugMines));
	}

	// 5 地雷被打掉：扔在木桩旁边 9 米（不触发），待命后先自己人打、再敌人打
	if (At(24.f))
	{
		Equip(EMechShoulder::Mine);
	}
	if (At(25.f))
	{
		Tac->Pressed(FVector::ZeroVector, DummyFeet + Side * 900.f);
	}
	if (At(27.2f))
	{
		Shot(TEXT("13_地雷待命"));
	}
	if (At(27.5f))  // 飞约 0.85 秒 + 落地 1 秒才待命
	{
		AMechMine* Mine = nullptr;
		for (TActorIterator<AMechMine> It(GetWorld()); It; ++It)
		{
			if (It->GetOwnerMech() == Me && It->IsActive() && It->IsArmed())
			{
				Mine = *It;
				break;
			}
		}
		if (!Mine)
		{
			Verdict(TEXT("地雷被打掉"), false, TEXT("没找到待命中的地雷（落地失败或被触发了）"));
		}
		else
		{
			UGameplayStatics::ApplyDamage(Mine, 200.f, nullptr, Me, TSubclassOf<UDamageType>());
			const bool bAfterOwn = Mine->IsActive();
			UGameplayStatics::ApplyDamage(Mine, 200.f, nullptr, Dummy, TSubclassOf<UDamageType>());
			const bool bAfterEnemy = Mine->IsActive();
			Verdict(TEXT("地雷被打掉"), bAfterOwn && !bAfterEnemy, FString::Printf(TEXT("自己人打后还在 %d，敌人打后还在 %d"), bAfterOwn ? 1 : 0, bAfterEnemy ? 1 : 0));
		}
	}
	if (At(27.8f))
	{
		Shot(TEXT("14_地雷碎掉"));
	}
	if (At(29.5f))
	{
		// 配装 / 选配的几处（11-01 用户定：五类、这四件并进肩部一共 11 件、按 E、禁用阶段能禁）：不用画面，数字能证明
		Verdict(TEXT("配装五类、肩部 11 件、能禁、按 E"), MechLoadout::NumCategories == 5 && MechLoadout::NumItems(MechLoadout::Shoulder) == 11 && MechDraft::IsBannable(MechLoadout::Shoulder) &&
			MechKeys::Default(EMechKey::Shoulder) == EKeys::E,
			FString::Printf(TEXT("%d 类，肩部 %d 件（第 8 件 %s），能禁 %d，默认键 %s，现在的键 %s"), static_cast<int32>(MechLoadout::NumCategories), MechLoadout::NumItems(MechLoadout::Shoulder),
				MechLoadout::Item(MechLoadout::Shoulder, 7).Name, MechDraft::IsBannable(MechLoadout::Shoulder) ? 1 : 0,
				*MechKeys::Default(EMechKey::Shoulder).ToString(), *MechKeys::Key(EMechKey::Shoulder).ToString()));
		UE_LOG(LogTemp, Display, TEXT("[tacticaltest] 汇总：通过 %d 项，没通过 %d 项"), TacPassed, TacFailed);
		ConsoleCommand(TEXT("quit"));
	}
}

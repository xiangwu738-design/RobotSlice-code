// -MechRunTest（11-01 保卫战肉鸽切片，MechRun.h）：每张卡放一遍对应动作，日志 [run] 记效果（穿透打中几个、三连发、分裂数量、爆炸半径、修甲到满、能量回了多少……），最后汇总。
//   UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Range?Defense" -game -MechRunTest -nullrhi -nosound -unattended          只看逻辑
//   UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Defense?Defense" -game -MechRunTest -RenderOffscreen -ForceRes -ResX=1920 -ResY=1080 -windowed -unattended     带截图（Saved/Screenshots/Run）
// 波次一直停在准备阶段，杂兵是测试自己刷的（瘫痪着，不动不打人，血量放大，不会被打死）；每一步开始前把卡、配装、位置、弹药、冷却都重置。
// 一步里只有这张卡的前置 + 这张卡，部分卡前面带一个「没有卡」的对照，确认效果确实是卡带来的。
#include "MechRun.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "MechCatalog.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechDefense.h"
#include "MechGrunt.h"
#include "MechHealthComponent.h"
#include "MechMissile.h"
#include "MechMovementComponent.h"
#include "MechScore.h"
#include "MechWeapons.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace MechRunTestLocal
{
	struct FStep
	{
		FString Name;
		float Dur = 1.f;
		TFunction<void()> Begin;
		/** T = 这一步过了多久，Prev = 上一帧的 T */
		TFunction<void(float T, float Prev)> Update;
	};

	TArray<FStep> Steps;
	int32 StepIndex = -1;
	float StepClock = 0.f;
	float Warmup = 0.f;
	bool bDone = false;
	int32 NumPassed = 0;
	int32 NumFailed = 0;
	TArray<FString> FailNames;

	UMechRunComponent* Run = nullptr;
	AMechCharacter* Me = nullptr;
	UMechCombatComponent* Combat = nullptr;
	APlayerController* PC = nullptr;
	AMechDefenseDirector* Dir = nullptr;
	FVector Origin = FVector::ZeroVector;
	FVector TestFwd = FVector::ForwardVector;
	FVector TestRgt = FVector::RightVector;
	float ForwardYaw = 0.f;

	TArray<TWeakObjectPtr<AMechGrunt>> G;
	TWeakObjectPtr<AMechCharacter> Dummy;
	// 每一步自己用的暂存
	float V0 = 0.f, V1 = 0.f, V2 = 0.f;
	int32 N0 = 0;
	bool bFlag = false;

	uint32 Bit(EMechRunCard C)
	{
		return 1u << static_cast<uint32>(C);
	}

	void Check(const FString& Name, bool bOk, const FString& Detail)
	{
		(bOk ? NumPassed : NumFailed)++;
		if (!bOk)
		{
			FailNames.Add(Name);
		}
		UE_LOG(LogTemp, Display, TEXT("[run] %s %s：%s"), bOk ? TEXT("通过") : TEXT("没通过"), *Name, *Detail);
	}

	void Shot(const FString& Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Run") / (Name + TEXT(".png")), true, false);
	}

	float GroundAt(const FVector& P)
	{
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(MechRunTestGround), false, Me);
		if (Me->GetWorld()->LineTraceSingleByObjectType(Hit, P + FVector(0.f, 0.f, 2000.f), P - FVector(0.f, 0.f, 5000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q))
		{
			return Hit.ImpactPoint.Z;
		}
		return P.Z - 100.f;
	}

	/** 前方 Dist、右边 Side（厘米），离地 Height */
	FVector Spot(float Dist, float Side, float Height)
	{
		FVector P = Origin + TestFwd * Dist + TestRgt * Side;
		P.Z = GroundAt(P) + Height;
		return P;
	}

	AMechGrunt* Spawn(EMechGrunt Kind, float Dist, float Side, float Height, float HPScale = 100.f)
	{
		AMechGrunt* R = Dir->DebugSpawnGrunt(Kind, Spot(Dist, Side, Height), HPScale, true);
		G.Add(R);
		return R;
	}

	void Face(const FVector& P)
	{
		const FRotator R = (P - PC->PlayerCameraManager->GetCameraLocation()).Rotation();
		PC->SetControlRotation(FRotator(FMath::Clamp(R.Pitch, -40.f, 40.f), R.Yaw, 0.f));
	}

	void FaceForward()
	{
		PC->SetControlRotation(FRotator(-3.f, ForwardYaw, 0.f));
	}

	AMechCharacter* NeedDummy(float Dist)
	{
		const FVector P = Spot(Dist, 0.f, 96.f * 1.35f + 5.f);
		if (!Dummy.IsValid())
		{
			const FTransform T(FRotator(0.f, ForwardYaw + 180.f, 0.f), P);
			AMechCharacter* D = Me->GetWorld()->SpawnActorDeferred<AMechCharacter>(AMechCharacter::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			D->AutoPossessAI = EAutoPossessAI::Disabled;
			D->SetTeam(1);
			D->DisplayName = TEXT("测试木桩");
			D->FinishSpawning(T);
			D->MakeTrainingDummy();
			Dummy = D;
		}
		Dummy->GetHealth()->ResetHealth();
		Dummy->GetCharacterMovement()->StopMovementImmediately();
		Dummy->SetActorLocation(P, false, nullptr, ETeleportType::TeleportPhysics);
		Dummy->MakeTrainingDummy();  // 记新位置（停手回原位）
		return Dummy.Get();
	}

	/** 这一步的准备：卡、配装、位置、弹药、冷却都重置，上一步的杂兵清掉 */
	void Setup(uint32 Cards, EMechFrame Frame = EMechFrame::Medium, EMechStance Stance = EMechStance::Gun)
	{
		Dir->DebugHoldPrep();
		for (const TWeakObjectPtr<AMechGrunt>& X : G)
		{
			if (X.IsValid())
			{
				X->DebugKill();
			}
		}
		G.Reset();
		if (Dummy.IsValid())
		{
			// 不用的时候挪到身后 40 米（木桩停手几秒会回「家」：家也一起改）
			FVector Away = Origin - TestFwd * 4000.f;
			Away.Z = GroundAt(Away) + 96.f * 1.35f + 5.f;
			Dummy->SetActorLocation(Away, false, nullptr, ETeleportType::TeleportPhysics);
			Dummy->MakeTrainingDummy();
		}
		for (TActorIterator<AMechMissile> It(Me->GetWorld()); It; ++It)
		{
			It->Destroy();
		}
		Run->DebugClearZones();
		Run->DebugReset(Cards);
		FMechLoadout L = Me->GetLoadout();
		L.Frame = Frame;
		L.Shooter = EMechShooter::Rifle;
		L.Melee = EMechMeleeWeapon::GreatSword;
		L.Shoulder = EMechShoulder::Missiles;
		Me->SetLoadout(L, true);
		Me->GetHealth()->SetAPScale(1.f);
		Me->GetHealth()->ResetHealth();
		Run->DebugResetCombat();
		Combat->SetTrigger(false);
		Combat->MeleeReleased();
		Combat->SetStanceNow(Stance);
		Combat->SpreadScale = 0.1f;  // 散布收小：测的是规则，不是枪法
		Me->GetCharacterMovement()->StopMovementImmediately();
		Me->SetActorLocation(Origin, false, nullptr, ETeleportType::TeleportPhysics);
		Me->SetActorRotation(FRotator(0.f, ForwardYaw, 0.f));
		FaceForward();
		V0 = V1 = V2 = 0.f;
		N0 = 0;
		bFlag = false;
	}

	float HP(int32 i)
	{
		return G.IsValidIndex(i) && G[i].IsValid() ? G[i]->GetHPFraction() : -1.f;
	}

	FString Pct(float F)
	{
		return FString::Printf(TEXT("%.2f%%"), F * 100.f);
	}

#define MECH_AT(X) (Prev < (X) && T >= (X))

	void AddStep(const FString& Name, float Dur, TFunction<void()> Begin, TFunction<void(float, float)> Update)
	{
		FStep& S = Steps.AddDefaulted_GetRef();
		S.Name = Name;
		S.Dur = Dur;
		S.Begin = MoveTemp(Begin);
		S.Update = MoveTemp(Update);
	}

	/** 一串三发 / 打一下：按下一帧就松 */
	void Click(float T, float Prev, float At)
	{
		if (MECH_AT(At))
		{
			Combat->SetTrigger(true);
		}
		if (MECH_AT(At + 0.03f))
		{
			Combat->SetTrigger(false);
		}
	}

	/** 按住左键到 Hold 秒后松开（从 At 算起；大剑 按住 0.22 秒起算重斩 / 蓄力） */
	void PressHold(float T, float Prev, float At, float Hold)
	{
		if (MECH_AT(At))
		{
			Combat->MeleePressed();
		}
		if (MECH_AT(At + Hold))
		{
			Combat->MeleeReleased();
		}
	}

	void BuildSteps()
	{
		using namespace MechRun;
		const float Reach = 440.f;  // 大剑的刀长（MechWeapons.cpp）

		// ================================================================ 公共：等级、经验、出卡
		AddStep(TEXT("等级和经验"), 2.2f, [] { Setup(0); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Gunner, 800.f, 0.f, 300.f, 0.001f);
			}
			if (MECH_AT(0.4f))
			{
				Run->HitEnemy(G[0].Get(), 100000.f, 0.f, Me->GetActorLocation(), EMechHitKind::Bullet);
				Check(TEXT("击杀经验（谁杀的算谁的）"), Run->GetXP() == XpKill[1], FString::Printf(TEXT("打死机枪无人机：经验 %d（表里 %d）"), Run->GetXP(), XpKill[1]));
				V0 = Me->GetHealth()->MaxAP;
				Me->GetHealth()->ApplyHit(V0 * 0.5f, 0.f, nullptr, Me->GetActorLocation());
				Run->AddXP(Run->GetNextXP() - Run->GetXP());
				const float S = Run->OutgoingScale();
				Check(TEXT("升级：装甲回满、上限 +3%、伤害 +2%"), Run->GetLevel() == 2 && FMath::IsNearlyEqual(Me->GetHealth()->MaxAP, V0 * 1.03f, 1.f) && Me->GetHealth()->GetAPFraction() > 0.999f && FMath::IsNearlyEqual(S, 1.02f, 0.001f),
					FString::Printf(TEXT("%d 级，装甲上限 %.0f → %.0f，装甲 %s，伤害倍数 %.2f，待选 %d"), Run->GetLevel(), V0, Me->GetHealth()->MaxAP, *Pct(Me->GetHealth()->GetAPFraction()), S, Run->GetPendingPicks()));
				Run->OnWaveEnd(3);
				Check(TEXT("守波经验"), Run->GetXP() == XpWaveBase + XpWavePer * 3, FString::Printf(TEXT("第 3 波打完：经验 %d（10 + 5 × 3）"), Run->GetXP()));
			}
			if (MECH_AT(0.8f))
			{
				const TArray<uint8>& O = Run->GetOffer();
				bool bDistinct = O.Num() == 3;
				for (int32 a = 0; a < O.Num(); ++a)
				{
					for (int32 b = a + 1; b < O.Num(); ++b)
					{
						bDistinct &= Card(static_cast<EMechRunCard>(O[a])).Item != Card(static_cast<EMechRunCard>(O[b])).Item;
					}
				}
				FString Names;
				for (const uint8 C : O)
				{
					Names += FString(Card(static_cast<EMechRunCard>(C)).Name) + TEXT(" ");
				}
				Check(TEXT("出 3 张卡，尽量不同件"), bDistinct, Names);
				Shot(TEXT("选卡"));
			}
			// 选卡走真的按键：模拟按一下数字键 1（AMechPlayerController::PlayerTick → LocalInput）
			if (MECH_AT(1.2f))
			{
				N0 = Run->GetOffer().IsValidIndex(0) ? Run->GetOffer()[0] : 0;
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::One, IE_Pressed, 1.f));
			}
			if (MECH_AT(1.3f))
			{
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::One, IE_Released, 0.f));
			}
			if (MECH_AT(1.6f))
			{
				const EMechRunCard First = static_cast<EMechRunCard>(N0);
				Check(TEXT("按 1 选卡生效"), Run->Has(First) && Run->GetPendingPicks() == 0 && Run->GetOffer().Num() == 0, FString::Printf(TEXT("选了 %s，剩 %d 次"), Card(First).Name, Run->GetPendingPicks()));
				Shot(TEXT("选卡后"));
			}
		});

		AddStep(TEXT("出卡规则（不锁路径）"), 0.8f, [] { Setup(Bit(EMechRunCard::Flood1)); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				// 拿了洪流 1，抽很多次：点射 1 应该还能出（不锁路径）；洪流 3 不能出（上一步没拿）；别家框架的卡不能出
				bool bBurst1 = false, bFlood2 = false, bFlood3 = false, bOtherFrame = false, bFlood1 = false;
				for (int32 i = 0; i < 300; ++i)
				{
					Run->DebugForceOffer();
					for (const uint8 C : Run->GetOffer())
					{
						const EMechRunCard Cd = static_cast<EMechRunCard>(C);
						bBurst1 |= Cd == EMechRunCard::Burst1;
						bFlood1 |= Cd == EMechRunCard::Flood1;
						bFlood2 |= Cd == EMechRunCard::Flood2;
						bFlood3 |= Cd == EMechRunCard::Flood3;
						bOtherFrame |= Card(Cd).Item == EMechRunItem::FrameVictor || Card(Cd).Item == EMechRunItem::FramePhase;
					}
				}
				Check(TEXT("不锁路径：两条线可以混着拿"), bBurst1 && bFlood2 && !bFlood3 && !bFlood1 && !bOtherFrame,
					FString::Printf(TEXT("点射 1 %d、洪流 2 %d、洪流 3 %d（要上一步）、已拿的洪流 1 %d、别家框架 %d"), bBurst1, bFlood2, bFlood3, bFlood1, bOtherFrame));
			}
			if (MECH_AT(0.4f))
			{
				// 全拿了（这台是新纪框架）：升级照常，只是没有卡
				Run->DebugReset(0xFFFFFFu);
				Me->GetHealth()->ApplyHit(1000.f, 0.f, nullptr, Me->GetActorLocation());
				Run->AddXP(Run->GetNextXP());
				Run->DebugForceOffer();
				Check(TEXT("没有可选的也正常升级"), Run->GetLevel() >= 2 && Run->GetOffer().Num() == 0 && Run->GetPendingPicks() == 0 && Me->GetHealth()->GetAPFraction() > 0.999f,
					FString::Printf(TEXT("%d 级，出 %d 张，待选 %d，装甲 %s"), Run->GetLevel(), Run->GetOffer().Num(), Run->GetPendingPicks(), *Pct(Me->GetHealth()->GetAPFraction())));
			}
		});

		AddStep(TEXT("重生恢复配装和等级"), 1.2f, [] { Setup(Bit(EMechRunCard::Flood1)); }, [](float T, float Prev) {
			if (MECH_AT(0.3f))
			{
				FMechLoadout L = Me->GetLoadout();
				L.Shooter = EMechShooter::Rifle;
				L.Melee = EMechMeleeWeapon::GreatSword;
				Me->SetLoadout(L, true);
				Run->AddXP(Run->GetNextXP());
				V0 = Me->GetHealth()->MaxAP;
			}
			if (MECH_AT(0.6f))
			{
				Dir->DebugRespawn(PC);  // 下一帧组件绑到新机体
			}
			if (MECH_AT(1.f))
			{
				Check(TEXT("重生后配装、等级、卡都还在"), Me->GetLoadout().Melee == EMechMeleeWeapon::GreatSword && Run->GetLevel() == 2 && Run->Has(EMechRunCard::Flood1) && FMath::IsNearlyEqual(Me->GetHealth()->MaxAP, V0, 1.f) && Me->GetRun() == Run,
					FString::Printf(TEXT("近战 %d（大剑 %d），%d 级，装甲上限 %.0f（重生前 %.0f）"), (int32)Me->GetLoadout().Melee, (int32)EMechMeleeWeapon::GreatSword, Run->GetLevel(), Me->GetHealth()->MaxAP, V0));
				Combat = Me->GetCombat();
			}
		});

		AddStep(TEXT("按住 Tab 看卡"), 1.6f, [] { Setup(Bit(EMechRunCard::Flood1) | Bit(EMechRunCard::Flood2) | Bit(EMechRunCard::Burst1) | Bit(EMechRunCard::Rift1) | Bit(EMechRunCard::Swarm1) | Bit(EMechRunCard::Swarm2) | Bit(EMechRunCard::Swarm3) | Bit(EMechRunCard::Repair1)); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Tab, IE_Pressed, 1.f));
			}
			if (MECH_AT(0.8f))
			{
				Shot(TEXT("已拿到的卡"));
			}
			if (MECH_AT(1.3f))
			{
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Tab, IE_Released, 0.f));
			}
		});

		// ================================================================ 步枪：洪流
		auto Pierce = [](bool bCard) {
			return [bCard](float T, float Prev) {
				if (MECH_AT(0.2f))
				{
					// 三架排成一线（都在镜头这条线上，Z 取镜头高度）
					const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
					const FVector Ray = PC->GetControlRotation().Vector();
					for (const float D : {1800.f, 2600.f, 3400.f})
					{
						G.Add(Dir->DebugSpawnGrunt(EMechGrunt::Gunner, Cam + Ray * D, 100.f, true));
					}
				}
				if (MECH_AT(0.4f))
				{
					Combat->SetTrigger(true);
				}
				if (MECH_AT(1.4f))
				{
					Combat->SetTrigger(false);
				}
				if (MECH_AT(1.8f))
				{
					const FString D = FString::Printf(TEXT("一发最多打中 %d 个，三架血量 %s / %s / %s"), Run->Debug.MaxPierce, *Pct(HP(0)), *Pct(HP(1)), *Pct(HP(2)));
					if (bCard)
					{
						Check(TEXT("洪流 1 穿透"), Run->Debug.MaxPierce >= 2 && HP(1) < 1.f, D);
					}
					else
					{
						Check(TEXT("对照：没有卡不穿透"), Run->Debug.MaxPierce == 0 && HP(0) < 1.f && HP(1) >= 1.f && HP(2) >= 1.f, D);
					}
				}
			};
		};
		AddStep(TEXT("对照 穿透"), 2.f, [] { Setup(0); }, Pierce(false));
		AddStep(TEXT("洪流 1 穿透"), 2.f, [] { Setup(Bit(EMechRunCard::Flood1)); }, Pierce(true));

		auto StreakSwitch = [](bool bCard) {
			return [bCard](float T, float Prev) {
				if (MECH_AT(0.2f))
				{
					Spawn(EMechGrunt::Gunner, 2200.f, -300.f, 250.f);
					Spawn(EMechGrunt::Gunner, 2200.f, 300.f, 250.f);
				}
				if (T > 0.3f && HP(0) > 0.f)
				{
					Face(G[T < 1.4f ? 0 : 1]->GetActorLocation());
				}
				if (MECH_AT(0.4f))
				{
					Combat->SetTrigger(true);
				}
				if (MECH_AT(1.4f))
				{
					N0 = Combat->GetStreak();
				}
				if (MECH_AT(1.8f))
				{
					Combat->SetTrigger(false);
					const int32 After = Combat->GetStreak();
					const FString D = FString::Printf(TEXT("换目标前 %d 层，换目标打 0.4 秒后 %d 层（一层 +3%% 伤害）"), N0, After);
					if (bCard)
					{
						Check(TEXT("洪流 2 换目标不清"), N0 >= 5 && After > N0, D);
					}
					else
					{
						Check(TEXT("对照：没有卡换目标清零（杂兵也算连中）"), N0 >= 5 && After <= 5, D);
					}
				}
			};
		};
		AddStep(TEXT("对照 换目标"), 2.f, [] { Setup(0); }, StreakSwitch(false));
		AddStep(TEXT("洪流 2 不断连"), 2.f, [] { Setup(Bit(EMechRunCard::Flood1) | Bit(EMechRunCard::Flood2)); }, StreakSwitch(true));

		AddStep(TEXT("洪流 3 追踪弹"), 2.f, [] { Setup(Bit(EMechRunCard::Flood1) | Bit(EMechRunCard::Flood2) | Bit(EMechRunCard::Flood3)); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Gunner, 2000.f, 0.f, 250.f);
				Spawn(EMechGrunt::Gunner, 2000.f, 1500.f, 900.f);  // 别的方向、别的高度：直射打不到
			}
			if (T > 0.3f && HP(0) > 0.f)
			{
				Face(G[0]->GetActorLocation());
			}
			if (MECH_AT(0.4f))
			{
				Run->DebugSetStreak(MechWeapons::StreakMax);
				Combat->SetTrigger(true);
			}
			if (MECH_AT(1.4f))
			{
				Combat->SetTrigger(false);
			}
			if (MECH_AT(1.8f))
			{
				Check(TEXT("洪流 3 满层分追踪弹"), Run->Debug.HomingSpawned > 0 && Run->Debug.HomingHits > 0 && HP(1) < 1.f,
					FString::Printf(TEXT("分出 %d 发，打中 %d 发，旁边那架血量 %s"), Run->Debug.HomingSpawned, Run->Debug.HomingHits, *Pct(HP(1))));
			}
		});

		// ================================================================ 步枪：点射
		AddStep(TEXT("点射 1 三连发"), 2.6f, [] { Setup(Bit(EMechRunCard::Burst1)); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Tank, 2500.f, 0.f, 120.f);
			}
			if (T > 0.3f && HP(0) > 0.f)
			{
				Face(G[0]->GetActorLocation());
			}
			if (MECH_AT(0.4f))
			{
				N0 = Combat->GetAmmo();
			}
			Click(T, Prev, 0.5f);
			if (MECH_AT(1.f))
			{
				Check(TEXT("点射 1 按一下打三发"), N0 - Combat->GetAmmo() == 3 && Run->Debug.BurstGroups == 1, FString::Printf(TEXT("弹药 %d → %d，%d 组 %d 发"), N0, Combat->GetAmmo(), Run->Debug.BurstGroups, Run->Debug.BurstShots));
				Run->Debug = FMechRunDebug();
				Run->DebugSetAmmo(Combat->GetMagazine());
				Combat->SetTrigger(true);
			}
			if (MECH_AT(2.2f))
			{
				Combat->SetTrigger(false);
				const int32 Shots = Run->Debug.BurstShots;
				Check(TEXT("点射 1 按住一组接一组"), Shots >= 6 && Shots % 3 == 0, FString::Printf(TEXT("按住 1.2 秒：%d 组 %d 发"), Run->Debug.BurstGroups, Shots));
			}
		});

		AddStep(TEXT("点射 2 返弹"), 1.5f, [] { Setup(Bit(EMechRunCard::Burst1) | Bit(EMechRunCard::Burst2)); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Tank, 2500.f, 0.f, 120.f);
			}
			if (T > 0.3f && HP(0) > 0.f)
			{
				Face(G[0]->GetActorLocation());
			}
			if (MECH_AT(0.4f))
			{
				Run->DebugSetAmmo(20);
				N0 = 20;
			}
			Click(T, Prev, 0.5f);
			if (MECH_AT(1.2f))
			{
				Check(TEXT("点射 2 三发全中返还"), Combat->GetAmmo() == N0 && Run->Debug.BurstRefunds == 1, FString::Printf(TEXT("弹药 %d → %d，返还 %d 次"), N0, Combat->GetAmmo(), Run->Debug.BurstRefunds));
			}
		});

		AddStep(TEXT("点射 3 引爆"), 1.5f, [] { Setup(Bit(EMechRunCard::Burst1) | Bit(EMechRunCard::Burst2) | Bit(EMechRunCard::Burst3)); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Tank, 2500.f, 0.f, 120.f);
				Spawn(EMechGrunt::Gunner, 2500.f, 550.f, 200.f);
			}
			if (T > 0.3f && HP(0) > 0.f)
			{
				Face(G[0]->GetActorLocation());
			}
			Click(T, Prev, 0.5f);
			if (MECH_AT(0.75f))
			{
				Shot(TEXT("点射引爆"));
			}
			if (MECH_AT(1.2f))
			{
				Check(TEXT("点射 3 同目标引爆"), Run->Debug.Detonations == 1 && HP(1) < 1.f, FString::Printf(TEXT("引爆 %d 次（半径 %.1f 米），旁边那架血量 %s"), Run->Debug.Detonations, DetonateRadius / 100.f, *Pct(HP(1))));
			}
		});

		// ================================================================ 大剑：地裂
		AddStep(TEXT("地裂 1 冲击波"), 2.6f, [] { Setup(Bit(EMechRunCard::Rift1), EMechFrame::Medium, EMechStance::Blade); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Tank, 1500.f, 0.f, 120.f);  // 20 米以内
				Spawn(EMechGrunt::Tank, 3200.f, 0.f, 120.f);  // 20 米以外
				Spawn(EMechGrunt::Gunner, 1000.f, 300.f, 1400.f);  // 高处的机枪无人机：贴地的冲击波打不到
			}
			PressHold(T, Prev, 0.4f, 0.35f);
			if (MECH_AT(2.3f))
			{
				Check(TEXT("地裂 1 重斩放 20 米冲击波"), Run->Debug.Waves == 1 && Run->Debug.WaveHits == 1 && HP(0) < 1.f && HP(1) >= 1.f && HP(2) >= 1.f,
					FString::Printf(TEXT("放了 %d 道，打中 %d 个；15 米的炮车 %s，32 米的炮车 %s，高处的无人机 %s"), Run->Debug.Waves, Run->Debug.WaveHits, *Pct(HP(0)), *Pct(HP(1)), *Pct(HP(2))));
			}
		});
		AddStep(TEXT("地裂 2 三叉"), 2.8f, [] { Setup(Bit(EMechRunCard::Rift1) | Bit(EMechRunCard::Rift2), EMechFrame::Medium, EMechStance::Blade); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				const FVector D1 = TestFwd.RotateAngleAxis(WaveFan, FVector::UpVector);
				const FVector D2 = TestFwd.RotateAngleAxis(-WaveFan, FVector::UpVector);
				Spawn(EMechGrunt::Tank, 1500.f, 0.f, 120.f);
				G.Add(Dir->DebugSpawnGrunt(EMechGrunt::Tank, Spot((D1 | TestFwd) * 1500.f, (D1 | TestRgt) * 1500.f, 120.f), 100.f, true));
				G.Add(Dir->DebugSpawnGrunt(EMechGrunt::Tank, Spot((D2 | TestFwd) * 1500.f, (D2 | TestRgt) * 1500.f, 120.f), 100.f, true));
			}
			PressHold(T, Prev, 0.4f, 0.35f);
			for (int32 k = 0; k < 6; ++k)
			{
				if (MECH_AT(0.95f + k * 0.1f))
				{
					Shot(FString::Printf(TEXT("地裂冲击波_%d"), k + 1));
				}
			}
			if (MECH_AT(2.5f))
			{
				Check(TEXT("地裂 2 三道扇形"), Run->Debug.Waves == 3 && Run->Debug.WaveHits == 3 && HP(1) < 1.f && HP(2) < 1.f, FString::Printf(TEXT("放了 %d 道，打中 %d 个；三台炮车 %s / %s / %s"), Run->Debug.Waves, Run->Debug.WaveHits, *Pct(HP(0)), *Pct(HP(1)), *Pct(HP(2))));
			}
		});
		AddStep(TEXT("地裂 3 余烬"), 6.5f, [] { Setup(Bit(EMechRunCard::Rift1), EMechFrame::Medium, EMechStance::Blade); Run->DebugGive(Bit(EMechRunCard::Rift3)); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Tank, 1200.f, 0.f, 120.f);
			}
			PressHold(T, Prev, 0.4f, 0.35f);
			if (MECH_AT(2.f))
			{
				V0 = HP(0);
				N0 = Run->DebugZoneCount();
				Shot(TEXT("地裂余烬"));
			}
			if (MECH_AT(4.f))
			{
				V1 = HP(0);
			}
			if (MECH_AT(6.2f))
			{
				Check(TEXT("地裂 3 路径持续伤害"), N0 == 1 && Run->Debug.ZonesSpawned == 1 && Run->Debug.ZoneHits >= 8 && V1 < V0 && Run->DebugZoneCount() == 0,
					FString::Printf(TEXT("留下 %d 段，%.0f 秒内跳了 %d 次；炮车 %s → %s，结束后还剩 %d 段"), N0, StripTime, Run->Debug.ZoneHits, *Pct(V0), *Pct(V1), Run->DebugZoneCount()));
			}
		});

		// ================================================================ 大剑：蓄力
		// 炮车放在普通重斩够不着、蓄满够得着的地方（够得着 = 1.2 × 刀长 + 球半径）
		const float ChargeTarget = Reach * 1.2f + 330.f + 450.f;
		AddStep(TEXT("对照 重斩够不着"), 2.2f, [] { Setup(0, EMechFrame::Medium, EMechStance::Blade); }, [ChargeTarget](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Tank, ChargeTarget, 0.f, 120.f);
			}
			PressHold(T, Prev, 0.4f, 0.35f);
			if (MECH_AT(1.9f))
			{
				Check(TEXT("对照：普通重斩够不着"), HP(0) >= 1.f, FString::Printf(TEXT("炮车离 %.1f 米，血量 %s"), ChargeTarget / 100.f, *Pct(HP(0))));
			}
		});
		AddStep(TEXT("蓄力 1 半蓄"), 3.4f, [] { Setup(Bit(EMechRunCard::Charge1), EMechFrame::Medium, EMechStance::Blade); }, [](float T, float Prev) {
			PressHold(T, Prev, 0.3f, 1.5f);
			if (MECH_AT(1.5f))
			{
				V0 = Combat->GetMeleeTime();
				Check(TEXT("蓄力 1 按住停在重斩起手"), Combat->GetMeleeMove() == EMechMelee::Heavy && Combat->GetRunChargeHold() == 1 && V0 < Combat->GetMeleeStrikeAt(), FString::Printf(TEXT("招式 %d，蓄力状态 %d，招式时间停在 %.2f 秒（砍下去是 %.2f 秒）"), (int32)Combat->GetMeleeMove(), Combat->GetRunChargeHold(), V0, Combat->GetMeleeStrikeAt()));
			}
			if (MECH_AT(3.1f))
			{
				Check(TEXT("蓄力 1 没蓄满，范围按比例"), Run->Debug.LastCharge > 0.2f && Run->Debug.LastCharge < 0.7f && Run->Debug.LastReach > 440.f * 1.1f && Run->Debug.LastReach < 440.f * ChargeReach,
					FString::Printf(TEXT("蓄了 %.0f%%，判定范围 %.1f 米（不蓄 %.1f 米）"), Run->Debug.LastCharge * 100.f, Run->Debug.LastReach / 100.f, 4.4f));
			}
		});
		AddStep(TEXT("蓄力 1 蓄满"), 5.2f, [] { Setup(Bit(EMechRunCard::Charge1), EMechFrame::Medium, EMechStance::Blade); }, [ChargeTarget](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Tank, ChargeTarget, 0.f, 120.f);
			}
			PressHold(T, Prev, 0.4f, 3.5f);
			if (MECH_AT(2.4f))
			{
				Shot(TEXT("蓄力光效"));
			}
			if (MECH_AT(5.f))
			{
				Check(TEXT("蓄力 1 蓄满够得着"), Run->Debug.LastCharge >= 0.99f && FMath::IsNearlyEqual(Run->Debug.LastReach, 440.f * ChargeReach, 1.f) && HP(0) < 1.f,
					FString::Printf(TEXT("蓄了 %.0f%%，判定范围 %.1f 米，炮车离 %.1f 米，血量 %s"), Run->Debug.LastCharge * 100.f, Run->Debug.LastReach / 100.f, ChargeTarget / 100.f, *Pct(HP(0))));
			}
		});
		auto ArmorStep = [](bool bCard) {
			return [bCard](float T, float Prev) {
				PressHold(T, Prev, 0.2f, 1.3f);
				if (MECH_AT(1.f))
				{
					UMechHealthComponent* H = Me->GetHealth();
					const float Stagger0 = H->GetStaggerFraction();
					const float Dealt = Me->ReceiveHit(1000.f, 400.f, nullptr, Me->GetActorLocation() + TestFwd * 100.f, EMechHitKind::Bullet);
					const FString D = FString::Printf(TEXT("蓄力中挨 1000 伤害：实际 %.0f，失衡 %.0f%% → %.0f%%"), Dealt, Stagger0 * 100.f, H->GetStaggerFraction() * 100.f);
					if (bCard)
					{
						Check(TEXT("蓄力 2 霸体减半"), FMath::IsNearlyEqual(Dealt, 1000.f * ChargeDamageTaken, 1.f) && H->GetStaggerFraction() <= Stagger0, D);
					}
					else
					{
						Check(TEXT("对照：只有蓄力 1 没有霸体"), FMath::IsNearlyEqual(Dealt, 1000.f, 1.f) && H->GetStaggerFraction() > Stagger0, D);
					}
				}
			};
		};
		AddStep(TEXT("对照 蓄力没霸体"), 2.2f, [] { Setup(Bit(EMechRunCard::Charge1), EMechFrame::Medium, EMechStance::Blade); }, ArmorStep(false));
		AddStep(TEXT("蓄力 2 霸体"), 2.2f, [] { Setup(Bit(EMechRunCard::Charge1) | Bit(EMechRunCard::Charge2), EMechFrame::Medium, EMechStance::Blade); }, ArmorStep(true));
		AddStep(TEXT("蓄力 3 没蓄满不放"), 2.8f, [] { Setup(Bit(EMechRunCard::Charge1) | Bit(EMechRunCard::Charge2) | Bit(EMechRunCard::Charge3), EMechFrame::Medium, EMechStance::Blade); }, [](float T, float Prev) {
			PressHold(T, Prev, 0.2f, 1.f);
			if (MECH_AT(2.5f))
			{
				Check(TEXT("蓄力 3 没蓄满不放"), Run->Debug.Waves == 0, FString::Printf(TEXT("蓄了 %.0f%%，放了 %d 道"), Run->Debug.LastCharge * 100.f, Run->Debug.Waves));
			}
		});
		AddStep(TEXT("蓄力 3 横贯"), 6.f, [] { Setup(Bit(EMechRunCard::Charge1) | Bit(EMechRunCard::Charge2) | Bit(EMechRunCard::Charge3), EMechFrame::Medium, EMechStance::Blade); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Tank, 8000.f, 0.f, 120.f);
				Spawn(EMechGrunt::Gunner, 5000.f, 100.f, 1400.f);
			}
			PressHold(T, Prev, 0.4f, 3.5f);
			if (MECH_AT(4.35f))
			{
				Shot(TEXT("横贯一刀"));
			}
			if (MECH_AT(5.7f))
			{
				Check(TEXT("蓄力 3 蓄满横贯"), Run->Debug.SlashHits >= 2 && HP(0) < 1.f && HP(1) < 1.f, FString::Printf(TEXT("打中 %d 个：80 米的炮车 %s，50 米高处的无人机 %s"), Run->Debug.SlashHits, *Pct(HP(0)), *Pct(HP(1))));
			}
		});

		// ================================================================ 导弹：蜂群
		AddStep(TEXT("蜂群 1 锁杂兵"), 5.f, [] { Setup(Bit(EMechRunCard::Swarm1)); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Gunner, 4000.f, 0.f, 800.f);
			}
			if (T > 0.3f && T < 2.f && HP(0) > 0.f)
			{
				Face(G[0]->GetActorLocation());
			}
			if (MECH_AT(1.7f))
			{
				Check(TEXT("蜂群 1 能锁上杂兵"), Run->GetLockActor() == G[0].Get() && Run->GetLockProgress() >= 1.f, FString::Printf(TEXT("锁定进度 %.0f%%"), Run->GetLockProgress() * 100.f));
				Shot(TEXT("蜂群锁杂兵"));
			}
			if (MECH_AT(2.f))
			{
				Combat->FireMissiles();
			}
			if (MECH_AT(2.4f))
			{
				// 发射后把目标挪开 15 米：直飞的打不中，追踪的打得中
				G[0]->SetActorLocation(G[0]->GetActorLocation() + TestRgt * 1500.f);
			}
			if (MECH_AT(4.7f))
			{
				Check(TEXT("蜂群 1 导弹追杂兵"), Run->Debug.SalvoTargets == 1 && HP(0) < 1.f, FString::Printf(TEXT("一轮 %d 枚，目标挪开 15 米后血量 %s"), Run->Debug.SalvoMissiles, *Pct(HP(0))));
			}
		});
		AddStep(TEXT("蜂群 2 分裂"), 5.f, [] { Setup(Bit(EMechRunCard::Swarm1) | Bit(EMechRunCard::Swarm2)); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Tank, 2800.f, 0.f, 120.f);
				Spawn(EMechGrunt::Tank, 2800.f, 1500.f, 120.f);
			}
			if (T > 0.3f && T < 2.f && HP(0) > 0.f)
			{
				Face(G[0]->GetActorLocation());
			}
			if (MECH_AT(1.6f))
			{
				Combat->FireMissiles();
			}
			if (MECH_AT(2.5f) || MECH_AT(2.8f))
			{
				Shot(MECH_AT(2.5f) ? TEXT("蜂群分裂_1") : TEXT("蜂群分裂_2"));
			}
			if (MECH_AT(4.7f))
			{
				Check(TEXT("蜂群 2 命中后分裂成 3 枚"), Run->Debug.Splits == Run->Debug.SalvoMissiles * SplitCount && Run->Debug.Splits > 0 && HP(1) < 1.f,
					FString::Printf(TEXT("母弹 %d 枚，分裂出 %d 枚（每枚 %d）；旁边 15 米的炮车血量 %s"), Run->Debug.SalvoMissiles, Run->Debug.Splits, SplitCount, *Pct(HP(1))));
			}
		});
		AddStep(TEXT("蜂群 3 全锁"), 5.f, [] { Setup(Bit(EMechRunCard::Swarm1) | Bit(EMechRunCard::Swarm2) | Bit(EMechRunCard::Swarm3)); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				for (int32 i = 0; i < 6; ++i)
				{
					Spawn(EMechGrunt::Gunner, 3000.f + 300.f * i, (i - 2.5f) * 900.f, 700.f + 150.f * i);
				}
			}
			if (MECH_AT(0.8f))
			{
				Shot(TEXT("蜂群全锁_框"));
				Combat->FireMissiles();
			}
			if (MECH_AT(1.7f))
			{
				Shot(TEXT("蜂群全锁"));
			}
			if (MECH_AT(4.7f))
			{
				int32 Hurt = 0;
				for (int32 i = 0; i < 6; ++i)
				{
					Hurt += HP(i) < 1.f ? 1 : 0;
				}
				Check(TEXT("蜂群 3 一次锁定视野内所有敌人"), Run->Debug.SalvoTargets == 6 && Run->Debug.SalvoMissiles == 6 && Hurt == 6, FString::Printf(TEXT("锁了 %d 个，打 %d 枚，%d 个掉血"), Run->Debug.SalvoTargets, Run->Debug.SalvoMissiles, Hurt));
			}
		});

		// ================================================================ 导弹：重弹
		AddStep(TEXT("重弹 1 合弹"), 3.f, [] { Setup(Bit(EMechRunCard::Heavy1)); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				AMechCharacter* D = NeedDummy(2500.f);
				Face(D->GetActorLocation());
			}
			if (MECH_AT(0.5f))
			{
				Combat->FireMissiles();
			}
			if (MECH_AT(0.8f))
			{
				int32 Count = 0;
				float Scale = 0.f;
				for (TActorIterator<AMechMissile> It(Me->GetWorld()); It; ++It)
				{
					++Count;
					Scale = It->GetActorScale3D().X;
				}
				Check(TEXT("重弹 1 一轮合成 1 枚"), Count == 1 && Run->Debug.SalvoMissiles == 1 && FMath::IsNearlyEqual(Scale, BigScale, 0.01f) && FMath::IsNearlyEqual(Run->Debug.LastMissileDamage, 180.f * 4.f * BigDamage, 1.f),
					FString::Printf(TEXT("场上 %d 枚，画面 ×%.1f，伤害 %.0f（普通一轮 4 × 180），半径 %.1f 米（普通 3.8 米）"), Count, Scale, Run->Debug.LastMissileDamage, Run->Debug.LastMissileRadius / 100.f));
			}
			if (MECH_AT(2.7f))
			{
				Check(TEXT("对照：重弹 1 不打失衡"), Dummy.IsValid() && !Dummy->GetHealth()->IsStaggered(), FString::Printf(TEXT("木桩失衡 %.0f%%"), Dummy.IsValid() ? Dummy->GetHealth()->GetStaggerFraction() * 100.f : -1.f));
			}
		});
		AddStep(TEXT("重弹 2 震爆"), 3.f, [] { Setup(Bit(EMechRunCard::Heavy1) | Bit(EMechRunCard::Heavy2)); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				AMechCharacter* D = NeedDummy(2500.f);
				Face(D->GetActorLocation());
				Spawn(EMechGrunt::Gunner, 2500.f, 800.f, 300.f);
			}
			if (MECH_AT(0.5f))
			{
				Combat->FireMissiles();
			}
			if (Dummy.IsValid() && Dummy->GetHealth()->IsStaggered())
			{
				bFlag = true;
			}
			if (MECH_AT(2.7f))
			{
				Check(TEXT("重弹 2 范围翻倍、打出失衡"), FMath::IsNearlyEqual(Run->Debug.LastMissileRadius, 380.f * BigRadius * ShockRadius, 1.f) && bFlag && Run->Debug.Stunned >= 1 && HP(0) < 1.f,
					FString::Printf(TEXT("半径 %.1f 米（重弹 1 是 %.1f 米）；机甲木桩失衡 %d；8 米外的杂兵瘫痪 %d 个，血量 %s"), Run->Debug.LastMissileRadius / 100.f, 380.f * BigRadius / 100.f, bFlag ? 1 : 0, Run->Debug.Stunned, *Pct(HP(0))));
			}
		});
		AddStep(TEXT("重弹 3 火场"), 5.5f, [] { Setup(Bit(EMechRunCard::Heavy1) | Bit(EMechRunCard::Heavy2) | Bit(EMechRunCard::Heavy3)); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Tank, 2500.f, 0.f, 120.f);
				Face(G[0]->GetActorLocation());
			}
			if (MECH_AT(0.5f))
			{
				Combat->FireMissiles();
			}
			if (V0 <= 0.f && Run->DebugZoneCount() > 0)
			{
				V0 = T;  // 火场出现
				V1 = HP(0);
			}
			if (V0 > 0.f && V2 <= 0.f && Run->DebugZoneCount() == 0)
			{
				V2 = T;  // 火场没了
			}
			if (V0 > 0.f && !bFlag && T > V0 + 1.5f)
			{
				bFlag = true;
				Shot(TEXT("火场"));
			}
			if (MECH_AT(5.2f))
			{
				Check(TEXT("重弹 3 落点留 3 秒火场"), V0 > 0.f && FMath::IsNearlyEqual(V2 - V0, FireTime, 0.25f) && Run->Debug.ZoneHits >= 8 && HP(0) < V1,
					FString::Printf(TEXT("烧了 %.1f 秒，跳了 %d 次；炮车 %s → %s"), V2 - V0, Run->Debug.ZoneHits, *Pct(V1), *Pct(HP(0))));
			}
		});

		// ================================================================ 框架
		auto Hurting = [](bool bCard) {
			return [bCard](float T, float Prev) {
				UMechHealthComponent* H = Me->GetHealth();
				if (MECH_AT(0.1f))
				{
					H->ApplyHit(H->MaxAP * 0.7f, 0.f, nullptr, Me->GetActorLocation());
				}
				// 一直挨打（每秒 1 点伤害：SinceHit 一直归零）
				for (float K = 0.5f; K < 6.f; K += 1.f)
				{
					if (MECH_AT(K))
					{
						H->ApplyHit(1.f, 0.f, nullptr, Me->GetActorLocation());
					}
				}
				if (MECH_AT(0.5f))
				{
					V0 = H->GetAPFraction();
				}
				if (MECH_AT(5.5f))
				{
					const float Rate = (H->GetAPFraction() - V0) / 5.f;
					const FString D = FString::Printf(TEXT("装甲 %s → %s，挨打中每秒修 %.2f%%（平时 2%%）"), *Pct(V0), *Pct(H->GetAPFraction()), Rate * 100.f);
					if (bCard)
					{
						Check(TEXT("抢修 1 挨打也修，慢一半"), Rate > 0.007f && Rate < 0.013f, D);
					}
					else
					{
						Check(TEXT("对照：没有卡挨打不修"), Rate < 0.001f, D);
					}
				}
			};
		};
		AddStep(TEXT("对照 抢修"), 5.8f, [] { Setup(0); }, Hurting(false));
		AddStep(TEXT("抢修 1"), 5.8f, [] { Setup(Bit(EMechRunCard::Repair1)); }, Hurting(true));
		AddStep(TEXT("抢修 2"), 5.2f, [] { Setup(Bit(EMechRunCard::Repair1) | Bit(EMechRunCard::Repair2)); }, [](float T, float Prev) {
			UMechHealthComponent* H = Me->GetHealth();
			if (MECH_AT(0.1f))
			{
				H->ApplyHit(H->MaxAP * 0.04f, 0.f, nullptr, Me->GetActorLocation());
				V0 = H->GetAPFraction();
			}
			if (MECH_AT(5.f))
			{
				Check(TEXT("抢修 2 修到满（原来最多一半）"), H->GetAPFraction() > 0.999f, FString::Printf(TEXT("装甲 %s → %s"), *Pct(V0), *Pct(H->GetAPFraction())));
			}
		});

		AddStep(TEXT("补给"), 1.5f, [] { Setup(Bit(EMechRunCard::Supply1) | Bit(EMechRunCard::Supply2), EMechFrame::VictorMedium); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				Spawn(EMechGrunt::Gunner, 800.f, 0.f, 300.f, 0.001f);
				Run->DebugSetAmmo(5);
				Run->DebugSetMissileTimer(8.f);
			}
			if (MECH_AT(0.4f))
			{
				V0 = Combat->GetMissileTimer();
				const int32 Mag = Combat->GetMagazine();
				Run->HitEnemy(G[0].Get(), 100000.f, 0.f, Me->GetActorLocation(), EMechHitKind::Bullet);
				Check(TEXT("补给 1 击杀补 1/3 弹匣"), Combat->GetAmmo() == 5 + FMath::CeilToInt(Mag * SupplyAmmo), FString::Printf(TEXT("弹匣 %d：5 → %d"), Mag, Combat->GetAmmo()));
				Check(TEXT("补给 2 击杀缩短肩部冷却"), Run->Debug.CooldownGiven > SupplyCooldown - 0.2f, FString::Printf(TEXT("导弹冷却 %.1f → %.1f 秒"), V0, Combat->GetMissileTimer()));
			}
		});

		AddStep(TEXT("空战 1"), 2.f, [] { Setup(Bit(EMechRunCard::Air1), EMechFrame::PhaseMedium); }, [](float T, float Prev) {
			if (MECH_AT(0.2f))
			{
				NeedDummy(1500.f);
			}
			if (MECH_AT(0.5f))
			{
				V0 = Dummy->ReceiveHit(1000.f, 0.f, Me, Dummy->GetActorLocation(), EMechHitKind::Bullet);
				Me->LaunchCharacter(FVector(0.f, 0.f, 1800.f), false, true);
			}
			if (MECH_AT(1.f))
			{
				const bool bAir = Me->GetCharacterMovement()->IsFalling();
				V1 = Dummy->ReceiveHit(1000.f, 0.f, Me, Dummy->GetActorLocation(), EMechHitKind::Bullet);
				Check(TEXT("空战 1 离地 +40%、地面 −20%"), bAir && FMath::IsNearlyEqual(V0, 1000.f * GroundDamage, 1.f) && FMath::IsNearlyEqual(V1, 1000.f * AirDamage, 1.f),
					FString::Printf(TEXT("打 1000：地面 %.0f，空中 %.0f（在空中 %d）"), V0, V1, bAir ? 1 : 0));
			}
		});
		AddStep(TEXT("空战 2"), 2.f, [] { Setup(Bit(EMechRunCard::Air1) | Bit(EMechRunCard::Air2), EMechFrame::PhaseMedium); }, [](float T, float Prev) {
			UMechMovementComponent* Move = Me->GetMechMovement();
			if (MECH_AT(0.1f))
			{
				Spawn(EMechGrunt::Gunner, 800.f, 0.f, 300.f, 0.001f);
				Spawn(EMechGrunt::Gunner, 800.f, 300.f, 300.f, 0.001f);
			}
			if (MECH_AT(0.3f))
			{
				Move->Energy = 10.f;
				Run->HitEnemy(G[0].Get(), 100000.f, 0.f, Me->GetActorLocation(), EMechHitKind::Bullet);
				V0 = Run->Debug.EnergyGiven;
				Me->LaunchCharacter(FVector(0.f, 0.f, 1800.f), false, true);
			}
			if (MECH_AT(0.8f))
			{
				const bool bAir = Move->IsFalling();
				const float Before = Move->Energy;
				Run->HitEnemy(G[1].Get(), 100000.f, 0.f, Me->GetActorLocation(), EMechHitKind::Bullet);
				Check(TEXT("空战 2 空中击杀回一次闪避的能量"), bAir && V0 <= 0.f && FMath::IsNearlyEqual(Run->Debug.EnergyGiven, Move->QuickBoostCost, 0.5f),
					FString::Printf(TEXT("地面击杀回 %.1f；空中击杀回 %.1f（一次闪避 %.1f），能量 %.1f → %.1f"), V0, Run->Debug.EnergyGiven, Move->QuickBoostCost, Before, Move->Energy));
			}
		});
	}
}

// ================================================================ 联机（-MechRunNetTest=Host|Client）
// 客户端一段一段往下走（每段先用控制台那条路 ServerCheat 换卡），房主看到客户端的卡变了就把靶子摆到它面前，过几秒看服务器上的计数。
namespace MechRunNetLocal
{
	using namespace MechRun;
	float Clock = 0.f;
	float PhaseT = 0.f;
	int32 Phase = 0;
	int32 Passed = 0;
	int32 Failed = 0;
	bool bQuit = false;
	uint32 SeenOwned = 0;
	float CheckAt = -1.f;
	int32 HostPhase = 0;
	float Number0 = 0.f;
	TArray<TWeakObjectPtr<AMechGrunt>> HostG;

	uint32 BitOf(EMechRunCard C)
	{
		return 1u << static_cast<uint32>(C);
	}

	void NetCheck(const TCHAR* Who, const FString& Name, bool bOk, const FString& Detail)
	{
		(bOk ? Passed : Failed)++;
		UE_LOG(LogTemp, Display, TEXT("[runnet] %s %s %s：%s"), Who, bOk ? TEXT("通过") : TEXT("没通过"), *Name, *Detail);
	}

	void NetShot(const FString& Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/RunNet") / (Name + TEXT(".png")), true, false);
	}

	/** 房主：这台机体前方 Dist、右边 Side、离机体中心 Height */
	AMechGrunt* HostSpawn(AMechDefenseDirector* Dir, const AMechCharacter* M, EMechGrunt Kind, float Dist, float Side, float Height)
	{
		const FRotator Yaw(0.f, M->GetControlRotation().Yaw, 0.f);
		const FVector P = M->GetActorLocation() + Yaw.RotateVector(FVector(Dist, Side, Height));
		AMechGrunt* G = Dir->DebugSpawnGrunt(Kind, P, 100.f, true);
		HostG.Add(G);
		return G;
	}

	void HostTick(UMechRunComponent* HostRun, float Dt)
	{
		UWorld* World = HostRun->GetWorld();
		AMechDefenseDirector* Dir = nullptr;
		for (TActorIterator<AMechDefenseDirector> It(World); It; ++It)
		{
			Dir = *It;
			break;
		}
		if (!Dir)
		{
			return;
		}
		Dir->DebugHoldPrep();
		APlayerController* ClientPC = nullptr;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (It->Get() && !It->Get()->IsLocalController())
			{
				ClientPC = It->Get();
			}
		}
		UMechRunComponent* R = UMechRunComponent::Of(ClientPC);
		AMechCharacter* M = ClientPC ? Cast<AMechCharacter>(ClientPC->GetPawn()) : nullptr;
		if (!R || !M)
		{
			if (bQuit)
			{
				return;
			}
			// 客户端走了：汇总退出
			if (HostPhase > 0)
			{
				bQuit = true;
				UE_LOG(LogTemp, Display, TEXT("[runnet] 房主 汇总：通过 %d 项，没通过 %d 项"), Passed, Failed);
				APlayerController* Me = Cast<APlayerController>(HostRun->GetOwner());
				FTimerHandle H;
				Me->GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Me, [Me]() { Me->ConsoleCommand(TEXT("quit")); }), 1.f, false);
			}
			return;
		}
		Clock += Dt;
		// ---- 第一步：客户端进来 3 秒后给它加经验（杀敌的经验走 AddXP，同一条路）
		if (HostPhase == 0)
		{
			PhaseT += Dt;
			if (PhaseT > 5.f)
			{
				HostPhase = 1;
				R->AddXP(R->GetNextXP() + 5);
				NetCheck(TEXT("房主"), TEXT("客户端升级（服务器算）"), R->GetLevel() == 2 && R->GetPendingPicks() == 1, FString::Printf(TEXT("%d 级，待选 %d"), R->GetLevel(), R->GetPendingPicks()));
				UMechCombatComponent* C = M->GetCombat();
				C->SpreadScale = 0.1f;
			}
			return;
		}
		if (R->GetOwned() != SeenOwned)
		{
			SeenOwned = R->GetOwned();
			const uint32 O = SeenOwned;
			for (const TWeakObjectPtr<AMechGrunt>& G : HostG)
			{
				if (G.IsValid())
				{
					G->DebugKill();
				}
			}
			HostG.Reset();
			R->Debug = FMechRunDebug();
			if (HostPhase == 1 && O != 0)
			{
				FString Name;
				for (int32 i = 0; i < NumCards; ++i)
				{
					if ((O >> i) & 1u)
					{
						Name += Card(static_cast<EMechRunCard>(i)).Name;
					}
				}
				NetCheck(TEXT("房主"), TEXT("客户端按键选卡到了服务器"), true, Name);
				HostPhase = 2;
			}
			if (O == BitOf(EMechRunCard::Flood1))
			{
				HostSpawn(Dir, M, EMechGrunt::Gunner, 1500.f, 0.f, 150.f);
				HostSpawn(Dir, M, EMechGrunt::Gunner, 2200.f, 0.f, 150.f);
				HostSpawn(Dir, M, EMechGrunt::Gunner, 2900.f, 0.f, 150.f);
				CheckAt = Clock + 3.5f;
				HostPhase = 10;
			}
			else if (O == BitOf(EMechRunCard::Rift1))
			{
				HostSpawn(Dir, M, EMechGrunt::Tank, 1200.f, 0.f, -10.f);
				CheckAt = Clock + 4.f;
				HostPhase = 20;
			}
			else if (O == BitOf(EMechRunCard::Charge1))
			{
				CheckAt = Clock + 4.5f;
				HostPhase = 30;
			}
			else if (O == (BitOf(EMechRunCard::Heavy1) | BitOf(EMechRunCard::Heavy2) | BitOf(EMechRunCard::Heavy3)))
			{
				HostSpawn(Dir, M, EMechGrunt::Tank, 2000.f, 0.f, -10.f);
				CheckAt = Clock + 5.f;
				HostPhase = 40;
			}
		}
		if (CheckAt > 0.f && Clock >= CheckAt)
		{
			CheckAt = -1.f;
			const FMechRunDebug& D = R->Debug;
			switch (HostPhase)
			{
			case 10:
				NetCheck(TEXT("房主"), TEXT("客户端的子弹穿透杂兵"), D.MaxPierce >= 2, FString::Printf(TEXT("一发最多打中 %d 个，第二架血量 %.2f%%"), D.MaxPierce, HostG.IsValidIndex(1) && HostG[1].IsValid() ? HostG[1]->GetHPFraction() * 100.f : -1.f));
				break;
			case 20:
				NetCheck(TEXT("房主"), TEXT("客户端的重斩放冲击波"), D.Waves == 1 && D.WaveHits >= 1, FString::Printf(TEXT("%d 道，打中 %d 个"), D.Waves, D.WaveHits));
				break;
			case 30:
				NetCheck(TEXT("房主"), TEXT("客户端按住蓄力、松手砍出去"), D.LastCharge > 0.15f && D.LastReach > 480.f, FString::Printf(TEXT("蓄了 %.0f%%，判定范围 %.1f 米"), D.LastCharge * 100.f, D.LastReach / 100.f));
				break;
			case 40:
				NetCheck(TEXT("房主"), TEXT("客户端的大导弹和火场"), D.SalvoMissiles == 1 && D.ZonesSpawned >= 1 && D.ZoneHits > 0, FString::Printf(TEXT("一轮 %d 枚，半径 %.1f 米，火场 %d 个，跳了 %d 次"), D.SalvoMissiles, D.LastMissileRadius / 100.f, D.ZonesSpawned, D.ZoneHits));
				break;
			default:
				break;
			}
		}
	}

	void ClientTick(UMechRunComponent* Run, float Dt)
	{
		APlayerController* PC = Cast<APlayerController>(Run->GetOwner());
		AMechCharacter* M = PC ? Cast<AMechCharacter>(PC->GetPawn()) : nullptr;
		if (!M || bQuit)
		{
			return;
		}
		UMechCombatComponent* C = M->GetCombat();
		PhaseT += Dt;
		const float T = PhaseT;
		auto Next = [] { ++Phase; PhaseT = 0.f; };
		auto Cheat = [Run](const TCHAR* What) { Run->ServerCheat(What, 0); };
		PC->SetControlRotation(FRotator(0.f, PC->GetControlRotation().Yaw, 0.f));
		switch (Phase)
		{
		case 0:
			// 等加经验（房主那边）
			if (Run->GetOffer().Num() > 0 && Run->GetLevel() >= 2)
			{
				FString Names;
				for (const uint8 X : Run->GetOffer())
				{
					Names += FString(Card(static_cast<EMechRunCard>(X)).Name) + TEXT(" ");
				}
				NetCheck(TEXT("客户端"), TEXT("看到升级和卡"), Run->GetOffer().Num() == 3, FString::Printf(TEXT("%d 级，经验 %d/%d，出卡：%s；装甲上限 %.0f"), Run->GetLevel(), Run->GetXP(), Run->GetNextXP(), *Names, M->GetHealth()->MaxAP));
				NetShot(TEXT("客户端选卡"));
				Next();
			}
			break;
		case 1:
			if (T > 1.f && Number0 == 0.f)
			{
				Number0 = 1.f;
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::One, IE_Pressed, 1.f));
			}
			if (T > 1.1f && Number0 == 1.f)
			{
				Number0 = 2.f;
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::One, IE_Released, 0.f));
			}
			if (Run->GetOwned() != 0)
			{
				NetCheck(TEXT("客户端"), TEXT("按 1 选卡，服务器发回来了"), Run->GetPendingPicks() == 0 && Run->GetOffer().Num() == 0, FString::Printf(TEXT("拿到第 %d 张（%s），待选 %d"), Run->GetLastPicked(), Run->GetLastPicked() >= 0 ? Card(static_cast<EMechRunCard>(Run->GetLastPicked())).Name : TEXT("?"), Run->GetPendingPicks()));
				{
					const float Base = MechCatalog::FrameSpec(M->GetLoadout().Frame).AP;
					NetCheck(TEXT("客户端"), TEXT("等级的装甲上限复制下来了"), FMath::IsNearlyEqual(M->GetHealth()->MaxAP, Base * 1.03f, 1.f), FString::Printf(TEXT("装甲上限 %.0f（这台框架 %.0f × 1.03）"), M->GetHealth()->MaxAP, Base));
				}
				Next();
			}
			else if (T > 8.f)
			{
				NetCheck(TEXT("客户端"), TEXT("按 1 选卡"), false, TEXT("8 秒没收到"));
				Next();
			}
			break;
		case 2:
			// 洪流 1：换卡 → 房主摆靶 → 开枪
			if (T < 0.1f)
			{
				Cheat(TEXT("clear"));
				Cheat(TEXT("flood1"));
				Next();
			}
			break;
		case 3:
			if (Run->Has(EMechRunCard::Flood1) && T > 1.f && !C->IsEngaged())
			{
				C->SetTrigger(true);
			}
			if (T > 2.2f)
			{
				C->SetTrigger(false);
				NetShot(TEXT("客户端_穿透"));
				Next();
			}
			break;
		case 4:
			if (T > 2.f)
			{
				Cheat(TEXT("clear"));
				Cheat(TEXT("rift1"));
				C->SwitchStance();
				Next();
			}
			break;
		case 5:
			if (T > 1.2f && Number0 < 3.f)
			{
				Number0 = 3.f;
				C->MeleePressed();
			}
			if (T > 1.6f && Number0 < 4.f)
			{
				Number0 = 4.f;
				C->MeleeReleased();
			}
			if (T > 2.3f && Number0 < 5.f)
			{
				Number0 = 5.f;
				NetShot(TEXT("客户端_冲击波"));
			}
			if (T > 3.8f)
			{
				const AMechRunFx* Fx = AMechRunFx::Get(M->GetWorld());
				NetCheck(TEXT("客户端"), TEXT("收到冲击波的画面"), Fx && Fx->DebugPlayed >= 1, FString::Printf(TEXT("放过 %d 次"), Fx ? Fx->DebugPlayed : 0));
				Cheat(TEXT("clear"));
				Cheat(TEXT("charge1"));
				Next();
			}
			break;
		case 6:
			if (T > 0.8f && Number0 < 6.f)
			{
				Number0 = 6.f;
				C->MeleePressed();
			}
			if (T > 2.2f && Number0 < 7.f)
			{
				Number0 = 7.f;
				NetCheck(TEXT("客户端"), TEXT("蓄力停在起手（复制下来的）"), C->GetRunChargeHold() != 0 && C->GetMeleeMove() == EMechMelee::Heavy && C->GetMeleeTime() < C->GetMeleeStrikeAt(), FString::Printf(TEXT("蓄力状态 %d，招式时间 %.2f / %.2f"), C->GetRunChargeHold(), C->GetMeleeTime(), C->GetMeleeStrikeAt()));
				NetShot(TEXT("客户端_蓄力"));
			}
			if (T > 2.6f && Number0 < 8.f)
			{
				Number0 = 8.f;
				C->MeleeReleased();
			}
			if (T > 4.5f)
			{
				C->SwitchStance();
				Cheat(TEXT("clear"));
				Cheat(TEXT("heavy3"));
				Next();
			}
			break;
		case 7:
			if (T > 1.5f && Number0 < 9.f)
			{
				Number0 = 9.f;
				C->FireMissiles();
			}
			if (T > 1.9f && Number0 < 10.f)
			{
				Number0 = 10.f;
				int32 Count = 0;
				float Scale = 0.f;
				for (TActorIterator<AMechMissile> It(M->GetWorld()); It; ++It)
				{
					++Count;
					Scale = It->GetActorScale3D().X;
				}
				NetCheck(TEXT("客户端"), TEXT("看到一枚大导弹"), Count == 1 && Scale > 2.f, FString::Printf(TEXT("%d 枚，画面 ×%.1f"), Count, Scale));
			}
			if (T > 3.2f && Number0 < 11.f)
			{
				Number0 = 11.f;
				NetShot(TEXT("客户端_火场"));
			}
			if (T > 6.f)
			{
				UE_LOG(LogTemp, Display, TEXT("[runnet] 客户端 汇总：通过 %d 项，没通过 %d 项"), Passed, Failed);
				bQuit = true;
				FTimerHandle H;
				PC->GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(PC, [PC]() { PC->ConsoleCommand(TEXT("quit")); }), 1.f, false);
			}
			break;
		default:
			break;
		}
	}
}

void MechRunTest::NetTick(UMechRunComponent* InRun, bool bHost, float DeltaTime)
{
	if (bHost)
	{
		MechRunNetLocal::HostTick(InRun, DeltaTime);
	}
	else
	{
		MechRunNetLocal::ClientTick(InRun, DeltaTime);
	}
}

void MechRunTest::Tick(UMechRunComponent* InRun, AMechCharacter* InMe, float DeltaTime)
{
	using namespace MechRunTestLocal;
	if (bDone)
	{
		return;
	}
	Run = InRun;
	Me = InMe;
	Combat = InMe->GetCombat();
	PC = Cast<APlayerController>(InRun->GetOwner());
	if (!Dir)
	{
		for (TActorIterator<AMechDefenseDirector> It(InMe->GetWorld()); It; ++It)
		{
			Dir = *It;
			break;
		}
		if (!Dir)
		{
			return;
		}
	}
	Dir->DebugHoldPrep();
	// 等配装发过来、灯光和机体都稳定了再开始
	Warmup += DeltaTime;
	if (Warmup < 3.f)
	{
		Origin = InMe->GetActorLocation();
		ForwardYaw = InMe->GetActorRotation().Yaw;
		TestFwd = FRotator(0.f, ForwardYaw, 0.f).Vector();
		TestRgt = TestFwd.RotateAngleAxis(90.f, FVector::UpVector);
		return;
	}
	if (Steps.IsEmpty())
	{
		BuildSteps();
		UE_LOG(LogTemp, Display, TEXT("[run] 开始 -MechRunTest：共 %d 步"), Steps.Num());
	}
	const float Prev = StepClock;
	StepClock += DeltaTime;
	if (StepIndex < 0 || StepClock >= Steps[StepIndex].Dur)
	{
		++StepIndex;
		StepClock = 0.f;
		if (!Steps.IsValidIndex(StepIndex))
		{
			bDone = true;
			Combat->SpreadScale = 1.f;
			FString Fails;
			for (const FString& F : FailNames)
			{
				Fails += F + TEXT("；");
			}
			UE_LOG(LogTemp, Display, TEXT("[run] 汇总：通过 %d 项，没通过 %d 项 %s"), NumPassed, NumFailed, *Fails);
			FTimerHandle H;
			PC->GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(PC, [Cap = PC]() { Cap->ConsoleCommand(TEXT("quit")); }), 2.f, false);
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("[run] ---- %s"), *Steps[StepIndex].Name);
		if (Steps[StepIndex].Begin)
		{
			Steps[StepIndex].Begin();
		}
		return;
	}
	if (Steps[StepIndex].Update)
	{
		Steps[StepIndex].Update(StepClock, Prev);
	}
}

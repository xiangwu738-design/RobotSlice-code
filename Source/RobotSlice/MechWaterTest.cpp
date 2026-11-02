// 落水损毁自测（11-02，-MechWaterTest，配 -MechAutoBattle）：开打后在真实对局里挨个试：
//   1 没人打过的红方送到水面上 → 落水，算场地（谁都不加击毁）
//   2 蓝方「打中」一台红方再送到水面上 → 落水，击毁算给那台蓝方
//   3 窗口外（把打中时间改到窗口之前）→ 算场地
//   4 有浅水的图（L_Nature）：蓝方站到岸边浅水（水底在水面下 KillDepth 以内）2 秒 → 不应该损毁；再把它送到水面下 KillDepth 以外 → 损毁
// 日志 [watertest]，结束 quit。
//   UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Pillars?Match?Team=4?Mode=Elim" -game -MechAutoBattle -MechWaterTest -RenderOffscreen -unattended -nosound -nosplash -windowed -ForceRes -ResX=1280 -ResY=720

#include "MechGameMode.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MechAIController.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechGameState.h"
#include "MechHealthComponent.h"
#include "MechHealthComponent.h"
#include "MechMapRules.h"
#include "MechMovementComponent.h"
#include "MechTypes.h"
#include "MechScore.h"

// 飞行手感实测（-MechFlightTest，配 -MechAutoBattle -nullrhi -BENCHMARK -FPS=60）：AI 全部停脑子，一台机体按脚本飞（走玩家那条路：看控制旋转），换三种框架，日志 [flighttest]。
//   平飞：按住 Shift 不动 25 秒 → 速度、能量最低多少、高度掉没掉（这就是「简单操作一直在天上」）
//   爬升：抬头 30° / 55°（看能爬多高、速度掉到多少、能量够不够）
//   俯冲：从 230 米低头 60° 冲 3.5 秒 → 冲到多快；抬平 → 这段速度滑多远才回到平飞速度
//   闪避：平飞中每隔 N 秒闪避一次 → 多频繁才会把能量用光、用光掉多高
//   松手：100 米高松开 Shift → 多久落水
void AMechGameMode::FlightTestTick(float DeltaSeconds)
{
	AMechGameState* GS = GetMechGameState();
	if (!GS || (!bRangeMode && GS->Phase != EMechRoundPhase::Fight) || Mechs.Num() == 0)
	{
		return;
	}
	GS->PhaseEndTime = GetWorld()->GetTimeSeconds() + 999.f;
	struct FCase
	{
		EMechFrame Frame;
		const TCHAR* Name;
	};
	static const FCase Cases[] = {{EMechFrame::Medium, TEXT("新纪中型")}, {EMechFrame::Heavy, TEXT("新纪重型")}, {EMechFrame::PhaseLight, TEXT("相位轻型")}};
	struct FState
	{
		int32 Case = -1;
		int32 Stage = 0;
		float Clock = 0.f;
		float MinE = 100.f;
		float MinZ = 1.e9f;
		float MaxZ = -1.e9f;
		float StartZ = 0.f;
		float Dodge = 0.f;
		int32 Dodges = 0;
		int32 Over = 0;
		bool bWasOver = false;
		float Ang = 0.f;
		bool bInit = false;
		float DodgeGap = 3.f;
		float MaxS = 0.f;
		float SumS = 0.f;
		float NumS = 0.f;
		FVector From = FVector::ZeroVector;
		bool bPulled = false;
		float PullAt = 0.f;
	};
	static FState T;
	AMechCharacter* M = Mechs[0];
	// 测试场模式（"/Game/Maps/L_Pillars?Range"）：试飞玩家自己那台，俯仰走真正的控制旋转（验证玩家那条路）
	const bool bPlayerPath = bRangeMode;
	if (bPlayerPath)
	{
		for (AMechCharacter* Candidate : Mechs)
		{
			if (Candidate && Candidate->IsPlayerControlled())
			{
				M = Candidate;
				break;
			}
		}
	}
	if (!M || T.Case >= 1000)
	{
		return;
	}
	AController* Ctrl = M->GetController();
	if (!T.bInit)
	{
		T.bInit = true;
		AMechAIController::bBrainOffForTest = true;
		for (AMechCharacter* Other : Mechs)
		{
			if (Other)
			{
				Other->SetBoostHeld(false);
				Other->SetJumpThrust(false);
				Other->GetCombat()->SetTrigger(false);
			}
		}
	}
	UMechMovementComponent* Move = M->GetMechMovement();
	auto Reset = [&](float Z) {
		Move->StopMovementImmediately();
		M->TeleportTo(FVector(ArenaCenter.X, ArenaCenter.Y, Z), FRotator::ZeroRotator, false, true);
		Move->Energy = Move->MaxEnergy;
		Move->bOverheated = false;
		Move->SetMovementMode(MOVE_Falling);
		Move->Velocity = FVector(Move->GliderRules ? Move->GliderRules->Cruise * Move->GlideSpeedScale : 1500.f, 0.f, 0.f);
		T.MinE = 100.f;
		T.MinZ = 1.e9f;
		T.MaxZ = -1.e9f;
		T.Dodge = T.DodgeGap;
		T.Dodges = 0;
		T.Over = 0;
		T.bWasOver = false;
		T.Clock = 0.f;
		T.StartZ = Z;
		T.MaxS = 0.f;
		T.SumS = 0.f;
		T.NumS = 0.f;
		T.bPulled = false;
		M->SetBoostHeld(false);
		M->SetJumpThrust(false);
	};
	auto StageZ = [](int32 Stage) { return Stage == 3 ? 23000.f : Stage == 7 ? 10000.f : 15000.f; };
	if (T.Case < 0)
	{
		T.Case = 0;
		T.Stage = 0;
		FMechLoadout L = M->GetLoadout();
		L.Frame = Cases[0].Frame;
		M->SetLoadout(L, true);
		Reset(StageZ(0));
		UE_LOG(LogTemp, Display, TEXT("[flighttest] 开始。回能 %.1f/秒，飞行基础耗能 %.0f/秒"), Move->RegenPerSecond * Move->AirRegenScale, Move->BoostDrain);
	}
	T.Clock += DeltaSeconds;
	const FVector P = M->GetActorLocation();
	const float Speed = M->GetVelocity().Size();
	if (M->GetHealth()->IsDead() && T.Stage != 100)
	{
		UE_LOG(LogTemp, Warning, TEXT("[flighttest] %s 第 %d 段 %.1f 秒：试飞的机体损毁了，结束"), Cases[T.Case].Name, T.Stage, T.Clock);
		GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("quit"));
		T.Case = 1000;
		return;
	}
	{
		static float LastTrace = -10.f;
		static int32 LastStage = -1;
		if (T.Stage != LastStage)
		{
			LastStage = T.Stage;
			LastTrace = -10.f;
		}
		if (T.Clock - LastTrace >= (T.Stage == 4 ? 0.25f : 1.f))
		{
			LastTrace = T.Clock;
			UE_LOG(LogTemp, Display, TEXT("[flighttrace] 段 %d t=%.0f 位置 %s 速度 %s 视线俯仰 %.0f 飞 %d 回能中 能量 %.0f 模式 %d 冲刺 %d"), T.Stage, T.Clock, *(P / 100.f).ToCompactString(), *(M->GetVelocity() / 100.f).ToCompactString(),
				Ctrl ? FRotator::NormalizeAxis(Ctrl->GetControlRotation().Pitch) : 0.f, Move->bGliding ? 1 : 0, Move->Energy, static_cast<int32>(Move->MovementMode), Move->bWantsBoost ? 1 : 0);
		}
	}
	// 测试不想真的落水：掉到离水 15 米就记下来
	T.MinE = FMath::Min(T.MinE, Move->Energy);
	T.MinZ = FMath::Min(T.MinZ, static_cast<float>(P.Z));
	T.MaxZ = FMath::Max(T.MaxZ, static_cast<float>(P.Z));
	T.MaxS = FMath::Max(T.MaxS, Speed);
	T.SumS += Speed;
	T.NumS += 1.f;
	if (Move->bOverheated && !T.bWasOver)
	{
		++T.Over;
	}
	T.bWasOver = Move->bOverheated;
	// 绕场地中央转大圈飞（水平方向慢慢转），别撞隐形墙
	T.Ang += DeltaSeconds * 14.f;
	float Pitch = 0.f;
	bool bShift = true;
	bool bDone = false;
	const TCHAR* Name = Cases[T.Case].Name;
	const float Cruise = Move->GliderRules ? Move->GliderRules->Cruise * Move->GlideSpeedScale : 1500.f;
	if (T.Stage == 100)
	{
		M->SetBoostHeld(false);
		static bool bShot = false;
		if (!bShot && T.Clock > 0.55f)
		{
			bShot = true;
			GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("shot showui"));  // 带 HUD 的截图（HighResShot 不带），存到 Saved/Screenshots/WindowsEditor
			UE_LOG(LogTemp, Display, TEXT("[flighttest] 贴水提示截图：离水 %.1f 米，下落 %.1f 米/秒"), (P.Z - 130.f) / 100.f, -M->GetVelocity().Z / 100.f);
		}
		if (T.Clock > 2.5f)
		{
			GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("quit"));
			T.Case = 1000;
		}
		return;
	}
	switch (T.Stage)
	{
	case 0:
		if (T.Clock > 25.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[flighttest] %s 平飞 25 秒（只按 Shift）：速度 %.1f 米/秒，能量最低 %.0f、结束 %.0f，过热 %d 次，高度 %.1f~%.1f 米"), Name, T.SumS / T.NumS / 100.f, T.MinE, Move->Energy, T.Over, T.MinZ / 100.f, T.MaxZ / 100.f);
			bDone = true;
		}
		break;
	case 1:
	case 2:
		Pitch = T.Stage == 1 ? 30.f : 55.f;
		if (T.Clock > 12.f || Move->bOverheated)
		{
			UE_LOG(LogTemp, Display, TEXT("[flighttest] %s 抬头 %.0f° 爬升 %.1f 秒：爬了 %.0f 米（%.1f 米/秒），速度 %.1f 米/秒，能量结束 %.0f，过热 %d 次"), Name, Pitch, T.Clock, (P.Z - T.StartZ) / 100.f, (P.Z - T.StartZ) / 100.f / T.Clock, Speed / 100.f, Move->Energy, T.Over);
			bDone = true;
		}
		break;
	case 3:
		if (!T.bPulled)
		{
			Pitch = -60.f;
			if (T.Clock > 3.5f)
			{
				T.bPulled = true;
				T.PullAt = T.Clock;
				T.From = P;
				UE_LOG(LogTemp, Display, TEXT("[flighttest] %s 低头 60° 冲 3.5 秒：掉了 %.0f 米，速度 %.1f 米/秒（平飞 %.1f）"), Name, (T.StartZ - P.Z) / 100.f, Speed / 100.f, Cruise / 100.f);
			}
		}
		else if (Speed < Cruise * 1.08f || T.Clock - T.PullAt > 20.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[flighttest] %s 抬平后：%.1f 秒滑了 %.0f 米才回到平飞速度，最低高度 %.0f 米，能量 %.0f"), Name, T.Clock - T.PullAt, FVector::Dist(P, T.From) / 100.f, T.MinZ / 100.f, Move->Energy);
			bDone = true;
		}
		break;
	case 4:
	case 5:
	case 6:
		T.Dodge -= DeltaSeconds;
		if (T.Dodge <= 0.f)
		{
			T.Dodge = T.DodgeGap;
			if (!Move->bOverheated)
			{
				M->QuickBoostToward(FRotator(0.f, T.Ang + 90.f, 0.f).Vector());
				++T.Dodges;
			}
		}
		if (T.Clock > 20.f || P.Z < 1500.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[flighttest] %s 平飞 + 每 %.1f 秒闪避：闪了 %d 次，能量最低 %.0f、结束 %.0f，过热 %d 次，最低高度 %.0f 米（巡航 150 米）%s"), Name, T.DodgeGap, T.Dodges, T.MinE, Move->Energy, T.Over, T.MinZ / 100.f, P.Z < 1500.f ? TEXT("，会落水") : TEXT(""));
			bDone = true;
		}
		break;
	default:
		bShift = false;
		if (P.Z < 200.f || T.Clock > 15.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[flighttest] %s 100 米高松开 Shift：%.1f 秒到水面，那时下落 %.1f 米/秒"), Name, T.Clock, -M->GetVelocity().Z / 100.f);
			bDone = true;
		}
		break;
	}
	M->SetBoostHeld(bShift);
	Move->bGlideAI = !bPlayerPath;  // 俯仰直接给（和玩家那条路只差俯仰的来源：玩家是控制旋转）
	Move->GlideAIPitch = Pitch;
	if (Ctrl)
	{
		Ctrl->SetControlRotation(FRotator(Pitch, T.Ang, 0.f));
	}
	M->AddMovementInput(FRotator(0.f, T.Ang, 0.f).Vector(), 1.f);
	if (bDone)
	{
		++T.Stage;
		if (bPlayerPath && T.Stage > 3)
		{
			// 最后看一眼贴水提示（PULL UP）：送到水面上方 10 米，松开 Shift，下落中截一张带 HUD 的图
			UE_LOG(LogTemp, Display, TEXT("[flighttest] 玩家路径验证结束，拍贴水提示"));
			T.Stage = 100;
			Reset(StageZ(0));
			if (const UMechWater* Water = GetWorld()->GetSubsystem<UMechWater>())
			{
				for (float R = 0.f; R < 60000.f; R += 800.f)
				{
					const FVector Probe = ArenaCenter + FVector(R, R * 0.4f, 0.f);
					if (!Water->IsSafeStep(Probe) && Water->GroundTop(Probe) < -300.f)
					{
						M->TeleportTo(FVector(Probe.X, Probe.Y, 1130.f), FRotator::ZeroRotator, false, true);
						break;
					}
				}
			}
			Move->Velocity = FVector(1200.f, 0.f, -200.f);
			return;
		}
		if (T.Stage > 7)
		{
			T.Stage = 0;
			++T.Case;
			if (T.Case >= UE_ARRAY_COUNT(Cases))
			{
				UE_LOG(LogTemp, Display, TEXT("[flighttest] 结束"));
				GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("quit"));
				T.Case = 1000;
				return;
			}
			FMechLoadout L = M->GetLoadout();
			L.Frame = Cases[T.Case].Frame;
			M->SetLoadout(L, true);
		}
		T.DodgeGap = T.Stage == 4 ? 4.f : T.Stage == 5 ? 2.5f : 1.5f;
		Reset(StageZ(T.Stage));
	}
}

void AMechGameMode::WaterTestTick(float DeltaSeconds)
{
	UMechWater* Water = GetWorld()->GetSubsystem<UMechWater>();
	AMechGameState* GS = GetMechGameState();
	if (!Water || !GS || GS->Phase != EMechRoundPhase::Fight)
	{
		return;
	}
	struct FState
	{
		int32 Step = 0;
		float Clock = 0.f;
		TWeakObjectPtr<AMechCharacter> Probe;
		TWeakObjectPtr<AMechCharacter> By;
		int32 ByKills = 0;
		int32 Pass = 0;
		int32 Fail = 0;
		FVector Shallow = FVector::ZeroVector;
		float WaterZ = 0.f;
		float KillDepth = 0.f;
	};
	static FState T;
	T.Clock += DeltaSeconds;
	// AI 停脑子：不然送到水面上它自己就飞走了（那是另一回事，全 AI 对战里量）
	if (!AMechAIController::bBrainOffForTest)
	{
		AMechAIController::bBrainOffForTest = true;
		for (AMechCharacter* Other : Mechs)
		{
			if (Other)
			{
				Other->SetBoostHeld(false);
				Other->SetJumpThrust(false);
				Other->GetCombat()->SetTrigger(false);
			}
		}
	}
	const float HalfH = 96.f * 1.35f;
	auto Pick = [this](int32 Team) -> AMechCharacter* {
		for (AMechCharacter* M : Mechs)
		{
			if (M && !M->GetHealth()->IsDead() && M->GetTeam() == Team)
			{
				return M;
			}
		}
		return nullptr;
	};
	auto Check = [](const TCHAR* What, bool bOk) {
		UE_LOG(LogTemp, Display, TEXT("[watertest] %s：%s"), What, bOk ? TEXT("通过") : TEXT("没过"));
		(bOk ? T.Pass : T.Fail) += 1;
	};
	// 水面上方一个点：从场地中间找一格不安全的
	auto OverWater = [&]() {
		for (float R = 0.f; R < 60000.f; R += 800.f)
		{
			for (int32 A = 0; A < 12; ++A)
			{
				const FVector P = ArenaCenter + FRotator(0.f, A * 30.f, 0.f).Vector() * R;
				const FMechWaterZone* Zone = Water->ZoneAt(P);
				if (Zone && !Water->IsSafeStep(P) && Water->GroundTop(P) < Zone->Z - Zone->KillDepth - 100.f)
				{
					return FVector(P.X, P.Y, Zone->Z + HalfH + 150.f);
				}
			}
		}
		return FVector::ZeroVector;
	};
	auto SendTo = [](AMechCharacter* M, const FVector& P) {
		M->GetCharacterMovement()->StopMovementImmediately();
		M->TeleportTo(P, M->GetActorRotation(), false, true);
		M->GetCharacterMovement()->Velocity = FVector(0.f, 0.f, -600.f);
	};
	switch (T.Step)
	{
	case 0:
		if (T.Clock > 2.f)
		{
			T.Step = 1;
		}
		break;
	case 1:  // 没人打过 → 场地
	{
		AMechCharacter* R = Pick(1);
		const FVector P = OverWater();
		if (!R || P.IsZero())
		{
			Check(TEXT("找不到红方或水面"), false);
			T.Step = 9;
			break;
		}
		AssistMarks.Remove(R->DisplayName);
		T.Probe = R;
		SendTo(R, P);
		UE_LOG(LogTemp, Display, TEXT("[watertest] 1 %s 送到水面上 %s（没人打过）"), *R->DisplayName, *P.ToCompactString());
		T.Clock = 0.f;
		T.Step = 2;
		break;
	}
	case 2:
		if (T.Clock > 2.5f)
		{
			const AMechCharacter* R = T.Probe.Get();
			Check(TEXT("1 落水损毁（无击毁者）"), !R || R->GetHealth()->IsDead());
			T.Step = GS->Phase == EMechRoundPhase::Fight ? 3 : 9;
		}
		break;
	case 3:  // 蓝方打过 → 算蓝方
	{
		AMechCharacter* R = Pick(1);
		AMechCharacter* B = Pick(0);
		const FVector P = OverWater();
		if (!R || !B)
		{
			UE_LOG(LogTemp, Warning, TEXT("[watertest] 一方没人了，跳过 2"));
			T.Step = 9;
			break;
		}
		AssistMarks.Remove(R->DisplayName);
		NoteContribution(B, R, 120.f, false);
		T.Probe = R;
		T.By = B;
		T.ByKills = Stats.FindOrAdd(B->DisplayName).Kills;
		SendTo(R, P);
		UE_LOG(LogTemp, Display, TEXT("[watertest] 2 %s 被 %s 打中后送到水面上，%s 现在击毁 %d"), *R->DisplayName, *B->DisplayName, *B->DisplayName, T.ByKills);
		T.Clock = 0.f;
		T.Step = 4;
		break;
	}
	case 4:
		if (T.Clock > 2.5f)
		{
			const AMechCharacter* B = T.By.Get();
			const AMechCharacter* R = T.Probe.Get();
			Check(TEXT("2 落水损毁"), !R || R->GetHealth()->IsDead());
			Check(TEXT("2 击毁算给打中它的人"), B && Stats.FindOrAdd(B->DisplayName).Kills == T.ByKills + 1);
			T.Step = GS->Phase == EMechRoundPhase::Fight ? 5 : 9;
		}
		break;
	case 5:  // 窗口外 → 场地
	{
		AMechCharacter* R = Pick(1);
		AMechCharacter* B = Pick(0);
		if (!R || !B)
		{
			T.Step = 9;
			break;
		}
		AssistMarks.Remove(R->DisplayName);
		FMechAssistMark& Mark = AssistMarks.FindOrAdd(R->DisplayName).FindOrAdd(B->DisplayName);
		Mark.Damage = 500.f;
		Mark.DamageTime = GetWorld()->GetTimeSeconds() - MechScore::AssistWindow - 1.f;
		T.Probe = R;
		T.By = B;
		T.ByKills = Stats.FindOrAdd(B->DisplayName).Kills;
		SendTo(R, OverWater());
		T.Clock = 0.f;
		T.Step = 6;
		break;
	}
	case 6:
		if (T.Clock > 2.5f)
		{
			const AMechCharacter* B = T.By.Get();
			const AMechCharacter* R = T.Probe.Get();
			Check(TEXT("3 窗口外不算击毁"), (!R || R->GetHealth()->IsDead()) && B && Stats.FindOrAdd(B->DisplayName).Kills == T.ByKills);
			T.Step = GS->Phase == EMechRoundPhase::Fight ? 7 : 9;
		}
		break;
	case 7:  // 浅水不误杀（只有水底够浅的格子才试）
	{
		AMechCharacter* B = Pick(0);
		if (!B)
		{
			T.Step = 9;
			break;
		}
		const FVector Around = OverWater();
		FVector Found = FVector::ZeroVector;
		for (float R = 0.f; R < 60000.f && Found.IsZero(); R += 200.f)
		{
			for (int32 A = 0; A < 24 && Found.IsZero(); ++A)
			{
				const FVector P = Around + FRotator(0.f, A * 15.f, 0.f).Vector() * R;
				const FMechWaterZone* Zone = Water->ZoneAt(P);
				if (!Zone)
				{
					continue;
				}
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(MechWaterTest), false);
				for (TActorIterator<AActor> It(GetWorld()); It; ++It)
				{
					if (It->GetClass()->GetName().Contains(TEXT("Water")))
					{
						Params.AddIgnoredActor(*It);
					}
				}
				if (GetWorld()->LineTraceSingleByObjectType(Hit, FVector(P.X, P.Y, Zone->Z + 3000.f), FVector(P.X, P.Y, Zone->Z - 3000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
				{
					const float Under = Zone->Z - Hit.ImpactPoint.Z;
					if (Under > Zone->KillDepth * 0.5f && Under < Zone->KillDepth * 0.85f && Hit.ImpactNormal.Z > 0.8f)
					{
						Found = Hit.ImpactPoint;
						T.WaterZ = Zone->Z;
						T.KillDepth = Zone->KillDepth;
					}
				}
			}
		}
		if (Found.IsZero())
		{
			UE_LOG(LogTemp, Display, TEXT("[watertest] 4 这张图没有 KillDepth 以内的浅水，跳过"));
			T.Step = 9;
			break;
		}
		T.Probe = B;
		T.Shallow = Found;
		B->GetCharacterMovement()->StopMovementImmediately();
		B->TeleportTo(Found + FVector(0.f, 0.f, HalfH + 5.f), B->GetActorRotation(), false, true);
		UE_LOG(LogTemp, Display, TEXT("[watertest] 4 %s 站到浅水 %s（水底在水面下 %.0f 厘米，落水线 %.0f）"), *B->DisplayName, *Found.ToCompactString(), T.WaterZ - Found.Z, T.KillDepth);
		T.Clock = 0.f;
		T.Step = 8;
		break;
	}
	case 8:
	{
		AMechCharacter* B = T.Probe.Get();
		// 按住它，2 秒内不能死
		if (B && !B->GetHealth()->IsDead() && T.Clock < 2.f)
		{
			B->TeleportTo(T.Shallow + FVector(0.f, 0.f, HalfH + 5.f), B->GetActorRotation(), false, true);
		}
		if (T.Clock > 2.f)
		{
			Check(TEXT("4 岸边浅水不误杀"), B && !B->GetHealth()->IsDead());
			T.Step = 9;
		}
		break;
	}
	default:
		UE_LOG(LogTemp, Display, TEXT("[watertest] 结束：通过 %d，没过 %d"), T.Pass, T.Fail);
		GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("quit"));
		T.Step = 100;
		break;
	case 100:
		break;
	}
}

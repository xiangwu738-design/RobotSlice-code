// -MechDefenseTest（10-31 保卫战第一版：波次和杂兵）：
//   UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Defense?Defense" -game -MechDefenseTest -RenderOffscreen -ForceRes -ResX=1920 -ResY=1080 -windowed
// 玩家不死。第 1 波：跳过准备，玩家对着最近的杂兵开 8 秒枪（看子弹能不能打死杂兵）。之后每 16 秒一档：把场上敌人全清掉 → 下一波开始 →
// 9 秒时截图（带 HUD）→ 14 秒记一行状态（各种杂兵活着几个、玩家装甲、指挥所、各种敌人打出的伤害）；第 4 波（有炮车）贴近一台炮车砍三刀，
// 看刀能不能伤到杂兵。打完第 10 波应该是胜利，日志 [defensetest]，最后一行汇总，跑完退出。截图在 Saved/Screenshots/Defense/Test。
#include "MechGameMode.h"

#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechDefense.h"
#include "MechGrunt.h"
#include "MechHealthComponent.h"
#include "UnrealClient.h"

namespace MechDefenseTestLocal
{
	TWeakObjectPtr<AMechDefenseDirector> Dir;
	int32 TestPassed = 0;
	int32 TestFailed = 0;
	float TankHPBefore = -1.f;
	TWeakObjectPtr<AMechGrunt> TestTank;

	void TestCheck(const TCHAR* Name, bool bOk, const FString& Detail)
	{
		(bOk ? TestPassed : TestFailed)++;
		UE_LOG(LogTemp, Display, TEXT("[defensetest] %s %s：%s"), bOk ? TEXT("通过") : TEXT("没通过"), Name, *Detail);
	}

	FString TestStatus(AMechDefenseDirector* D, AMechCharacter* Me)
	{
		int32 Alive[4] = {0, 0, 0, 0};
		for (const TWeakObjectPtr<AMechGrunt>& G : D->GetGrunts())
		{
			if (G.IsValid() && G->IsAlive())
			{
				++Alive[(int32)G->GetKind()];
			}
		}
		FString Dmg;
		for (const TPair<FString, float>& P : D->DebugDamage)
		{
			Dmg += FString::Printf(TEXT(" %s %.0f"), *P.Key, P.Value);
		}
		int32 Shown = 0;
		for (const TWeakObjectPtr<AMechGrunt>& G : D->GetGrunts())
		{
			if (G.IsValid() && G->IsAlive() && G->GetKind() == EMechGrunt::Kamikaze && Shown++ < 3)
			{
				UE_LOG(LogTemp, Display, TEXT("[defensetest]   %s"), *G->DebugInfo());
			}
		}
		return FString::Printf(TEXT("第 %d 波 阶段 %d 剩 %d | 活着：自爆 %d 机枪 %d 炮车 %d 迫击炮 %d | 玩家装甲 %.0f%% 指挥所 %.0f%% | 累计击杀 %d（玩家 %d）| 伤害%s"),
			D->Wave, (int32)D->Phase, D->EnemiesLeft, Alive[0], Alive[1], Alive[2], Alive[3], Me->GetHealth()->GetAPFraction() * 100.f,
			D->Core ? D->Core->GetHPFraction() * 100.f : -1.f, D->DebugKills, D->DebugKillsByPlayers, *Dmg);
	}

	AMechGrunt* TestNearest(AMechDefenseDirector* D, const FVector& From, int32 KindFilter = -1)
	{
		AMechGrunt* Best = nullptr;
		float BestD = TNumericLimits<float>::Max();
		for (const TWeakObjectPtr<AMechGrunt>& G : D->GetGrunts())
		{
			if (G.IsValid() && G->IsAlive() && (KindFilter < 0 || (int32)G->GetKind() == KindFilter))
			{
				const float Dd = FVector::Dist(G->GetActorLocation(), From);
				if (Dd < BestD)
				{
					BestD = Dd;
					Best = G.Get();
				}
			}
		}
		return Best;
	}
}

void AMechPlayerController::DefenseTestTick(float DeltaTime)
{
	using namespace MechDefenseTestLocal;
	AMechCharacter* Me = Cast<AMechCharacter>(GetPawn());
	if (!Me || !HasAuthority())
	{
		return;
	}
	if (!Dir.IsValid())
	{
		for (TActorIterator<AMechDefenseDirector> It(GetWorld()); It; ++It)
		{
			Dir = *It;
		}
		if (!Dir.IsValid())
		{
			return;
		}
	}
	AMechDefenseDirector* D = Dir.Get();
	const float T0 = DefenseTestClock;
	DefenseTestClock += DeltaTime;
	const float T = DefenseTestClock;
	auto At = [&](float S) { return T0 < S && T >= S; };
	Me->GetHealth()->bNoDeath = true;
	UMechCombatComponent* Combat = Me->GetCombat();
	auto Shot = [&](const FString& Name) {
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Defense/Test") / (Name + TEXT(".png")), true, false);
	};
	auto Face = [&](const FVector& P) {
		const FRotator R = (P - PlayerCameraManager->GetCameraLocation()).Rotation();
		SetControlRotation(FRotator(FMath::Clamp(R.Pitch, -40.f, 30.f), R.Yaw, 0.f));
	};

	// ---- 第 1 波：跳过准备，对着最近的杂兵开 8 秒枪
	if (At(1.f))
	{
		TestCheck(TEXT("调度器和指挥所"), D->Core != nullptr && D->Phase == EMechDefensePhase::Prep, FString::Printf(TEXT("阶段 %d，指挥所 %s"), (int32)D->Phase,
			D->Core ? *D->Core->GetActorLocation().ToCompactString() : TEXT("没有")));
	}
	if (At(2.f))
	{
		// 测试里玩家站着不防守（只每 16 秒清一次场），指挥所约第 7 波就被打爆（10-31 实测，失败流程也算验证过了）；
		// 这里把指挥所血量调高十倍，好把 10 波跑完、验证胜利流程
		if (D->Core)
		{
			D->Core->MaxHP = D->Core->HP = 250000.f;
		}
		D->DebugSkipPrep();
	}
	if (At(3.f))
	{
		TestCheck(TEXT("第 1 波开始"), D->Wave == 1 && D->Phase == EMechDefensePhase::Fight, FString::Printf(TEXT("第 %d 波，阶段 %d，剩 %d"), D->Wave, (int32)D->Phase, D->EnemiesLeft));
	}
	if (T > 5.f && T < 13.f)
	{
		if (AMechGrunt* G = TestNearest(D, Me->GetActorLocation()))
		{
			Face(G->GetActorLocation());
			Combat->SetTrigger(true);
		}
	}
	if (At(9.f))
	{
		Shot(TEXT("w1_开火"));
	}
	if (At(13.f))
	{
		Combat->SetTrigger(false);
		TestCheck(TEXT("子弹能打死杂兵"), D->DebugKillsByPlayers > 0, FString::Printf(TEXT("玩家 8 秒击杀 %d"), D->DebugKillsByPlayers));
		UE_LOG(LogTemp, Display, TEXT("[defensetest] %s"), *TestStatus(D, Me));
	}
	// ---- 之后每 16 秒一档：清场 → 下一波 → 截图 → 记状态
	if (T < 16.f)
	{
		return;
	}
	const int32 Slot = FMath::FloorToInt((T - 16.f) / 16.f);
	const float S0 = (T0 - 16.f) - Slot * 16.f;
	const float S = (T - 16.f) - Slot * 16.f;
	auto In = [&](float X) { return S0 < X && S >= X; };
	if (In(0.f) || (Slot == 0 && S0 < 0.f))
	{
		// -MechDefenseTestWaves=N：只跑前 N 波（调试用）
		static int32 MaxWaves = 100;
		static const bool bParsed = FParse::Value(FCommandLine::Get(), TEXT("MechDefenseTestWaves="), MaxWaves);
		if (D->Wave >= MaxWaves && D->Phase == EMechDefensePhase::Fight)
		{
			UE_LOG(LogTemp, Display, TEXT("[defensetest] %s"), *TestStatus(D, Me));
			UE_LOG(LogTemp, Display, TEXT("[defensetest] 只跑前 %d 波，退出（%d）"), MaxWaves, bParsed ? 1 : 0);
			DefenseTestClock = -1000.f;
			ConsoleCommand(TEXT("quit"));
			return;
		}
		if (D->Phase == EMechDefensePhase::Won || D->Phase == EMechDefensePhase::Lost)
		{
			TestCheck(TEXT("守满 10 波 = 胜利"), D->Phase == EMechDefensePhase::Won && D->Wave == D->NumWaves, FString::Printf(TEXT("阶段 %d，第 %d 波"), (int32)D->Phase, D->Wave));
			Shot(TEXT("结束"));
			UE_LOG(LogTemp, Display, TEXT("[defensetest] %s"), *TestStatus(D, Me));
			UE_LOG(LogTemp, Display, TEXT("[defensetest] 汇总：通过 %d 项，没通过 %d 项"), TestPassed, TestFailed);
			DefenseTestClock = -1000.f;  // 只汇总一次
			FTimerHandle H;
			GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]() { ConsoleCommand(TEXT("quit")); }), 2.f, false);
			return;
		}
		D->DebugKillAll();
	}
	if (In(0.5f))
	{
		D->DebugSkipPrep();
	}
	if (In(2.f))
	{
		UE_LOG(LogTemp, Display, TEXT("[defensetest] 第 %d 波开始：剩 %d"), D->Wave, D->EnemiesLeft);
	}
	// 第 4 波（有炮车）：贴到一台炮车跟前砍三刀
	if (D->Wave == 4)
	{
		if (In(6.f))
		{
			if (AMechGrunt* Tank = TestNearest(D, Me->GetActorLocation(), (int32)EMechGrunt::Tank))
			{
				TestTank = Tank;
				TankHPBefore = Tank->GetHPFraction();
				const FVector Away = (Me->GetActorLocation() - Tank->GetActorLocation()).GetSafeNormal2D();
				Me->SetActorLocation(Tank->GetActorLocation() + Away * 650.f + FVector(0.f, 0.f, 150.f), false, nullptr, ETeleportType::TeleportPhysics);
				Me->SetActorRotation(FRotator(0.f, (-Away).Rotation().Yaw, 0.f));
				if (Combat->GetStance() == EMechStance::Gun)
				{
					Combat->SwitchStance();
				}
			}
		}
		for (const float K : {7.f, 7.6f, 8.2f})
		{
			if (TestTank.IsValid())
			{
				Face(TestTank->GetActorLocation());
			}
			if (In(K))
			{
				Combat->MeleePressed();
			}
			if (In(K + 0.05f))
			{
				Combat->MeleeReleased();
			}
		}
		if (In(9.5f))
		{
			const float After = TestTank.IsValid() ? TestTank->GetHPFraction() : 0.f;
			TestCheck(TEXT("刀能砍伤炮车"), TankHPBefore > 0.f && After < TankHPBefore, FString::Printf(TEXT("炮车血量 %.0f%% → %.0f%%"), TankHPBefore * 100.f, After * 100.f));
			if (Combat->GetStance() == EMechStance::Blade)
			{
				Combat->SwitchStance();
			}
		}
	}
	else if (S > 3.f && S < 9.f)
	{
		// 镜头朝着最近的敌人（截图里要有东西）
		if (AMechGrunt* G = TestNearest(D, Me->GetActorLocation()))
		{
			Face(G->GetActorLocation());
		}
	}
	if (In(9.f))
	{
		Shot(FString::Printf(TEXT("w%d"), D->Wave));
	}
	if (In(14.f))
	{
		UE_LOG(LogTemp, Display, TEXT("[defensetest] %s"), *TestStatus(D, Me));
		if (D->Wave == 2)
		{
			TestCheck(TEXT("杂兵打得到玩家或指挥所"), D->DebugDamage.Num() > 0, TestStatus(D, Me));
		}
	}
}

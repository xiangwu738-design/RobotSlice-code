// -MechFrameTraitTest（测试场 "/Game/Maps/L_Range?Range"）：三家框架特色（10-31）过一遍（AMechPlayerController::FrameTraitTestTick，声明在 MechGameMode.h）。
//  1 九台框架挨个换上：装甲、移动速度、快速闪避耗能、弹匣、散布 / 锁定倍数和 MechCatalog::FrameSpec、给用户的表一致
//  2 新纪 中型现场抢修：打掉七成装甲，5 秒时还没开始修，8 秒时在修且装甲回升，18 秒时停在一半
//  3 维克托 中型抗干扰：失衡 2 秒后已经恢复（默认 2.6 秒，这里 1.7 秒）；中 EMP（4 秒）2.8 秒后已经过去
//  4 相位 中型怕 EMP：中 EMP（4 秒）4.5 秒后还在，6.5 秒后过去
// 日志 [frametest]（最后一行汇总通过几项），约 32 秒退出。

#include "MechGameMode.h"

#include "EngineUtils.h"
#include "MechCatalog.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechHealthComponent.h"
#include "MechLoadout.h"
#include "MechMovementComponent.h"
#include "MechShoulderComponent.h"
#include "MechWeapons.h"

namespace MechFrameTestLocal
{
	int32 NumPassed = 0;
	int32 NumFailed = 0;

	void Report(const TCHAR* Name, bool bOk, const FString& Detail)
	{
		(bOk ? NumPassed : NumFailed)++;
		UE_LOG(LogTemp, Display, TEXT("[frametest] %s %s：%s"), bOk ? TEXT("通过") : TEXT("没通过"), Name, *Detail);
	}
}

void AMechPlayerController::FrameTraitTestTick(float DeltaTime)
{
	using namespace MechFrameTestLocal;
	AMechCharacter* Me = Cast<AMechCharacter>(GetPawn());
	if (!Me || !HasAuthority())
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
		return;  // 机体和木桩都生成了才开始
	}
	const float T0 = FrameTraitTestClock;
	FrameTraitTestClock += DeltaTime;
	const float T = FrameTraitTestClock;
	auto At = [&](float S) { return T0 < S && T >= S; };
	UMechHealthComponent* Health = Me->GetHealth();
	UMechCombatComponent* Combat = Me->GetCombat();
	UMechShoulderComponent* Shoulder = Me->GetShoulder();
	const UMechMovementComponent* Move = Me->GetMechMovement();
	auto Wear = [&](EMechFrame Frame) {
		FMechLoadout L = Me->GetLoadout();
		L.Frame = Frame;
		ServerApplyLoadout(L);
	};
	static float HurtAt = 0.f;

	if (At(1.f))
	{
		// 给用户的表（10-31）：装甲、速度；顺序 = EMechFrame（每家 中 / 轻 / 重）
		static const float ExpectAP[MechNumFrames] = {10800.f, 8100.f, 14000.f, 10200.f, 7650.f, 13200.f, 9600.f, 7200.f, 12400.f};
		static const float ExpectSpeed[MechNumFrames] = {0.97f, 1.09f, 0.85f, 1.f, 1.12f, 0.88f, 1.04f, 1.16f, 0.92f};
		const UMechMovementComponent* Def = GetDefault<UMechMovementComponent>();
		bool bAll = true;
		for (int32 f = 0; f < MechNumFrames; ++f)
		{
			const EMechFrame Frame = static_cast<EMechFrame>(f);
			Wear(Frame);
			const MechCatalog::FFrameSpec S = MechCatalog::FrameSpec(Frame);
			const float Speed = Move->WalkSpeed / Def->WalkSpeed;
			const int32 BaseMag = Combat->GetGun().Magazine;
			const int32 WantMag = BaseMag > 0 ? FMath::Max(1, FMath::RoundToInt(BaseMag * S.Magazine)) : 0;
			const bool bOk = FMath::IsNearlyEqual(Health->MaxAP, ExpectAP[f], 1.f) && FMath::IsNearlyEqual(Health->MaxAP, S.AP, 1.f)
				&& FMath::IsNearlyEqual(Speed, ExpectSpeed[f], 0.006f) && Combat->GetMagazine() == WantMag
				&& FMath::IsNearlyEqual(Combat->FrameSpreadScale, S.Spread) && FMath::IsNearlyEqual(Combat->FrameLockScale, S.Lock)
				&& FMath::IsNearlyEqual(Shoulder->EmpTimeScale, S.Emp) && FMath::IsNearlyEqual(Health->StaggerTimeScale, S.Disrupt)
				&& Health->bFieldRepair == S.bRepair;
			bAll &= bOk;
			UE_LOG(LogTemp, Display, TEXT("[frametest]   %s：装甲 %.0f（表 %.0f）失衡上限 %.0f 速度 ×%.3f 闪避耗能 %.1f 弹匣 %d/%d 散布 ×%.2f 锁定 ×%.2f EMP ×%.2f 失衡僵直 ×%.2f 抢修 %d %s"),
				MechLoadout::Item(MechLoadout::Frame, f).Name, Health->MaxAP, ExpectAP[f], S.Stagger, Speed, Move->QuickBoostCost, Combat->GetMagazine(), BaseMag,
				Combat->FrameSpreadScale, Combat->FrameLockScale, Shoulder->EmpTimeScale, Health->StaggerTimeScale, Health->bFieldRepair ? 1 : 0, bOk ? TEXT("") : TEXT("← 不对"));
		}
		Report(TEXT("九台框架数值"), bAll, TEXT("逐台见上面"));
		Wear(EMechFrame::Medium);
	}
	if (At(2.f))
	{
		Health->ApplyHit(Health->MaxAP * 0.7f, 0.f, Dummy, Me->GetActorLocation());
		HurtAt = T;
		UE_LOG(LogTemp, Display, TEXT("[frametest] 新纪 中型打掉七成装甲：剩 %.0f / %.0f"), Health->GetAP(), Health->MaxAP);
	}
	if (At(7.f))
	{
		Report(TEXT("抢修要等 6 秒"), !Health->IsRepairing() && Health->GetAPFraction() < 0.305f,
			FString::Printf(TEXT("挨打后 %.1f 秒：在修 %d，装甲 %.1f%%"), T - HurtAt, Health->IsRepairing() ? 1 : 0, Health->GetAPFraction() * 100.f));
	}
	if (At(10.f))
	{
		Report(TEXT("抢修中装甲回升"), Health->IsRepairing() && Health->GetAPFraction() > 0.32f,
			FString::Printf(TEXT("挨打后 %.1f 秒：在修 %d，装甲 %.1f%%（应约 34%%）"), T - HurtAt, Health->IsRepairing() ? 1 : 0, Health->GetAPFraction() * 100.f));
	}
	if (At(20.f))
	{
		Report(TEXT("抢修停在一半"), !Health->IsRepairing() && FMath::IsNearlyEqual(Health->GetAPFraction(), 0.5f, 0.006f),
			FString::Printf(TEXT("挨打后 %.1f 秒：在修 %d，装甲 %.1f%%"), T - HurtAt, Health->IsRepairing() ? 1 : 0, Health->GetAPFraction() * 100.f));
		Wear(EMechFrame::VictorMedium);
	}
	if (At(21.f))
	{
		Health->ApplyHit(0.f, Health->MaxStagger * 2.f, Dummy, Me->GetActorLocation());
		Shoulder->ApplyEmp(4.f);
		HurtAt = T;
	}
	if (At(21.5f))
	{
		Report(TEXT("维克托被打进失衡"), Health->IsStaggered(), TEXT("0.5 秒时应在失衡中"));
	}
	if (At(23.f))
	{
		Report(TEXT("维克托失衡 1.7 秒就恢复"), !Health->IsStaggered(), FString::Printf(TEXT("2 秒时失衡 %d（默认 2.6 秒）"), Health->IsStaggered() ? 1 : 0));
	}
	if (At(23.8f))
	{
		Report(TEXT("维克托 EMP 2.6 秒就过去"), !Shoulder->IsEmped(), FString::Printf(TEXT("2.8 秒时 EMP 中 %d（默认 4 秒）"), Shoulder->IsEmped() ? 1 : 0));
		Wear(EMechFrame::PhaseMedium);
	}
	if (At(24.5f))
	{
		Shoulder->ApplyEmp(4.f);
		HurtAt = T;
	}
	if (At(29.f))
	{
		Report(TEXT("相位 EMP 更久"), Shoulder->IsEmped(), FString::Printf(TEXT("4.5 秒时 EMP 中 %d（应约 6 秒）"), Shoulder->IsEmped() ? 1 : 0));
	}
	if (At(31.f))
	{
		Report(TEXT("相位 EMP 6 秒后过去"), !Shoulder->IsEmped(), FString::Printf(TEXT("6.5 秒时 EMP 中 %d"), Shoulder->IsEmped() ? 1 : 0));
	}
	if (At(31.5f))
	{
		UE_LOG(LogTemp, Display, TEXT("[frametest] 汇总：通过 %d 项，没通过 %d 项"), NumPassed, NumFailed);
		ConsoleCommand(TEXT("quit"));
	}
}

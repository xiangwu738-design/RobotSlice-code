// 保卫战的 HUD（10-31，AMechHUD 的一部分，和 MechHUD.cpp 同一套画法和颜色）：战斗界面不写中文提示，全是数字、线条和形状。
//  上方正中：10 段波次条（打完的实心、正在打的一闪一闪）、下面「3/10」和剩余敌人数（红色小菱形 + 数字）、指挥所血条（挨打闪红、三成以下变红）
//  准备阶段：波次条下面一个大的倒计时数字
//  场上的杂兵：屏幕里 220 米内的，头顶一个红色小菱形（炮车、迫击炮大一号）；刷怪点在 170 米外的空旷处，不标的话远处的小无人机很难找
//  结束：正中一行大字（守住了 / 指挥所被摧毁）和打到第几波

#include "MechHUD.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "MechCharacter.h"
#include "MechDefense.h"
#include "MechGameState.h"
#include "MechGrunt.h"
#include "MechHudTheme.h"
#include "MechScore.h"

namespace MechHudDefenseLocal
{
	FLinearColor Fade(FLinearColor C, float A)
	{
		C.A *= A;
		return C;
	}
}

void AMechHUD::DrawDefense(AMechCharacter* Mech)
{
	using namespace MechHudDefenseLocal;
	if (!DefenseDirector.IsValid())
	{
		for (TActorIterator<AMechDefenseDirector> It(GetWorld()); It; ++It)
		{
			DefenseDirector = *It;
			break;
		}
	}
	const AMechDefenseDirector* D = DefenseDirector.Get();
	if (!D || !PlayerOwner || !PlayerOwner->PlayerCameraManager)
	{
		return;
	}
	const FMechHudTheme& Th = MechHudTheme::Get();
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const float T = GetWorld()->GetTimeSeconds();
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const float Now = GS ? GS->GetServerWorldTimeSeconds() : T;

	// ---- 场上的杂兵：头顶红色小菱形
	const FVector CamLoc = PlayerOwner->PlayerCameraManager->GetCameraLocation();
	for (TActorIterator<AMechGrunt> It(GetWorld()); It; ++It)
	{
		const AMechGrunt* G = *It;
		if (!G->IsAlive() || G->IsHidden())
		{
			continue;
		}
		const FVector At = G->GetActorLocation() + FVector(0.f, 0.f, G->GetKind() == EMechGrunt::Tank ? 450.f : 220.f);
		const float Dist = FVector::Dist(At, CamLoc);
		FVector2D P;
		if (Dist > 22000.f || !UGameplayStatics::ProjectWorldToScreen(PlayerOwner, At, P) || P.X < 0.f || P.X > W || P.Y < 0.f || P.Y > H)
		{
			continue;
		}
		const bool bBig = G->GetKind() == EMechGrunt::Tank || G->GetKind() == EMechGrunt::Mortar;
		const float S = (bBig ? 6.f : 4.f) * UI;
		const FLinearColor C = Fade(Th.Enemy, FMath::GetMappedRangeValueClamped(FVector2D(3000.f, 22000.f), FVector2D(0.95f, 0.5f), Dist));
		Triangle(FVector2D(P.X, P.Y - S), FVector2D(P.X + S, P.Y), FVector2D(P.X - S, P.Y), C);
		Triangle(FVector2D(P.X, P.Y + S), FVector2D(P.X + S, P.Y), FVector2D(P.X - S, P.Y), C);
		// 血厚的（炮车、迫击炮）挨过打的下面画一道细血条
		if (bBig && G->GetHPFraction() < 0.999f)
		{
			Bar(P.X - 14.f * UI, P.Y + S + 2.f * UI, 28.f * UI, 2.f * UI, G->GetHPFraction(), Th.Enemy);
		}
	}

	// ---- 上方：波次条
	const int32 N = FMath::Max(D->NumWaves, 1);
	const float SegW = 20.f * UI;
	const float Gap = 4.f * UI;
	const float Total = N * SegW + (N - 1) * Gap;
	const float X0 = W * 0.5f - Total * 0.5f;
	const float Y0 = 58.f * UI;
	const bool bFight = D->Phase == EMechDefensePhase::Fight;
	for (int32 i = 0; i < N; ++i)
	{
		const int32 WaveNo = i + 1;
		const bool bDone = WaveNo < D->Wave || (WaveNo == D->Wave && !bFight) || D->Phase == EMechDefensePhase::Won;
		const bool bNow = WaveNo == D->Wave && bFight;
		const bool bBoss = WaveNo == 5 || WaveNo == 10;
		FLinearColor C = bDone ? Th.Accent : bNow ? Fade(Th.Accent, 0.55f + 0.45f * FMath::Sin(T * 6.f)) : Th.Track;
		if (bBoss && !bDone && !bNow)
		{
			C = Fade(Th.Gold, 0.5f);  // 首领波：金色的空格
		}
		DrawRect(C, X0 + i * (SegW + Gap), Y0, SegW, (bBoss ? 7.f : 5.f) * UI);
	}
	Text(FString::Printf(TEXT("%d/%d"), FMath::Max(D->Wave, 1), N), W * 0.5f, Y0 + 9.f * UI, 15.f, Th.Ink, 0.5f);
	if (bFight)
	{
		// 剩余敌人：红菱形 + 数字（右边）
		const float RX = X0 + Total + 14.f * UI;
		const float RY = Y0 + 3.f * UI;
		const float S = 5.f * UI;
		Triangle(FVector2D(RX, RY - S), FVector2D(RX + S, RY), FVector2D(RX - S, RY), Th.Enemy);
		Triangle(FVector2D(RX, RY + S), FVector2D(RX + S, RY), FVector2D(RX - S, RY), Th.Enemy);
		Text(FString::FromInt(D->EnemiesLeft), RX + 9.f * UI, RY - 9.f * UI, 15.f, Th.Ink);
	}
	// ---- 指挥所血条：挨打闪红，三成以下一直红
	if (const AMechDefenseCore* C = D->Core)
	{
		const float Frac = C->GetHPFraction();
		const bool bHit = T - C->LastHitTime < 0.25f;
		const FLinearColor Fill = Frac < 0.3f ? Fade(Th.Enemy, 0.7f + 0.3f * FMath::Sin(T * 10.f)) : bHit ? Th.Enemy : Th.Ally;
		const float BW = Total;
		Bar(X0, Y0 + 30.f * UI, BW, 4.f * UI, Frac, Fill);
		// 左头一个小方块 = 指挥所
		DrawRect(Fill, X0 - 10.f * UI, Y0 + 29.f * UI, 6.f * UI, 6.f * UI);
	}
	// ---- 准备阶段：大倒计时
	if (D->Phase == EMechDefensePhase::Prep)
	{
		const int32 Left = FMath::CeilToInt(FMath::Max(0.f, D->PhaseEndTime - Now));
		Text(FString::FromInt(Left), W * 0.5f, Y0 + 44.f * UI, 34.f, Left <= 3 ? Th.Warn : Th.Accent, 0.5f);
	}
	DrawRun(Mech);  // 肉鸽成长（MechHudRun.cpp）
	// ---- 结束
	if (D->Phase == EMechDefensePhase::Won || D->Phase == EMechDefensePhase::Lost)
	{
		const bool bWon = D->Phase == EMechDefensePhase::Won;
		Text(bWon ? TEXT("守住了") : TEXT("指挥所被摧毁"), W * 0.5f, H * 0.36f, 40.f, bWon ? Th.Gold : Th.Enemy, 0.5f);
		Text(FString::Printf(TEXT("%d/%d"), D->Wave, N), W * 0.5f, H * 0.36f + 52.f * UI, 20.f, Th.Ink, 0.5f);
		// 11-01 积分制：保卫战没有结算场景，这里补一行 MVP（玩家里积分最高）+ 积分和最突出的两项（MechScore.h）
		if (const AMechGameState* MGS = GetWorld()->GetGameState<AMechGameState>(); MGS && MGS->Results.Num() > 0)
		{
			const int32 Mvp = MechScore::PickMVP(MGS->Results, 0);
			if (MGS->Results.IsValidIndex(Mvp))
			{
				const FMechResultRow& R = MGS->Results[Mvp];
				const float MY = H * 0.36f + 92.f * UI;
				Text(TEXT("MVP"), W * 0.5f - 12.f * UI, MY, 16.f, Th.Gold, 1.f);
				Text(R.Name, W * 0.5f + 12.f * UI, MY - 2.f * UI, 19.f, Th.Ink, 0.f);
				Text(MechScore::Reason(R, MGS->Results), W * 0.5f, MY + 30.f * UI, 14.f, Th.Dim, 0.5f);
			}
		}
	}
}

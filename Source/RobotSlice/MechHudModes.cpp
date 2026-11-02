// 死斗的 HUD（AMechHUD 的一部分，和 MechHUD.cpp 同一套画法和颜色）：上方比分条、阵亡重生读条、重生保护标记、Tab 计分板。
// 战斗界面不写中文提示：全是数字、线条和形状。名次写成「#2/6」，目标击毁数是进度线正中下方的小数字（两边长到中间 = 到目标）。

#include "MechHUD.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "MechCharacter.h"
#include "MechEffectComponent.h"
#include "MechGameMode.h"
#include "MechGameState.h"
#include "MechHealthComponent.h"
#include "MechHudTheme.h"
#include "MechRules.h"

namespace
{
	// 和 MechHUD.cpp 同一套色：主色 = 自己，红 = 敌人，蓝 = 蓝队，金 = 第一名。
	// 10-31 起是当前 HUD 主题（MechHudTheme.h）成员的引用：比分条、计分板跟着主题变
	const FLinearColor& Ink = MechHudTheme::Get().Ink;
	const FLinearColor& Dim = MechHudTheme::Get().Dim;
	const FLinearColor& Faint = MechHudTheme::Get().Faint;
	const FLinearColor& Accent = MechHudTheme::Get().Accent;
	const FLinearColor& Track = MechHudTheme::Get().Track;
	const FLinearColor& Ally = MechHudTheme::Get().Ally;
	const FLinearColor& Enemy = MechHudTheme::Get().Enemy;
	const FLinearColor& Warn = MechHudTheme::Get().Warn;
	const FLinearColor& Gold = MechHudTheme::Get().Gold;
	const FLinearColor& Guard = MechHudTheme::Get().Guard;

	FLinearColor Fade(FLinearColor C, float A)
	{
		C.A *= A;
		return C;
	}

	/** 个人死斗排名：击毁多的在前，同击毁阵亡少的在前 */
	void SortByKills(TArray<FMechResultRow>& Rows)
	{
		Rows.Sort([](const FMechResultRow& A, const FMechResultRow& B) {
			return A.Kills != B.Kills ? A.Kills > B.Kills : A.Deaths != B.Deaths ? A.Deaths < B.Deaths : A.Name < B.Name;
		});
	}

	/** 名次（同击毁同名次）：1 起 */
	int32 RankOf(const TArray<FMechResultRow>& Rows, int32 Kills)
	{
		int32 Ahead = 0;
		for (const FMechResultRow& R : Rows)
		{
			Ahead += R.Kills > Kills ? 1 : 0;
		}
		return Ahead + 1;
	}
}

// ---------------------------------------------------------------- 上方正中：时间、两边分数、进度线（长到中间 = 到目标）、目标数
void AMechHUD::DrawDMRound()
{
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	if (!GS)
	{
		return;
	}
	const float CX = Canvas->SizeX * 0.5f;
	const float H = Canvas->SizeY;
	const float Remaining = GS->GetPhaseRemaining();
	const bool bFight = GS->Phase == EMechRoundPhase::Fight;
	const AMechCharacter* Mech = GetFocusMech();
	const float Y = 20.f * UI;
	FString Clock = TEXT("--:--");
	if (bFight)
	{
		const int32 Sec = FMath::CeilToInt(Remaining);
		Clock = FString::Printf(TEXT("%d:%02d"), Sec / 60, Sec % 60);
	}
	Readout(Clock, CX, Y, 22.f, bFight && Remaining < 30.f ? Warn : Ink, 0.5f);

	const float LY = Y + 36.f * UI;
	const float Half = 220.f * UI;
	const int32 Target = FMath::Max(1, GS->KillTarget());
	DrawRect(Faint, CX - Half, LY, Half * 2.f, FMath::Max(1.f, UI));
	DrawRect(Dim, CX - 0.5f * UI, LY - 3.f * UI, FMath::Max(1.f, UI), 7.f * UI);  // 正中一道小刻度 = 目标
	Text(FString::FromInt(Target), CX, LY + 6.f * UI, 11.f, Dim, 0.5f);

	int32 Left = 0;
	int32 Right = 0;
	FLinearColor LeftColor = Ally;
	FLinearColor RightColor = Enemy;
	if (GS->IsSoloDM())
	{
		// 个人：左边自己的击毁和名次（青），右边第一名（自己领先时是第二名）的击毁
		TArray<FMechResultRow> Rows = GS->Results;
		SortByKills(Rows);
		const FString MyName = Mech ? Mech->DisplayName : FString();
		const FMechResultRow* Mine = Rows.FindByPredicate([&MyName](const FMechResultRow& R) { return R.Name == MyName; });
		const FMechResultRow* Other = nullptr;
		for (const FMechResultRow& R : Rows)
		{
			if (&R != Mine)
			{
				Other = &R;  // 已经按击毁排好：除了自己以外的第一个 = 领先者（自己第一时就是第二名）
				break;
			}
		}
		Left = Mine ? Mine->Kills : 0;
		Right = Other ? Other->Kills : 0;
		const int32 MyRank = Mine ? RankOf(Rows, Mine->Kills) : Rows.Num();
		const int32 OtherRank = Other ? RankOf(Rows, Other->Kills) : 0;
		LeftColor = MyRank == 1 ? Gold : Accent;
		RightColor = OtherRank == 1 ? Gold : Enemy;
		Readout(FString::FromInt(Left), CX - 66.f * UI, Y - 3.f * UI, 26.f, Accent, 1.f);
		Readout(FString::FromInt(Right), CX + 66.f * UI, Y - 3.f * UI, 26.f, Fade(Ink, 0.8f), 0.f);
		if (Mine)
		{
			Text(FString::Printf(TEXT("#%d/%d"), MyRank, Rows.Num()), CX - 66.f * UI, LY + 6.f * UI, 12.f, LeftColor, 1.f);
		}
		if (Other)
		{
			Text(FString::Printf(TEXT("#%d"), OtherRank), CX + 66.f * UI, LY + 6.f * UI, 12.f, RightColor, 0.f);
		}
	}
	else
	{
		// 团队：两队击毁数（队伍色），存活格（实心活着、空心等重生）
		Left = GS->TeamScore[0];
		Right = GS->TeamScore[1];
		Readout(FString::FromInt(Left), CX - 66.f * UI, Y - 3.f * UI, 26.f, Ally, 1.f);
		Readout(FString::FromInt(Right), CX + 66.f * UI, Y - 3.f * UI, 26.f, Enemy, 0.f);
		int32 Alive[2] = {0, 0};
		for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->GetHealth()->IsDead() && !It->IsTrainingDummy() && It->GetTeam() >= 0 && It->GetTeam() < 2)
			{
				++Alive[It->GetTeam()];
			}
		}
		const float Pip = 7.f * UI;
		for (int32 Team = 0; Team < 2; ++Team)
		{
			const FLinearColor C = Team == 0 ? Ally : Enemy;
			for (int32 i = 0; i < GS->TeamCount[Team]; ++i)  // 11-02 两队台数可以不一样
			{
				const float PX = Team == 0 ? CX - 14.f * UI - (i + 1) * (Pip + 5.f * UI) : CX + 14.f * UI + i * (Pip + 5.f * UI);
				const float PY = LY + 9.f * UI;
				if (i < Alive[Team])
				{
					DrawRect(C, PX, PY, Pip, Pip);
				}
				else
				{
					Box(PX, PY, Pip, Pip, Fade(C, 0.45f), 1.f);
				}
			}
		}
	}
	const float L0 = Half * FMath::Clamp(Left / static_cast<float>(Target), 0.f, 1.f);
	const float L1 = Half * FMath::Clamp(Right / static_cast<float>(Target), 0.f, 1.f);
	DrawRect(LeftColor, CX - Half, LY - 0.5f * UI, L0, 2.f * UI);
	DrawRect(RightColor, CX + Half - L1, LY - 0.5f * UI, L1, 2.f * UI);

	// 开局倒计时：正中大数字
	if (GS->Phase == EMechRoundPhase::Countdown)
	{
		const FString Big = FString::FromInt(FMath::Max(1, FMath::CeilToInt(Remaining)));
		const float BY = H * 0.3f;
		const float BW = Measure(Big, 64.f).X + 120.f * UI;
		DrawRect(Fade(Ink, 0.7f), CX - BW * 0.5f, BY - 12.f * UI, BW, 2.f * UI);
		DrawRect(Fade(Ink, 0.7f), CX - BW * 0.5f, BY + 92.f * UI, BW, 2.f * UI);
		Text(Big, CX, BY, 64.f, Ink, 0.5f);
	}
}

// ---------------------------------------------------------------- 阵亡重生读条、重生保护
void AMechHUD::DrawDeathmatch(AMechCharacter* Mech)
{
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	if (!GS || !PlayerOwner || GS->Phase != EMechRoundPhase::Fight)
	{
		return;
	}
	const float Now = GS->GetServerWorldTimeSeconds();
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const float CX = W * 0.5f;
	const float CY = H * 0.5f;
	const FName GuardId = MechRules::SpawnGuardId();

	// 阵亡：正中一圈重生读条 + 剩几秒（和夺芯一样）
	if (Mech && Mech->GetHealth()->IsDead())
	{
		if (const FMechRespawn* R = GS->RespawnOf(Mech))
		{
			const float Left = FMath::Max(0.f, R->RespawnAt - Now);
			const float Total = FMath::Max(0.1f, R->RespawnAt - R->DiedAt);
			Arc(CX, CY, 46.f * UI, 0.f, 2.f * PI, Track, 2.f);
			Arc(CX, CY, 46.f * UI, 0.f, 2.f * PI * FMath::Clamp(1.f - Left / Total, 0.f, 1.f), Accent, 2.f);
			const FString S = FString::FromInt(FMath::CeilToInt(Left));
			Text(S, CX, CY - Measure(S, 40.f).Y * 0.5f, 40.f, Ink, 0.5f);
		}
		return;
	}

	// 自己在重生保护中：准星外一圈白色细环（六段），按剩余时间收短，轻轻呼吸
	if (Mech && Mech->GetEffects())
	{
		for (const FMechEffect& E : Mech->GetEffects()->GetEffects())
		{
			if (E.Id != GuardId || E.Duration <= 0.f)
			{
				continue;
			}
			const float Frac = FMath::Clamp((E.EndTime - Now) / E.Duration, 0.f, 1.f);
			const float Breath = 0.65f + 0.35f * FMath::Sin(GetWorld()->GetTimeSeconds() * 7.f);
			const float R = 64.f * UI;
			for (int32 k = 0; k < 6; ++k)
			{
				const float From = k * PI / 3.f + 0.08f;
				const float To = (k + 1) * PI / 3.f - 0.08f;
				const float Cut = 2.f * PI * Frac;
				if (From < Cut)
				{
					Arc(CX, CY, R, From, FMath::Min(To, Cut), Fade(Guard, Breath), 2.f);
				}
			}
		}
	}

	// 敌人在重生保护中：头顶一个淡白六边形（这时打它不掉装甲）
	const FVector CamLoc = PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		const AMechCharacter* Other = *It;
		if (Other == Mech || Other->IsTrainingDummy() || Other->GetHealth()->IsDead() || !Other->GetEffects() || !Other->GetEffects()->HasEffect(GuardId))
		{
			continue;
		}
		const FVector Head = Other->GetActorLocation() + FVector(0.f, 0.f, 290.f);
		if (FVector::Dist(Head, CamLoc) > 15000.f)
		{
			continue;
		}
		FVector2D P;
		if (UGameplayStatics::ProjectWorldToScreen(PlayerOwner, Head, P) && P.X > 0.f && P.X < W && P.Y > 0.f && P.Y < H)
		{
			PluginIcon(P.X, P.Y, 10.f * UI, GuardId, 0.75f, 1.5f);
		}
	}
}

// ---------------------------------------------------------------- 按住 Tab：死斗计分板（列头只用缩写：KO 击毁、DTH 阵亡、DMG 输出）
void AMechHUD::DrawDMScoreboard()
{
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	if (!GS)
	{
		return;
	}
	const bool bSolo = GS->IsSoloDM();
	TArray<FMechResultRow> Rows = GS->Results;
	if (bSolo)
	{
		SortByKills(Rows);
	}
	else
	{
		// 11-01 积分制（MechScore.h）：团队死斗队内按积分排（个人死斗名次照旧按击毁，用户定的「点数就是击杀数」）
		Rows.Sort([](const FMechResultRow& A, const FMechResultRow& B) {
			return A.Team != B.Team ? A.Team < B.Team : A.Score != B.Score ? A.Score > B.Score : A.Kills != B.Kills ? A.Kills > B.Kills : A.Deaths < B.Deaths;
		});
	}
	TMap<FString, const AMechCharacter*> ByName;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->GetHealth()->IsDead() || !ByName.Contains(It->DisplayName))
		{
			ByName.Add(It->DisplayName, *It);  // 重生前旧机体还在：优先活着的那台
		}
	}
	const AMechCharacter* Focus = GetFocusMech();
	const FString MyName = Focus ? Focus->DisplayName : FString();
	const float PW = 780.f * UI;  // 11-01：多了 SCORE、AST 两列，700 → 780
	const float RowH = 32.f * UI;
	const float PH = 96.f * UI + Rows.Num() * RowH;
	const float X = (Canvas->SizeX - PW) * 0.5f;
	const float Y = (Canvas->SizeY - PH) * 0.42f;
	DrawRect(Fade(MechHudTheme::Get().Shade, 0.62f), X, Y, PW, PH);
	DrawRect(Accent, X, Y, PW, 2.f * UI);
	if (bSolo)
	{
		// 个人：目标击毁数（金色小菱形 + 数字）
		const float DX = X + PW * 0.5f;
		const float DY = Y + 30.f * UI;
		const float S = 6.f * UI;
		Triangle(FVector2D(DX - 28.f * UI, DY - S), FVector2D(DX - 28.f * UI + S, DY), FVector2D(DX - 28.f * UI - S, DY), Gold);
		Triangle(FVector2D(DX - 28.f * UI, DY + S), FVector2D(DX - 28.f * UI + S, DY), FVector2D(DX - 28.f * UI - S, DY), Gold);
		Text(FString::FromInt(GS->KillTarget()), DX - 14.f * UI, Y + 16.f * UI, 26.f, Gold, 0.f);
	}
	else
	{
		Text(FString::FromInt(GS->TeamScore[0]), X + PW * 0.5f - 18.f * UI, Y + 16.f * UI, 26.f, Ally, 1.f);
		Text(TEXT(":"), X + PW * 0.5f, Y + 16.f * UI, 26.f, Dim, 0.5f);
		Text(FString::FromInt(GS->TeamScore[1]), X + PW * 0.5f + 18.f * UI, Y + 16.f * UI, 26.f, Enemy, 0.f);
		Text(FString::Printf(TEXT("/%d"), GS->KillTarget()), X + PW * 0.5f + 70.f * UI, Y + 26.f * UI, 13.f, Dim, 0.f);
	}
	// 11-01 积分制：加 SCORE 积分（MVP 按它挑）、AST 助攻；个人死斗的名次和金色第一名仍按 KO
	const float Cols[] = {28.f, 270.f, 360.f, 460.f, 540.f, 620.f, 710.f};
	static const TCHAR* Heads[] = {TEXT(""), TEXT("AP"), TEXT("SCORE"), TEXT("KO"), TEXT("AST"), TEXT("DTH"), TEXT("DMG")};
	float RY = Y + 62.f * UI;
	for (int32 c = 1; c < 7; ++c)
	{
		Text(Heads[c], X + Cols[c] * UI, RY, 11.f, Dim);
	}
	RY += 24.f * UI;
	for (const FMechResultRow& R : Rows)
	{
		const bool bMe = R.Name == MyName;
		const FLinearColor C = bSolo ? (bMe ? Accent : Ink) : (R.Team == 0 ? Ally : Enemy);
		const AMechCharacter* const* Found = ByName.Find(R.Name);
		const bool bDead = Found && (*Found)->GetHealth()->IsDead();
		DrawRect(Fade(C, bMe ? 0.9f : 0.6f), X + 14.f * UI, RY + 2.f * UI, 2.f * UI, RowH - 10.f * UI);
		FString Name = R.Name;
		if (bSolo)
		{
			Name = FString::Printf(TEXT("%d  %s"), RankOf(Rows, R.Kills), *R.Name);
		}
		Text(Name, X + Cols[0] * UI, RY, 16.f, bDead ? Fade(C, 0.45f) : C);
		if (Found)
		{
			FString APText = FString::Printf(TEXT("%.0f"), (*Found)->GetHealth()->GetAP());
			if (bDead)
			{
				const FMechRespawn* Wait = GS->RespawnOf(*Found);
				APText = Wait ? FString::Printf(TEXT("%.0fs"), FMath::CeilToFloat(FMath::Max(0.f, Wait->RespawnAt - GS->GetServerWorldTimeSeconds()))) : FString(TEXT("—"));
			}
			Text(APText, X + Cols[1] * UI, RY, 16.f, bDead ? Faint : Ink);
		}
		Text(FString::FromInt(R.Score), X + Cols[2] * UI, RY, 16.f, Ink);
		Text(FString::FromInt(R.Kills), X + Cols[3] * UI, RY, 16.f, bSolo && RankOf(Rows, R.Kills) == 1 && R.Kills > 0 ? Gold : Ink);
		Text(FString::FromInt(R.Assists), X + Cols[4] * UI, RY, 16.f, Ink);
		Text(FString::FromInt(R.Deaths), X + Cols[5] * UI, RY, 16.f, Ink);
		Text(FString::FromInt(R.Dealt), X + Cols[6] * UI, RY, 16.f, Dim);
		RY += RowH;
	}
}

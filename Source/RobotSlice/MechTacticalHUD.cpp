// 肩部的紧急脱离、弹射升空、震荡波、地雷这四件的 HUD 小件（10-31 做的时候是独立的战术装备，单独有一套图标和冷却位；11-01 并入肩部，
// 冷却和图标位就是肩部那一格，在 MechHUD.cpp 的 DrawStrip / DrawDash）。照现有 HUD 的风格——只画细线、几何小图标和数字，不写中文提示。
//  DrawTacticalIcon：这四件的图标；
//  DrawTacticalMines：地雷在场上还有几颗（小格）；
//  DrawTactical：震荡波蓄力时准星外一圈橙色读条。

#include "MechHUD.h"

#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "MechCharacter.h"
#include "MechHealthComponent.h"
#include "MechHudTheme.h"
#include "MechMine.h"
#include "MechTacticalComponent.h"

void AMechHUD::DrawTacticalIcon(EMechShoulder Type, float X, float Y, float S, const FLinearColor& C)
{
	// 紧急脱离两道 »，弹射升空朝上的三角 + 地面线，震荡波一点 + 一圈 + 两段外弧，地雷扁梯形 + 顶上小灯
	switch (Type)
	{
	case EMechShoulder::Burst:
		SoftLine(X - S, Y - S * 0.8f, X - S * 0.2f, Y, C, 1.5f);
		SoftLine(X - S * 0.2f, Y, X - S, Y + S * 0.8f, C, 1.5f);
		SoftLine(X - S * 0.1f, Y - S * 0.8f, X + S * 0.7f, Y, C, 1.5f);
		SoftLine(X + S * 0.7f, Y, X - S * 0.1f, Y + S * 0.8f, C, 1.5f);
		break;
	case EMechShoulder::Launch:
		Triangle(FVector2D(X, Y - S), FVector2D(X + S * 0.75f, Y + S * 0.15f), FVector2D(X - S * 0.75f, Y + S * 0.15f), C);
		DrawRect(C, X - S, Y + S * 0.7f, S * 2.f, 1.5f * UI);
		break;
	case EMechShoulder::Shockwave:
		DrawRect(C, X - 1.5f * UI, Y - 1.5f * UI, 3.f * UI, 3.f * UI);
		Arc(X, Y, S * 0.55f, 0.f, 2.f * PI, C, 1.2f);
		Arc(X, Y, S, -0.9f, 0.9f, C, 1.2f);
		Arc(X, Y, S, PI - 0.9f, PI + 0.9f, C, 1.2f);
		break;
	default:
		SoftLine(X - S, Y + S * 0.5f, X + S, Y + S * 0.5f, C, 1.5f);
		SoftLine(X - S, Y + S * 0.5f, X - S * 0.6f, Y, C, 1.5f);
		SoftLine(X + S, Y + S * 0.5f, X + S * 0.6f, Y, C, 1.5f);
		SoftLine(X - S * 0.6f, Y, X + S * 0.6f, Y, C, 1.5f);
		DrawRect(C, X - 1.5f * UI, Y - S * 0.55f, 3.f * UI, 3.f * UI);
		break;
	}
}

void AMechHUD::DrawTacticalMines(AMechCharacter* Mech, float X, float Y, const FLinearColor& On)
{
	int32 Count = 0;
	for (TActorIterator<AMechMine> It(GetWorld()); It; ++It)
	{
		if (It->IsActive() && It->GetOwnerMech() == Mech)
		{
			++Count;
		}
	}
	const int32 Limit = Mech->GetTactical()->MineLimit();
	for (int32 i = 0; i < Limit; ++i)
	{
		DrawRect(i < Count ? On : MechHudTheme::Get().Track, X + i * 12.f * UI, Y, 9.f * UI, 3.f * UI);
	}
}

void AMechHUD::DrawTactical(AMechCharacter* Mech)
{
	if (!Mech || !Canvas || Mech->GetHealth()->IsDead() || !Mech->GetTactical())
	{
		return;
	}
	// 蓄满就炸；对面看得到的是机体发橙光和地上的圈
	const float Charge = Mech->GetTactical()->GetCharge();
	if (Charge >= 0.f)
	{
		const FLinearColor Warn = MechHudTheme::Get().Warn;
		FLinearColor Faded = Warn;
		Faded.A *= 0.3f;
		const float CX = Canvas->SizeX * 0.5f;
		const float CY = Canvas->SizeY * 0.5f;
		Arc(CX, CY, 30.f * UI, 0.f, 2.f * PI, Faded, 1.f);
		Arc(CX, CY, 30.f * UI, 0.f, 2.f * PI * Charge, Warn, 2.f);
	}
}

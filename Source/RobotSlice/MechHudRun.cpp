// 保卫战肉鸽成长的 HUD（11-01，AMechHUD 的一部分，见 MechRun.h）。用户要 HUD 科幻简约：克制、不挡视野，颜色全走主题（MechHudTheme::Get()），换框架跟着变。
//  仪表盘正上方：等级数字 + 一道细经验条，还有几次没选（×N）
//  有卡可选时经验条上面一排小卡（最多 3 张）：左上数字键、右上「步枪 · 洪流 1/3」、卡名、一句规则；左边一道细色条表示哪件。不暂停，打得忙可以攒着
//  按住 Tab：同一个位置换成「已拿到的卡」：带着的每件武器和框架一列，每条路径几步走了几步（实心方块），下面写最新那一步的规则
//  导弹带了蜂群：锁杂兵时杂兵身上一个框（锁上变实），全锁时视野内所有敌人一个小框
// 战斗界面平时不写字的规矩不破：只有选卡、看卡时才有字。

#include "MechHUD.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "MechCharacter.h"
#include "MechHudTheme.h"
#include "MechRun.h"
#include "MechSettings.h"

namespace MechHudRunLocal
{
	FLinearColor Fade(FLinearColor C, float A)
	{
		C.A *= A;
		return C;
	}

	/** 哪件的色条：主题里挑四种，一眼分得开 */
	FLinearColor ItemColor(EMechRunItem Item, const FMechHudTheme& Th)
	{
		switch (Item)
		{
		case EMechRunItem::Rifle:
			return Th.Accent;
		case EMechRunItem::GreatSword:
			return Th.Warn;
		case EMechRunItem::Missiles:
			return Th.Gold;
		default:
			return Th.Heal;
		}
	}
}

void AMechHUD::DrawRun(AMechCharacter* Mech)
{
	using namespace MechHudRunLocal;
	const UMechRunComponent* Run = UMechRunComponent::Of(PlayerOwner);
	if (!Run || !Mech)
	{
		return;
	}
	const FMechHudTheme& Th = MechHudTheme::Get();
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const float CX = W * 0.5f;
	const float RT = GetWorld()->GetRealTimeSeconds();

	// ---- 等级和经验：仪表盘（H − 108）正上方一道细条
	const float BarW = 300.f * UI;
	const float BarY = H - 122.f * UI;
	const float BarX = CX - BarW * 0.5f;
	const float Frac = float(Run->GetXP()) / FMath::Max(Run->GetNextXP(), 1);
	DrawRect(Fade(Th.Track, 0.8f), BarX, BarY, BarW, 2.f * UI);
	DrawRect(Th.Accent, BarX, BarY, BarW * FMath::Clamp(Frac, 0.f, 1.f), 2.f * UI);
	Text(FString::Printf(TEXT("LV %d"), Run->GetLevel()), BarX - 10.f * UI, BarY - 9.f * UI, 15.f, Th.Ink, 1.f);
	if (Run->GetPendingPicks() > 1)
	{
		Text(FString::Printf(TEXT("×%d"), Run->GetPendingPicks()), BarX + BarW + 10.f * UI, BarY - 9.f * UI, 15.f, Th.Gold);
	}

	const float RowBottom = BarY - 12.f * UI;
	const bool bTab = MechKeys::IsDown(PlayerOwner, EMechKey::Scoreboard);
	if (bTab)
	{
		// ---- 按住 Tab：已拿到的卡。带着的每件一列（没带的不画，换回来还在）
		struct FCol
		{
			EMechRunItem Item;
			const TCHAR* Name;
			EMechRunCard Paths[2];
			int32 NumPaths;
		};
		static const FCol Cols[] = {
			{EMechRunItem::Rifle, TEXT("步枪"), {EMechRunCard::Flood1, EMechRunCard::Burst1}, 2},
			{EMechRunItem::GreatSword, TEXT("大剑"), {EMechRunCard::Rift1, EMechRunCard::Charge1}, 2},
			{EMechRunItem::Missiles, TEXT("导弹"), {EMechRunCard::Swarm1, EMechRunCard::Heavy1}, 2},
			{EMechRunItem::FrameNewEra, TEXT("新纪框架"), {EMechRunCard::Repair1, EMechRunCard::Repair1}, 1},
			{EMechRunItem::FrameVictor, TEXT("维克托框架"), {EMechRunCard::Supply1, EMechRunCard::Supply1}, 1},
			{EMechRunItem::FramePhase, TEXT("相位框架"), {EMechRunCard::Air1, EMechRunCard::Air1}, 1},
		};
		TArray<const FCol*> Shown;
		for (const FCol& C : Cols)
		{
			if (Run->ItemEquipped(C.Item))
			{
				Shown.Add(&C);
			}
		}
		const float ColW = 236.f * UI;
		const float Gap = 8.f * UI;
		const float PanelH = 128.f * UI;
		const float Total = Shown.Num() * ColW + FMath::Max(0, Shown.Num() - 1) * Gap;
		const float Y0 = RowBottom - PanelH;
		float X = CX - Total * 0.5f;
		for (const FCol* C : Shown)
		{
			const FLinearColor Col = ItemColor(C->Item, Th);
			DrawRect(Fade(Th.Shade, 0.82f), X, Y0, ColW, PanelH);
			DrawRect(Col, X, Y0, ColW, 2.f * UI);
			Text(C->Name, X + 10.f * UI, Y0 + 8.f * UI, 15.f, Col);
			float PY = Y0 + 32.f * UI;
			for (int32 p = 0; p < C->NumPaths; ++p)
			{
				const MechRun::FCard& First = MechRun::Card(C->Paths[p]);
				Text(First.PathName, X + 10.f * UI, PY, 13.f, Th.Ink);
				// 一步一个小方块：拿了实心，没拿空框
				int32 Latest = -1;
				for (int32 s = 0; s < First.Steps; ++s)
				{
					const EMechRunCard Step = static_cast<EMechRunCard>(static_cast<int32>(C->Paths[p]) + s);
					const float SX = X + 62.f * UI + s * 12.f * UI;
					const float SY = PY + 5.f * UI;
					if (Run->Has(Step))
					{
						DrawRect(Col, SX, SY, 8.f * UI, 8.f * UI);
						Latest = s;
					}
					else
					{
						Box(SX, SY, 8.f * UI, 8.f * UI, Fade(Th.Dim, 0.7f), 1.f);
					}
				}
				if (Latest >= 0)
				{
					const EMechRunCard Step = static_cast<EMechRunCard>(static_cast<int32>(C->Paths[p]) + Latest);
					Text(MechRun::Card(Step).Rule, X + 10.f * UI, PY + 20.f * UI, 12.f, Fade(Th.Ink, 0.8f));
				}
				PY += 45.f * UI;
			}
			X += ColW + Gap;
		}
	}
	else
	{
		// ---- 选卡：最多 3 张，数字键选
		const TArray<uint8>& Offer = Run->GetOffer();
		const float CardW = 252.f * UI;
		const float CardH = 78.f * UI;
		const float Gap = 10.f * UI;
		const float Total = Offer.Num() * CardW + FMath::Max(0, Offer.Num() - 1) * Gap;
		const float Y0 = RowBottom - CardH;
		float X = CX - Total * 0.5f;
		for (int32 i = 0; i < Offer.Num(); ++i)
		{
			const MechRun::FCard& Card = MechRun::Card(static_cast<EMechRunCard>(Offer[i]));
			const FLinearColor Col = ItemColor(Card.Item, Th);
			DrawRect(Fade(Th.Shade, 0.82f), X, Y0, CardW, CardH);  // 底要够暗：卡下面就是地面和爆炸，淡了规则那行看不清
			DrawRect(Col, X, Y0, 3.f * UI, CardH);
			// 左上数字键（小方框）
			const float KeyS = 18.f * UI;
			Box(X + 11.f * UI, Y0 + 8.f * UI, KeyS, KeyS, Th.Ink, 1.f);
			Text(FString::FromInt(i + 1), X + 11.f * UI + KeyS * 0.5f, Y0 + 8.f * UI, 14.f, Th.Ink, 0.5f);
			// 右上：哪件 · 哪条路径 第几步
			Text(FString::Printf(TEXT("%s · %s %d/%d"), Card.ItemName, Card.PathName, Card.Step, Card.Steps), X + CardW - 10.f * UI, Y0 + 9.f * UI, 12.f, Col, 1.f);
			Text(Card.Name, X + 11.f * UI, Y0 + 30.f * UI, 17.f, Th.Ink);
			Text(Card.Rule, X + 11.f * UI, Y0 + 54.f * UI, 13.f, Fade(Th.Ink, 0.82f));
			X += CardW + Gap;
		}
		// 刚选的卡：经验条上面闪一下卡名，1.2 秒淡出（手上没有下一组卡时才画）
		if (Offer.IsEmpty() && Run->GetLastPicked() >= 0 && RT - Run->GetLastPickedTime() < 1.2f)
		{
			const float A = FMath::Clamp((1.2f - (RT - Run->GetLastPickedTime())) / 0.4f, 0.f, 1.f);
			const MechRun::FCard& Card = MechRun::Card(static_cast<EMechRunCard>(Run->GetLastPicked()));
			Text(Card.Name, CX, RowBottom - 22.f * UI, 15.f, Fade(ItemColor(Card.Item, Th), A), 0.5f);
		}
	}

	// ---- 导弹：蜂群的锁定框
	if (!PlayerOwner->PlayerCameraManager)
	{
		return;
	}
	auto Mark = [&](const AActor* A, float Half, const FLinearColor& C, float Thick) {
		FVector2D P;
		if (A && UGameplayStatics::ProjectWorldToScreen(PlayerOwner, A->GetActorLocation(), P) && P.X > 0.f && P.X < W && P.Y > 0.f && P.Y < H)
		{
			Brackets(P.X, P.Y, Half, Half * 0.5f, C, Thick);
		}
	};
	if (Run->Active(EMechRunCard::Swarm3))
	{
		// 全锁：视野内所有敌人一个小框（HUD 不用每帧查，0.2 秒查一次）
		const float T = GetWorld()->GetTimeSeconds();
		if (T - RunMarksTime > 0.2f)
		{
			RunMarksTime = T;
			RunMarks.Reset();
			TArray<AActor*> All;
			Run->CollectAllInView(PlayerOwner->PlayerCameraManager->GetCameraLocation(), PlayerOwner->PlayerCameraManager->GetCameraRotation().Vector(), All);
			for (AActor* A : All)
			{
				RunMarks.Add(A);
			}
		}
		for (const TWeakObjectPtr<AActor>& A : RunMarks)
		{
			Mark(A.Get(), 13.f * UI, Fade(Th.Danger, 0.9f), 1.5f);
		}
	}
	else if (const AActor* L = Run->GetLockActor(); L && Run->Active(EMechRunCard::Swarm1))
	{
		const float P = Run->GetLockProgress();
		Mark(L, FMath::Lerp(34.f, 18.f, P) * UI, P >= 1.f ? Th.Danger : Fade(Th.Caution, 0.85f), P >= 1.f ? 2.f : 1.f);
	}
}

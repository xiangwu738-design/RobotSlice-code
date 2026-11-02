// 彩蛋的 HUD（11-02，AMechHUD 的一部分，和 MechHUD.cpp 同一套画法和颜色；彩蛋说明见 MechEaster.h）：
//  无人机、狗群的狗：头顶和机体一样的红色倒三角（小一号）；准星附近的能锁定，锁定框、导弹锁定进度、血条、距离和锁机体一样，
//    框右上角多一个品牌色的小菱形（无人机红、狗青），看得出锁的不是机体。机体锁定优先：锁着机体时不锁它们。
//  苏联的狗：队友色的小三角（友方），不能锁定。
//  全场提示（压力板被踩下、狗群出笼）：上方一条，5 秒淡出。

#include "MechHUD.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechEaster.h"
#include "MechEasterBot.h"
#include "MechHealthComponent.h"
#include "MechHudTheme.h"
#include "MechSmoke.h"
#include "MechTypes.h"
#include "Sound/SoundBase.h"

namespace MechHudEasterLocal
{
	FLinearColor Fade(FLinearColor C, float A)
	{
		C.A *= A;
		return C;
	}
}

void AMechHUD::DrawEaster(AMechCharacter* Mech)
{
	using namespace MechHudEasterLocal;
	UWorld* World = GetWorld();
	UMechEasterSubsystem* Sub = World ? World->GetSubsystem<UMechEasterSubsystem>() : nullptr;
	if (!Sub || !Mech || !PlayerOwner || !PlayerOwner->PlayerCameraManager || !Canvas)
	{
		return;
	}
	const FMechHudTheme& Th = MechHudTheme::Get();
	const float ScreenW = Canvas->SizeX;
	const float ScreenH = Canvas->SizeY;
	const float Now = World->GetTimeSeconds();
	const float Dt = World->GetDeltaSeconds();
	const FVector CamLoc = PlayerOwner->PlayerCameraManager->GetCameraLocation();
	const FVector CamDir = PlayerOwner->PlayerCameraManager->GetCameraRotation().Vector();

	// ---- 全场提示
	const float Age = Now - Sub->AnnounceTime;
	if (Age >= 0.f && Age < 5.f && !Sub->AnnounceText.IsEmpty())
	{
		const float A = FMath::Clamp(Age / 0.25f, 0.f, 1.f) * FMath::Clamp((5.f - Age) / 0.6f, 0.f, 1.f);
		const float Y = ScreenH * 0.2f;
		const float BW = 700.f * UI;
		DrawRect(Fade(Th.Plate, 0.6f * A), ScreenW * 0.5f - BW * 0.5f, Y - 8.f * UI, BW, 40.f * UI);
		DrawRect(Fade(Th.Warn, A), ScreenW * 0.5f - BW * 0.5f, Y - 8.f * UI, BW, 2.f * UI);
		Text(Sub->AnnounceText, ScreenW * 0.5f, Y, 20.f, Fade(Th.Warn, A), 0.5f);
		if (Sub->AnnounceSounded != Sub->AnnounceTime && LockSound)
		{
			Sub->AnnounceSounded = Sub->AnnounceTime;
			UGameplayStatics::PlaySound2D(this, LockSound, 0.6f, 0.7f);
		}
	}

	// ---- 能不能锁：自己操控的这台、活着、没被 EMP、没锁着机体（机体优先）
	UMechCombatComponent* Combat = Mech->GetCombat();
	const bool bCanLock = Mech == PlayerOwner->GetPawn() && Mech->IsLocallyControlled() && Combat && !Combat->GetTarget() && !Mech->GetHealth()->IsDead() && !Mech->IsEmped();
	const float Range = Combat ? Combat->GetLockRange() : 26000.f;
	auto AngleTo = [&CamLoc, &CamDir](const FVector& P) {
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp((P - CamLoc).GetSafeNormal() | CamDir, -1.f, 1.f)));
	};
	auto Visible = [World, Mech, &CamLoc](const AActor* A, const FVector& P) {
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(MechEasterLockLOS), false, Mech);
		Q.AddIgnoredActor(A);
		return !World->LineTraceSingleByObjectType(Hit, CamLoc, P, FCollisionObjectQueryParams(ECC_WorldStatic), Q) && !AMechSmoke::BlocksLine(World, CamLoc, P);
	};
	AActor* Keep = bCanLock ? Sub->LocalLock.Get() : nullptr;
	if (Keep)
	{
		const AMechEasterBot* KB = Cast<AMechEasterBot>(Keep);
		const FVector KP = KB ? KB->GetVisualCenter() : FVector::ZeroVector;
		if (!MechEaster::IsLockable(Keep) || !KB || AngleTo(KP) >= MechEaster::LockKeepCone || FVector::Dist(KP, CamLoc) > Range || !Visible(Keep, KP))
		{
			Keep = nullptr;
		}
	}
	AActor* Best = Keep;
	float BestAngle = Keep ? AngleTo(Cast<AMechEasterBot>(Keep)->GetVisualCenter()) * 0.6f : MechEaster::LockCone;

	// ---- 头顶标记 + 找锁定目标
	for (TActorIterator<AMechEasterBot> It(World); It; ++It)
	{
		AMechEasterBot* B = *It;
		if (!B->IsAlive() || B->IsHidden())
		{
			continue;
		}
		const bool bHostile = B->IsHostile();
		const FVector P = B->GetVisualCenter();
		const float Dist = FVector::Dist(P, CamLoc);
		FVector2D S;
		const float MaxDist = bHostile ? 20000.f : 6000.f;
		const FVector Head = P + FVector(0.f, 0.f, B->GetKind() == EMechEasterKind::Drone ? 110.f : 90.f);
		if (Dist < MaxDist && UGameplayStatics::ProjectWorldToScreen(PlayerOwner, Head, S) && S.X > 0.f && S.X < ScreenW && S.Y > 0.f && S.Y < ScreenH)
		{
			// 敌对的：和机体一样的红色倒三角，小一号；苏联的狗：队友色（友方）
			const float Sz = (bHostile ? 5.f : 4.f) * UI;
			const FLinearColor C = Fade(bHostile ? Th.Enemy : Th.Ally, FMath::GetMappedRangeValueClamped(FVector2D(2000.f, MaxDist), FVector2D(0.85f, 0.35f), Dist));
			Triangle(FVector2D(S.X - Sz, S.Y - Sz), FVector2D(S.X + Sz, S.Y - Sz), FVector2D(S.X, S.Y + Sz * 0.6f), C);
		}
		if (!bCanLock || !bHostile || B == Keep || Dist > Range)
		{
			continue;
		}
		const float A = AngleTo(P);
		if (A < BestAngle && Visible(B, P))
		{
			Best = B;
			BestAngle = A;
		}
	}
	if (Best != Sub->LocalLock.Get())
	{
		Sub->LocalLock = Best;
		Sub->LocalLockProgress = 0.f;
	}
	// 导弹锁定进度：配装是导弹时才有（同机体）
	const bool bMissiles = Mech->GetLoadout().Shoulder == EMechShoulder::Missiles;
	Sub->LocalLockProgress = Best && bMissiles ? FMath::Min(1.f, Sub->LocalLockProgress + Dt / MechEaster::LockTime) : 0.f;

	// ---- 锁定框
	const AMechEasterBot* T = Cast<AMechEasterBot>(Best);
	const bool bLocked = T && Sub->LocalLockProgress >= 1.f;
	if (T)
	{
		FVector2D P;
		const FVector At = T->GetVisualCenter();
		if (UGameplayStatics::ProjectWorldToScreen(PlayerOwner, At, P))
		{
			const float Lock = Sub->LocalLockProgress;
			const float Dist = FVector::Dist(At, Mech->GetActorLocation());
			const float Half = FMath::Clamp(7000.f / FMath::Max(Dist, 100.f), 16.f, 70.f) * UI;
			const float BoxHalf = Half * (1.6f - 0.6f * Lock);
			const FLinearColor C = bLocked ? Th.Enemy : Fade(Th.Ink, 0.9f);
			Brackets(P.X, P.Y, BoxHalf, 10.f * UI, C, 1.5f);
			if (bLocked)
			{
				Brackets(P.X, P.Y, BoxHalf + 6.f * UI, 8.f * UI, C, 1.f);
			}
			// 框右上角的小菱形：品牌色（无人机维克托红、狗相位青），表示锁的不是机体
			const FLinearColor Brand = T->GetKind() == EMechEasterKind::Drone ? FLinearColor(1.f, 0.12f, 0.06f, 0.95f) : FLinearColor(0.1f, 0.85f, 1.f, 0.95f);
			const float DX = P.X + BoxHalf + 7.f * UI;
			const float DY = P.Y - BoxHalf;
			const float DS = 4.f * UI;
			Triangle(FVector2D(DX, DY - DS), FVector2D(DX + DS, DY), FVector2D(DX - DS, DY), Brand);
			Triangle(FVector2D(DX, DY + DS), FVector2D(DX + DS, DY), FVector2D(DX - DS, DY), Brand);
			// 框下面：血条（红）和距离
			const float BW = 90.f * UI;
			const float TX = P.X - BW * 0.5f;
			const float TY = P.Y + BoxHalf + 10.f * UI;
			DrawRect(Fade(Th.Enemy, 0.2f), TX, TY, BW, 3.f * UI);
			DrawRect(Th.Enemy, TX, TY, BW * T->GetHPFraction(), 3.f * UI);
			Text(FString::Printf(TEXT("%.0fm"), Dist / 100.f), P.X, TY + 6.f * UI, 13.f, C, 0.5f);
		}
	}
	if (bLocked && !Sub->bLocalLocked && LockSound)
	{
		UGameplayStatics::PlaySound2D(this, LockSound, 0.5f);
	}
	Sub->bLocalLocked = bLocked;
}

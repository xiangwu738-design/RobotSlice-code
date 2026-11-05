#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

/** 武器发光和刀光共用第三栏点缀色；机体敌我灯仍走队伍色。 */
namespace MechWeaponGlow
{
	inline bool IsWeapon(const UMeshComponent* Mesh)
	{
		const UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Mesh);
		return Static && Static->GetStaticMesh() && Static->GetStaticMesh()->GetName().StartsWith(TEXT("SM_RF_"));
	}
	inline bool IsGlowSlot(FName Slot)
	{
		return Slot == TEXT("MI_Robot_Accent") || Slot == TEXT("RF_Brand") || Slot == TEXT("RF_Lens") || Slot == TEXT("RF_Original");
	}
	inline FLinearColor EffectColor(FColor Accent)
	{
		const FLinearColor Color(Accent);
		const float Peak = FMath::Max3(Color.R, Color.G, Color.B);
		return Peak > SMALL_NUMBER ? FLinearColor(Color.R / Peak, Color.G / Peak, Color.B / Peak, 1.f) : FLinearColor::Black;
	}
	inline FLinearColor CoreColor(const FLinearColor& Color)
	{
		return Color.IsAlmostBlack() ? FLinearColor::Black : FMath::Lerp(Color, FLinearColor::White, .25f) * 1.8f;
	}
	inline void Apply(UMeshComponent* Mesh, FColor Accent)
	{
		if (!IsWeapon(Mesh)) return;
		const TArray<FName> Slots = Mesh->GetMaterialSlotNames();
		for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
		{
			if (!IsGlowSlot(Slots[Slot])) continue;
			if (UMaterialInstanceDynamic* MID = Mesh->CreateAndSetMaterialInstanceDynamic(Slot))
			{
				MID->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(Accent));
				MID->SetVectorParameterValue(TEXT("HeatColor"), EffectColor(Accent));
				// 原贴图只沿原遮罩发光；新灯条/镜头使用独立小面积槽。
				MID->SetScalarParameterValue(TEXT("EmissiveStrength"), Slots[Slot] == TEXT("RF_Original") ? 8.f : 16.f);
				if (Slots[Slot] != TEXT("RF_Original")) MID->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(Accent));
			}
		}
	}
}

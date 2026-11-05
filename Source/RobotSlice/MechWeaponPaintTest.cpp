// -MechWeaponPaintTest：测试场 -nullrhi 下逐把换装、交替换色顺序，检查左右手三色与固定材质。
// 不写玩家设置，不截图；结果在 Saved/Automation/WeaponPaint.json。
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechMeleeFx.h"
#include "MechWeaponGlow.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"

namespace MechWeaponPaintChecks
{
	bool Run(float DeltaSeconds)
	{
		static float Elapsed = 0.f;
		Elapsed += DeltaSeconds;
		AMechCharacter* Subject = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				APlayerController* Controller = World && World->IsGameWorld() ? World->GetFirstPlayerController() : nullptr;
				if (Controller) Subject = Cast<AMechCharacter>(Controller->GetPawn());
				if (Subject) break;
			}
		}
		if (!Subject)
		{
			if (Elapsed < 90.f) return true;
			UE_LOG(LogTemp, Error, TEXT("[weaponpaint] FAIL: no player pawn"));
			FPlatformMisc::RequestExitWithStatus(false, 1);
			return false;
		}
		int32 Checks = 0, Failures = 0;
		auto Verify = [&](bool Passed, const FString& Detail) {
			++Checks;
			if (!Passed) { ++Failures; UE_LOG(LogTemp, Error, TEXT("[weaponpaint] %s"), *Detail); }
		};
		const FMechLoadout OriginalLoadout = Subject->GetLoadout();
		const FMechPaint OriginalPaint = Subject->GetPaint();
		const EMechStance OriginalStance = Subject->GetCombat()->GetStance();
		const FColor Schemes[4][3] = {
			{FColor(0x5a,0x63,0x6c), FColor(0x38,0x3f,0x46), FColor(0xcf,0x5f,0x1b)},
			{FColor(0xa6,0x92,0x70), FColor(0x50,0x48,0x3d), FColor(0xce,0x6b,0x30)},
			{FColor(0x46,0x65,0x84), FColor(0x26,0x34,0x44), FColor(0x63,0x37,0xef)},
			{FColor(0x46,0x65,0x84), FColor(0x26,0x34,0x44), FColor::Black}};
		const FName PaintSlots[] = {TEXT("MI_Robot_Paint"),TEXT("MI_Robot_PaintDark"),TEXT("MI_Robot_Accent")};
		for (int32 Scheme = 0; Scheme < 4; ++Scheme)
		{
			FMechPaint Selected;
			Selected.Primary = Schemes[Scheme][0]; Selected.Secondary = Schemes[Scheme][1]; Selected.Accent = Schemes[Scheme][2];
			for (int32 Step = 0; Step < 14; ++Step)
			{
				FMechLoadout Load = OriginalLoadout;
				Load.Shooter = static_cast<decltype(Load.Shooter)>(Step % 6);
				Load.Melee = static_cast<decltype(Load.Melee)>(Step % 7);
				if (Step % 2 == 0) Subject->SetPaint(Selected);
				Subject->SetLoadout(Load, false);
				if (Step % 2 != 0) Subject->SetPaint(Selected);
				TInlineComponentArray<UStaticMeshComponent*> Components;
				Subject->GetComponents(Components);
				int32 Weapons = 0;
				for (UStaticMeshComponent* Component : Components)
				{
					const FString Name = Component->GetName();
					if (Name != TEXT("RifleMesh") && Name != TEXT("BladeMesh") && Name != TEXT("OffGunMesh") && Name != TEXT("OffBladeMesh") && Name != TEXT("KnifeMeshes")) continue;
					++Weapons;
					UStaticMesh* Mesh = Component->GetStaticMesh();
					Verify(Mesh && Mesh->GetName().StartsWith(TEXT("SM_RF_")), Name + TEXT(" did not use new mesh"));
					for (int32 Role = 0; Role < 3; ++Role)
					{
						const int32 Slot = Component->GetMaterialIndex(PaintSlots[Role]);
						UMaterialInstanceDynamic* Dynamic = Slot >= 0 ? Cast<UMaterialInstanceDynamic>(Component->GetMaterial(Slot)) : nullptr;
						Verify(Dynamic && Dynamic->K2_GetVectorParameterValue(TEXT("BaseColor")).Equals(FLinearColor(Schemes[Scheme][Role]), .0001f),
							FString::Printf(TEXT("%s scheme=%d step=%d role=%d"), *Name, Scheme, Step, Role));
					}
					if (Mesh)
					{
						const TArray<FName> Names = Component->GetMaterialSlotNames();
						for (int32 Slot = 0; Slot < Names.Num(); ++Slot)
						{
							if (MechWeaponGlow::IsGlowSlot(Names[Slot]))
							{
								UMaterialInstanceDynamic* Glow = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(Slot));
								Verify(Glow && Glow->K2_GetVectorParameterValue(TEXT("Emissive")).Equals(FLinearColor(Selected.Accent), .0001f)
									&& Glow->K2_GetScalarParameterValue(TEXT("EmissiveStrength")) > 0.f,
									FString::Printf(TEXT("%s slot=%s glow=%s expected=%s strength=%.3f"), *Name, *Names[Slot].ToString(),
										Glow ? *Glow->K2_GetVectorParameterValue(TEXT("Emissive")).ToString() : TEXT("no MID"),
										*FLinearColor(Selected.Accent).ToString(), Glow ? Glow->K2_GetScalarParameterValue(TEXT("EmissiveStrength")) : -1.f));
							}
							else if (Names[Slot].ToString().StartsWith(TEXT("RF_")))
								Verify(Component->GetMaterial(Slot) == Mesh->GetMaterial(Slot), Name + TEXT(" fixed material was tinted"));
						}
					}
				}
				Verify(Weapons == 5, TEXT("Missing main/offhand/thrown weapon components"));
				TInlineComponentArray<UPointLightComponent*> Lights;
				Subject->GetComponents(Lights);
				int32 WeaponLights = 0;
				for (UPointLightComponent* Light : Lights)
				{
					if (!Light->GetName().EndsWith(TEXT("GlowLight"))) continue;
					++WeaponLights;
					Verify(Light->GetLightColor().Equals(FLinearColor(Selected.Accent), .01f) && !Light->CastShadows
						&& Light->AttenuationRadius <= 140.f, TEXT("Weapon light color or budget mismatch"));
				}
				Verify(WeaponLights == 4, TEXT("Missing weapon lights"));
				UMechCombatComponent* Combat = Subject->GetCombat();
				Combat->SetStanceNow(EMechStance::Blade);
				if (Load.Melee == EMechMeleeWeapon::Dagger) Combat->RequestDaggerSlash(); else Combat->RequestLight();
				UMechMeleeFx* Fx = Combat->GetMeleeFx();
				Verify(Fx != nullptr && Combat->IsMeleeActive(), TEXT("Melee effect test did not start"));
				if (Fx)
				{
					Fx->OnSwingStart(Step % 2 ? EMechMelee::Heavy : EMechMelee::Light1, false, Step % 2 != 0);
					Fx->TickComponent(.016f, LEVELTICK_All, nullptr);
				}
				TInlineComponentArray<UMeshComponent*> Effects;
				Subject->GetComponents(Effects);
				int32 Trails = 0;
				for (UMeshComponent* Effect : Effects)
				{
					const FName EffectName = Effect->GetFName();
					const bool bArc = EffectName == TEXT("BladeArcR") || EffectName == TEXT("BladeArcL");
					const bool bStreak = EffectName == TEXT("KnifeStreaks") || EffectName == TEXT("PowerStreaks");
					if (!bArc && !bStreak) continue;
					++Trails;
					FLinearColor Expected = MechWeaponGlow::EffectColor(Selected.Accent) * (EffectName == TEXT("BladeArcL") ? .85f : 1.f);
					Expected.A = 1.f;
					UMaterialInstanceDynamic* Trail = Cast<UMaterialInstanceDynamic>(Effect->GetMaterial(0));
					Verify(Trail && Trail->K2_GetVectorParameterValue(bArc ? TEXT("Color") : TEXT("Emissive")).Equals(Expected, .0001f),
						Effect->GetName() + TEXT(" trail did not follow accent"));
					if (bArc && Selected.Accent == FColor::Black)
						Verify(Trail && Trail->K2_GetVectorParameterValue(TEXT("CoreColor")).IsAlmostBlack(), TEXT("Black accent produced a white trail core"));
				}
				Verify(Trails == 4, TEXT("Missing blade/knife trails"));
			}
		}
		Subject->SetLoadout(OriginalLoadout, false); Subject->SetPaint(OriginalPaint);
		Subject->GetCombat()->SetStanceNow(OriginalStance);
		const FString Folder = FPaths::ProjectSavedDir() / TEXT("Automation");
		IFileManager::Get().MakeDirectory(*Folder, true);
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"checks\":%d,\"failures\":%d,\"loadout_changes\":56,\"palettes\":4,\"glow_and_trails\":true}"), Checks, Failures), *(Folder / TEXT("WeaponPaint.json")));
		UE_LOG(LogTemp, Display, TEXT("[weaponpaint] %s checks=%d failures=%d loadouts=56 palettes=4 glow_and_trails=true"), Failures ? TEXT("FAIL") : TEXT("PASS"), Checks, Failures);
		FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
		return false;
	}
	FDelayedAutoRegisterHelper Register(EDelayedRegisterRunPhase::EndOfEngineInit, [] {
		if (FParse::Param(FCommandLine::Get(), TEXT("MechWeaponPaintTest")))
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Run), .5f);
	});
}

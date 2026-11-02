#include "MechMapRules.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MechCharacter.h"
#include "MechGameState.h"
#include "MechMaps.h"

namespace MechMapRules
{
	float HeatScale(const UWorld* World) { return World && World->GetMapName().EndsWith(TEXT("L_Soviet")) ? 0.7f : 1.f; }
	float EnergyRegenScale(const UWorld* World) { return World && World->GetMapName().EndsWith(TEXT("L_Soviet")) ? 0.75f : 1.f; }

	// ---------------------------------------------------------------- 想调石柱山水（石林）的飞行手感只改这里
	// 11-02 用户：能量回复提升、推进耗能不减；俯冲飞行；推进器固定成专用款，操作简单就能一直在天上飞。实测数字见 -MechFlightTest（MechWaterTest.cpp）和 README「石柱山水」。
	static FFlight MakePillars()
	{
		FFlight F;
		F.bAirMap = true;
		F.bFixedBooster = true;
		F.BoosterName = TEXT("石林飞行器");
		F.BoosterDesc = TEXT("石林专用，每个人都一样。空中按住 Shift 就沿视线飞，低头俯冲越冲越快，抬头把速度换成高度；回能很快，正常飞不会掉下去");
		F.FlameColor = FLinearColor(0.25f, 1.f, 0.72f);
		F.AirRegen = 1.7f;
		return F;
	}

	const FFlight& FlightFor(const UWorld* World)
	{
		static const FFlight Normal;
		static const FFlight Pillars = MakePillars();
		return World && World->GetMapName().EndsWith(TEXT("L_Pillars")) ? Pillars : Normal;
	}

	namespace
	{
		bool GBoosterLocked = false;
		const FFlight* GLockedFlight = nullptr;
	}

	bool IsBoosterLocked()
	{
		return GBoosterLocked;
	}

	const FFlight& LockedFlight()
	{
		static const FFlight Normal;
		return GLockedFlight ? *GLockedFlight : Normal;
	}

	void UpdateBoosterLock(const UWorld* World)
	{
		const FFlight* F = World ? &FlightFor(World) : nullptr;
		if (!F || !F->bFixedBooster)
		{
			F = nullptr;
			// 还没进图：房间里选的是这张图，配装页也要锁（MechMaps 的表和地图名对上）
			if (const AMechGameState* GS = World ? World->GetGameState<AMechGameState>() : nullptr)
			{
				static const FFlight Pillars = MakePillars();
				if (FString(MechMaps::Get(GS->Room.Map).Path).EndsWith(TEXT("L_Pillars")))
				{
					F = &Pillars;
				}
			}
		}
		GBoosterLocked = F != nullptr;
		GLockedFlight = F;
	}
}

void UMechWater::ScanZones()
{
	bScanned = true;
	Zones.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag(TEXT("WaterKill")))
		{
			continue;
		}
		FMechWaterZone Z;
		const FVector L = It->GetActorLocation();
		Z.Z = L.Z;
		Z.Center = FVector2D(L.X, L.Y);
		for (const FName& T : It->Tags)
		{
			const FString S = T.ToString();
			if (S.StartsWith(TEXT("KillDepth=")))
			{
				Z.KillDepth = FCString::Atof(*S.Mid(10));
			}
			else if (S.StartsWith(TEXT("HalfX=")))
			{
				Z.Half.X = FCString::Atof(*S.Mid(6));
				Z.bBounded = true;
			}
			else if (S.StartsWith(TEXT("HalfY=")))
			{
				Z.Half.Y = FCString::Atof(*S.Mid(6));
				Z.bBounded = true;
			}
		}
		Zones.Add(Z);
		UE_LOG(LogTemp, Display, TEXT("[water] 水面 %.0f 厘米，落水深度 %.0f 厘米%s"), Z.Z, Z.KillDepth,
			Z.bBounded ? *FString::Printf(TEXT("，范围 %.0f×%.0f 米"), Z.Half.X / 50.f, Z.Half.Y / 50.f) : TEXT("，整张图"));
	}
}

bool UMechWater::HasWater()
{
	if (!bScanned)
	{
		ScanZones();
	}
	return Zones.Num() > 0;
}

const FMechWaterZone* UMechWater::ZoneAt(const FVector& P)
{
	if (!HasWater())
	{
		return nullptr;
	}
	for (const FMechWaterZone& Z : Zones)
	{
		if (!Z.bBounded || (FMath::Abs(P.X - Z.Center.X) <= Z.Half.X && FMath::Abs(P.Y - Z.Center.Y) <= Z.Half.Y))
		{
			return &Z;
		}
	}
	return nullptr;
}

bool UMechWater::IsDrowned(const AMechCharacter* Mech)
{
	if (!Mech)
	{
		return false;
	}
	const FVector L = Mech->GetActorLocation();
	const FMechWaterZone* Z = ZoneAt(L);
	if (!Z)
	{
		return false;
	}
	const float Feet = L.Z - Mech->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	return Feet < Z->Z - Z->KillDepth;
}

bool UMechWater::OverWater(const FVector& Feet, const AActor* Ignore, float& OutClear)
{
	const FMechWaterZone* Z = ZoneAt(Feet);
	if (!Z)
	{
		return false;
	}
	OutClear = Feet.Z - Z->Z;
	// 只看水面以上：中间有地面就是站在 / 飞在陆地上方
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechWaterProbe), false, Ignore);
	return !GetWorld()->LineTraceSingleByObjectType(Hit, Feet + FVector(0.f, 0.f, 30.f), FVector(Feet.X, Feet.Y, Z->Z - Z->KillDepth * 0.5f),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params);
}

void UMechWater::BuildGrid(const FVector& Center, const FVector2D& Extent)
{
	Cells.Reset();
	if (!HasWater())
	{
		return;
	}
	const double T0 = FPlatformTime::Seconds();
	// 4 米一格（实测 8 米一格、9000 格只要 0.03 秒）；场地外放宽 30 米（AI 被打飞到场地外也有格子查）
	Cell = 400.f;
	const FVector2D Ext = Extent + FVector2D(3000.f, 3000.f);
	GridMin = FVector2D(Center.X, Center.Y) - Ext;
	NX = FMath::CeilToInt(Ext.X * 2.f / Cell);
	NY = FMath::CeilToInt(Ext.Y * 2.f / Cell);
	Cells.SetNum(NX * NY);
	// 从场地最高处往下打：水面（L_Nature 的 WaterBody 自己可能带碰撞）不算地面
	float MaxZ = 0.f;
	float MinZ = 0.f;
	bool bFirst = true;
	for (const FMechWaterZone& Z : Zones)
	{
		MaxZ = bFirst ? Z.Z : FMath::Max(MaxZ, Z.Z);
		MinZ = bFirst ? Z.Z - Z.KillDepth : FMath::Min(MinZ, Z.Z - Z.KillDepth);
		bFirst = false;
	}
	const float TraceTop = FMath::Max(MaxZ + 40000.f, Center.Z + 40000.f);
	const float TraceBottom = MinZ - 3000.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechWaterGrid), false);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->GetClass()->GetName().Contains(TEXT("WaterBody")) || It->GetClass()->GetName().Contains(TEXT("WaterZone")))
		{
			Params.AddIgnoredActor(*It);
		}
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	int32 Land = 0;
	int32 Safe = 0;
	for (int32 Y = 0; Y < NY; ++Y)
	{
		for (int32 X = 0; X < NX; ++X)
		{
			FCell& C = Cells[X + Y * NX];
			const FVector2D O = GridMin + FVector2D((X + 0.5f) * Cell, (Y + 0.5f) * Cell);
			const FMechWaterZone* Zone = ZoneAt(FVector(O.X, O.Y, 0.f));
			// 中心 + 两个对角点：三个都不下水才算安全
			static const FVector2D Off[3] = {FVector2D(0.f, 0.f), FVector2D(-0.33f, -0.33f), FVector2D(0.33f, 0.33f)};
			float Top = -1.e9f;
			float Low = 1.e9f;
			float NormalZ = 0.f;
			int32 Hits = 0;
			for (int32 i = 0; i < 3; ++i)
			{
				const FVector2D P = O + Off[i] * Cell;
				FHitResult Hit;
				if (GetWorld()->LineTraceSingleByObjectType(Hit, FVector(P.X, P.Y, TraceTop), FVector(P.X, P.Y, TraceBottom), Objects, Params))
				{
					Top = FMath::Max(Top, static_cast<float>(Hit.ImpactPoint.Z));
					Low = FMath::Min(Low, static_cast<float>(Hit.ImpactPoint.Z));
					if (i == 0)
					{
						NormalZ = Hit.ImpactNormal.Z;
					}
					++Hits;
				}
				else
				{
					Low = -1.e9f;
				}
			}
			C.Top = Top;
			C.Low = Hits > 0 ? Low : -1.e9f;
			const float SafeLine = Zone ? Zone->Z - Zone->KillDepth * 0.25f : -1.e9f;
			C.bSafe = C.Low > SafeLine;
			// 能落脚：不下水、够平（格子里高差小、中心坡度小）
			C.bLand = C.bSafe && Top - C.Low < Cell * 0.45f && NormalZ > 0.72f;
			Safe += C.bSafe;
			Land += C.bLand;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[water] 地面格子 %d×%d（%.0f 米一格）：能站 %d、能落脚 %d，用时 %.2f 秒"), NX, NY, Cell / 100.f, Safe, Land, FPlatformTime::Seconds() - T0);
}

const UMechWater::FCell* UMechWater::CellAt(const FVector& P) const
{
	if (Cells.Num() == 0)
	{
		return nullptr;
	}
	const int32 X = FMath::FloorToInt((P.X - GridMin.X) / Cell);
	const int32 Y = FMath::FloorToInt((P.Y - GridMin.Y) / Cell);
	if (X < 0 || Y < 0 || X >= NX || Y >= NY)
	{
		return nullptr;
	}
	return &Cells[X + Y * NX];
}

float UMechWater::GroundTop(const FVector& P) const
{
	const FCell* C = CellAt(P);
	return C ? C->Top : -1.e9f;
}

bool UMechWater::IsSafeStep(const FVector& P) const
{
	const FCell* C = CellAt(P);
	return C && C->bSafe;
}

bool UMechWater::IsSafeLine(const FVector& P, const FVector& Dir, float Len, int32 Steps) const
{
	for (int32 i = 1; i <= Steps; ++i)
	{
		if (!IsSafeStep(P + Dir * (Len * i / Steps)))
		{
			return false;
		}
	}
	return true;
}

bool UMechWater::FindLanding(const FVector& P, float MaxRise, float Radius, FVector& Out) const
{
	if (Cells.Num() == 0)
	{
		return false;
	}
	const int32 CX = FMath::FloorToInt((P.X - GridMin.X) / Cell);
	const int32 CY = FMath::FloorToInt((P.Y - GridMin.Y) / Cell);
	const int32 Rings = FMath::CeilToInt(Radius / Cell);
	const FCell* Best = nullptr;
	FVector2D BestAt = FVector2D::ZeroVector;
	double BestD = 1.e18;
	auto Check = [&](int32 X, int32 Y) {
		if (X < 0 || Y < 0 || X >= NX || Y >= NY)
		{
			return;
		}
		const FCell& C = Cells[X + Y * NX];
		if (!C.bLand || C.Top > P.Z + MaxRise)
		{
			return;
		}
		const FVector2D At = GridMin + FVector2D((X + 0.5f) * Cell, (Y + 0.5f) * Cell);
		const double D = FVector2D::DistSquared(At, FVector2D(P.X, P.Y));
		if (D < BestD)
		{
			BestD = D;
			Best = &C;
			BestAt = At;
		}
	};
	for (int32 R = 0; R <= Rings; ++R)
	{
		if (Best && (R - 1) * Cell > FMath::Sqrt(BestD))
		{
			break;  // 这一圈不可能更近了
		}
		for (int32 X = CX - R; X <= CX + R; ++X)
		{
			Check(X, CY - R);
			if (R > 0)
			{
				Check(X, CY + R);
			}
		}
		for (int32 Y = CY - R + 1; Y <= CY + R - 1; ++Y)
		{
			Check(CX - R, Y);
			Check(CX + R, Y);
		}
	}
	if (!Best)
	{
		return false;
	}
	Out = FVector(BestAt.X, BestAt.Y, Best->Top);
	return true;
}

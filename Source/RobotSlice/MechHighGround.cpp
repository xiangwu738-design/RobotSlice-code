#include "MechHighGround.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformTime.h"
#include "Algo/Reverse.h"

namespace
{
	/** 机体胶囊（半径 57、半高 130 厘米）略缩一点：节点上放得下才算能站 */
	constexpr float FitRadius = 50.f;
	constexpr float FitHalfHeight = 122.f;
	/** 一步能走上 / 走下的高差（引擎 MaxStepHeight 是 61 厘米） */
	constexpr float StepZ = 60.f;
	/** 原地跳（不喷射）能上去的高差：起跳 7.2 米/秒、重力 ×1.4，顶点约 1.9 米，留点余量 */
	constexpr float HopZ = 140.f;
	/** 喷射最多算到多高（格子边的上限；每台机体按自己的推进器再限） */
	constexpr float ClimbLimit = 3200.f;
	/** 每帧建图最多花多少秒 */
	constexpr double FrameBudget = 0.004;

	const int32 DX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
	const int32 DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
}

bool UMechHighGround::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UMechHighGround::Update()
{
	if (Phase == EPhase::Ready)
	{
		return true;
	}
	if (Phase == EPhase::Disabled)
	{
		return false;
	}
	// 好几台 AI 每帧都会调，一帧只建一份时间
	if (LastFrame == GFrameCounter)
	{
		return false;
	}
	LastFrame = GFrameCounter;
	if (Phase == EPhase::NotStarted)
	{
		Begin();
		if (Phase == EPhase::Disabled)
		{
			return false;
		}
	}
	const double Start = FPlatformTime::Seconds();
	while (FPlatformTime::Seconds() - Start < FrameBudget && Phase != EPhase::Ready)
	{
		if (Phase == EPhase::Columns)
		{
			BuildColumn(Cursor++);
			if (Cursor >= NX * NY)
			{
				Phase = EPhase::Edges;
				Cursor = 0;
			}
		}
		else if (Phase == EPhase::Edges)
		{
			BuildEdges(Cursor++);
			if (Cursor >= Nodes.Num())
			{
				Phase = EPhase::Flood;
			}
		}
		else if (Phase == EPhase::Flood)
		{
			Finish();
			Phase = EPhase::Ready;
		}
	}
	BuildSeconds += FPlatformTime::Seconds() - Start;
	++BuildFrames;
	if (Phase == EPhase::Ready)
	{
		UE_LOG(LogTemp, Display, TEXT("[high] 高处图建好：%s"), *Describe());
	}
	return Phase == EPhase::Ready;
}

void UMechHighGround::Begin()
{
	// 场地标记：和 AMechGameMode::ComputeArena 同一个约定（带 MechArena 标签的点，HalfX= / HalfY= 厘米）
	const AActor* Marker = nullptr;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("MechArena")))
		{
			Marker = *It;
			break;
		}
	}
	if (!Marker)
	{
		Phase = EPhase::Disabled;
		return;
	}
	Center = Marker->GetActorLocation();
	Half = FVector2D(6000.f, 6000.f);
	for (const FName& T : Marker->Tags)
	{
		const FString S = T.ToString();
		if (S.StartsWith(TEXT("HalfX=")))
		{
			Half.X = FCString::Atof(*S.Mid(6));
		}
		else if (S.StartsWith(TEXT("HalfY=")))
		{
			Half.Y = FCString::Atof(*S.Mid(6));
		}
	}
	// 贴着隐形墙那一圈不要（机体中心离墙至少一个胶囊半径）
	NX = FMath::Max(1, FMath::FloorToInt((Half.X * 2.f - 200.f) / Cell));
	NY = FMath::Max(1, FMath::FloorToInt((Half.Y * 2.f - 200.f) / Cell));
	Origin = FVector(Center.X - NX * Cell * 0.5f, Center.Y - NY * Cell * 0.5f, Center.Z);
	// 头顶隐形天花板（城区 46 米、园区 42 米）以上站不了人；从 55 米往下打，起点在高楼里面也没关系（楼里的面灌不到，最后会去掉）
	TopZ = Center.Z + 5500.f;
	BottomZ = Center.Z - 2000.f;
	ColFirst.Init(0, NX * NY);
	ColNum.Init(0, NX * NY);
	Nodes.Reserve(NX * NY * 2);
	// 出生点（两队出生区、死斗重生点、PlayerStart）当灌水的起点：这些地方一定是能走到的地面
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->IsA<APlayerStart>() || It->ActorHasTag(TEXT("DMSpawn")) || It->ActorHasTag(TEXT("TeamSpawn0")) || It->ActorHasTag(TEXT("TeamSpawn1")))
		{
			Seeds.Add(It->GetActorLocation());
		}
	}
	Seeds.Add(Center + FVector(0.f, 0.f, 200.f));
	Phase = EPhase::Columns;
	Cursor = 0;
	UE_LOG(LogTemp, Display, TEXT("[high] 开始建高处图：场地 %.0f×%.0f 米、%d×%d 格（%.0f 米一格），出生点 %d 个"), Half.X / 50.f, Half.Y / 50.f, NX, NY,
		Cell / 100.f, Seeds.Num());
}

FVector UMechHighGround::ColCenter(int32 C) const
{
	return FVector(Origin.X + (C % NX + 0.5f) * Cell, Origin.Y + (C / NX + 0.5f) * Cell, 0.f);
}

int32 UMechHighGround::ColOf(const FVector& P) const
{
	const int32 X = FMath::FloorToInt((P.X - Origin.X) / Cell);
	const int32 Y = FMath::FloorToInt((P.Y - Origin.Y) / Cell);
	return (X < 0 || Y < 0 || X >= NX || Y >= NY) ? INDEX_NONE : Y * NX + X;
}

bool UMechHighGround::LineClear(const FVector& A, const FVector& B) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechHighLine), true);
	FCollisionObjectQueryParams Obj(ECC_WorldStatic);
	Obj.AddObjectTypesToQuery(ECC_Vehicle);  // 隐形墙、天花板也算挡
	return !GetWorld()->LineTraceSingleByObjectType(Hit, A, B, Obj, Params);
}

void UMechHighGround::BuildColumn(int32 C)
{
	ColFirst[C] = Nodes.Num();
	const FVector P = ColCenter(C);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechHighColumn), true);
	FCollisionObjectQueryParams Fit;
	Fit.AddObjectTypesToQuery(ECC_WorldStatic);
	Fit.AddObjectTypesToQuery(ECC_Vehicle);  // 隐形墙、天花板（只挡机体的那种）
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(FitRadius, FitHalfHeight);
	float Z = TopZ;
	float LastZ = TNumericLimits<float>::Max();
	for (int32 k = 0; k < 10 && Z > BottomZ; ++k)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByObjectType(Hit, FVector(P.X, P.Y, Z), FVector(P.X, P.Y, BottomZ), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			break;
		}
		const FVector Feet = Hit.ImpactPoint;
		Z = Feet.Z - 25.f;
		// 太陡的面（墙、斜屋顶）不能站；同一层的夹层 / 底面（离上一层不到 1.5 米）不算
		if (Hit.ImpactNormal.Z < 0.7f || LastZ - Feet.Z < 150.f)
		{
			continue;
		}
		// 胶囊放得下：头顶有梁、贴着墙、在桥下净空不够的地方都不算
		if (GetWorld()->OverlapAnyTestByObjectType(Feet + FVector(0.f, 0.f, FitHalfHeight + 15.f), FQuat::Identity, Fit, Capsule, Params))
		{
			continue;
		}
		FNode& N = Nodes.AddDefaulted_GetRef();
		N.Feet = Feet;
		N.Col = C;
		LastZ = Feet.Z;
		if (++ColNum[C] >= 6)
		{
			break;
		}
	}
}

void UMechHighGround::BuildEdges(int32 NIdx)
{
	FNode& N = Nodes[NIdx];
	N.FirstEdge = Edges.Num();
	const int32 CX = N.Col % NX;
	const int32 CY = N.Col / NX;
	const FVector A = N.Feet;
	auto Add = [&](int32 To, uint8 Kind, float Rise)
	{
		const FVector B = Nodes[To].Feet;
		const float H = FVector::Dist2D(A, B);
		float Cost = H;
		switch (Kind)
		{
		case Walk:
			Cost += FMath::Abs(B.Z - A.Z) * 0.5f;
			break;
		case Hop:
			Cost += 250.f;
			break;
		case Drop:
			Cost += 150.f + (A.Z - B.Z) * 0.15f;
			break;
		default:  // 喷射：慢、费能量，还容易被打
			Cost += 500.f + Rise * 1.6f;
			break;
		}
		FEdge& E = Edges.AddDefaulted_GetRef();
		E.To = To;
		E.Kind = Kind;
		E.Cost = Cost;
		E.Rise = Rise;
		++EdgeCount[Kind];
	};
	for (int32 d = 0; d < 8; ++d)
	{
		const int32 X = CX + DX[d];
		const int32 Y = CY + DY[d];
		if (X < 0 || Y < 0 || X >= NX || Y >= NY)
		{
			continue;
		}
		const int32 MC = Y * NX + X;
		const int32 First = ColFirst[MC];
		const int32 Num = ColNum[MC];
		// 同一高度的那层（走 / 跳过矮护栏）；高差 0.6–1.6 米但中间是斜坡（园区高架环路的坡道）也算走
		int32 Level = INDEX_NONE;
		for (int32 i = First; i < First + Num; ++i)
		{
			if (FMath::Abs(Nodes[i].Feet.Z - A.Z) <= StepZ)
			{
				Level = i;
				break;
			}
		}
		for (int32 i = First; i < First + Num && Level == INDEX_NONE; ++i)
		{
			const FVector B = Nodes[i].Feet;
			if (FMath::Abs(B.Z - A.Z) > 160.f)
			{
				continue;
			}
			const FVector Mid = (A + B) * 0.5f;
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MechHighRamp), true);
			if (GetWorld()->LineTraceSingleByObjectType(Hit, FVector(Mid.X, Mid.Y, FMath::Max(A.Z, B.Z) + 150.f), FVector(Mid.X, Mid.Y, FMath::Min(A.Z, B.Z) - 100.f),
				FCollisionObjectQueryParams(ECC_WorldStatic), Params) && Hit.ImpactNormal.Z >= 0.7f && FMath::Abs(Hit.ImpactPoint.Z - Mid.Z) < 35.f)
			{
				Level = i;
			}
		}
		if (Level != INDEX_NONE)
		{
			const FVector B = Nodes[Level].Feet;
			const float Top = FMath::Max(A.Z, B.Z);
			if (LineClear(FVector(A.X, A.Y, Top + 110.f), FVector(B.X, B.Y, Top + 110.f)) && LineClear(FVector(A.X, A.Y, Top + 220.f), FVector(B.X, B.Y, Top + 220.f)))
			{
				Add(Level, Walk, 0.f);
			}
			else if (LineClear(FVector(A.X, A.Y, Top + 250.f), FVector(B.X, B.Y, Top + 250.f)) && LineClear(A + FVector(0.f, 0.f, 100.f), A + FVector(0.f, 0.f, 330.f)))
			{
				Add(Level, Hop, 0.f);
			}
		}
		// 往下：只到下面最高的那层（中间没有能站的面），而且这一格同高度没有能走过去的面
		if (Level == INDEX_NONE)
		{
			for (int32 i = First; i < First + Num; ++i)
			{
				const FVector B = Nodes[i].Feet;
				if (B.Z < A.Z - StepZ)
				{
					if (LineClear(FVector(A.X, A.Y, A.Z + 110.f), FVector(B.X, B.Y, A.Z + 110.f)) &&
						LineClear(FVector(A.X, A.Y, A.Z + 220.f), FVector(B.X, B.Y, A.Z + 220.f)) &&
						LineClear(FVector(B.X, B.Y, A.Z + 60.f), B + FVector(0.f, 0.f, 60.f)))
					{
						Add(i, Drop, 0.f);
					}
					break;
				}
			}
		}
		// 往上：跳（1.4 米内）或喷射。头顶要一直空到目标面上方 2.5 米，到了那个高度再横着过去
		for (int32 i = First + Num - 1; i >= First; --i)
		{
			const FVector B = Nodes[i].Feet;
			const float Rise = B.Z - A.Z;
			if (Rise <= StepZ || Rise > ClimbLimit || i == Level)
			{
				continue;
			}
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MechHighClimb), true);
			FCollisionObjectQueryParams Obj(ECC_WorldStatic);
			Obj.AddObjectTypesToQuery(ECC_Vehicle);  // 天花板（46 / 42 米）以上的楼顶喷不上去
			const bool bUp = !GetWorld()->SweepSingleByObjectType(Hit, A + FVector(0.f, 0.f, 100.f), FVector(A.X, A.Y, B.Z + 250.f), FQuat::Identity,
				Obj, FCollisionShape::MakeSphere(35.f), Params);
			if (!bUp)
			{
				break;  // 头顶被挡住了，更高的层也上不去
			}
			if (LineClear(FVector(A.X, A.Y, B.Z + 200.f), FVector(B.X, B.Y, B.Z + 200.f)))
			{
				Add(i, Rise <= HopZ ? Hop : Climb, Rise);
			}
			break;  // 只连上面最近的一层
		}
	}
	N.NumEdges = static_cast<uint8>(FMath::Min(255, Edges.Num() - N.FirstEdge));
}

void UMechHighGround::Finish()
{
	const int32 Num = Nodes.Num();
	// 灌水：从出生点出发能到（正向）、而且能回到出生点（反向）的节点才有效——楼里面、封死的顶、跳下去上不来的坑都去掉
	TArray<int32> Start;
	for (const FVector& S : Seeds)
	{
		const int32 C = ColOf(S);
		if (C == INDEX_NONE)
		{
			continue;
		}
		int32 Best = INDEX_NONE;
		for (int32 i = ColFirst[C]; i < ColFirst[C] + ColNum[C]; ++i)
		{
			if (Nodes[i].Feet.Z <= S.Z + 150.f && Nodes[i].Feet.Z >= S.Z - 600.f)
			{
				Best = i;
				break;
			}
		}
		if (Best != INDEX_NONE)
		{
			Start.AddUnique(Best);
		}
	}
	TArray<uint8> Fwd, Rev;
	Fwd.Init(0, Num);
	Rev.Init(0, Num);
	TArray<int32> Queue = Start;
	for (int32 S : Start)
	{
		Fwd[S] = 1;
	}
	for (int32 q = 0; q < Queue.Num(); ++q)
	{
		const FNode& U = Nodes[Queue[q]];
		for (int32 e = U.FirstEdge; e < U.FirstEdge + U.NumEdges; ++e)
		{
			if (!Fwd[Edges[e].To])
			{
				Fwd[Edges[e].To] = 1;
				Queue.Add(Edges[e].To);
			}
		}
	}
	// 反向：先建「谁连到我」
	TArray<int32> InFirst, InList;
	InFirst.Init(0, Num + 1);
	for (const FEdge& E : Edges)
	{
		++InFirst[E.To + 1];
	}
	for (int32 i = 0; i < Num; ++i)
	{
		InFirst[i + 1] += InFirst[i];
	}
	InList.SetNumUninitialized(Edges.Num());
	{
		TArray<int32> Fill = InFirst;
		for (int32 u = 0; u < Num; ++u)
		{
			for (int32 e = Nodes[u].FirstEdge; e < Nodes[u].FirstEdge + Nodes[u].NumEdges; ++e)
			{
				InList[Fill[Edges[e].To]++] = u;
			}
		}
	}
	Queue = Start;
	for (int32 S : Start)
	{
		Rev[S] = 1;
	}
	for (int32 q = 0; q < Queue.Num(); ++q)
	{
		const int32 V = Queue[q];
		for (int32 k = InFirst[V]; k < InFirst[V + 1]; ++k)
		{
			if (!Rev[InList[k]])
			{
				Rev[InList[k]] = 1;
				Queue.Add(InList[k]);
			}
		}
	}
	int32 Valid = 0;
	TMap<int32, int32> Bins;  // 50 厘米一档
	for (int32 i = 0; i < Num; ++i)
	{
		Nodes[i].bValid = Fwd[i] && Rev[i];
		if (Nodes[i].bValid)
		{
			++Valid;
			++Bins.FindOrAdd(FMath::FloorToInt(Nodes[i].Feet.Z / 50.f));
		}
	}
	// 街面 = 有效节点里最多的那个高度
	int32 BestBin = 0;
	int32 BestCount = -1;
	for (const TPair<int32, int32>& B : Bins)
	{
		if (B.Value > BestCount)
		{
			BestCount = B.Value;
			BestBin = B.Key;
		}
	}
	Floor = BestBin * 50.f;
	MaxHighZ = Floor;
	for (const FNode& N : Nodes)
	{
		if (N.bValid)
		{
			MaxHighZ = FMath::Max(MaxHighZ, N.Feet.Z);
		}
	}
	// 高处片区：高出街面 5 米的有效节点，按走 / 跳的边连成片；太小的片（3 格以下：护栏顶、窄梁）不算
	ZoneSize.Reset();
	for (int32 i = 0; i < Num; ++i)
	{
		Nodes[i].Zone = -1;
	}
	TArray<int32> Stack;
	for (int32 i = 0; i < Num; ++i)
	{
		if (!Nodes[i].bValid || Nodes[i].Zone >= 0 || Nodes[i].Feet.Z - Floor < HighMin)
		{
			continue;
		}
		const int16 Z = static_cast<int16>(ZoneSize.Num());
		int32 Count = 0;
		Stack.Reset();
		Stack.Add(i);
		Nodes[i].Zone = Z;
		while (Stack.Num())
		{
			const int32 U = Stack.Pop(EAllowShrinking::No);
			++Count;
			for (int32 e = Nodes[U].FirstEdge; e < Nodes[U].FirstEdge + Nodes[U].NumEdges; ++e)
			{
				const FEdge& E = Edges[e];
				FNode& V = Nodes[E.To];
				if ((E.Kind == Walk || E.Kind == Hop) && V.bValid && V.Zone < 0 && V.Feet.Z - Floor >= HighMin)
				{
					V.Zone = Z;
					Stack.Add(E.To);
				}
			}
		}
		ZoneSize.Add(Count);
	}
	Perches.Reset();
	for (int32 i = 0; i < Num; ++i)
	{
		if (Nodes[i].Zone >= 0 && ZoneSize[Nodes[i].Zone] < 3)
		{
			Nodes[i].Zone = -1;
		}
		if (Nodes[i].Zone >= 0)
		{
			Perches.Add(i);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[high] 灌水：起点 %d 个，有效节点 %d / %d"), Start.Num(), Valid, Num);
}

FString UMechHighGround::Describe() const
{
	int32 Valid = 0;
	int32 Big = 0;
	for (const FNode& N : Nodes)
	{
		Valid += N.bValid ? 1 : 0;
	}
	for (int32 S : ZoneSize)
	{
		Big += S >= 3 ? 1 : 0;
	}
	return FString::Printf(TEXT("节点 %d（有效 %d），街面 Z=%.0f，最高能站 %.1f 米，高处节点 %d、片区 %d 个（3 格以上 %d 个）；边 走 %d / 跳 %d / 下 %d / 喷 %d；耗时 %.0f 毫秒（%d 帧）"),
		Nodes.Num(), Valid, Floor, (MaxHighZ - Floor) / 100.f, Perches.Num(), ZoneSize.Num(), Big, EdgeCount[Walk], EdgeCount[Hop], EdgeCount[Drop], EdgeCount[Climb],
		BuildSeconds * 1000.0, BuildFrames);
}

int32 UMechHighGround::FindNode(const FVector& Feet, float MaxBelow, float MaxAbove) const
{
	if (Phase != EPhase::Ready)
	{
		return INDEX_NONE;
	}
	const int32 C0 = ColOf(Feet);
	if (C0 == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	// 先看自己这一格，没有再看周围 8 格（站在格子边上、贴着墙）
	int32 Best = INDEX_NONE;
	float BestD = TNumericLimits<float>::Max();
	for (int32 d = -1; d < 8; ++d)
	{
		const int32 X = C0 % NX + (d < 0 ? 0 : DX[d]);
		const int32 Y = C0 / NX + (d < 0 ? 0 : DY[d]);
		if (X < 0 || Y < 0 || X >= NX || Y >= NY)
		{
			continue;
		}
		const int32 C = Y * NX + X;
		for (int32 i = ColFirst[C]; i < ColFirst[C] + ColNum[C]; ++i)
		{
			const FNode& N = Nodes[i];
			if (!N.bValid || N.Feet.Z > Feet.Z + MaxAbove || N.Feet.Z < Feet.Z - MaxBelow)
			{
				continue;
			}
			const float D = FVector::Dist2D(N.Feet, Feet) + FMath::Abs(N.Feet.Z - Feet.Z) * 0.5f;
			if (D < BestD)
			{
				BestD = D;
				Best = i;
			}
		}
		if (d < 0 && Best != INDEX_NONE)
		{
			break;
		}
	}
	return Best;
}

int32 UMechHighGround::NearestNode(const FVector& P, float Radius, bool bFloorOnly) const
{
	if (Phase != EPhase::Ready)
	{
		return INDEX_NONE;
	}
	const int32 R = FMath::CeilToInt(Radius / Cell);
	const int32 X0 = FMath::FloorToInt((P.X - Origin.X) / Cell);
	const int32 Y0 = FMath::FloorToInt((P.Y - Origin.Y) / Cell);
	int32 Best = INDEX_NONE;
	float BestD = TNumericLimits<float>::Max();
	for (int32 Y = FMath::Max(0, Y0 - R); Y <= FMath::Min(NY - 1, Y0 + R); ++Y)
	{
		for (int32 X = FMath::Max(0, X0 - R); X <= FMath::Min(NX - 1, X0 + R); ++X)
		{
			const int32 C = Y * NX + X;
			for (int32 i = ColFirst[C]; i < ColFirst[C] + ColNum[C]; ++i)
			{
				const FNode& N = Nodes[i];
				if (!N.bValid || (bFloorOnly && N.Feet.Z - Floor > 300.f))
				{
					continue;
				}
				const float D = FVector::Dist2D(N.Feet, P) + (bFloorOnly ? 0.f : FMath::Abs(N.Feet.Z - P.Z));
				if (D < BestD && FVector::Dist2D(N.Feet, P) <= Radius)
				{
					BestD = D;
					Best = i;
				}
			}
		}
	}
	return Best;
}

uint8 UMechHighGround::EdgeKind(int32 From, int32 To) const
{
	if (!Nodes.IsValidIndex(From))
	{
		return 255;
	}
	const FNode& N = Nodes[From];
	for (int32 e = N.FirstEdge; e < N.FirstEdge + N.NumEdges; ++e)
	{
		if (Edges[e].To == To)
		{
			return Edges[e].Kind;
		}
	}
	return 255;
}

bool UMechHighGround::StepStaysInZone(int32 NIdx, const FVector& Dir) const
{
	if (!IsValidNode(NIdx) || Nodes[NIdx].Zone < 0)
	{
		return false;
	}
	// 方向取最近的 8 方向之一，看那边一格是不是同一片高处、走得过去
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	const int32 d = (FMath::RoundToInt(Yaw / 45.f) % 8 + 8) % 8;
	const FNode& N = Nodes[NIdx];
	for (int32 e = N.FirstEdge; e < N.FirstEdge + N.NumEdges; ++e)
	{
		const FEdge& E = Edges[e];
		const FNode& M = Nodes[E.To];
		if (E.Kind == Walk && M.Zone == N.Zone && M.Col == N.Col + DY[d] * NX + DX[d] &&
			FMath::Abs(M.Col % NX - N.Col % NX) <= 1)
		{
			return true;
		}
	}
	return false;
}

bool UMechHighGround::FindPath(int32 From, int32 To, const FAbility& Ability, float Seed, const TSet<uint64>* Bad, TArray<FVector>& OutPts,
	TArray<uint8>& OutKinds, TArray<int32>* OutNodes, float* OutCost) const
{
	OutPts.Reset();
	OutKinds.Reset();
	if (OutNodes)
	{
		OutNodes->Reset();
	}
	if (!IsValidNode(From) || !IsValidNode(To))
	{
		return false;
	}
	if (From == To)
	{
		return true;
	}
	const int32 Num = Nodes.Num();
	TArray<float> G;
	TArray<int32> Prev;
	TArray<uint8> Closed;
	G.Init(TNumericLimits<float>::Max(), Num);
	Prev.Init(INDEX_NONE, Num);
	Closed.Init(0, Num);
	// 每台 AI 的路线偏好：格子代价乘一个随位置平滑变化的系数（1–1.9），不同的 Seed 偏好不同的街 / 不同的一侧
	auto Mult = [Seed](const FVector& P)
	{
		if (Seed == 0.f)
		{
			return 1.f;
		}
		const float n = FMath::PerlinNoise2D(FVector2D(P.X / 2600.f + Seed, P.Y / 2600.f - Seed * 0.7f));
		return 1.f + 0.9f * FMath::Clamp(n * 0.5f + 0.5f, 0.f, 1.f);
	};
	struct FOpen
	{
		float F;
		int32 N;
		bool operator<(const FOpen& O) const { return F < O.F; }
	};
	TArray<FOpen> Open;
	const FVector Goal = Nodes[To].Feet;
	G[From] = 0.f;
	Open.HeapPush({static_cast<float>(FVector::Dist(Nodes[From].Feet, Goal)), From});
	bool bFound = false;
	while (Open.Num())
	{
		FOpen Cur;
		Open.HeapPop(Cur, EAllowShrinking::No);
		const int32 U = Cur.N;
		if (Closed[U])
		{
			continue;
		}
		Closed[U] = 1;
		if (U == To)
		{
			bFound = true;
			break;
		}
		const FNode& NU = Nodes[U];
		const float MU = Mult(NU.Feet);
		for (int32 e = NU.FirstEdge; e < NU.FirstEdge + NU.NumEdges; ++e)
		{
			const FEdge& E = Edges[e];
			if (!Nodes[E.To].bValid || Closed[E.To])
			{
				continue;
			}
			if (E.Kind == Climb && E.Rise > Ability.MaxClimb)
			{
				continue;
			}
			if (Bad && Bad->Contains((static_cast<uint64>(U) << 32) | static_cast<uint32>(E.To)))
			{
				continue;
			}
			const float NG = G[U] + E.Cost * 0.5f * (MU + Mult(Nodes[E.To].Feet));
			if (NG < G[E.To])
			{
				G[E.To] = NG;
				Prev[E.To] = U;
				Open.HeapPush({NG + static_cast<float>(FVector::Dist(Nodes[E.To].Feet, Goal)), E.To});
			}
		}
	}
	if (!bFound)
	{
		return false;
	}
	TArray<int32> Chain;
	for (int32 V = To; V != From; V = Prev[V])
	{
		Chain.Add(V);
	}
	Algo::Reverse(Chain);
	int32 PrevNode = From;
	for (int32 V : Chain)
	{
		OutPts.Add(Nodes[V].Feet);
		OutKinds.Add(EdgeKind(PrevNode, V));
		if (OutNodes)
		{
			OutNodes->Add(V);
		}
		PrevNode = V;
	}
	if (OutCost)
	{
		*OutCost = G[To];
	}
	return true;
}

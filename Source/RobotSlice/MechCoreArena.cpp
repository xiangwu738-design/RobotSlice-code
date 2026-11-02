#include "MechCoreArena.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float LiftCycle = 12.f;    // 升降平台：4 秒停高处、2 秒下、4 秒停低处、2 秒上
	constexpr float GateCycle = 30.f;    // 闸门：每 30 秒关 10 秒（第 20 秒开始关），关之前 3 秒闪灯
	constexpr float GateCloseAt = 20.f;

	UStaticMesh* CubeMesh()
	{
		static UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		return Mesh;
	}
}

AMechCoreArena::AMechCoreArena()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(1.f);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMechCoreArena::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechCoreArena, Layout);
	DOREPLIFETIME(AMechCoreArena, bSwapSides);
	DOREPLIFETIME(AMechCoreArena, bCollisionOnly);
}

float AMechCoreArena::NetTime() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void AMechCoreArena::BeginPlay()
{
	Super::BeginPlay();
	if (bBuilt)
	{
		return;
	}
	bBuilt = true;
	if (HasAuthority() && ActorHasTag(TEXT("CollisionOnly")))
	{
		bCollisionOnly = true;
	}
	Nodes.Reset();
	Edges.Reset();
	if (BuildTerrain())
	{
		Layout = 2;
	}
	else if (Layout == 0)
	{
		BuildFactory();
		GraphFactory();
	}
	else
	{
		BuildCanyon();
		GraphCanyon();
	}
	if (bCollisionOnly && Layout != 2)
	{
		ApplyCollisionOnly();
	}
	for (FNode& Node : Nodes)
	{
		Node.P += GetActorLocation();
	}
	UE_LOG(LogTemp, Display, TEXT("[core] 场地%s搭好：%d 组方块，路点 %d、路段 %d%s"), Layout == 0 ? TEXT("装配厂") : Layout == 1 ? TEXT("峡谷") : TEXT("自然地形"),
		Parts.Num(), Nodes.Num(), Edges.Num(), bCollisionOnly && Layout != 2 ? TEXT("（只留碰撞，外观用地图）") : TEXT(""));
}

UMaterialInterface* AMechCoreArena::ArtRockMaterial(UMaterialInterface* Fallback) const
{
	static UMaterialInterface* Rock = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Env/Volcano/MI_Volcano_BoxRock.MI_Volcano_BoxRock"));
	return Rock ? Rock : Fallback;
}

void AMechCoreArena::ApplyCollisionOnly()
{
	// 岩体、地面（含坡道）、浅河、墙：地图的地形已经按这些面挖好，只要碰撞
	static const FName HideNames[] = {TEXT("Floors"), TEXT("Rock"), TEXT("Water"), TEXT("Walls")};
	int32 Hidden = 0;
	for (UInstancedStaticMeshComponent* ISM : Parts)
	{
		if (!ISM)
		{
			continue;
		}
		const FName N = ISM->GetFName();
		bool bHide = false;
		for (const FName& H : HideNames)
		{
			bHide |= (N == H);
		}
		if (bHide)
		{
			ISM->SetHiddenInGame(true);
			ISM->SetCastShadow(false);
			ISM->SetAffectDistanceFieldLighting(false);
			++Hidden;
		}
		else if (N == TEXT("Cover"))
		{
			ISM->SetMaterial(0, ArtRockMaterial(ISM->GetMaterial(0)));
		}
		else if (N == TEXT("Bridge"))
		{
			// 桥是人造结构：用世界投影混凝土，不用灰盒网格
			if (UMaterialInterface* Concrete = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Env/Materials/MI_ConcreteWall.MI_ConcreteWall")))
			{
				ISM->SetMaterial(0, Concrete);
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[core] 只留碰撞：隐藏 %d 组灰盒外观"), Hidden);
}

// ---------------------------------------------------------------- 搭建工具
void AMechCoreArena::AddMapRect(float X0, float Y0, float X1, float Y1, uint8 Kind)
{
	MapShapes.Add({FBox2D(FVector2D(FMath::Min(X0, X1), FMath::Min(Y0, Y1)), FVector2D(FMath::Max(X0, X1), FMath::Max(Y0, Y1))), Kind});
}

UInstancedStaticMeshComponent* AMechCoreArena::NewISM(const TCHAR* Name, UMaterialInterface* Material, bool bCollide)
{
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this, Name);
	ISM->SetStaticMesh(CubeMesh());
	if (Material)
	{
		ISM->SetMaterial(0, Material);
	}
	ISM->SetMobility(EComponentMobility::Movable);
	ISM->SetCollisionProfileName(bCollide ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
	ISM->SetupAttachment(RootComponent);
	ISM->RegisterComponent();
	Parts.Add(ISM);
	return ISM;
}

void AMechCoreArena::AddBox(UInstancedStaticMeshComponent* Into, const FVector& Center, const FVector& Size, const FRotator& Rot)
{
	Into->AddInstance(FTransform(Rot, Center, Size / 100.f), false);
}

void AMechCoreArena::AddSpan(UInstancedStaticMeshComponent* Into, const FVector& A, const FVector& B)
{
	const FVector Lo(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z));
	const FVector Hi(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z));
	AddBox(Into, (Lo + Hi) * 0.5f, Hi - Lo);
}

void AMechCoreArena::AddSlab(UInstancedStaticMeshComponent* Into, float ZTop, float Thick, const FBox2D& Area, const TArray<FBox2D>& Holes)
{
	// 按所有洞的边把区域切成格子，不在洞里的格子按行合并成长条
	TArray<double> Xs = {Area.Min.X, Area.Max.X};
	TArray<double> Ys = {Area.Min.Y, Area.Max.Y};
	for (const FBox2D& H : Holes)
	{
		Xs.Add(FMath::Clamp(H.Min.X, Area.Min.X, Area.Max.X));
		Xs.Add(FMath::Clamp(H.Max.X, Area.Min.X, Area.Max.X));
		Ys.Add(FMath::Clamp(H.Min.Y, Area.Min.Y, Area.Max.Y));
		Ys.Add(FMath::Clamp(H.Max.Y, Area.Min.Y, Area.Max.Y));
	}
	Xs.Sort();
	Ys.Sort();
	auto InHole = [&Holes](double X, double Y) {
		for (const FBox2D& H : Holes)
		{
			if (X > H.Min.X && X < H.Max.X && Y > H.Min.Y && Y < H.Max.Y)
			{
				return true;
			}
		}
		return false;
	};
	for (int32 j = 0; j + 1 < Ys.Num(); ++j)
	{
		const double Y0 = Ys[j], Y1 = Ys[j + 1];
		if (Y1 - Y0 < 1.f)
		{
			continue;
		}
		int32 i = 0;
		while (i + 1 < Xs.Num())
		{
			if (Xs[i + 1] - Xs[i] < 1.f || InHole((Xs[i] + Xs[i + 1]) * 0.5f, (Y0 + Y1) * 0.5f))
			{
				++i;
				continue;
			}
			int32 k = i + 1;
			while (k + 1 < Xs.Num() && !InHole((Xs[k] + Xs[k + 1]) * 0.5f, (Y0 + Y1) * 0.5f))
			{
				++k;
			}
			const double X0 = Xs[i], X1 = Xs[k];
			AddBox(Into, FVector((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, ZTop - Thick * 0.5f), FVector(X1 - X0, Y1 - Y0, Thick));
			i = k;
		}
	}
}

void AMechCoreArena::AddRamp(UInstancedStaticMeshComponent* Into, const FVector& High, const FVector& Low, float Width)
{
	const float Thick = 60.f;
	const FVector D = Low - High;
	const FRotator Rot = D.Rotation();
	const FVector Up = Rot.RotateVector(FVector::UpVector);
	AddBox(Into, (High + Low) * 0.5f - Up * Thick * 0.5f, FVector(D.Size() + 30.f, Width, Thick), Rot);
}

void AMechCoreArena::AddLight(const FVector& At, float Intensity, float Radius, const FLinearColor& Color)
{
	UPointLightComponent* L = NewObject<UPointLightComponent>(this);
	L->SetupAttachment(RootComponent);
	L->SetRelativeLocation(At);
	L->SetIntensityUnits(ELightUnits::Candelas);
	L->SetIntensity(Intensity);
	L->SetAttenuationRadius(Radius);
	L->SetLightColor(Color);
	L->SetCastShadows(false);
	L->RegisterComponent();
}

void AMechCoreArena::BuildMovers(UMaterialInterface* LiftMat, UMaterialInterface* GateMat, UMaterialInterface* Glow)
{
	for (int32 Half = 0; Half < 2; ++Half)
	{
		UStaticMeshComponent* Lift = NewObject<UStaticMeshComponent>(this);
		Lift->SetStaticMesh(CubeMesh());
		Lift->SetMaterial(0, LiftMat);
		Lift->SetMobility(EComponentMobility::Movable);
		Lift->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Lift->SetupAttachment(RootComponent);
		Lift->SetRelativeScale3D(FVector(LiftSize / 100.f, LiftSize / 100.f, 0.6f));
		Lift->SetRelativeLocation(FVector(LiftXY[Half].X, LiftXY[Half].Y, LiftHighZ - 30.f));
		Lift->RegisterComponent();
		Lifts[Half] = Lift;

		UStaticMeshComponent* Gate = NewObject<UStaticMeshComponent>(this);
		Gate->SetStaticMesh(CubeMesh());
		Gate->SetMaterial(0, GateMat);
		Gate->SetMobility(EComponentMobility::Movable);
		Gate->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Gate->SetupAttachment(RootComponent);
		Gate->SetRelativeScale3D(GateSize / 100.f);
		Gate->SetRelativeLocation(FVector(GateXY[Half].X, GateXY[Half].Y, GateOpenZ));
		Gate->RegisterComponent();
		Gates[Half] = Gate;

		// 闸门上方的警示灯（关之前闪）
		UStaticMeshComponent* Lamp = NewObject<UStaticMeshComponent>(this);
		Lamp->SetStaticMesh(CubeMesh());
		Lamp->SetMobility(EComponentMobility::Movable);
		Lamp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Lamp->SetupAttachment(RootComponent);
		Lamp->SetRelativeScale3D(LampSize / 100.f);
		Lamp->SetRelativeLocation(LampAt[Half]);
		Lamp->RegisterComponent();
		if (Glow)
		{
			GateLampMat[Half] = UMaterialInstanceDynamic::Create(Glow, this);
			GateLampMat[Half]->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(1.f, 0.55f, 0.05f));
			GateLampMat[Half]->SetScalarParameterValue(TEXT("EmissiveStrength"), 2.f);
			Lamp->SetMaterial(0, GateLampMat[Half]);
		}
	}
}

// ---------------------------------------------------------------- 峡谷（默认）
// 坐标：X 沿峡谷（±130 米），Y 横向；谷底 Z 0，崖壁栈道 Z 9 米，北崖顶 22 米、南崖顶 26 米
void AMechCoreArena::BuildCanyon()
{
	constexpr float Len = 13000.f;      // 峡谷两头的端墙
	constexpr float FloorW = 1800.f;    // 谷底半宽
	constexpr float LedgeZ = 900.f;     // 崖壁栈道高度
	constexpr float LedgeW = 2500.f;    // 栈道外沿（崖壁里侧）
	constexpr float CaveY0 = 2700.f;    // 北侧岩洞 Y 范围、高 6 米
	constexpr float CaveY1 = 3700.f;
	constexpr float CaveH = 600.f;
	constexpr float OuterY = 4600.f;
	constexpr float RockEnd = 12000.f;  // 崖体到这里，再往外是两头的出生区

	Layers = {0.f, LedgeZ, 2200.f};
	HalfExtent = FVector2D(12600.f, 4200.f);
	StationNames[0] = TEXT("谷底");
	StationNames[1] = TEXT("崖壁栈道");
	FoundryPt = FVector::ZeroVector;
	for (int32 Half = 0; Half < 2; ++Half)
	{
		const float S = Half == 0 ? -1.f : 1.f;
		SpawnPt[Half] = FVector(S * 12000.f, 0.f, 0.f);
		StationPt[Half][0] = FVector(S * 8800.f, 1000.f, 0.f);
		StationPt[Half][1] = FVector(S * 8200.f, -2150.f, LedgeZ);
		LiftXY[Half] = FVector(S * 7400.f, -1350.f, 0.f);
		GateXY[Half] = FVector(S * 4500.f, (CaveY0 + CaveY1) * 0.5f, 0.f);
		LampAt[Half] = FVector(S * 4500.f, (CaveY0 + CaveY1) * 0.5f, CaveH - 40.f);
	}
	LiftSize = 780.f;
	LiftHighZ = LedgeZ;
	LiftLowZ = 0.f;
	GateSize = FVector(200.f, CaveY1 - CaveY0, CaveH);
	GateShutZ = CaveH * 0.5f;
	GateOpenZ = -CaveH * 0.5f - 100.f;
	LampSize = FVector(60.f, CaveY1 - CaveY0, 25.f);

	// 小地图
	MapLo = FVector2D(-Len, -OuterY);
	MapHi = FVector2D(Len, OuterY);
	AddMapRect(-Len, -OuterY, Len, OuterY, 3);
	AddMapRect(-Len, -FloorW, Len, FloorW, 0);
	AddMapRect(-RockEnd, -500.f, RockEnd, 500.f, 4);
	AddMapRect(-RockEnd, FloorW, RockEnd, LedgeW, 1);
	AddMapRect(-RockEnd, -LedgeW, RockEnd, -FloorW, 1);
	AddMapRect(-9000.f, CaveY0, 9000.f, CaveY1, 2);
	AddMapRect(-500.f, FloorW, 500.f, CaveY0, 2);
	AddMapRect(-300.f, -FloorW, 300.f, FloorW, 1);
	for (const float S : {-1.f, 1.f})
	{
		AddMapRect(S * 7600.f, FloorW, S * 8400.f, CaveY0, 2);
		AddMapRect(S * 1300.f, -1800.f, S * 3700.f, -1200.f, 1);
		AddMapRect(S * 1300.f, 1200.f, S * 3700.f, 1800.f, 1);
		AddMapRect(S * 7000.f, -1740.f, S * 7800.f, -960.f, 1);
	}

	UMaterialInterface* Grid = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));
	UMaterialInterface* Grid2 = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray_02.MI_PrototypeGrid_Gray_02"));
	UMaterialInterface* Dark = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark.MI_PrototypeGrid_TopDark"));
	UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Weapons/Materials/MI_Tracer.MI_Tracer"));
	UInstancedStaticMeshComponent* Floors = NewISM(TEXT("Floors"), Grid);
	UInstancedStaticMeshComponent* Rock = NewISM(TEXT("Rock"), Grid2);
	UInstancedStaticMeshComponent* Cover = NewISM(TEXT("Cover"), Dark);
	UInstancedStaticMeshComponent* Water = NewISM(TEXT("Water"), Dark);
	Water->ComponentTags.Add(TEXT("Water"));  // 移动组件按这个标签减速

	// 谷底（整块，崖体压在上面）、浅河、两头端墙和两侧外墙
	AddSlab(Floors, 0.f, 100.f, FBox2D(FVector2D(-Len, -OuterY), FVector2D(Len, OuterY)), {});
	AddBox(Water, FVector(0.f, 0.f, 1.5f), FVector(RockEnd * 2.f, 1000.f, 3.f));
	for (const float S : {-1.f, 1.f})
	{
		AddSpan(Rock, FVector(S * Len, -OuterY - 200.f, 0.f), FVector(S * (Len + 200.f), OuterY + 200.f, 4500.f));
		AddSpan(Rock, FVector(-Len - 200.f, S * OuterY, 0.f), FVector(Len + 200.f, S * (OuterY + 200.f), 4500.f));
	}

	// 北侧：栈道下面的崖基（两处洞口：中央、两头回收站 A 旁边）+ 栈道 + 岩洞外壁 + 洞顶崖体
	const TArray<FVector2D> NorthBase = {{-RockEnd, -8400.f}, {-7600.f, -500.f}, {500.f, 7600.f}, {8400.f, RockEnd}};
	for (const FVector2D& Seg : NorthBase)
	{
		AddSpan(Rock, FVector(Seg.X, FloorW, 0.f), FVector(Seg.Y, CaveY0, CaveH));
	}
	AddSpan(Rock, FVector(-RockEnd, FloorW, CaveH), FVector(RockEnd, LedgeW, LedgeZ));        // 栈道（洞口上方的过梁）
	AddSpan(Rock, FVector(-RockEnd, LedgeW, CaveH), FVector(RockEnd, OuterY, 2200.f));        // 洞顶到北崖顶
	AddSpan(Rock, FVector(-RockEnd, CaveY1, 0.f), FVector(RockEnd, OuterY, CaveH));           // 岩洞外壁
	// 南侧：栈道崖基（实心）+ 更高的南崖
	AddSpan(Rock, FVector(-RockEnd, -LedgeW, 0.f), FVector(RockEnd, -FloorW, LedgeZ));
	AddSpan(Rock, FVector(-RockEnd, -OuterY, 0.f), FVector(RockEnd, -LedgeW, 2600.f));
	for (const float S : {-1.f, 1.f})
	{
		// 两头：崖体封口，岩洞在回收站 A 后面到头
		AddSpan(Rock, FVector(S * RockEnd, FloorW, 0.f), FVector(S * Len, OuterY, 2200.f));
		AddSpan(Rock, FVector(S * RockEnd, -OuterY, 0.f), FVector(S * Len, -FloorW, 2600.f));
		AddSpan(Rock, FVector(S * 9000.f, CaveY0, 0.f), FVector(S * RockEnd, CaveY1, CaveH));

		// 坡道：中央两侧贴着崖壁，往家的方向爬上栈道
		AddRamp(Floors, FVector(S * 3700.f, -1500.f, LedgeZ), FVector(S * 1300.f, -1500.f, 0.f), 600.f);
		AddRamp(Floors, FVector(S * 3700.f, 1500.f, LedgeZ), FVector(S * 1300.f, 1500.f, 0.f), 600.f);

		// 谷底大石（掩体）、小石
		for (const FVector2D& P : {FVector2D(2200.f, 300.f), FVector2D(4800.f, -1500.f), FVector2D(5200.f, 1550.f), FVector2D(7000.f, 500.f), FVector2D(9600.f, -1400.f)})
		{
			AddBox(Cover, FVector(S * P.X, P.Y, 300.f), FVector(500.f, 500.f, 600.f), FRotator(0.f, P.X * 0.013f, 0.f));
		}
		for (const FVector2D& P : {FVector2D(1500.f, -300.f), FVector2D(3800.f, 600.f), FVector2D(6400.f, -1500.f), FVector2D(8000.f, -400.f), FVector2D(10500.f, 900.f)})
		{
			AddBox(Cover, FVector(S * P.X, P.Y, 125.f), FVector(300.f, 300.f, 250.f), FRotator(0.f, P.X * 0.021f, 0.f));
		}
		// 栈道上的矮石（贴外侧，留出走道）、岩洞里的石头
		for (const float X : {4800.f, 6800.f, 9500.f})
		{
			AddBox(Cover, FVector(S * X, -2380.f, LedgeZ + 80.f), FVector(300.f, 250.f, 160.f));
			AddBox(Cover, FVector(S * X, 2380.f, LedgeZ + 80.f), FVector(300.f, 250.f, 160.f));
		}
		AddBox(Cover, FVector(S * 2500.f, 3550.f, 120.f), FVector(400.f, 250.f, 240.f));
		AddBox(Cover, FVector(S * 6000.f, 2850.f, 120.f), FVector(400.f, 250.f, 240.f));
		// 崖顶几块石头（飞上去的狙击位）
		AddBox(Cover, FVector(S * 5000.f, 3400.f, 2200.f + 150.f), FVector(500.f, 400.f, 300.f));
		AddBox(Cover, FVector(S * 6500.f, -3400.f, 2600.f + 150.f), FVector(500.f, 400.f, 300.f));
	}
	// 中央：铸造台（四根柱子 + 顶），上方栈道高度一座横跨峡谷的桥
	for (const float SX : {-1.f, 1.f})
	{
		for (const float SY : {-1.f, 1.f})
		{
			AddBox(Cover, FVector(SX * 450.f, SY * 450.f, 350.f), FVector(300.f, 300.f, 700.f));
		}
	}
	AddBox(Cover, FVector(0.f, 0.f, 750.f), FVector(1200.f, 1200.f, 100.f));
	// 桥单独一组：只留碰撞时地面隐藏，桥照常显示
	UInstancedStaticMeshComponent* Bridge = NewISM(TEXT("Bridge"), Grid);
	AddSpan(Bridge, FVector(-300.f, -FloorW, LedgeZ - 60.f), FVector(300.f, FloorW, LedgeZ));
	if (bCollisionOnly)
	{
		// 地形是高度场做不出岩洞：补一层没碰撞的岩石内壁（洞底、洞顶、内壁、两头封口），
		// 地形在三个洞口挖到了谷底，洞口上方补栈道过梁和一块封到崖顶的岩石
		UInstancedStaticMeshComponent* CaveVis = NewISM(TEXT("CaveVis"), ArtRockMaterial(Grid2), false);
		AddSpan(CaveVis, FVector(-9000.f, CaveY0, -10.f), FVector(9000.f, CaveY1, 0.f));
		AddSpan(CaveVis, FVector(-9000.f, CaveY0 - 40.f, CaveH), FVector(9000.f, CaveY1 + 40.f, CaveH + 50.f));
		AddSpan(CaveVis, FVector(-9000.f, CaveY1, 0.f), FVector(9000.f, CaveY1 + 40.f, CaveH));
		for (const float S : {-1.f, 1.f})
		{
			AddSpan(CaveVis, FVector(S * 9000.f, CaveY0, 0.f), FVector(S * 9040.f, CaveY1, CaveH));
		}
		for (const FVector2D& Seg : NorthBase)
		{
			if (Seg.Y > -9000.f && Seg.X < 9000.f)
			{
				AddSpan(CaveVis, FVector(FMath::Max(Seg.X, -9000.f), CaveY0 - 40.f, 0.f), FVector(FMath::Min(Seg.Y, 9000.f), CaveY0, CaveH));
			}
		}
		for (const FVector2D& Door : {FVector2D(-8400.f, -7600.f), FVector2D(-500.f, 500.f), FVector2D(7600.f, 8400.f)})
		{
			AddSpan(CaveVis, FVector(Door.X, FloorW, CaveH), FVector(Door.Y, LedgeW, LedgeZ));
			AddSpan(CaveVis, FVector(Door.X, LedgeW, CaveH), FVector(Door.Y, CaveY0 + 100.f, 2200.f));
		}
	}
	// 岩洞里的灯
	for (float X = -8000.f; X <= 8000.f; X += 2000.f)
	{
		AddLight(FVector(X, (CaveY0 + CaveY1) * 0.5f, CaveH - 80.f), 1800.f, 2200.f, FLinearColor(1.f, 0.8f, 0.6f));
	}
	BuildMovers(Dark, Rock->GetMaterial(0), Glow);
}

void AMechCoreArena::GraphCanyon()
{
	constexpr float LedgeZ = 900.f;
	const int32 Foundry = AddNode(FVector(0.f, 0.f, 0.f));
	const int32 C0 = AddNode(FVector(0.f, -1000.f, 0.f));
	const int32 C1 = AddNode(FVector(0.f, 1000.f, 0.f));
	const int32 CaveDoorC = AddNode(FVector(0.f, 2200.f, 0.f));
	const int32 CaveC = AddNode(FVector(0.f, 3200.f, 0.f));
	const int32 Bridge = AddNode(FVector(0.f, 0.f, LedgeZ));
	const int32 BridgeS = AddNode(FVector(0.f, -2150.f, LedgeZ));
	const int32 BridgeN = AddNode(FVector(0.f, 2150.f, LedgeZ));
	AddEdge(Foundry, C0);
	AddEdge(Foundry, C1);
	AddEdge(C0, C1);
	AddEdge(C1, CaveDoorC);
	AddEdge(CaveDoorC, CaveC);
	AddEdge(Bridge, BridgeS);
	AddEdge(Bridge, BridgeN);
	for (int32 Half = 0; Half < 2; ++Half)
	{
		const float S = Half == 0 ? -1.f : 1.f;
		auto N = [this, S](float X, float Y, float Z) { return AddNode(FVector(S * X, Y, Z)); };
		const int32 FS1 = N(3000.f, -1000.f, 0.f);
		const int32 FS2 = N(6000.f, -1000.f, 0.f);
		const int32 FN1 = N(3000.f, 1000.f, 0.f);
		const int32 FN2 = N(6000.f, 1000.f, 0.f);
		const int32 StationA = N(8800.f, 1000.f, 0.f);
		const int32 Spawn = N(12000.f, 0.f, 0.f);
		const int32 RampSBot = N(1250.f, -1500.f, 0.f);
		const int32 RampSTop = N(3750.f, -1500.f, LedgeZ);
		const int32 LedgeS1 = N(3750.f, -2150.f, LedgeZ);
		const int32 LedgeS2 = N(6000.f, -2150.f, LedgeZ);
		const int32 StationB = N(8200.f, -2150.f, LedgeZ);
		const int32 LiftBot = N(7400.f, -1350.f, 0.f);
		const int32 LiftTop = N(7400.f, -1350.f, LedgeZ);
		const int32 RampNBot = N(1250.f, 1500.f, 0.f);
		const int32 RampNTop = N(3750.f, 1500.f, LedgeZ);
		const int32 LedgeN1 = N(3750.f, 2150.f, LedgeZ);
		const int32 LedgeN2 = N(8000.f, 2150.f, LedgeZ);
		const int32 Cave1 = N(3000.f, 3200.f, 0.f);
		const int32 CaveGate = N(4500.f, 3200.f, 0.f);
		const int32 Cave2 = N(6500.f, 3200.f, 0.f);
		const int32 Cave3 = N(8000.f, 3200.f, 0.f);
		const int32 CaveDoorH = N(8000.f, 2200.f, 0.f);

		// ① 谷底直线（南北两岸，中间浅河走得慢）
		AddEdge(C0, FS1);
		AddEdge(FS1, FS2);
		AddEdge(C1, FN1);
		AddEdge(FN1, FN2);
		AddEdge(FS1, FN1);
		AddEdge(FN2, StationA);
		AddEdge(FS2, StationA);
		AddEdge(StationA, Spawn);
		AddEdge(FS2, Spawn);
		// ② 南侧栈道
		AddEdge(C0, RampSBot);
		AddEdge(FS1, RampSBot);
		AddEdge(RampSBot, RampSTop);
		AddEdge(RampSTop, LedgeS1);
		AddEdge(LedgeS1, LedgeS2);
		AddEdge(LedgeS2, StationB);
		AddEdge(BridgeS, LedgeS1);
		// ③ 北侧岩洞（中段落石闸）
		AddEdge(CaveC, Cave1);
		AddEdge(Cave1, CaveGate, Gate, Half);
		AddEdge(CaveGate, Cave2, Gate, Half);
		AddEdge(Cave2, Cave3);
		AddEdge(Cave3, CaveDoorH);
		AddEdge(CaveDoorH, StationA);
		AddEdge(CaveDoorH, FN2);
		// ④ 谷底坐吊笼上栈道
		AddEdge(FS2, LiftBot);
		AddEdge(LiftBot, LiftTop, Lift, Half);
		AddEdge(LiftTop, StationB);
		AddEdge(LiftTop, LedgeS2);
		// 北侧栈道（只给护送 / 拦截的人走）
		AddEdge(C1, RampNBot);
		AddEdge(FN1, RampNBot);
		AddEdge(RampNBot, RampNTop);
		AddEdge(RampNTop, LedgeN1);
		AddEdge(LedgeN1, LedgeN2);
		AddEdge(BridgeN, LedgeN1);
	}
}

// ---------------------------------------------------------------- 装配厂（?Core=Factory）
// 三层：上层栈桥（Z +1200）、中层车间（Z 0，地面）、底层物流（Z -700，层高 7 米）
void AMechCoreArena::BuildFactory()
{
	constexpr float MidZ = 0.f;
	constexpr float LowZ = -700.f;
	constexpr float TopZ = 1200.f;
	constexpr float HalfLen = 10000.f;
	constexpr float HalfWide = 3500.f;
	constexpr float RampTopX = 1400.f;
	constexpr float RampLowX = 3400.f;
	constexpr float CorrY0 = 600.f;
	constexpr float CorrY1 = 1800.f;
	constexpr float RoomX0 = 5000.f;
	constexpr float RoomX1 = 8800.f;
	constexpr float RoomY0 = -3000.f;
	constexpr float LiftX = 5700.f;
	constexpr float LiftY = -300.f;
	constexpr float LiftHalf = 500.f;
	constexpr float GateX = 4300.f;

	Layers = {LowZ, MidZ, TopZ};
	HalfExtent = FVector2D(9800.f, 3400.f);
	StationNames[0] = TEXT("中层");
	StationNames[1] = TEXT("底层");
	FoundryPt = FVector(0.f, 0.f, MidZ);
	for (int32 Half = 0; Half < 2; ++Half)
	{
		const float S = Half == 0 ? -1.f : 1.f;
		SpawnPt[Half] = FVector(S * 9300.f, 0.f, MidZ);
		StationPt[Half][0] = FVector(S * 7000.f, -800.f, MidZ);
		StationPt[Half][1] = FVector(S * 7400.f, 900.f, LowZ);
		LiftXY[Half] = FVector(S * LiftX, LiftY, 0.f);
		GateXY[Half] = FVector(S * GateX, (CorrY0 + CorrY1) * 0.5f, 0.f);
		LampAt[Half] = FVector(S * GateX, (CorrY0 + CorrY1) * 0.5f, MidZ - 120.f);
	}
	LiftSize = LiftHalf * 2.f - 30.f;
	LiftHighZ = MidZ;
	LiftLowZ = LowZ;
	GateSize = FVector(100.f, CorrY1 - CorrY0, MidZ - 100.f - LowZ);
	GateShutZ = (LowZ + MidZ - 100.f) * 0.5f;
	GateOpenZ = LowZ - 400.f;
	LampSize = FVector(60.f, CorrY1 - CorrY0, 25.f);

	// 小地图
	MapLo = FVector2D(-HalfLen - 200.f, -HalfWide - 200.f);
	MapHi = FVector2D(HalfLen + 200.f, HalfWide + 200.f);
	AddMapRect(MapLo.X, MapLo.Y, MapHi.X, MapHi.Y, 3);
	AddMapRect(-HalfLen, -HalfWide, HalfLen, HalfWide, 0);
	AddMapRect(-RoomX0, CorrY0, RoomX0, CorrY1, 2);
	for (const float S : {-1.f, 1.f})
	{
		AddMapRect(S * RoomX0, RoomY0, S * RoomX1, CorrY1, 2);
		AddMapRect(-HalfLen, S * 2300.f, HalfLen, S * 2500.f, 3);
		for (const float SY : {-1.f, 1.f})
		{
			AddMapRect(S * 2000.f, SY * 1700.f, S * 7000.f, SY * 2300.f, 1);
			AddMapRect(0.f, SY * 1700.f, S * 1500.f, SY * 2300.f, 1);
		}
	}
	AddMapRect(-300.f, -1700.f, 300.f, 1700.f, 1);

	UMaterialInterface* Grid = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));
	UMaterialInterface* Grid2 = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray_02.MI_PrototypeGrid_Gray_02"));
	UMaterialInterface* Dark = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark.MI_PrototypeGrid_TopDark"));
	UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Weapons/Materials/MI_Tracer.MI_Tracer"));

	UInstancedStaticMeshComponent* Floors = NewISM(TEXT("Floors"), Grid);
	UInstancedStaticMeshComponent* Walls = NewISM(TEXT("Walls"), Grid2);
	UInstancedStaticMeshComponent* Cover = NewISM(TEXT("Cover"), Dark);
	UInstancedStaticMeshComponent* Belts[2] = {NewISM(TEXT("BeltWest"), Dark), NewISM(TEXT("BeltEast"), Dark)};
	// 传送带：带标签的地面，移动组件按标签给顺向 / 逆向的速度倍数（西半边往 -X 送、东半边往 +X 送，都是往各自的家送）
	Belts[0]->ComponentTags.Add(TEXT("ConveyorNeg"));
	Belts[1]->ComponentTags.Add(TEXT("ConveyorPos"));

	// 中层地面（挖出坡道口和货梯井）
	TArray<FBox2D> MidHoles;
	for (const float S : {-1.f, 1.f})
	{
		MidHoles.Add(FBox2D(FVector2D(FMath::Min(S * RampTopX, S * RampLowX), CorrY0), FVector2D(FMath::Max(S * RampTopX, S * RampLowX), CorrY1)));
		MidHoles.Add(FBox2D(FVector2D(S * LiftX - LiftHalf, LiftY - LiftHalf), FVector2D(S * LiftX + LiftHalf, LiftY + LiftHalf)));
	}
	AddSlab(Floors, MidZ, 100.f, FBox2D(FVector2D(-HalfLen, -HalfWide), FVector2D(HalfLen, HalfWide)), MidHoles);

	// 外墙 25 米高，不加顶（灰盒阶段亮一点）
	for (const float S : {-1.f, 1.f})
	{
		AddBox(Walls, FVector(S * (HalfLen + 100.f), 0.f, 1250.f), FVector(200.f, HalfWide * 2.f + 400.f, 2500.f));
		AddBox(Walls, FVector(0.f, S * (HalfWide + 100.f), 1250.f), FVector(HalfLen * 2.f + 400.f, 200.f, 2500.f));
	}
	// 大厅和两侧维修侧廊之间的装配线隔墙（6 米高，飞得过去），三处门洞
	const TArray<FVector2D> SideSegs = {{-10000.f, -8000.f}, {-7200.f, -4800.f}, {-4000.f, -1800.f}, {-1000.f, 1000.f}, {1800.f, 4000.f}, {4800.f, 7200.f}, {8000.f, 10000.f}};
	for (const float SY : {-1.f, 1.f})
	{
		for (const FVector2D& Seg : SideSegs)
		{
			AddBox(Walls, FVector((Seg.X + Seg.Y) * 0.5f, SY * 2400.f, 300.f), FVector(Seg.Y - Seg.X, 200.f, 600.f));
		}
	}
	// 中央铸造台：四根柱子围着，核心在正中间地面上出；上层一座天桥把南北栈桥连起来
	for (const float SX : {-1.f, 1.f})
	{
		for (const float SY : {-1.f, 1.f})
		{
			AddBox(Cover, FVector(SX * 450.f, SY * 450.f, 350.f), FVector(300.f, 300.f, 700.f));
		}
	}
	AddBox(Cover, FVector(0.f, 0.f, 760.f), FVector(1200.f, 1200.f, 120.f));
	AddBox(Floors, FVector(0.f, 0.f, TopZ - 30.f), FVector(600.f, 3400.f, 60.f));

	for (const float S : {-1.f, 1.f})
	{
		// 大厅掩体：高的像吊着的半成品机体，矮的是货箱
		AddBox(Cover, FVector(S * 2600.f, -600.f, 450.f), FVector(400.f, 400.f, 900.f));
		AddBox(Cover, FVector(S * 4000.f, -2050.f, 450.f), FVector(400.f, 400.f, 900.f));
		AddBox(Cover, FVector(S * 8200.f, 1600.f, 450.f), FVector(400.f, 400.f, 900.f));
		for (const FVector2D& C : {FVector2D(1500.f, -800.f), FVector2D(3200.f, -1000.f), FVector2D(5000.f, 1200.f), FVector2D(6600.f, -2000.f), FVector2D(8400.f, -1800.f)})
		{
			AddBox(Cover, FVector(S * C.X, C.Y, 100.f), FVector(300.f, 300.f, 200.f));
		}
		for (const float SY : {-1.f, 1.f})
		{
			AddBox(Cover, FVector(S * 2800.f, SY * 2750.f, 120.f), FVector(400.f, 300.f, 240.f));
			AddBox(Cover, FVector(S * 5800.f, SY * 3350.f, 120.f), FVector(500.f, 200.f, 240.f));
			AddBox(Cover, FVector(S * 8800.f, SY * 2750.f, 120.f), FVector(400.f, 300.f, 240.f));
			// 上层栈桥（两条长栈桥中间各断两个 5 米的缺口）和柱子
			for (const FVector2D& Seg : {FVector2D(2000.f, 7000.f), FVector2D(0.f, 1500.f)})
			{
				AddBox(Floors, FVector(S * (Seg.X + Seg.Y) * 0.5f, SY * 2000.f, TopZ - 30.f), FVector(Seg.Y - Seg.X, 600.f, 60.f));
			}
			for (const float PX : {3000.f, 6000.f})
			{
				AddBox(Walls, FVector(S * PX, SY * 2000.f, (TopZ - 60.f) * 0.5f), FVector(200.f, 200.f, TopZ - 60.f));
			}
		}
		// 坡道：中层下到底层主通道
		AddRamp(Floors, FVector(S * RampTopX, (CorrY0 + CorrY1) * 0.5f, MidZ), FVector(S * RampLowX, (CorrY0 + CorrY1) * 0.5f, LowZ), CorrY1 - CorrY0);
		// 底层：货仓地面、墙（墙顶顶到中层楼板底）
		AddSlab(Floors, LowZ, 100.f, FBox2D(FVector2D(FMath::Min(S * RoomX0, S * RoomX1), RoomY0), FVector2D(FMath::Max(S * RoomX0, S * RoomX1), CorrY1)), {});
		const float WallZ = (LowZ + MidZ - 100.f) * 0.5f;
		const float WallH = MidZ - 100.f - LowZ;
		AddBox(Walls, FVector(S * (RoomX0 - 100.f), (RoomY0 + CorrY0) * 0.5f, WallZ), FVector(200.f, CorrY0 - RoomY0, WallH));
		AddBox(Walls, FVector(S * (RoomX0 + RoomX1) * 0.5f, RoomY0 - 100.f, WallZ), FVector(RoomX1 - RoomX0 + 400.f, 200.f, WallH));
		AddBox(Walls, FVector(S * (RoomX1 + 100.f), (RoomY0 + CorrY1) * 0.5f, WallZ), FVector(200.f, CorrY1 - RoomY0 + 400.f, WallH));
		AddBox(Cover, FVector(S * 6400.f, -2200.f, LowZ + 200.f), FVector(1200.f, 300.f, 400.f));
		AddBox(Cover, FVector(S * 8000.f, -400.f, LowZ + 150.f), FVector(300.f, 800.f, 300.f));
		AddBox(Belts[S < 0.f ? 0 : 1], FVector(S * (RampLowX + RoomX0) * 0.5f, (CorrY0 + CorrY1) * 0.5f, LowZ + 5.f),
			FVector(RoomX0 - RampLowX - 100.f, CorrY1 - CorrY0 - 200.f, 10.f));
		for (const FVector2D& P : {FVector2D(6000.f, -1600.f), FVector2D(7800.f, -1600.f), FVector2D(6000.f, 800.f), FVector2D(7800.f, 800.f)})
		{
			AddLight(FVector(S * P.X, P.Y, LowZ + 520.f), 2500.f, 2600.f, FLinearColor(1.f, 0.86f, 0.7f));
		}
		for (const float PX : {1800.f, 3600.f})
		{
			AddLight(FVector(S * PX, (CorrY0 + CorrY1) * 0.5f, LowZ + 520.f), 2000.f, 2200.f, FLinearColor(0.75f, 0.85f, 1.f));
		}
	}
	// 底层主通道地面和两侧墙
	AddSlab(Floors, LowZ, 100.f, FBox2D(FVector2D(-RoomX0, CorrY0), FVector2D(RoomX0, CorrY1)), {});
	{
		const float WallZ = (LowZ + MidZ - 100.f) * 0.5f;
		const float WallH = MidZ - 100.f - LowZ;
		AddBox(Walls, FVector(0.f, CorrY1 + 100.f, WallZ), FVector(RoomX1 * 2.f + 400.f, 200.f, WallH));
		AddBox(Walls, FVector(0.f, CorrY0 - 100.f, WallZ), FVector((RoomX0 - 200.f) * 2.f, 200.f, WallH));
	}
	BuildMovers(Dark, Walls->GetMaterial(0), Glow);
}

void AMechCoreArena::GraphFactory()
{
	constexpr float MidZ = 0.f;
	constexpr float LowZ = -700.f;
	const int32 Foundry = AddNode(FVector(0.f, 0.f, MidZ));
	const int32 C0 = AddNode(FVector(0.f, -1600.f, MidZ));
	const int32 C1 = AddNode(FVector(0.f, 1150.f, MidZ));
	AddEdge(Foundry, C0);
	AddEdge(Foundry, C1);
	for (int32 Half = 0; Half < 2; ++Half)
	{
		const float S = Half == 0 ? -1.f : 1.f;
		auto N = [this, S](float X, float Y, float Z) { return AddNode(FVector(S * X, Y, Z)); };
		const int32 HallS1 = N(2000.f, -1600.f, MidZ);
		const int32 HallS2 = N(4200.f, -1600.f, MidZ);
		const int32 HallS3 = N(6200.f, -1600.f, MidZ);
		const int32 StationA = N(7000.f, -800.f, MidZ);
		const int32 Spawn = N(9300.f, 0.f, MidZ);
		const int32 HallC1 = N(4000.f, 200.f, MidZ);
		const int32 HallC2 = N(7600.f, 800.f, MidZ);
		const int32 RampTop = N(1150.f, 1200.f, MidZ);
		const int32 RampBot = N(3700.f, 1200.f, LowZ);
		const int32 GateN = N(4300.f, 1200.f, LowZ);
		const int32 RoomIn = N(5500.f, 1200.f, LowZ);
		const int32 StationB = N(7400.f, 900.f, LowZ);
		const int32 RoomMid = N(6800.f, -1000.f, LowZ);
		const int32 LiftTop = N(5700.f, -300.f, MidZ);
		const int32 LiftBot = N(5700.f, -300.f, LowZ);
		const int32 DoorS1 = N(1400.f, -2400.f, MidZ);
		const int32 SideS1 = N(1400.f, -3050.f, MidZ);
		const int32 SideS2 = N(4400.f, -3050.f, MidZ);
		const int32 SideS3 = N(7600.f, -3050.f, MidZ);
		const int32 DoorS3 = N(7600.f, -2400.f, MidZ);
		const int32 DoorN1 = N(1400.f, 2400.f, MidZ);
		const int32 SideN1 = N(1400.f, 3050.f, MidZ);
		const int32 SideN2 = N(4400.f, 3050.f, MidZ);
		const int32 SideN3 = N(7600.f, 3050.f, MidZ);
		const int32 DoorN3 = N(7600.f, 2400.f, MidZ);
		AddEdge(C0, HallS1);
		AddEdge(HallS1, HallS2);
		AddEdge(HallS2, HallS3);
		AddEdge(HallS3, StationA);
		AddEdge(StationA, Spawn);
		AddEdge(HallS1, HallC1);
		AddEdge(HallC1, HallC2);
		AddEdge(HallC2, StationA);
		AddEdge(HallC2, Spawn);
		AddEdge(C0, DoorS1);
		AddEdge(HallS1, DoorS1);
		AddEdge(DoorS1, SideS1);
		AddEdge(SideS1, SideS2);
		AddEdge(SideS2, SideS3);
		AddEdge(SideS3, DoorS3);
		AddEdge(DoorS3, StationA);
		AddEdge(DoorS3, Spawn);
		AddEdge(C1, DoorN1);
		AddEdge(DoorN1, SideN1);
		AddEdge(SideN1, SideN2);
		AddEdge(SideN2, SideN3);
		AddEdge(SideN3, DoorN3);
		AddEdge(DoorN3, HallC2);
		AddEdge(C1, RampTop);
		AddEdge(RampTop, RampBot);
		AddEdge(RampBot, GateN, Gate, Half);
		AddEdge(GateN, RoomIn, Gate, Half);
		AddEdge(RoomIn, StationB);
		AddEdge(RoomIn, RoomMid);
		AddEdge(RoomMid, StationB);
		AddEdge(HallS2, LiftTop);
		AddEdge(HallC1, LiftTop);
		AddEdge(LiftTop, LiftBot, Lift, Half);
		AddEdge(LiftBot, RoomMid);
		AddEdge(LiftBot, StationB);
	}
}

// ---------------------------------------------------------------- 自然地形（地图里的 CoreArena 点带 CoreTerrain 标签）
// 不搭灰盒：地形、路线、掩体都是地图自己的（山谷包原地形，ue_valley_setup.py / ue_valley_arena.py 摆进地图）。
// 地图里带标签的点：CoreSpawn + Half0/Half1、CoreStation + Half0/Half1 + A/B、CoreFoundry、CoreNode + Id=N + Link=M（可多个）；
// CoreArena 点上的 HalfX= / HalfY= 是活动范围半长 / 半宽（厘米）。树干、石头的隐形碰撞是地图里带 CoreObstacles 标签的 actor。
bool AMechCoreArena::BuildTerrain()
{
	AActor* Marker = nullptr;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("CoreArena")) && It->ActorHasTag(TEXT("CoreTerrain")))
		{
			Marker = *It;
			break;
		}
	}
	if (!Marker)
	{
		return false;
	}
	auto TagNumbers = [](const AActor* A, const TCHAR* Key) {
		TArray<float> Out;
		const FString Prefix = FString(Key) + TEXT("=");
		for (const FName& T : A->Tags)
		{
			const FString S = T.ToString();
			if (S.StartsWith(Prefix))
			{
				Out.Add(FCString::Atof(*S.Mid(Prefix.Len())));
			}
		}
		return Out;
	};
	const FVector O = GetActorLocation();
	HalfExtent = FVector2D(52000.f, 27000.f);
	if (const TArray<float> V = TagNumbers(Marker, TEXT("HalfX")); V.Num())
	{
		HalfExtent.X = V[0];
	}
	if (const TArray<float> V = TagNumbers(Marker, TEXT("HalfY")); V.Num())
	{
		HalfExtent.Y = V[0];
	}
	StationNames[0] = TEXT("中路");
	StationNames[1] = TEXT("侧路");
	FoundryPt = FVector::ZeroVector;
	for (int32 Half = 0; Half < 2; ++Half)
	{
		const float S = Half == 0 ? -1.f : 1.f;
		SpawnPt[Half] = FVector(S * HalfExtent.X * 0.74f, 0.f, 0.f);
		StationPt[Half][0] = FVector(S * HalfExtent.X * 0.58f, 0.f, 0.f);
		StationPt[Half][1] = FVector(S * HalfExtent.X * 0.56f, S * 9000.f, 0.f);
	}

	// 地图里的标签点 → 出生 / 回收站 / 铸造台 / 路点（坐标相对场地原点）
	TMap<int32, int32> IdToNode;
	TArray<TPair<int32, int32>> Links;
	TArray<AActor*> Obstacles;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* A = *It;
		const FVector P = A->GetActorLocation() - O;
		const int32 Half = A->ActorHasTag(TEXT("Half1")) ? 1 : 0;
		if (A->ActorHasTag(TEXT("CoreObstacles")))
		{
			Obstacles.Add(A);
		}
		else if (A->ActorHasTag(TEXT("CoreSpawn")))
		{
			SpawnPt[Half] = P;
		}
		else if (A->ActorHasTag(TEXT("CoreStation")))
		{
			StationPt[Half][A->ActorHasTag(TEXT("B")) ? 1 : 0] = P;
		}
		else if (A->ActorHasTag(TEXT("CoreFoundry")))
		{
			FoundryPt = P;
		}
		else if (A->ActorHasTag(TEXT("CoreNode")))
		{
			const TArray<float> Id = TagNumbers(A, TEXT("Id"));
			if (Id.Num())
			{
				IdToNode.Add(FMath::RoundToInt(Id[0]), AddNode(P));
				for (const float L : TagNumbers(A, TEXT("Link")))
				{
					Links.Add({FMath::RoundToInt(Id[0]), FMath::RoundToInt(L)});
				}
			}
		}
	}
	for (const TPair<int32, int32>& L : Links)
	{
		// 每段路两头的点都记了对方，只按小号到大号加一次
		if (L.Key < L.Value && IdToNode.Contains(L.Key) && IdToNode.Contains(L.Value))
		{
			AddEdge(IdToNode[L.Key], IdToNode[L.Value]);
		}
	}

	// 小地图：20 米一格采样地形高度（不打树、石头的隐形碰撞），按相对铸造台的高低和坡度分类，同类格子按行合并
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoreTerrainMap), false);
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActors(Obstacles);
	constexpr float Cell = 2000.f;
	const int32 NX = FMath::CeilToInt(2.f * HalfExtent.X / Cell);
	const int32 NY = FMath::CeilToInt(2.f * HalfExtent.Y / Cell);
	TArray<float> Hgt;
	Hgt.SetNumZeroed((NX + 1) * (NY + 1));
	float MinZ = FoundryPt.Z, MaxZ = FoundryPt.Z;
	for (int32 j = 0; j <= NY; ++j)
	{
		for (int32 i = 0; i <= NX; ++i)
		{
			const FVector At = O + FVector(-HalfExtent.X + i * Cell, -HalfExtent.Y + j * Cell, 0.f);
			FHitResult Hit;
			float Z = FoundryPt.Z;
			if (GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0.f, 0.f, 300000.f), At - FVector(0.f, 0.f, 300000.f),
				FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{
				Z = Hit.ImpactPoint.Z - O.Z;
			}
			Hgt[j * (NX + 1) + i] = Z;
			MinZ = FMath::Min(MinZ, Z);
			MaxZ = FMath::Max(MaxZ, Z);
		}
	}
	MapLo = -HalfExtent;
	MapHi = HalfExtent;
	const float SteepRise = Cell * FMath::Tan(FMath::DegreesToRadians(38.f));
	for (int32 j = 0; j < NY; ++j)
	{
		int32 RunStart = 0;
		uint8 RunKind = 255;
		for (int32 i = 0; i <= NX; ++i)
		{
			uint8 Kind = 255;
			if (i < NX)
			{
				const float C[4] = {Hgt[j * (NX + 1) + i], Hgt[j * (NX + 1) + i + 1], Hgt[(j + 1) * (NX + 1) + i], Hgt[(j + 1) * (NX + 1) + i + 1]};
				const float Lo = FMath::Min(FMath::Min(C[0], C[1]), FMath::Min(C[2], C[3]));
				const float Hi = FMath::Max(FMath::Max(C[0], C[1]), FMath::Max(C[2], C[3]));
				const float Mid = (C[0] + C[1] + C[2] + C[3]) * 0.25f - FoundryPt.Z;
				Kind = Hi - Lo > SteepRise ? 3 : Mid < -800.f ? 2 : Mid > 1200.f ? 1 : 0;
			}
			if (Kind != RunKind)
			{
				if (RunKind != 255)
				{
					AddMapRect(-HalfExtent.X + RunStart * Cell, -HalfExtent.Y + j * Cell, FMath::Min(HalfExtent.X, -HalfExtent.X + i * Cell),
						FMath::Min(HalfExtent.Y, -HalfExtent.Y + (j + 1) * Cell), RunKind);
				}
				RunStart = i;
				RunKind = Kind;
			}
		}
	}
	Layers = {MinZ - 3000.f};
	CenterZOffset = MinZ + 1500.f;

	// 铸造台（四根柱子插进地里 + 顶），地图是自然地形，用混凝土。不垫平台：坡地上平台下坡那边会露出几米高的墙，带芯的跳不上去
	UMaterialInterface* Concrete = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Env/Materials/MI_ConcreteWall.MI_ConcreteWall"));
	UInstancedStaticMeshComponent* Foundry = NewISM(TEXT("Foundry"), Concrete);
	for (const float SX : {-1.f, 1.f})
	{
		for (const float SY : {-1.f, 1.f})
		{
			AddBox(Foundry, FoundryPt + FVector(SX * 450.f, SY * 450.f, 100.f), FVector(300.f, 300.f, 1300.f));
		}
	}
	AddBox(Foundry, FoundryPt + FVector(0.f, 0.f, 750.f), FVector(1200.f, 1200.f, 100.f));

	// 隐形边界：四面墙 + 顶，飞出活动范围就挡住（地形本身 8 公里，外面不会掉下去）
	UInstancedStaticMeshComponent* Bounds = NewISM(TEXT("Bounds"), nullptr);
	Bounds->SetHiddenInGame(true);
	Bounds->SetCastShadow(false);
	Bounds->SetAffectDistanceFieldLighting(false);
	Bounds->SetAffectDynamicIndirectLighting(false);
	const float Z0 = MinZ - 5000.f, Z1 = MaxZ + 15000.f, T = 400.f;
	const float HX = HalfExtent.X, HY = HalfExtent.Y;
	for (const float S : {-1.f, 1.f})
	{
		AddSpan(Bounds, FVector(S * HX, -HY - T, Z0), FVector(S * (HX + T), HY + T, Z1));
		AddSpan(Bounds, FVector(-HX - T, S * HY, Z0), FVector(HX + T, S * (HY + T), Z1));
	}
	AddSpan(Bounds, FVector(-HX - T, -HY - T, Z1), FVector(HX + T, HY + T, Z1 + T));

	UE_LOG(LogTemp, Display, TEXT("[core] 自然地形：活动范围 %.0f×%.0f 米，地面 %.0f~%.0f 米，路点 %d、路段 %d，障碍 actor %d，小地图 %d 块"),
		HalfExtent.X * 0.02f, HalfExtent.Y * 0.02f, MinZ * 0.01f, MaxZ * 0.01f, Nodes.Num(), Edges.Num(), Obstacles.Num(), MapShapes.Num());
	return true;
}

// ---------------------------------------------------------------- 机关
void AMechCoreArena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float T = NetTime();
	const float G = FMath::Fmod(T, GateCycle);
	// 0 开、1 关：第 20 秒起 0.5 秒落下，第 29.5 秒起 0.5 秒升起
	const float Closed = G < GateCloseAt ? 0.f : G < GateCloseAt + 0.5f ? (G - GateCloseAt) / 0.5f : G < GateCycle - 0.5f ? 1.f : (GateCycle - G) / 0.5f;
	const bool bWarn = G > GateCloseAt - 3.f && G < GateCloseAt;
	for (int32 Half = 0; Half < 2; ++Half)
	{
		if (Lifts[Half])
		{
			Lifts[Half]->SetRelativeLocation(FVector(LiftXY[Half].X, LiftXY[Half].Y, FMath::Lerp(LiftHighZ, LiftLowZ, LiftDepth(Half)) - 30.f));
		}
		if (Gates[Half])
		{
			Gates[Half]->SetRelativeLocation(FVector(GateXY[Half].X, GateXY[Half].Y, FMath::Lerp(GateOpenZ, GateShutZ, Closed)));
		}
		if (GateLampMat[Half])
		{
			const bool bOn = Closed > 0.f || (bWarn && FMath::Fmod(T, 0.5f) < 0.25f);
			GateLampMat[Half]->SetScalarParameterValue(TEXT("EmissiveStrength"), bOn ? 60.f : 2.f);
		}
	}
}

bool AMechCoreArena::IsGateClosed(int32 Half, float Lookahead) const
{
	const float G0 = FMath::Fmod(NetTime(), GateCycle);
	const float G1 = FMath::Fmod(NetTime() + Lookahead, GateCycle);
	return G0 >= GateCloseAt || G1 >= GateCloseAt;
}

float AMechCoreArena::LiftDepth(int32 Half) const
{
	const float L = FMath::Fmod(NetTime(), LiftCycle);
	if (L < 4.f)
	{
		return 0.f;
	}
	if (L < 6.f)
	{
		return FMath::SmoothStep(0.f, 1.f, (L - 4.f) / 2.f);
	}
	if (L < 10.f)
	{
		return 1.f;
	}
	return 1.f - FMath::SmoothStep(0.f, 1.f, (L - 10.f) / 2.f);
}

FVector AMechCoreArena::LiftCenter(int32 Half, bool bLow) const
{
	const FVector& XY = LiftXY[Half == 0 ? 0 : 1];
	return GetActorLocation() + FVector(XY.X, XY.Y, bLow ? LiftLowZ : LiftHighZ);
}

int32 AMechCoreArena::LayerOf(const FVector& P) const
{
	const float Z = P.Z - GetActorLocation().Z + 50.f;
	int32 Layer = 0;
	for (int32 i = 0; i < Layers.Num(); ++i)
	{
		if (Layers[i] <= Z)
		{
			Layer = i;
		}
	}
	return Layer;
}

// ---------------------------------------------------------------- 路点图
int32 AMechCoreArena::AddNode(const FVector& P)
{
	return Nodes.Add({P});
}

void AMechCoreArena::AddEdge(int32 A, int32 B, uint8 Kind, int32 Half)
{
	Edges.Add({A, B, Kind, Half});
}

int32 AMechCoreArena::NearestNode(const FVector& P) const
{
	if (Layout == 2)
	{
		// 自然地形：只看水平距离会挑到隔着坡坎 / 树的点（直线走不过去，AI 在两个点之间来回切）。
		// 按水平距离 + 高差排前 6 个，挑第一个从膝盖高度打过去没被地形、树石挡住的
		TArray<TPair<float, int32>> Cands;
		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			const float DZ = FMath::Abs(Nodes[i].P.Z - P.Z);
			Cands.Add({FVector::Dist2D(Nodes[i].P, P) + 2.f * FMath::Max(0.f, DZ - 200.f), i});
		}
		Cands.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CoreNearestNode), false);
		for (int32 k = 0; k < FMath::Min(6, Cands.Num()); ++k)
		{
			const FVector N = Nodes[Cands[k].Value].P;
			FHitResult Hit;
			if (!GetWorld()->LineTraceSingleByObjectType(Hit, P + FVector(0.f, 0.f, 60.f), N + FVector(0.f, 0.f, 60.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{
				return Cands[k].Value;
			}
		}
		return Cands.Num() ? Cands[0].Value : INDEX_NONE;
	}
	// 在最高层（飞上去的地方）就当在下一层，走下去
	const int32 Layer = FMath::Min(LayerOf(P), FMath::Max(0, Layers.Num() - 2));
	int32 Best = INDEX_NONE;
	float BestD = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		float D = FVector::Dist2D(Nodes[i].P, P);
		if (LayerOf(Nodes[i].P) != Layer)
		{
			D += 100000.f;
		}
		if (D < BestD)
		{
			BestD = D;
			Best = i;
		}
	}
	return Best;
}

bool AMechCoreArena::FindPath(const FVector& From, const FVector& To, TArray<FVector>& Out, TArray<uint8>* OutKinds) const
{
	Out.Reset();
	if (OutKinds)
	{
		OutKinds->Reset();
	}
	const int32 A = NearestNode(From);
	const int32 B = NearestNode(To);
	if (A == INDEX_NONE || B == INDEX_NONE)
	{
		return false;
	}
	// 小图，直接 Dijkstra。闸门 4 秒内要关就当走不通；升降台多算一段等的时间
	TArray<float> Dist;
	TArray<int32> Prev;
	TArray<uint8> PrevKind;
	TArray<bool> Done;
	Dist.Init(TNumericLimits<float>::Max(), Nodes.Num());
	Prev.Init(INDEX_NONE, Nodes.Num());
	PrevKind.Init(Walk, Nodes.Num());
	Done.Init(false, Nodes.Num());
	Dist[A] = 0.f;
	for (int32 Iter = 0; Iter < Nodes.Num(); ++Iter)
	{
		int32 U = INDEX_NONE;
		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			if (!Done[i] && (U == INDEX_NONE || Dist[i] < Dist[U]))
			{
				U = i;
			}
		}
		if (U == INDEX_NONE || Dist[U] == TNumericLimits<float>::Max())
		{
			break;
		}
		Done[U] = true;
		if (U == B)
		{
			break;
		}
		for (const FEdge& E : Edges)
		{
			if (E.A != U && E.B != U)
			{
				continue;
			}
			const int32 V = E.A == U ? E.B : E.A;
			if (E.Kind == Gate && IsGateClosed(E.Half, 4.f))
			{
				continue;
			}
			const float Cost = FVector::Dist(Nodes[U].P, Nodes[V].P) + (E.Kind == Lift ? 2500.f : 0.f);
			if (Dist[U] + Cost < Dist[V])
			{
				Dist[V] = Dist[U] + Cost;
				Prev[V] = U;
				PrevKind[V] = E.Kind;
			}
		}
	}
	if (Prev[B] == INDEX_NONE && A != B)
	{
		return false;
	}
	TArray<int32> Rev;
	for (int32 V = B; V != INDEX_NONE; V = Prev[V])
	{
		Rev.Add(V);
	}
	for (int32 i = Rev.Num() - 1; i >= 0; --i)
	{
		Out.Add(Nodes[Rev[i]].P);
		if (OutKinds)
		{
			OutKinds->Add(PrevKind[Rev[i]]);
		}
	}
	Out.Add(To);
	if (OutKinds)
	{
		OutKinds->Add(Walk);
	}
	return true;
}

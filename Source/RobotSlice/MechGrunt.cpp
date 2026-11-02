#include "MechGrunt.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MechCharacter.h"
#include "MechDefense.h"
#include "MechHealthComponent.h"
#include "MechRun.h"
#include "MechScore.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace MechGruntLocal
{
	template <typename T>
	T* Load(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<T> Finder(Path);
		return Finder.Object;
	}

	// ---- 数值（第一版，按「打几枪掉」「躲不躲得开」定的，试玩后再调）
	// 自爆无人机
	constexpr float KamikazeSpeed = 2000.f;
	constexpr float KamikazeArmDist = 650.f;     // 离目标这么近就停下倒数
	constexpr float KamikazeArmTime = 1.f;
	constexpr float KamikazeRadius = 700.f;
	constexpr float KamikazeDamage = 900.f;
	constexpr float KamikazeCoreDamage = 600.f;
	// 机枪无人机
	constexpr float GunnerSpeed = 1600.f;
	constexpr float GunnerOrbit = 4000.f;
	constexpr float GunnerShotSpeed = 6500.f;   // 慢：40 米外 0.6 秒才到，侧闪躲得开
	constexpr float GunnerDamage = 70.f;
	constexpr float GunnerCoreDamage = 40.f;
	constexpr int32 GunnerBurst = 5;
	// 履带炮车
	constexpr float TankSpeed = 450.f;
	constexpr float TankAimTime = 1.5f;
	constexpr float TankLockBefore = 0.35f;     // 开炮前这么久瞄准线定住（之后躲开就打不中）
	constexpr float TankShellSpeed = 5500.f;
	constexpr float TankShellGravity = 400.f;
	constexpr float TankDamage = 1100.f;
	constexpr float TankCoreDamage = 900.f;
	constexpr float TankBlast = 500.f;
	constexpr float TankCooldown = 4.f;
	// 迫击炮
	constexpr float MortarWarn = 2.f;
	constexpr float MortarRadius = 700.f;
	constexpr float MortarDamage = 800.f;
	constexpr float MortarCoreDamage = 500.f;

	const FLinearColor Red(1.f, 0.12f, 0.06f);

	/** 让圆柱（轴沿局部 Z）从 A 指到 B 的旋转 */
	FRotator AlongZ(const FVector& Dir)
	{
		return FRotationMatrix::MakeFromZ(Dir.GetSafeNormal()).Rotator();
	}
}

using namespace MechGruntLocal;

float AMechGrunt::BaseHP(EMechGrunt K)
{
	switch (K)
	{
	case EMechGrunt::Kamikaze:
		return 160.f;   // 步枪两枪
	case EMechGrunt::Gunner:
		return 450.f;
	case EMechGrunt::Tank:
		return 3200.f;  // 要集火
	case EMechGrunt::Mortar:
		return 2600.f;
	}
	return 100.f;
}

const TCHAR* AMechGrunt::KindName(EMechGrunt K)
{
	switch (K)
	{
	case EMechGrunt::Kamikaze:
		return TEXT("自爆无人机");
	case EMechGrunt::Gunner:
		return TEXT("机枪无人机");
	case EMechGrunt::Tank:
		return TEXT("履带炮车");
	case EMechGrunt::Mortar:
		return TEXT("迫击炮");
	}
	return TEXT("?");
}

AMechGrunt::AMechGrunt()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(false);
	bAlwaysRelevant = true;  // 一波最多几十个，远处的也要同步（不然刷怪点在 150 米外，客户端看不见来敌）
	SetNetUpdateFrequency(15.f);

	// 根：打得中的球（照地雷：物体类型 WorldDynamic、只查询；炮车在 BuildVisuals 里改成挡机体）
	HitBox = CreateDefaultSubobject<USphereComponent>(TEXT("HitBox"));
	RootComponent = HitBox;
	HitBox->InitSphereRadius(110.f);
	HitBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitBox->SetCollisionObjectType(ECC_WorldDynamic);
	HitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitBox->SetGenerateOverlapEvents(false);
	Tags.Add(TEXT("MechShootable"));
	Tags.Add(TEXT("MechGrunt"));

	auto Part = [this](const TCHAR* Name) {
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(HitBox);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->bAffectDistanceFieldLighting = false;
		C->bAffectDynamicIndirectLighting = false;
		return C;
	};
	Body = Part(TEXT("Body"));
	Turret = Part(TEXT("Turret"));
	Barrel = Part(TEXT("Barrel"));
	Eye = Part(TEXT("Eye"));
	Eye->SetCastShadow(false);
	Laser = Part(TEXT("Laser"));
	Laser->SetCastShadow(false);
	Laser->SetUsingAbsoluteLocation(true);
	Laser->SetUsingAbsoluteRotation(true);
	Laser->SetUsingAbsoluteScale(true);
	Laser->SetVisibility(false);
	Ring = Part(TEXT("Ring"));
	Ring->SetCastShadow(false);
	Ring->SetUsingAbsoluteLocation(true);
	Ring->SetUsingAbsoluteRotation(true);
	Ring->SetUsingAbsoluteScale(true);
	Ring->SetVisibility(false);

	CubeMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube"));
	CylinderMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder"));
	SphereMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere"));
	PlaneMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Plane"));
	MissileMesh = Load<UStaticMesh>(TEXT("/Game/Weapons/Meshes/SM_Missile"));
	PodMesh = Load<UStaticMesh>(TEXT("/Game/Weapons/Meshes/SM_MissilePod"));
	MetalMat = Load<UMaterialInterface>(TEXT("/Game/Robot/Materials/MI_Robot_PaintDark"));
	GlowMat = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/MI_Tracer"));
	RingMat = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_ShockRing"));
	ExplosionFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_02_Exp_01"));
	SmallExplosionFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"));
	ExplosionSound = Load<USoundBase>(TEXT("/Game/Audio/Combat/S_explosion_small"));
}

void AMechGrunt::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechGrunt, Kind);
	DOREPLIFETIME(AMechGrunt, RepPos);
	DOREPLIFETIME(AMechGrunt, RepVel);
	DOREPLIFETIME(AMechGrunt, RepYaw);
	DOREPLIFETIME(AMechGrunt, RepAimYaw);
	DOREPLIFETIME(AMechGrunt, HP);
	DOREPLIFETIME(AMechGrunt, MaxHP);
	DOREPLIFETIME(AMechGrunt, bDead);
	DOREPLIFETIME(AMechGrunt, RepState);
	DOREPLIFETIME(AMechGrunt, RepAimPoint);
}

void AMechGrunt::Init(EMechGrunt InKind, AMechDefenseDirector* InDirector, float HPScale)
{
	Kind = InKind;
	Director = InDirector;
	MaxHP = HP = BaseHP(InKind) * HPScale;
	OrbitAngle = FMath::FRandRange(0.f, 2.f * PI);
	FireClock = FMath::FRandRange(1.f, 3.f);
	BuildVisuals();
}

void AMechGrunt::OnRep_Kind()
{
	BuildVisuals();
}

void AMechGrunt::BuildVisuals()
{
	if (bBuilt)
	{
		return;
	}
	bBuilt = true;
	EyeMID = GlowMat ? UMaterialInstanceDynamic::Create(GlowMat, this) : nullptr;
	if (EyeMID)
	{
		EyeMID->SetVectorParameterValue(TEXT("Emissive"), Red);
		EyeMID->SetScalarParameterValue(TEXT("EmissiveStrength"), 30.f);
	}
	auto Fit = [](UStaticMeshComponent* C, UStaticMesh* M, float Length) {
		C->SetStaticMesh(M);
		if (M)
		{
			const FVector E = M->GetBoundingBox().GetSize();
			C->SetRelativeScale3D(FVector(Length / FMath::Max(E.GetMax(), 1.f)));
		}
	};
	Eye->SetStaticMesh(SphereMesh);
	if (EyeMID)
	{
		Eye->SetMaterial(0, EyeMID);
	}
	switch (Kind)
	{
	case EMechGrunt::Kamikaze:
		// 占位：导弹模型当机身（像一枚会飞的弹头），机头一颗红灯
		HitBox->SetSphereRadius(110.f);
		Fit(Body, MissileMesh, 220.f);
		Eye->SetRelativeLocation(FVector(120.f, 0.f, 0.f));
		Eye->SetRelativeScale3D(FVector(0.28f));
		break;
	case EMechGrunt::Gunner:
		// 占位：导弹巢当机身，前面一颗红灯
		HitBox->SetSphereRadius(140.f);
		Fit(Body, PodMesh, 260.f);
		Eye->SetRelativeLocation(FVector(110.f, 0.f, 0.f));
		Eye->SetRelativeScale3D(FVector(0.32f));
		break;
	case EMechGrunt::Tank:
	{
		// 占位：方块车身 + 方块炮塔 + 圆柱炮管（6 米长、1.4 米高的车身），炮口红灯；碰撞球挡机体
		HitBox->SetSphereRadius(330.f);
		HitBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		HitBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Body->SetStaticMesh(CubeMesh);
		Body->SetRelativeLocation(FVector(0.f, 0.f, -50.f));
		Body->SetRelativeScale3D(FVector(6.f, 3.4f, 1.4f));
		Turret->SetStaticMesh(CubeMesh);
		Turret->SetRelativeScale3D(FVector(3.f, 2.4f, 1.1f));
		Barrel->SetStaticMesh(CylinderMesh);
		Barrel->SetRelativeScale3D(FVector(0.36f, 0.36f, 4.5f));
		Eye->SetRelativeScale3D(FVector(0.45f));
		for (UStaticMeshComponent* C : {Body.Get(), Turret.Get(), Barrel.Get()})
		{
			C->SetMaterial(0, MetalMat);
		}
		break;
	}
	case EMechGrunt::Mortar:
		// 占位：方块底座 + 斜着朝天的粗圆柱炮管
		HitBox->SetSphereRadius(260.f);
		Body->SetStaticMesh(CubeMesh);
		Body->SetRelativeLocation(FVector(0.f, 0.f, -50.f));
		Body->SetRelativeScale3D(FVector(3.2f, 3.2f, 1.f));
		Barrel->SetStaticMesh(CylinderMesh);
		Barrel->SetRelativeScale3D(FVector(0.6f, 0.6f, 3.2f));
		Eye->SetRelativeScale3D(FVector(0.4f));
		for (UStaticMeshComponent* C : {Body.Get(), Barrel.Get()})
		{
			C->SetMaterial(0, MetalMat);
		}
		break;
	}
	if (GlowMat)
	{
		UMaterialInstanceDynamic* LaserMID = UMaterialInstanceDynamic::Create(GlowMat, this);
		LaserMID->SetVectorParameterValue(TEXT("Emissive"), Red);
		LaserMID->SetScalarParameterValue(TEXT("EmissiveStrength"), 12.f);  // 40 时亮过头变成白线
		Laser->SetStaticMesh(CylinderMesh);
		Laser->SetMaterial(0, LaserMID);
	}
	if (RingMat)
	{
		RingMID = UMaterialInstanceDynamic::Create(RingMat, this);
		RingMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.15f, 0.08f));
		Ring->SetStaticMesh(PlaneMesh);
		Ring->SetMaterial(0, RingMID);
	}
}

// ---------------------------------------------------------------- 每帧
void AMechGrunt::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDead)
	{
		return;
	}
	if (HasAuthority())
	{
		ServerTick(DeltaTime);
	}
	else
	{
		ClientSmooth(DeltaTime);
	}
	UpdateVisualState(DeltaTime);
}

void AMechGrunt::ClientSmooth(float DeltaTime)
{
	// 位置按 15 次/秒到，中间按速度往前推一点再追过去（指数平滑），朝向同样追
	const FVector Want = FVector(RepPos) + FVector(RepVel) * 0.05f;
	const float K = 1.f - FMath::Exp(-12.f * DeltaTime);
	SetActorLocation(FMath::Lerp(GetActorLocation(), Want, K));
	FRotator R = GetActorRotation();
	R.Yaw = FMath::RInterpTo(FRotator(0.f, R.Yaw, 0.f), FRotator(0.f, RepYaw, 0.f), DeltaTime, 8.f).Yaw;
	SetActorRotation(R);
}

void AMechGrunt::UpdateVisualState(float DeltaTime)
{
	const float Now = GetWorld()->GetTimeSeconds();
	BlinkClock += DeltaTime;
	// 红灯：平时慢闪，倒数 / 瞄准时快闪
	if (EyeMID)
	{
		const float Rate = RepState ? 12.f : 2.f;
		EyeMID->SetScalarParameterValue(TEXT("EmissiveStrength"), (RepState ? 60.f : 18.f) * (0.55f + 0.45f * FMath::Sin(BlinkClock * Rate)));
	}
	// 炮塔、炮管朝向（相对车身）
	if (Kind == EMechGrunt::Tank)
	{
		// 圆柱的轴是局部 Z：先俯 90 度躺成沿 X，再跟炮塔一起转
		const FQuat Rel = FRotator(0.f, RepAimYaw - GetActorRotation().Yaw, 0.f).Quaternion();
		const FVector Fwd = Rel.GetForwardVector();
		Turret->SetRelativeLocationAndRotation(FVector(0.f, 0.f, 75.f), Rel);
		Barrel->SetRelativeLocationAndRotation(FVector(0.f, 0.f, 75.f) + Fwd * (150.f + 225.f), Rel * FRotator(-90.f, 0.f, 0.f).Quaternion());
		Eye->SetRelativeLocation(FVector(0.f, 0.f, 75.f) + Fwd * 610.f);
	}
	else if (Kind == EMechGrunt::Mortar)
	{
		// 炮管仰 55 度朝着瞄准方向
		const FQuat Tube = FRotator(0.f, RepAimYaw - GetActorRotation().Yaw, 0.f).Quaternion() * FRotator(55.f - 90.f, 0.f, 0.f).Quaternion();
		const FVector Axis = Tube.GetUpVector();
		Barrel->SetRelativeLocationAndRotation(FVector(0.f, 0.f, 20.f) + Axis * 160.f, Tube);
		Eye->SetRelativeLocation(FVector(0.f, 0.f, 20.f) + Axis * 330.f);
	}
	// 炮车瞄准线：炮口到瞄准点，快闪
	const bool bLaser = Kind == EMechGrunt::Tank && RepState == 1;
	if (bLaser)
	{
		const FVector From = Eye->GetComponentLocation();
		const FVector To = RepAimPoint;
		const float Len = FVector::Dist(From, To);
		Laser->SetWorldLocationAndRotation((From + To) * 0.5f, AlongZ(To - From));
		Laser->SetWorldScale3D(FVector(0.05f, 0.05f, Len / 100.f));
	}
	if (Laser->IsVisible() != bLaser)
	{
		Laser->SetVisibility(bLaser);
	}
	// 自爆无人机倒数：脚下红圈从中间涨到边（涨满就炸）
	const bool bRing = Kind == EMechGrunt::Kamikaze && RepState == 1;
	if (bRing && !Ring->IsVisible())
	{
		RingStart = Now;
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(MechGruntRing), false, this);
		const FVector P = GetActorLocation();
		const FVector G = GetWorld()->LineTraceSingleByChannel(Hit, P, P - FVector(0.f, 0.f, 5000.f), ECC_Visibility, Q) ? Hit.ImpactPoint : P - FVector(0.f, 0.f, 300.f);
		Ring->SetWorldLocationAndRotation(G + FVector(0.f, 0.f, 15.f), FRotator::ZeroRotator);
		Ring->SetWorldScale3D(FVector(KamikazeRadius * 2.f / 100.f));
		Ring->SetVisibility(true);
	}
	else if (!bRing && Ring->IsVisible())
	{
		Ring->SetVisibility(false);
	}
	if (bRing && RingMID)
	{
		const float T = FMath::Clamp((Now - RingStart) / KamikazeArmTime, 0.f, 1.f);
		RingMID->SetVectorParameterValue(TEXT("Prm"), FLinearColor(T, 0.03f, 1.4f, 2.f));
	}
}

// ---------------------------------------------------------------- 服务器
float AMechGrunt::GroundAt(const FVector& P) const
{
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(MechGruntGround), false, this);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, P + FVector(0.f, 0.f, 3000.f), P - FVector(0.f, 0.f, 20000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q))
	{
		return Hit.ImpactPoint.Z;
	}
	return P.Z - 500.f;
}

void AMechGrunt::PickTarget()
{
	AMechDefenseDirector* D = Director.Get();
	if (!D)
	{
		return;
	}
	TArray<AMechCharacter*> Players;
	D->GetPlayers(Players);
	const FVector P = GetActorLocation();
	AMechCharacter* Nearest = nullptr;
	float Best = TNumericLimits<float>::Max();
	for (AMechCharacter* M : Players)
	{
		const float Dd = FVector::Dist(M->GetActorLocation(), P);
		if (Dd < Best)
		{
			Best = Dd;
			Nearest = M;
		}
	}
	AActor* Core = D->Core;
	switch (Kind)
	{
	case EMechGrunt::Kamikaze:
		Target = Nearest && Best < 8000.f ? static_cast<AActor*>(Nearest) : Core;
		break;
	case EMechGrunt::Gunner:
		Target = Nearest && Best < 9000.f ? static_cast<AActor*>(Nearest) : Core;
		break;
	case EMechGrunt::Tank:
		Target = Nearest && Best < 3500.f ? static_cast<AActor*>(Nearest) : Core;
		break;
	case EMechGrunt::Mortar:
	{
		// 一半砸指挥所，一半砸指挥所旁边 40 米内的人
		AMechCharacter* NearCore = nullptr;
		if (Core)
		{
			for (AMechCharacter* M : Players)
			{
				if (FVector::Dist(M->GetActorLocation(), Core->GetActorLocation()) < 4000.f && FMath::RandBool())
				{
					NearCore = M;
					break;
				}
			}
		}
		Target = NearCore ? static_cast<AActor*>(NearCore) : Core;
		break;
	}
	}
	if (!Target.IsValid())
	{
		Target = Nearest ? static_cast<AActor*>(Nearest) : Core;
	}
}

FVector AMechGrunt::TargetPoint() const
{
	const AActor* T = Target.Get();
	if (!T)
	{
		return GetActorLocation();
	}
	if (T->IsA<AMechDefenseCore>())
	{
		return T->GetActorLocation() + FVector(0.f, 0.f, 350.f);
	}
	return T->GetActorLocation();
}

void AMechGrunt::RunStun(float Seconds)
{
	if (HasAuthority())
	{
		RunStunTimer = FMath::Max(RunStunTimer, Seconds);
	}
}

void AMechGrunt::ServerTick(float DeltaTime)
{
	// 肉鸽 重弹 2：瘫痪中什么都不做（红灯快闪靠 RepState）；自爆无人机倒数中被瘫痪，醒了从头再来
	if (RunStunTimer > 0.f)
	{
		RunStunTimer -= DeltaTime;
		RepState = RunStunTimer > 0.f ? 2 : 0;
		Vel = FVector::ZeroVector;
		RepPos = GetActorLocation();
		RepVel = FVector::ZeroVector;
		return;
	}
	RetargetClock -= DeltaTime;
	if (RetargetClock <= 0.f || !Target.IsValid())
	{
		RetargetClock = 0.5f;
		PickTarget();
	}
	if (const AMechCharacter* M = Cast<AMechCharacter>(Target.Get()); M && M->GetHealth()->IsDead())
	{
		PickTarget();
	}
	switch (Kind)
	{
	case EMechGrunt::Kamikaze:
		TickKamikaze(DeltaTime);
		break;
	case EMechGrunt::Gunner:
		TickGunner(DeltaTime);
		break;
	case EMechGrunt::Tank:
		TickTank(DeltaTime);
		break;
	case EMechGrunt::Mortar:
		TickMortar(DeltaTime);
		break;
	}
	if (bDead)
	{
		return;
	}
	RepPos = GetActorLocation();
	RepVel = Vel;
	RepYaw = GetActorRotation().Yaw;
}

void AMechGrunt::TickKamikaze(float DeltaTime)
{
	FVector P = GetActorLocation();
	if (RepState == 1)
	{
		// 倒数：停住，时间到就炸
		StateClock -= DeltaTime;
		Vel = FVector::ZeroVector;
		if (StateClock <= 0.f)
		{
			if (AMechDefenseDirector* D = Director.Get())
			{
				D->Blast(P, KamikazeRadius, KamikazeDamage, 350.f, KamikazeCoreDamage, this, false);
			}
			Die(nullptr, true);
		}
		return;
	}
	const FVector T = TargetPoint();
	const FVector To = T - P;
	const float Flat = FVector(To.X, To.Y, 0.f).Size();
	// 远的时候贴着地面 4 米飞，近了（25 米内）直接冲过去
	FVector Want = T;
	if (Flat > 2500.f)
	{
		GroundClock -= DeltaTime;
		if (GroundClock <= 0.f)
		{
			GroundClock = 0.3f;
			GroundZ = GroundAt(P);
		}
		Want.Z = FMath::Max(GroundZ + 400.f, T.Z);
	}
	const FVector Desired = (Want - P).GetSafeNormal() * KamikazeSpeed;
	Vel = FMath::VInterpTo(Vel, Desired, DeltaTime, 3.f);
	P += Vel * DeltaTime;
	SetActorLocation(P);
	if (!Vel.IsNearlyZero())
	{
		SetActorRotation(Vel.Rotation());
	}
	const float Reach = Target.IsValid() && Target->IsA<AMechDefenseCore>() ? 900.f : KamikazeArmDist;
	if (To.Size() < Reach)
	{
		RepState = 1;
		StateClock = KamikazeArmTime;
	}
}

void AMechGrunt::TickGunner(float DeltaTime)
{
	FVector P = GetActorLocation();
	const FVector T = TargetPoint();
	// 绕目标 40 米一圈、离地 14 米
	OrbitAngle += DeltaTime * 0.25f;
	FVector Want = T + FVector(FMath::Cos(OrbitAngle), FMath::Sin(OrbitAngle), 0.f) * GunnerOrbit;
	GroundClock -= DeltaTime;
	if (GroundClock <= 0.f)
	{
		GroundClock = 0.3f;
		GroundZ = GroundAt(Want);
	}
	Want.Z = FMath::Max(GroundZ + 1400.f, T.Z + 900.f);
	const FVector Desired = (Want - P).GetClampedToMaxSize(GunnerSpeed * 1.5f).GetClampedToMaxSize(GunnerSpeed);
	Vel = FMath::VInterpTo(Vel, Desired, DeltaTime, 2.f);
	P += Vel * DeltaTime;
	SetActorLocation(P);
	SetActorRotation(FRotator(0.f, (T - P).Rotation().Yaw, 0.f));
	// 开火：一串 5 发，每发 0.13 秒，打目标的提前量（慢弹，横移就躲开）
	FireClock -= DeltaTime;
	if (FireClock <= 0.f && BurstLeft == 0 && FVector::Dist(T, P) < 7000.f)
	{
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(MechGruntSight), false, this);
		Q.AddIgnoredActor(Target.Get());
		if (!GetWorld()->LineTraceSingleByChannel(Hit, P, T, ECC_Visibility, Q))
		{
			BurstLeft = GunnerBurst;
			BurstClock = 0.f;
		}
		FireClock = 3.f + FMath::FRandRange(0.f, 0.6f);
	}
	if (BurstLeft > 0)
	{
		BurstClock -= DeltaTime;
		if (BurstClock <= 0.f)
		{
			BurstClock = 0.13f;
			--BurstLeft;
			const float Travel = FVector::Dist(T, P) / GunnerShotSpeed;
			const FVector Lead = Target.IsValid() ? Target->GetVelocity() * Travel * 0.6f : FVector::ZeroVector;
			const FVector Dir = FMath::VRandCone((T + Lead - P).GetSafeNormal(), FMath::DegreesToRadians(1.2f));
			if (AMechDefenseDirector* D = Director.Get())
			{
				D->FireShot(P + Dir * 160.f, Dir * GunnerShotSpeed, 0.f, GunnerDamage, 0.f, GunnerCoreDamage, this, 0);
			}
		}
	}
}

void AMechGrunt::TickTank(float DeltaTime)
{
	FVector P = GetActorLocation();
	const FVector T = TargetPoint();
	const bool bCore = Target.IsValid() && Target->IsA<AMechDefenseCore>();
	const float Flat = FVector(T.X - P.X, T.Y - P.Y, 0.f).Size();
	RepAimYaw = FMath::FixedTurn(RepAimYaw, (T - P).Rotation().Yaw, 90.f * DeltaTime);
	FireClock -= DeltaTime;
	if (RepState == 1)
	{
		// 瞄准：瞄准线跟着目标，开炮前 0.35 秒定住
		StateClock -= DeltaTime;
		if (StateClock > TankLockBefore)
		{
			RepAimPoint = T;
		}
		if (StateClock <= 0.f)
		{
			RepState = 0;
			FireClock = TankCooldown;
			const FVector Muzzle = Eye->GetComponentLocation();
			const FVector Aim = RepAimPoint;
			const float Time = FVector::Dist(Aim, Muzzle) / TankShellSpeed;
			// 弹道补偿：往上抬掉下落的那一截
			const FVector V = (Aim + FVector(0.f, 0.f, 0.5f * TankShellGravity * Time * Time) - Muzzle).GetSafeNormal() * TankShellSpeed;
			if (AMechDefenseDirector* D = Director.Get())
			{
				D->FireShot(Muzzle, V, TankShellGravity, TankDamage, TankBlast, TankCoreDamage, this, 1);
			}
		}
		Vel = FVector::ZeroVector;
		return;
	}
	const float Stop = bCore ? 5500.f : 2500.f;
	if (Flat <= Stop)
	{
		Vel = FVector::ZeroVector;
		if (FireClock <= 0.f)
		{
			RepState = 1;
			StateClock = TankAimTime;
			RepAimPoint = T;
		}
	}
	else
	{
		// 开过去：贴地、绕开挡路的东西；卡住了往侧面绕一会儿
		float Heading = (T - P).Rotation().Yaw;
		AvoidClock -= DeltaTime;
		if (AvoidClock <= 0.f)
		{
			AvoidClock = 0.3f;
			FHitResult Hit;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(MechGruntAvoid), false, this);
			const FVector Eyes = P + FVector(0.f, 0.f, 100.f);
			if (GetWorld()->LineTraceSingleByObjectType(Hit, Eyes, Eyes + GetActorForwardVector() * 700.f, FCollisionObjectQueryParams(ECC_WorldStatic), Q))
			{
				DetourTime = 2.5f;
				DetourSign = FMath::RandBool() ? 1.f : -1.f;
			}
		}
		StuckClock += DeltaTime;
		if (StuckClock > 3.f)
		{
			if (FVector::Dist2D(P, StuckFrom) < 150.f)
			{
				DetourTime = 2.5f;
				DetourSign = -DetourSign;
			}
			StuckFrom = P;
			StuckClock = 0.f;
		}
		if (DetourTime > 0.f)
		{
			DetourTime -= DeltaTime;
			Heading += 70.f * DetourSign;
		}
		const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw, Heading, 50.f * DeltaTime);
		SetActorRotation(FRotator(0.f, Yaw, 0.f));
		Vel = FRotator(0.f, Yaw, 0.f).Vector() * TankSpeed;
		P += Vel * DeltaTime;
	}
	P.Z = GroundAt(P) + 120.f;
	SetActorLocation(P);
}

void AMechGrunt::TickMortar(float DeltaTime)
{
	Vel = FVector::ZeroVector;
	FVector P = GetActorLocation();
	if (GroundClock <= 0.f)
	{
		GroundClock = 1000.f;  // 只贴一次地
		P.Z = GroundAt(P) + 100.f;
		SetActorLocation(P);
	}
	const FVector T = TargetPoint();
	RepAimYaw = FMath::FixedTurn(RepAimYaw, (T - P).Rotation().Yaw, 60.f * DeltaTime);
	// 炮弹在路上（落点红圈正在涨）：到时间就砸
	if (RepState == 1)
	{
		StateClock -= DeltaTime;
		if (StateClock <= 0.f)
		{
			RepState = 0;
			if (AMechDefenseDirector* D = Director.Get())
			{
				D->Blast(RepAimPoint, MortarRadius, MortarDamage, 300.f, MortarCoreDamage, this, false);
			}
		}
		return;
	}
	FireClock -= DeltaTime;
	if (FireClock <= 0.f)
	{
		FireClock = 7.f + FMath::FRandRange(0.f, 2.f);
		// 落点：目标附近 0–8 米随机，贴地
		FVector At = T + FVector(FMath::FRandRange(-800.f, 800.f), FMath::FRandRange(-800.f, 800.f), 0.f);
		At.Z = GroundAt(At);
		RepAimPoint = At;
		RepState = 1;
		StateClock = MortarWarn;
		if (AMechDefenseDirector* D = Director.Get())
		{
			D->Warn(At, MortarRadius, MortarWarn);
			D->Blast(Eye->GetComponentLocation(), 0.f, 0.f, 0.f, 0.f, this, false);  // 只有炮口火光和声音（半径 0 不伤人）
		}
	}
}

// ---------------------------------------------------------------- 被打 / 死
float AMechGrunt::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || bDead || DamageAmount <= 0.f)
	{
		return 0.f;
	}
	AMechCharacter* By = Cast<AMechCharacter>(DamageCauser);
	if (!By && EventInstigator)
	{
		By = Cast<AMechCharacter>(EventInstigator->GetPawn());
	}
	if (By && By->GetTeam() != 0)
	{
		return 0.f;  // 敌方机甲（精英）的子弹打不坏自己人
	}
	// 11-01 肉鸽：等级、空战的伤害倍数（杂兵不走 UMechHealthComponent::ApplyHit，这里补；没有 Run 的不变）
	if (By && By->GetRun())
	{
		DamageAmount *= By->GetRun()->OutgoingScale();
	}
	MechScore::NoteGruntDamage(By, FMath::Min(DamageAmount, HP));  // 11-01 积分制：输出 / 伤害分按实际打掉的血（溢出的不算）
	HP -= DamageAmount;
	if (HP <= 0.f)
	{
		HP = 0.f;
		Die(By, false);
	}
	return DamageAmount;
}

FString AMechGrunt::DebugInfo() const
{
	const AActor* T = Target.Get();
	return FString::Printf(TEXT("%s 离目标 %.0f 米（%s）状态 %d 速度 %.0f 离地 %.0f 米"), KindName(Kind), T ? FVector::Dist(T->GetActorLocation(), GetActorLocation()) / 100.f : -1.f,
		T ? (T->IsA<AMechDefenseCore>() ? TEXT("指挥所") : TEXT("玩家")) : TEXT("没有"), RepState, Vel.Size(), (GetActorLocation().Z - GroundAt(GetActorLocation())) / 100.f);
}

void AMechGrunt::DebugKill()
{
	if (HasAuthority() && !bDead)
	{
		HP = 0.f;
		Die(nullptr, false);
	}
}

void AMechGrunt::Die(AActor* Killer, bool bExploded)
{
	if (bDead)
	{
		return;
	}
	bDead = true;
	RepState = 0;
	if (AMechDefenseDirector* D = Director.Get())
	{
		D->OnGruntDied(this, Killer);
	}
	if (!bExploded)
	{
		MulticastDeathFX(GetActorLocation(), Kind == EMechGrunt::Tank || Kind == EMechGrunt::Mortar);
	}
	OnRep_Dead();
	SetLifeSpan(0.6f);
}

void AMechGrunt::OnRep_Dead()
{
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
}

void AMechGrunt::MulticastDeathFX_Implementation(FVector_NetQuantize At, bool bBig)
{
	if (UNiagaraSystem* FX = bBig ? ExplosionFX.Get() : SmallExplosionFX.Get())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, FX, At, FRotator::ZeroRotator, FVector(bBig ? 1.2f : 1.4f), true, true, ENCPoolMethod::AutoRelease);
	}
	if (ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ExplosionSound, At, FRotator::ZeroRotator, bBig ? 0.8f : 0.45f, bBig ? 0.85f : 1.3f);
	}
}

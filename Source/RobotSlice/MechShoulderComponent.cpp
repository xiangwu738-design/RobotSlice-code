#include "MechShoulderComponent.h"
#include "MechLoop.h"

#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/RootMotionSource.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "RHI.h"
#include "MechCharacter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MechCombatComponent.h"
#include "MechEffectComponent.h"
#include "MechGrenade.h"
#include "MechHealthComponent.h"
#include "MechMeshComponent.h"
#include "MechMovementComponent.h"
#include "MechNet.h"
#include "MechScore.h"
#include "MechSmoke.h"
#include "MechLoadout.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 发射器模型里的点（模型坐标，厘米，未缩放；数是 lookdev/make_shoulder_game.py 打出来的）
	const FVector GrappleMuzzle(34.f, 0.f, 0.f);
	const FVector ClawRest(29.5f, 0.f, 0.f);
	const FVector NetMuzzle(36.f, 0.f, 0.f);
	const FVector ArtilleryMuzzle(51.f, 0.f, 0.f);
	const FVector SmokeMuzzle(20.f, 0.f, 0.f);
	const FVector EmpMuzzle(21.f, 0.f, 0.f);
	const FVector EmitterFront(6.f, 0.f, 0.f);
	// 这四件的数来自 lookdev/make_shoulder_tactical.py：推进包喷口中间、弹射板上面、环形发生器中心、布雷器托架前端
	const FVector BurstNozzle(-17.f, 0.f, -26.f);
	const FVector LaunchTop(0.f, 0.f, 26.f);
	const FVector ShockCenter(0.f, 0.f, 0.f);
	const FVector MineChute(25.f, 0.f, -11.5f);
	/** 爪子抓住时爪根离抓点多远（爪长约 43 厘米，指尖扎进去一点） */
	const float ClawReach = 34.f;
	const float RetractTime = 0.28f;
	const int32 CableSegments = 14;
	const float CableRadius = 2.4f;
	const FName PullName(TEXT("MechGrapple"));
	const FName DragName(TEXT("MechGrappleDrag"));

	template <typename T>
	T* Load(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<T> Finder(Path);
		return Finder.Object;
	}
}

UMechShoulderComponent::UMechShoulderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;  // 机体移动、动画、网格平滑都完了再摆无人机，爪子、缆绳跟着它
	SetIsReplicatedByDefault(true);
	CableMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder"));
	CableMaterial = Load<UMaterialInterface>(TEXT("/Game/Robot/Materials/MI_Robot_Frame"));
	LaunchFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Muzzle"));
	SparkFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"));
	GrappleFireSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_grapple_fire"));
	GrappleCableSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_grapple_cable"));
	GrappleLatchSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_grapple_latch"));
	GrappleReelSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_grapple_reel"));
	GrappleReleaseSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_grapple_release"));
	NetFireSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_net_fire"));
	NetStruggleSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_net_break"));
	ShieldDomeMesh = Load<UStaticMesh>(TEXT("/Game/Weapons/Meshes/SM_ShieldDome"));
	ShieldMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_ShieldFX"));
	ArtilleryFireSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_artillery_fire"));
	ArtilleryReadySound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_artillery_ready"));
	LauncherPopSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_launcher_pop"));
	ShieldUpSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_shield_up"));
	ShieldHitSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_shield_hit"));
	ShieldDownSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_shield_down"));
	ShieldHumSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_shield_hum"));
	EmpZapSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_emp_zap"));
	// EMP 电弧：叠加、不受光的蓝白细线（tools/ue_make_emp_fx.py 生成，带白天补偿）
	EmpArcMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_EmpArc"));
	Attenuation = Load<USoundAttenuation>(TEXT("/Game/Audio/SA_Combat"));
	SphereMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere"));
}

void UMechShoulderComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* Owner = GetOwner();
	// 缆绳：一串拉长的圆柱，每帧按发射口到爪根的位置重摆
	Cable = NewObject<UInstancedStaticMeshComponent>(Owner, TEXT("GrappleCable"));
	Cable->SetStaticMesh(CableMesh);
	if (CableMaterial)
	{
		Cable->SetMaterial(0, CableMaterial);
	}
	Cable->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cable->SetCastShadow(true);
	Cable->bAffectDistanceFieldLighting = false;
	Cable->bAffectDynamicIndirectLighting = false;
	Cable->LightingChannels.bChannel1 = true;
	Cable->SetUsingAbsoluteLocation(true);
	Cable->SetUsingAbsoluteRotation(true);
	Cable->SetUsingAbsoluteScale(true);
	Cable->SetupAttachment(Owner->GetRootComponent());
	Cable->SetHasPerInstancePrevTransforms(true);
	Cable->RegisterComponent();
	Cable->SetWorldTransform(FTransform::Identity);

	// 护盾：六边形网格弧面，球心在胸口（跟着机体朝向），平时藏着
	ShieldMesh = NewObject<UStaticMeshComponent>(Owner, TEXT("ShieldDome"));
	ShieldMesh->SetStaticMesh(ShieldDomeMesh);
	ShieldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShieldMesh->SetCastShadow(false);
	ShieldMesh->bAffectDistanceFieldLighting = false;
	ShieldMesh->bAffectDynamicIndirectLighting = false;
	ShieldMesh->SetupAttachment(Owner->GetRootComponent());
	ShieldMesh->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
	ShieldMesh->SetVisibility(false);
	ShieldMesh->RegisterComponent();
	// 盾跟着画面上的网格走（挂在胶囊上的话，别人的机体网络平滑时盾和机体会错开一截；-MechPropsOnCapsule 保留旧挂法做对比）
	const AMechCharacter* OwnerMech = Cast<AMechCharacter>(Owner);
	if (OwnerMech && OwnerMech->GetMesh() && !FParse::Param(FCommandLine::Get(), TEXT("MechPropsOnCapsule")))
	{
		ShieldMesh->SetUsingAbsoluteScale(true);
		ShieldMesh->AttachToComponent(OwnerMech->GetMesh(), FAttachmentTransformRules::KeepWorldTransform);
	}
	// EMP 打中时身上爬的电弧：和缆绳一样是一串拉长的圆柱（绝对坐标，骨骼定稿后每帧重摆），平时没有实例
	EmpArcs = NewObject<UInstancedStaticMeshComponent>(Owner, TEXT("EmpArcs"));
	EmpArcs->SetStaticMesh(CableMesh);
	if (EmpArcMaterial)
	{
		EmpArcs->SetMaterial(0, EmpArcMaterial);
	}
	EmpArcs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EmpArcs->SetCastShadow(false);
	EmpArcs->bAffectDistanceFieldLighting = false;
	EmpArcs->bAffectDynamicIndirectLighting = false;
	EmpArcs->SetUsingAbsoluteLocation(true);
	EmpArcs->SetUsingAbsoluteRotation(true);
	EmpArcs->SetUsingAbsoluteScale(true);
	EmpArcs->SetupAttachment(Owner->GetRootComponent());
	EmpArcs->RegisterComponent();
	EmpArcs->SetWorldTransform(FTransform::Identity);
	// 身上的电弧比材质默认（8）暗一些（10-30 用户：EMP「还是太亮 / 刺眼」），仍是白芯蓝边、看得清
	if (EmpArcMaterial)
	{
		EmpArcMID = UMaterialInstanceDynamic::Create(EmpArcMaterial, this);
		EmpArcMID->SetScalarParameterValue(TEXT("Strength"), 4.5f);
		EmpArcs->SetMaterial(0, EmpArcMID);
	}

	// 构图测试：-MechDroneOffset=X,Y,Z（机体坐标，厘米，相对胶囊中心）
	FString OffsetArg;
	if (FParse::Value(FCommandLine::Get(), TEXT("MechDroneOffset="), OffsetArg, false))
	{
		TArray<FString> Parts;
		OffsetArg.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 3)
		{
			DroneOffset = FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
		}
	}
	// 每台的浮动相位错开（几台站在一起不会一起上下）
	DronePhase = FMath::FRandRange(0.f, 2.f * PI);
	// 电弧闪的时候照亮机体表面的一盏蓝白小灯（挂在胸椎上，跟着网格走）
	EmpLight = NewObject<UPointLightComponent>(Owner, TEXT("EmpLight"));
	EmpLight->SetLightColor(FLinearColor(0.55f, 0.78f, 1.f));
	EmpLight->SetAttenuationRadius(450.f);
	EmpLight->SetCastShadows(false);
	EmpLight->SetIntensity(0.f);
	EmpLight->SetVisibility(false);
	if (OwnerMech && OwnerMech->GetMesh())
	{
		EmpLight->SetupAttachment(OwnerMech->GetMesh(), TEXT("spine_03"));
	}
	else
	{
		EmpLight->SetupAttachment(Owner->GetRootComponent());
	}
	EmpLight->RegisterComponent();
	// 导入时网格的材质槽会被新导入器重置成默认材质，这里直接挂 M_ShieldFX
	if (ShieldMaterial)
	{
		ShieldMID = UMaterialInstanceDynamic::Create(ShieldMaterial, this);
		ShieldMesh->SetMaterial(0, ShieldMID);
	}
	static bool bLogged = false;
	if (!bLogged && ShieldMaterial)
	{
		bLogged = true;
		UE_LOG(LogTemp, Display, TEXT("[shoulder] 护盾材质 %s 编译中或出错 %d"), *ShieldMaterial->GetPathName(),
			ShieldMaterial->GetMaterial()->IsCompilingOrHadCompileError(GMaxRHIShaderPlatform) ? 1 : 0);
	}
	ShieldHum = NewObject<UAudioComponent>(Owner, TEXT("ShieldHum"));
	ShieldHum->SetupAttachment(Owner->GetRootComponent());
	ShieldHum->bAutoActivate = false;
	ShieldHum->SetSound(ShieldHumSound);
	ShieldHum->AttenuationSettings = Attenuation;
	ShieldHum->RegisterComponent();
}

void UMechShoulderComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMechShoulderComponent, Grapple);
	DOREPLIFETIME(UMechShoulderComponent, bNetted);
	DOREPLIFETIME(UMechShoulderComponent, NetStart);
	DOREPLIFETIME(UMechShoulderComponent, NetEnd);
	DOREPLIFETIME_CONDITION(UMechShoulderComponent, Cooldown, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMechShoulderComponent, bArtyDeployed, COND_SkipOwner);
	DOREPLIFETIME(UMechShoulderComponent, ShieldEnd);
	DOREPLIFETIME_CONDITION(UMechShoulderComponent, CooldownLength, COND_OwnerOnly);
}

void UMechShoulderComponent::GetEffectSystems(TArray<UNiagaraSystem*>& Out) const
{
	Out.AddUnique(LaunchFX);
	Out.AddUnique(SparkFX);
	if (const AMechNet* Net = GetDefault<AMechNet>())
	{
		Net->GetEffectSystems(Out);
	}
}

// ---------------------------------------------------------------- 小工具
AMechCharacter* UMechShoulderComponent::GetMech() const
{
	return Cast<AMechCharacter>(GetOwner());
}

float UMechShoulderComponent::ServerNow() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

UStaticMeshComponent* UMechShoulderComponent::Launcher() const
{
	const AMechCharacter* Mech = GetMech();
	return Mech && Mech->GetMechMesh() ? Cast<UStaticMeshComponent>(Mech->GetMechMesh()->Props[2].Get()) : nullptr;
}

UStaticMeshComponent* UMechShoulderComponent::Claw() const
{
	const AMechCharacter* Mech = GetMech();
	return Mech ? Mech->GetClawMesh() : nullptr;
}

FVector UMechShoulderComponent::MuzzleLocalFor(EMechShoulder Type)
{
	switch (Type)
	{
	case EMechShoulder::Net:
		return NetMuzzle;
	case EMechShoulder::Burst:
		return BurstNozzle;
	case EMechShoulder::Artillery:
		return ArtilleryMuzzle;
	case EMechShoulder::Smoke:
		return SmokeMuzzle;
	case EMechShoulder::Launch:
		return LaunchTop;
	case EMechShoulder::Emp:
		return EmpMuzzle;
	case EMechShoulder::Shield:
		return EmitterFront;
	case EMechShoulder::Shockwave:
		return ShockCenter;
	case EMechShoulder::Mine:
		return MineChute;
	case EMechShoulder::Missiles:
		return FVector(26.f, 0.f, 0.f);  // 四个发射管的中间（发射管各自的位置在战斗组件 PodTubes）
	default:
		return GrappleMuzzle;
	}
}

FVector UMechShoulderComponent::Muzzle() const
{
	const AMechCharacter* Mech = GetMech();
	if (!Mech)
	{
		return FVector::ZeroVector;
	}
	return LaunchPointToward(Mech->GetCombat()->GetPoseAimPoint(), MuzzleLocalFor(Mech->GetLoadout().Shoulder));
}

// ---------------------------------------------------------------- 浮空无人机
void UMechShoulderComponent::CacheDroneMesh()
{
	UStaticMeshComponent* Body = Launcher();
	const UStaticMesh* Mesh = Body ? Body->GetStaticMesh() : nullptr;
	if (!Mesh || Mesh == DroneMeshFor)
	{
		return;
	}
	DroneMeshFor = Mesh;
	const FBoxSphereBounds B = Mesh->GetBounds();
	DroneMeshCenter = B.Origin;
	DroneMeshExtent = B.BoxExtent;
	// 六个挂件模型大小差很多（火炮炮管长、烟雾弹发射器小），统一到最长边约 58 厘米（以前挂在背后是 ×1.35，浮空的机身小一点不压机体剪影）
	DroneScale = FMath::Clamp(58.f / FMath::Max(2.f * DroneMeshExtent.GetMax(), 1.f), 0.7f, 1.35f);
	UE_LOG(LogTemp, Display, TEXT("[drone] %s 机身 %s 包围盒中心 %s 半尺寸 %s → 缩放 %.2f"), GetOwner() ? *GetOwner()->GetName() : TEXT("?"), *Mesh->GetName(),
		*DroneMeshCenter.ToCompactString(), *DroneMeshExtent.ToCompactString(), DroneScale);
	// 底下两个小推进器：第一次用到时建（组件 BeginPlay 时机体还没把挂件网格交给网格组件），挂在机身网格上
	AActor* Owner = GetOwner();
	for (int32 i = 0; i < 2; ++i)
	{
		if (!DronePads[i])
		{
			UStaticMeshComponent* Pad = NewObject<UStaticMeshComponent>(Owner, *FString::Printf(TEXT("DronePad%d"), i));
			Pad->SetStaticMesh(CableMesh);
			if (CableMaterial)
			{
				Pad->SetMaterial(0, CableMaterial);
			}
			Pad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Pad->SetCastShadow(false);
			Pad->bAffectDistanceFieldLighting = false;
			Pad->bAffectDynamicIndirectLighting = false;
			Pad->LightingChannels.bChannel1 = true;
			Pad->SetupAttachment(Body);
			Pad->RegisterComponent();
			DronePads[i] = Pad;
		}
		if (!DroneGlows[i])
		{
			UStaticMeshComponent* Glow = NewObject<UStaticMeshComponent>(Owner, *FString::Printf(TEXT("DroneGlow%d"), i));
			Glow->SetStaticMesh(SphereMesh);
			if (EmpArcMaterial)
			{
				if (!DroneGlowMID)
				{
					// 和 EMP 电弧同一个叠加材质（白芯、冷蓝边，白天按曝光补偿），亮度压低：只是两个小光点，不花哨
					DroneGlowMID = UMaterialInstanceDynamic::Create(EmpArcMaterial, this);
					DroneGlowMID->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(0.35f, 0.75f, 1.f));
					DroneGlowMID->SetScalarParameterValue(TEXT("Strength"), 0.2f);
				}
				Glow->SetMaterial(0, DroneGlowMID);
			}
			Glow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Glow->SetCastShadow(false);
			Glow->bAffectDistanceFieldLighting = false;
			Glow->bAffectDynamicIndirectLighting = false;
			Glow->SetupAttachment(Body);
			Glow->RegisterComponent();
			DroneGlows[i] = Glow;
		}
	}
	// 摆在机身底面左右两侧（模型坐标；子组件的缩放会乘上机身的缩放，所以按世界尺寸除回去）
	const float S = DroneScale;
	const float Side = FMath::Max(DroneMeshExtent.Y * 0.6f, 9.f / S);
	const FVector Bottom(DroneMeshCenter.X - DroneMeshExtent.X * 0.15f, DroneMeshCenter.Y, DroneMeshCenter.Z - DroneMeshExtent.Z);
	for (int32 i = 0; i < 2; ++i)
	{
		const FVector At = Bottom + FVector(0.f, i == 0 ? -Side : Side, -1.5f / S);
		// 引擎圆柱半径 50、高 100：推进口半径 6.5 厘米、高 3 厘米；光点半径 3 厘米、压扁，嵌在推进口下面
		DronePads[i]->SetRelativeLocationAndRotation(At, FRotator::ZeroRotator);
		DronePads[i]->SetRelativeScale3D(FVector(6.5f / 50.f, 6.5f / 50.f, 3.f / 100.f) / S);
		DroneGlows[i]->SetRelativeLocationAndRotation(At - FVector(0.f, 0.f, 1.8f / S), FRotator::ZeroRotator);
		DroneGlows[i]->SetRelativeScale3D(FVector(3.f / 50.f, 3.f / 50.f, 1.f / 50.f) / S);
	}
}

FTransform UMechShoulderComponent::DroneFrame(const FVector& Center, const FQuat& Rot) const
{
	return FTransform(Rot, Center - Rot.RotateVector(DroneMeshCenter * DroneScale), FVector(DroneScale));
}

FVector UMechShoulderComponent::ClampToWorld(const FVector& From, const FVector& To, float Radius) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechDrone), false, GetOwner());
	FHitResult Hit;
	if (!GetWorld()->SweepSingleByObjectType(Hit, From, To, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(Radius), Params))
	{
		return To;
	}
	if (Hit.bStartPenetrating)
	{
		return From;  // 头顶本身就贴着天花板之类：就待在头边
	}
	return From + (To - From).GetSafeNormal() * FMath::Max(0.f, Hit.Distance - 2.f);
}

float UMechShoulderComponent::DroneAimYaw() const
{
	const AMechCharacter* Mech = GetMech();
	if (!Mech)
	{
		return 0.f;
	}
	const FVector Aim = Mech->GetCombat()->GetPoseAimDirection().GetSafeNormal2D();
	return Aim.IsNearlyZero() ? Mech->GetActorRotation().Yaw : Aim.Rotation().Yaw;
}

FVector UMechShoulderComponent::DroneIdeal(bool bVisual, float Yaw) const
{
	const AMechCharacter* Mech = GetMech();
	if (!Mech)
	{
		return FVector::ZeroVector;
	}
	// 摆位按瞄准方向的水平朝向（不按机体朝向）：没交战时机体朝移动方向转，往镜头这边跑时按机体朝向摆的话无人机会翻到准星上
	const FVector Center = bVisual && Mech->GetMechMesh() ? Mech->GetMechMesh()->VisualPivot() - FVector(0.f, 0.f, 100.f) : Mech->GetActorLocation();
	// 交战时往背离敌人的一侧挪一点（UpdateDrone 里平滑算好的方向和程度；判定和画面用同一个，发射起点也跟着挪）
	const FVector Ideal = Center + FRotator(0.f, Yaw, 0.f).RotateVector(DroneOffset) + (DroneAway * DroneEngagePush + FVector(0.f, 0.f, 10.f)) * DroneEngage;
	// 贴墙：从头部往理想点扫一个球（机身半径约 28 厘米），碰到就停在墙前
	return ClampToWorld(Center + FVector(0.f, 0.f, 90.f), Ideal, 28.f);
}

FVector UMechShoulderComponent::LaunchPoint(const FVector& Dir, const FVector& Local) const
{
	const FVector Center = DroneIdeal(false, DroneAimYaw());
	const FVector D = Dir.IsNearlyZero() ? FVector::ForwardVector : Dir.GetSafeNormal();
	const FVector P = DroneFrame(Center, FRotationMatrix::MakeFromX(D).ToQuat()).TransformPosition(Local);
	return ClampToWorld(Center, P, 6.f);  // 发射口探出机身二三十厘米，贴墙时也别探进墙里（弹在墙里生成会穿墙）
}

FVector UMechShoulderComponent::LaunchPointToward(const FVector& AimPoint, const FVector& Local) const
{
	const FVector Center = DroneIdeal(false, DroneAimYaw());
	const FVector D = (AimPoint - Center).IsNearlyZero() ? FVector::ForwardVector : (AimPoint - Center).GetSafeNormal();
	const FVector P = DroneFrame(Center, FRotationMatrix::MakeFromX(D).ToQuat()).TransformPosition(Local);
	return ClampToWorld(Center, P, 6.f);
}

FVector UMechShoulderComponent::VisualPoint(const FVector& Dir, const FVector& Local) const
{
	if (!bDroneInit)
	{
		return LaunchPoint(Dir, Local);
	}
	const FVector D = Dir.IsNearlyZero() ? DroneRot.GetForwardVector() : Dir.GetSafeNormal();
	return DroneFrame(DronePos, FRotationMatrix::MakeFromX(D).ToQuat()).TransformPosition(Local);
}

FVector UMechShoulderComponent::LaunchVisualOffset(const AActor* Shooter, const FVector& LogicPos, const FVector& Dir, const FVector& Local)
{
	const AMechCharacter* M = Cast<AMechCharacter>(Shooter);
	const UMechShoulderComponent* S = M ? M->GetShoulder() : nullptr;
	if (!S || !S->bDroneInit || S->bDroneDown)
	{
		return FVector::ZeroVector;
	}
	const FVector Off = S->VisualPoint(Dir, Local) - LogicPos;
	// 偏太远：不是刚发射的（进视野时已经飞了一大段），不往回拽
	return Off.SizeSquared() < FMath::Square(1500.f) ? Off : FVector::ZeroVector;
}

void UMechShoulderComponent::OnDroneFired(const FVector& Dir)
{
	DroneFiredAt = GetWorld()->GetTimeSeconds();
	DroneFiredDir = Dir.IsNearlyZero() ? DroneRot.GetForwardVector() : Dir.GetSafeNormal();
	if (bDroneInit)
	{
		DroneRot = FRotationMatrix::MakeFromX(DroneFiredDir).ToQuat();  // 这一帧特效按「已经对着 Dir」的发射口放，机身马上对上
		DroneVel -= DroneFiredDir * 260.f;  // 后坐：往后一顿，弹簧再拉回来
	}
}

void UMechShoulderComponent::UpdateDrone(float DeltaTime)
{
	AMechCharacter* Mech = GetMech();
	UStaticMeshComponent* Body = Launcher();
	if (!Mech || !Body || DeltaTime <= 0.f)
	{
		return;
	}
	CacheDroneMesh();
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bDead = Mech->GetHealth()->IsDead();
	// 摆位朝向：瞄准方向的水平朝向，限速平滑（甩鼠标时无人机绕着机体飞过去，不从头顶直线穿过）
	const float WantYaw = DroneAimYaw();
	if (!bDroneInit)
	{
		DroneYaw = WantYaw;
	}
	else
	{
		const float Delta = FMath::FindDeltaAngleDegrees(DroneYaw, WantYaw);
		DroneYaw += FMath::Clamp(Delta * (1.f - FMath::Exp(-DeltaTime * 10.f)), -540.f * DeltaTime, 540.f * DeltaTime);
	}
	// 交战（锁定了敌人 / 2 秒内开过枪、放过肩部武器）：理想点往背离敌人的方向挪（10-30 用户试玩：「接敌时稍微远离敌人」）。
	// 方向按瞄准朝向拆开，只留「往后」「往左外」两个分量（敌人在左前方时不会把无人机推到机体或准星上；敌人在身后就往左外挪），
	// 方向和程度都平滑过渡，不跳
	{
		const UMechCombatComponent* C0 = Mech->GetCombat();
		const AMechCharacter* Foe = C0->GetTarget();
		const bool bFoe = Foe && !Foe->GetHealth()->IsDead();
		const bool bEngaged = bFoe || Now - C0->GetLastShotTime() < 2.f || Now - LastFireTime < 2.f || Now - C0->GetLastMissileTime() < 2.f;
		const FQuat AimQ = FRotator(0.f, DroneYaw, 0.f).Quaternion();
		const FVector Away = bFoe ? (Mech->GetActorLocation() - Foe->GetActorLocation()).GetSafeNormal2D() : -AimQ.GetForwardVector();
		FVector L = AimQ.UnrotateVector(Away);
		L = FVector(FMath::Min(L.X, 0.f), FMath::Min(L.Y, 0.f), 0.f);
		if (L.SizeSquared() < 0.04f)
		{
			L = FVector(0.f, -0.6f, 0.f);
		}
		DroneAway = FMath::VInterpTo(DroneAway, AimQ.RotateVector(L), DeltaTime, 3.f);
		DroneEngage = FMath::FInterpTo(DroneEngage, bEngaged ? 1.f : 0.f, DeltaTime, 2.5f);
	}
	const FVector Ideal = DroneIdeal(true, DroneYaw);
	DroneIdealVis = Ideal;
	const FQuat VisQ = Mech->GetMechMesh() ? Mech->GetMechMesh()->VisualActorQuat() : Mech->GetActorQuat();
	// 推进器跟着机身显示 / 隐藏（模型查看只看武器、主菜单展示这些地方会藏挂件），第一人称 / 开镜时在自己画面里也藏着（机体那边给挂件设了 OwnerNoSee）
	const bool bBodyShown = Body->IsVisible() && !Body->bHiddenInGame;
	for (int32 i = 0; i < 2; ++i)
	{
		for (UStaticMeshComponent* C2 : {DronePads[i].Get(), DroneGlows[i].Get()})
		{
			if (!C2)
			{
				continue;
			}
			const bool bShow = bBodyShown && !(bDead && C2 == DroneGlows[i]);
			if (C2->IsVisible() != bShow)
			{
				C2->SetVisibility(bShow);
			}
			if (C2->bOwnerNoSee != Body->bOwnerNoSee)
			{
				C2->SetOwnerNoSee(Body->bOwnerNoSee);
			}
		}
	}
	UMechMeshComponent* MM = Mech->GetMechMesh();
	if (bDead)
	{
		DroneGrab = 0.f;
		if (MM)
		{
			MM->LeftReachWeight = 0.f;
		}
		// 阵亡：断电掉下去（光点灭、翻滚着落地），躺在残骸边上
		if (!bDroneDown)
		{
			bDroneDown = true;
			if (!bDroneInit)
			{
				bDroneInit = true;
				DronePos = Ideal;
				DroneRot = FRotator(0.f, VisQ.Rotator().Yaw, 0.f).Quaternion();
			}
			DroneVel += FVector(0.f, 0.f, -60.f);
			DroneSpin = FMath::VRand() * 4.f;
		}
		if (!DroneVel.IsZero())
		{
			const FVector Next = DronePos + DroneVel * DeltaTime;
			const FVector Clamped = ClampToWorld(DronePos, Next, 18.f);
			if (!Clamped.Equals(Next, 0.5f))
			{
				DronePos = Clamped;
				DroneVel = FVector::ZeroVector;
				DroneSpin = FVector::ZeroVector;
			}
			else
			{
				DronePos = Next;
				DroneVel.Z -= 980.f * DeltaTime;
				DroneVel.X *= FMath::Exp(-DeltaTime * 1.5f);
				DroneVel.Y *= FMath::Exp(-DeltaTime * 1.5f);
				if (DroneVel.Z < -4000.f || DronePos.Z < Mech->GetActorLocation().Z - 5000.f)
				{
					DroneVel = FVector::ZeroVector;  // 掉出地图：别一直算
				}
			}
			const float Spin = DroneSpin.Size();
			if (Spin > 0.01f)
			{
				DroneRot = (FQuat(DroneSpin / Spin, Spin * DeltaTime) * DroneRot).GetNormalized();
			}
		}
		Body->SetWorldTransform(DroneFrame(DronePos, DroneRot));
		return;
	}
	if (bDroneDown)
	{
		// 复活 / 换回合（同一台机体没换）：重新起飞，直接回到理想点
		bDroneDown = false;
		bDroneInit = false;
	}
	if (!bDroneInit || FVector::DistSquared(Ideal, DronePos) > FMath::Square(500.f))
	{
		// 第一帧、出生、传送（换回合、木桩回原位）：直接到位
		bDroneInit = true;
		DronePos = Ideal;
		DroneVel = FVector::ZeroVector;
		DronePrevIdeal = Ideal;
		DroneRot = FRotator(0.f, VisQ.Rotator().Yaw, 0.f).Quaternion();
	}
	// 弹簧阻尼追理想点，带速度前馈（追的是理想点的速度）：匀速跟着走时不落后，起步 / 急停 / 快速闪避 / 转身时才拖一点、晃回来。
	// 分步积分：卡顿的一帧不会算飞
	const FVector IdealVel = ((Ideal - DronePrevIdeal) / DeltaTime).GetClampedToMaxSize(8000.f);
	DronePrevIdeal = Ideal;
	constexpr float K = 90.f;    // 约 9.5 弧度/秒
	constexpr float C = 14.f;    // 阻尼比约 0.74：到位前略冲一点点，像悬停的东西
	constexpr float MaxLag = 80.f;
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime / (1.f / 90.f)), 1, 8);
	const float H = DeltaTime / Steps;
	for (int32 i = 0; i < Steps; ++i)
	{
		const FVector Target = Ideal - IdealVel * (DeltaTime - H * (i + 1));  // 理想点在这一小步时的位置（按匀速插回去）
		DroneVel += (K * (Target - DronePos) + C * (IdealVel - DroneVel)) * H;
		DronePos += DroneVel * H;
	}
	// 拖得太远（快速闪避一下几十米/秒）就贴着上限走
	FVector Lag = DronePos - Ideal;
	if (Lag.Size() > MaxLag)
	{
		Lag = Lag.GetSafeNormal() * MaxLag;
		DronePos = Ideal + Lag;
		const FVector N = Lag.GetSafeNormal();
		DroneVel -= N * FMath::Max(0.f, (DroneVel - IdealVel) | N);
	}
	// 钩爪自拉（抓住地形、根运动把自己拉过去）：左手伸向拉的方向前上方抓住无人机，无人机在手里领着机体飞过去（10-30 用户：
	// 「机器人抓住无人机，被无人机拉着飞过去」）。手的目标点按机体画面坐标交给网格组件，下一帧骨骼定稿时按那一帧的网格位置算（手不落后）；
	// 无人机摆在这一帧解完 IK 的左手上（骨骼定稿在这之前）。右手照旧拿着武器（长枪按 IK 之前的两只手摆，不跟着左手走）。
	// 拉敌人 / 救队友不抓。只是画面：拉动本身照旧是根运动（自己这一端预测），各端按自己看到的状态和无人机算
	const bool bGrabWant = Vis.State == EMechGrapple::Latched;
	DroneGrab = FMath::FInterpConstantTo(DroneGrab, bGrabWant ? 1.f : 0.f, DeltaTime, bGrabWant ? 5.f : 4.f);
	const float Grab = FMath::SmoothStep(0.f, 1.f, DroneGrab);
	if (MM)
	{
		if (Grab > 0.f)
		{
			// 拉的方向（机体画面坐标），往下不超过 -0.25：抓地面拉自己时手也是往前上方伸
			FVector Pull = VisQ.UnrotateVector((Vis.Tip - MM->VisualPivot()).GetSafeNormal());
			Pull.Z = FMath::Max(Pull.Z, -0.25f);
			Pull = Pull.IsNearlyZero() ? FVector::ForwardVector : Pull.GetSafeNormal();
			MM->LeftReachLocal = Pull * 70.f + FVector(0.f, -25.f, 40.f);  // 胸口往拉的方向 70、左 25、上 40 厘米：手臂伸直、朝着锚点那边往前上方
		}
		MM->LeftReachWeight = Grab;
		if (Grab > 0.f)
		{
			// 握点 = 左手骨头顺着前臂再往外 12 厘米（手心）；无人机底面中间贴在握点上
			const FVector Hand = MM->GetBoneLocation(TEXT("hand_l"));
			const FVector Fore = (Hand - MM->GetBoneLocation(TEXT("lowerarm_l"))).GetSafeNormal();
			const FVector Held = Hand + Fore * 12.f + DroneRot.GetUpVector() * (DroneMeshExtent.Z * DroneScale + 2.f);
			DronePos = FMath::Lerp(DronePos, Held, Grab);
			if (Grab > 0.99f)
			{
				DroneVel = IdealVel;  // 松手时从手上平滑飞回去，不带着旧速度乱甩
			}
		}
	}
	// 朝向：开火 / 瞄准时转向目标，平时朝机体前方
	const UMechCombatComponent* Combat = Mech->GetCombat();
	FVector Face = VisQ.GetForwardVector().GetSafeNormal2D();
	bool bAim = false;
	if (Vis.State != EMechGrapple::Idle && Claw())
	{
		Face = Claw()->GetComponentLocation() - DronePos;  // 钩爪飞出去 / 抓着：对着爪子（缆绳从发射口笔直出去）
		bAim = true;
	}
	else if (IsArtilleryDeployed() || Now - LastFireTime < 0.9f || Now - Combat->GetLastMissileTime() < 0.9f || Now - DroneFiredAt < 0.9f)
	{
		const AMechCharacter* T = Combat->GetTarget();
		Face = Mech->GetLoadout().Shoulder == EMechShoulder::Missiles && T && !T->GetHealth()->IsDead() ? MechLoop::Near(GetWorld(), T->GetActorLocation(), DronePos) - DronePos
			: Combat->GetPoseAimPoint() - DronePos;
		bAim = true;
	}
	if (Face.IsNearlyZero())
	{
		Face = VisQ.GetForwardVector();
	}
	FQuat Want = FRotationMatrix::MakeFromX(Face.GetSafeNormal()).ToQuat();
	// 按落后的方向倾斜（像多旋翼：落在后面就低头往前追，被甩到左边就往右倾），开火后坐抬一下头
	const FVector LocalLag = Want.UnrotateVector(Lag);
	const float Kick = FMath::Clamp(1.f - (Now - DroneFiredAt) / 0.25f, 0.f, 1.f);
	const float Pitch = FMath::Clamp(LocalLag.X * 0.35f, -22.f, 22.f) * (1.f - Grab) + Kick * Kick * 12.f;
	const float Roll = FMath::Clamp(-LocalLag.Y * 0.35f, -22.f, 22.f) * (1.f - Grab);
	Want = Want * FRotator(Pitch, 0.f, Roll).Quaternion();
	const float Rate = Now - DroneFiredAt < 0.12f ? 40.f : bAim ? 14.f : 6.f;
	DroneRot = FQuat::Slerp(DroneRot, Want, 1.f - FMath::Exp(-DeltaTime * Rate)).GetNormalized();
	// 轻微上下浮动（只在画面上，不进判定）
	const float Tm = Now + DronePhase;
	const FVector Bob(0.f, 0.f, (3.5f * FMath::Sin(Tm * 3.4f) + 1.2f * FMath::Sin(Tm * 7.9f)) * (1.f - Grab));
	Body->SetWorldTransform(DroneFrame(DronePos + Bob, DroneRot));
	// 推进光：跟着动起来时稍亮（落后得多 = 推力大）
	if (DroneGlowMID)
	{
		// 10-30 第一轮截图里白天也是两团白光、像车灯 → 压到 0.18–0.3（亮度按曝光归一，白天夜里屏幕上差不多；再亮就被泛光冲成白点）
		DroneGlowMID->SetScalarParameterValue(TEXT("Strength"), 0.18f + 0.12f * FMath::Clamp(Lag.Size() / 50.f, 0.f, 1.f) + 0.02f * FMath::Sin(Tm * 31.f));
	}
}

FVector UMechShoulderComponent::BodyPoint(const AMechCharacter* M)
{
	// VisualPivot 是画面上的胶囊中心往上 1 米，胸口在胶囊中心往上 40 厘米
	return M->GetMechMesh() ? M->GetMechMesh()->VisualPivot() - FVector(0.f, 0.f, 60.f) : M->GetActorLocation() + FVector(0.f, 0.f, 40.f);
}

void UMechShoulderComponent::AppendArc(TArray<FTransform>& Out, const FVector& A, const FVector& B, float Radius, float Jag, FRandomStream& Rand, int32 Segments)
{
	const FVector D = B - A;
	const float Len = D.Size();
	if (Len < 1.f || Segments < 1)
	{
		return;
	}
	FVector S1, S2;
	(D / Len).FindBestAxisVectors(S1, S2);
	const float R = Radius / 50.f;  // 引擎自带圆柱：半径 50、高 100、原点在中心、沿 Z
	FVector Prev = A;
	for (int32 i = 1; i <= Segments; ++i)
	{
		const float T = float(i) / Segments;
		FVector P = A + D * T;
		if (i < Segments)
		{
			// 中间的折点往两侧随机甩开，两头收拢（正弦包络），像电弧不像锯齿
			P += (S1 * Rand.FRandRange(-1.f, 1.f) + S2 * Rand.FRandRange(-1.f, 1.f)) * Len * Jag * FMath::Sin(T * PI);
		}
		const FVector Seg = P - Prev;
		const float L = Seg.Size();
		if (L > 0.1f)
		{
			Out.Add(FTransform(FRotationMatrix::MakeFromZ(Seg / L).ToQuat(), (Prev + P) * 0.5f, FVector(R, R, L / 100.f)));
		}
		Prev = P;
	}
}

FVector UMechShoulderComponent::GetClawRestWorld() const
{
	const UStaticMeshComponent* L = Launcher();
	return L ? L->GetComponentTransform().TransformPosition(ClawRest) : FVector::ZeroVector;
}

float UMechShoulderComponent::GrappleCd() const
{
	const AMechCharacter* Mech = GetMech();
	return GrappleCooldown * (Mech && Mech->IsOwnMaker(MechLoadout::Shoulder) ? MechWeapons::OwnGrappleCooldown : 1.f);
}

float UMechShoulderComponent::NetCd() const
{
	const AMechCharacter* Mech = GetMech();
	return NetCooldown * (Mech && Mech->IsOwnMaker(MechLoadout::Shoulder) ? MechWeapons::OwnNetCooldown : 1.f);
}

float UMechShoulderComponent::ShieldTime() const
{
	const AMechCharacter* Mech = GetMech();
	return Mech && Mech->IsOwnMaker(MechLoadout::Shoulder) ? MechWeapons::OwnShieldTime : ShieldDuration;
}

float UMechShoulderComponent::ScaledCooldown(float Base) const
{
	const AMechCharacter* Mech = GetMech();
	return Base * (Mech ? FMath::Min(1.f, Mech->EffectScale(EMechStat::MissileCooldown)) : 1.f);
}

bool UMechShoulderComponent::CanFire() const
{
	const AMechCharacter* Mech = GetMech();
	if (!Mech || !Mech->CanAct() || Mech->GetHealth()->IsStaggered() || bNetted)
	{
		return false;
	}
	// 带着核心不能用钩爪飞（夺芯里带芯本来就不能飞）
	return !(Mech->GetLoadout().Shoulder == EMechShoulder::Grapple && Mech->IsCarryingCore());
}

float UMechShoulderComponent::GetReady() const
{
	return Cooldown <= 0.f ? 1.f : FMath::Clamp(1.f - Cooldown / FMath::Max(CooldownLength, 0.01f), 0.f, 1.f);
}

float UMechShoulderComponent::GetNetLeft() const
{
	if (!bNetted)
	{
		return 0.f;
	}
	return FMath::Clamp((NetEnd - ServerNow()) / (NetDuration * NetTimeScale), 0.f, 1.f);
}

void UMechShoulderComponent::PlayAt(USoundBase* Sound, const FVector& At, float Volume, float Pitch) const
{
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, At, FRotator::ZeroRotator, Volume, Pitch * (1.f + FMath::FRandRange(-0.05f, 0.05f)), 0.f, Attenuation);
	}
}

void UMechShoulderComponent::OnLoadoutChanged()
{
	AMechCharacter* Mech = GetMech();
	if (!Mech)
	{
		return;
	}
	if (GetOwner()->HasAuthority())
	{
		if (Grapple.State != EMechGrapple::Idle)
		{
			RemovePull(Mech, PullName);
			Grapple.State = EMechGrapple::Idle;
		}
		Cooldown = 0.f;
		CooldownLength = 1.f;
	}
	bPendingPull = false;
	bPredicting = false;
	bArtyHeld = false;
	Vis.State = EMechGrapple::Idle;
}

// ---------------------------------------------------------------- 瞄准
void UMechShoulderComponent::AimShot(float Speed, FVector& OutOrigin, FVector& OutDir, FVector* OutPoint) const
{
	AMechCharacter* Mech = GetMech();
	UMechCombatComponent* Combat = Mech->GetCombat();
	OutOrigin = Muzzle();
	AMechCharacter* Target = Combat->GetTarget();
	const bool bTarget = Target && !Target->GetHealth()->IsDead();
	auto LeadPoint = [&]() {
		FVector P = MechLoop::Near(GetWorld(), Target->GetActorLocation(), OutOrigin) + FVector(0.f, 0.f, 30.f);  // 老街循环
		if (Speed > 0.f)
		{
			P += Target->GetVelocity() * (FVector::Dist(OutOrigin, P) / Speed) * Combat->LeadAccuracy;
		}
		return P;
	};
	FVector Aim = Combat->GetFreeAimPoint();
	if (!Mech->IsPlayerControlled())
	{
		// AI：对锁定的目标（网带提前量），手抖按 AI 强度
		if (bTarget)
		{
			Aim = LeadPoint() + FMath::VRand() * 120.f * Combat->AIAimErrorScale;
		}
	}
	else if (bTarget && Mech->GetController())
	{
		// 玩家：准星贴近目标（远处 3 度、近处最多 10 度内）就对准他——网很大、钩爪是去抓，比子弹宽容
		FVector ViewLoc;
		FRotator ViewRot;
		Mech->GetController()->GetPlayerViewPoint(ViewLoc, ViewRot);
		const FVector Lead = LeadPoint();
		const float Dist = FVector::Dist(Lead, ViewLoc);
		const float Cone = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(400.f / FMath::Max(Dist, 1.f))), 3.f, 10.f);
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp((Lead - ViewLoc).GetSafeNormal() | ViewRot.Vector(), -1.f, 1.f)));
		if (Angle < Cone)
		{
			Aim = Lead;
		}
	}
	// 发射口跟着无人机转向瞄准点（机身朝向变了，发射口的位置也跟着变一点）
	OutOrigin = LaunchPointToward(Aim, MuzzleLocalFor(Mech->GetLoadout().Shoulder));
	OutDir = (Aim - OutOrigin).GetSafeNormal();
	if (OutDir.IsNearlyZero())
	{
		OutDir = Mech->GetActorForwardVector();
	}
	if (OutPoint)
	{
		*OutPoint = Aim;
	}
}

// ---------------------------------------------------------------- 发射
void UMechShoulderComponent::Fire()
{
	AMechCharacter* Mech = GetMech();
	if (!Mech || Cooldown > 0.f || !CanFire())
	{
		return;
	}
	const EMechShoulder Type = Mech->GetLoadout().Shoulder;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Type == EMechShoulder::Grapple)
	{
		if (Vis.State != EMechGrapple::Idle)
		{
			return;
		}
		FVector Origin, Dir;
		AimShot(0.f, Origin, Dir);
		// 按住右键：准星附近有能勾到的队友就去救他（没有就照常发射）
		AMechCharacter* Ally = IsAllyMode() ? FindAllyTarget(Origin) : nullptr;
		if (Ally)
		{
			Dir = (BodyPoint(Ally) - Origin).GetSafeNormal();
		}
		Origin = FVector(FMath::RoundToDouble(Origin.X), FMath::RoundToDouble(Origin.Y), FMath::RoundToDouble(Origin.Z));  // 和发给服务器的一样（整厘米）
		FTraceResult Hit;
		if (Ally)
		{
			Hit.Mech = Ally;
			Hit.Point = BodyPoint(Ally);
			Hit.Dist = FVector::Dist(Origin, Hit.Point);
		}
		else
		{
			Hit = TraceGrapple(Origin, Dir);
		}
		const bool bWorld = Hit.bWorld && !Hit.Mech;
		FVector Target = FVector::ZeroVector;
		uint16 Ms = 0;
		if (bWorld)
		{
			PullTarget(Hit, Origin, Target, Ms);
		}
		++LocalStamp;
		LastFireTime = Now;
		Mech->GetCombat()->NoteEngaged();
		if (GetOwner()->HasAuthority())
		{
			FireGrappleAuthority(Origin, Dir, LocalStamp, bWorld, Target, Ms, Ally);
			return;
		}
		// 客户端预测：爪子马上飞出去；抓的是地形就在爪子飞到时自己先拉（服务器用同一个目标和时长，两边一致）
		bPredicting = true;
		bPendingPull = bWorld;
		PullTo = Target;
		PullMs = Ms;
		const float Flight = (Hit.Mech || Hit.bWorld ? Hit.Dist : GrappleRange) / ClawSpeed;
		PhaseEnd = Now + Flight;
		EnterVis(EMechGrapple::Flying, Hit.Mech ? Hit.Mech->GetActorLocation() : Hit.bWorld ? Hit.Point : Origin + Dir * GrappleRange, Hit.Mech, Flight);
		Vis.Dir = Dir;
		Cooldown = CooldownLength = (Hit.Mech || Hit.bWorld) ? GrappleCd() : GrappleMissCooldown;
		MulticastLaunchFX_Implementation(Origin, Dir, static_cast<uint8>(EMechShoulder::Grapple));  // 自己先播（组播回来时跳过）
		ServerFireGrapple(Origin, Dir, LocalStamp, bWorld, Target, Ms, Ally);
	}
	else if (Type == EMechShoulder::Net)
	{
		FVector Origin, Dir;
		AimShot(NetSpeed, Origin, Dir);
		LastFireTime = Now;
		Mech->GetCombat()->NoteEngaged();
		if (GetOwner()->HasAuthority())
		{
			FireNetAuthority(Origin, Dir);
		}
		else
		{
			Cooldown = CooldownLength = NetCd();
			MulticastLaunchFX_Implementation(Origin, Dir, static_cast<uint8>(EMechShoulder::Net));
			ServerFireNet(Origin, Dir);
		}
	}
	else if (Type == EMechShoulder::Smoke || Type == EMechShoulder::Emp)
	{
		// 抛向准星（最远 40 米）；AI 的烟雾弹丢在自己和目标之间（挡住对方的视线和锁定）
		FVector Origin, Dir, Point;
		AimShot(GrenadeSpeed, Origin, Dir, &Point);
		const AMechCharacter* Target = Mech->GetCombat()->GetTarget();
		if (Type == EMechShoulder::Smoke && !Mech->IsPlayerControlled() && Target)
		{
			const FVector Me = Mech->GetActorLocation();
			Point = Me + MechLoop::Delta(GetWorld(), Me, Target->GetActorLocation()).GetClampedToMaxSize(1000.f) - FVector(0.f, 0.f, 100.f);
		}
		if (FVector::Dist(Origin, Point) > GrenadeRange)
		{
			Point = Origin + (Point - Origin).GetSafeNormal() * GrenadeRange;
		}
		const FVector Vel = LobVelocity(Origin, Point);
		const uint8 Kind = static_cast<uint8>(Type == EMechShoulder::Smoke ? EMechGrenadeKind::Smoke : EMechGrenadeKind::Emp);
		LastFireTime = Now;
		Mech->GetCombat()->NoteEngaged();
		if (GetOwner()->HasAuthority())
		{
			FireGrenadeAuthority(Kind, Origin, Vel);
		}
		else
		{
			Cooldown = CooldownLength = GrenadeCooldown;
			MulticastLaunchFX_Implementation(Origin, Vel.GetSafeNormal(), static_cast<uint8>(Type));
			ServerFireGrenade(Kind, Origin, Vel);
		}
	}
}

void UMechShoulderComponent::MulticastLaunchFX_Implementation(FVector_NetQuantize At, FVector_NetQuantizeNormal Dir, uint8 Type)
{
	const AMechCharacter* Mech = GetMech();
	// 操控的客户端发射时自己已经播过了
	const bool bSelfPlayed = Mech && Mech->IsLocallyControlled() && !GetOwner()->HasAuthority();
	if (bSelfPlayed && GetWorld()->GetTimeSeconds() - LastFireTime > 0.01f)
	{
		return;
	}
	LastFireTime = GetWorld()->GetTimeSeconds();
	const EMechShoulder S = static_cast<EMechShoulder>(Type);
	const float FxScale = S == EMechShoulder::Artillery ? 1.6f : S == EMechShoulder::Net ? 0.8f : S == EMechShoulder::Grapple ? 0.45f : 0.6f;
	// 枪口焰从本机看到的无人机发射口出（服务器发来的 At 是判定用的理想位置，和画面上带弹簧延迟的无人机差几十厘米）；无人机马上转向、后坐一下
	OnDroneFired(Dir);
	if (Mech)
	{
		At = VisualPoint(Dir, MuzzleLocalFor(S));
	}
	if (LaunchFX)
	{
		// Ex_03_Muzzle 里有一条「动态光束」，终点是用户参数 Target（世界坐标），不设就是 (0,0,0)：每次发射从发射口拉一条线到世界原点
		// （和战斗组件 SpawnMuzzleFX 同一个问题）。先不激活、把终点设到口前 1 厘米（光束缩成看不见的一点），再激活
		if (UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, LaunchFX, At, Dir.Rotation(), FVector(FxScale), true, false,
			ENCPoolMethod::AutoRelease))
		{
			NC->SetVariablePosition(TEXT("Target"), FVector(At) + Dir.GetSafeNormal());
			NC->Activate(true);
		}
	}
	switch (S)
	{
	case EMechShoulder::Net:
		PlayAt(NetFireSound, At, 0.9f, 0.9f);
		break;
	case EMechShoulder::Artillery:
		PlayAt(ArtilleryFireSound, At, 1.f, 0.9f);
		break;
	case EMechShoulder::Smoke:
	case EMechShoulder::Emp:
		PlayAt(LauncherPopSound, At, 0.9f, S == EMechShoulder::Emp ? 1.25f : 1.f);
		break;
	default:
		PlayAt(GrappleFireSound, At, 0.9f);
		PlayAt(GrappleCableSound, At, 0.45f, 1.15f);
		break;
	}
}

// ---------------------------------------------------------------- 钩爪：瞄哪抓哪
UMechShoulderComponent::FTraceResult UMechShoulderComponent::TraceGrapple(const FVector& Origin, const FVector& Dir) const
{
	FTraceResult R;
	AMechCharacter* Mech = GetMech();
	const FVector End = Origin + Dir * GrappleRange;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechGrapple), false, Mech);
	FHitResult Hit;
	float WorldDist = GrappleRange;
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Origin, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		R.bWorld = true;
		R.Point = Hit.ImpactPoint;
		R.Normal = Hit.ImpactNormal;
		WorldDist = Hit.Distance;
		R.Dist = WorldDist;
	}
	// 敌方机体：射线离机体胶囊中线够近就算抓到（爪子张开有一米宽），取最先碰到的
	const FVector WorldEnd = Origin + Dir * WorldDist;
	float Best = WorldDist;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		AMechCharacter* M = *It;
		if (M == Mech || M->GetTeam() == Mech->GetTeam() || M->GetHealth()->IsDead())
		{
			continue;
		}
		const UCapsuleComponent* Cap = M->GetCapsuleComponent();
		const float Half = Cap->GetScaledCapsuleHalfHeight() * 0.7f;
		const FVector C = MechLoop::Near(GetWorld(), M->GetActorLocation(), Origin);  // 老街循环：钩爪 / 网按离发射点最近的那份
		FVector OnRay, OnBody;
		FMath::SegmentDistToSegmentSafe(Origin, WorldEnd, C - FVector(0.f, 0.f, Half), C + FVector(0.f, 0.f, Half), OnRay, OnBody);
		const float Along = FVector::Dist(Origin, OnRay);
		if (FVector::Dist(OnRay, OnBody) < Cap->GetScaledCapsuleRadius() + 70.f && Along < Best)
		{
			Best = Along;
			R.Mech = M;
			R.Point = OnBody;
			R.Dist = Along;
		}
	}
	return R;
}

bool UMechShoulderComponent::IsAllyMode() const
{
	const AMechCharacter* Mech = GetMech();
	if (!Mech || !Mech->IsLocallyControlled() || !Mech->IsPlayerControlled())
	{
		return false;  // AI 队友不用这个
	}
	const UMechCombatComponent* Combat = Mech->GetCombat();
	if (Combat->IsAiming() || Combat->IsGuarding())
	{
		return true;
	}
	// 右键按着但瞄准 / 格挡这一刻不成立（近战出招中、破防、切姿态）：直接看按键，「按住右键」的意思不变
	const APlayerController* PC = Cast<APlayerController>(Mech->GetController());
	return PC && (PC->IsInputKeyDown(EKeys::RightMouseButton) || PC->IsInputKeyDown(EKeys::Gamepad_LeftTrigger));
}

bool UMechShoulderComponent::AllyReachable(const AMechCharacter* Ally, const FVector& Origin, float Range) const
{
	const FVector P = BodyPoint(Ally);
	if (FVector::Dist(Origin, P) > Range)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechGrappleAlly), false, GetOwner());
	Params.AddIgnoredActor(Ally);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByObjectType(Hit, Origin, P, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
}

AMechCharacter* UMechShoulderComponent::FindAllyTarget(const FVector& Origin) const
{
	const AMechCharacter* Mech = GetMech();
	AController* Controller = Mech ? Mech->GetController() : nullptr;
	if (!Controller)
	{
		return nullptr;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	Controller->GetPlayerViewPoint(ViewLoc, ViewRot);
	const FVector ViewDir = ViewRot.Vector();
	// 准星离队友胸口多近算对上：按距离换成角度（远处 3 度，近处最多 9 度），比锁敌人严一点，免得在人堆里勾错人
	AMechCharacter* Best = nullptr;
	float BestScore = 1.f;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		AMechCharacter* M = *It;
		if (M == Mech || M->GetTeam() != Mech->GetTeam() || M->GetHealth()->IsDead())
		{
			continue;
		}
		const FVector P = BodyPoint(M);
		const float Dist = FVector::Dist(P, ViewLoc);
		const float Cone = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(AllyAimRadius / FMath::Max(Dist, 1.f))), 3.f, 9.f);
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp((P - ViewLoc).GetSafeNormal() | ViewDir, -1.f, 1.f)));
		const float Score = Angle / Cone;
		if (Score < BestScore && AllyReachable(M, Origin, GrappleRange))
		{
			Best = M;
			BestScore = Score;
		}
	}
	return Best;
}

void UMechShoulderComponent::PullTarget(const FTraceResult& Hit, const FVector& Origin, FVector& OutTarget, uint16& OutMs) const
{
	const AMechCharacter* Mech = GetMech();
	const UCapsuleComponent* Cap = Mech->GetCapsuleComponent();
	const float Radius = Cap->GetScaledCapsuleRadius();
	const float Half = Cap->GetScaledCapsuleHalfHeight();
	// 停在抓点前面一个身位；抓的是平台顶（法线朝上）就站到上面去
	FVector T = Hit.Point + Hit.Normal * (Radius + 40.f);
	if (Hit.Normal.Z > 0.6f)
	{
		T.Z = Hit.Point.Z + Half + 20.f;
	}
	OutTarget = FVector(FMath::RoundToDouble(T.X), FMath::RoundToDouble(T.Y), FMath::RoundToDouble(T.Z));
	OutMs = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(FVector::Dist(Origin, OutTarget) / PullSpeed * 1000.f), 200, 2500));
}

void UMechShoulderComponent::ApplyPull(AMechCharacter* Who, FName Name, const FVector& Target, uint16 Ms, float FinishSpeed)
{
	UMechMovementComponent* Move = Who ? Who->GetMechMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	Move->RemoveRootMotionSource(Name);
	TSharedPtr<FRootMotionSource_MoveToForce> RM = MakeShared<FRootMotionSource_MoveToForce>();
	RM->InstanceName = Name;
	RM->AccumulateMode = ERootMotionAccumulateMode::Override;
	RM->Priority = 500;
	RM->StartLocation = Who->GetActorLocation();
	RM->TargetLocation = Target;
	RM->Duration = Ms / 1000.f;
	RM->bRestrictSpeedToExpected = false;
	// 到了以后保留一点速度（荡过去的感觉），不会突然停死
	RM->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
	RM->FinishVelocityParams.ClampVelocity = FinishSpeed;
	Move->ApplyRootMotionSource(RM);
}

void UMechShoulderComponent::RemovePull(AMechCharacter* Who, FName Name)
{
	if (UMechMovementComponent* Move = Who ? Who->GetMechMovement() : nullptr)
	{
		Move->RemoveRootMotionSource(Name);
	}
}

static bool HasPull(const AMechCharacter* Who, FName Name)
{
	UMechMovementComponent* Move = Who ? Who->GetMechMovement() : nullptr;
	return Move && Move->GetRootMotionSource(Name).IsValid();
}

void UMechShoulderComponent::OnQuickBoostCancel()
{
	AMechCharacter* Mech = GetMech();
	if (HasPull(Mech, PullName))
	{
		RemovePull(Mech, PullName);  // 状态机下一帧看到拉动没了就收爪
	}
}

// ---------------------------------------------------------------- 钩爪：服务器
void UMechShoulderComponent::ServerFireGrapple_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir, uint8 Stamp, bool bWorld,
	FVector_NetQuantize Target, uint16 Ms, AMechCharacter* Ally)
{
	LastFireTime = GetWorld()->GetTimeSeconds();
	if (AMechCharacter* Mech = GetMech())
	{
		Mech->GetCombat()->NoteEngaged();
	}
	FireGrappleAuthority(Origin, Dir, Stamp, bWorld, Target, Ms, Ally);
}

void UMechShoulderComponent::FireGrappleAuthority(const FVector& Origin, const FVector& Dir, uint8 Stamp, bool bClientWorld, const FVector& ClientTarget, uint16 ClientMs,
	AMechCharacter* Ally)
{
	AMechCharacter* Mech = GetMech();
	const bool bRemote = !Mech->IsLocallyControlled() && Mech->IsPlayerControlled();
	if (Cooldown > 0.f || !CanFire() || Grapple.State != EMechGrapple::Idle)
	{
		if (bRemote)
		{
			ClientCancelGrapple(Stamp);
		}
		return;
	}
	// 发射点和服务器上的发射口差太多就用服务器的（只防明显不对）
	const FVector O = FVector::Dist(Origin, Muzzle()) > 400.f ? Muzzle() : Origin;
	FTraceResult Hit;
	Grapple.bAlly = false;
	if (Ally)
	{
		// 救队友：服务器再核一遍（同队、活着、射程内、中间没墙；两端位置有点差，射程放宽一成半）。不成立就当没抓到——
		// 不能改成沿这个方向照常抓：客户端没预测拉自己，服务器拉了会被一路纠正
		if (Ally != Mech && Ally->GetTeam() == Mech->GetTeam() && !Ally->GetHealth()->IsDead() && AllyReachable(Ally, O, GrappleRange * 1.15f))
		{
			Hit.Mech = Ally;
			Hit.Point = BodyPoint(Ally);
			Hit.Dist = FVector::Dist(O, Hit.Point);
			Grapple.bAlly = true;
		}
	}
	else
	{
		Hit = TraceGrapple(O, Dir);
	}
	Grapple.Stamp = Stamp;
	Grapple.Origin = O;
	Grapple.Victim = Hit.Mech;
	bPendingPull = false;
	float Dist = GrappleRange;
	if (Hit.Mech)
	{
		Grapple.Tip = MechLoop::Near(GetWorld(), Hit.Mech->GetActorLocation(), O);
		Dist = Hit.Dist;
	}
	else if (Hit.bWorld)
	{
		Grapple.Tip = Hit.Point;
		Dist = Hit.Dist;
		FVector T;
		uint16 Ms;
		PullTarget(Hit, O, T, Ms);
		if (bClientWorld && FVector::Dist(T, ClientTarget) < 400.f)
		{
			T = ClientTarget;  // 用客户端算的：两边目标、时长一模一样，根运动对得上，不会被拉回
			Ms = ClientMs;
		}
		bPendingPull = true;
		PullTo = T;
		PullMs = Ms;
	}
	else
	{
		Grapple.Tip = O + Dir * GrappleRange;
	}
	Grapple.FlightTime = Dist / ClawSpeed;
	const float Scale = FMath::Min(1.f, Mech->EffectScale(EMechStat::MissileCooldown));
	Cooldown = CooldownLength = (Hit.Mech || Hit.bWorld ? GrappleCd() : GrappleMissCooldown) * Scale;
	PhaseEnd = GetWorld()->GetTimeSeconds() + Grapple.FlightTime;
	SetRep(EMechGrapple::Flying);
	Vis.Dir = Dir;
	MulticastLaunchFX(O, Dir, static_cast<uint8>(EMechShoulder::Grapple));
	UE_LOG(LogTemp, Display, TEXT("[shoulder] %s 钩爪 → %s%s 距离 %.0f"), *Mech->DisplayName, Grapple.bAlly ? TEXT("队友 ") : TEXT(""),
		Hit.Mech ? *Hit.Mech->DisplayName : Hit.bWorld ? TEXT("地形") : TEXT("没抓到"), Dist);
}

void UMechShoulderComponent::SetRep(EMechGrapple State)
{
	Grapple.State = State;
	Grapple.ServerTime = ServerNow();
	if (State == EMechGrapple::Retract)
	{
		PhaseEnd = GetWorld()->GetTimeSeconds() + RetractTime;
	}
	const AMechCharacter* V = Grapple.Victim;
	EnterVis(State, State == EMechGrapple::Hooked && V ? V->GetActorLocation() : FVector(Grapple.Tip), Grapple.Victim, Grapple.FlightTime);
}

void UMechShoulderComponent::TickGrappleAuthority()
{
	AMechCharacter* Mech = GetMech();
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bAble = Mech->CanAct() && !Mech->GetHealth()->IsStaggered() && !bNetted;
	const bool bRemote = !Mech->IsLocallyControlled() && Mech->IsPlayerControlled();
	auto Abort = [&]() {
		// 被打断（失衡、被网住、击毁）：松开、收爪；操控的客户端撤掉自己的预测
		RemovePull(Mech, PullName);
		bPendingPull = false;
		if (bRemote)
		{
			ClientCancelGrapple(Grapple.Stamp);
		}
		SetRep(EMechGrapple::Retract);
	};
	switch (Grapple.State)
	{
	case EMechGrapple::Flying:
		if (!bAble)
		{
			Abort();
		}
		else if (Now >= PhaseEnd)
		{
			AMechCharacter* V = Grapple.Victim;
			if (V)
			{
				if (V->GetHealth()->IsDead() || FVector::Dist(V->GetActorLocation(), Mech->GetActorLocation()) > GrappleRange * 1.3f)
				{
					SetRep(EMechGrapple::Retract);
					break;
				}
				// 对方开着护盾、爪子从正面来：弹开收回（队友的盾不挡）
				if (UMechShoulderComponent* VS = V->GetShoulder(); !Grapple.bAlly && VS && VS->BlocksFrom(Mech->GetActorLocation()))
				{
					VS->OnShieldBlocked(V->GetActorLocation() + (Mech->GetActorLocation() - V->GetActorLocation()).GetSafeNormal() * ShieldRadius);
					SetRep(EMechGrapple::Retract);
					break;
				}
				// 抓住敌人：伤害 + 一点失衡，把他拉到自己身前（中间有墙就停在墙前）；抓住队友：不伤害，拉到身前近一点的地方
				const FVector At = V->GetActorLocation();
				if (!Grapple.bAlly)
				{
					const float Dealt = V->ReceiveHit(GrappleDamage, GrappleImpact, Mech, At, EMechHitKind::Missile, 1.4f);
					Mech->GetCombat()->ConfirmHit(V, true, EMechHitKind::Missile, Dealt, At);
				}
				const FVector Me = MechLoop::Near(GetWorld(), Mech->GetActorLocation(), At);  // 老街循环：按被抓那台那一边算拉到哪
				FVector ToV = At - Me;
				ToV.Z = 0.f;
				ToV = ToV.IsNearlyZero() ? Mech->GetActorForwardVector() : ToV.GetSafeNormal();
				FVector T = Me + ToV * (Grapple.bAlly ? AllyDragStop : DragStop) + FVector(0.f, 0.f, 40.f);
				FHitResult Wall;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(MechGrappleDrag), false, V);
				Params.AddIgnoredActor(Mech);
				if (GetWorld()->LineTraceSingleByObjectType(Wall, At, T, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
				{
					T = Wall.Location - (T - At).GetSafeNormal() * (V->GetCapsuleComponent()->GetScaledCapsuleRadius() + 20.f);
				}
				T = FVector(FMath::RoundToDouble(T.X), FMath::RoundToDouble(T.Y), FMath::RoundToDouble(T.Z));
				const uint16 Ms = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(FVector::Dist(At, T) / DragSpeed * 1000.f), 150, 2000));
				if (UMechShoulderComponent* VS = V->GetShoulder())
				{
					// 11-01 积分制（MechScore.h）：钩爪拉敌人 = 控制命中；拉队友时他正被网住 = 救人（拉过来顺带挣脱，见 ReceiveDrag）
					if (!Grapple.bAlly)
					{
						MechScore::NoteControl(Mech, V);
					}
					else if (VS->IsNetted())
					{
						MechScore::NoteRescue(Mech, false);
					}
					VS->ReceiveDrag(T, Ms, Grapple.bAlly);
				}
				++(Grapple.bAlly ? DebugRescues : DebugHooks);
				PhaseEnd = Now + Ms / 1000.f + 0.05f;
				SetRep(EMechGrapple::Hooked);
				UE_LOG(LogTemp, Display, TEXT("[shoulder] %s 钩住%s %s，拉 %.0f 厘米 %d 毫秒"), *Mech->DisplayName, Grapple.bAlly ? TEXT("队友") : TEXT(""), *V->DisplayName,
					FVector::Dist(At, T), Ms);
			}
			else if (bPendingPull)
			{
				bPendingPull = false;
				ApplyPull(Mech, PullName, PullTo, PullMs, 900.f);
				++DebugLatches;
				PhaseEnd = Now + PullMs / 1000.f + 0.1f;
				SetRep(EMechGrapple::Latched);
			}
			else
			{
				SetRep(EMechGrapple::Retract);
			}
		}
		break;
	case EMechGrapple::Latched:
		if (!bAble)
		{
			Abort();
		}
		else if (Now >= PhaseEnd || !HasPull(Mech, PullName))
		{
			RemovePull(Mech, PullName);
			SetRep(EMechGrapple::Retract);
		}
		break;
	case EMechGrapple::Hooked:
	{
		const AMechCharacter* V = Grapple.Victim;
		if (!V || V->GetHealth()->IsDead() || Now >= PhaseEnd || !Mech->CanAct())
		{
			SetRep(EMechGrapple::Retract);
		}
		break;
	}
	case EMechGrapple::Retract:
		if (Now >= PhaseEnd)
		{
			Grapple.Victim = nullptr;
			SetRep(EMechGrapple::Idle);
		}
		break;
	default:
		break;
	}
}

void UMechShoulderComponent::ReceiveDrag(const FVector& Target, uint16 Ms, bool bRescue)
{
	AMechCharacter* Mech = GetMech();
	// 队友来救：被网住的先挣脱（网的移动限制会把根运动压住）
	if (bRescue && bNetted)
	{
		ReleaseNet();
	}
	// 自己正在钩别人 / 拉自己：断掉
	if (Grapple.State != EMechGrapple::Idle && Grapple.State != EMechGrapple::Retract)
	{
		RemovePull(Mech, PullName);
		bPendingPull = false;
		if (!Mech->IsLocallyControlled() && Mech->IsPlayerControlled())
		{
			ClientCancelGrapple(Grapple.Stamp);
		}
		SetRep(EMechGrapple::Retract);
	}
	if (!Mech->GetController())
	{
		// 测试场木桩没有控制器：默认不算移动、移动模式也是「无」，根运动不起作用
		UMechMovementComponent* Move = Mech->GetMechMovement();
		Move->bRunPhysicsWithNoController = true;
		if (Move->MovementMode == MOVE_None)
		{
			Move->SetDefaultMovementMode();
		}
	}
	ApplyPull(Mech, DragName, Target, Ms, 300.f);
	if (!Mech->IsLocallyControlled() && Mech->IsPlayerControlled())
	{
		ClientApplyDrag(Target, Ms, bRescue);  // 被拉的玩家那一端按同一个目标拉，免得被一帧帧拉回
	}
}

void UMechShoulderComponent::ClientApplyDrag_Implementation(FVector_NetQuantize Target, uint16 Ms, bool bRescue)
{
	AMechCharacter* Mech = GetMech();
	if (bRescue && Mech)
	{
		// 网已经在服务器上解开了，但 bNetted 的复制可能比这条消息晚到：本地先放开移动，两边按同样的速度上限算根运动
		Mech->GetMechMovement()->bNetLock = false;
	}
	ApplyPull(Mech, DragName, Target, Ms, 300.f);
}

void UMechShoulderComponent::ClientCancelGrapple_Implementation(uint8 Stamp)
{
	if (Stamp != LocalStamp || !bPredicting)
	{
		return;
	}
	RemovePull(GetMech(), PullName);
	bPendingPull = false;
	PhaseEnd = GetWorld()->GetTimeSeconds() + RetractTime;
	EnterVis(EMechGrapple::Retract, Vis.Tip, nullptr, 0.f);
}

// ---------------------------------------------------------------- 钩爪：操控的客户端自己推进（预测）
void UMechShoulderComponent::TickGrapplePredicted()
{
	if (!bPredicting)
	{
		return;
	}
	AMechCharacter* Mech = GetMech();
	const float Now = GetWorld()->GetTimeSeconds();
	switch (Vis.State)
	{
	case EMechGrapple::Flying:
		if (Now >= PhaseEnd)
		{
			if (bPendingPull)
			{
				bPendingPull = false;
				ApplyPull(Mech, PullName, PullTo, PullMs, 900.f);
				PhaseEnd = Now + PullMs / 1000.f + 0.1f;
				EnterVis(EMechGrapple::Latched, Vis.Tip, nullptr, 0.f);
			}
			else if (!Vis.Victim.IsValid() || Now >= PhaseEnd + 0.6f)
			{
				// 没抓到（或等服务器说「钩住了」等太久）：收回
				PhaseEnd = Now + RetractTime;
				EnterVis(EMechGrapple::Retract, Vis.Tip, nullptr, 0.f);
			}
		}
		break;
	case EMechGrapple::Latched:
		if (Now >= PhaseEnd || !HasPull(Mech, PullName))
		{
			RemovePull(Mech, PullName);
			PhaseEnd = Now + RetractTime;
			EnterVis(EMechGrapple::Retract, Vis.Tip, nullptr, 0.f);
		}
		break;
	case EMechGrapple::Idle:
		bPredicting = false;
		break;
	default:
		break;
	}
}

void UMechShoulderComponent::OnRep_Grapple()
{
	AMechCharacter* Mech = GetMech();
	if (!Mech)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Mech->IsLocallyControlled())
	{
		// 自己的机体：画面按自己预测的走，只在服务器的结果和预测不一样时跟服务器
		if (Grapple.Stamp != LocalStamp)
		{
			return;
		}
		AMechCharacter* V = Grapple.Victim;
		if (Grapple.State == EMechGrapple::Hooked && Vis.State != EMechGrapple::Hooked && V)
		{
			RemovePull(Mech, PullName);
			bPendingPull = false;
			EnterVis(EMechGrapple::Hooked, V->GetActorLocation(), V, 0.f);
		}
		else if (Grapple.State == EMechGrapple::Flying && V && !Vis.Victim.IsValid())
		{
			bPendingPull = false;  // 服务器抓到的是人，不是地形
			Vis.Victim = V;
		}
		else if ((Grapple.State == EMechGrapple::Retract || Grapple.State == EMechGrapple::Idle) && Vis.State == EMechGrapple::Hooked)
		{
			EnterVis(EMechGrapple::Retract, Vis.Tip, nullptr, 0.f);
		}
		return;
	}
	// 别人的机体：照着复制来的状态画
	switch (Grapple.State)
	{
	case EMechGrapple::Flying:
		EnterVis(EMechGrapple::Flying, Grapple.Tip, Grapple.Victim, Grapple.FlightTime);
		Vis.Start = Now - FMath::Clamp(ServerNow() - Grapple.ServerTime, 0.f, Grapple.FlightTime);
		Vis.Dir = (FVector(Grapple.Tip) - FVector(Grapple.Origin)).GetSafeNormal();
		break;
	case EMechGrapple::Hooked:
		if (const AMechCharacter* V = Grapple.Victim)
		{
			EnterVis(EMechGrapple::Hooked, V->GetActorLocation(), Grapple.Victim, 0.f);
		}
		break;
	default:
		EnterVis(Grapple.State, Grapple.Tip, nullptr, 0.f);
		break;
	}
}

// ---------------------------------------------------------------- 钩爪：画面
void UMechShoulderComponent::EnterVis(EMechGrapple State, const FVector& Tip, AMechCharacter* Victim, float FlightTime)
{
	const EMechGrapple Old = Vis.State;
	const UStaticMeshComponent* C = Claw();
	Vis.From = C ? C->GetComponentLocation() : Muzzle();
	Vis.State = State;
	Vis.Tip = Tip;
	Vis.Victim = Victim;
	Vis.Start = GetWorld()->GetTimeSeconds();
	Vis.FlightTime = FlightTime;
	if (State == Old)
	{
		return;
	}
	AMechCharacter* Mech = GetMech();
	switch (State)
	{
	case EMechGrapple::Latched:
		PlayAt(GrappleLatchSound, Tip, 0.9f);
		PlayAt(GrappleReelSound, Mech->GetActorLocation(), 0.7f, 1.1f);
		if (SparkFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SparkFX, Tip, (-Vis.Dir).Rotation(), FVector(0.6f), true, true, ENCPoolMethod::AutoRelease);
		}
		break;
	case EMechGrapple::Hooked:
		PlayAt(GrappleLatchSound, Tip, Grapple.bAlly ? 0.7f : 1.f, Grapple.bAlly ? 1.1f : 0.85f);
		PlayAt(GrappleReelSound, Mech->GetActorLocation(), 0.8f, 0.95f);
		if (SparkFX && !Grapple.bAlly)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SparkFX, Tip + FVector(0.f, 0.f, 40.f), (Mech->GetActorLocation() - Tip).Rotation(), FVector(1.f), true,
				true, ENCPoolMethod::AutoRelease);
		}
		break;
	case EMechGrapple::Retract:
		if (Old == EMechGrapple::Latched || Old == EMechGrapple::Hooked)
		{
			PlayAt(GrappleReleaseSound, Muzzle(), 0.5f, 1.3f);
		}
		break;
	default:
		break;
	}
}

void UMechShoulderComponent::UpdateVisuals(float DeltaTime)
{
	AMechCharacter* Mech = GetMech();
	UStaticMeshComponent* C = Claw();
	UStaticMeshComponent* L = Launcher();
	if (!Mech || !C || !L || !Cable)
	{
		return;
	}
	const bool bGrapple = Mech->GetLoadout().Shoulder == EMechShoulder::Grapple;
	if (C->IsVisible() != bGrapple)
	{
		C->SetVisibility(bGrapple);
	}
	if (!bGrapple || Mech->GetHealth()->IsDead())
	{
		if (Cable->GetInstanceCount() > 0)
		{
			Cable->ClearInstances();
		}
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const float T = Now - Vis.Start;
	// 画面上的发射口：本机看到的无人机（这一帧 UpdateDrone 刚摆好、正对着爪子）；判定用的 Muzzle() 是不带弹簧的理想位置
	const FVector M = L->GetComponentTransform().TransformPosition(GrappleMuzzle);
	FVector P;
	FQuat Q = L->GetComponentQuat();
	float Sag = 0.f;
	auto Toward = [&](const FVector& To) { return FRotationMatrix::MakeFromX((To - M).GetSafeNormal()).ToQuat(); };
	auto VictimPoint = [this](const AMechCharacter* V) { return GetOwner() ? MechLoop::Near(GetWorld(), BodyPoint(V), GetOwner()->GetActorLocation()) : BodyPoint(V); };  // 老街循环：画最近的那份  // 按画面上的网格（别人的机体网格平滑追着胶囊）
	switch (Vis.State)
	{
	case EMechGrapple::Flying:
	{
		const FVector Tip = Vis.Victim.IsValid() ? VictimPoint(Vis.Victim.Get()) : Vis.Tip;
		const float A = Vis.FlightTime > 0.f ? FMath::Clamp(T / Vis.FlightTime, 0.f, 1.f) : 1.f;
		P = FMath::Lerp(M, Tip, A);
		Q = Toward(Tip);
		Sag = 0.02f;
		break;
	}
	case EMechGrapple::Latched:
		Q = Toward(Vis.Tip);
		P = Vis.Tip - Q.GetForwardVector() * ClawReach;
		break;
	case EMechGrapple::Hooked:
		if (Vis.Victim.IsValid())
		{
			const FVector V = VictimPoint(Vis.Victim.Get());
			Q = Toward(V);
			P = V - Q.GetForwardVector() * (Vis.Victim->GetCapsuleComponent()->GetScaledCapsuleRadius() * 0.6f);
		}
		else
		{
			P = Vis.Tip;
		}
		break;
	case EMechGrapple::Retract:
	{
		const float A = FMath::Clamp(T / RetractTime, 0.f, 1.f);
		P = FMath::Lerp(Vis.From, M, A * A);
		Q = Toward(Vis.From);
		Sag = 0.06f * FMath::Sin(A * PI);
		if (A >= 1.f && !GetOwner()->HasAuthority())
		{
			Vis.State = EMechGrapple::Idle;  // 客户端画面收好（服务器由状态机切）
		}
		break;
	}
	default:
		P = L->GetComponentTransform().TransformPosition(ClawRest);
		break;
	}
	C->SetWorldLocationAndRotation(P, Q);
	if (!FMath::IsNearlyEqual(C->GetComponentScale().X, DroneScale, 0.01f))
	{
		C->SetWorldScale3D(FVector(DroneScale));  // 爪子和无人机机身一样大（机身按模型统一了大小）
	}

	// 缆绳：发射口到爪根，松的时候往下垂一点
	TArray<FTransform> Xf;
	if (Vis.State != EMechGrapple::Idle && FVector::DistSquared(M, P) > 400.f)
	{
		const float Len = FVector::Dist(M, P);
		const float R = CableRadius / 50.f;
		auto At = [&](float A) { return FMath::Lerp(M, P, A) - FVector(0.f, 0.f, Sag * Len * 4.f * A * (1.f - A)); };
		for (int32 i = 0; i < CableSegments; ++i)
		{
			const FVector A = At(float(i) / CableSegments);
			const FVector B = At(float(i + 1) / CableSegments);
			const FVector D = B - A;
			Xf.Add(FTransform(FRotationMatrix::MakeFromZ(D.GetSafeNormal()).ToQuat(), (A + B) * 0.5f, FVector(R, R, D.Size() / 100.f + 0.01f)));
		}
	}
	if (Xf.Num() || Cable->GetInstanceCount() > 0)
	{
		Cable->ClearInstances();
		if (Xf.Num())
		{
			Cable->AddInstances(Xf, false, true);
			Cable->BatchUpdateInstancesTransforms(0, Xf, Xf, true, true, true);
		}
	}
}

void UMechShoulderComponent::UpdateAimHint()
{
	AimHint = 0;
	AllyAim = nullptr;
	AMechCharacter* Mech = GetMech();
	if (!Mech->IsLocallyControlled() || !Mech->IsPlayerControlled() || Mech->GetLoadout().Shoulder != EMechShoulder::Grapple
		|| Vis.State != EMechGrapple::Idle || !CanFire())
	{
		return;
	}
	FVector Origin, Dir;
	AimShot(0.f, Origin, Dir);
	if (IsAllyMode())
	{
		if (AMechCharacter* Ally = FindAllyTarget(Origin))
		{
			AllyAim = Ally;
			AimHint = 3;
			return;
		}
	}
	const FTraceResult Hit = TraceGrapple(Origin, Dir);
	AimHint = Hit.Mech ? 2 : Hit.bWorld ? 1 : 0;
}

// ---------------------------------------------------------------- 捕获网
void UMechShoulderComponent::ServerFireNet_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir)
{
	LastFireTime = GetWorld()->GetTimeSeconds();
	if (AMechCharacter* Mech = GetMech())
	{
		Mech->GetCombat()->NoteEngaged();
	}
	FireNetAuthority(Origin, Dir);
}

void UMechShoulderComponent::FireNetAuthority(const FVector& Origin, const FVector& Dir)
{
	AMechCharacter* Mech = GetMech();
	if (Cooldown > 0.f || !CanFire())
	{
		return;
	}
	const FVector O = FVector::Dist(Origin, Muzzle()) > 400.f ? Muzzle() : Origin;
	const FTransform At(Dir.Rotation(), O);
	AMechNet* Net = GetWorld()->SpawnActorDeferred<AMechNet>(AMechNet::StaticClass(), At, Mech, Mech, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Net)
	{
		// 带一半自己的速度（冲刺中发射不会显得慢吞吞）
		Net->Init(Mech, O, Dir * NetSpeed + Mech->GetVelocity() * 0.5f);
		Net->FinishSpawning(At);
	}
	Cooldown = CooldownLength = NetCd() * FMath::Min(1.f, Mech->EffectScale(EMechStat::MissileCooldown));
	MulticastLaunchFX(O, Dir, static_cast<uint8>(EMechShoulder::Net));
	UE_LOG(LogTemp, Display, TEXT("[shoulder] %s 发射捕获网"), *Mech->DisplayName);
}

void UMechShoulderComponent::ApplyNet(AMechCharacter* By, AMechNet* Net)
{
	AMechCharacter* Mech = GetMech();
	if (bNetted && HeldNet && HeldNet != Net)
	{
		HeldNet->Break();
	}
	bNetted = true;
	HeldNet = Net;
	NetStart = ServerNow();
	NetEnd = NetStart + NetDuration * NetTimeScale;  // 框架特色：维克托重工抗干扰，网住的时间短一些
	ApplyNetLock();
	// 自己的钩爪断掉
	if (Grapple.State != EMechGrapple::Idle && Grapple.State != EMechGrapple::Retract)
	{
		RemovePull(Mech, PullName);
		bPendingPull = false;
		if (!Mech->IsLocallyControlled() && Mech->IsPlayerControlled())
		{
			ClientCancelGrapple(Grapple.Stamp);
		}
		SetRep(EMechGrapple::Retract);
	}
}

void UMechShoulderComponent::Struggle()
{
	if (!bNetted)
	{
		return;
	}
	NetEnd = FMath::Max(NetStart + NetMinHold, NetEnd - NetStruggle);
	MulticastStruggle();
}

void UMechShoulderComponent::MulticastStruggle_Implementation()
{
	LastStruggleTime = GetWorld()->GetTimeSeconds();
	if (const AMechCharacter* Mech = GetMech())
	{
		PlayAt(NetStruggleSound, Mech->GetActorLocation(), 0.45f, 1.35f);
	}
}

void UMechShoulderComponent::ReleaseNet()
{
	bNetted = false;
	ApplyNetLock();
	if (HeldNet)
	{
		HeldNet->Break();
	}
	HeldNet = nullptr;
}

void UMechShoulderComponent::OnRep_Netted()
{
	ApplyNetLock();
}

void UMechShoulderComponent::ApplyNetLock()
{
	if (AMechCharacter* Mech = GetMech())
	{
		Mech->GetMechMovement()->bNetLock = bNetted;
		if (bNetted)
		{
			Mech->GetMechMovement()->bWantsThrust = false;
			Mech->GetCombat()->SetTrigger(false);
		}
	}
}

// ---------------------------------------------------------------- E 按下 / 松开
void UMechShoulderComponent::Pressed()
{
	AMechCharacter* Mech = GetMech();
	if (!Mech)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	switch (Mech->GetLoadout().Shoulder)
	{
	case EMechShoulder::Artillery:
		// 按住架炮：0.45 秒架好（开镜、自动瞄准），松开开炮
		if (bArtyHeld || Cooldown > 0.f || !CanFire())
		{
			return;
		}
		bArtyHeld = true;
		ArtyHoldStart = Now;
		PlayAt(ArtilleryReadySound, Muzzle(), 0.6f, 1.1f);
		if (GetOwner()->HasAuthority())
		{
			bArtyDeployed = true;
		}
		else
		{
			ServerSetArtillery(true);
		}
		Mech->GetCombat()->NoteEngaged();
		return;
	case EMechShoulder::Shield:
		if (Cooldown > 0.f || !CanFire() || IsShieldUp())
		{
			return;
		}
		if (GetOwner()->HasAuthority())
		{
			ActivateShieldAuthority();
		}
		else
		{
			Cooldown = CooldownLength = ShieldCooldown;
			ServerShield();
		}
		return;
	default:
		Fire();
		return;
	}
}

void UMechShoulderComponent::Released()
{
	if (!bArtyHeld)
	{
		return;
	}
	const bool bReady = GetArtilleryDeploy() >= 1.f;
	bArtyHeld = false;
	if (GetOwner()->HasAuthority())
	{
		bArtyDeployed = false;
	}
	else
	{
		ServerSetArtillery(false);
	}
	if (bReady && Cooldown <= 0.f && CanFire())
	{
		FireArtillery();
	}
}

// ---------------------------------------------------------------- 火炮
float UMechShoulderComponent::GetArtilleryDeploy() const
{
	return bArtyHeld ? FMath::Clamp((GetWorld()->GetTimeSeconds() - ArtyHoldStart) / ArtilleryDeployTime, 0.f, 1.f) : 0.f;
}

void UMechShoulderComponent::ServerSetArtillery_Implementation(bool bOn)
{
	bArtyDeployed = bOn;
}

void UMechShoulderComponent::FireArtillery()
{
	AMechCharacter* Mech = GetMech();
	FVector Origin, Dir;
	AimShot(ShellSpeed, Origin, Dir);
	LastFireTime = GetWorld()->GetTimeSeconds();
	Mech->GetCombat()->NoteEngaged();
	Mech->CameraKick(-Dir, 0.55f, 5.f);  // 开炮的后坐：镜头往后一顿、上抬
	if (GetOwner()->HasAuthority())
	{
		FireShellAuthority(Origin, Dir);
		return;
	}
	Cooldown = CooldownLength = ArtilleryCooldown;
	MulticastLaunchFX_Implementation(Origin, Dir, static_cast<uint8>(EMechShoulder::Artillery));
	ServerFireShell(Origin, Dir);
}

void UMechShoulderComponent::ServerFireShell_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir)
{
	LastFireTime = GetWorld()->GetTimeSeconds();
	FireShellAuthority(Origin, Dir);
}

void UMechShoulderComponent::FireShellAuthority(const FVector& Origin, const FVector& Dir)
{
	AMechCharacter* Mech = GetMech();
	if (Cooldown > 0.f || !CanFire())
	{
		return;
	}
	const FVector O = FVector::Dist(Origin, Muzzle()) > 400.f ? Muzzle() : Origin;
	const FTransform At(Dir.Rotation(), O);
	if (AMechGrenade* Shell = GetWorld()->SpawnActorDeferred<AMechGrenade>(AMechGrenade::StaticClass(), At, Mech, Mech, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		Shell->Init(Mech, EMechGrenadeKind::Shell, O, Dir * ShellSpeed, 0.f);
		Shell->FinishSpawning(At);
	}
	Cooldown = CooldownLength = ScaledCooldown(ArtilleryCooldown) * (GetMech() && GetMech()->IsOwnMaker(MechLoadout::Shoulder) ? MechWeapons::OwnArtilleryCooldown : 1.f);
	Mech->GetCombat()->NoteEngaged();
	MulticastLaunchFX(O, Dir, static_cast<uint8>(EMechShoulder::Artillery));
	UE_LOG(LogTemp, Display, TEXT("[shoulder] %s 开炮"), *Mech->DisplayName);
}

void UMechShoulderComponent::ArtilleryAutoAim(float DeltaTime)
{
	AMechCharacter* Mech = GetMech();
	AController* Controller = Mech->GetController();
	if (!Controller || !Mech->IsLocallyControlled() || !Mech->IsPlayerControlled() || GetArtilleryDeploy() < 0.6f)
	{
		return;
	}
	const AMechCharacter* Target = Mech->GetCombat()->GetTarget();
	if (!Target || Target->GetHealth()->IsDead())
	{
		return;
	}
	// 自瞄：准星离目标（提前量点）25 度内就往它身上拉，架好后基本贴住
	FVector ViewLoc;
	FRotator ViewRot;
	Controller->GetPlayerViewPoint(ViewLoc, ViewRot);
	FVector Lead = MechLoop::Near(GetWorld(), Target->GetActorLocation(), ViewLoc) + FVector(0.f, 0.f, 30.f);
	Lead += Target->GetVelocity() * (FVector::Dist(Muzzle(), Lead) / ShellSpeed);
	const FRotator Want = (Lead - ViewLoc).Rotation();
	const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Want.Vector() | ViewRot.Vector(), -1.f, 1.f)));
	if (Angle < 25.f)
	{
		Controller->SetControlRotation(FMath::RInterpTo(Controller->GetControlRotation(), Want, DeltaTime, 10.f));
	}
}

// ---------------------------------------------------------------- 烟雾弹 / EMP
FVector UMechShoulderComponent::LobVelocity(const FVector& Origin, const FVector& Target)
{
	const float T = FMath::Clamp(FVector::Dist(Origin, Target) / GrenadeSpeed, 0.35f, 1.5f);
	return (Target - Origin) / T + FVector(0.f, 0.f, 0.5f * GrenadeGravity * T);
}

void UMechShoulderComponent::ServerFireGrenade_Implementation(uint8 Kind, FVector_NetQuantize Origin, FVector_NetQuantize10 Velocity)
{
	LastFireTime = GetWorld()->GetTimeSeconds();
	FireGrenadeAuthority(Kind, Origin, Velocity);
}

void UMechShoulderComponent::FireGrenadeAuthority(uint8 Kind, const FVector& Origin, const FVector& Velocity)
{
	AMechCharacter* Mech = GetMech();
	if (Cooldown > 0.f || !CanFire() || Kind > static_cast<uint8>(EMechGrenadeKind::Emp))
	{
		return;
	}
	const FVector O = FVector::Dist(Origin, Muzzle()) > 400.f ? Muzzle() : Origin;
	const FVector V = Velocity.GetClampedToMaxSize(GrenadeRange / 0.35f);
	const FTransform At(V.Rotation(), O);
	if (AMechGrenade* G = GetWorld()->SpawnActorDeferred<AMechGrenade>(AMechGrenade::StaticClass(), At, Mech, Mech, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		G->Init(Mech, static_cast<EMechGrenadeKind>(Kind), O, V, GrenadeGravity);
		G->FinishSpawning(At);
	}
	// 烟雾弹同厂冷却短一点（11-01）；EMP 的同厂加成是失灵时间，在 AMechGrenade 里
	const bool bOwnSmoke = static_cast<EMechGrenadeKind>(Kind) == EMechGrenadeKind::Smoke && Mech->IsOwnMaker(MechLoadout::Shoulder);
	Cooldown = CooldownLength = ScaledCooldown(GrenadeCooldown) * (bOwnSmoke ? MechWeapons::OwnSmokeCooldown : 1.f);
	Mech->GetCombat()->NoteEngaged();
	const EMechShoulder Type = Kind == static_cast<uint8>(EMechGrenadeKind::Smoke) ? EMechShoulder::Smoke : EMechShoulder::Emp;
	MulticastLaunchFX(O, V.GetSafeNormal(), static_cast<uint8>(Type));
	UE_LOG(LogTemp, Display, TEXT("[shoulder] %s 抛出%s"), *Mech->DisplayName, Type == EMechShoulder::Smoke ? TEXT("烟雾弹") : TEXT(" EMP"));
}

// ---------------------------------------------------------------- 护盾
bool UMechShoulderComponent::IsShieldUp() const
{
	return ServerNow() < ShieldEnd;
}

float UMechShoulderComponent::GetShieldLeft() const
{
	return IsShieldUp() ? FMath::Clamp((ShieldEnd - ServerNow()) / ShieldTime(), 0.f, 1.f) : 0.f;
}

bool UMechShoulderComponent::BlocksFrom(const FVector& From) const
{
	const AMechCharacter* Mech = GetMech();
	if (!Mech || !IsShieldUp() || Mech->GetHealth()->IsDead())
	{
		return false;
	}
	FVector To = From - Mech->GetActorLocation();
	To.Z = 0.f;
	if (To.IsNearlyZero())
	{
		return true;
	}
	return (To.GetSafeNormal() | Mech->GetActorForwardVector().GetSafeNormal2D()) > FMath::Cos(FMath::DegreesToRadians(ShieldAngle));
}

void UMechShoulderComponent::ServerShield_Implementation()
{
	ActivateShieldAuthority();
}

void UMechShoulderComponent::ActivateShieldAuthority()
{
	AMechCharacter* Mech = GetMech();
	if (Cooldown > 0.f || !CanFire() || IsShieldUp())
	{
		return;
	}
	ShieldEnd = ServerNow() + ShieldTime();
	Cooldown = CooldownLength = ScaledCooldown(ShieldCooldown);
	Mech->GetCombat()->NoteEngaged();
	UE_LOG(LogTemp, Display, TEXT("[shoulder] %s 开盾"), *Mech->DisplayName);
}

void UMechShoulderComponent::OnShieldBlocked(const FVector& At)
{
	++DebugBlocks;
	MulticastShieldHit(At);
}

void UMechShoulderComponent::MulticastShieldHit_Implementation(FVector_NetQuantize At)
{
	// 打中点换算成盾面 UV（和 lookdev/make_shoulder_game.py 的展开一致：u = 偏航×2.1、v = 俯仰×2.1；
	// 导入后 Y 轴镜像、V 翻转，所以 u 取负偏航、v 取 1 − 俯仰×2.1），材质从这里放一圈波纹
	if (ShieldMesh)
	{
		const FVector L = ShieldMesh->GetComponentTransform().InverseTransformPosition(At);
		const float Yaw = FMath::Atan2(L.Y, L.X);
		const float Pitch = FMath::Atan2(L.Z, FVector2D(L.X, L.Y).Size());
		ShieldHits[ShieldHitNext] = FLinearColor(-Yaw * 2.1f, 1.f - Pitch * 2.1f, GetWorld()->GetTimeSeconds(), 1.f);
		if (ShieldMID)
		{
			ShieldMID->SetVectorParameterValue(*FString::Printf(TEXT("Hit%d"), ShieldHitNext), ShieldHits[ShieldHitNext]);
		}
		ShieldHitNext = (ShieldHitNext + 1) % 4;
	}
	PlayAt(ShieldHitSound, At, 0.7f);
	if (SparkFX)
	{
		const AMechCharacter* Mech = GetMech();
		const FVector Out = Mech ? (FVector(At) - Mech->GetActorLocation()).GetSafeNormal() : FVector::UpVector;
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SparkFX, At, Out.Rotation(), FVector(0.5f), true, true, ENCPoolMethod::AutoRelease);
	}
}

void UMechShoulderComponent::UpdateShield(float DeltaTime)
{
	AMechCharacter* Mech = GetMech();
	if (!ShieldMesh)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bUp = IsShieldUp() && !Mech->GetHealth()->IsDead();
	if (bUp && (!bShieldShown || bShieldClosing))
	{
		// 展开：蜂窝从发生器那一侧的中心往外一圈圈亮起来（材质按 Deploy 0→1）
		bShieldShown = true;
		bShieldClosing = false;
		ShieldShownAt = Now;
		ShieldMesh->SetVisibility(true);
		PlayAt(ShieldUpSound, Mech->GetActorLocation(), 0.9f);
		ShieldHum->Play();
	}
	else if (!bUp && bShieldShown && !bShieldClosing)
	{
		// 到时间：0.3 秒往回收，再藏起来
		bShieldClosing = true;
		ShieldClosedAt = Now;
		ShieldHum->Stop();
		PlayAt(ShieldDownSound, Mech->GetActorLocation(), 0.6f);
	}
	if (!bShieldShown)
	{
		return;
	}
	float Deploy = FMath::Clamp((Now - ShieldShownAt) / 0.35f, 0.f, 1.f);
	if (bShieldClosing)
	{
		Deploy = 1.f - FMath::Clamp((Now - ShieldClosedAt) / 0.3f, 0.f, 1.f);
		if (Deploy <= 0.f)
		{
			bShieldShown = false;
			bShieldClosing = false;
			ShieldMesh->SetVisibility(false);
			return;
		}
	}
	// 最后 0.6 秒闪烁提示要没了；自己的盾在自己画面里淡一点（挡视线）
	const float Left = GetShieldLeft() * ShieldTime();
	const float Blink = bUp && Left < 0.6f && FMath::Fmod(Now, 0.12f) < 0.06f ? 0.35f : 1.f;
	const bool bOwnView = Mech->IsLocallyControlled() && Mech->IsPlayerControlled();
	if (ShieldMID)
	{
		ShieldMID->SetVectorParameterValue(TEXT("Emissive"), AMechCharacter::TeamGlowColor(Mech->GetTeam()));
		// 测试：-MechShieldGain= 整体亮度倍数（模型查看拍护盾时三张各用一档，找合适的亮度）
		static float Gain = 1.f;
		static const bool bHasGain = FParse::Value(FCommandLine::Get(), TEXT("MechShieldGain="), Gain);
		ShieldMID->SetScalarParameterValue(TEXT("Strength"), (bOwnView ? 0.45f : 1.f) * Blink * ShieldGain * (bHasGain ? Gain : 1.f));
		ShieldMID->SetScalarParameterValue(TEXT("Deploy"), 1.f - FMath::Square(1.f - Deploy));
	}
}

// ---------------------------------------------------------------- EMP（被打中的一方）
void UMechShoulderComponent::ApplyEmp(float Duration)
{
	AMechCharacter* Mech = GetMech();
	if (!Mech || Mech->GetHealth()->IsDead())
	{
		return;
	}
	Duration *= EmpTimeScale;  // 框架特色：维克托重工抗干扰短一些，相位电子怕 EMP 长一些
	if (UMechEffectComponent* Fx = Mech->GetEffects())
	{
		Fx->AddEffect(TEXT("Emp"), Duration);
	}
	// 能量清零、过热：推进器要等效果过去、能量回到 35 才能再用
	UMechMovementComponent* Move = Mech->GetMechMovement();
	Move->Energy = 0.f;
	Move->bOverheated = true;
	MulticastEmp(Duration);
	UE_LOG(LogTemp, Display, TEXT("[shoulder] %s 被 EMP 打中 %.1f 秒"), *Mech->DisplayName, Duration);
}

bool UMechShoulderComponent::IsEmped() const
{
	const AMechCharacter* Mech = GetMech();
	return Mech && Mech->GetEffects() && Mech->GetEffects()->HasEffect(TEXT("Emp"));
}

void UMechShoulderComponent::MulticastEmp_Implementation(float Duration)
{
	AMechCharacter* Mech = GetMech();
	EmpStart = GetWorld()->GetTimeSeconds();
	EmpUntil = EmpStart + Duration;
	EmpArcList.Reset();
	if (Mech)
	{
		PlayAt(EmpZapSound, Mech->GetActorLocation(), 0.8f);
		Mech->FlashBody(FLinearColor(0.3f, 0.6f, 1.f), 1.2f);
		Mech->CameraKick(FVector::UpVector, 0.35f);
	}
}

void UMechShoulderComponent::UpdateEmpArcs(float DeltaTime)
{
	AMechCharacter* Mech = GetMech();
	const float Now = GetWorld()->GetTimeSeconds();
	USkeletalMeshComponent* Body = Mech ? Mech->GetMesh() : nullptr;
	if (!Body || !EmpArcs || Now >= EmpUntil || Mech->GetHealth()->IsDead())
	{
		if (EmpArcs && EmpArcs->GetInstanceCount() > 0)
		{
			EmpArcs->ClearInstances();
		}
		if (EmpLight && EmpLight->IsVisible())
		{
			EmpLight->SetVisibility(false);
		}
		EmpArcList.Reset();
		return;
	}
	if (EmpBones.Num() == 0)
	{
		// 电弧挂在这些骨头附近（躯干、四肢、头），沿着骨架爬
		static const TCHAR* Names[] = {TEXT("pelvis"), TEXT("spine_01"), TEXT("spine_03"), TEXT("spine_05"), TEXT("neck_01"), TEXT("head"), TEXT("clavicle_l"),
			TEXT("clavicle_r"), TEXT("upperarm_l"), TEXT("upperarm_r"), TEXT("lowerarm_l"), TEXT("lowerarm_r"), TEXT("hand_l"), TEXT("hand_r"), TEXT("thigh_l"),
			TEXT("thigh_r"), TEXT("calf_l"), TEXT("calf_r"), TEXT("foot_l"), TEXT("foot_r")};
		for (const TCHAR* N : Names)
		{
			const int32 I = Body->GetBoneIndex(N);
			if (I != INDEX_NONE)
			{
				EmpBones.Add(I);
			}
		}
		if (EmpBones.Num() == 0)
		{
			return;
		}
	}
	const float Age = Now - EmpStart;
	const float Left = EmpUntil - Now;
	// 刚打中最密，中间稳定几道，最后 0.8 秒变稀（电放完了）
	const int32 Want = Age < 0.45f ? 9 : Left < 0.8f ? 2 : 5;
	EmpArcList.RemoveAll([Now](const FEmpArc& A) { return Now >= A.Die; });
	const FQuat BodyQ = Body->GetComponentQuat();
	const FVector Center = BodyPoint(Mech);
	auto OffsetFor = [&](int32 Bone) {
		// 偏离骨头一点、往体表外推一点（电弧贴着装甲表面，不钻在身体里），存成网格局部方向，跟着机体转
		const FVector P = Body->GetBoneLocation(Body->GetBoneName(Bone));
		const FVector Out = (P - Center).GetSafeNormal2D();
		return BodyQ.UnrotateVector(Out * FMath::FRandRange(10.f, 22.f) + FMath::VRand() * FMath::FRandRange(6.f, 16.f));
	};
	while (EmpArcList.Num() < Want)
	{
		FEmpArc A;
		A.BoneA = EmpBones[FMath::RandRange(0, EmpBones.Num() - 1)];
		// 另一头：多半是往上一两节的骨头（沿着四肢爬），三成随便挑一根（横跨躯干的长弧）
		if (FMath::FRand() < 0.7f)
		{
			FName P = Body->GetParentBone(Body->GetBoneName(A.BoneA));
			for (int32 k = 0; k < 3 && P != NAME_None; ++k)
			{
				const int32 I = Body->GetBoneIndex(P);
				if (EmpBones.Contains(I))
				{
					A.BoneB = I;
					break;
				}
				P = Body->GetParentBone(P);
			}
		}
		if (A.BoneB == INDEX_NONE)
		{
			A.BoneB = EmpBones[FMath::RandRange(0, EmpBones.Num() - 1)];
		}
		A.OffA = OffsetFor(A.BoneA);
		A.OffB = OffsetFor(A.BoneB);
		A.Die = Now + FMath::FRandRange(0.08f, 0.22f);
		A.Seed = FMath::Rand();
		EmpArcList.Add(A);
	}
	// 折线每 0.05 秒重新甩一次（一闪一闪），中间几帧不变（每帧都变看着像噪点）
	EmpJagTimer -= DeltaTime;
	if (EmpJagTimer <= 0.f)
	{
		EmpJagTimer = 0.05f;
		EmpJagSeed = FMath::Rand();
	}
	// 粗细：近处 1.2 厘米；远了至少一个像素左右（远处的机体也看得出在冒电）
	float Radius = 1.2f;
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		Radius = FMath::Max(Radius, FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), Center) * 0.0011f);
	}
	TArray<FTransform> Xf;
	for (const FEmpArc& A : EmpArcList)
	{
		const FVector PA = Body->GetBoneLocation(Body->GetBoneName(A.BoneA)) + BodyQ.RotateVector(A.OffA);
		const FVector PB = Body->GetBoneLocation(Body->GetBoneName(A.BoneB)) + BodyQ.RotateVector(A.OffB);
		FRandomStream R(A.Seed ^ EmpJagSeed);
		AppendArc(Xf, PA, PB, Radius, 0.16f, R, FVector::Dist(PA, PB) > 80.f ? 8 : 5);
	}
	EmpArcs->ClearInstances();
	if (Xf.Num())
	{
		EmpArcs->AddInstances(Xf, false, true);
	}
	// 小灯跟着电弧闪（夜里照亮机体表面；白天主要靠电弧本身的自发光）
	if (EmpLight)
	{
		EmpLight->SetVisibility(true);
		// 10-30 再压一半（用户：EMP「太亮 / 刺眼」）
		EmpLight->SetIntensity((Age < 0.4f ? 6000.f : 2000.f) * FMath::FRandRange(0.35f, 1.f) * (Left < 0.8f ? 0.5f : 1.f));
	}
}

// ---------------------------------------------------------------- 每帧
void UMechShoulderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AMechCharacter* Mech = GetMech();
	if (!Mech)
	{
		return;
	}
	if (GetOwner()->HasAuthority())
	{
		Cooldown = FMath::Max(0.f, Cooldown - DeltaTime);
		TickGrappleAuthority();
		if (bNetted && (ServerNow() >= NetEnd || Mech->GetHealth()->IsDead()))
		{
			ReleaseNet();
		}
	}
	else
	{
		Cooldown = FMath::Max(0.f, Cooldown - DeltaTime);  // 复制来的值之间本地也往下走，HUD 读数连续
		if (Mech->IsLocallyControlled())
		{
			TickGrapplePredicted();
		}
	}
	// 火炮：架着的时候失衡 / 被网住 / 换了配装就收炮；玩家架好后自动瞄准
	if (bArtyHeld && (!CanFire() || Mech->GetLoadout().Shoulder != EMechShoulder::Artillery))
	{
		bArtyHeld = false;
		if (GetOwner()->HasAuthority())
		{
			bArtyDeployed = false;
		}
		else
		{
			ServerSetArtillery(false);
		}
	}
	if (bArtyHeld)
	{
		ArtilleryAutoAim(DeltaTime);
	}
	UpdateShield(DeltaTime);
	// 被 EMP 打中：身上细电弧一直爬，到效果结束
	UpdateEmpArcs(DeltaTime);
	UpdateAimHint();
	// 无人机先摆好（机体移动、动画、网格平滑都在这之前），爪子和缆绳跟着它
	UpdateDrone(DeltaTime);
	UpdateVisuals(DeltaTime);
}

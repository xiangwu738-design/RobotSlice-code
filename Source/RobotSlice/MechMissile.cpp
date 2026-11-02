#include "MechMissile.h"
#include "MechLoop.h"
#include "MechEaster.h"
#include "MechGrunt.h"
#include "MechRun.h"
#include "MechSmoke.h"

#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/DamageType.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechHealthComponent.h"
#include "MechShoulderComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const float ArmTime = 0.15f;        // 发射后多久开始追踪
	const float Thrust = 5200.f;        // 追踪段加速度
	const float MaxLife = 7.f;
	const float ProximityFuse = 220.f;  // 离目标中心这么近就引爆
	const float FullDamageSkin = 120.f; // 离机体体表这么近吃满额伤害（近炸引信炸的基本都在这里面）
}

AMechMissile::AMechMissile()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(24.f);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);  // 同一组齐射挨得很近，导弹之间不相撞
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);  // 机体只重叠：队友穿过去，敌人在重叠时引爆
	Collision->SetGenerateOverlapEvents(true);
	Collision->SetNotifyRigidBodyCollision(true);
	RootComponent = Collision;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> MissileMesh(TEXT("/Game/Weapons/Meshes/SM_Missile"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	if (MissileMesh.Succeeded())
	{
		Mesh->SetStaticMesh(MissileMesh.Object);
	}

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Collision);
	Glow->SetRelativeLocation(FVector(-60.f, 0.f, 0.f));
	Glow->SetLightColor(FLinearColor(1.f, 0.55f, 0.22f));
	Glow->SetIntensity(2500.f);
	Glow->SetAttenuationRadius(500.f);
	Glow->SetCastShadows(false);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->InitialSpeed = 0.f;
	Movement->MaxSpeed = 6500.f;
	Movement->ProjectileGravityScale = 0.f;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
	Movement->HomingAccelerationMagnitude = 9000.f;

	// 尾焰：引擎锥体 + 曳光弹那种橙色自发光，尖朝后
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlameMat(TEXT("/Game/Weapons/Materials/MI_Tracer"));
	Flame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Flame"));
	Flame->SetupAttachment(Collision);
	Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Flame->SetCastShadow(false);
	Flame->SetRelativeLocation(FVector(-62.f, 0.f, 0.f));
	Flame->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	Flame->SetRelativeScale3D(FVector(0.09f, 0.09f, 0.45f));
	if (Cone.Succeeded())
	{
		Flame->SetStaticMesh(Cone.Object);
	}
	if (FlameMat.Succeeded())
	{
		Flame->SetMaterial(0, FlameMat.Object);
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ExpAsset(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_02_Exp_01"));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> DustAsset(TEXT("/Game/AAAVFX/Course_00/Particle/Ex_02_Projectile/Projectile_Exp_01"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ExpSound(TEXT("/Game/Audio/Combat/S_explosion_med"));
	static ConstructorHelpers::FObjectFinder<USoundAttenuation> Att(TEXT("/Game/Audio/SA_Combat"));
	ExplosionFX = ExpAsset.Object;
	DustFX = DustAsset.Object;
	ExplosionSound = ExpSound.Object;
	Attenuation = Att.Object;
}

void AMechMissile::Init(AMechCharacter* InShooter, AMechCharacter* InTarget, float InDamage, float InImpact, float InRadius)
{
	Shooter = InShooter;
	Target = InTarget;
	Damage = InDamage;
	Impact = InImpact;
	Radius = InRadius;
	Team = InShooter ? InShooter->GetTeam() : 0;
	SetInstigator(InShooter);
	if (InShooter)
	{
		Collision->IgnoreActorWhenMoving(InShooter, true);
	}
}

void AMechMissile::Launch(const FVector& Velocity)
{
	Movement->Velocity = Velocity;
}

void AMechMissile::BeginPlay()
{
	Super::BeginPlay();
	Collision->OnComponentHit.AddDynamic(this, &AMechMissile::OnHit);
	Collision->OnComponentBeginOverlap.AddDynamic(this, &AMechMissile::OnOverlap);
	// 画面上从本机看到的无人机发射管出（射手 = 生成时给的 Instigator，复制给各端）
	BaseRel[0] = Mesh->GetRelativeLocation();
	BaseRel[1] = Flame->GetRelativeLocation();
	BaseRel[2] = Glow->GetRelativeLocation();
	VisOffset = UMechShoulderComponent::LaunchVisualOffset(GetInstigator(), GetActorLocation(), GetActorForwardVector(),
		UMechShoulderComponent::MuzzleLocalFor(EMechShoulder::Missiles));
	ApplyVisOffset();
}

void AMechMissile::ApplyVisOffset()
{
	if (VisOffset.IsZero())
	{
		return;
	}
	const float K = UMechShoulderComponent::LaunchVisualFade(VisAge);
	const FVector Local = GetActorQuat().UnrotateVector(VisOffset * K);
	Mesh->SetRelativeLocation(BaseRel[0] + Local);
	Flame->SetRelativeLocation(BaseRel[1] + Local);
	Glow->SetRelativeLocation(BaseRel[2] + Local);
	if (K <= 0.f)
	{
		VisOffset = FVector::ZeroVector;
	}
}

void AMechMissile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bExploded)
	{
		return;
	}
	Age += DeltaSeconds;
	VisAge += DeltaSeconds;
	ApplyVisOffset();
	const float Flicker = FMath::Sin(Age * 70.f);
	Glow->SetIntensity(2000.f + 900.f * Flicker);
	Flame->SetRelativeScale3D(FVector(0.09f, 0.09f, 0.4f + 0.08f * Flicker));
	if (!HasAuthority())
	{
		return;
	}
	// 11-02 老街循环（MechLoop.h）：飞出接缝半个周期挪回来（服务器挪，各端按同步来的位置跳过去）
	if (const float LoopShift = MechLoop::WrapShift(GetWorld(), GetActorLocation().X, MechLoop::ProjectileMargin(GetWorld())); LoopShift != 0.f)
	{
		SetActorLocation(GetActorLocation() + FVector(LoopShift, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
	// 追踪段：沿机头方向持续加速；有目标且目标还活着就开追踪
	if (Age > ArmTime)
	{
		const FVector Fwd = Movement->Velocity.GetSafeNormal();
		Movement->Velocity = (Movement->Velocity + Fwd * Thrust * DeltaSeconds).GetClampedToMaxSize(Movement->MaxSpeed);
		// 穿过烟幕（或目标在烟里）：干扰箔片把导弹的导引头骗掉，永久丢失目标，出了烟也不再追（10-30 烟雾弹加强）
		if (Target && AMechSmoke::BlocksLine(GetWorld(), GetActorLocation(), MechLoop::Near(GetWorld(), Target->GetActorLocation(), GetActorLocation())))
		{
			UE_LOG(LogTemp, Display, TEXT("[missile] 被烟幕干扰，丢失目标 %s"), *Target->DisplayName);
			Target = nullptr;
		}
		const bool bAlive = Target && !Target->GetHealth()->IsDead();
		if (bAlive && !Movement->bIsHomingProjectile)
		{
			Movement->HomingTargetComponent = Target->GetCapsuleComponent();
			Movement->bIsHomingProjectile = true;
		}
		if (bAlive && MechLoop::IsActive(GetWorld()))
		{
			Movement->HomingTargetComponent = Target->GetLoopNearestCapsule(GetActorLocation());  // 老街循环：追离自己最近的那份（镜像胶囊）
		}
		else if (!bAlive)
		{
			Movement->bIsHomingProjectile = false;
		}
		if (bAlive && MechLoop::Dist(GetWorld(), GetActorLocation(), Target->GetActorLocation()) < ProximityFuse)
		{
			UE_LOG(LogTemp, Display, TEXT("[missile] 近炸 %s 飞行 %.2f 秒"), Shooter ? *Shooter->DisplayName : TEXT("?"), Age);
			Explode(GetActorLocation());
			return;
		}
		// 11-01 肉鸽 蜂群：追杂兵（杂兵的碰撞球是 WorldDynamic，导弹不和它相撞，靠近炸）；目标死了就直飞，RunLife 到了自爆
		if (const AMechGrunt* G = Cast<AMechGrunt>(RunTarget.Get()); G && !Target)
		{
			if (G->IsAlive())
			{
				// 导弹默认的转向是给「机甲能闪掉」调的（最高速转弯半径 47 米）：杂兵又小又多，照那个数绕着目标转圈打不中。
				// 追杂兵时转得更急、飞得慢一点（转弯半径约 7 米），锁上了基本就打得中
				Movement->HomingTargetComponent = G->GetRootComponent();
				Movement->bIsHomingProjectile = true;
				Movement->HomingAccelerationMagnitude = 30000.f;
				Movement->MaxSpeed = 4500.f;
				if (FVector::Dist(GetActorLocation(), G->GetActorLocation()) < ProximityFuse + 80.f)
				{
					Explode(GetActorLocation());
					return;
				}
			}
			else
			{
				RunTarget = nullptr;
				Movement->bIsHomingProjectile = false;
			}
		}
		// 11-02 彩蛋：没锁机体时追准星附近的无人机 / 狗（MechEaster.h），贴近就炸
		if (!Target && MechEaster::SteerMissile(this, Shooter, Movement, RunTarget)) { Explode(GetActorLocation()); return; }
	}
	if (RunLife > 0.f && Age > RunLife)
	{
		Explode(GetActorLocation());
		return;
	}
	// 11-01 肉鸽：直飞的导弹碰到杂兵也炸（杂兵的碰撞球不挡导弹，不加这条瞄着打也穿过去，重弹没锁定时打不到无人机）。对战没有 Run，不走这里
	if (!Target && !RunTarget.IsValid() && Age > ArmTime && Shooter && Shooter->GetRun())
	{
		TArray<FOverlapResult> Near;
		FCollisionQueryParams NearParams(SCENE_QUERY_STAT(MechRunFuse), false, this);
		GetWorld()->OverlapMultiByObjectType(Near, GetActorLocation(), FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldDynamic), FCollisionShape::MakeSphere(ProximityFuse * 0.5f), NearParams);
		for (const FOverlapResult& O : Near)
		{
			if (O.GetActor() && O.GetActor()->ActorHasTag(TEXT("MechGrunt")))
			{
				Explode(GetActorLocation());
				return;
			}
		}
	}
	if (Age > MaxLife)
	{
		UE_LOG(LogTemp, Display, TEXT("[missile] 超时自爆 %s 离目标 %.0f 米"), Shooter ? *Shooter->DisplayName : TEXT("?"), Target ? FVector::Dist(GetActorLocation(), Target->GetActorLocation()) / 100.f : -1.f);
		Explode(GetActorLocation());
	}
}

void AMechMissile::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (HasAuthority() && OtherActor != Shooter)
	{
		UE_LOG(LogTemp, Display, TEXT("[missile] 撞到 %s %s 飞行 %.2f 秒 离目标 %.0f 米"), Shooter ? *Shooter->DisplayName : TEXT("?"), *GetNameSafe(OtherActor), Age,
			Target ? FVector::Dist(Hit.ImpactPoint, Target->GetActorLocation()) / 100.f : -1.f);
		Explode(Hit.ImpactPoint);
	}
}

void AMechMissile::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& Sweep)
{
	const AMechCharacter* Mech = Cast<AMechCharacter>(OtherActor);
	if (HasAuthority() && Shooter && Mech && Mech != Shooter && Mech->GetTeam() != Team)
	{
		UE_LOG(LogTemp, Display, TEXT("[missile] 命中机体 %s 飞行 %.2f 秒"), Shooter ? *Shooter->DisplayName : TEXT("?"), Age);
		Explode(GetActorLocation());
	}
}

void AMechMissile::Explode(const FVector& At)
{
	if (bExploded)
	{
		return;
	}
	bExploded = true;
	// 范围伤害：离机体体表 1.2 米内满额，往外到爆炸半径边上降到一半；只伤敌方。
	// 以前按到胶囊中心的距离算，近炸引信在离中心 2.2 米就炸，几乎每发都只吃到七八成（10-30 玩家说导弹弱，改成按体表算）
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MissileBlast), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, At, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(Radius), Params);
	TSet<AMechCharacter*> Done;
	for (const FOverlapResult& O : Overlaps)
	{
		AMechCharacter* Mech = Cast<AMechCharacter>(O.GetActor());
		if (!Mech || Done.Contains(Mech) || Mech->GetTeam() == Team || Mech->GetHealth()->IsDead())
		{
			continue;
		}
		Done.Add(Mech);
		const UCapsuleComponent* Cap = Mech->GetCapsuleComponent();
		const FVector C = MechLoop::Near(GetWorld(), Mech->GetActorLocation(), At);  // 老街循环：炸到的可能是镜像胶囊
		const float Half = Cap->GetScaledCapsuleHalfHeight() - Cap->GetScaledCapsuleRadius();
		const float Skin = FMath::Max(0.f, FVector::Dist(At, FMath::ClosestPointOnSegment(At, C - FVector(0.f, 0.f, Half), C + FVector(0.f, 0.f, Half)))
			- Cap->GetScaledCapsuleRadius());
		const float Falloff = 1.f - 0.5f * FMath::Clamp((Skin - FullDamageSkin) / FMath::Max(Radius - FullDamageSkin, 1.f), 0.f, 1.f);
		const bool bDirect = Mech->GetHealth()->IsStaggered();
		AMechCharacter* From = Cast<AMechCharacter>(Shooter);
		const float Dealt = Mech->ReceiveHit(Damage * Falloff, Impact * Falloff, Shooter, At, EMechHitKind::Missile, RunStagger);
		UE_LOG(LogTemp, Display, TEXT("[missile] 伤害 %s %.0f（离体表 %.0f 厘米，系数 %.2f；旧算法系数 %.2f）"), *Mech->DisplayName, Dealt, Skin, Falloff,
			1.f - 0.5f * FMath::Clamp(FVector::Dist(At, C) / Radius, 0.f, 1.f));
		if (Dealt > 0.f && From)
		{
			From->GetCombat()->ConfirmHit(Mech, bDirect, EMechHitKind::Missile, Dealt, Mech->GetActorLocation() + FVector(0.f, 0.f, 60.f));
		}
	}
	// 保卫战的杂兵（10-31）：WorldDynamic 的碰撞球，按 ApplyDamage 交给它（范围内满额的八成，不细算体表）
	TArray<FOverlapResult> Props;
	GetWorld()->OverlapMultiByObjectType(Props, At, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldDynamic), FCollisionShape::MakeSphere(Radius), Params);
	TSet<AActor*> DoneProps;
	for (const FOverlapResult& O : Props)
	{
		AActor* A = O.GetActor();
		if (A && !DoneProps.Contains(A) && A->ActorHasTag(TEXT("MechGrunt")))
		{
			DoneProps.Add(A);
			const AMechCharacter* By = Cast<AMechCharacter>(Shooter);
			UGameplayStatics::ApplyDamage(A, Damage * 0.8f, By ? By->GetController() : nullptr, Shooter, UDamageType::StaticClass());
		}
	}
	// 11-01 肉鸽：分裂、瘫痪杂兵、火场（没有 Run 的不变）
	if (Shooter && Shooter->GetRun())
	{
		Shooter->GetRun()->OnMissileExplode(this, At);
	}
	MulticastExplodeFX(At);
	Movement->StopMovementImmediately();
	SetActorEnableCollision(false);
	Mesh->SetVisibility(false);
	SetLifeSpan(0.3f);
}

void AMechMissile::MulticastExplodeFX_Implementation(FVector_NetQuantize At)
{
	bExploded = true;
	if (ExplosionFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ExplosionFX, At, FRotator::ZeroRotator, FVector(0.9f), true, true, ENCPoolMethod::AutoRelease);
	}
	if (DustFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, DustFX, At, FRotator::ZeroRotator, FVector(0.8f), true, true, ENCPoolMethod::AutoRelease);
	}
	if (ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ExplosionSound, At, FRotator::ZeroRotator, 0.9f, FMath::FRandRange(0.9f, 1.1f), 0.f, Attenuation);
	}
	Mesh->SetVisibility(false);
	Flame->SetVisibility(false);
	Glow->SetIntensity(0.f);
	// 爆点 25 米内的本机玩家：镜头被冲击波推一下，越近越狠
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (AMechCharacter* Near = Cast<AMechCharacter>(PC->GetPawn()))
		{
			const float Dist = FVector::Dist(Near->GetActorLocation(), At);
			if (Dist < 2500.f)
			{
				const float K = 1.f - Dist / 2500.f;
				Near->CameraKick(Near->GetActorLocation() - At, 0.15f + 0.6f * K * K);
				Near->AddRecoil(0.35f * K);
			}
		}
	}
}

void AMechMissile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechMissile, Target);
}

void AMechMissile::GetEffectSystems(TArray<UNiagaraSystem*>& Out) const
{
	Out.AddUnique(ExplosionFX);
	Out.AddUnique(DustFX);
}

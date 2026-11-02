#include "MechNet.h"

#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechHealthComponent.h"
#include "MechMeshComponent.h"
#include "MechScore.h"
#include "MechShoulderComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	template <typename T>
	T* Load(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<T> Finder(Path);
		return Finder.Object;
	}
	/** 裹住机体的网按 2.4 米高的机体做（模型高 2.5 米，含头顶余量） */
	const float WrapHeight = 240.f;
}

AMechNet::AMechNet()
{
	PrimaryActorTick.bCanEverTick = true;
	// 罩住以后跟着机体画面上的网格走：放在所有移动、网络平滑、骨骼定稿之后，免得比机体慢一帧
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	bReplicates = true;
	SetReplicatingMovement(false);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(true);
	Mesh->bAffectDistanceFieldLighting = false;
	Mesh->bAffectDynamicIndirectLighting = false;
	Mesh->LightingChannels.bChannel1 = true;  // 机体补光也照到网
	OpenMesh = Load<UStaticMesh>(TEXT("/Game/Weapons/Meshes/SM_NetOpen"));
	WrapMesh = Load<UStaticMesh>(TEXT("/Game/Weapons/Meshes/SM_NetWrapped"));
	if (OpenMesh)
	{
		Mesh->SetStaticMesh(OpenMesh);
	}
	ShockAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("ShockAudio"));
	ShockAudio->SetupAttachment(Mesh);
	ShockAudio->bAutoActivate = false;
	ShockAudio->SetSound(Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_net_shock")));
	SparkFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"));
	CatchFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"));  // 电弧那个（Thunder Exp_01）带一大团黑烟，把机体整个盖住
	FlySound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_net_fly"));
	HitSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_net_hit"));
	BreakSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_net_break"));
	Attenuation = Load<USoundAttenuation>(TEXT("/Game/Audio/SA_Combat"));
	ShockAudio->AttenuationSettings = Attenuation;
}

void AMechNet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechNet, State);
	DOREPLIFETIME_CONDITION(AMechNet, LaunchPos, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechNet, LaunchVel, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechNet, LaunchTime, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechNet, Team, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechNet, Shooter, COND_InitialOnly);
	DOREPLIFETIME(AMechNet, Victim);
	DOREPLIFETIME(AMechNet, DropPos);
	DOREPLIFETIME(AMechNet, DropNormal);
	DOREPLIFETIME(AMechNet, DropOpen);
}

void AMechNet::Init(AMechCharacter* InShooter, const FVector& Origin, const FVector& Velocity)
{
	Shooter = InShooter;
	Team = InShooter ? InShooter->GetTeam() : 0;
	LaunchPos = Origin;
	LaunchVel = Velocity;
	LaunchTime = ServerNow();
}

float AMechNet::ServerNow() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

FVector AMechNet::PosAt(float T) const
{
	return LaunchPos + LaunchVel * T - FVector(0.f, 0.f, 0.5f * UMechShoulderComponent::NetGravity * T * T);
}

FVector AMechNet::VelAt(float T) const
{
	return LaunchVel - FVector(0.f, 0.f, UMechShoulderComponent::NetGravity * T);
}

float AMechNet::OpenAt(float T)
{
	// 先快后慢地张开（甩出去那一下最快）
	const float A = FMath::Clamp(T / 0.3f, 0.f, 1.f);
	return 1.f - FMath::Square(1.f - A);
}

void AMechNet::BeginPlay()
{
	Super::BeginPlay();
	// 结点发光：队伍色（远处分得清是谁的网）
	const int32 Slot = Mesh->GetMaterialIndex(TEXT("MI_Robot_Glow"));
	if (Slot != INDEX_NONE)
	{
		GlowMID = Mesh->CreateAndSetMaterialInstanceDynamic(Slot);
	}
	if (GlowMID)
	{
		GlowMID->SetVectorParameterValue(TEXT("Emissive"), AMechCharacter::TeamGlowColor(Team));
	}
	PrevT = FMath::Max(0.f, ServerNow() - LaunchTime);
	if (FlySound)
	{
		UGameplayStatics::SpawnSoundAttached(FlySound, Mesh, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true, 0.7f,
			1.f + FMath::FRandRange(-0.05f, 0.05f), 0.f, Attenuation);
	}
	StateStart = GetWorld()->GetTimeSeconds();
	if (State != EMechNetState::Flying)
	{
		EnterState();  // 进入视野时已经不在飞了（复制过来就是罩住 / 掉地的状态）
	}
	else if (PrevT < 0.5f)
	{
		VisOffset = UMechShoulderComponent::LaunchVisualOffset(Shooter, PosAt(PrevT), VelAt(PrevT), UMechShoulderComponent::MuzzleLocalFor(EMechShoulder::Net));
	}
}

void AMechNet::GetEffectSystems(TArray<UNiagaraSystem*>& Out) const
{
	Out.AddUnique(SparkFX);
	Out.AddUnique(CatchFX);
}

// ---------------------------------------------------------------- 服务器：罩住 / 掉地 / 破网
void AMechNet::Catch(AMechCharacter* Mech, const FVector& At)
{
	Victim = Mech;
	State = EMechNetState::Wrapped;
	if (UMechShoulderComponent* S = Mech->GetShoulder())
	{
		S->ApplyNet(Shooter, this);
	}
	MechScore::NoteControl(Shooter, Mech);  // 11-01 积分制：网罩住 = 控制命中（加分、算助攻）
	const float Dealt = Mech->ReceiveHit(UMechShoulderComponent::NetDamage, 100.f, Shooter, At, EMechHitKind::Missile);
	if (Shooter && Shooter->GetCombat())
	{
		Shooter->GetCombat()->ConfirmHit(Mech, true, EMechHitKind::Missile, Dealt, At);
	}
	if (Shooter && Shooter->GetShoulder())
	{
		++Shooter->GetShoulder()->DebugNets;
	}
	UE_LOG(LogTemp, Display, TEXT("[shoulder] 网罩住 %s（%s 发射，飞了 %.2f 秒）"), *Mech->DisplayName, Shooter ? *Shooter->DisplayName : TEXT("?"),
		ServerNow() - LaunchTime);
	EnterState();
}

void AMechNet::Drop(const FVector& At, const FVector& Normal)
{
	DropPos = At;
	DropNormal = Normal;
	DropOpen = OpenAt(PrevT);
	State = EMechNetState::Dropped;
	SetLifeSpan(2.4f);
	EnterState();
}

void AMechNet::Break()
{
	if (State != EMechNetState::Wrapped)
	{
		return;
	}
	State = EMechNetState::Broken;
	SetLifeSpan(1.f);
	EnterState();
}

void AMechNet::OnRep_State()
{
	EnterState();
}

void AMechNet::EnterState()
{
	StateStart = GetWorld()->GetTimeSeconds();
	switch (State)
	{
	case EMechNetState::Wrapped:
		if (WrapMesh)
		{
			Mesh->SetStaticMesh(WrapMesh);
			// 换网格后材质槽要重新挂（裹住的网也有发光结点）
			Mesh->EmptyOverrideMaterials();
			const int32 Slot = Mesh->GetMaterialIndex(TEXT("MI_Robot_Glow"));
			GlowMID = Slot != INDEX_NONE ? Mesh->CreateAndSetMaterialInstanceDynamic(Slot) : nullptr;
			if (GlowMID)
			{
				GlowMID->SetVectorParameterValue(TEXT("Emissive"), AMechCharacter::TeamGlowColor(Team));
			}
		}
		if (Victim)
		{
			const FVector At = Victim->GetActorLocation();
			UGameplayStatics::PlaySoundAtLocation(this, HitSound, At, FRotator::ZeroRotator, 1.f, 1.f, 0.f, Attenuation);
			if (CatchFX)
			{
				for (int32 k = 0; k < 3; ++k)
				{
					UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, CatchFX, At + FVector(0.f, 0.f, 40.f + 60.f * k), FRotator(FMath::FRandRange(-30.f, 60.f), 120.f * k, 0.f),
						FVector(1.3f), true, true, ENCPoolMethod::AutoRelease);
				}
			}
		}
		ShockAudio->Play();
		break;
	case EMechNetState::Dropped:
		break;
	case EMechNetState::Broken:
		ShockAudio->Stop();
		Mesh->SetVisibility(false);
		if (Victim)
		{
			const FVector At = Victim->GetActorLocation();
			UGameplayStatics::PlaySoundAtLocation(this, BreakSound, At, FRotator::ZeroRotator, 0.9f, 1.f, 0.f, Attenuation);
			if (SparkFX)
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SparkFX, At + FVector(0.f, 0.f, 60.f), FRotator(90.f, 0.f, 0.f), FVector(1.4f), true, true,
					ENCPoolMethod::AutoRelease);
			}
		}
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------- 每帧
void AMechNet::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorld()->GetTimeSeconds();
	const float Local = Now - StateStart;

	if (State == EMechNetState::Flying)
	{
		const float T = FMath::Max(0.f, ServerNow() - LaunchTime);
		const FVector Pos = PosAt(T);
		const float Open = OpenAt(T);
		// 网面朝飞行方向（模型 +X 是鼓出去的那面），边飞边慢慢转
		const FQuat Face = FRotationMatrix::MakeFromX(VelAt(T).GetSafeNormal()).ToQuat() * FQuat(FVector::ForwardVector, T * 1.6f);
		// 画面位置 = 判定弹道 + 刚出膛时的偏移（碰撞都按 PosAt 算）
		SetActorLocationAndRotation(VisOffset.IsZero() ? Pos : Pos + VisOffset * UMechShoulderComponent::LaunchVisualFade(Local), Face);
		SetActorScale3D(FVector(FMath::Lerp(0.25f, 1.f, Open), FMath::Lerp(0.08f, 1.f, Open), FMath::Lerp(0.08f, 1.f, Open)));

		if (HasAuthority())
		{
			const FVector Prev = PosAt(PrevT);
			PrevT = T;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MechNet), false, this);
			if (Shooter)
			{
				Params.AddIgnoredActor(Shooter);
			}
			// 扫过的这一段里有没有敌方机体：按网张开的半径（八成）和机体胶囊的竖直中线比距离
			const float R = 40.f + OpenRadius * 0.8f * Open;
			AMechCharacter* Best = nullptr;
			float BestT = 2.f;
			FVector BestAt = Pos;
			for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
			{
				AMechCharacter* M = *It;
				if (M == Shooter || M->GetTeam() == Team || M->GetHealth()->IsDead() || (M->GetShoulder() && M->GetShoulder()->IsNetted()))
				{
					continue;
				}
				const UCapsuleComponent* Cap = M->GetCapsuleComponent();
				const float Half = Cap->GetScaledCapsuleHalfHeight() * 0.7f;
				const FVector C = M->GetActorLocation();
				FVector OnPath, OnBody;
				FMath::SegmentDistToSegmentSafe(Prev, Pos, C - FVector(0.f, 0.f, Half), C + FVector(0.f, 0.f, Half), OnPath, OnBody);
				if (FVector::Dist(OnPath, OnBody) < R + Cap->GetScaledCapsuleRadius() * 0.5f)
				{
					const float Along = FVector::Dist(Prev, OnPath) / FMath::Max(1.f, FVector::Dist(Prev, Pos));
					if (Along < BestT)
					{
						Best = M;
						BestT = Along;
						BestAt = OnBody;
					}
				}
			}
			FHitResult Hit;
			const bool bWall = GetWorld()->LineTraceSingleByObjectType(Hit, Prev, Pos, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
			if (Best && (!bWall || BestT <= Hit.Time + 0.05f))
			{
				// 对方开着护盾、网从正面来：撞在盾上掉下去
				if (UMechShoulderComponent* S = Best->GetShoulder(); S && S->BlocksFrom(Pos))
				{
					S->OnShieldBlocked(Pos);
					FHitResult Ground;
					const bool bGround = GetWorld()->LineTraceSingleByObjectType(Ground, Pos, Pos - FVector(0.f, 0.f, 4000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params);
					Drop(bGround ? Ground.ImpactPoint : Pos, bGround ? Ground.ImpactNormal : FVector::UpVector);
					return;
				}
				Catch(Best, BestAt);
				return;
			}
			if (bWall)
			{
				Drop(Hit.ImpactPoint, Hit.ImpactNormal);
				return;
			}
			if (T > MaxFlight)
			{
				// 飞到头：往下找地面掉下去，找不到就直接消失
				FHitResult Ground;
				if (GetWorld()->LineTraceSingleByObjectType(Ground, Pos, Pos - FVector(0.f, 0.f, 4000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
				{
					Drop(Ground.ImpactPoint, Ground.ImpactNormal);
				}
				else
				{
					Destroy();
				}
			}
		}
		return;
	}

	if (State == EMechNetState::Dropped)
	{
		// 摊在地上（鼓的一面朝上），1.6 秒后缩没
		const float Shrink = 1.f - FMath::Clamp((Local - 1.6f) / 0.5f, 0.f, 1.f);
		const float S = FMath::Lerp(0.25f, 1.f, DropOpen) * Shrink;
		SetActorLocationAndRotation(DropPos + DropNormal * 12.f, FRotationMatrix::MakeFromX(DropNormal).ToQuat());
		SetActorScale3D(FVector(0.35f * FMath::Max(S, 0.01f), FMath::Max(S, 0.01f), FMath::Max(S, 0.01f)));
		if (GlowMID)
		{
			GlowMID->SetVectorParameterValue(TEXT("Emissive"), AMechCharacter::TeamGlowColor(Team) * Shrink * 0.6f);
		}
		return;
	}

	if (State == EMechNetState::Wrapped && Victim)
	{
		// 跟着网住的机体：原点在脚底，按机体高度缩放。位置、朝向按画面上的网格算（别人的机体在本机是网格平滑追着胶囊，
		// 网络修正时胶囊瞬移，按胶囊摆网会和机体分开一截）
		const UCapsuleComponent* Cap = Victim->GetCapsuleComponent();
		const float Height = Cap->GetScaledCapsuleHalfHeight() * 2.f;
		const float S = Height / WrapHeight;
		// 罩上的一瞬间从大往小收紧
		const float Tight = 1.f + 0.35f * FMath::Exp(-Local * 9.f);
		const UMechMeshComponent* VM = Victim->GetMechMesh();
		const FVector Feet = VM->VisualPivot() - FVector(0.f, 0.f, 100.f + Cap->GetScaledCapsuleHalfHeight());
		SetActorLocationAndRotation(Feet, FRotator(0.f, VM->VisualActorQuat().Rotator().Yaw, 0.f));
		SetActorScale3D(FVector(S * Tight, S * Tight, S));
		// 通电：结点一闪一闪，身上隔一会儿冒一簇火花
		if (GlowMID)
		{
			const float Flicker = 0.7f + 2.6f * FMath::Pow(FMath::FRand(), 3.f);
			GlowMID->SetVectorParameterValue(TEXT("Emissive"), AMechCharacter::TeamGlowColor(Team) * Flicker);
		}
		SparkTimer -= DeltaSeconds;
		if (SparkTimer <= 0.f && SparkFX)
		{
			SparkTimer = FMath::FRandRange(0.1f, 0.22f);
			const float A = FMath::FRandRange(0.f, 2.f * PI);
			const FVector At = Feet + FVector(FMath::Cos(A) * 62.f * S, FMath::Sin(A) * 62.f * S, FMath::FRandRange(50.f, 230.f) * S);
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SparkFX, At, FRotationMatrix::MakeFromZ(FVector(FMath::Cos(A), FMath::Sin(A), 0.f)).Rotator(),
				FVector(0.5f), true, true, ENCPoolMethod::AutoRelease);
		}
	}
}

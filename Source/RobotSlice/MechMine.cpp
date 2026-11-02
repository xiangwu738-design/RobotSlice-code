#include "MechMine.h"
#include "MechLoop.h"

#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechGameState.h"
#include "MechHealthComponent.h"
#include "MechTacticalComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

// 名字带前缀的命名空间（不用匿名命名空间）：将来合并编译（unity）时和别的文件里同名的 Load / 颜色常量不打架
namespace MechMineLocal
{
	template <typename T>
	T* Load(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<T> Finder(Path);
		return Finder.Object;
	}

	/** 地雷的范围圈颜色：暖橙（和 EMP 的蓝分开；震荡波偏白一点） */
	const FLinearColor RingColor(1.f, 0.42f, 0.12f);
}

AMechMine::AMechMine()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(false);

	// 根：打得中的小球。物体类型 WorldDynamic、只做查询、对所有通道忽略：子弹按物体类型查（打得中），
	// 机体走路、镜头、AI 视线（按通道或只查 WorldStatic）都当它不存在
	HitBox = CreateDefaultSubobject<USphereComponent>(TEXT("HitBox"));
	RootComponent = HitBox;
	HitBox->InitSphereRadius(40.f);
	HitBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitBox->SetCollisionObjectType(ECC_WorldDynamic);
	HitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitBox->SetGenerateOverlapEvents(false);
	// 战斗组件的子弹打中带这个标签的东西时，把伤害交给它的 TakeDamage
	Tags.Add(TEXT("MechShootable"));

	// 雷身：占位用烟雾弹 / EMP 弹的弹罐（SM_Grenade，带队伍色发光槽），躺在地上；以后换成扁圆的地雷模型（见设计文档的素材清单）
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(HitBox);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(true);
	Body->bAffectDistanceFieldLighting = false;
	Body->bAffectDynamicIndirectLighting = false;
	// 模型原点在底面中心；落地时根（HitBox）停在地面上方 6 厘米（ImpactPoint + Normal * 6）
	Body->SetRelativeLocation(FVector(0.f, 0.f, -6.f));
	if (UStaticMesh* MineMesh = MechMineLocal::Load<UStaticMesh>(TEXT("/Game/Weapons/Meshes/SM_Mine")))
	{
		Body->SetStaticMesh(MineMesh);
	}

	// 顶上的小灯：引擎小球 + 充能步枪光束那个叠加材质（不受光、自带白天曝光补偿），白天也看得见一闪一闪
	SphereMesh = MechMineLocal::Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere"));
	Beacon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beacon"));
	Beacon->SetupAttachment(HitBox);
	Beacon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beacon->SetCastShadow(false);
	Beacon->bAffectDistanceFieldLighting = false;
	Beacon->bAffectDynamicIndirectLighting = false;
	Beacon->SetRelativeLocation(FVector(0.f, 0.f, 8.f));
	Beacon->SetRelativeScale3D(FVector(0.1f));
	if (SphereMesh)
	{
		Beacon->SetStaticMesh(SphereMesh);
	}

	// 夜里照亮一小圈地面（白天主要靠小灯本身）
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(HitBox);
	Light->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
	Light->SetCastShadows(false);
	Light->SetIntensity(0.f);
	Light->SetAttenuationRadius(350.f);

	BeaconMaterial = MechMineLocal::Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_MechBeam"));
	// 范围圈：一圈细圆柱，和小灯同一个光束材质（见 MechRangeRing）
	CylinderMesh = MechMineLocal::Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder"));
	RingMaterial = BeaconMaterial;
	BlastFX = MechMineLocal::Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_05/Particles/Ex_03_Explosion"));
	SparkFX = MechMineLocal::Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"));
	BeepSound = MechMineLocal::Load<USoundBase>(TEXT("/Game/Audio/Combat/S_lock_beep"));
	BlastSound = MechMineLocal::Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_artillery_boom"));
	BreakSound = MechMineLocal::Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_net_break"));
	Attenuation = MechMineLocal::Load<USoundAttenuation>(TEXT("/Game/Audio/SA_Combat"));
}

void AMechMine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMechMine, OwnerMech, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechMine, Team, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechMine, LaunchPos, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechMine, LaunchVel, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechMine, LaunchTime, COND_InitialOnly);
	DOREPLIFETIME(AMechMine, Phase);
	DOREPLIFETIME(AMechMine, PhaseTime);
	DOREPLIFETIME(AMechMine, RestPos);
	DOREPLIFETIME(AMechMine, RestNormal);
}

void AMechMine::Init(AMechCharacter* InOwner, const FVector& Origin, const FVector& Velocity)
{
	OwnerMech = InOwner;
	Team = InOwner ? InOwner->GetTeam() : 0;
	LaunchPos = Origin;
	LaunchVel = Velocity;
	LaunchTime = ServerNow();
}

void AMechMine::GetEffectSystems(TArray<UNiagaraSystem*>& Out) const
{
	Out.AddUnique(BlastFX);
	Out.AddUnique(SparkFX);
}

float AMechMine::ServerNow() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

FVector AMechMine::PosAt(float T) const
{
	return LaunchPos + LaunchVel * T - FVector(0.f, 0.f, 0.5f * Gravity * T * T);
}

FVector AMechMine::VelAt(float T) const
{
	return LaunchVel - FVector(0.f, 0.f, Gravity * T);
}

bool AMechMine::IsArmed() const
{
	return Phase == EMechMinePhase::Triggered || (Phase == EMechMinePhase::Landed && ServerNow() - PhaseTime >= ArmTime);
}

void AMechMine::BeginPlay()
{
	Super::BeginPlay();
	const FLinearColor TeamColor = AMechCharacter::TeamGlowColor(Team);
	// 雷身的发光槽和小灯都是队伍色：远处一眼分得清是谁的雷
	const int32 Slot = Body->GetMaterialIndex(TEXT("MI_Robot_Glow"));
	if (Slot != INDEX_NONE)
	{
		BodyGlowMID = Body->CreateAndSetMaterialInstanceDynamic(Slot);
		if (BodyGlowMID)
		{
			BodyGlowMID->SetVectorParameterValue(TEXT("Emissive"), TeamColor);
		}
	}
	if (BeaconMaterial)
	{
		BeaconMID = UMaterialInstanceDynamic::Create(BeaconMaterial, this);
		BeaconMID->SetVectorParameterValue(TEXT("Color"), TeamColor);
		BeaconMID->SetVectorParameterValue(TEXT("Emissive"), TeamColor);
		Beacon->SetMaterial(0, BeaconMID);
	}
	Light->SetLightColor(TeamColor);
	UMaterialInstanceDynamic* MID = nullptr;
	Ring = MechRangeRing::Create(this, TEXT("MineRing"), CylinderMesh, RingMaterial, MID);
	RingMID = MID;
	// 客户端：雷进视野时可能已经落地 / 已经在倒计时
	PrevT = FMath::Max(0.f, ServerNow() - LaunchTime);
	if (Phase == EMechMinePhase::Flying)
	{
		SetActorLocation(PosAt(PrevT));
	}
	else
	{
		OnRep_Phase();
	}
}

// ---------------------------------------------------------------- 每帧：飞、落地、待命、触发、炸
void AMechMine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = ServerNow();
	if (Phase == EMechMinePhase::Flying)
	{
		const float T = FMath::Max(0.f, Now - LaunchTime);
		const FVector Prev = PosAt(PrevT);
		const FVector Pos = PosAt(T);
		PrevT = T;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MechMine), false, this);
		if (OwnerMech)
		{
			Params.AddIgnoredActor(OwnerMech);
		}
		FHitResult Hit;
		const bool bHit = GetWorld()->LineTraceSingleByObjectType(Hit, Prev, Pos, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
		if (HasAuthority())
		{
			if (bHit)
			{
				if (Hit.ImpactNormal.Z >= 0.6f)
				{
					Land(Hit.ImpactPoint, Hit.ImpactNormal);
				}
				else
				{
					// 打在墙上：顺着墙掉到下面的地上（找不到地 = 扔出场地了，哑掉）
					FHitResult Floor;
					const FVector From = Hit.ImpactPoint + Hit.ImpactNormal * 30.f;
					if (GetWorld()->LineTraceSingleByObjectType(Floor, From, From - FVector(0.f, 0.f, 3000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params) &&
						Floor.ImpactNormal.Z >= 0.5f)
					{
						Land(Floor.ImpactPoint, Floor.ImpactNormal);
					}
					else
					{
						Break(false);
					}
				}
				return;
			}
			if (T > 3.f)
			{
				Break(false);  // 掉进虚空 / 飞出场地
				return;
			}
		}
		else if (bHit && !bVisualStuck)
		{
			// 客户端：画面上先停在碰到的地方，等服务器发来的落地位置（不然会先穿进地里再跳回来）
			bVisualStuck = true;
			SetActorLocation(Hit.ImpactPoint + Hit.ImpactNormal * 6.f);
		}
		if (!bVisualStuck)
		{
			SetActorLocationAndRotation(Pos, FRotator(T * 400.f, T * 250.f, 0.f));  // 边飞边翻滚
		}
	}
	else if (HasAuthority() && Phase != EMechMinePhase::Done)
	{
		// 主人不在了（换回合时旧机体被删）、回合没在进行、放太久了：哑掉（不炸）
		const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
		const bool bRoundOn = !GS || GS->Phase == EMechRoundPhase::Waiting || GS->CanAct();
		if (!IsValid(OwnerMech) || !bRoundOn || (Phase == EMechMinePhase::Landed && Now - PhaseTime > ArmTime + LifeTime))
		{
			Break(false);
			return;
		}
		if (Phase == EMechMinePhase::Landed && IsArmed())
		{
			for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
			{
				const AMechCharacter* M = *It;
				if (M->GetTeam() == Team || M->GetHealth()->IsDead())
				{
					continue;
				}
				if (DistToMech(M, RestPos) < TriggerRadius && HasLineTo(M, RestPos))
				{
					Phase = EMechMinePhase::Triggered;
					PhaseTime = Now;
					OnRep_Phase();
					UE_LOG(LogTemp, Display, TEXT("[tactical] 地雷（%s 的）被 %s 触发"), OwnerMech ? *OwnerMech->DisplayName : TEXT("?"), *M->DisplayName);
					break;
				}
			}
		}
		else if (Phase == EMechMinePhase::Triggered && Now - PhaseTime >= FuseTime)
		{
			Explode();
		}
	}
	UpdateVisuals();
}

void AMechMine::Land(const FVector& At, const FVector& Normal)
{
	RestPos = At + Normal.GetSafeNormal() * 4.f;
	RestNormal = Normal.GetSafeNormal();
	Phase = EMechMinePhase::Landed;
	PhaseTime = ServerNow();
	OnRep_Phase();
}

void AMechMine::OnRep_Phase()
{
	if (Phase == EMechMinePhase::Landed || Phase == EMechMinePhase::Triggered)
	{
		// 贴在地面上：雷的 Z 轴沿地面法线
		const FVector N = RestNormal.IsNearlyZero() ? FVector::UpVector : FVector(RestNormal);
		SetActorLocationAndRotation(FVector(RestPos), FRotationMatrix::MakeFromZ(N).Rotator());
		bVisualStuck = true;
	}
	else if (Phase == EMechMinePhase::Done)
	{
		Body->SetVisibility(false);
		Beacon->SetVisibility(false);
		Light->SetIntensity(0.f);
		HitBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

float AMechMine::DistToMech(const AMechCharacter* M, const FVector& P)
{
	// 到机体胶囊中线的距离再减半径（和 EMP / 炮弹的算法一样）
	const UCapsuleComponent* Cap = M->GetCapsuleComponent();
	const float Half = Cap->GetScaledCapsuleHalfHeight();
	const FVector C = MechLoop::Near(M->GetWorld(), M->GetActorLocation(), P);  // 老街循环
	return FMath::Max(0.f, FVector::Dist(P, FMath::ClosestPointOnSegment(P, C - FVector(0.f, 0.f, Half), C + FVector(0.f, 0.f, Half))) - Cap->GetScaledCapsuleRadius());
}

bool AMechMine::HasLineTo(const AMechCharacter* M, const FVector& From) const
{
	// 隔着墙不触发、不伤害（从雷上方一点看机体中心）
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechMineSight), false, this);
	Params.AddIgnoredActor(M);
	FHitResult Hit;
	const FVector Start = From + FVector(RestNormal) * 30.f;
	return !GetWorld()->LineTraceSingleByObjectType(Hit, Start, MechLoop::Near(GetWorld(), M->GetActorLocation(), Start), FCollisionObjectQueryParams(ECC_WorldStatic), Params);
}

// ---------------------------------------------------------------- 服务器：炸 / 碎
void AMechMine::Explode()
{
	if (!HasAuthority() || Phase == EMechMinePhase::Done)
	{
		return;
	}
	const FVector At = RestPos;
	Phase = EMechMinePhase::Done;
	PhaseTime = ServerNow();
	OnRep_Phase();
	MulticastExplode(At);
	int32 Hits = 0;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		AMechCharacter* M = *It;
		if (M->GetTeam() == Team || M->GetHealth()->IsDead())
		{
			continue;
		}
		const float D = DistToMech(M, At);
		if (D >= BlastRadius || !HasLineTo(M, At))
		{
			continue;
		}
		// 中心满额，边上四成；冲击跟着衰减（击退方向 = 从雷往外，机体受击时按受击点自己推）
		const float Fall = FMath::Lerp(1.f, 0.4f, D / BlastRadius);
		const float Dealt = M->ReceiveHit(BlastDamage * Fall, BlastImpact * Fall, OwnerMech, At, EMechHitKind::Missile);
		if (OwnerMech && Dealt > 0.f)
		{
			OwnerMech->GetCombat()->ConfirmHit(M, false, EMechHitKind::Missile, Dealt, At);
		}
		++Hits;
	}
	SetLifeSpan(1.f);  // 爆炸的范围圈要放 0.5 秒（客户端收到消息还要晚一点），之后删掉
	UE_LOG(LogTemp, Display, TEXT("[tactical] 地雷（%s 的）爆炸，炸到 %d 台"), OwnerMech ? *OwnerMech->DisplayName : TEXT("?"), Hits);
}

void AMechMine::Break(bool bEffects)
{
	if (!HasAuthority() || Phase == EMechMinePhase::Done)
	{
		return;
	}
	const FVector At = GetActorLocation();
	Phase = EMechMinePhase::Done;
	PhaseTime = ServerNow();
	OnRep_Phase();
	if (bEffects)
	{
		MulticastBreak(At);
	}
	SetLifeSpan(0.5f);
	UE_LOG(LogTemp, Display, TEXT("[tactical] 地雷（%s 的）%s"), OwnerMech ? *OwnerMech->DisplayName : TEXT("?"), bEffects ? TEXT("被打掉") : TEXT("哑掉（超时 / 主人不在 / 回合结束）"));
}

float AMechMine::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || Phase == EMechMinePhase::Done || DamageAmount <= 0.f)
	{
		return 0.f;
	}
	const AMechCharacter* By = Cast<AMechCharacter>(DamageCauser);
	if (!By && EventInstigator)
	{
		By = Cast<AMechCharacter>(EventInstigator->GetPawn());
	}
	if (By && By->GetTeam() == Team)
	{
		return 0.f;  // 自己人的子弹打不掉（会被它挡一下）
	}
	HP -= DamageAmount;
	if (HP <= 0.f)
	{
		Break(true);
	}
	return DamageAmount;
}

void AMechMine::MulticastExplode_Implementation(FVector_NetQuantize At)
{
	Body->SetVisibility(false);
	Beacon->SetVisibility(false);
	Light->SetIntensity(0.f);
	BlastStart = GetWorld()->GetTimeSeconds();
	if (BlastFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BlastFX, At, FRotator::ZeroRotator, FVector(0.85f), true, true, ENCPoolMethod::AutoRelease);
	}
	if (BlastSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, BlastSound, At, FRotator::ZeroRotator, 0.9f, 1.15f, 0.f, Attenuation);
	}
	// 本机玩家离得近：镜头冲一下
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (AMechCharacter* Me = Cast<AMechCharacter>(PC->GetPawn()))
		{
			const float D = FVector::Dist(Me->GetActorLocation(), FVector(At));
			if (D < 2000.f)
			{
				Me->CameraKick((Me->GetActorLocation() - FVector(At)).GetSafeNormal(), (1.f - D / 2000.f) * 0.5f);
			}
		}
	}
}

void AMechMine::MulticastBreak_Implementation(FVector_NetQuantize At)
{
	Body->SetVisibility(false);
	Beacon->SetVisibility(false);
	Light->SetIntensity(0.f);
	if (SparkFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SparkFX, At, FRotator(90.f, 0.f, 0.f), FVector(0.6f), true, true, ENCPoolMethod::AutoRelease);
	}
	if (BreakSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, BreakSound, At, FRotator::ZeroRotator, 0.6f, 1.3f, 0.f, Attenuation);
	}
}

// ---------------------------------------------------------------- 画面：灯、嘀嘀声、范围圈（各端）
void AMechMine::UpdateVisuals()
{
	const float Now = ServerNow();
	const float Local = GetWorld()->GetTimeSeconds();
	const float Age = Now - PhaseTime;
	// 灯的亮度 0–1：飞行中常亮；落地没待命慢闪；待命每 1.2 秒短促一闪；倒计时快闪
	float Blink = 0.f;
	switch (Phase)
	{
	case EMechMinePhase::Flying:
		Blink = 0.6f;
		break;
	case EMechMinePhase::Landed:
		if (Age < ArmTime)
		{
			Blink = FMath::Fmod(Age, 0.5f) < 0.25f ? 0.7f : 0.15f;
		}
		else
		{
			Blink = FMath::Fmod(Age - ArmTime, 1.2f) < 0.12f ? 1.f : 0.2f;
			if (!bArmBeeped && BeepSound)
			{
				// 待命那一下轻轻「嘀」一声（附近的人听得到：这里有雷）
				bArmBeeped = true;
				UGameplayStatics::PlaySoundAtLocation(this, BeepSound, GetActorLocation(), FRotator::ZeroRotator, 0.35f, 0.8f, 0.f, Attenuation);
			}
		}
		break;
	case EMechMinePhase::Triggered:
		Blink = FMath::Fmod(Age, 0.1f) < 0.05f ? 1.f : 0.3f;
		if (BeepSound && Local - LastBeep > 0.11f)
		{
			LastBeep = Local;
			UGameplayStatics::PlaySoundAtLocation(this, BeepSound, GetActorLocation(), FRotator::ZeroRotator, 0.8f, 1.6f, 0.f, Attenuation);
		}
		break;
	default:
		break;
	}
	if (Phase != EMechMinePhase::Done)
	{
		const FLinearColor TeamColor = AMechCharacter::TeamGlowColor(Team);
		if (BeaconMID)
		{
			BeaconMID->SetScalarParameterValue(TEXT("Intensity"), 0.4f + Blink * 2.6f);
			BeaconMID->SetScalarParameterValue(TEXT("EmissiveStrength"), (0.4f + Blink * 2.6f) * 3.f);
		}
		if (BodyGlowMID)
		{
			BodyGlowMID->SetVectorParameterValue(TEXT("Emissive"), TeamColor * (0.3f + Blink * 3.f));
		}
		Light->SetIntensity(Blink * 900.f);
	}
	// 引爆倒计时：地上亮出 5 米爆炸范围的虚线圈（跟着灯一闪一闪，看到就该闪出去）；炸开后：一道实线圈从雷 0.25 秒扩到 5 米再淡掉
	if (Ring)
	{
		const float SinceBlast = Local - BlastStart;
		if (Phase == EMechMinePhase::Triggered)
		{
			MechRangeRing::Update(Ring, RingMID, FVector(RestPos), BlastRadius, 1.2f + 1.8f * Blink, MechMineLocal::RingColor, true);
		}
		else if (BlastStart >= 0.f && SinceBlast < 0.5f)
		{
			const float Fade = 1.f - FMath::Clamp((SinceBlast - 0.25f) / 0.25f, 0.f, 1.f);
			MechRangeRing::Update(Ring, RingMID, FVector(RestPos), BlastRadius * (1.f - FMath::Exp(-SinceBlast * 14.f)), 3.f * Fade, MechMineLocal::RingColor, false);
		}
		else
		{
			MechRangeRing::Hide(Ring);
		}
	}
}

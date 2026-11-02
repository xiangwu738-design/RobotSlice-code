#include "MechMapRules.h"
#include "MechCombatComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MechCharacter.h"
#include "MechHealthComponent.h"
#include "MechMeleeFx.h"
#include "MechMeshComponent.h"
#include "MechMissile.h"
#include "MechRun.h"
#include "MechScore.h"
#include "MechShoulderComponent.h"
#include "MechSmoke.h"
#include "MechMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "MechEffectComponent.h"
#include "MechLoadout.h"

namespace
{
	// 武器模型里的关键点（模型坐标，厘米，未缩放；枪口在 MechWeapons 里各把自己的）
	const FVector BladeTrailAt(75.f, 0.f, 0.f);  // 刀光挂在刀身中段
	const FVector PodTubes[4] = {FVector(26.f, -7.f, -6.f), FVector(26.f, 7.f, 6.f), FVector(26.f, 7.f, -6.f), FVector(26.f, -7.f, 6.f)};
	// 近战招式（双手刀的基准，其他刀按 MechWeapons 的快慢、伤害、冲击倍数换算）：
	// 出招后多久判定命中、从什么时候起可以接下一招、整招多长、伤害、冲击、往前送的速度（第一下轻击和重斩按距离冲向目标）
	struct FMeleeSpec
	{
		float StrikeAt;
		float Cancel;
		float Duration;
		float Damage;
		float Impact;
		float Step;
	};
	const FMeleeSpec MeleeSpecs[6] = {
		{0.f, 0.f, 0.f, 0.f, 0.f, 0.f},                   // 无
		{0.24f, 0.36f, 0.62f, 380.f, 200.f, 600.f},       // 轻击一（带突进）
		{0.14f, 0.28f, 0.52f, 380.f, 220.f, 500.f},       // 轻击二
		{0.16f, 0.30f, 0.55f, 420.f, 260.f, 500.f},       // 轻击三
		{0.22f, 0.55f, 0.80f, 650.f, 700.f, 700.f},       // 轻击四：收尾一刀，冲击大
		{0.38f, 0.75f, 0.95f, 1400.f, 950.f, 900.f},      // 重斩
	};
	const FMeleeSpec& Spec(EMechMelee Move)
	{
		return MeleeSpecs[static_cast<int32>(Move)];
	}
	bool IsLight(EMechMelee Move)
	{
		return Move >= EMechMelee::Light1 && Move <= EMechMelee::Light4;
	}
	const float EngagedHold = 2.0f;      // 停火后还保持面朝瞄准方向多久
	const float KnifeLife = 0.8f;        // 飞刀飞多久（7500 × 0.8 = 60 米）
	const float ThrowInterval = 0.3f;
	const float KnifeCharge = 0.4f;      // 投掷匕首：按住左键走满一圈（秒）投一把
	const float KnifeTurn = 240.f;       // 匕首追踪：每秒最多往目标拐多少度
	const float PowerKnifeTurn = 360.f;
	const float PowerKnifeScale = 3.f;   // 强力匕首：伤害、冲击倍数；飞得快 30%、画大一号
	// 出招标志（服务器定、随出招组播）：单手刀闪避斩、双手刀弹反后的加成一刀
	constexpr uint8 MoveFlagDash = 1;
	constexpr uint8 MoveFlagParry = 2;
	// 格挡反馈的种类
	constexpr uint8 BlockNormal = 0;
	constexpr uint8 BlockParry = 1;
	constexpr uint8 BlockBreak = 2;
	// 拼刀组播的标志：这台要被弹开、这一份负责放场面上的特效和声音（两台各发一次，场面只放一份）
	constexpr uint8 ClashFlagKnocked = 1;
	constexpr uint8 ClashFlagFx = 2;
	const float BeamShow = 0.3f;         // 充能步枪的光束留多久
	const float Lunge3DHeight = 250.f;   // 目标比自己高 / 低这么多（厘米）才按三维方向突进，平地上还是水平冲（不跳起来）

	/** 身体各段（和 AMechCharacter::ClassifyHit 同一套骨段胶囊，半径是模板人形尺寸，乘机体缩放）：狙击开镜吸附找离射线最近的点用 */
	struct FBodySeg
	{
		const TCHAR* A;
		const TCHAR* B;  // 空 = 头（从头骨往上一段）
		float Radius;
	};
	const FBodySeg BodySegs[] = {
		{TEXT("pelvis"), TEXT("spine_03"), 28.f}, {TEXT("spine_03"), TEXT("spine_05"), 30.f}, {TEXT("spine_05"), TEXT("neck_01"), 11.f},
		{TEXT("head"), nullptr, 15.f},
		{TEXT("upperarm_l"), TEXT("lowerarm_l"), 9.f}, {TEXT("lowerarm_l"), TEXT("hand_l"), 8.f},
		{TEXT("upperarm_r"), TEXT("lowerarm_r"), 9.f}, {TEXT("lowerarm_r"), TEXT("hand_r"), 8.f},
		{TEXT("thigh_l"), TEXT("calf_l"), 13.f}, {TEXT("calf_l"), TEXT("foot_l"), 11.f},
		{TEXT("thigh_r"), TEXT("calf_r"), 13.f}, {TEXT("calf_r"), TEXT("foot_r"), 11.f},
	};

	/** 各把枪的枪声音高：霰弹、狙击低沉，冲锋枪、手枪清脆 */
	float GunPitch(EMechShooter Gun)
	{
		switch (Gun)
		{
		case EMechShooter::SMG:
			return 1.25f;
		case EMechShooter::Shotgun:
			return 0.72f;
		case EMechShooter::Marksman:
			return 0.85f;
		case EMechShooter::Sniper:
			return 0.58f;
		case EMechShooter::DualPistol:
			return 1.15f;
		default:
			return 1.f;
		}
	}

	template <typename T>
	T* Load(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<T> Finder(Path);
		return Finder.Object;
	}

	/** 放枪口焰 Ex_03_Muzzle（开枪、充能步枪光束、导弹出膛都用它）。
	 *  这个特效（AAAVFX 课程 4 的命中扫描枪）里有一条「动态光束」发射器，终点是用户参数 Target（世界坐标），课程里由开枪代码设成命中点；
	 *  我们以前没设，默认 (0,0,0)：每开一枪都从枪口拉一条细光线到世界原点。测试场出生点就在原点，这条线正好在眼前、跟着机体晃
	 *  （10-30 玩家：「子弹光线拉出来……只在竞技场出现」；关掉动态模糊 / TSR / 曳光本身都还在，关掉粒子就没了）；
	 *  别的地图原点远在场地外，线又长又细，多半不在画面里。现在终点设在枪口前 1 厘米，光束缩成看不见的一点，火光、火花照旧。
	 *  -MechTracerOld 保留旧做法（只为对比截图） */
	void SpawnMuzzleFX(UObject* Context, UNiagaraSystem* System, const FVector& At, const FRotator& Rot, const FVector& Scale)
	{
		static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("MechTracerOld"));
		UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAtLocation(Context, System, At, Rot, Scale, true, bOld, ENCPoolMethod::AutoRelease);
		if (NC && !bOld)
		{
			NC->SetVariablePosition(TEXT("Target"), At + Rot.Vector());
			NC->Activate(true);
		}
	}
}

UMechCombatComponent::UMechCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(true);

	TracerMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder"));
	TracerMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/MI_Tracer"));
	MuzzleFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Muzzle"));
	HitMechFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"));
	HitWorldFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"));
	BladeTrailFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_01/Particles/Ex_01_Sword_Trail/Trail_01"));
	BladeHitFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_02_Exp_02"));
	MissileLaunchFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Muzzle"));
	KnifeMesh = Load<UStaticMesh>(TEXT("/Game/Weapons/Meshes/SM_Sword03"));
	for (const TCHAR* P : {TEXT("/Game/Audio/Combat/S_rifle_fire_a"), TEXT("/Game/Audio/Combat/S_rifle_fire_b"), TEXT("/Game/Audio/Combat/S_rifle_fire_c")})
	{
		RifleSounds.Add(Load<USoundBase>(P));
	}
	for (const TCHAR* P : {TEXT("/Game/Audio/Combat/S_hit_metal_1"), TEXT("/Game/Audio/Combat/S_hit_metal_2"), TEXT("/Game/Audio/Combat/S_hit_metal_3"),
		TEXT("/Game/Audio/Combat/S_hit_metal_4")})
	{
		MetalHitSounds.Add(Load<USoundBase>(P));
	}
	for (const TCHAR* P : {TEXT("/Game/Audio/Combat/S_blade_swing_1"), TEXT("/Game/Audio/Combat/S_blade_swing_2")})
	{
		SwingSounds.Add(Load<USoundBase>(P));
	}
	for (const TCHAR* P : {TEXT("/Game/Audio/Combat/S_blade_hit_1"), TEXT("/Game/Audio/Combat/S_blade_hit_2")})
	{
		BladeHitSounds.Add(Load<USoundBase>(P));
	}
	GroundHitSound = Load<USoundBase>(TEXT("/Game/Audio/Combat/S_hit_ground"));
	ThumpSound = Load<USoundBase>(TEXT("/Game/Audio/Combat/S_missile_thump"));
	ReloadSound = Load<USoundBase>(TEXT("/Game/Audio/Combat/S_rifle_reload"));
	MissileLaunchSound = Load<USoundBase>(TEXT("/Game/Audio/Combat/S_missile_launch"));
	Attenuation = Load<USoundAttenuation>(TEXT("/Game/Audio/SA_Combat"));
	// 格挡（刀打盾）、弹反（清脆的「叮」+ 长一点的金属余音）、破防（金属碎裂）：音效全集里挑的，见 tools/ue_import_combat_fx.py
	GuardBlockSound = Load<USoundBase>(TEXT("/Game/Audio/Combat/S_guard_block"));
	ParryTingSound = Load<USoundBase>(TEXT("/Game/Audio/Combat/S_parry_ting"));
	ParryRingSound = Load<USoundBase>(TEXT("/Game/Audio/Combat/S_parry_ring"));
	GuardBreakSound = Load<USoundBase>(TEXT("/Game/Audio/Combat/S_guard_break"));
	// 充能步枪：开始充能用火炮的上膛声（音调抬高），光束用火炮开炮声
	ChargeSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_artillery_ready"));
	BeamSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_artillery_fire"));
	HeavyHitSound = Load<USoundBase>(TEXT("/Game/Audio/Combat/S_explosion_small"));
	BeamMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_MechBeam"));
}

void UMechCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	Ammo = GetMagazine();
	Knives = GetBlade().Knives;
	AActor* Owner = GetOwner();

	// 曳光弹：一组拉长的发光圆柱，每帧按子弹位置重摆
	Tracers = NewObject<UInstancedStaticMeshComponent>(Owner, TEXT("Tracers"));
	Tracers->SetStaticMesh(TracerMesh);
	if (TracerMaterial)
	{
		Tracers->SetMaterial(0, TracerMaterial);
	}
	Tracers->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tracers->SetCastShadow(false);
	Tracers->SetUsingAbsoluteLocation(true);
	Tracers->SetUsingAbsoluteRotation(true);
	Tracers->SetUsingAbsoluteScale(true);
	Tracers->SetupAttachment(Owner->GetRootComponent());
	Tracers->SetHasPerInstancePrevTransforms(true);  // 上一帧位置自己给（= 这一帧），见 UpdateTracers
	Tracers->RegisterComponent();
	Tracers->SetWorldTransform(FTransform::Identity);

	// 飞刀：和曳光一样每帧按位置重摆，画成匕首模型
	KnifeMeshes = NewObject<UInstancedStaticMeshComponent>(Owner, TEXT("KnifeMeshes"));
	KnifeMeshes->SetStaticMesh(KnifeMesh);
	KnifeMeshes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	KnifeMeshes->SetCastShadow(false);
	KnifeMeshes->bAffectDistanceFieldLighting = false;
	KnifeMeshes->bAffectDynamicIndirectLighting = false;
	KnifeMeshes->SetUsingAbsoluteLocation(true);
	KnifeMeshes->SetUsingAbsoluteRotation(true);
	KnifeMeshes->SetUsingAbsoluteScale(true);
	KnifeMeshes->SetupAttachment(Owner->GetRootComponent());
	KnifeMeshes->SetHasPerInstancePrevTransforms(true);
	KnifeMeshes->RegisterComponent();
	KnifeMeshes->SetWorldTransform(FTransform::Identity);

	// 飞刀尾迹（10-31 近战特效）：曳光材质换个颜色，普通匕首冷白、强力匕首橙色
	for (int32 k = 0; k < 2; ++k)
	{
		UInstancedStaticMeshComponent* S = NewObject<UInstancedStaticMeshComponent>(Owner, k == 0 ? TEXT("KnifeStreaks") : TEXT("PowerStreaks"));
		S->SetStaticMesh(TracerMesh);
		if (TracerMaterial)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(TracerMaterial, this);
			MID->SetVectorParameterValue(TEXT("Emissive"), k == 0 ? FLinearColor(0.55f, 0.75f, 1.f) : FLinearColor(1.f, 0.45f, 0.12f));
			MID->SetScalarParameterValue(TEXT("EmissiveStrength"), k == 0 ? 40.f : 80.f);
			S->SetMaterial(0, MID);
		}
		S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		S->SetCastShadow(false);
		S->SetUsingAbsoluteLocation(true);
		S->SetUsingAbsoluteRotation(true);
		S->SetUsingAbsoluteScale(true);
		S->SetupAttachment(Owner->GetRootComponent());
		S->SetHasPerInstancePrevTransforms(true);
		S->RegisterComponent();
		S->SetWorldTransform(FTransform::Identity);
		(k == 0 ? KnifeStreaks : PowerStreaks) = S;
	}

	// 近战特效（10-31）：只管画面，专用服务器不建；-MechMeleeFxOld 用以前的粒子刀光（对比用）
	static const bool bMeleeFxOld = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeFxOld"));
	if (!bMeleeFxOld && GetNetMode() != NM_DedicatedServer)
	{
		MeleeFx = NewObject<UMechMeleeFx>(Owner, TEXT("MeleeFx"));
		MeleeFx->RegisterComponent();
	}

	MuzzleLight = NewObject<UPointLightComponent>(Owner, TEXT("MuzzleLight"));
	MuzzleLight->SetLightColor(FLinearColor(1.f, 0.62f, 0.3f));
	MuzzleLight->SetAttenuationRadius(900.f);
	MuzzleLight->SetCastShadows(false);
	MuzzleLight->SetIntensity(0.f);
	MuzzleLight->SetUsingAbsoluteLocation(true);
	MuzzleLight->SetupAttachment(Owner->GetRootComponent());
	MuzzleLight->RegisterComponent();
}

void UMechCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, Ammo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, ReloadTimer, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, MissileTimer, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, HeavyTimer, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, Heat, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, OverheatTimer, COND_OwnerOnly);
	DOREPLIFETIME(UMechCombatComponent, Charge);  // 充能激光所有人都看得见
	DOREPLIFETIME_CONDITION(UMechCombatComponent, Streak, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, Knives, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, KnifeStreak, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, KnifeTimer, COND_OwnerOnly);
	DOREPLIFETIME(UMechCombatComponent, Stance);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, bAiming, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, bGuarding, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, Target, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, RepAimPoint, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UMechCombatComponent, RepLockThreat, COND_SkipOwner);  // 被锁的人那一端响告警（自己用不着）
	DOREPLIFETIME(UMechCombatComponent, RunShotFlags);
	DOREPLIFETIME(UMechCombatComponent, RunChargeHold);
}

void UMechCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Bullets.Reset();
	Super::EndPlay(EndPlayReason);
}

AMechCharacter* UMechCombatComponent::GetMech() const
{
	return Cast<AMechCharacter>(GetOwner());
}

bool UMechCombatComponent::CanAct() const
{
	const AMechCharacter* Mech = GetMech();
	return Mech && Mech->CanAct() && !Mech->GetHealth()->IsStaggered() && !Mech->IsNetted();
}

bool UMechCombatComponent::IsLocallyControlled() const
{
	const AMechCharacter* Mech = GetMech();
	return Mech && Mech->IsLocallyControlled();
}

float UMechCombatComponent::GetSwitchProgress() const
{
	return SwitchTime < 0.f ? -1.f : FMath::Clamp(SwitchTime / SwitchLen, 0.f, 1.f);
}

bool UMechCombatComponent::IsBladeInHand() const
{
	if (SwitchTime >= 0.f && SwitchTime < SwitchLen * 0.5f)
	{
		return SwitchFrom == EMechStance::Blade;
	}
	return Stance == EMechStance::Blade;
}

UMechCombatComponent::FMove UMechCombatComponent::CurrentMove(EMechMelee Move) const
{
	const FMeleeSpec& S = Spec(Move);
	const MechWeapons::FBlade& B = GetBlade();
	// 出招快慢再乘这一招的倍数（快刀连斩层数，出招时定下、组播给各端，动作对齐用同一个数）
	const float Time = B.TimeScale * (Move == MeleeMove ? MoveTimeScale : 1.f);
	const uint8 Flags = Move == MeleeMove ? MoveFlags : 0;
	FMove M;
	M.StrikeAt = S.StrikeAt * Time;
	M.Cancel = S.Cancel * Time;
	M.Duration = S.Duration * Time;
	M.Damage = S.Damage * B.DamageScale;
	M.Impact = S.Impact * B.ImpactScale;
	M.Step = S.Step;
	if (Move == MeleeMove && ClashCancelAt >= 0.f)
	{
		M.Cancel = FMath::Min(M.Cancel, ClashCancelAt);  // 11-01 拼刀：和匕首拼上没被弹开的一方，这一刀的后摇随时能取消（动作照放，不跳帧）
	}
	if (Flags & MoveFlagDash)
	{
		M.Damage *= MechWeapons::DashSlashDamage;  // 单手刀闪避斩
		M.Impact *= MechWeapons::DashSlashImpact;
	}
	return M;
}

float UMechCombatComponent::LungeRange(EMechMelee Move) const
{
	if (Move == MeleeMove && (MoveFlags & MoveFlagDash))
	{
		return MechWeapons::DashSlashRange;
	}
	return Move == EMechMelee::Heavy ? HeavyLungeRange : Move == EMechMelee::Light1 ? LightLungeRange : 700.f;
}

bool UMechCombatComponent::CanDashSlash() const
{
	return BladeType == EMechMeleeWeapon::OneHand && Stance == EMechStance::Blade && GetWorld()->GetTimeSeconds() - LastDodgeTime < MechWeapons::DashSlashWindow * OwnScale(MechLoadout::Melee, MechWeapons::OwnDashSlashWindow);
}

float UMechCombatComponent::OwnScale(int32 Category, float Scale) const
{
	const AMechCharacter* Mech = GetMech();
	return Mech && Mech->IsOwnMaker(Category) ? Scale : 1.f;
}

int32 UMechCombatComponent::BaseSalvo() const
{
	const AMechCharacter* Mech = GetMech();
	return MissilesPerSalvo + (Mech && Mech->IsOwnMaker(MechLoadout::Shoulder) ? MechWeapons::OwnSalvoExtra : 0);
}

int32 UMechCombatComponent::SalvoSize() const
{
	return RunSalvo > 0 ? RunSalvo : BaseSalvo();
}

bool UMechCombatComponent::IsSuperArmor() const
{
	if (RunChargeHold == 1)
	{
		return false;  // 11-01 肉鸽 蓄力 1：按住蓄的时候没有霸体（蓄力 2 才有），松手砍出去照大剑的霸体重斩
	}
	// 11-01 拼刀：按这一招本来的可取消时刻算，不看 ClashCancelAt（和匕首拼上后能提前取消，但没取消的话霸体照旧到原来的时刻）
	return BladeType == EMechMeleeWeapon::GreatSword && MeleeMove == EMechMelee::Heavy && MeleeTime < Spec(MeleeMove).Cancel * GetBlade().TimeScale * MoveTimeScale;
}

void UMechCombatComponent::InterruptMelee()
{
	if (GetOwner()->HasAuthority() && MeleeMove != EMechMelee::None && !IsSuperArmor())
	{
		MulticastInterrupt();
	}
}

void UMechCombatComponent::MulticastInterrupt_Implementation()
{
	EndMelee();
	ComboWindow = 0.f;
}

float UMechCombatComponent::GetMeleeStrikeAt() const
{
	return CurrentMove(MeleeMove).StrikeAt;
}

float UMechCombatComponent::GetMeleeCancel() const
{
	return CurrentMove(MeleeMove).Cancel;
}

float UMechCombatComponent::GetMeleeDuration() const
{
	return CurrentMove(MeleeMove).Duration;
}

float UMechCombatComponent::GetKnifeProgress() const
{
	const MechWeapons::FBlade& B = GetBlade();
	return Knives >= B.Knives || B.KnifeRegen <= 0.f ? 1.f : 1.f - KnifeTimer / (B.KnifeRegen * OwnScale(MechLoadout::Melee, MechWeapons::OwnKnifeRegen));
}

int32 UMechCombatComponent::GetComboStep() const
{
	return IsLight(MeleeMove) ? static_cast<int32>(MeleeMove) - static_cast<int32>(EMechMelee::Light1) + 1 : 0;
}

float UMechCombatComponent::GetMeleeHoldFraction() const
{
	return bMeleeHeld && !bHeavyFired ? FMath::Clamp(MeleeHold / (GetBlade().Knives > 0 ? KnifeCharge : HeavyHold), 0.f, 1.f) : 0.f;
}

bool UMechCombatComponent::IsEngaged() const
{
	const float Now = GetWorld()->GetTimeSeconds();
	return bTrigger || bCharging || Charge > 0.f || MeleeMove != EMechMelee::None || bAiming || bGuarding || PendingMissiles > 0 || Now - LastEngagedTime < EngagedHold;
}

void UMechCombatComponent::SetForcedTarget(AMechCharacter* InTarget)
{
	bForcedTarget = true;
	Target = InTarget;
}

// ---------------------------------------------------------------- 操作入口
void UMechCombatComponent::SetTrigger(bool bHeld)
{
	bTrigger = bHeld;  // 客户端在 Tick 里和服务器对齐
}

void UMechCombatComponent::ServerSetTrigger_Implementation(bool bHeld)
{
	bTrigger = bHeld;
}

void UMechCombatComponent::SetWeapons(EMechShooter InGun, EMechMeleeWeapon InBlade)
{
	const bool bGunChanged = InGun != GunType;
	const bool bBladeChanged = InBlade != BladeType;
	GunType = InGun;
	BladeType = InBlade;
	if (GetOwner()->HasAuthority())
	{
		if (bGunChanged || !HasBegunPlay())
		{
			Ammo = GetMagazine();
			ReloadTimer = 0.f;
			Heat = 0.f;
			OverheatTimer = 0.f;
			Charge = 0.f;
			Streak = 0;
		}
		else if (GetMagazine() > 0)
		{
			Ammo = FMath::Min(Ammo, GetMagazine());  // 枪没换、框架换了（弹匣倍数变小）
		}
		if (bBladeChanged || !HasBegunPlay())
		{
			Knives = GetBlade().Knives;
			KnifeTimer = 0.f;
			HuajingHits = 0;
		}
	}
	bCharging = false;
	FireTimer = 0.f;
}

void UMechCombatComponent::OnQuickBoost()
{
	// 11-01 肉鸽 蓄力：蓄着的时候闪避 = 不蓄了（这一刀作废），不然按住左键就站着躲不了
	if (RunChargeHold != 0 && GetOwner()->HasAuthority())
	{
		bRunChargeWanted = false;
		RunChargeHold = 0;
		HeavyTimer = 0.f;
		MulticastInterrupt();
	}
	// 砍出去之后（后摇）快速闪避：直接收招，不用等动作放完
	if (MeleeMove != EMechMelee::None)
	{
		const FMove S = CurrentMove(MeleeMove);
		if (MeleeTime >= FMath::Min(S.Cancel, S.StrikeAt + 0.1f))
		{
			EndMelee();
		}
	}
	// 单手刀闪避斩：记下闪避时刻，之后一小段时间里出轻击就是远距离突进斩
	LastDodgeTime = GetWorld()->GetTimeSeconds();
	// 双持手枪闪避装满：10-30 晚玩家试玩后去掉（先是每次、后来 6 秒一次）改成重斩砍中补满；11-01 用户又要回来、不要冷却
	// （「手枪还是冲刺回弹，没有回弹cd」），重斩补满也留着。弹药以服务器为准（Ammo 复制）；各端都在这里收掉换弹、记下补满时间，
	// 自己这台的 HUD「装满」图形和上膛声不用等服务器
	const MechWeapons::FGun& G = GetGun();
	if (G.bDodgeRefill && G.Magazine > 0 && (Ammo < GetMagazine() || ReloadTimer > 0.f))
	{
		if (GetOwner()->HasAuthority())
		{
			Ammo = GetMagazine();
		}
		ReloadTimer = 0.f;
		LastRefillTime = GetWorld()->GetTimeSeconds();
		if (ReloadSound && GetMech() && GetMech()->IsLocallyControlled())
		{
			UGameplayStatics::PlaySoundAtLocation(this, ReloadSound, GetOwner()->GetActorLocation(), FRotator::ZeroRotator, 0.9f, 1.25f, 0.f, Attenuation);
		}
	}
}

void UMechCombatComponent::Reload()
{
	if (!GetOwner()->HasAuthority())
	{
		ServerReload();
		return;
	}
	const MechWeapons::FGun& G = GetGun();
	if (ReloadTimer <= 0.f && G.Magazine > 0 && Ammo < GetMagazine())
	{
		ReloadTimer = G.ReloadTime * (GetMech() ? GetMech()->EffectScale(EMechStat::ReloadTime) : 1.f) * (G.bDual ? OwnScale(MechLoadout::Shooter, MechWeapons::OwnPistolReload) : 1.f);
		Charge = 0.f;
		bCharging = false;
		MulticastReload();
	}
}

void UMechCombatComponent::ServerReload_Implementation()
{
	Reload();
}

void UMechCombatComponent::MulticastReload_Implementation()
{
	PlayAt(ReloadSound, GetMuzzle(), 0.8f);
}

void UMechCombatComponent::FireMissiles()
{
	if (!GetOwner()->HasAuthority())
	{
		ServerFireMissiles(Target, MissileLock >= 1.f);
		return;
	}
	if (MissileTimer > 0.f || PendingMissiles > 0 || !CanAct())
	{
		return;
	}
	RunSalvo = 0;
	PendingMissiles = SalvoSize();
	bSalvoLocked = MissileLock >= 1.f && Target != nullptr;
	if (UMechRunComponent* Run = GetMech() ? GetMech()->GetRun() : nullptr)
	{
		Run->OnFireMissiles(PendingMissiles, bSalvoLocked);  // 11-01 肉鸽：重弹一枚、蜂群锁杂兵 / 全锁
	}
	MissileTimer = MissileCooldown * (GetMech() && GetMech()->IsOwnMaker(MechLoadout::Shoulder) ? MechWeapons::OwnMissileCooldown : 1.f) * (GetMech() ? FMath::Min(1.f, GetMech()->EffectScale(EMechStat::MissileCooldown)) : 1.f);
	MissileSalvoTimer = 0.f;
	LastEngagedTime = GetWorld()->GetTimeSeconds();
}

void UMechCombatComponent::ServerFireMissiles_Implementation(AMechCharacter* InTarget, bool bLocked)
{
	Target = InTarget;
	MissileLock = bLocked ? 1.f : 0.f;
	FireMissiles();
}

// ---------------------------------------------------------------- 姿态切换、右键
void UMechCombatComponent::SwitchStance()
{
	bMeleeHeld = false;
	if (!GetOwner()->HasAuthority())
	{
		ServerSwitchStance();
		return;
	}
	if (SwitchTime >= 0.f || !CanAct())
	{
		return;
	}
	if (MeleeMove != EMechMelee::None)
	{
		// 砍出去之后（后摇）可以直接切枪打断，砍出去之前不行
		const FMove S = CurrentMove(MeleeMove);
		if (Stance != EMechStance::Blade || MeleeTime < FMath::Min(S.Cancel, S.StrikeAt + 0.1f))
		{
			return;
		}
	}
	if (Stance == EMechStance::Gun && GetMech() && GetMech()->IsCarryingCore())
	{
		return;  // 夺芯带芯：拿不了刀
	}
	bTrigger = false;
	MulticastStanceSwitch(Stance == EMechStance::Gun ? EMechStance::Blade : EMechStance::Gun);
}

void UMechCombatComponent::ServerSwitchStance_Implementation()
{
	SwitchStance();
}

void UMechCombatComponent::MulticastStanceSwitch_Implementation(EMechStance NewStance)
{
	if (MeleeMove != EMechMelee::None)
	{
		EndMelee();  // 切枪打断后摇
	}
	SwitchFrom = NewStance == EMechStance::Gun ? EMechStance::Blade : EMechStance::Gun;
	Stance = NewStance;
	SwitchTime = 0.f;
	SwitchLen = NewStance == EMechStance::Gun ? 0.32f : 0.45f;
	bAiming = bGuarding = false;
	if (SwingSounds.Num())
	{
		PlayAt(SwingSounds[0], GetOwner()->GetActorLocation(), 0.45f, 0.05f);
	}
}

void UMechCombatComponent::SetAltHeld(bool bHeld)
{
	bAltHeld = bHeld;
	if (!GetOwner()->HasAuthority() && IsLocallyControlled())
	{
		ServerSetAlt(bHeld);
	}
}

void UMechCombatComponent::ServerSetAlt_Implementation(bool bHeld)
{
	bAltHeld = bHeld;
}

void UMechCombatComponent::UpdateStance(float DeltaTime)
{
	if (SwitchTime >= 0.f)
	{
		SwitchTime += DeltaTime;
		if (SwitchTime >= SwitchLen)
		{
			SwitchTime = -1.f;
		}
	}
	GuardBreakTimer = FMath::Max(0.f, GuardBreakTimer - DeltaTime);
	if (!bAltHeld)
	{
		bNoParryUntilRelease = false;
	}
	// 服务器和操控者本地按同样的条件算瞄准 / 格挡（其他端用复制下来的）
	if (GetOwner()->HasAuthority() || IsLocallyControlled())
	{
		const bool bFree = CanAct() && SwitchTime < 0.f;
		bAiming = bAltHeld && bFree && Stance == EMechStance::Gun;
		// 投掷匕首不能格挡（10-30 用户改：右键改成近身挥刀，见 RequestDaggerSlash）
		bGuarding = bAltHeld && bFree && Stance == EMechStance::Blade && MeleeMove == EMechMelee::None && GuardBreakTimer <= 0.f
			&& GetBlade().Knives == 0;
	}
	if (bGuarding)
	{
		GuardTime += DeltaTime;
		SinceGuardEnd = 0.f;
	}
	else
	{
		GuardTime = 0.f;
		SinceGuardEnd += DeltaTime;
	}
	SinceBlock += DeltaTime;
}

// ---------------------------------------------------------------- 近战出招
void UMechCombatComponent::MeleePressed()
{
	bMeleeHeld = true;
	bHeavyFired = false;
	MeleeHold = 0.f;
	if (GetBlade().Knives > 0)
	{
		return;  // 投掷匕首：按住走满一圈才投（见 TickComponent），提前松手不投
	}
	RequestLight();  // 按下就出轻击（以前等松开才出，要先判断是不是按住重斩，慢一拍）；按住到 HeavyHold 再转重斩
}

void UMechCombatComponent::MeleeReleased()
{
	bMeleeHeld = false;
	MeleeHold = 0.f;
}

bool UMechCombatComponent::CanMelee() const
{
	return Stance == EMechStance::Blade && SwitchTime < 0.f && CanAct() && !bGuarding && !(GetMech() && GetMech()->IsCarryingCore());
}

void UMechCombatComponent::RequestLight()
{
	if (!GetOwner()->HasAuthority())
	{
		ServerRequestMelee(Target, false);
		return;
	}
	if (!CanMelee())
	{
		return;
	}
	if (GetBlade().Knives > 0)
	{
		ThrowKnivesAuthority(1);
		return;
	}
	LightCombo();
}

void UMechCombatComponent::LightCombo()
{
	if (MeleeMove == EMechMelee::None)
	{
		// 上一下刚收招就再按：接着连
		const bool bChain = ComboWindow > 0.f && LastLight >= EMechMelee::Light1 && LastLight < EMechMelee::Light4;
		StartMove(bChain ? static_cast<EMechMelee>(static_cast<uint8>(LastLight) + 1) : EMechMelee::Light1);
	}
	else if (MeleeMove >= EMechMelee::Light1 && MeleeMove < EMechMelee::Light4)
	{
		bQueuedLight = true;  // 到可取消时刻接下一下
	}
}

void UMechCombatComponent::RequestDaggerSlash()
{
	if (!GetOwner()->HasAuthority())
	{
		ServerRequestDaggerSlash(Target);
		return;
	}
	// 匕首的左键是按住投掷，右键（10-30 用户改）是近身挥刀：和别的刀一样的四段轻击连招（伤害、够得着的距离按匕首的表）
	if (GetBlade().Knives > 0 && CanMelee())
	{
		LightCombo();
	}
}

void UMechCombatComponent::ServerRequestDaggerSlash_Implementation(AMechCharacter* InTarget)
{
	Target = InTarget;
	RequestDaggerSlash();
}

void UMechCombatComponent::RequestHeavy()
{
	if (!GetOwner()->HasAuthority())
	{
		ServerRequestMelee(Target, true);
		return;
	}
	if (GetBlade().Knives > 0)
	{
		if (CanMelee())
		{
			ThrowKnivesAuthority(3);
		}
		return;
	}
	if (!CanMelee() || HeavyTimer > 0.f)
	{
		return;
	}
	// 空手、轻击还没砍到（直接收成重斩）、轻击过了可取消时刻都能马上出重斩；轻击已经砍到、还没到可取消时刻就排队
	const FMove Now = CurrentMove(MeleeMove);
	if (MeleeMove == EMechMelee::None || (IsLight(MeleeMove) && (MeleeTime < Now.StrikeAt || MeleeTime >= Now.Cancel)))
	{
		StartMove(EMechMelee::Heavy);
	}
	else if (IsLight(MeleeMove))
	{
		bQueuedHeavy = true;
	}
}

void UMechCombatComponent::ServerRequestMelee_Implementation(AMechCharacter* InTarget, bool bHeavy)
{
	Target = InTarget;
	if (bHeavy)
	{
		RequestHeavy();
	}
	else
	{
		RequestLight();
	}
}

void UMechCombatComponent::StartMove(EMechMelee Move)
{
	AMechCharacter* Mech = GetMech();
	const float Now = GetWorld()->GetTimeSeconds();
	// 这一招的标志和快慢（先定好再设 MeleeMove，CurrentMove / LungeRange 按它们算）：
	// 单手刀闪避斩 = 刚闪避过出的轻击；双手刀弹反后的下一刀带加成（出这一刀就用掉）；快刀按连斩层数出刀更快
	MoveFlags = 0;
	if (IsLight(Move) && CanDashSlash())
	{
		MoveFlags |= MoveFlagDash;
		LastDodgeTime = -100.f;  // 一次闪避只换一刀
	}
	MoveTimeScale = 1.f;
	if (BladeType == EMechMeleeWeapon::QuickBlade && Mech->GetEffects())
	{
		MoveTimeScale = 1.f - MechWeapons::FlurryHaste * FMath::Min(Mech->GetEffects()->GetStacks(TEXT("Flurry")), MechWeapons::FlurryMax);
	}
	MoveTimeScale = FMath::RoundToFloat(MoveTimeScale * 100.f) / 100.f;  // 和组播的百分比一致，各端动作对齐同一个数
	MeleeMove = Move;
	MeleeTime = 0.f;
	bMeleeHitDone = false;
	bSwingLanded = false;
	ClashCancelAt = -1.f;
	bQueuedLight = false;
	bQueuedHeavy = false;
	if (Move == EMechMelee::Heavy)
	{
		HeavyTimer = HeavyCooldown;
		// 11-01 肉鸽 蓄力：左键还按着，这一刀停在起手（见 UpdateMelee）
		if (bRunChargeWanted)
		{
			const UMechRunComponent* Run = Mech->GetRun();
			RunChargeHold = Run && Run->Active(EMechRunCard::Charge2) ? 2 : 1;
			RunChargeT0 = Now;
			RunChargeLevel = 0.f;
		}
	}
	else
	{
		LastLight = Move;
	}
	// 突进方向：突进距离内有目标就向着它（三维：目标在上方 / 下方明显高差时朝它冲上去 / 扑下去，见 UpdateMelee），没有就朝前
	LungeDir = Mech->GetActorForwardVector().GetSafeNormal2D();
	if (Target && !Target->GetHealth()->IsDead())
	{
		const FVector To = Target->GetActorLocation() - Mech->GetActorLocation();
		if (To.Size() < LungeRange(Move))
		{
			const bool b3D = FMath::Abs(To.Z) > Lunge3DHeight && (To.Z > 0.f || Mech->GetCharacterMovement()->IsFalling());
			LungeDir = b3D ? To.GetSafeNormal() : To.GetSafeNormal2D();
		}
	}
	LastEngagedTime = Now;
	MulticastMeleeStart(Move, LungeDir, static_cast<uint8>(FMath::RoundToInt(MoveTimeScale * 100.f)), MoveFlags);
}

void UMechCombatComponent::ServerSetAim_Implementation(AMechCharacter* InTarget, FVector_NetQuantize AimPoint)
{
	Target = InTarget;
	RemoteAimPoint = AimPoint;
}

void UMechCombatComponent::ClientHitConfirm_Implementation(bool bDirect, bool bKill, EMechHitKind Kind, float Damage, FVector_NetQuantize At, EMechHitPart Part)
{
	LastHitTime = GetWorld()->GetTimeSeconds();
	bLastHitDirect = bDirect;
	bLastHitKill = bKill;
	LastHitKind = Kind;
	AMechCharacter* Mech = GetMech();
	if (!Mech || !Mech->IsPlayerControlled() || !Mech->IsLocallyControlled())
	{
		return;
	}
	// 伤害飘字：同一处 0.12 秒内的（霰弹的一组弹丸、冲锋枪连发）合成一个数，打中过头就按头算
	if (Popups.Num() && LastHitTime - Popups.Last().Time < 0.12f && Popups.Last().Kind == Kind && FVector::Dist(Popups.Last().At, At) < 300.f)
	{
		FMechDamagePopup& P = Popups.Last();
		P.Damage += Damage;
		P.bDirect |= bDirect;
		P.bKill |= bKill;
		if (Part == EMechHitPart::Head || (Part == EMechHitPart::Torso && P.Part == EMechHitPart::Limb))
		{
			P.Part = Part;
		}
	}
	else
	{
		FMechDamagePopup& P = Popups.AddDefaulted_GetRef();
		P.At = At;
		P.Damage = Damage;
		P.Time = LastHitTime;
		P.Part = Part;
		P.Kind = Kind;
		P.bDirect = bDirect;
		P.bKill = bKill;
	}
	Popups.RemoveAll([this](const FMechDamagePopup& P) { return LastHitTime - P.Time > 1.3f; });
	// -MechHitShots：命中那一帧截图（子弹每 1.5 秒最多一张，导弹 / 刀 / 击毁每次都截），存到 Saved/Screenshots/Hits
	static const bool bHitShots = FParse::Param(FCommandLine::Get(), TEXT("MechHitShots"));
	static int32 HitShotIndex = 0;
	static double LastBulletShot = -100.0;
	if (bHitShots && (Kind != EMechHitKind::Bullet || bKill || FPlatformTime::Seconds() - LastBulletShot > 1.5))
	{
		if (Kind == EMechHitKind::Bullet)
		{
			LastBulletShot = FPlatformTime::Seconds();
		}
		static const TCHAR* KindNames[] = {TEXT("子弹"), TEXT("导弹"), TEXT("刀")};
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Hits") /
			FString::Printf(TEXT("%03d_打中_%s%s%s.png"), HitShotIndex++, KindNames[static_cast<int32>(Kind)], bDirect ? TEXT("_直击") : TEXT(""),
				bKill ? TEXT("_击毁") : TEXT("")), true, false);
	}
	// 命中确认音：子弹是清脆的「叮」（太密时一秒最多二十声），直击、导弹、刀加一声闷响，击毁最重
	const float Real = GetWorld()->GetRealTimeSeconds();
	if (MetalHitSounds.Num() && (Kind != EMechHitKind::Bullet || Real - ConfirmSoundTime > 0.05f))
	{
		ConfirmSoundTime = Real;
		UGameplayStatics::PlaySound2D(this, MetalHitSounds[FMath::RandHelper(MetalHitSounds.Num())], bDirect ? 0.3f : 0.13f,
			bDirect ? 1.25f : 1.7f + FMath::FRandRange(-0.08f, 0.08f));
	}
	if (ThumpSound && (bKill || bDirect || Kind != EMechHitKind::Bullet))
	{
		UGameplayStatics::PlaySound2D(this, ThumpSound, bKill ? 0.8f : 0.32f, bKill ? 0.7f : 1.15f);
	}
	if (bKill)
	{
		Mech->CameraKick(Mech->GetActorForwardVector(), 0.9f);
		// 单机：击毁瞬间整个世界慢放一下（联机时没法只让一个人慢放，就不做）
		if (GetNetMode() == NM_Standalone)
		{
			UGameplayStatics::SetGlobalTimeDilation(this, 0.25f);
			FTimerHandle Restore;
			GetWorld()->GetTimerManager().SetTimer(Restore, FTimerDelegate::CreateWeakLambda(this, [this]() {
				UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
			}), 0.11f, false);
		}
	}
}

void UMechCombatComponent::FlashLight(const FVector& At, float Intensity, const FLinearColor& Color)
{
	if (!ImpactLight)
	{
		ImpactLight = NewObject<UPointLightComponent>(GetOwner(), TEXT("ImpactLight"));
		ImpactLight->SetMobility(EComponentMobility::Movable);
		ImpactLight->SetCastShadows(false);
		ImpactLight->SetAttenuationRadius(1100.f);
		ImpactLight->SetSourceRadius(40.f);
		ImpactLight->RegisterComponent();
	}
	ImpactLight->SetWorldLocation(At);
	ImpactLight->SetLightColor(Color);
	ImpactLightLevel = FMath::Max(ImpactLightLevel, Intensity);
	ImpactLight->SetIntensity(ImpactLightLevel);
	ImpactLight->SetVisibility(true);
}

// ---------------------------------------------------------------- 每帧
void UMechCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AMechCharacter* Mech = GetMech();
	if (!Mech)
	{
		return;
	}
	if (ImpactLightLevel > 0.f)
	{
		ImpactLightLevel *= FMath::Exp(-DeltaTime * 28.f);
		if (ImpactLightLevel < 30.f)
		{
			ImpactLightLevel = 0.f;
			ImpactLight->SetVisibility(false);
		}
		else
		{
			ImpactLight->SetIntensity(ImpactLightLevel);
		}
	}
	if (Mech->GetHealth()->IsDead())
	{
		bTrigger = false;
		bAltHeld = false;
		PendingMissiles = 0;
		MeleeMove = EMechMelee::None;
		bCharging = false;
		Charge = 0.f;
		RunChargeHold = 0;
		bRunChargeWanted = false;
	}
	// 11-01 肉鸽 蓄力：松手（不管走的是哪条路，切枪之类清掉 bMeleeHeld 也算）告诉服务器砍出去
	if (bRunChargePressed && !bMeleeHeld)
	{
		bRunChargePressed = false;
		if (UMechRunComponent* Run = Mech->GetRun())
		{
			Run->ServerChargeEnd();
		}
	}
	// 左键按住过了 HeavyHold 就出重斩（本地操控的那一端判断）；投掷匕首是按住走满一圈投一把，一直按着就接着蓄下一把
	if (IsLocallyControlled() && bMeleeHeld && !bHeavyFired)
	{
		MeleeHold += DeltaTime;
		if (GetBlade().Knives > 0)
		{
			if (MeleeHold >= KnifeCharge && Knives > 0 && CanMelee())
			{
				MeleeHold = 0.f;
				RequestLight();
			}
			MeleeHold = FMath::Min(MeleeHold, KnifeCharge);  // 没匕首了停在满圈，回出一把马上投
		}
		else if (MeleeHold >= HeavyHold)
		{
			bHeavyFired = true;
			// 11-01 肉鸽 蓄力：有这张卡就不马上砍，按住一直蓄，松手才砍
			UMechRunComponent* Run = Mech->GetRun();
			if (Run && Run->WantsChargeHold())
			{
				bRunChargePressed = true;
				Run->ServerChargeBegin(Target);
			}
			else
			{
				RequestHeavy();
			}
		}
	}
	UpdateLock(DeltaTime);
	// 告警（10-31）：服务器把导弹锁定进度按 16 档复制出去，被锁的那一端响 RWR 式告警（UMechHudAlerts）。
	// 只算导弹就绪时的锁定：冷却中锁着也打不出来，算进去 AI 一直锁着人就一直响
	if (GetOwner()->HasAuthority())
	{
		const bool bArmed = Target && !Mech->GetHealth()->IsDead() && Mech->GetLoadout().Shoulder == EMechShoulder::Missiles && MissileTimer <= 0.f && PendingMissiles == 0;
		RepLockThreat = bArmed ? static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(MissileLock, 0.f, 1.f) * 15.f) * 17) : 0;
	}
	if (!GetOwner()->HasAuthority() && IsLocallyControlled() && bTrigger != bTriggerSent && GetOwner()->GetNetConnection())
	{
		ServerSetTrigger(bTrigger);
		bTriggerSent = bTrigger;
	}

	// 枪臂指向：玩家对着准星（子弹也打准星），AI 对着锁定的目标
	const FVector Muzzle = GetMuzzle();
	const bool bAimTarget = !Mech->IsPlayerControlled() && Target && !Target->GetHealth()->IsDead();
	const FVector AimPoint = bAimTarget ? Target->GetActorLocation() + FVector(0.f, 0.f, 30.f) : GetFreeAimPoint();
	AimDir = AimOverride.IsNearlyZero() ? (AimPoint - Muzzle).GetSafeNormal() : AimOverride.GetSafeNormal();
	// 摆姿势用的瞄准点：枪管要指着子弹真正去的地方（开火时辅助瞄准会把子弹拉向目标，枪管不跟着就像子弹从枪侧面歪出去）。
	// 方向从胸口算、不从枪口算：枪口跟着上身转，从枪口算的方向又去转上身，贴着墙时一直来回抖。
	// 本机操控的一端和服务器各自算；别人的机体在客户端上没有控制器（控制朝向是 0），以前枪一直朝世界 +X 拧——改用服务器同步来的点
	const FVector Chest = GetPoseAimPivot();
	if (!AimOverride.IsNearlyZero())
	{
		PoseAimPoint = Chest + AimOverride.GetSafeNormal() * 20000.f;
	}
	else if (Mech->IsLocallyControlled() || GetOwner()->HasAuthority())
	{
		const MechWeapons::FGun& G = GetGun();
		const bool bFreeAim = bAiming && (G.ChargeTime > 0.f || G.EnergyPerShot > 0.f);
		const FVector Far = Mech->IsPlayerControlled() ? GetFreeAimPoint() : ComputeAimPoint(Chest, G.BulletSpeed);
		PoseAimPoint = Chest + AssistedAimDir(Chest, G.BulletSpeed, bFreeAim) * FMath::Max(FVector::Dist(Far, Chest), 500.f);
	}
	else if (!RepAimPoint.IsZero())
	{
		PoseAimPoint = RepAimPoint;
	}
	else
	{
		PoseAimPoint = Chest + Mech->GetActorForwardVector() * 20000.f;
	}
	PoseAimDir = (PoseAimPoint - Chest).GetSafeNormal();
	if (GetOwner()->HasAuthority())
	{
		RepAimPoint = PoseAimPoint;
	}

	if (GetOwner()->HasAuthority())
	{
		UpdateGun(DeltaTime);
		UpdateKnives(DeltaTime);
		UpdateMissiles(DeltaTime);
	}
	UpdateStance(DeltaTime);
	UpdateMelee(DeltaTime);
	UpdateClashKnock(DeltaTime);
	UpdateBullets(DeltaTime);
	UpdateTracers();
	UpdateBeams(DeltaTime);
	// 霰弹枪吸血：一发的几颗弹丸同一帧打中，合成一次通知（回复本身在打中时就加了）
	if (PendingLifesteal > 0.f && GetOwner()->HasAuthority())
	{
		MulticastLifesteal(PendingLifesteal);
		PendingLifesteal = 0.f;
	}
	// -MechTTKTest（测试场）：本机玩家这台由脚本换枪换刀打木桩，量每秒伤害和失衡（MechCombatTest.cpp）
	static const bool bTTKTest = FParse::Param(FCommandLine::Get(), TEXT("MechTTKTest"));
	static const bool bDuelTest = FParse::Param(FCommandLine::Get(), TEXT("MechDuelTest"));
	if ((bTTKTest || bDuelTest) && Mech->IsPlayerControlled() && Mech->IsLocallyControlled() && GetOwner()->HasAuthority())
	{
		if (bTTKTest)
		{
			MechCombatTest::Tick(Mech, DeltaTime);
		}
		else
		{
			MechCombatTest::DuelTick(Mech, DeltaTime);
		}
	}
	static const bool bMeleeFeelTest = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeFeelTest"));
	if (bMeleeFeelTest && Mech->IsPlayerControlled() && Mech->IsLocallyControlled() && GetOwner()->HasAuthority())
	{
		MechCombatTest::MeleeFeelTick(Mech, DeltaTime);  // 刀剑手感逐帧测（MechMeleeFeelTest.cpp）
	}

	MuzzleFlash = FMath::Max(0.f, MuzzleFlash - DeltaTime * 22.f);
	MuzzleLight->SetWorldLocation(Muzzle);
	MuzzleLight->SetIntensity(MuzzleFlash * 9000.f);
}

void UMechCombatComponent::UpdateLock(float DeltaTime)
{
	AMechCharacter* Mech = GetMech();
	const bool bLocalPlayer = Mech->IsLocallyControlled() && Mech->IsPlayerControlled();
	if (Target && Target->GetHealth()->IsDead())
	{
		Target = nullptr;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	if (bLocalPlayer && Mech->GetController())
	{
		Mech->GetController()->GetPlayerViewPoint(ViewLoc, ViewRot);
	}
	else
	{
		ViewLoc = Mech->GetActorLocation() + FVector(0.f, 0.f, 120.f);
		ViewRot = Mech->GetControlRotation();
	}
	const FVector ViewDir = ViewRot.Vector();
	auto Visible = [&](const AMechCharacter* Other) {
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MechLockLOS), false, Mech);
		Params.AddIgnoredActor(Other);
		if (GetWorld()->LineTraceSingleByObjectType(Hit, ViewLoc, Other->GetActorLocation(), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			return false;
		}
		if (AMechSmoke::BlocksLine(GetWorld(), ViewLoc, Other->GetActorLocation()))
		{
			// 隔着烟幕锁不定：记下来，HUD 在它身上画干扰框
			JammedTarget = const_cast<AMechCharacter*>(Other);
			JamTime = GetWorld()->GetTimeSeconds();
			return false;
		}
		return true;
	};
	// 被 EMP 打中：锁定失效（玩家丢掉目标；AI 的目标由控制器给，只是导弹锁不上）
	if (Mech->IsEmped())
	{
		if (bLocalPlayer && !bForcedTarget)
		{
			Target = nullptr;
		}
		MissileLock = 0.f;
		return;
	}
	auto AngleTo = [&](const AMechCharacter* Other) {
		const FVector To = (Other->GetActorLocation() - ViewLoc).GetSafeNormal();
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(To | ViewDir, -1.f, 1.f)));
	};

	if (bLocalPlayer && !bForcedTarget)
	{
		// 玩家：已锁的目标还在放宽的范围里就保持，否则在中心锥里挑角度最小的
		AMechCharacter* Keep = nullptr;
		if (Target && AngleTo(Target) < LockKeepCone && FVector::Dist(Target->GetActorLocation(), ViewLoc) < LockRange && Visible(Target))
		{
			Keep = Target;
		}
		AMechCharacter* Best = Keep;
		float BestAngle = Keep ? AngleTo(Keep) * 0.6f : LockCone;
		for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
		{
			AMechCharacter* Other = *It;
			if (Other == Mech || Other == Keep || Other->GetTeam() == Mech->GetTeam() || Other->GetHealth()->IsDead())
			{
				continue;
			}
			const float A = AngleTo(Other);
			if (A < BestAngle && FVector::Dist(Other->GetActorLocation(), ViewLoc) < LockRange && Visible(Other))
			{
				Best = Other;
				BestAngle = A;
			}
		}
		if (Best != Target)
		{
			Target = Best;
			MissileLock = 0.f;
		}
		if (!GetOwner()->HasAuthority())
		{
			ServerSetAim(Target, GetFreeAimPoint());
		}
	}

	// 导弹锁定：目标在锁定锥里、看得见、够近就逐渐锁上
	const bool bHolding = Target && FVector::Dist(Target->GetActorLocation(), ViewLoc) < LockRange &&
		(bForcedTarget || AngleTo(Target) < LockKeepCone) && Visible(Target);
	MissileLock = bHolding ? FMath::Min(1.f, MissileLock + DeltaTime / (MissileLockTime * FrameLockScale)) : FMath::Max(0.f, MissileLock - DeltaTime * 2.f);
}

FVector UMechCombatComponent::GetPoseAimPivot() const
{
	const AMechCharacter* Mech = GetMech();
	// 10-31 联机抖动：客户端上别人的机体，胶囊在两包之间按速度外推、来包时跳回服务器位置，从胶囊指向同步来的瞄准点，方向跟着一漂一跳
	// （上身、枪管随之一抖）。改从收到的最新位置算（瞄准点也是那个时刻的），画面上的姿势再按这个方向慢慢转过去（AMechCharacter::UpdatePose）
	if (Mech && Mech->GetMechMovement()->IsInterpActive())
	{
		return Mech->GetMechMovement()->GetInterpLatestLocation() + FVector(0.f, 0.f, 100.f);
	}
	return Mech ? Mech->GetActorLocation() + FVector(0.f, 0.f, 100.f) : FVector::ZeroVector;
}

FVector UMechCombatComponent::GetFreeAimPoint() const
{
	const AMechCharacter* Mech = GetMech();
	if (Mech->IsLocallyControlled() && Mech->IsPlayerControlled() && Mech->GetController())
	{
		FVector ViewLoc;
		FRotator ViewRot;
		Mech->GetController()->GetPlayerViewPoint(ViewLoc, ViewRot);
		const FVector End = ViewLoc + ViewRot.Vector() * LockRange;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MechFreeAim), false, Mech);
		if (GetWorld()->LineTraceSingleByObjectType(Hit, ViewLoc + ViewRot.Vector() * 600.f, End,
			FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), Params))
		{
			return Hit.ImpactPoint;
		}
		return End;
	}
	if (!Mech->IsLocallyControlled() && Mech->IsPlayerControlled() && !RemoteAimPoint.IsZero())
	{
		return RemoteAimPoint;
	}
	return Mech->GetActorLocation() + Mech->GetControlRotation().Vector() * 20000.f;
}

FVector UMechCombatComponent::GetMuzzle(int32 Hand) const
{
	const AMechCharacter* Mech = GetMech();
	return Mech ? Mech->GetMechMesh()->GetPropPoint(Hand == 1 ? 3 : 1, GetGun().Muzzle) : FVector::ZeroVector;
}

FVector UMechCombatComponent::AssistedAimDir(const FVector& From, float Speed, bool bNoAssist) const
{
	const AMechCharacter* Mech = GetMech();
	if (!Mech->IsPlayerControlled())
	{
		return (ComputeAimPoint(From, Speed) - From).GetSafeNormal();
	}
	const FVector Free = (GetFreeAimPoint() - From).GetSafeNormal();
	if (bNoAssist || !Target || Target->GetHealth()->IsDead())
	{
		return Free;
	}
	const FVector Lead = (ComputeAimPoint(From, Speed) - From).GetSafeNormal();
	const float Deg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Free | Lead, -1.f, 1.f)));
	const float Dist = FMath::Max(FVector::Dist(From, Target->GetActorLocation()), 1.f);
	const float Cone = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(MechWeapons::AimAssistRadius / Dist)), MechWeapons::AimAssistMinCone, MechWeapons::AimAssistMaxCone);
	const float X = FMath::Clamp(Deg / Cone, 0.f, 1.f);
	const float Pull = MechWeapons::AimAssistStrength * (1.f - X * X);
	return Pull > 0.f ? FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Free, Lead), Pull).RotateVector(Free).GetSafeNormal() : Free;
}

FVector UMechCombatComponent::ComputeAimPoint(const FVector& From, float Speed) const
{
	if (Target && !Target->GetHealth()->IsDead())
	{
		FVector P = Target->GetActorLocation() + FVector(0.f, 0.f, 30.f);
		const float Dist = FVector::Dist(From, P);
		const float T = Dist / FMath::Max(Speed, 1.f);
		P += Target->GetVelocity() * T * LeadAccuracy;
		const AMechCharacter* Self = GetMech();
		if (Self && !Self->IsPlayerControlled())
		{
			// AI 的手不是死准的：瞄准点按距离慢慢晃，远了会打偏（以前永远对着中心，人打不过它）
			const float Clock = GetWorld()->GetTimeSeconds() + static_cast<float>(GetUniqueID() % 97);
			const FVector Side = FVector::CrossProduct(FVector::UpVector, (P - From).GetSafeNormal()).GetSafeNormal();
			const float Wobble = Dist * FMath::Tan(FMath::DegreesToRadians(MechWeapons::AIAimError)) * 1.6f * AIAimErrorScale;
			P += (Side * FMath::PerlinNoise1D(Clock * 0.8f) + FVector::UpVector * 0.6f * FMath::PerlinNoise1D(Clock * 1.1f + 31.f)) * Wobble;
		}
		return P;
	}
	return GetFreeAimPoint();
}

// ---------------------------------------------------------------- 枪
void UMechCombatComponent::UpdateGun(float DeltaTime)
{
	const MechWeapons::FGun& G = GetGun();
	AMechCharacter* Mech = GetMech();
	const float Now = GetWorld()->GetTimeSeconds();
	FireTimer -= DeltaTime;
	// 冲锋枪：停火就散热；过热时锁住，热量跟着锁定时间降
	if (G.HeatPerShot > 0.f)
	{
		if (OverheatTimer > 0.f)
		{
			OverheatTimer = FMath::Max(0.f, OverheatTimer - DeltaTime);
			Heat = OverheatTimer / MechWeapons::OverheatLock;
		}
		else if (Now - LastShotTime > 0.15f)
		{
			Heat = FMath::Max(0.f, Heat - MechWeapons::HeatCoolPerSecond * DeltaTime);
		}
	}
	// 步枪连中：停火一会就清零
	if (Streak > 0 && Now - StreakTime > MechWeapons::StreakWindow * OwnScale(MechLoadout::Shooter, MechWeapons::OwnStreakWindow))
	{
		Streak = 0;
	}
	if (ReloadTimer > 0.f)
	{
		ReloadTimer -= DeltaTime;
		if (ReloadTimer <= 0.f)
		{
			ReloadTimer = 0.f;
			Ammo = GetMagazine();
		}
		return;
	}
	const bool bCanFire = CanAct() && Stance == EMechStance::Gun && SwitchTime < 0.f;
	auto Shoot = [&](float InCharge) {
		FireShotAuthority(InCharge);
		if (G.Magazine > 0)
		{
			--Ammo;
		}
		if (G.HeatPerShot > 0.f)
		{
			Heat = FMath::Min(1.f, Heat + G.HeatPerShot * MechMapRules::HeatScale(GetWorld()));
			if (Heat >= 1.f)
			{
				OverheatTimer = MechWeapons::OverheatLock;
			}
		}
		if (G.EnergyPerShot > 0.f)
		{
			UMechMovementComponent* Move = Mech->GetMechMovement();
			Move->Energy = FMath::Max(0.f, Move->Energy - G.EnergyPerShot * OwnScale(MechLoadout::Shooter, MechWeapons::OwnSniperEnergy));
		}
		FireTimer = FMath::Max(FireTimer, -G.Interval) + G.Interval;
		LastEngagedTime = Now;
		if (G.Magazine > 0 && Ammo <= 0)
		{
			Reload();
		}
	};

	// 充能步枪（10-30 改，类似 Apex 旧版充能步枪）：按住充能，枪口激光照到的敌人持续掉血；充满自动打出光束（玩家和 AI 一样）；
	// 提前松开 = 按当时的充能打出（10-30 晚玩家：松手就发射，不会撤回；威力见 MechWeapons::ChargeShotScale，最少两成）
	if (G.ChargeTime > 0.f)
	{
		if (!bCanFire)
		{
			bCharging = false;
			Charge = 0.f;
			LaserTickTimer = 0.f;
			FireTimer = FMath::Max(FireTimer, 0.f);
			return;
		}
		if (bTrigger && FireTimer <= 0.f && Ammo > 0)
		{
			bCharging = true;
			Charge = FMath::Min(1.f, Charge + DeltaTime / (G.ChargeTime * OwnScale(MechLoadout::Shooter, MechWeapons::OwnChargeTime)));
			LastEngagedTime = Now;
			LaserTickTimer += DeltaTime;
			while (LaserTickTimer >= MechWeapons::LaserTick)
			{
				LaserTickTimer -= MechWeapons::LaserTick;
				LaserDamageAuthority();
			}
			if (Charge >= 1.f)
			{
				FireBeamAuthority(1.f);
				--Ammo;
				bCharging = false;
				Charge = 0.f;
				LaserTickTimer = 0.f;
				FireTimer = G.Interval;
				if (Ammo <= 0)
				{
					Reload();
				}
			}
		}
		else if (bCharging)
		{
			// 松开：按充到的程度打出这一发（耗一发弹）
			FireBeamAuthority(MechWeapons::ChargeShotScale(Charge));
			--Ammo;
			bCharging = false;
			Charge = 0.f;
			LaserTickTimer = 0.f;
			FireTimer = G.Interval;
			if (Ammo <= 0)
			{
				Reload();
			}
		}
		else
		{
			FireTimer = FMath::Max(FireTimer, 0.f);
		}
		return;
	}

	// 11-01 肉鸽 点射：三连发。按一下就打完一组（松手也打完），按住就一组接一组
	UMechRunComponent* Run = Mech->GetRun();
	const bool bBurstMode = Run && Run->IsBurstMode();
	if ((!bTrigger && !(bBurstMode && Run->IsBurstGoing())) || !bCanFire)
	{
		FireTimer = FMath::Max(FireTimer, 0.f);
		return;
	}
	if (FireTimer > 0.f || OverheatTimer > 0.f)
	{
		return;
	}
	if (G.EnergyPerShot > 0.f && Mech->GetMechMovement()->Energy < G.EnergyPerShot * OwnScale(MechLoadout::Shooter, MechWeapons::OwnSniperEnergy))
	{
		FireTimer = 0.f;
		return;  // 狙击枪：推进能量不够一发
	}
	if (G.Magazine > 0 && Ammo <= 0)
	{
		Reload();
		return;
	}
	if (bBurstMode)
	{
		Run->BeforeBurstShot();
	}
	Shoot(0.f);
	if (bBurstMode)
	{
		Run->AfterBurstShot();
	}
}

void UMechCombatComponent::FireShotAuthority(float InCharge)
{
	const MechWeapons::FGun& G = GetGun();
	const int32 Hand = G.bDual ? 1 - ShotHand : 0;  // 双持：左右手交替
	const FVector Muzzle = GetMuzzle(Hand);
	// 狙击枪开镜：打准星，准星贴近敌人身体时吸附到离准星最近的部位（瞄头附近就打头）；其余玩家开火打准星、贴近目标时往目标那边帮一下
	const bool bFreeAim = bAiming && (G.ChargeTime > 0.f || G.EnergyPerShot > 0.f);
	FVector Dir = AssistedAimDir(Muzzle, G.BulletSpeed, bFreeAim);
	const float Spread = G.Spread * (bAiming ? G.AimSpread : 1.f) * SpreadScale * FrameSpreadScale;  // 右键瞄准时散布小很多
	if (G.Pellets <= 1)
	{
		Dir = FMath::VRandCone(Dir, FMath::DegreesToRadians(Spread));
	}
	if (bAiming && G.bScopeMagnet && GetMech()->IsPlayerControlled())
	{
		Dir = ScopeMagnetDir(Muzzle, Dir, G.BulletSpeed);
	}
	MulticastShot(Muzzle, Dir, static_cast<uint8>(Hand), static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(InCharge, 0.f, 1.f) * 255.f)),
		static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Spread * 10.f), 0, 255)), FMath::Rand());
}

void UMechCombatComponent::MulticastShot_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir, uint8 Hand, uint8 ChargeByte,
	uint8 SpreadTenths, int32 Seed)
{
	const MechWeapons::FGun& G = GetGun();
	const float C = ChargeByte / 255.f;  // 以前精准步枪松开时的充能；充能步枪改走光束（MulticastBeam）后这里总是 0
	const float Damage = G.Damage;
	const float Impact = G.Impact;
	const float Width = G.TracerWidth * (1.f + C);
	const bool bLive = GetOwner()->HasAuthority();
	// 客户端上的子弹只是画面：从本机看到的枪口出发、飞向服务器那发子弹会打到的地方。
	// 服务器的枪口位置在客户端上对不上（自己的机体比服务器超前半个来回，别人的机体在平滑追赶），直接用的话曳光从枪旁边几十厘米处冒出来
	FVector From = Origin;
	if (!bLive)
	{
		const FVector Local = GetMuzzle(Hand);
		if (FVector::DistSquared(Local, Origin) < FMath::Square(600.f))
		{
			From = Local;
		}
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechShotVisual), false, GetOwner());
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	auto Fire = [&](const FVector& D) {
		FVector V = D;
		if (From != Origin)
		{
			const FVector End = Origin + D * G.BulletSpeed * G.BulletLife;
			FHitResult Hit;
			const bool bHit = GetWorld()->LineTraceSingleByObjectType(Hit, Origin, End, Objects, Params);
			V = ((bHit ? Hit.ImpactPoint : End) - From).GetSafeNormal();
		}
		AddBullet(From, V, bLive, G.BulletSpeed, G.BulletLife, Damage, Impact, Width, false);
		// 11-01 肉鸽：洪流穿透（各端的画面都要穿过去）、点射的组号和伤害（服务器那份）
		if (RunShotFlags != 0)
		{
			Bullets.Last().bPierce = (RunShotFlags & 1) != 0;
		}
		if (bLive)
		{
			if (UMechRunComponent* Run = GetMech()->GetRun())
			{
				Run->TagBullet(Bullets.Last());
			}
		}
	};
	if (G.Pellets > 1)
	{
		// 霰弹：各端用同一个种子算出同样的弹丸方向
		FRandomStream Stream(Seed);
		const float Cone = FMath::DegreesToRadians(SpreadTenths / 10.f);
		for (int32 i = 0; i < G.Pellets; ++i)
		{
			Fire(Stream.VRandCone(Dir, Cone));
		}
	}
	else
	{
		Fire(Dir);
	}
	ShotHand = Hand;
	LastEngagedTime = GetWorld()->GetTimeSeconds();
	LastShotTime = LastEngagedTime;
	MuzzleFlash = 1.f + C;
	if (MuzzleFX)
	{
		// 每发都放枪口焰：和其他特效一样走组件池，反复新建 / 销毁组件会让游戏线程抖
		const float Size = G.Pellets > 1 ? 1.2f : 0.7f * (1.f + C) * (G.EnergyPerShot > 0.f ? 1.6f : 1.f);
		SpawnMuzzleFX(this, MuzzleFX, From, Dir.Rotation(), FVector(Size));
	}
	if (RifleSounds.Num())
	{
		USoundBase* Sound = RifleSounds[FMath::RandHelper(RifleSounds.Num())];
		if (Sound)
		{
			// 射速越快单发越轻（冲锋枪连发不糊成一片），霰弹、狙击这种一发一发的响一点
			const float ShotVolume = G.Pellets > 1 || G.EnergyPerShot > 0.f ? 0.8f : 0.62f * FMath::Clamp(G.Interval / 0.15f, 0.45f, 1.f);
			UGameplayStatics::PlaySoundAtLocation(this, Sound, Origin, FRotator::ZeroRotator, ShotVolume,
				GunPitch(GunType) * (1.f - 0.15f * C) + FMath::FRandRange(-0.08f, 0.08f), 0.f, Attenuation);
		}
	}
	if (AMechCharacter* Mech = GetMech())
	{
		const float Heavy = G.Kick / 16.f;
		Mech->AddRecoil(0.06f * Heavy);
		Mech->CameraKick(-Dir, 0.05f * Heavy, G.Kick * (bAiming ? 0.6f : 1.f));
		// 狙击枪耗能：服务器在开火时扣过了，操控的客户端这里也扣一份，免得本地预测的能量对不上
		if (G.EnergyPerShot > 0.f && Mech->IsLocallyControlled() && !GetOwner()->HasAuthority())
		{
			UMechMovementComponent* Move = Mech->GetMechMovement();
			Move->Energy = FMath::Max(0.f, Move->Energy - G.EnergyPerShot * OwnScale(MechLoadout::Shooter, MechWeapons::OwnSniperEnergy));
		}
	}
}

void UMechCombatComponent::MulticastKnifePush_Implementation(AMechCharacter* Victim, FVector_NetQuantize100 Push)
{
	if (Victim && !Victim->GetHealth()->IsDead() && (Victim->HasAuthority() || Victim->IsLocallyControlled()))
	{
		Victim->AddKnifePush(Push);
	}
}

void UMechCombatComponent::MulticastLifesteal_Implementation(float Amount)
{
	// 霰弹枪吸血：机体身上淡淡闪一下绿光（回得多更亮），HUD 弹药数字变绿两秒；不再放声音（每发都响太吵）
	LastHealTime = GetWorld()->GetTimeSeconds();
	if (AMechCharacter* Mech = GetMech())
	{
		FlashLight(Mech->GetActorLocation() + FVector(0.f, 0.f, 120.f), FMath::GetMappedRangeValueClamped(FVector2D(50.f, 300.f), FVector2D(1200.f, 4000.f), Amount),
			FLinearColor(0.3f, 1.f, 0.45f));
	}
}

// ---------------------------------------------------------------- 充能步枪
FVector UMechCombatComponent::LaserDirFrom(const FVector& From) const
{
	const FVector D = (PoseAimPoint - From).GetSafeNormal();
	return D.IsNearlyZero() ? AimDir : D;
}

void UMechCombatComponent::LaserDamageAuthority()
{
	const MechWeapons::FGun& G = GetGun();
	AMechCharacter* Mech = GetMech();
	const FVector From = GetMuzzle();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechLaser), false, Mech);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByObjectType(Hit, From, From + LaserDirFrom(From) * MechWeapons::BeamRange, Objects, Params))
	{
		return;
	}
	AMechCharacter* Victim = Cast<AMechCharacter>(Hit.GetActor());
	if (!Victim || Victim->GetTeam() == Mech->GetTeam() || Victim->GetHealth()->IsDead())
	{
		return;
	}
	const bool bDirect = Victim->GetHealth()->IsStaggered();
	const float Dealt = Victim->ReceiveHit(G.LaserDPS * MechWeapons::LaserTick, G.LaserImpact * MechWeapons::LaserTick, Mech, Hit.ImpactPoint, EMechHitKind::Bullet, 1.f);
	if (Dealt > 0.f)
	{
		ConfirmHit(Victim, bDirect, EMechHitKind::Bullet, Dealt, Hit.ImpactPoint);
	}
}

void UMechCombatComponent::FireBeamAuthority(float Power)
{
	const MechWeapons::FGun& G = GetGun();
	AMechCharacter* Mech = GetMech();
	const FVector Muzzle = GetMuzzle();
	// 命中扫描：方向和别的枪一样（玩家打准星、贴近目标时帮一下，开镜不帮；AI 打锁定目标），弹速当成无穷大不算提前量
	const bool bFreeAim = bAiming;
	FVector Dir = AssistedAimDir(Muzzle, 1.e8f, bFreeAim);
	Dir = FMath::VRandCone(Dir, FMath::DegreesToRadians(G.Spread * (bAiming ? G.AimSpread : 1.f) * SpreadScale * FrameSpreadScale));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechBeam), false, Mech);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FHitResult Hit;
	const FVector Far = Muzzle + Dir * MechWeapons::BeamRange;
	const bool bHit = GetWorld()->LineTraceSingleByObjectType(Hit, Muzzle, Far, Objects, Params);
	const FVector End = bHit ? Hit.ImpactPoint : Far;
	AMechCharacter* Victim = bHit ? Cast<AMechCharacter>(Hit.GetActor()) : nullptr;
	if (Victim && Victim->GetTeam() != Mech->GetTeam() && !Victim->GetHealth()->IsDead())
	{
		const bool bDirect = Victim->GetHealth()->IsStaggered();
		const EMechHitPart Part = Victim->ClassifyHit(Hit.ImpactPoint, Dir);
		++DebugPartHits[static_cast<int32>(Part)];
		const float Dealt = Victim->ReceiveHit(G.Damage * Power * MechWeapons::PartScale[static_cast<int32>(Part)], G.Impact * Power, Mech, Hit.ImpactPoint,
			EMechHitKind::Bullet);
		if (Dealt > 0.f)
		{
			ConfirmHit(Victim, bDirect, EMechHitKind::Bullet, Dealt, Hit.ImpactPoint, Part);
		}
	}
	LastEngagedTime = GetWorld()->GetTimeSeconds();
	MulticastBeam(Muzzle, End, Victim != nullptr, static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Power, 0.f, 1.f) * 255.f)));
}

void UMechCombatComponent::MulticastBeam_Implementation(FVector_NetQuantize Origin, FVector_NetQuantize End, bool bHitMech, uint8 PowerByte)
{
	const MechWeapons::FGun& G = GetGun();
	const float Now = GetWorld()->GetTimeSeconds();
	BeamTime = Now;
	BeamTo = End;
	BeamPower = FMath::Max(PowerByte / 255.f, 0.05f);  // 光束粗细、亮度、枪口焰、命中火花、声音都按它缩
	LastShotTime = LastEngagedTime = Now;
	MuzzleFlash = 2.f;
	const FVector From = GetMuzzle();
	const FVector Dir = (End - From).GetSafeNormal();
	const float P = BeamPower;
	if (MuzzleFX)
	{
		SpawnMuzzleFX(this, MuzzleFX, From, Dir.Rotation(), FVector(FMath::Lerp(0.7f, 1.5f, P)));
	}
	if (UNiagaraSystem* FX = bHitMech ? HitMechFX.Get() : HitWorldFX.Get())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, FX, End, (-Dir).Rotation(), FVector((bHitMech ? 1.8f : 0.9f) * FMath::Lerp(0.5f, 1.f, P)), true, true,
			ENCPoolMethod::AutoRelease);
	}
	FlashLight(End - Dir * 40.f, (bHitMech ? 5000.f : 2500.f) * FMath::Lerp(0.4f, 1.f, P), FLinearColor(1.f, 0.7f, 0.45f));
	// 光束声：火炮开炮声抬高音调（短促、带电感），再叠一层低沉的枪声；弱的一发更轻更尖
	if (BeamSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, BeamSound, Origin, FRotator::ZeroRotator, FMath::Lerp(0.4f, 0.75f, P),
			FMath::Lerp(1.75f, 1.45f, P) + FMath::FRandRange(-0.04f, 0.04f), 0.f, Attenuation);
	}
	if (RifleSounds.Num() && RifleSounds[0])
	{
		UGameplayStatics::PlaySoundAtLocation(this, RifleSounds[FMath::RandHelper(RifleSounds.Num())], Origin, FRotator::ZeroRotator, FMath::Lerp(0.45f, 0.7f, P),
			FMath::Lerp(0.9f, 0.7f, P), 0.f, Attenuation);
	}
	if (AMechCharacter* Mech = GetMech())
	{
		const float Heavy = G.Kick / 16.f * FMath::Lerp(0.4f, 1.f, P);
		Mech->AddRecoil(0.06f * Heavy);
		Mech->CameraKick(-Dir, 0.05f * Heavy, G.Kick * (bAiming ? 0.6f : 1.f));
	}
}

void UMechCombatComponent::EnsureBeamMeshes()
{
	if (LaserMeshes)
	{
		return;
	}
	AActor* Owner = GetOwner();
	UMaterialInterface* Base = BeamMaterial ? BeamMaterial.Get() : TracerMaterial.Get();
	auto Make = [&](const TCHAR* Name, const FLinearColor& Color, TObjectPtr<UMaterialInstanceDynamic>& OutMID) {
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(Owner, Name);
		C->SetStaticMesh(TracerMesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->bAffectDistanceFieldLighting = false;
		C->bAffectDynamicIndirectLighting = false;
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetupAttachment(Owner->GetRootComponent());
		C->SetHasPerInstancePrevTransforms(true);
		C->RegisterComponent();
		C->SetWorldTransform(FTransform::Identity);
		if (Base)
		{
			OutMID = UMaterialInstanceDynamic::Create(Base, this);
			// 光束材质的参数是 Color / Intensity；退回曳光材质时是 Emissive / EmissiveStrength，两套都设
			OutMID->SetVectorParameterValue(TEXT("Color"), Color);
			OutMID->SetVectorParameterValue(TEXT("Emissive"), Color);
			C->SetMaterial(0, OutMID);
		}
		return C;
	};
	// 激光：偏红的细线（被瞄的人一眼认得出是警告）；光束：白热的芯 + 橙色的光晕，工业感、不花哨
	LaserMeshes = Make(TEXT("ChargeLaser"), FLinearColor(1.f, 0.1f, 0.05f), LaserMID);
	BeamCoreMeshes = Make(TEXT("ChargeBeamCore"), FLinearColor(1.f, 0.82f, 0.62f), BeamCoreMID);
	BeamGlowMeshes = Make(TEXT("ChargeBeamGlow"), FLinearColor(1.f, 0.42f, 0.12f), BeamGlowMID);
}

void UMechCombatComponent::UpdateBeams(float DeltaTime)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const MechWeapons::FGun& G = GetGun();
	AMechCharacter* Mech = GetMech();
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bDead = Mech->GetHealth()->IsDead();
	const bool bLaser = G.ChargeTime > 0.f && Charge > 0.f && Stance == EMechStance::Gun && !bDead;
	const float BeamAge = Now - BeamTime;
	const bool bBeam = G.ChargeTime > 0.f && BeamAge < BeamShow && !bDead;
	// 刚开始充能：上膛声（音调抬高，像电容充电）
	if (bLaser && PrevVisualCharge <= 0.f && ChargeSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ChargeSound, GetMuzzle(), FRotator::ZeroRotator, 0.55f, 1.6f, 0.f, Attenuation);
	}
	PrevVisualCharge = bLaser ? Charge : 0.f;
	if (!bLaser && !bBeam)
	{
		if (LaserMeshes && (LaserMeshes->GetInstanceCount() || BeamCoreMeshes->GetInstanceCount()))
		{
			LaserMeshes->ClearInstances();
			BeamCoreMeshes->ClearInstances();
			BeamGlowMeshes->ClearInstances();
		}
		return;
	}
	EnsureBeamMeshes();
	// 粗细按离本机镜头的距离补偿（每段各算），远处的人也看得见这道线；分 8 段画
	FVector Cam = Mech->GetActorLocation();
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		Cam = PC->PlayerCameraManager->GetCameraLocation();
	}
	auto Segments = [&](TArray<FTransform>& Out, const FVector& A, const FVector& B, float Scale, float MinR, float MaxR) {
		const int32 N = 8;
		const FVector D = (B - A).GetSafeNormal();
		const FQuat Q = FRotationMatrix::MakeFromZ(D).ToQuat();
		const float Len = FVector::Dist(A, B);
		for (int32 i = 0; i < N; ++i)
		{
			const FVector P0 = A + D * (Len * i / N);
			const FVector P1 = A + D * (Len * (i + 1) / N);
			const FVector Mid = (P0 + P1) * 0.5f;
			const float R = FMath::Clamp(FVector::Dist(Mid, Cam) * Scale, MinR, MaxR);
			Out.Add(FTransform(Q, Mid, FVector(R / 50.f, R / 50.f, Len / N / 100.f)));  // 引擎圆柱：半径 50、高 100
		}
	};
	TArray<FTransform> Laser, Core, Glow;
	if (bLaser)
	{
		const FVector From = GetMuzzle();
		const FVector Dir = LaserDirFrom(From);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MechLaserFx), false, Mech);
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		FHitResult Hit;
		const bool bHit = GetWorld()->LineTraceSingleByObjectType(Hit, From, From + Dir * MechWeapons::BeamRange, Objects, Params);
		const FVector To = bHit ? Hit.ImpactPoint : From + Dir * MechWeapons::BeamRange;
		Segments(Laser, From, To, 0.0008f * (0.7f + 0.6f * Charge), 0.8f, 14.f);
		// 照在机体上：一小撮火花（本机自己放，0.12 秒一次）
		LaserSparkTimer -= DeltaTime;
		if (bHit && Cast<AMechCharacter>(Hit.GetActor()) && LaserSparkTimer <= 0.f && HitMechFX)
		{
			LaserSparkTimer = 0.12f;
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitMechFX, To, Hit.ImpactNormal.Rotation(), FVector(0.45f), true, true, ENCPoolMethod::AutoRelease);
		}
		if (LaserMID)
		{
			// 充得越满越亮，快满时轻微闪烁（提醒被瞄的人：要打了）
			const float Flicker = Charge > 0.75f ? 0.8f + 0.2f * FMath::Sin(Now * 60.f) : 1.f;
			const float I = FMath::Lerp(2.5f, 8.f, Charge) * Flicker;  // 白天实测：太亮会糊成白线、看不出红色
			LaserMID->SetScalarParameterValue(TEXT("Intensity"), I);
			LaserMID->SetScalarParameterValue(TEXT("EmissiveStrength"), I * 3.f);
		}
	}
	if (bBeam)
	{
		const float A = 1.f - BeamAge / BeamShow;
		// 提前松开的弱一发：粗细 ×0.45–1、亮度 ×0.3–1（按这一发的威力）
		const float W = FMath::Lerp(0.45f, 1.f, BeamPower);
		const float L = FMath::Lerp(0.3f, 1.f, BeamPower);
		const FVector From = GetMuzzle();
		Segments(Core, From, BeamTo, 0.0015f * (0.35f + 0.65f * A) * W, 2.5f * W, 22.f * W);
		Segments(Glow, From, BeamTo, 0.0025f * (0.5f + 0.5f * A) * W, 5.f * W, 35.f * W);
		// 白天实测（10-30 测试场）：芯 40、光晕 5 时整条光束泛光成一大片白，挡住画面；6 / 1.2 还是一大片 → 芯 3、光晕 0.3、光晕变细（最后这档没再截图）
		if (BeamCoreMID)
		{
			BeamCoreMID->SetScalarParameterValue(TEXT("Intensity"), 3.f * A * A * L);
			BeamCoreMID->SetScalarParameterValue(TEXT("EmissiveStrength"), 40.f * A * A * L);
		}
		if (BeamGlowMID)
		{
			BeamGlowMID->SetScalarParameterValue(TEXT("Intensity"), 0.3f * A * L);
			BeamGlowMID->SetScalarParameterValue(TEXT("EmissiveStrength"), 6.f * A * L);
		}
	}
	auto Rebuild = [](UInstancedStaticMeshComponent* Into, const TArray<FTransform>& Xf) {
		Into->ClearInstances();
		if (Xf.Num())
		{
			Into->AddInstances(Xf, false, true);
			Into->BatchUpdateInstancesTransforms(0, Xf, Xf, true, true, true);
		}
	};
	Rebuild(LaserMeshes, Laser);
	Rebuild(BeamCoreMeshes, Core);
	Rebuild(BeamGlowMeshes, Glow);
}

// ---------------------------------------------------------------- 狙击开镜吸附
FVector UMechCombatComponent::ScopeMagnetDir(const FVector& From, const FVector& Free, float Speed) const
{
	const AMechCharacter* Mech = GetMech();
	const FVector RayEnd = From + Free * LockRange;
	float BestAngle = TNumericLimits<float>::Max();
	FVector BestPoint = FVector::ZeroVector;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		const AMechCharacter* Other = *It;
		if (Other == Mech || Other->GetTeam() == Mech->GetTeam() || Other->GetHealth()->IsDead())
		{
			continue;
		}
		const float Dist = FVector::Dist(From, Other->GetActorLocation());
		if (Dist > LockRange || ((Other->GetActorLocation() - From) | Free) <= 0.f)
		{
			continue;
		}
		// 身体按子弹飞到时的位置算（玩家自己留了提前量才吸得上；直接瞄着身体打移动目标，吸附也不替你补提前量）
		const FVector Shift = Other->GetVelocity() * (Dist / FMath::Max(Speed, 1.f));
		const float Window = FMath::DegreesToRadians(FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(MechWeapons::ScopeMagnetRadius / Dist)),
			MechWeapons::ScopeMagnetMin, MechWeapons::ScopeMagnetMax));
		const UMechMeshComponent* M = Other->GetMechMesh();
		const float S = M->GetComponentScale().Z;
		for (const FBodySeg& Seg : BodySegs)
		{
			const FVector P0 = M->GetBoneLocation(Seg.A) + Shift;
			const FVector P1 = (Seg.B ? M->GetBoneLocation(Seg.B) : M->GetBoneLocation(Seg.A) + Other->GetActorUpVector() * 24.f * S) + Shift;
			FVector OnRay, OnBone;
			FMath::SegmentDistToSegmentSafe(From, RayEnd, P0, P1, OnRay, OnBone);
			const float R = Seg.Radius * S;
			const float Miss = FVector::Dist(OnRay, OnBone) - R;
			if (Miss <= 0.f)
			{
				return Free;  // 射线本来就穿过这台敌人的身体：照准星打，不改
			}
			const float Angle = Miss / FMath::Max(FVector::Dist(From, OnRay), 1.f);
			if (Angle < Window && Angle < BestAngle)
			{
				BestAngle = Angle;
				// 打到这段骨头离射线最近的表面上（往里收一点，确保真的打进胶囊）
				BestPoint = OnBone + (OnRay - OnBone).GetSafeNormal() * R * 0.5f;
			}
		}
	}
	return BestAngle < TNumericLimits<float>::Max() ? (BestPoint - From).GetSafeNormal() : Free;
}

// ---------------------------------------------------------------- 投掷匕首
void UMechCombatComponent::UpdateKnives(float DeltaTime)
{
	const MechWeapons::FBlade& B = GetBlade();
	ThrowCooldown = FMath::Max(0.f, ThrowCooldown - DeltaTime);
	if (B.Knives <= 0 || Knives >= B.Knives)
	{
		KnifeTimer = 0.f;
		return;
	}
	if (KnifeTimer <= 0.f)
	{
		KnifeTimer = B.KnifeRegen * OwnScale(MechLoadout::Melee, MechWeapons::OwnKnifeRegen);
	}
	KnifeTimer -= DeltaTime;
	if (KnifeTimer <= 0.f)
	{
		++Knives;
		KnifeTimer = Knives < B.Knives ? B.KnifeRegen * OwnScale(MechLoadout::Melee, MechWeapons::OwnKnifeRegen) : 0.f;
	}
}

void UMechCombatComponent::ThrowKnivesAuthority(int32 Count)
{
	const MechWeapons::FBlade& B = GetBlade();
	if (Knives <= 0 || ThrowCooldown > 0.f || MeleeMove != EMechMelee::None)
	{
		return;
	}
	Count = FMath::Min(Count, Knives);
	Knives -= Count;
	AMechCharacter* Mech = GetMech();
	// 每投 4 把普通匕首，下一把是强力匕首；AI 没有蓄力条，按同样的节奏投
	const bool bPower = Count == 1 && KnifeStreak >= 4;
	KnifeStreak = bPower ? 0 : FMath::Min(KnifeStreak + Count, 4);
	ThrowCooldown = Mech->IsPlayerControlled() ? ThrowInterval : FMath::Max(ThrowInterval, KnifeCharge);
	AMechCharacter* Homing = Target && !Target->GetHealth()->IsDead() ? Target.Get() : nullptr;
	const FVector Origin = Mech->GetMechMesh()->GetPropPoint(0, FVector(20.f, 0.f, 0.f)) + FVector(0.f, 0.f, 25.f);
	const FVector Dir = AssistedAimDir(Origin, B.KnifeSpeed, false);
	if (!Dir.IsNearlyZero())
	{
		Mech->SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
	}
	MulticastKnives(Origin, Dir, static_cast<uint8>(Count), bPower, Homing);
}

void UMechCombatComponent::MulticastKnives_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir, uint8 Count, bool bPower, AMechCharacter* Homing)
{
	const MechWeapons::FBlade& B = GetBlade();
	const bool bLive = GetOwner()->HasAuthority();
	// 一把直线飞；三把扇形，左右各偏 7 度
	for (int32 i = 0; i < Count; ++i)
	{
		const float Yaw = Count > 1 ? (i - (Count - 1) * 0.5f) * 7.f : 0.f;
		const FVector D = Dir.RotateAngleAxis(Yaw, FVector::UpVector);
		const float Mul = bPower ? PowerKnifeScale : 1.f;
		AddBullet(Origin, D, bLive, B.KnifeSpeed * (bPower ? 1.3f : 1.f), KnifeLife, B.KnifeDamage * Mul, B.KnifeImpact * Mul, 1.f, true);
		FMechBullet& Knife = Bullets.Last();
		Knife.Homing = Homing;
		Knife.Turn = bPower ? PowerKnifeTurn : KnifeTurn;
		Knife.Scale = bPower ? 1.6f : 1.f;
	}
	LastEngagedTime = GetWorld()->GetTimeSeconds();
	LastThrowTime = LastEngagedTime;
	if (SwingSounds.Num() && SwingSounds[0])
	{
		PlayAt(SwingSounds[FMath::RandHelper(SwingSounds.Num())], Origin, 0.8f, 0.1f);
	}
	if (AMechCharacter* Mech = GetMech())
	{
		Mech->CameraKick(FVector(Dir), Count > 1 ? 0.2f : 0.1f);
	}
}

void UMechCombatComponent::AddBullet(const FVector& Origin, const FVector& Dir, bool bLive, float Speed, float Life, float Damage, float Impact, float Width,
	bool bKnife)
{
	FMechBullet& B = Bullets.AddDefaulted_GetRef();
	B.Pos = Origin;
	B.Vel = Dir * Speed;
	B.Life = Life;
	B.bLive = bLive;
	B.Damage = Damage;
	B.Impact = Impact;
	B.Width = Width;
	B.bKnife = bKnife;
}

void UMechCombatComponent::UpdateBullets(float DeltaTime)
{
	AMechCharacter* Mech = GetMech();
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechBullet), false, Mech);
	UMechRunComponent* Run = Mech->GetRun();  // 11-01 肉鸽（MechRun.h）；对战里是空，下面的钩子都不走
	for (int32 i = Bullets.Num() - 1; i >= 0; --i)
	{
		FMechBullet& B = Bullets[i];
		// 11-01 肉鸽 洪流 3：追踪弹，追的可能是杂兵（上面 Homing 那条路只认机体）
		if (B.RunHoming.IsValid())
		{
			const FVector Cur = B.Vel.GetSafeNormal();
			const FVector Want = (B.RunHoming->GetActorLocation() - B.Pos).GetSafeNormal();
			const float Ang = FMath::Acos(FMath::Clamp(Cur | Want, -1.f, 1.f));
			if (Ang > KINDA_SMALL_NUMBER)
			{
				const float Step = FMath::Min(1.f, FMath::DegreesToRadians(B.Turn) * DeltaTime / Ang);
				B.Vel = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Cur, Want), Step).RotateVector(Cur) * B.Vel.Size();
			}
		}
		if (B.bKnife && B.Homing.IsValid() && !B.Homing->GetHealth()->IsDead())
		{
			// 匕首追踪：飞行方向每秒最多往目标拐 Turn 度，速度大小不变；已经飞过头（背对目标）就不再拐
			const FVector Cur = B.Vel.GetSafeNormal();
			const FVector Want = (B.Homing->GetActorLocation() + FVector(0.f, 0.f, 30.f) - B.Pos).GetSafeNormal();
			const float Ang = FMath::Acos(FMath::Clamp(Cur | Want, -1.f, 1.f));
			if (Ang > KINDA_SMALL_NUMBER && Ang < PI * 0.6f)
			{
				const float Step = FMath::Min(1.f, FMath::DegreesToRadians(B.Turn) * DeltaTime / Ang);
				B.Vel = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Cur, Want), Step).RotateVector(Cur) * B.Vel.Size();
			}
		}
		const FVector Next = B.Pos + B.Vel * DeltaTime;
		FHitResult Hit;
		bool bBulletHit;
		if (B.bPierce)
		{
			// 11-01 肉鸽 洪流 1：打中杂兵不停，记下来接着往前查（只穿杂兵，机甲和地形照常挡）
			FCollisionQueryParams PierceParams = Params;
			for (const TWeakObjectPtr<AActor>& P : B.Pierced)
			{
				PierceParams.AddIgnoredActor(P.Get());
			}
			bBulletHit = GetWorld()->LineTraceSingleByObjectType(Hit, B.Pos, Next, Objects, PierceParams);
			while (bBulletHit && Hit.GetActor() && Hit.GetActor()->ActorHasTag(TEXT("MechGrunt")))
			{
				AActor* Pierced = Hit.GetActor();
				if (B.bLive)
				{
					float PierceDamage = B.Damage;
					if (Run)
					{
						Run->NotePierce(B);
						PierceDamage = Run->OnBulletHitGrunt(Pierced, B, PierceDamage, Hit.ImpactPoint);
					}
					UGameplayStatics::ApplyDamage(Pierced, PierceDamage, nullptr, Mech, TSubclassOf<UDamageType>());
				}
				if (HitMechFX)
				{
					UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitMechFX, Hit.ImpactPoint, Hit.ImpactNormal.Rotation(), FVector(0.7f), true, true, ENCPoolMethod::AutoRelease);
				}
				B.Pierced.Add(Pierced);
				PierceParams.AddIgnoredActor(Pierced);
				bBulletHit = GetWorld()->LineTraceSingleByObjectType(Hit, B.Pos, Next, Objects, PierceParams);
			}
		}
		else
		{
			bBulletHit = GetWorld()->LineTraceSingleByObjectType(Hit, B.Pos, Next, Objects, Params);
		}
		if (bBulletHit)
		{
			AMechCharacter* Victim = Cast<AMechCharacter>(Hit.GetActor());
			if (Victim)
			{
				if (B.bLive && Victim->GetTeam() != Mech->GetTeam() && !Victim->GetHealth()->IsDead())
				{
					const MechWeapons::FGun& G = GetGun();
					const bool bDirect = Victim->GetHealth()->IsStaggered();
					float Damage = B.Damage;
					if (!B.bKnife && !B.bRunExtra && G.StreakBonus > 0.f)
					{
						// 步枪：连续打中同一个目标，每层 +3%（11-01 肉鸽 洪流 2：换目标不清）
						const float Now = GetWorld()->GetTimeSeconds();
						if ((StreakTarget.Get() != Victim && !(Run && Run->KeepsStreak())) || Now - StreakTime > MechWeapons::StreakWindow * OwnScale(MechLoadout::Shooter, MechWeapons::OwnStreakWindow))
						{
							Streak = 0;
						}
						Damage *= 1.f + G.StreakBonus * Streak;
						Streak = FMath::Min(Streak + 1, MechWeapons::StreakMax);
						StreakTarget = Victim;
						StreakTime = Now;
					}
					// 分部位：头 ×1.5、躯干 ×1、四肢 ×0.85
					const EMechHitPart Part = Victim->ClassifyHit(Hit.ImpactPoint, B.Vel);
					Damage *= MechWeapons::PartScale[static_cast<int32>(Part)];
					++DebugPartHits[static_cast<int32>(Part)];
					// 飞了多远（距离衰减、霰弹近距离冲击加成都按它）
					const float Flown = B.Traveled + Hit.Distance;
					float Impact = B.Impact;
					if (!B.bKnife)
					{
						Damage *= G.FalloffAt(Flown - (Mech->IsOwnMaker(MechLoadout::Shooter) ? MechWeapons::OwnFalloffShift : 0.f));  // 冲锋枪：20 米外开始掉伤害
						if (G.CloseRange > 0.f && Flown < G.CloseRange)
						{
							Impact *= G.CloseImpact;  // 霰弹枪：近距离冲击加成（压制冲上来的刀）
						}
					}
					const EMechHitKind Kind = B.bKnife ? EMechHitKind::Blade : EMechHitKind::Bullet;
					if (B.bKnife && Victim->GetShoulder() && Victim->GetShoulder()->BlocksFrom(Hit.ImpactPoint))
					{
						Victim->GetShoulder()->OnShieldBlocked(Hit.ImpactPoint);  // 飞刀也是远程：护盾挡得住
						Damage = 0.f;
					}
					const float Dealt = Damage > 0.f ? Victim->ReceiveHit(Damage, Impact, Mech, Hit.ImpactPoint, Kind, B.bKnife ? GetBlade().StaggerScale : -1.f) : 0.f;
					if (Dealt > 0.f)
					{
						ConfirmHit(Victim, bDirect, Kind, Dealt, Hit.ImpactPoint, Part);
						const float KnifeDist = FVector::Dist(Victim->GetActorLocation(), Mech->GetActorLocation());
						if (B.bKnife && KnifeDist < MechWeapons::KnifeCloseRange)
						{
							// 11-01 用户：击退随距离线性衰减，贴脸最大，到 KnifeCloseRange 正好没有
							const UMechCombatComponent* KnifeTarget = Victim->GetCombat();
							const float PushSpeed = MechWeapons::KnifeCloseKnockback * (1.f - KnifeDist / MechWeapons::KnifeCloseRange)
								* (KnifeTarget && KnifeTarget->IsGuarding() ? 0.2f : 1.f);
							MulticastKnifePush(Victim, (Victim->GetActorLocation() - Mech->GetActorLocation()).GetSafeNormal2D() * PushSpeed);
						}
						if (Run && !B.bKnife)
						{
							Run->OnBulletHitMech(Victim, B, Hit.ImpactPoint);  // 11-01 肉鸽：追踪弹、点射全中
						}
						if (!B.bKnife && G.Lifesteal > 0.f)
						{
							// 霰弹枪：按造成的伤害回复装甲（10-30 替换掉「击毁回 3000」：打中就回，不用等击毁）
							const float Steal = Dealt * (G.Lifesteal + (Mech->IsOwnMaker(MechLoadout::Shooter) ? MechWeapons::OwnLifesteal : 0.f));
							Mech->GetHealth()->Heal(Steal);
							PendingLifesteal += Steal;
						}
						// 霰弹枪：打中正在出刀、刀还没砍下的人 → 这一刀直接断掉（大剑霸体重斩除外，InterruptMelee 里判断）
						UMechCombatComponent* VC = Victim->GetCombat();
						if (!B.bKnife && G.bInterruptMelee && VC && VC->IsMeleeActive() && VC->GetMeleeTime() < VC->GetMeleeStrikeAt())
						{
							VC->InterruptMelee();
						}
					}
				}
				if (HitMechFX)
				{
					const float Size = B.bKnife ? 1.6f : 0.85f * FMath::Max(1.f, B.Width * 0.8f);
					UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitMechFX, Hit.ImpactPoint, Hit.ImpactNormal.Rotation(), FVector(Size), true, true, ENCPoolMethod::AutoRelease);
				}
				if (B.bKnife && MeleeFx && Victim->GetTeam() != Mech->GetTeam())
				{
					MeleeFx->OnKnifeHit(Victim, Hit.ImpactPoint, B.Vel.GetSafeNormal(), B.Scale > 1.2f);  // 扎中处烧红一小块
				}
				FlashLight(Hit.ImpactPoint + Hit.ImpactNormal * 40.f, Victim->GetHealth()->IsStaggered() ? 1600.f : 500.f, FLinearColor(1.f, 0.62f, 0.3f));
				const float HitNow = GetWorld()->GetTimeSeconds();
				if (MetalHitSounds.Num() && (B.bKnife || HitNow - LastMetalHitSound > 0.07f))
				{
					LastMetalHitSound = HitNow;
					PlayAt(MetalHitSounds[FMath::RandHelper(MetalHitSounds.Num())], Hit.ImpactPoint, B.bKnife ? 0.6f : 0.38f, 0.12f);
				}
			}
			else
			{
				// 打中场上能被打掉的东西（肩部装备的地雷，带 MechShootable 标签）：服务器上把这一发的伤害交给它自己（TakeDamage 里分敌我、扣血）
				if (B.bLive && Hit.GetActor() && Hit.GetActor()->ActorHasTag(TEXT("MechShootable")))
				{
					float ShootDamage = B.Damage;
					if (Run && !B.bKnife && Hit.GetActor()->ActorHasTag(TEXT("MechGrunt")))
					{
						ShootDamage = Run->OnBulletHitGrunt(Hit.GetActor(), B, ShootDamage, Hit.ImpactPoint);  // 11-01 肉鸽：杂兵也算连中、点射全中
					}
					UGameplayStatics::ApplyDamage(Hit.GetActor(), ShootDamage, nullptr, Mech, TSubclassOf<UDamageType>());
				}
				if (HitWorldFX)
				{
					UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitWorldFX, Hit.ImpactPoint, Hit.ImpactNormal.Rotation(), FVector(0.35f), true, true, ENCPoolMethod::AutoRelease);
				}
				if (FMath::FRand() < 0.4f)
				{
					PlayAt(GroundHitSound, Hit.ImpactPoint, 0.22f, 0.15f);
				}
			}
			Bullets.RemoveAtSwap(i);
			continue;
		}
		B.Traveled += B.Vel.Size() * DeltaTime;
		B.Pos = Next;
		B.Life -= DeltaTime;
		if (B.Life <= 0.f)
		{
			Bullets.RemoveAtSwap(i);
		}
	}
}

void UMechCombatComponent::UpdateTracers()
{
	if (!Tracers)
	{
		return;
	}
	const AMechCharacter* Mech = GetMech();
	const USceneComponent* Held = Mech ? Mech->GetMechMesh()->Props[0].Get() : nullptr;
	const FVector KnifeScale = Held ? Held->GetComponentScale() : FVector(1.f);
	TArray<FTransform> Lines;
	TArray<FTransform> KnifeXf;
	TArray<FTransform> StreakXf[2];
	for (const FMechBullet& B : Bullets)
	{
		const FVector Dir = B.Vel.GetSafeNormal();
		if (B.bKnife)
		{
			// 匕首刀尖朝前飞，绕飞行方向打转
			const float Spin = B.Traveled * 0.12f;
			const FQuat Q = FRotationMatrix::MakeFromX(Dir).ToQuat() * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Spin));
			KnifeXf.Add(FTransform(Q, B.Pos, KnifeScale * B.Scale));
			// 尾迹（10-31）：普通匕首一道细的冷白光，强力匕首一道更长更粗的橙光
			const bool bPowerKnife = B.Scale > 1.2f;
			const float SLen = FMath::Min(B.Traveled + 30.f, bPowerKnife ? 420.f : 260.f);
			const float SR = bPowerKnife ? 0.065f : 0.03f;
			StreakXf[bPowerKnife ? 1 : 0].Add(FTransform(FRotationMatrix::MakeFromZ(Dir).ToQuat(), B.Pos - Dir * (SLen * 0.5f + 15.f), FVector(SR, SR, SLen / 100.f)));
			continue;
		}
		const float Len = FMath::Min(B.Traveled + 60.f, 700.f);
		const FVector Center = B.Pos - Dir * (Len * 0.5f);
		const float R = 0.045f * B.Width;
		Lines.Add(FTransform(FRotationMatrix::MakeFromZ(Dir).ToQuat(), Center, FVector(R, R, Len / 100.f)));
	}
	// 每帧整批重摆，「上一帧位置」给成和这一帧一样：实例编号每帧对应的子弹会变（打出去、打中就增减），
	// 按编号记的上一帧位置会把新子弹和别处的旧子弹连成一条运动模糊 / 时域重建的长亮线，射速快时一闪一闪
	auto Rebuild = [](UInstancedStaticMeshComponent* Into, const TArray<FTransform>& Xf) {
		Into->ClearInstances();
		if (Xf.Num())
		{
			Into->AddInstances(Xf, false, true);
			Into->BatchUpdateInstancesTransforms(0, Xf, Xf, true, true, true);
		}
	};
	Rebuild(Tracers, Lines);
	if (KnifeMeshes)
	{
		Rebuild(KnifeMeshes, KnifeXf);
	}
	if (KnifeStreaks && PowerStreaks)
	{
		Rebuild(KnifeStreaks, StreakXf[0]);
		Rebuild(PowerStreaks, StreakXf[1]);
	}
}

void UMechCombatComponent::ConfirmHit(AMechCharacter* Victim, bool bDirect, EMechHitKind Kind, float Damage, const FVector& At, EMechHitPart Part)
{
	DebugDamage += Damage;
	DebugBaseDamage += bDirect ? Damage / FMath::Max(Victim->GetHealth()->DirectHitScale, 1.f) : Damage;
	ClientHitConfirm(bDirect, Victim->GetHealth()->IsDead(), Kind, Damage, At, Part);
}

// ---------------------------------------------------------------- 导弹
void UMechCombatComponent::UpdateMissiles(float DeltaTime)
{
	MissileTimer = FMath::Max(0.f, MissileTimer - DeltaTime);
	if (PendingMissiles <= 0)
	{
		return;
	}
	MissileSalvoTimer -= DeltaTime;
	if (MissileSalvoTimer <= 0.f)
	{
		LaunchOneMissile();
		--PendingMissiles;
		MissileSalvoTimer = 0.09f;
		if (PendingMissiles <= 0)
		{
			RunSalvo = 0;  // 11-01 肉鸽：这一轮打完，枚数回到照常
		}
	}
}

void UMechCombatComponent::LaunchOneMissile()
{
	AMechCharacter* Mech = GetMech();
	const int32 Tube = FMath::Abs(SalvoSize() - PendingMissiles) % 4;
	// 11-01 肉鸽：这一枚追谁、威力（重弹、全锁、锁杂兵）；没有 Run 时和以前完全一样
	AMechCharacter* MechAim = bSalvoLocked ? Target.Get() : nullptr;
	AActor* RunAim = nullptr;
	float ThisDamage = MissileDamage;
	float ThisImpact = MissileImpact;
	float ThisRadius = MissileRadius;
	float ThisScale = 1.f;
	float ThisStagger = -1.f;
	UMechRunComponent* Run = Mech->GetRun();
	if (Run)
	{
		Run->ModifyMissile(FMath::Abs(SalvoSize() - PendingMissiles), RunAim, MechAim, ThisDamage, ThisImpact, ThisRadius, ThisScale, ThisStagger);
	}
	const FVector LockPoint = RunAim ? RunAim->GetActorLocation() : MechAim ? MechAim->GetActorLocation() : FVector::ZeroVector;
	const bool bAimLocked = RunAim != nullptr || MechAim != nullptr;
	// 导弹巢是浮空无人机：判定用的发射管位置按无人机的理想位置（胶囊 + 瞄准方向，不带弹簧延迟）朝目标算，服务器上确定；
	// 各端画面上的无人机带弹簧延迟，发射特效和导弹刚出现时从本机看到的发射管出（MulticastMissileLaunch、AMechMissile::BeginPlay）
	const FVector AimAt = bAimLocked ? LockPoint : GetFreeAimPoint();
	const FVector At = Mech->GetShoulder()->LaunchPointToward(AimAt, PodTubes[Tube]);
	const FVector Fwd = Mech->GetActorForwardVector();
	const FVector Right = Mech->GetActorRightVector();
	FVector Dir;
	if (bAimLocked)
	{
		// 朝目标飞，几发左右各偏一点、稍微抬头，追踪时从不同方向收拢
		const FVector ToTarget = (LockPoint - At).GetSafeNormal();
		Dir = (ToTarget + Right * FMath::FRandRange(-0.35f, 0.35f) + FVector::UpVector * 0.08f).GetSafeNormal();
	}
	else
	{
		Dir = (GetFreeAimPoint() - At).GetSafeNormal();
		Dir = FMath::VRandCone(Dir, FMath::DegreesToRadians(2.5f));
	}
	if (PendingMissiles == SalvoSize())
	{
		UE_LOG(LogTemp, Display, TEXT("[missile] 发射 %s → %s 锁定 %d"), *Mech->DisplayName, Target ? *Target->DisplayName : TEXT("无"), bSalvoLocked ? 1 : 0);
	}
	const FTransform SpawnAt(Dir.Rotation(), At, FVector(ThisScale));
	AMechMissile* Missile = GetWorld()->SpawnActorDeferred<AMechMissile>(AMechMissile::StaticClass(), SpawnAt, Mech, Mech,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Missile)
	{
		Missile->Init(Mech, MechAim, ThisDamage, ThisImpact, ThisRadius);
		Missile->RunTarget = RunAim;
		Missile->RunStagger = ThisStagger;
		Missile->FinishSpawning(SpawnAt);
		Missile->Launch(Dir * (bSalvoLocked || RunAim ? 2400.f : 3600.f));  // 锁定中目标死了照旧按 2400（bSalvoLocked 还是真）
	}
	MulticastMissileLaunch(At, static_cast<uint8>(Tube));
}

void UMechCombatComponent::MulticastMissileLaunch_Implementation(FVector_NetQuantize At, uint8 Tube)
{
	// 发射焰从本机看到的无人机发射管出、朝无人机对着的方向（目标或准星）；无人机转过去、后坐一下
	FRotator FxRot = GetOwner()->GetActorRotation() + FRotator(40.f, 0.f, 0.f);
	if (AMechCharacter* Mech = GetMech(); Mech && Mech->GetShoulder())
	{
		UMechShoulderComponent* S = Mech->GetShoulder();
		const AMechCharacter* T = Target.Get();
		const FVector Dir = T && !T->GetHealth()->IsDead() ? (T->GetActorLocation() - S->GetDroneLocation()).GetSafeNormal() : GetPoseAimDirection();
		S->OnDroneFired(Dir);
		At = S->VisualPoint(Dir, PodTubes[Tube % 4]);
		FxRot = Dir.Rotation();
	}
	if (MissileLaunchFX)
	{
		SpawnMuzzleFX(this, MissileLaunchFX, At, FxRot, FVector(0.6f));
	}
	PlayAt(MissileLaunchSound, At, 0.7f, 0.1f);
	LastEngagedTime = GetWorld()->GetTimeSeconds();
	LastMissileTime = LastEngagedTime;
}

// ---------------------------------------------------------------- 刀
void UMechCombatComponent::MulticastMeleeStart_Implementation(EMechMelee Move, FVector_NetQuantizeNormal Dir, uint8 SpeedPct, uint8 Flags)
{
	if (!GetOwner()->HasAuthority())
	{
		MoveTimeScale = FMath::Max(SpeedPct, static_cast<uint8>(10)) / 100.f;
		MoveFlags = Flags;
		MeleeMove = Move;
		MeleeTime = 0.f;
		ClashCancelAt = -1.f;
		LungeDir = Dir;
		if (Flags & MoveFlagDash)
		{
			LastDodgeTime = -100.f;
		}
	}
	LastEngagedTime = GetWorld()->GetTimeSeconds();
	AMechCharacter* Mech = GetMech();
	if (SwingSounds.Num())
	{
		USoundBase* Swing = SwingSounds[FMath::RandHelper(SwingSounds.Num())];
		if (Swing)
		{
			// 挥刀声偏高、利落（用户：「刀剑的声音稍微清脆一点」）：轻击短促清亮，重斩稍低但不闷；重的刀（出招慢）低一点，双刀高一点
			const float Weight = 1.f / FMath::Sqrt(FMath::Max(GetBlade().TimeScale, 0.3f));
			UGameplayStatics::PlaySoundAtLocation(this, Swing, Mech->GetActorLocation(), FRotator::ZeroRotator, Move == EMechMelee::Heavy ? 0.85f : 0.65f,
				(Move == EMechMelee::Heavy ? 1.0f : 1.3f + FMath::FRandRange(-0.06f, 0.06f)) * Weight, 0.f, Attenuation);
		}
	}
	Mech->CameraKick(FVector(Dir), Move == EMechMelee::Heavy ? 0.35f : 0.16f);
	if (BladeTrail)
	{
		BladeTrail->Deactivate();
		BladeTrail = nullptr;
	}
	if (BladeTrail2)
	{
		BladeTrail2->Deactivate();
		BladeTrail2 = nullptr;
	}
	if (MeleeFx)
	{
		// 10-31 近战特效：扫掠面刀光、刀刃热光（MechMeleeFx），不再放粒子刀光
		MeleeFx->OnSwingStart(Move, (Flags & MoveFlagDash) != 0, (Flags & MoveFlagParry) != 0);
		return;
	}
	// 刀光挂在刀身中段（按这把刀的长短换算）
	const FVector TrailAt = BladeTrailAt * (GetBlade().Reach / 380.f);
	USceneComponent* Blade = Mech->GetMechMesh()->Props[0];
	if (BladeTrailFX && Blade)
	{
		BladeTrail = UNiagaraFunctionLibrary::SpawnSystemAttached(BladeTrailFX, Blade, NAME_None, TrailAt, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, true);
	}
	USceneComponent* Left = Mech->GetMechMesh()->Props[4];
	if (BladeTrailFX && Left && GetBlade().bTwin)
	{
		BladeTrail2 = UNiagaraFunctionLibrary::SpawnSystemAttached(BladeTrailFX, Left, NAME_None, TrailAt, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, true);
	}
}

void UMechCombatComponent::UpdateMelee(float DeltaTime)
{
	const bool bAuth = GetOwner()->HasAuthority();
	if (bAuth)
	{
		HeavyTimer = FMath::Max(0.f, HeavyTimer - DeltaTime);
		ComboWindow = FMath::Max(0.f, ComboWindow - DeltaTime);
	}
	if (MeleeMove == EMechMelee::None)
	{
		return;
	}
	const FMove S = CurrentMove(MeleeMove);
	const float Reach = GetBlade().Reach;
	AMechCharacter* Mech = GetMech();
	// 11-01 肉鸽 蓄力（不改动画的简单版）：招式时间停在重斩起手的那个姿势；不往前冲，站定、慢慢转向准星。松手（RunChargeHold 清零）后从这里接着砍
	if (RunChargeHold != 0)
	{
		MeleeTime = FMath::Min(MeleeTime + DeltaTime, S.StrikeAt * MechRun::ChargeHoldPose);
		if (bAuth || Mech->IsLocallyControlled())
		{
			UCharacterMovementComponent* HoldMove = Mech->GetCharacterMovement();
			const float Decay = FMath::Exp(-8.f * DeltaTime);
			HoldMove->Velocity.X *= Decay;
			HoldMove->Velocity.Y *= Decay;
			if (const AController* Ctrl = Mech->GetController())
			{
				TurnToward(Ctrl->GetControlRotation().Yaw, 240.f, DeltaTime);
			}
		}
		RunChargeClock += DeltaTime;
		if (AMechRunFx* Fx = AMechRunFx::Get(GetWorld()))
		{
			Fx->ChargeTick(Mech, FMath::Min(RunChargeClock / MechRun::ChargeFull, 1.f), Reach);
		}
		return;
	}
	const float Prev = MeleeTime;
	MeleeTime += DeltaTime;
	if (bAuth || Mech->IsLocallyControlled())
	{
		// 命中前往前送：第一下轻击和重斩按剩余距离调速度，命中那一刻刚好贴到刀的长度（服务器和操控者本地各算一份，联机不被拉回）
		UCharacterMovementComponent* Move = Mech->GetCharacterMovement();
		// 10-30 刀剑手感：突进速度曲线、砍下后的减速、转身都改了（DriveMelee）；-MechMeleeOld 用下面改之前的做法（对比用）
		static const bool bMeleeOld = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeOld"));
		if (!bMeleeOld)
		{
			DriveMelee(S, Reach, DeltaTime);
		}
		else if (MeleeTime < S.StrikeAt)
		{
			// 先快后慢：每帧走掉剩余距离的一大截，越接近越慢，刀落下时刚好贴到刀长。
			// 只管「朝目标 / 朝前」这一个方向的速度，玩家按的横向移动保留（10-30 用户：出刀时被锁住是大问题——以前整个速度被接管）；
			// 近战距离里有目标才追着它冲、转身对着它，没有就在原来的速度上加一点往前的步子
			const float TimeLeft = FMath::Max(0.04f, S.StrikeAt - MeleeTime);
			float Speed = S.Step * FMath::Clamp(1.6f * (1.f - MeleeTime / S.StrikeAt), 0.f, 1.6f);
			FVector Dir = LungeDir.GetSafeNormal2D();
			bool bChase = false;
			bool b3D = false;
			const bool bDash = (MoveFlags & MoveFlagDash) != 0;
			const bool bLunge = MeleeMove == EMechMelee::Light1 || MeleeMove == EMechMelee::Heavy || bDash;
			if (Target && !Target->GetHealth()->IsDead())
			{
				// 10-30 玩家：突进改成向着敌人、三维的——目标在上方 / 下方（高差超过 Lunge3DHeight）也直接冲过去，
				// 地面冲空中目标切成空中状态、冲刺中不受重力；高差不大时还是水平冲（平地上不往上跳）
				const FVector To = Target->GetActorLocation() - Mech->GetActorLocation();
				const float Dist = To.Size();
				if (Dist < LungeRange(MeleeMove))
				{
					b3D = FMath::Abs(To.Z) > Lunge3DHeight && (To.Z > 0.f || Move->IsFalling());
					Dir = b3D ? To.GetSafeNormal() : To.GetSafeNormal2D();
					Speed = FMath::Clamp(2.2f * (Dist - Reach * 0.6f) / TimeLeft, 0.f, bDash ? 8000.f : bLunge ? 6000.f : 1800.f);
					bChase = true;
				}
			}
			const FVector Flat(Move->Velocity.X, Move->Velocity.Y, 0.f);
			if (b3D)
			{
				// 三维突进：整个速度朝目标，横向只留一半（还能稍微绕一下）；朝上冲时从地面切到空中状态，不然走路状态会把向上的速度吃掉。
				// 每帧都把速度整个设一遍，重力只来得及作用一帧，等于冲刺中抵消了重力
				const FVector Dir2 = Dir.GetSafeNormal2D();
				const FVector Side = Dir2.IsNearlyZero() ? FVector::ZeroVector : Flat - Dir2 * (Flat | Dir2);
				if (Dir.Z > 0.15f && Move->IsMovingOnGround())
				{
					Move->SetMovementMode(MOVE_Falling);
				}
				Move->Velocity = Dir * Speed + Side * 0.5f;
			}
			else
			{
				const float AlongNow = Flat | Dir;
				const FVector Side = Flat - Dir * AlongNow;
				const float Along = bChase ? Speed : FMath::Max(Speed * 0.5f, AlongNow);
				Move->Velocity = FVector(Dir.X * Along + Side.X, Dir.Y * Along + Side.Y, FMath::Min(Move->Velocity.Z, 0.f));
			}
			if (bChase && !FVector(Dir.X, Dir.Y, 0.f).IsNearlyZero())
			{
				Mech->SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
			}
		}
		else if (MeleeTime < FMath::Min(S.Cancel, S.StrikeAt + 0.1f))
		{
			// 刀落下后只「钉」0.1 秒（快速减速，不是急停），之后照常能走；在空中砍到时竖直方向也一起减（挂一下再落）
			const float Keep = FMath::Exp(-DeltaTime * 10.f);
			Move->Velocity.X *= Keep;
			Move->Velocity.Y *= Keep;
			if (Move->IsFalling())
			{
				Move->Velocity.Z *= Keep;
			}
		}
		if (bAuth && Prev < S.StrikeAt && MeleeTime >= S.StrikeAt && !bMeleeHitDone)
		{
			bMeleeHitDone = true;
			MeleeStrikeAuthority();
		}
		if (bAuth && bQueuedHeavy && MeleeTime >= S.Cancel && IsLight(MeleeMove) && HeavyTimer <= 0.f)
		{
			StartMove(EMechMelee::Heavy);
			return;
		}
		if (bAuth && bQueuedLight && MeleeTime >= S.Cancel && MeleeMove >= EMechMelee::Light1 && MeleeMove < EMechMelee::Light4)
		{
			StartMove(static_cast<EMechMelee>(static_cast<uint8>(MeleeMove) + 1));
			return;
		}
	}
	if (MeleeTime >= S.Duration)
	{
		if (bAuth)
		{
			ComboWindow = (MeleeMove >= EMechMelee::Light1 && MeleeMove < EMechMelee::Light4) ? 0.35f : 0.f;
		}
		EndMelee();
	}
}

bool UMechCombatComponent::IsMeleeDriving() const
{
	static const bool bMeleeOld = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeOld"));
	// 11-01 拼刀被弹开也算：速度由 UpdateClashKnock 每帧设，移动组件别按「没有输入」刹车（地面每帧吃掉两成，弹不出几米）、别自己转向
	return ClashKnockClock >= 0.f || (!bMeleeOld && MeleeMove != EMechMelee::None && MeleeTime < CurrentMove(MeleeMove).Cancel);
}

namespace
{
	// 突进的速度曲线（10-30 刀剑手感），t 从这一招第一次设速度算起、T = 到命中时刻还有多久（速度在下一帧的移动里才生效，
	// 出招那一帧本来就走不了，曲线从第一次能设速度的那一帧开始）：
	//  · 加速段：开头 Ta（0.045–0.07 秒）从 35% 平滑加到满速——以前第一帧就从 0 跳到最快，看着是被弹出去的；
	//  · 减速段：最后 40% 的时间平滑减到 EndFrac（到达时 9 米/秒左右，最多三成）——以前按「剩余距离 × 2.2 / 剩余时间」
	//    一路减到 0，刀落下那一刻正好停死，砍下去没有分量；现在带着一点速度砍进去，之后由砍下后的减速收住（留一点惯性）。
	// 速度 = 峰值 × 曲线；峰值每帧按「剩余距离 ÷ 剩下这段曲线的面积」重算（目标在动、碰撞挡住都能追上）
	constexpr float LungeStartFrac = 0.35f;
	constexpr float LungeArriveSpeed = 900.f;
	float LungeShape(float t, float T, float EndFrac)
	{
		const float Ta = FMath::Clamp(0.25f * T, 0.045f, 0.07f);
		const float Td = 0.4f * T;
		auto Smooth = [](float X) { X = FMath::Clamp(X, 0.f, 1.f); return X * X * (3.f - 2.f * X); };
		const float Up = t < Ta ? LungeStartFrac + (1.f - LungeStartFrac) * Smooth(t / Ta) : 1.f;
		const float Down = t > T - Td ? 1.f - (1.f - EndFrac) * Smooth((t - (T - Td)) / Td) : 1.f;
		return FMath::Min(Up, Down);
	}
	/** 从 t 到 T 曲线下的面积（秒）：峰值速度 × 面积 = 还能走多远 */
	float LungeShapeArea(float t, float T, float EndFrac)
	{
		if (t >= T)
		{
			return 0.f;
		}
		constexpr int32 N = 16;
		const float Step = (T - t) / N;
		float Sum = 0.f;
		for (int32 i = 0; i < N; ++i)
		{
			Sum += LungeShape(t + (i + 0.5f) * Step, T, EndFrac);
		}
		return Sum * Step;
	}
}

void UMechCombatComponent::DriveMelee(const FMove& S, float Reach, float Dt)
{
	// 出刀时的速度和朝向（10-30 玩家：「突进 / 转向生硬：冲过去时突然加速或停住、机体瞬间转向敌人」）。
	// 以前：①第一帧速度直接设成最快，之后「剩余距离 × 2.2 / 剩余时间」一路减到 0，刀落下时正好停死，再「钉」0.1 秒；
	// ②追目标时每帧 SetActorRotation 直接对准目标（第一帧瞬间转过去）；③速度是战斗组件在物理之后设的，下一帧移动组件
	// 没有输入就按地面摩擦 + 刹车减掉一截（60 帧时每帧两成，帧率越低掉得越多），实际速度曲线和两端结果都跟帧率有关。
	// 现在：到可取消时刻之前速度整个由这里定（移动组件见 IsMeleeDriving，不再刹车、不再自己转向）：
	//  · 命中前按 LungeShape 的曲线冲（短加速段、减速段，带着约 9 米/秒砍进去），没目标就按同样的曲线往前送一小步；
	//    峰值上限 65 / 85（闪避斩）/ 18（贴身）米/秒：以前名义上 60 / 80 / 18，但被刹车吃掉后 60 帧时实际只有 48 / 64 / 14，
	//    新曲线有加速、减速段，峰值要高一点才能在同样的命中时刻冲到同样远（13–14 米的第一刀照样贴得上）；
	//  · 已经贴到（或很近、本来就在往前走）时不一刀切到 0：往前的速度最多按惯性（每秒 8 的指数）收；
	//  · 砍下后按每秒 8 的指数减速（0.1 秒剩 45%，以前 0.1 秒剩 37% 再被刹车吃掉），空中砍到时竖直方向挂 0.12 秒再落；
	//  · 原来的横向速度（不朝目标的那部分）也按每秒 8 收，不再一帧砍掉一半；
	//  · 转身：追目标时最快 1260°/秒、角加速度 16000°/秒²，快对准时按比例收（缓出）；没目标 / 砍下之后照旧朝瞄准方向转，最快 720°/秒（和移动组件平时一样）。
	// 服务器和操控者本地各算一份（同样的公式，只差帧时间），联机不被拉回的原则不变。
	AMechCharacter* Mech = GetMech();
	UCharacterMovementComponent* Move = Mech->GetCharacterMovement();
	if (MeleeTime >= S.Cancel)
	{
		MeleeYawRate = 0.f;  // 可取消之后交还移动组件
		return;
	}
	if (MeleeTime <= Dt + 1e-4f)
	{
		MeleeDriveT0 = MeleeTime;  // 这一招第一次设速度
	}
	const bool bDash = (MoveFlags & MoveFlagDash) != 0;
	const bool bLunge = MeleeMove == EMechMelee::Light1 || MeleeMove == EMechMelee::Heavy || bDash;
	const FVector Flat(Move->Velocity.X, Move->Velocity.Y, 0.f);
	const float Decay = FMath::Exp(-8.f * Dt);
	// 朝向：没目标时朝瞄准方向（玩家的镜头 / AI 的关注点），没有控制器（测试场木桩）就不转
	const AController* Ctrl = Mech->GetController();
	bool bHasWantYaw = Ctrl != nullptr;
	float WantYaw = Ctrl ? Ctrl->GetControlRotation().Yaw : 0.f;
	float TurnRate = 720.f;
	if (MeleeTime < S.StrikeAt)
	{
		FVector Dir = LungeDir.GetSafeNormal2D();
		bool bChase = false;
		bool b3D = false;
		float Dist = 0.f;
		if (Target && !Target->GetHealth()->IsDead())
		{
			const FVector To = Target->GetActorLocation() - Mech->GetActorLocation();
			Dist = To.Size();
			if (Dist < LungeRange(MeleeMove))
			{
				b3D = FMath::Abs(To.Z) > Lunge3DHeight && (To.Z > 0.f || Move->IsFalling());
				Dir = b3D ? To.GetSafeNormal() : To.GetSafeNormal2D();
				bChase = true;
			}
		}
		// 曲线的时间从第一次设速度算起（这一帧设的速度用在接下来这一帧的移动里，命中前最后一次设速度是在命中前一帧）
		const float Tau = MeleeTime - MeleeDriveT0;
		const float Span = FMath::Max(S.StrikeAt - MeleeDriveT0, 0.02f);
		float Speed;
		if (bChase)
		{
			// 砍下时离目标 0.6 个刀长（突进的再多留半米：砍下后带着惯性还要滑一段）。到达速度约 9 米/秒（不超过峰值三成）：
			// 先按三成算一次峰值，再按它定到达速度重算
			const float Arrive = Reach * 0.6f + (bLunge ? 50.f : 0.f);
			const float Left = FMath::Max(0.f, Dist - Arrive);
			const float Cap = bDash ? 8500.f : bLunge ? 6500.f : 1800.f;
			const float Peak0 = Left / FMath::Max(LungeShapeArea(Tau, Span, 0.3f), 0.004f);
			const float EndFrac = FMath::Clamp(LungeArriveSpeed / FMath::Max(FMath::Min(Peak0, Cap), 1.f), 0.12f, 0.3f);
			const float Peak = FMath::Min(Left / FMath::Max(LungeShapeArea(Tau, Span, EndFrac), 0.004f), Cap);
			Speed = Peak * LungeShape(Tau, Span, EndFrac);
		}
		else
		{
			// 没目标：往前送一小步，走的距离和以前一样（0.4 × Step × 命中时刻）
			Speed = 0.4f * S.Step * S.StrikeAt / FMath::Max(LungeShapeArea(0.f, Span, 0.3f), 0.01f) * LungeShape(Tau, Span, 0.3f);
		}
		if (b3D)
		{
			// 三维突进：整个速度朝目标；朝上冲时从地面切到空中状态，不然走路状态会把向上的速度吃掉。
			// 每帧都把速度整个设一遍，重力只来得及作用一帧，等于冲刺中抵消了重力
			const FVector Dir2 = Dir.GetSafeNormal2D();
			const FVector Side = Dir2.IsNearlyZero() ? FVector::ZeroVector : Flat - Dir2 * (Flat | Dir2);
			if (Dir.Z > 0.15f && Move->IsMovingOnGround())
			{
				Move->SetMovementMode(MOVE_Falling);
			}
			Move->Velocity = Dir * Speed + Side * Decay;
		}
		else
		{
			const float AlongNow = Flat | Dir;
			const FVector Side = Flat - Dir * AlongNow;
			// 往前的速度最多按惯性收（冲刺中出刀不先顿一下再加速；已经贴到目标时不一刀切到 0）。突进减速段（12 米/秒以上）不受这个限制，
			// 不然到达时太快、砍下后会一头撞上去
			const float Along = FMath::Max(Speed, FMath::Min(AlongNow, bChase ? 1200.f : AlongNow) * Decay);
			Move->Velocity = FVector(Dir.X * Along + Side.X * Decay, Dir.Y * Along + Side.Y * Decay, FMath::Min(Move->Velocity.Z, 0.f));
		}
		if (bChase && !FVector(Dir.X, Dir.Y, 0.f).IsNearlyZero())
		{
			WantYaw = Dir.Rotation().Yaw;
			bHasWantYaw = true;
			TurnRate = 1260.f;
		}
	}
	else
	{
		// 砍下后：带着惯性减速（以前 0.1 秒内按每秒 10 的指数「钉」住，再被地面刹车一下吃掉）；空中砍到时竖直方向挂一下再落
		Move->Velocity.X *= Decay;
		Move->Velocity.Y *= Decay;
		if (Move->IsFalling() && MeleeTime < S.StrikeAt + 0.12f)
		{
			Move->Velocity.Z *= FMath::Exp(-10.f * Dt);
		}
	}
	if (bHasWantYaw)
	{
		TurnToward(WantYaw, TurnRate, Dt);
	}
}

void UMechCombatComponent::TurnToward(float WantYaw, float MaxRate, float Dt)
{
	AMechCharacter* Mech = GetMech();
	const float Yaw = Mech->GetActorRotation().Yaw;
	const float Delta = FMath::FindDeltaAngleDegrees(Yaw, WantYaw);
	// 想要的角速度：离得远按上限转，快对准时按比例收（缓出，时间常数约 0.07 秒）；角速度本身按加速度上限变（缓入，0 → 1260 约 0.08 秒）
	const float WantRate = FMath::Clamp(Delta * 14.f, -MaxRate, MaxRate);
	MeleeYawRate = FMath::FInterpConstantTo(MeleeYawRate, WantRate, Dt, 16000.f);
	float Step = MeleeYawRate * Dt;
	if (Step * Delta > 0.f && FMath::Abs(Step) > FMath::Abs(Delta))
	{
		Step = Delta;  // 不转过头
	}
	if (FMath::Abs(Step) > 1e-3f)
	{
		Mech->SetActorRotation(FRotator(0.f, Yaw + Step, 0.f));
	}
}

void UMechCombatComponent::EndMelee()
{
	MeleeMove = EMechMelee::None;
	MeleeTime = 0.f;
	ClashCancelAt = -1.f;
	RunChargeClock = 0.f;
	if (GetOwner()->HasAuthority())
	{
		RunChargeHold = 0;
	}
	MeleeYawRate = 0.f;
	bQueuedLight = false;
	bQueuedHeavy = false;
	if (BladeTrail)
	{
		BladeTrail->Deactivate();
		BladeTrail = nullptr;
	}
	if (BladeTrail2)
	{
		BladeTrail2->Deactivate();
		BladeTrail2 = nullptr;
	}
}

void UMechCombatComponent::MeleeStrikeAuthority()
{
	AMechCharacter* Mech = GetMech();
	const FMove S = CurrentMove(MeleeMove);
	// 11-01 肉鸽 蓄力：这一刀的判定范围和伤害按蓄力放大；没有 Run 时两个倍数都是 1
	float RunReachScale = 1.f;
	float RunDamageScale = 1.f;
	UMechRunComponent* Run = Mech->GetRun();
	if (Run)
	{
		Run->OnStrikeBegin(MeleeMove, RunReachScale, RunDamageScale);
	}
	int32 RunHits = 0;
	const double HuajingNow = GetWorld()->GetTimeSeconds();
	if (HuajingNow - HuajingLastHit > MechWeapons::HuajingResetTime)
	{
		HuajingHits = 0;  // 2.5 秒没打中人，恢复满
	}
	HuajingSwingMult = GetHuajingMult();
	bool bHuajingHit = false;
	const float Reach = GetBlade().Reach * RunReachScale;
	const FVector Center = Mech->GetActorLocation() + Mech->GetActorForwardVector() * Reach * 0.55f;
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechBlade), false, Mech);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(Reach * 0.65f), Params);
	// 11-01 拼刀：先看够得着的敌人里有没有正好也在出刀、和自己面对面的：有就两刀相撞，这一刀到此为止（同时够着的别人、杂兵都不算砍中）。
	// 对方在格挡时没有在出刀（格挡要 MeleeMove 为空），照常走下面的弹反 / 格挡 / 破防
	for (const FOverlapResult& O : Overlaps)
	{
		AMechCharacter* Victim = Cast<AMechCharacter>(O.GetActor());
		if (Victim && Victim->GetTeam() != Mech->GetTeam() && !Victim->GetHealth()->IsDead() && TryBladeClash(Victim))
		{
			return;
		}
	}
	TSet<AMechCharacter*> Done;
	for (const FOverlapResult& O : Overlaps)
	{
		AMechCharacter* Victim = Cast<AMechCharacter>(O.GetActor());
		if (!Victim || Done.Contains(Victim) || Victim->GetTeam() == Mech->GetTeam() || Victim->GetHealth()->IsDead())
		{
			continue;
		}
		Done.Add(Victim);
		UMechCombatComponent* VC = Victim->GetCombat();
		bSwingLanded = true;
		const bool bDirect = Victim->GetHealth()->IsStaggered();
		const FVector At = (Victim->GetActorLocation() + Mech->GetActorLocation()) * 0.5f + FVector(0.f, 0.f, 40.f);
		// 告诉对方的格挡：是谁砍的（双方都要格挡反馈）、是不是钝器（打在格挡上直接破防）
		if (VC)
		{
			VC->IncomingAttacker = Mech;
			VC->bIncomingGuardBreak = BladeType == EMechMeleeWeapon::Blunt && MeleeMove == EMechMelee::Heavy;  // 11-01 晚 用户：钝器太强，轻击砍格挡走普通弹刀，只有重斩才破防
			VC->bLastIncomingBlocked = false;
		}
		float HuajingMul = 1.f;  // 化境：越打越低；打中失衡的敌人追加（所有制造商统一 x1.5）
		if (BladeType == EMechMeleeWeapon::Katana)
		{
			HuajingMul = HuajingSwingMult * (bDirect ? MechWeapons::HuajingDirect : 1.f);
		}
		const float Dealt = Victim->ReceiveHit(S.Damage * RunDamageScale * HuajingMul, S.Impact, Mech, At, EMechHitKind::Blade, GetBlade().StaggerScale);
		if (BladeType == EMechMeleeWeapon::Katana && Dealt > 0.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[huajing] 乘数 %.2f 直击 %d 伤害 %.0f"), HuajingMul, bDirect ? 1 : 0, Dealt);
		}
		const bool bBlocked = VC && VC->bLastIncomingBlocked;
		RunHits += Dealt > 0.f ? 1 : 0;
		if (VC)
		{
			VC->IncomingAttacker = nullptr;
			VC->bIncomingGuardBreak = false;
		}
		if (Dealt > 0.f)
		{
			bHuajingHit = true;
			ConfirmHit(Victim, bDirect, EMechHitKind::Blade, Dealt, Victim->GetActorLocation() + FVector(0.f, 0.f, 60.f));
			if (!bBlocked)
			{
				// 双刀割裂：给对方挂一层（受到伤害增加），快刀连斩：给自己叠一层（出刀更快、移速略升）；挡住的不算
				if (BladeType == EMechMeleeWeapon::TwinBlades && Victim->GetEffects())
				{
					Victim->GetEffects()->AddEffect(TEXT("Rend"), MechWeapons::RendTime * OwnScale(MechLoadout::Melee, MechWeapons::OwnRendTime), 1);
				}
				if (BladeType == EMechMeleeWeapon::QuickBlade && Mech->GetEffects())
				{
					Mech->GetEffects()->AddEffect(TEXT("Flurry"), MechWeapons::FlurryTime * OwnScale(MechLoadout::Melee, MechWeapons::OwnFlurryTime), 1);
				}
			}
		}
		if (bBlocked && !Victim->GetHealth()->IsDead())
		{
			// 11-01 晚 格挡弹刀：挡住了（耗能、掉血在 TryBlock 和上面的 ReceiveHit 里算完了），这一刀作废、连招断掉，同时够着的别人、杂兵也不算砍中
			SettleClash(Victim, true);
			return;
		}
		if (!bBlocked)
		{
			// 重斩砍中（没被挡住）= 打出重击：反馈加一档；配的是双持手枪的话弹匣立即补满（10-30 晚玩家：鼓励刀枪切换），一刀只补一次
			const bool bHeavyHit = MeleeMove == EMechMelee::Heavy && Dealt > 0.f;
			const MechWeapons::FGun& G = GetGun();
			const bool bRefill = bHeavyHit && !bRefilledThisSwing && G.bHeavyRefill && G.Magazine > 0;
			if (bRefill)
			{
				bRefilledThisSwing = true;
				Ammo = GetMagazine();
				ReloadTimer = 0.f;
			}
			MulticastBladeImpact(At, S.Impact, bHeavyHit, bRefill, Victim);  // 挡住时的火花、声音、顿帧走 MulticastBlocked
		}
	}
	// 保卫战的杂兵（10-31，MechGrunt.h）：不是机体，碰撞球是 WorldDynamic 的「打得中的球」，伤害按 ApplyDamage 交给它自己（同子弹打地雷）
	TArray<FOverlapResult> Props;
	GetWorld()->OverlapMultiByObjectType(Props, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldDynamic), FCollisionShape::MakeSphere(Reach * 0.65f), Params);
	TSet<AActor*> DoneProps;
	for (const FOverlapResult& O : Props)
	{
		AActor* A = O.GetActor();
		if (!A || DoneProps.Contains(A) || !A->ActorHasTag(TEXT("MechGrunt")))
		{
			continue;
		}
		DoneProps.Add(A);
		if (UGameplayStatics::ApplyDamage(A, S.Damage * RunDamageScale, Mech->GetController(), Mech, UDamageType::StaticClass()) > 0.f)
		{
			++RunHits;
			MulticastBladeImpact(A->GetActorLocation(), S.Impact, MeleeMove == EMechMelee::Heavy, false, nullptr);
		}
	}
	bRefilledThisSwing = false;
	if (bHuajingHit && BladeType == EMechMeleeWeapon::Katana)
	{
		++HuajingHits;  // 这一刀打中人（不论几个），倍数降一档
		HuajingLastHit = HuajingNow;
	}
	if (Run)
	{
		Run->OnStrikeEnd(MeleeMove, S.Damage, Reach, RunHits);  // 11-01 肉鸽：地裂冲击波、横贯一刀
	}
}

// ---------------------------------------------------------------- 拼刀（数值在 MechWeapons::Clash*）
bool UMechCombatComponent::TryBladeClash(AMechCharacter* Victim)
{
	AMechCharacter* Mech = GetMech();
	UMechCombatComponent* VC = Victim->GetCombat();
	// 对方要真的在出刀：蓄力（肉鸽）还没砍下去的不算，这一刀已经砍中过人的也不算（刀已经过去了）；失衡 / 僵直的人刀早被打断，这里再防一手
	if (!VC || !VC->IsMeleeActive() || VC->RunChargeHold != 0 || VC->bSwingLanded || Victim->GetHealth()->IsStaggered() || Mech->GetHealth()->IsStaggered())
	{
		return false;
	}
	// 对方的刀正处于落下前后的窗口：早砍中的一方先结算（错开时间就是普通命中）
	if (FMath::Abs(VC->MeleeTime - VC->GetMeleeStrikeAt()) > MechWeapons::ClashWindow)
	{
		return false;
	}
	// 两人大致面对面：侧面、背后砍过去是砍人，不是拼刀
	const FVector To = (Victim->GetActorLocation() - Mech->GetActorLocation()).GetSafeNormal2D();
	const float Cone = FMath::Cos(FMath::DegreesToRadians(MechWeapons::ClashAngle));
	if (FVector::DotProduct(Mech->GetActorForwardVector().GetSafeNormal2D(), To) < Cone
		|| FVector::DotProduct(Victim->GetActorForwardVector().GetSafeNormal2D(), -To) < Cone)
	{
		return false;
	}
	SettleClash(Victim, false);
	return true;
}

void UMechCombatComponent::SettleClash(AMechCharacter* Victim, bool bGuard)
{
	AMechCharacter* Mech = GetMech();
	UMechCombatComponent* VC = Victim->GetCombat();
	// 结果（11-01 用户：只会讲两人弹开，没有弱势优势一说，匕首除外）：两刀都作废。一般两人都弹开；匕首和别的刀拼只弹匕首方，匕首对匕首两人都弹
	const bool bMyDagger = GetBlade().Knives > 0;
	const bool bItsDagger = VC->GetBlade().Knives > 0;
	const bool bKnockMe = bMyDagger || !bItsDagger;
	const bool bKnockIt = bItsDagger || !bMyDagger;
	// 日志分开写，好分开数（-MechAutoBattle 量频率）
	if (bGuard)
	{
		UE_LOG(LogTemp, Display, TEXT("[clash] 格挡弹刀：%s（%s 第 %d 招）砍在 %s（%s）的格挡上：弹开 %s %s"), *Mech->DisplayName, GetBlade().Name, static_cast<int32>(MeleeMove),
			*Victim->DisplayName, VC->GetBlade().Name, bKnockMe ? *Mech->DisplayName : TEXT(""), bKnockIt ? *Victim->DisplayName : TEXT(""));
		VC->bNoParryUntilRelease = true;
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[clash] 同时出刀：%s（%s 第 %d 招，已砍下）× %s（%s 第 %d 招，离落下 %+.2f 秒）：弹开 %s %s"), *Mech->DisplayName, GetBlade().Name, static_cast<int32>(MeleeMove),
			*Victim->DisplayName, VC->GetBlade().Name, static_cast<int32>(VC->MeleeMove), VC->GetMeleeStrikeAt() - VC->MeleeTime,
			bKnockMe ? *Mech->DisplayName : TEXT(""), bKnockIt ? *Victim->DisplayName : TEXT(""));
	}
	const FVector At = (Mech->GetActorLocation() + Victim->GetActorLocation()) * 0.5f + FVector(0.f, 0.f, 60.f);
	auto Settle = [&At](UMechCombatComponent* C, AMechCharacter* Self, AMechCharacter* Other, bool bKnock, bool bFx) {
		C->bMeleeHitDone = true;  // 这一刀不再结算（还没砍下的那一方）
		C->bSwingLanded = true;
		C->RunChargeLevel = 0.f;  // 肉鸽蓄力：这一刀作废，蓄的也没了（地裂冲击波在 OnStrikeEnd，这里到不了）
		if (bKnock)
		{
			C->MulticastInterrupt();  // 不走 InterruptMelee：大剑霸体重斩也要收招（用户：没有优势，霸体不例外）
		}
		C->MulticastClash(At, Other, static_cast<uint8>((bKnock ? ClashFlagKnocked : 0) | (bFx ? ClashFlagFx : 0)));
		if (bKnock)
		{
			Self->GetHealth()->Stun(MechWeapons::ClashStun, true);  // 放在组播后面：同一条可靠通道里，客户端先开始弹开再收到僵直
		}
	};
	Settle(this, Mech, Victim, bKnockMe, true);  // 火花、声音这些场面上的东西只放一份
	Settle(VC, Victim, Mech, bKnockIt, false);
}

void UMechCombatComponent::MulticastClash_Implementation(FVector_NetQuantize At, AMechCharacter* Other, uint8 Flags)
{
	AMechCharacter* Mech = GetMech();
	if (!Mech)
	{
		return;
	}
	const bool bKnocked = (Flags & ClashFlagKnocked) != 0;
	FVector Away = Other ? (Mech->GetActorLocation() - Other->GetActorLocation()).GetSafeNormal2D() : FVector::ZeroVector;
	if (Away.IsNearlyZero())
	{
		Away = -Mech->GetActorForwardVector().GetSafeNormal2D();  // 正好叠在一起 / 对方这一端还没有：朝身后弹
	}
	ClashFxTime = GetWorld()->GetTimeSeconds();
	++DebugClashes;
	if (bGuarding)
	{
		SinceBlock = 0.f;  // 格挡弹刀不走 MulticastBlocked，格挡挨了一下的动作和准星旁的闪白在这里记
	}
	if (!GetOwner()->HasAuthority())
	{
		UE_LOG(LogTemp, Display, TEXT("[clash] 客户端 %.2f：%s 收到拼刀（弹开 %d，放场面 %d，本机操控 %d）"), ClashFxTime, *Mech->DisplayName, bKnocked ? 1 : 0, (Flags & ClashFlagFx) ? 1 : 0, Mech->IsLocallyControlled() ? 1 : 0);
	}
	// 顿帧在前：顿帧期间组件的时钟也停着，火花炸开那一下定格，顿完才飞出去
	Mech->HitStop(bKnocked ? MechWeapons::ClashStop : MechWeapons::ClashStopSafe);
	Mech->CameraKick(Away, bKnocked ? MechWeapons::ClashKick : MechWeapons::ClashKick * 0.3f);
	Mech->AddRecoil(bKnocked ? MechWeapons::ClashShake : MechWeapons::ClashShake * 0.3f);
	if (bKnocked)
	{
		Mech->PlayClashRecoil(-Away);
		// 服务器和操控者本地各走一份（同一条曲线、同一个方向，和突进一样），联机不被拉回
		if (GetOwner()->HasAuthority() || Mech->IsLocallyControlled())
		{
			ClashKnockDir = Away;
			ClashKnockDist = GetBlade().Knives > 0 ? MechWeapons::ClashKnockDagger : MechWeapons::ClashKnockDistance;
			ClashKnockClock = 0.f;
			ClashKnockAccum = 0.f;
			ClashKnockFrom = Mech->GetActorLocation();
			UCharacterMovementComponent* Move = Mech->GetCharacterMovement();
			if (GetOwner()->HasAuthority() && !Mech->IsLocallyControlled() && Mech->IsPlayerControlled())
			{
				// 别的玩家操控的这台：服务器这段时间照他的位置算，不纠正（引擎给「极高速移动一小段」留的开关，用完要关）
				Move->bIgnoreClientMovementErrorChecksAndCorrection = true;
				Move->bServerAcceptClientAuthoritativePosition = true;
				ClashTrustTimer = MechWeapons::ClashStun + MechWeapons::ClashNetTrust;
			}
			// 速度不在这里设，由移动组件的子步（ClashKnockSubstep）设：客户端同一帧要是还收到了位置纠正，移动组件接下来要把没确认的移动重放一遍（往返那么长时间的），
			// 这时速度已经是 54 米/秒就会多冲十几米（往返 300 毫秒测出来 26 米）。重放时不走子步（见 CalcVelocity）
		}
	}
	else
	{
		// 和匕首拼上没被弹开的一方：这一刀已经作废，但人不受影响——动作接着放完，随时能取消（接下一招、闪避、切枪）；举着格挡的就接着举
		if (MeleeMove != EMechMelee::None)
		{
			bMeleeHitDone = true;
			ClashCancelAt = MeleeTime;
		}
	}
	if (Flags & ClashFlagFx)
	{
		PlayClashFx(At, Other);
	}
}

float UMechCombatComponent::ClashKnockSpeed(float Time) const
{
	const float Left = FMath::Max(0.f, 1.f - Time / MechWeapons::ClashKnockTime);
	return 3.f * ClashKnockDist / MechWeapons::ClashKnockTime * Left * Left;  // 积分正好是 ClashKnockDist
}

void UMechCombatComponent::ClashKnockSubstep(float Dt, FVector& InOutVelocity)
{
	ClashKnockAccum += Dt;
	const float Speed = ClashKnockSpeed(ClashKnockClock + ClashKnockAccum);
	InOutVelocity.X = ClashKnockDir.X * Speed;
	InOutVelocity.Y = ClashKnockDir.Y * Speed;
}

void UMechCombatComponent::UpdateClashKnock(float DeltaTime)
{
	if (ClashTrustTimer > 0.f)
	{
		ClashTrustTimer -= DeltaTime;
		if (ClashTrustTimer <= 0.f)
		{
			UCharacterMovementComponent* TrustMove = GetMech()->GetCharacterMovement();
			TrustMove->bIgnoreClientMovementErrorChecksAndCorrection = false;
			TrustMove->bServerAcceptClientAuthoritativePosition = false;
		}
	}
	if (ClashKnockClock < 0.f)
	{
		return;
	}
	if (ClashKnockClock == 0.f)
	{
		UCharacterMovementComponent* FirstMove = GetMech()->GetCharacterMovement();
		if (!FirstMove->IsMovingOnGround())
		{
			FirstMove->Velocity.Z = MechWeapons::ClashAirLift;
		}
	}
	ClashKnockClock += DeltaTime;
	ClashKnockAccum = 0.f;
	// 僵直结束就交还移动组件（能动了就该听玩家的）；按时间算，不看 IsStunned：客户端上僵直状态比组播晚到，第一帧可能还是假
	if (ClashKnockClock >= FMath::Min(MechWeapons::ClashKnockTime, MechWeapons::ClashStun) || GetMech()->GetHealth()->IsDead())
	{
		ClashKnockClock = -1.f;
		UE_LOG(LogTemp, Display, TEXT("[clash] %s %.2f：%s 僵直结束，这段弹开走了 %.1f 米"), GetOwner()->HasAuthority() ? TEXT("服务器") : TEXT("客户端"), GetWorld()->GetTimeSeconds(), *GetMech()->DisplayName,
			FVector::Dist2D(GetMech()->GetActorLocation(), ClashKnockFrom) / 100.f);
		UCharacterMovementComponent* Move = GetMech()->GetCharacterMovement();
		if (Move->IsFalling())
		{
			// 空中没有地面摩擦，剩的速度（约 8.6 米/秒）能再飘 6 米：先收掉一半，别在空中比地面多弹一大截
			Move->Velocity.X *= 0.5f;
			Move->Velocity.Y *= 0.5f;
		}
		return;
	}
	// 移动组件的子步会按曲线重算；这里再设一次是给还没走子步的那一下（速度读数、动作用）
	UCharacterMovementComponent* KnockMove = GetMech()->GetCharacterMovement();
	const float KnockSpeed = ClashKnockSpeed(ClashKnockClock);
	KnockMove->Velocity.X = ClashKnockDir.X * KnockSpeed;
	KnockMove->Velocity.Y = ClashKnockDir.Y * KnockSpeed;
}

void UMechCombatComponent::PlayClashFx(const FVector& Mid, AMechCharacter* Other)
{
	AMechCharacter* Mech = GetMech();
	if (GetNetMode() == NM_DedicatedServer || !Mech)
	{
		return;
	}
	// 火花放在两把刀最近的地方（各端按自己看到的刀算，这一帧的刀还在刀光记录里）；动作没对上、离得太远就放在两人中间
	FVector Spark = Mid;
	UMechCombatComponent* OC = Other ? Other->GetCombat() : nullptr;
	FVector BaseA, TipA, BaseB, TipB;
	if (MeleeFx && OC && OC->MeleeFx && MeleeFx->GetBladeNow(BaseA, TipA) && OC->MeleeFx->GetBladeNow(BaseB, TipB))
	{
		FVector OnA, OnB;
		FMath::SegmentDistToSegmentSafe(BaseA, TipA, BaseB, TipB, OnA, OnB);
		const FVector Cross = (OnA + OnB) * 0.5f;
		if (FVector::Dist(Cross, Mid) < 300.f)
		{
			Spark = Cross;
		}
	}
	const FVector Axis = Other ? (Other->GetActorLocation() - Mech->GetActorLocation()).GetSafeNormal() : Mech->GetActorForwardVector();
	const FVector Side = FVector::CrossProduct(Axis, FVector::UpVector).GetSafeNormal();
	// 一大团：两束金属火花朝两侧喷（比弹反的 2.8 大），中间一团刀光爆点（和弹反一样 0.9；放大了黑烟太多，看着像爆炸不像金属相撞）；主要的火星在 OnClash 里
	if (HitMechFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitMechFX, Spark, Side.Rotation(), FVector(3.4f), true, true, ENCPoolMethod::AutoRelease);
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitMechFX, Spark, (-Side).Rotation(), FVector(3.4f), true, true, ENCPoolMethod::AutoRelease);
	}
	if (BladeHitFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BladeHitFX, Spark, Axis.Rotation(), FVector(0.9f), true, true, ENCPoolMethod::AutoRelease);
	}
	FlashLight(Spark, 18000.f, FLinearColor(1.f, 0.88f, 0.72f));
	if (MeleeFx)
	{
		MeleeFx->OnClash(Spark, Axis);
	}
	// 金属撞金属：弹反那声「叮」和余音压低音调（更沉、更响），叠一下金属撞击，再垫一点低频，听得出是两把大刀砸在一起
	auto Play = [&](USoundBase* Sound, float Volume, float Pitch) {
		if (Sound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, Sound, Spark, FRotator::ZeroRotator, Volume, Pitch + FMath::FRandRange(-0.03f, 0.03f), 0.f, Attenuation);
		}
	};
	Play(ParryTingSound, 1.f, 0.74f);
	Play(ParryRingSound, 0.85f, 0.72f);
	Play(MetalHitSounds.Num() ? MetalHitSounds[FMath::RandHelper(MetalHitSounds.Num())].Get() : nullptr, 0.9f, 0.8f);
	Play(HeavyHitSound, 0.4f, 0.75f);
}

float UMechCombatComponent::GetHuajingMult() const
{
	const int32 Hits = GetWorld() && GetWorld()->GetTimeSeconds() - HuajingLastHit > MechWeapons::HuajingResetTime ? 0 : HuajingHits;
	return FMath::Max(MechWeapons::HuajingMinMult, MechWeapons::HuajingFirstMult - MechWeapons::HuajingStep * Hits);
}

bool UMechCombatComponent::TryBlock(const FVector& From, float& Damage, float& Impact)
{
	AMechCharacter* Mech = GetMech();
	bLastIncomingBlocked = false;
	if (!bGuarding || !Mech)
	{
		return false;
	}
	const FVector To = (From - Mech->GetActorLocation()).GetSafeNormal2D();
	if (FVector::DotProduct(Mech->GetActorForwardVector().GetSafeNormal2D(), To) < FMath::Cos(FMath::DegreesToRadians(GuardAngle)))
	{
		return false;  // 侧面、背后挡不住
	}
	AMechCharacter* By = IncomingAttacker.Get();
	if (bIncomingGuardBreak)
	{
		// 钝器重斩砍在格挡上：直接破防（不管能量够不够），这一下照吃全额，一段时间不能再挡
		GuardBreakTimer = MechWeapons::GuardBreakTime * (By && By->IsOwnMaker(MechLoadout::Melee) ? MechWeapons::OwnGuardBreak : 1.f);
		bGuarding = false;
		MulticastBlocked(From, BlockBreak, By);
		return false;
	}
	UMechMovementComponent* Move = Mech->GetMechMovement();
	const float Cost = GuardEnergyCost + Impact / 40.f;
	if (Move->Energy < Cost)
	{
		// 能量不够：破防，这一下照吃，短时间不能再挡
		Move->Energy = 0.f;
		GuardBreakTimer = 1.2f;
		bGuarding = false;
		MulticastBlocked(From, BlockBreak, By);
		return false;
	}
	Move->Energy -= Cost;
	Damage *= GuardDamageScale;
	bLastIncomingBlocked = By != nullptr;
	if (By)
	{
		// 11-01 晚 用户：格挡架势挨刀 = 弹刀（砍的一方接着 SettleClash）。反馈走拼刀那套，这里不放格挡反馈，别叠两份；不积累失衡，冲击清零
		Impact = 0.f;
		return true;
	}
	Impact *= GuardImpactScale;
	MulticastBlocked(From, BlockNormal, By);
	return true;
}

void UMechCombatComponent::MulticastBlocked_Implementation(FVector_NetQuantize At, uint8 Kind, AMechCharacter* By)
{
	// 格挡反馈（10-30 玩家：挡住刀时双方都要有明确反馈，弹反更强一档，破防要和格挡分得开）：
	//  挡住：冷白火花 + 「刀打盾」+ 双方轻微顿帧和镜头冲击；弹反：更大更亮的火花 + 清脆的「叮」和金属余音 + 更长的顿帧，HUD 准星旁一圈金色；
	//  破防：红橙火花 + 金属碎裂声，挡的一方镜头被砸一下。子弹打在格挡上（By 为空）只有小火花和声音，不顿帧
	AMechCharacter* Mech = GetMech();
	const bool bMelee = By != nullptr;
	if (Kind != BlockBreak)
	{
		SinceBlock = 0.f;
	}
	const FVector Spark = Mech->GetActorLocation() + (At - Mech->GetActorLocation()).GetSafeNormal() * 160.f + FVector(0.f, 0.f, 60.f);
	static const float SparkSize[3] = {1.6f, 2.8f, 2.4f};
	static const float LightLevel[3] = {3500.f, 9000.f, 6000.f};
	static const FLinearColor LightColor[3] = {FLinearColor(0.7f, 0.85f, 1.f), FLinearColor(1.f, 0.9f, 0.6f), FLinearColor(1.f, 0.32f, 0.15f)};
	const int32 K = FMath::Clamp<int32>(Kind, 0, 2);
	if (HitMechFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitMechFX, Spark, (At - Spark).Rotation(), FVector(bMelee ? SparkSize[K] : 1.2f), true, true,
			ENCPoolMethod::AutoRelease);
	}
	if (K == BlockParry && BladeHitFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BladeHitFX, Spark, (At - Spark).Rotation(), FVector(0.9f), true, true, ENCPoolMethod::AutoRelease);
	}
	FlashLight(Spark, bMelee ? LightLevel[K] : 2500.f, LightColor[K]);
	if (MeleeFx)
	{
		MeleeFx->OnBlocked(Spark, K, bMelee ? By : nullptr);  // 金属反光、弹反金环、破防红环（10-31 近战特效）
	}
	auto Play = [&](USoundBase* Sound, float Volume, float Pitch) {
		if (Sound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, Sound, Spark, FRotator::ZeroRotator, Volume, Pitch + FMath::FRandRange(-0.04f, 0.04f), 0.f, Attenuation);
		}
	};
	USoundBase* Metal = MetalHitSounds.Num() ? MetalHitSounds[FMath::RandHelper(MetalHitSounds.Num())].Get() : nullptr;
	if (!bMelee)
	{
		Play(Metal, 0.55f, 1.4f);
	}
	else if (K == BlockParry)
	{
		Play(ParryTingSound, 0.9f, 1.f);
		Play(ParryRingSound, 0.7f, 1.05f);
		Play(GuardBlockSound, 0.6f, 0.8f);
	}
	else if (K == BlockBreak)
	{
		Play(GuardBreakSound, 0.85f, 0.95f);
		Play(Metal, 0.6f, 0.9f);
	}
	else
	{
		Play(GuardBlockSound ? GuardBlockSound.Get() : Metal, 0.8f, 1.1f);
	}
	if (!bMelee)
	{
		return;
	}
	// 双方：顿帧、镜头冲击、HUD 提示（挡的一方 = Mech，砍的一方 = By）
	static const float Stop[3] = {0.05f, 0.1f, 0.07f};
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector Toward = (By->GetActorLocation() - Mech->GetActorLocation()).GetSafeNormal2D();
	BlockFxTime = Now;
	BlockFxKind = Kind;
	bBlockFxDefender = true;
	Mech->HitStop(Stop[K]);
	Mech->CameraKick(-Toward, K == BlockBreak ? 0.5f : K == BlockParry ? 0.18f : 0.15f);
	if (UMechCombatComponent* BC = By->GetCombat())
	{
		BC->BlockFxTime = Now;
		BC->BlockFxKind = Kind;
		BC->bBlockFxDefender = false;
	}
	By->HitStop(Stop[K] * (K == BlockParry ? 1.6f : 1.f));  // 被弹反的一方顿得更久
	By->CameraKick(Toward, K == BlockParry ? 0.45f : 0.15f);
}

void UMechCombatComponent::MulticastBladeImpact_Implementation(FVector_NetQuantize At, float Impact, bool bHeavy, bool bRefill, AMechCharacter* Victim)
{
	// 重斩砍中 = 打出重击（10-30 晚玩家：重击的反馈要明显一点；轻击上一轮嫌屏幕抖得厉害，保持减弱，只给重击加一档）：
	// 更大的刀光爆点和火花、厚重的低频（小爆炸声压低）、顿帧 0.12 秒（轻击最多 0.09）、攻击者镜头一下短促的冲击（不到色散那档），
	// HUD 准星处重击专用的命中图形（UMechCombatComponent::GetLastHeavyHitTime）。和格挡 / 弹反 / 破防（MulticastBlocked）分开
	const float Now = GetWorld()->GetTimeSeconds();
	if (bHeavy)
	{
		LastHeavyHitTime = Now;
		if (BladeHitFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BladeHitFX, At, GetOwner()->GetActorRotation(), FVector(2.4f), true, true, ENCPoolMethod::AutoRelease);
		}
		if (HeavyHitSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, HeavyHitSound, At, FRotator::ZeroRotator, 0.6f, 0.8f + FMath::FRandRange(-0.04f, 0.04f), 0.f, Attenuation);
		}
		if (ThumpSound)
		{
			PlayAt(ThumpSound, At, 0.85f, 0.03f);
		}
	}
	// 双持手枪被这一刀补满：上膛声 + HUD「装满」图形
	if (bRefill)
	{
		LastRefillTime = Now;
		if (ReloadSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, ReloadSound, GetOwner()->GetActorLocation(), FRotator::ZeroRotator, 0.9f, 1.25f, 0.f, Attenuation);
		}
	}
	// 轻击、第四段、重斩分档：特效大小、点光亮度、顿帧长短、镜头冲击都跟着冲击走
	const float Heavy01 = FMath::GetMappedRangeValueClamped(FVector2D(200.f, 950.f), FVector2D(0.f, 1.f), Impact);
	if (BladeHitFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BladeHitFX, At, GetOwner()->GetActorRotation(), FVector(0.8f + 0.7f * Heavy01), true, true,
			ENCPoolMethod::AutoRelease);
	}
	// 火花顺着刀走的方向甩出去（10-31 近战特效；以前一律朝机体正前方）
	const FVector SparkDir = MeleeFx && !MeleeFx->GetSwingDir().IsNearlyZero() ? (MeleeFx->GetSwingDir() + GetOwner()->GetActorForwardVector() * 0.5f).GetSafeNormal()
		: GetOwner()->GetActorForwardVector();
	if (HitMechFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitMechFX, At, SparkDir.Rotation(), FVector(1.8f + 1.2f * Heavy01), true, true,
			ENCPoolMethod::AutoRelease);
	}
	if (MeleeFx)
	{
		MeleeFx->OnBladeHit(Victim, At, Heavy01, bHeavy);  // 刀痕、冲击环、尘土
	}
	// 刀砍中：刀声调高一点，再叠一层高频的金属「铮」，清脆；第四段、重斩垫的低频闷响减弱
	if (BladeHitSounds.Num() && BladeHitSounds[0])
	{
		UGameplayStatics::PlaySoundAtLocation(this, BladeHitSounds[FMath::RandHelper(BladeHitSounds.Num())], At, FRotator::ZeroRotator, 0.75f,
			1.22f + FMath::FRandRange(-0.05f, 0.05f), 0.f, Attenuation);
	}
	if (MetalHitSounds.Num() && MetalHitSounds[0])
	{
		UGameplayStatics::PlaySoundAtLocation(this, MetalHitSounds[FMath::RandHelper(MetalHitSounds.Num())], At, FRotator::ZeroRotator, 0.55f,
			1.6f + FMath::FRandRange(-0.08f, 0.08f), 0.f, Attenuation);
	}
	if (Heavy01 > 0.5f && ThumpSound)
	{
		PlayAt(ThumpSound, At, 0.5f, 0.05f);
	}
	FlashLight(At, bHeavy ? 13000.f : FMath::Lerp(3500.f, 9000.f, Heavy01), FLinearColor(1.f, 0.75f, 0.5f));
	if (AMechCharacter* Mech = GetMech())
	{
		Mech->HitStop(bHeavy ? 0.12f : FMath::Lerp(0.04f, 0.09f, Heavy01));  // 10-30 用户：顿帧稍微短一点（原 0.07–0.16）；重击 0.12
		// 砍中时攻击者自己的镜头（10-30 晚玩家：挥刀命中时屏幕抖得太厉害）：抖动 0.3–0.6 → 0.08–0.18，镜头冲击 0.4–1 → 0.12–0.32
		// （最重的一刀才有一点视野收缩，不再有色散）；被砍的一方、开枪的镜头不变。重击单独一档：冲击 0.45（视野收一下，不到色散）、抖动 0.22
		Mech->AddRecoil(bHeavy ? 0.22f : FMath::Lerp(0.08f, 0.18f, Heavy01));
		Mech->CameraKick(Mech->GetActorForwardVector(), bHeavy ? 0.45f : FMath::Lerp(0.12f, 0.32f, Heavy01));
	}
}

void UMechCombatComponent::GetEffectSystems(TArray<UNiagaraSystem*>& Out) const
{
	for (UNiagaraSystem* S : {MuzzleFX.Get(), HitMechFX.Get(), HitWorldFX.Get(), BladeTrailFX.Get(), BladeHitFX.Get(), MissileLaunchFX.Get()})
	{
		Out.AddUnique(S);
	}
}

void UMechCombatComponent::PlayAt(USoundBase* Sound, const FVector& At, float Volume, float PitchJitter) const
{
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, At, FRotator::ZeroRotator, Volume, 1.f + FMath::FRandRange(-PitchJitter, PitchJitter),
			0.f, Attenuation);
	}
}

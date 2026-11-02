#include "MechCharacter.h"
#include "GameFramework/InputSettings.h"
#include "MechMapRules.h"
#include "MechRun.h"
#include "MechSettings.h"
#include "MechComms.h"
#include "MechCoreDirector.h"
#include "MechFeed.h"
#include "EngineUtils.h"
#include "MechLoadout.h"
#include "MechCatalog.h"
#include "MechShoulderComponent.h"
#include "MechTacticalComponent.h"

#include "Animation/AnimInstance.h"
#include "MechAnimInstance.h"
#include "LightingBlueprintLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/CommandLine.h"
#include "Camera/CameraActor.h"
#include "GameFramework/HUD.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MechAIController.h"
#include "MechCombatComponent.h"
#include "MechGameMode.h"
#include "MechGameState.h"
#include "MechHealthComponent.h"
#include "MechMeshComponent.h"
#include "MechMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "MechWeapons.h"
#include "MechEffectComponent.h"
#include "MechVisualTest.h"
#include "MechFrameLineup.h"

namespace
{
	// 两个背部喷口在角色本地坐标里的位置（胶囊中心为原点，X 朝前），火焰朝后下方喷。
	// 10-30 以前一直用这组：只是估的，比 v3 机体背上真正的喷口高约 35 厘米、往外偏 8 厘米，而且只跟着网格整体走。现在只给 -MechThrusterOld 对比用
	const FVector NozzleOffset(-38.f, 16.f, 62.f);
	const FVector FlameDirection = FVector(-0.55f, 0.f, -0.83f).GetSafeNormal();
	// 背上两个主推进器的喷口（喇叭口外沿的中心）和喷射方向：10-31 起每台机体不同，在 MechCatalog::FrameBody（网格绑定姿势坐标，厘米，
	// 未乘 RobotScale；前 +Y、左 +X、上 +Z；右边那个 X 取反）。v3 那组是从 lookdev/rig/SKM_Robot_v3.blend 量的
	// （robot_lookdev.py 的 nozzle('主推进器', 躯干, (±0.07, 0.27, 0.08), 朝 (±0.1, 0.6, -1), r=0.05)，喇叭口在挂点沿喷口轴 r×1.3 处），
	// 其余八台是导出脚本量的（lookdev/rig/factions/manifest.json）。喷口零件都绑在 spine_05 上（躯干零件跟胸口走）
	const FName ThrusterBone(TEXT("spine_05"));

	// 武器握持点相对手 / 背的偏移（武器模型坐标，厘米，未缩放）
	const FVector BladeGrip(5.f, 0.f, 0.f);
	const FVector RifleGrip(5.f, 0.f, 0.f);
	// 武器挂点（绑定姿势下的机体坐标：前 X、右 Y、上 Z，厘米，未缩放，相对那根骨头在绑定姿势下的位置）
	// 近战：刀握在右手——照武士刀动作包的插槽（KatanaSocket 挂在 ik_hand_gun 上、和右手重合、无偏移；刀身沿刀模型 +Z，刀柄末端在原点），
	// 也就是刀身沿 hand_r 的 +Z，握把中心离骨头原点约 10 厘米；我们的刀模型刀身沿 +X、刃口朝 -Z
	const FTransform SwordInHand(FRotationMatrix::MakeFromXZ(FVector(0.f, 0.f, 1.f), FVector(0.f, -1.f, 0.f)).ToQuat(), FVector(0.f, 0.f, 10.f));
	// 左手（双刀）：模板人形左右骨头互为镜像，刀身沿 hand_l 的 -Z
	const FTransform SwordInLeftHand(FRotationMatrix::MakeFromXZ(FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f)).ToQuat(), FVector(0.f, 0.f, -10.f));
	// 收起：刀挂左腰、刀尖朝后下（双刀第二把挂右腰；大剑、钝器太长，斜背在背后），步枪斜背在背后、枪口朝下，双持手枪插在两边大腿外侧
	const FVector SwordStowOffset(6.f, -26.f, -4.f);
	const FRotator SwordStowRot(-20.f, 180.f, 0.f);
	const FVector SwordStowOffsetR(6.f, 26.f, -4.f);
	const FVector LongBladeStowOffset(-26.f, 8.f, 6.f);
	const FRotator LongBladeStowRot(-65.f, 180.f, 0.f);
	const FVector RifleStowOffset(-24.f, -6.f, -4.f);
	const FRotator RifleStowRot(-75.f, 180.f, 0.f);
	const FVector PistolStowOffset(2.f, 24.f, -22.f);
	const FRotator PistolStowRot(-85.f, 0.f, 0.f);
	const FVector PodMount(-22.f, 26.f, 22.f);  // 相对 spine_05，按机体朝向：后、右、上

	// 10-31 握持：武器表里的柄 / 握把 / 护木 → 网格组件的 FMechGripShape（标准握法：柄心线穿过手心空腔，见 UMechMeshComponent::GripPropInHand）。
	// -MechOldGrip = 10-31 以前的挂法（刀照动作包算的挂点、枪握把在手腕前 5 厘米、左手腕拉到握点、手指不动），拍改前对比用
	bool OldGrip()
	{
		static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("MechOldGrip"));
		return bOld;
	}
	FMechGripShape BladeGripShape(const MechWeapons::FBlade& B, bool bLeftSupport)
	{
		FMechGripShape S;
		S.Center = bLeftSupport ? FVector(B.LeftAt, B.Grip.Y, B.Grip.Z) : B.Grip;
		S.Axis = FVector::ForwardVector;
		S.Wrap = FVector(0.f, 0.f, B.EdgeZ);
		S.HalfD = S.HalfN = S.Corner = B.GripRadius;
		S.AlongMin = B.GripRange.X - S.Center.X;
		S.AlongMax = B.GripRange.Y - S.Center.X;
		S.HandDir = B.HandDir;
		return S;
	}
	FMechGripShape GunGripShape(const MechWeapons::FGun& G)
	{
		// 食指在握把顶下面一点：手心空腔中心离握把顶 4.6 厘米
		FMechGripShape S;
		const FVector Up = G.GripUp.GetSafeNormal();
		const float At = G.GripAlong.Y - 4.6f;
		S.Center = G.GripCenter + Up * At;
		S.Axis = Up;
		S.Wrap = FVector(Up.Z, 0.f, -Up.X);   // 握把正前方
		S.HalfD = G.GripSize.X;
		S.HalfN = G.GripSize.Y;
		S.Corner = G.GripSize.Z;
		S.AlongMin = G.GripAlong.X - At;
		S.AlongMax = G.GripAlong.Y - At;
		S.bTrigger = true;
		// 10-31 握持第二版：握把在手里斜多少按枪设（充能步枪的斜杆斜着横过手心）；-MechGripV1 照第一版一律贴着指根线
		static const bool bGripV1 = FParse::Param(FCommandLine::Get(), TEXT("MechGripV1"));
		S.HandDir = bGripV1 ? FVector(1.f, 0.f, 0.225f) : G.GripHandDir;
		return S;
	}
	FMechGripShape ForeGripShape(const MechWeapons::FGun& G)
	{
		// 左手托护木：手心朝上、手指往枪的右边包、拇指在左边
		FMechGripShape S;
		S.Center = G.ForeCenter;
		S.Axis = FVector::ForwardVector;
		S.Wrap = FVector::RightVector;
		S.HalfD = G.ForeHalf.X;
		S.HalfN = G.ForeHalf.Y;
		S.Corner = FMath::Min(G.ForeHalf.X, G.ForeHalf.Y) * 0.6f;
		S.AlongMin = -8.f;
		S.AlongMax = 8.f;
		S.bSupport = true;
		return S;
	}

	// 队伍颜色：发光件，蓝队青色、红队红色
	const FLinearColor TeamGlow[2] = {FLinearColor(0.15f, 0.75f, 1.f), FLinearColor(1.f, 0.12f, 0.06f)};
}

AMechCharacter::AMechCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMechMeshComponent>(ACharacter::MeshComponentName)
		.SetDefaultSubobjectClass<UMechMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	// 模板人形是 1.8 米、胶囊 42×96；机体整体放大 RobotScale 倍
	const float S = RobotScale;
	GetCapsuleComponent()->InitCapsuleSize(42.f * S, 96.f * S);

	// 默认框架（新纪建设中型）的网格；10-31 起九种框架各有各的网格，ApplyLoadout 按配装换（MechCatalog::FrameMesh）。
	// 阵营网格还没导入时退回 v3
	static ConstructorHelpers::FObjectFinderOptional<USkeletalMesh> DefaultFrameMesh(MechCatalog::FrameMesh(static_cast<int32>(EMechFrame::Medium)));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> RobotMesh(TEXT("/Game/Robot/Meshes/SKM_Robot_v3"));
	if (DefaultFrameMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMeshAsset(DefaultFrameMesh.Get());
	}
	else if (RobotMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMeshAsset(RobotMesh.Object);
	}
	// 移动动作：C++ 混合 Game Animation Sample 的走跑横移循环（见 MechAnimInstance）
	GetMesh()->SetAnimInstanceClass(UMechAnimInstance::StaticClass());
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -96.f * S), FRotator(0.f, -90.f, 0.f));
	GetMesh()->SetRelativeScale3D(FVector(S));
	// 10-31 联机抖动：同步给别人的朝向，引擎默认每个分量 1 字节（1.4 度一档），慢慢转身时插值出来的转速一档一档地变；
	// 改成 2 字节（0.0055 度一档，每次多 3 字节）。两端必须一样，所以只能在构造里定，不做命令行开关
	GetReplicatedMovement_Mutable().RotationQuantizationLevel = ERotatorQuantization::ShortComponents;
	// 用网格自带的包围盒，不按物理资产算：静止时按物理资产算出的盒子只有脚边一小块，会被地面遮挡剔除，机体整个消失
	GetMesh()->bComponentUseFixedSkelBounds = true;
	GetMesh()->SetBoundsScale(1.25f);

	// 模板的移动、视角、跳跃按键
	static ConstructorHelpers::FObjectFinder<UInputAction> MoveIA(TEXT("/Game/Input/Actions/IA_Move"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LookIA(TEXT("/Game/Input/Actions/IA_Look"));
	static ConstructorHelpers::FObjectFinder<UInputAction> MouseLookIA(TEXT("/Game/Input/Actions/IA_MouseLook"));
	static ConstructorHelpers::FObjectFinder<UInputAction> JumpIA(TEXT("/Game/Input/Actions/IA_Jump"));
	MoveAction = MoveIA.Object;
	LookAction = LookIA.Object;
	MouseLookAction = MouseLookIA.Object;
	JumpAction = JumpIA.Object;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxAcceleration = 3200.f;
	Move->BrakingDecelerationWalking = 2600.f;
	Move->BrakingDecelerationFalling = 600.f;
	Move->GroundFriction = 6.f;
	Move->JumpZVelocity = 720.f;
	Move->AirControl = 0.6f;
	Move->GravityScale = 1.4f;
	Move->RotationRate = FRotator(0.f, 720.f, 0.f);
	Move->MaxStepHeight = 45.f * S;

	// 镜头：肩后方、稍高，带一点跟随延迟
	GetCameraBoom()->TargetArmLength = 560.f;
	GetCameraBoom()->SocketOffset = FVector(0.f, 70.f, 110.f);
	GetCameraBoom()->bEnableCameraLag = true;
	GetCameraBoom()->CameraLagSpeed = 12.f;
	GetFollowCamera()->SetFieldOfView(BaseFOV);
	// 高速移动时镜头运动模糊太重，压低
	GetFollowCamera()->PostProcessSettings.bOverride_MotionBlurAmount = true;
	GetFollowCamera()->PostProcessSettings.MotionBlurAmount = 0.2f;
	GetFollowCamera()->PostProcessSettings.bOverride_MotionBlurMax = true;
	GetFollowCamera()->PostProcessSettings.MotionBlurMax = 2.f;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HitFlash(TEXT("/Game/Robot/Materials/M_HitFlash"));
	HitFlashMaterial = HitFlash.Object;

	// 推进器火焰：两只发光的锥体（短、白蓝，不拖长），加一盏冷光
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/Robot/Materials/MI_Robot_Glow"));
	// 10-30：机体的发光材质 MI_Robot_Glow 是不透明受光材质 + 固定自发光，白天（测试场、自然图、山谷）自动曝光高十几档，锥体就成了两根黑刺。
	// 换成充能步枪光束那个叠加材质（不受光、中间亮边缘暗、自带白天曝光补偿），在 BeginPlay 里做动态实例
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlameMat(TEXT("/Game/Weapons/Materials/M_MechBeam"));
	FlameMaterial = FlameMat.Object;
	auto MakeFlame = [&](const TCHAR* Name, float Side) {
		UStaticMeshComponent* Flame = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Flame->SetupAttachment(RootComponent);
		if (Cone.Succeeded())
		{
			Flame->SetStaticMesh(Cone.Object);
		}
		if (Glow.Succeeded())
		{
			Flame->SetMaterial(0, Glow.Object);
		}
		Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Flame->SetCastShadow(false);
		Flame->SetVisibility(false);
		// 锥体尖端朝 +Z：转到朝向喷射方向
		Flame->SetRelativeRotation(FRotationMatrix::MakeFromZ(FlameDirection).Rotator());
		Flame->SetRelativeLocation(FVector(NozzleOffset.X, NozzleOffset.Y * Side, NozzleOffset.Z));
		return Flame;
	};
	ThrusterLeft = MakeFlame(TEXT("ThrusterLeft"), 1.f);
	ThrusterRight = MakeFlame(TEXT("ThrusterRight"), -1.f);

	ThrusterLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ThrusterLight"));
	ThrusterLight->SetupAttachment(RootComponent);
	ThrusterLight->SetRelativeLocation(NozzleOffset * FVector(1.f, 0.f, 1.f) + FlameDirection * 40.f);
	ThrusterLight->SetLightColor(FLinearColor(0.75f, 0.88f, 1.f));
	ThrusterLight->SetAttenuationRadius(600.f);
	ThrusterLight->SetIntensity(0.f);
	ThrusterLight->SetCastShadows(false);

	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(RootComponent);
	FillLight->SetRelativeLocation(FVector(260.f, -90.f, 170.f));
	FillLight->SetLightColor(FLinearColor(0.82f, 0.88f, 1.f));
	FillLight->SetIntensity(1100.f);
	FillLight->SetAttenuationRadius(1000.f);
	FillLight->SetCastShadows(false);
	FillLight->LightingChannels.bChannel0 = false;
	FillLight->LightingChannels.bChannel1 = true;
	GetMesh()->LightingChannels.bChannel1 = true;

	// 冲刺（按住）和快速闪避这两个按键在代码里建，不另做资源
	BoostAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_Boost"));
	BoostAction->ValueType = EInputActionValueType::Boolean;
	QuickBoostAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_QuickBoost"));
	QuickBoostAction->ValueType = EInputActionValueType::Boolean;
	// 映射在这里先空着：键位跟着设置走（玩家可以改键），第一次给玩家装输入时由 ApplyKeyBindings 按设置填（BuildKeyMappings）。
	// 构造类默认对象时 GameUserSettings.ini 可能还没读，不能在这里取设置
	MechMappingContext = CreateDefaultSubobject<UInputMappingContext>(TEXT("IMC_Mech"));

	// 战斗按键：左键 / RT 步枪，右键 / LT 刀，E / RB 导弹，R / X 换弹
	auto MakeAction = [&](const TCHAR* Name) {
		UInputAction* A = CreateDefaultSubobject<UInputAction>(Name);
		A->ValueType = EInputActionValueType::Boolean;
		return A;
	};
	FireAction = MakeAction(TEXT("IA_Fire"));
	SlashAction = MakeAction(TEXT("IA_Slash"));
	MissileAction = MakeAction(TEXT("IA_Missile"));
	ReloadAction = MakeAction(TEXT("IA_Reload"));
	JumpHoldAction = MakeAction(TEXT("IA_JumpHold"));
	StanceAction = MakeAction(TEXT("IA_Stance"));

	Health = CreateDefaultSubobject<UMechHealthComponent>(TEXT("Health"));
	Combat = CreateDefaultSubobject<UMechCombatComponent>(TEXT("Combat"));
	Effects = CreateDefaultSubobject<UMechEffectComponent>(TEXT("Effects"));
	Shoulder = CreateDefaultSubobject<UMechShoulderComponent>(TEXT("Shoulder"));
	Tactical = CreateDefaultSubobject<UMechTacticalComponent>(TEXT("Tactical"));

	// 武器：不挂骨头，由网格组件在骨骼定稿后按手的位置每帧摆放（绝对坐标）
	// 步枪：9776 科幻枪包 GUN 3 号（无托步枪，底色压成冷灰）；剑：NO454 科幻剑包 07 号。摆正流程见 lookdev/weapon_normalize.py
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RifleAsset(TEXT("/Game/Weapons/Meshes/SM_Gun3"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BladeAsset(TEXT("/Game/Weapons/Meshes/SM_Sword07"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PodAsset(TEXT("/Game/Weapons/Meshes/SM_MissilePod"));
	// 钩爪、捕获网：lookdev/make_shoulder_game.py（机甲组件库零件拼装 + 程序生成的网）
	static ConstructorHelpers::FObjectFinder<UStaticMesh> GrappleAsset(TEXT("/Game/Weapons/Meshes/SM_GrappleLauncher"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> NetLauncherAsset(TEXT("/Game/Weapons/Meshes/SM_NetLauncher"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ClawAsset(TEXT("/Game/Weapons/Meshes/SM_GrappleClaw"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ArtilleryAsset(TEXT("/Game/Weapons/Meshes/SM_Artillery"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> EmitterAsset(TEXT("/Game/Weapons/Meshes/SM_ShieldEmitter"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SmokeAsset(TEXT("/Game/Weapons/Meshes/SM_SmokeLauncher"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> EmpAsset(TEXT("/Game/Weapons/Meshes/SM_EmpLauncher"));
	// 紧急脱离、弹射升空、震荡波、地雷：lookdev/make_shoulder_tactical.py（用户：「补全剩下的装备模型」）
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BurstAsset(TEXT("/Game/Weapons/Meshes/SM_BurstPack"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LaunchAsset(TEXT("/Game/Weapons/Meshes/SM_LaunchSpring"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ShockAsset2(TEXT("/Game/Weapons/Meshes/SM_ShockRing"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> MineLayerAsset(TEXT("/Game/Weapons/Meshes/SM_MineLayer"));
	auto MakeWeapon = [&](const TCHAR* Name, UStaticMesh* Asset) {
		UStaticMeshComponent* W = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		W->SetupAttachment(RootComponent);
		W->SetUsingAbsoluteLocation(true);
		W->SetUsingAbsoluteRotation(true);
		W->SetUsingAbsoluteScale(true);
		W->SetRelativeScale3D(FVector(S));
		W->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		W->LightingChannels.bChannel1 = true;
		// 武器每帧都在动、模型又细：不进距离场和 Lumen 场景，否则每帧重算距离场、重拍 Lumen 卡片，游戏 / 渲染线程各多好几毫秒
		W->bAffectDistanceFieldLighting = false;
		W->bAffectDynamicIndirectLighting = false;
		if (Asset)
		{
			W->SetStaticMesh(Asset);
		}
		return W;
	};
	RifleMesh = MakeWeapon(TEXT("RifleMesh"), RifleAsset.Object);
	BladeMesh = MakeWeapon(TEXT("BladeMesh"), BladeAsset.Object);
	PodMesh = MakeWeapon(TEXT("PodMesh"), PodAsset.Object);
	ClawMesh = MakeWeapon(TEXT("ClawMesh"), ClawAsset.Object);
	ClawMesh->SetVisibility(false);
	ShoulderMeshes[static_cast<int32>(EMechShoulder::Missiles)] = PodAsset.Object;
	ShoulderMeshes[static_cast<int32>(EMechShoulder::Artillery)] = ArtilleryAsset.Object;
	ShoulderMeshes[static_cast<int32>(EMechShoulder::Shield)] = EmitterAsset.Object;
	ShoulderMeshes[static_cast<int32>(EMechShoulder::Grapple)] = GrappleAsset.Object;
	ShoulderMeshes[static_cast<int32>(EMechShoulder::Net)] = NetLauncherAsset.Object;
	ShoulderMeshes[static_cast<int32>(EMechShoulder::Smoke)] = SmokeAsset.Object;
	ShoulderMeshes[static_cast<int32>(EMechShoulder::Emp)] = EmpAsset.Object;
	ShoulderMeshes[static_cast<int32>(EMechShoulder::Burst)] = BurstAsset.Object;
	ShoulderMeshes[static_cast<int32>(EMechShoulder::Launch)] = LaunchAsset.Object;
	ShoulderMeshes[static_cast<int32>(EMechShoulder::Shockwave)] = ShockAsset2.Object;
	ShoulderMeshes[static_cast<int32>(EMechShoulder::Mine)] = MineLayerAsset.Object;
	static_assert(UE_ARRAY_COUNT(ShoulderMeshes) == static_cast<int32>(EMechShoulder::Mine) + 1, "数组大小要和 EMechShoulder 一致（UPROPERTY 的数组大小只能写数字）");
	OffGunMesh = MakeWeapon(TEXT("OffGunMesh"), nullptr);
	OffBladeMesh = MakeWeapon(TEXT("OffBladeMesh"), nullptr);
	OffGunMesh->SetVisibility(false);
	OffBladeMesh->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<USoundBase> BoostLoop(TEXT("/Game/Audio/Combat/S_boost_loop"));
	BoostAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("BoostAudio"));
	BoostAudio->SetupAttachment(RootComponent);
	BoostAudio->bAutoActivate = false;
	if (BoostLoop.Succeeded())
	{
		BoostAudio->SetSound(BoostLoop.Object);
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> DeathAsset(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_02_Exp_01"));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ShockAsset(TEXT("/Game/AAAVFX/Course_01/Particles/Ex_02_Earth_Explosion/Exp_01"));
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> StaggerAsset(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"));
	static ConstructorHelpers::FObjectFinder<USoundBase> DeathSnd(TEXT("/Game/Audio/Combat/S_explosion_big"));
	static ConstructorHelpers::FObjectFinder<USoundBase> StaggerSnd(TEXT("/Game/Audio/Combat/S_stagger"));
	static ConstructorHelpers::FObjectFinder<USoundBase> QuickSnd(TEXT("/Game/Audio/Combat/S_quickboost"));
	static ConstructorHelpers::FObjectFinder<USoundAttenuation> Att(TEXT("/Game/Audio/SA_Combat"));
	DeathFX = DeathAsset.Object;
	DeathShockFX = ShockAsset.Object;
	StaggerFX = StaggerAsset.Object;
	DeathSound = DeathSnd.Object;
	StaggerSound = StaggerSnd.Object;
	QuickBoostSound = QuickSnd.Object;
	Attenuation = Att.Object;

	AIControllerClass = AMechAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

UMechMeshComponent* AMechCharacter::GetMechMesh() const
{
	return Cast<UMechMeshComponent>(GetMesh());
}

void AMechCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechCharacter, Team);
	DOREPLIFETIME(AMechCharacter, DisplayName);
	DOREPLIFETIME(AMechCharacter, Paint);
	DOREPLIFETIME(AMechCharacter, Loadout);
	DOREPLIFETIME(AMechCharacter, bTrainingDummy);
	DOREPLIFETIME(AMechCharacter, bCarryingCore);
	DOREPLIFETIME_CONDITION(AMechCharacter, NetEnergy, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AMechCharacter, MobilityFX, COND_SkipOwner);
}

UMechMovementComponent* AMechCharacter::GetMechMovement() const
{
	return Cast<UMechMovementComponent>(GetCharacterMovement());
}

float AMechCharacter::GetEnergy() const
{
	return GetMechMovement()->Energy;
}

float AMechCharacter::GetMaxEnergy() const
{
	return GetMechMovement()->MaxEnergy;
}

bool AMechCharacter::IsBoosting() const
{
	return (IsLocallyControlled() || HasAuthority()) ? GetMechMovement()->bBoostActive : (MobilityFX & 1) != 0;
}

bool AMechCharacter::IsThrustingUp() const
{
	return (IsLocallyControlled() || HasAuthority()) ? GetMechMovement()->bThrusting : (MobilityFX & 2) != 0;
}

bool AMechCharacter::IsOverheated() const
{
	return GetMechMovement()->bOverheated;
}

void AMechCharacter::SetBoostHeld(bool bHeld)
{
	GetMechMovement()->bWantsBoost = bHeld;
}

void AMechCharacter::OnQuickBoostExecuted(const FVector& Dir)
{
	if (bClientUpdating)
	{
		return;  // 客户端纠正后重放旧移动，不重复播
	}
	StartDodgeAnim(Dir);
	Combat->OnQuickBoost();  // 双持手枪：闪避时自动装满弹匣（只在服务器上生效）
	if (IsLocallyControlled() && QuickBoostSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, QuickBoostSound, GetActorLocation(), FRotator::ZeroRotator, 0.7f,
			FMath::FRandRange(0.95f, 1.08f), 0.f, Attenuation);
	}
	if (HasAuthority())
	{
		MulticastQuickBoostFX(Dir);
	}
}

void AMechCharacter::MulticastQuickBoostFX_Implementation(FVector_NetQuantizeNormal Dir)
{
	// 自己操控的那台已经在本地预测时播过了（服务器上的也已经记过）
	if (IsLocallyControlled() || HasAuthority())
	{
		return;
	}
	StartDodgeAnim(Dir);
	RemoteQuickFlash = 1.f;
	if (QuickBoostSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, QuickBoostSound, GetActorLocation(), FRotator::ZeroRotator, 0.7f,
			FMath::FRandRange(0.95f, 1.08f), 0.f, Attenuation);
	}
}

void AMechCharacter::OnWallJumpExecuted(const FVector& Normal)
{
	if (bClientUpdating)
	{
		return;  // 客户端纠正后重放旧移动，不重复播
	}
	PlayWallJumpFX(Normal);
	if (HasAuthority())
	{
		MulticastWallJumpFX(Normal);
	}
	UE_LOG(LogTemp, Verbose, TEXT("[walljump] %s 蹬墙 法线 %s 速度 %s"), *GetName(), *Normal.ToCompactString(), *GetVelocity().ToCompactString());
}

void AMechCharacter::MulticastWallJumpFX_Implementation(FVector_NetQuantizeNormal Normal)
{
	// 自己操控的那台已经在本地预测时播过了（服务器上的也已经播过）
	if (IsLocallyControlled() || HasAuthority())
	{
		return;
	}
	PlayWallJumpFX(Normal);
}

void AMechCharacter::PlayWallJumpFX(const FVector& Normal)
{
	WallKickStart = GetWorld()->GetTimeSeconds();
	// 蹬墙：脚蹬金属墙的一声闷响 + 推进器跟着喷一下（借快速闪避的声音，压低音调、轻一点）
	if (QuickBoostSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, QuickBoostSound, GetActorLocation() - Normal * 60.f, FRotator::ZeroRotator, IsLocallyControlled() ? 0.5f : 0.4f,
			FMath::FRandRange(0.72f, 0.8f), 0.f, Attenuation);
	}
	if (!IsLocallyControlled())
	{
		RemoteQuickFlash = FMath::Max(RemoteQuickFlash, 0.5f);
	}
}

float AMechCharacter::GetSinceWallKick() const
{
	return GetWorld()->GetTimeSeconds() - WallKickStart;
}

void AMechCharacter::SetTeam(int32 InTeam)
{
	Team = InTeam;
	ApplyTeamColor();
}

void AMechCharacter::OnRep_Team()
{
	ApplyTeamColor();
}

void AMechCharacter::ApplyTeamColor()
{
	// 发光件换成队伍色，远处也分得清敌我
	const FLinearColor Glow = TeamGlow[FMath::Clamp(Team, 0, 1)];
	const int32 Slot = GetMesh()->GetMaterialIndex(TEXT("MI_Robot_Glow"));
	if (Slot != INDEX_NONE)
	{
		if (UMaterialInstanceDynamic* MID = GetMesh()->CreateAndSetMaterialInstanceDynamic(Slot))
		{
			MID->SetVectorParameterValue(TEXT("Emissive"), Glow);
		}
	}
	for (UStaticMeshComponent* W : {RifleMesh.Get(), BladeMesh.Get(), PodMesh.Get(), OffGunMesh.Get(), OffBladeMesh.Get(), ClawMesh.Get()})
	{
		for (int32 WSlot = 0; WSlot < W->GetNumMaterials(); ++WSlot)
		{
			UMaterialInterface* Mat = W->GetMaterial(WSlot);
			const bool bGlow = W->GetMaterialSlotNames().IsValidIndex(WSlot) && W->GetMaterialSlotNames()[WSlot] == TEXT("MI_Robot_Glow");
			const bool bWeapon = Mat && Mat->GetBaseMaterial() && Mat->GetBaseMaterial()->GetName() == TEXT("M_WeaponPBR");
			if (bGlow || bWeapon)
			{
				if (UMaterialInstanceDynamic* MID = W->CreateAndSetMaterialInstanceDynamic(WSlot))
				{
					MID->SetVectorParameterValue(TEXT("Emissive"), Glow);
				}
			}
		}
	}
}

void AMechCharacter::GetEffectSystems(TArray<UNiagaraSystem*>& Out) const
{
	for (UNiagaraSystem* S : {DeathFX.Get(), DeathShockFX.Get(), StaggerFX.Get()})
	{
		Out.AddUnique(S);
	}
	Combat->GetEffectSystems(Out);
	Shoulder->GetEffectSystems(Out);
	Tactical->GetEffectSystems(Out);
}

bool AMechCharacter::CanAct() const
{
	if (Health->IsDead())
	{
		return false;
	}
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	return !GS || GS->Phase == EMechRoundPhase::Waiting || GS->CanAct();
}

void AMechCharacter::DoMove(float Right, float Forward)
{
	if (Combat && Combat->IsMeleeActive() && Combat->GetMeleeTime() < Combat->GetMeleeCancel())
	{
		return;
	}
	Super::DoMove(Right, Forward);
}

void AMechCharacter::DoLook(float Yaw, float Pitch)
{
	static const bool bLookCalib = FParse::Param(FCommandLine::Get(), TEXT("MechLookCalib"));
	if (bLookCalib && (Yaw != 0.f || Pitch != 0.f))
	{
		UE_LOG(LogTemp, Display, TEXT("[look] DoLook 收到 yaw %.4f pitch %.4f（鼠标计数）"), Yaw, Pitch);
	}
	if (MechComms::WheelLook(this, Yaw, Pitch))
	{
		return;  // 11-01 标点轮盘开着：鼠标移动拿去选轮盘上的项，镜头不转（MechComms.h）
	}
	const FMechSettings& S = FMechSettings::Get();
	// 瞄准时慢一点，狙击开镜更慢
	const bool bScope = Combat && Combat->IsAiming() && Combat->GetGun().EnergyPerShot > 0.f;
	const bool bArty = Shoulder && Shoulder->GetArtilleryDeploy() >= 1.f;
	// 狙击开镜压在敌人身上时也有粘滞（10-30 玩家：开镜要帮一点），比平时重一档，见 MechWeapons::ScopeLookScale
	// 11-02 CS2 灵敏度：每个鼠标计数 = 灵敏度 x 0.022 度。引擎还会把这里的数乘上 InputYaw/PitchScale（旧缩放，开着时），先除掉
	float YawK = 1.f, PitchK = 1.f;
	if (const APlayerController* LookPC = Cast<APlayerController>(GetController()); LookPC && GetDefault<UInputSettings>()->bEnableLegacyInputScales)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		YawK = FMath::Max(0.01f, FMath::Abs(LookPC->GetDeprecatedInputYawScale()));
		PitchK = FMath::Max(0.01f, FMath::Abs(LookPC->GetDeprecatedInputPitchScale()));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	const float Base = S.MouseSensitivity * FMechSettings::DegreesPerCount / FMechSettings::LookEngineExtra;
	const float Mode = bScope ? S.ZoomSensitivityRatio * MechWeapons::ScopeLookScale(AimFriction) : bArty ? 0.4f : (Combat && Combat->IsAiming() ? 0.6f : 1.f) * AimFriction;
	Super::DoLook(Yaw * Base * Mode / YawK, Pitch * Base * Mode / PitchK * (S.bInvertY ? -1.f : 1.f));
}

EMechHitPart AMechCharacter::ClassifyHit(const FVector& From, const FVector& Dir) const
{
	// 各段骨头当成粗细不同的胶囊（半径按模板人形尺寸，再乘机体缩放）。子弹那条线先穿进哪段就算打中哪个部位；
	// 一段都没穿进（擦着胶囊边过去）就算离得最近的那段（按各自粗细折算）
	struct FSeg
	{
		const TCHAR* A;
		const TCHAR* B;  // 空 = 从 A 往上一段（头：头骨原点在后脑下面，头往上长）
		float Radius;
		EMechHitPart Part;
	};
	static const FSeg Segs[] = {
		{TEXT("pelvis"), TEXT("spine_03"), 28.f, EMechHitPart::Torso}, {TEXT("spine_03"), TEXT("spine_05"), 30.f, EMechHitPart::Torso},
		{TEXT("spine_05"), TEXT("neck_01"), 11.f, EMechHitPart::Torso}, {TEXT("clavicle_l"), TEXT("upperarm_l"), 13.f, EMechHitPart::Torso},
		{TEXT("clavicle_r"), TEXT("upperarm_r"), 13.f, EMechHitPart::Torso},
		{TEXT("head"), nullptr, 15.f, EMechHitPart::Head},
		{TEXT("upperarm_l"), TEXT("lowerarm_l"), 9.f, EMechHitPart::Limb}, {TEXT("lowerarm_l"), TEXT("hand_l"), 8.f, EMechHitPart::Limb},
		{TEXT("upperarm_r"), TEXT("lowerarm_r"), 9.f, EMechHitPart::Limb}, {TEXT("lowerarm_r"), TEXT("hand_r"), 8.f, EMechHitPart::Limb},
		{TEXT("thigh_l"), TEXT("calf_l"), 13.f, EMechHitPart::Limb}, {TEXT("calf_l"), TEXT("foot_l"), 11.f, EMechHitPart::Limb},
		{TEXT("thigh_r"), TEXT("calf_r"), 13.f, EMechHitPart::Limb}, {TEXT("calf_r"), TEXT("foot_r"), 11.f, EMechHitPart::Limb},
	};
	const UMechMeshComponent* M = GetMechMesh();
	const float S = M->GetComponentScale().Z;
	const FVector D = Dir.GetSafeNormal();
	const FVector A = From - D * 40.f * S;
	const FVector B = From + D * 160.f * S;
	float FirstEntry = TNumericLimits<float>::Max();
	float BestMiss = TNumericLimits<float>::Max();
	EMechHitPart Entered = EMechHitPart::Torso;
	EMechHitPart Nearest = EMechHitPart::Torso;
	bool bEntered = false;
	for (const FSeg& Seg : Segs)
	{
		const FVector P0 = M->GetBoneLocation(Seg.A);
		const FVector P1 = Seg.B ? M->GetBoneLocation(Seg.B) : P0 + GetActorUpVector() * 24.f * S;
		FVector OnRay, OnBone;
		FMath::SegmentDistToSegmentSafe(A, B, P0, P1, OnRay, OnBone);
		const float Dist = FVector::Dist(OnRay, OnBone);
		const float R = Seg.Radius * S;
		if (Dist < R)
		{
			// 穿进这段的位置（沿子弹方向）：离最近点往回退一段弦长的一半
			// 四肢要明显挡在前面才算（端枪时小臂横在胸前，贴着胸口的那一下还是算躯干）
			const float Entry = FVector::DotProduct(OnRay - A, D) - FMath::Sqrt(R * R - Dist * Dist) + (Seg.Part == EMechHitPart::Limb ? 10.f * S : 0.f);
			if (Entry < FirstEntry)
			{
				FirstEntry = Entry;
				Entered = Seg.Part;
				bEntered = true;
			}
		}
		else if (Dist / R < BestMiss)
		{
			BestMiss = Dist / R;
			Nearest = Seg.Part;
		}
	}
	return bEntered ? Entered : Nearest;
}

float AMechCharacter::ReceiveHit(float Damage, float Impact, AActor* Attacker, const FVector& At, EMechHitKind Kind, float StaggerScale)
{
	if (!HasAuthority() || Health->IsDead())
	{
		return 0.f;
	}
	if (Kind != EMechHitKind::Blade && Shoulder->BlocksFrom(At))
	{
		Shoulder->OnShieldBlocked(At);  // 肩部护盾：正面来的远程攻击全挡
		return 0.f;
	}
	Combat->TryBlock(At, Damage, Impact);  // 近战姿态正面格挡：伤害和冲击大减
	// 11-01 积分制：这一下有没有把对方打进失衡（之前没失衡、之后真失衡了；被弹反的短僵直不算）——打出失衡加分、算助攻（MechScore.h）
	const bool bWasBroken = Health->IsStaggered() && !Health->IsStunned();
	MechFeed::NoteHit(this, Attacker, Kind);  // 11-01 信息区：结算前记下这一下是子弹 / 刀 / 爆炸（被打爆时击毁那一行的武器图标按它）
	const float Dealt = Health->ApplyHit(Damage, Impact, Attacker, At,
		StaggerScale >= 0.f ? StaggerScale : Kind == EMechHitKind::Blade ? Health->BladeStaggerScale : 1.f);
	if (AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>())
	{
		GM->RecordHit(Cast<AMechCharacter>(Attacker), this, Dealt, Kind, !bWasBroken && Health->IsStaggered() && !Health->IsStunned());
	}
	return Dealt;
}

void AMechCharacter::HitStop(float Seconds)
{
	CustomTimeDilation = 0.03f;
	GetWorldTimerManager().SetTimer(HitStopTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { CustomTimeDilation = 1.f; }), Seconds, false);
}

void AMechCharacter::AddRecoil(float Amount)
{
	if (IsLocallyControlled() && IsPlayerControlled())
	{
		Trauma = FMath::Min(1.f, Trauma + Amount);
	}
}

void AMechCharacter::QuickBoostToward(const FVector& Dir)
{
	GetMechMovement()->QuickBoostDirOverride = Dir.GetSafeNormal2D();
	GetMechMovement()->bWantsQuickBoost = true;
}

void AMechCharacter::SetJumpThrust(bool bHeld)
{
	UMechMovementComponent* Move = GetMechMovement();
	if (bHeld && !Move->bWantsThrust)
	{
		JumpPressed();
	}
	else if (!bHeld && Move->bWantsThrust)
	{
		JumpReleased();
	}
}

void AMechCharacter::FirePressed()
{
	// 左键：射击姿态开火；近战姿态点按轻击、按住重斩
	if (Combat->GetStance() == EMechStance::Blade)
	{
		Combat->MeleePressed();
	}
	else
	{
		Combat->SetTrigger(true);
	}
}

void AMechCharacter::FireReleased()
{
	Combat->SetTrigger(false);
	Combat->MeleeReleased();
}

void AMechCharacter::MissilePressed()
{
	// E：肩部装备（导弹在战斗组件里；紧急脱离、弹射升空、震荡波、地雷在战术组件里，移动型按移动输入的方向，地雷扔向准星；
	// 其余在肩部组件里，火炮要按住、松开）
	if (Loadout.Shoulder == EMechShoulder::Missiles)
	{
		Combat->FireMissiles();
	}
	else if (MechIsTacticalShoulder(Loadout.Shoulder))
	{
		Tactical->Pressed();
	}
	else
	{
		Shoulder->Pressed();
	}
}

void AMechCharacter::MissileReleased()
{
	if (Loadout.Shoulder != EMechShoulder::Missiles)
	{
		Shoulder->Released();
	}
}

void AMechCharacter::PlayThrusterBurst(float Strength, const FVector& DodgeWorldDir)
{
	// 和别人机体的快速闪避闪光共用一个量（UpdateThrusterFX 按它把喷焰、推进灯推亮，每秒衰减 4）
	RemoteQuickFlash = FMath::Max(RemoteQuickFlash, Strength);
	if (!DodgeWorldDir.IsNearlyZero())
	{
		StartDodgeAnim(DodgeWorldDir);
	}
}

bool AMechCharacter::IsEmped() const
{
	return Shoulder && Shoulder->IsEmped();
}

bool AMechCharacter::IsNetted() const
{
	return Shoulder && Shoulder->IsNetted();
}

float AMechCharacter::GetShoulderReady() const
{
	if (MechIsTacticalShoulder(Loadout.Shoulder))
	{
		return Tactical->GetReady();
	}
	return Loadout.Shoulder == EMechShoulder::Missiles ? Combat->GetMissileReady() : Shoulder->GetReady();
}

float AMechCharacter::GetShoulderTimer() const
{
	if (MechIsTacticalShoulder(Loadout.Shoulder))
	{
		return Tactical->GetCooldown();
	}
	return Loadout.Shoulder == EMechShoulder::Missiles ? Combat->GetMissileTimer() : Shoulder->GetCooldown();
}

FLinearColor AMechCharacter::TeamGlowColor(int32 InTeam)
{
	return TeamGlow[FMath::Clamp(InTeam, 0, 1)];
}

void AMechCharacter::SlashPressed()
{
	// 右键：射击姿态瞄准，近战姿态格挡（按住）；投掷匕首不能格挡，右键是近身挥刀（10-30 用户改）。
	// 按住的状态照样记下（钩爪救队友也看「按住右键」）
	Combat->SetAltHeld(true);
	if (Combat->GetStance() == EMechStance::Blade && Combat->GetBlade().Knives > 0)
	{
		Combat->RequestDaggerSlash();
	}
}

void AMechCharacter::AltReleased()
{
	Combat->SetAltHeld(false);
}

void AMechCharacter::StancePressed()
{
	Combat->SetTrigger(false);
	Combat->SwitchStance();
}

void AMechCharacter::ReloadPressed()
{
	Combat->Reload();
}

void AMechCharacter::BeginPlay()
{
	Super::BeginPlay();
	UMechMeshComponent* M = GetMechMesh();
	M->Props[0] = BladeMesh;
	M->Props[1] = RifleMesh;
	M->Props[2] = PodMesh;
	M->Props[3] = OffGunMesh;
	M->Props[4] = OffBladeMesh;
	M->PropOffset[0] = BladeGrip;
	M->PropOffset[1] = RifleGrip;
	M->PropOffset[2] = PodMount;
	M->PropOffset[3] = RifleGrip;
	M->PropOffset[4] = BladeGrip;
	Health->OnHit.AddUObject(this, &AMechCharacter::OnHealthHit);
	Health->OnStaggered.AddUObject(this, &AMechCharacter::OnHealthStaggered);
	Health->OnDied.AddUObject(this, &AMechCharacter::OnHealthDied);
	if (HasAuthority())
	{
		// 服务器上不管在不在画面里都刷新骨骼：枪口位置、命中部位都按骨骼算，画面外的机体骨骼不刷新就会打偏
		// （自动对战里镜头跟着的那队吃亏；联机时房主的画面会影响全场判定）
		GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
	BaseSocketOffset = GetCameraBoom()->SocketOffset;
	// 喷焰、推进器灯、补光改挂到网格上：别人的机体在本机是网格平滑追着胶囊（网络修正时胶囊瞬移），挂在胶囊上会一跳一跳地错开。
	// 缩放按世界算（网格放大了 RobotScale 倍，喷焰长短在 UpdateThrusterFX 里按世界尺寸设）
	for (USceneComponent* C : {static_cast<USceneComponent*>(ThrusterLeft.Get()), static_cast<USceneComponent*>(ThrusterRight.Get()),
		static_cast<USceneComponent*>(ThrusterLight.Get()), static_cast<USceneComponent*>(FillLight.Get())})
	{
		C->SetUsingAbsoluteScale(true);
		C->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepWorldTransform);
	}
	SetupThrusterNozzles();
	// 肩部挂件（PodMesh）和爪子不在这里：10-30 改成浮空无人机，由肩部组件每帧按弹簧阻尼摆（绝对坐标，挂在胶囊上只是为了跟着 Actor 生灭）。
	// 手里的刀枪、左手刀枪也挂到网格上（缩放仍按世界）。摆放照旧：骨骼定稿时 PlaceProps 按手的位置设世界变换，
	// 引擎自己换算成相对网格的变换。以前挂在胶囊上、用绝对坐标，有两种情况会和机体分家：① 摆完以后网格这一帧又动了
	// （近战突进追身时战斗组件在物理之后转身、传送、换回合），道具留在原地差一帧；② 这一帧骨骼没刷新（客户端上画面外 / 被挡住的
	// 别人的机体不刷新骨骼，也就不调 PlaceProps），网格跟着胶囊走了，道具还停在最后一次摆的地方——机体从掩体后出来那一帧，枪浮在半空。
	// 挂在网格上以后，这两种情况道具都跟着网格整体走（姿势是上一次的，但不会分家）。测量见 -MechJitterLog 第二行「脱离网格」；
	// -MechPropsOnCapsule 保留旧挂法（只为对比测量）
	if (!FParse::Param(FCommandLine::Get(), TEXT("MechPropsOnCapsule")))
	{
		for (UStaticMeshComponent* W : {RifleMesh.Get(), BladeMesh.Get(), OffGunMesh.Get(), OffBladeMesh.Get()})
		{
			W->SetUsingAbsoluteLocation(false);
			W->SetUsingAbsoluteRotation(false);
			W->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepWorldTransform);
		}
	}
	ApplyTeamColor();
	bDemo = FParse::Param(FCommandLine::Get(), TEXT("MechDemo"));
	bAnimTest = FParse::Param(FCommandLine::Get(), TEXT("MechAnimTest"));
	bAirTest = FParse::Param(FCommandLine::Get(), TEXT("MechAirTest"));
	bWallTest = FParse::Param(FCommandLine::Get(), TEXT("MechWallTest"));
	FString ShotList;
	bViewerAuto = FParse::Param(FCommandLine::Get(), TEXT("MechViewerShots")) || FParse::Value(FCommandLine::Get(), TEXT("MechViewerShots="), ShotList);
	bViewer = bViewerAuto || FParse::Param(FCommandLine::Get(), TEXT("MechViewer")) || GetWorld()->URL.HasOption(TEXT("Viewer"));  // 主菜单进「模型查看」走地址参数
	bBeauty = FParse::Param(FCommandLine::Get(), TEXT("MechBeauty"));
	FString PoseMode;
	if (FParse::Value(FCommandLine::Get(), TEXT("MechPose="), PoseMode) && PoseMode == TEXT("ref"))
	{
		// 调试：不放动画，只显示绑定姿势
		GetMesh()->SetAnimInstanceClass(nullptr);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("MechBright")))
	{
		// 调试：补光调到很亮并照所有物体，用来区分「机体没渲染」和「机体太暗」
		FillLight->SetIntensity(60000.f);
		FillLight->LightingChannels.bChannel0 = true;
		FillLight->MarkRenderStateDirty();
	}
	ApplyLoadout();  // 默认配装（中型 + 均衡型）；服务器换装时会再调
	UE_LOG(LogTemp, Display, TEXT("[mech] 网格 %s 材质数 %d 缩放 %s 相对位置 %s 包围盒 %s"), *GetNameSafe(GetMesh()->GetSkeletalMeshAsset()),
		GetMesh()->GetNumMaterials(), *GetMesh()->GetRelativeScale3D().ToString(), *GetMesh()->GetRelativeLocation().ToString(),
		*GetMesh()->Bounds.BoxExtent.ToString());
}

void AMechCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 映射和输入动作用类默认对象上的同一份：每回合都换新机体，若各用各的，上一台的映射会留在玩家身上，
	// 把同样的按键（左右键、E、R、空格、Shift、Ctrl）先吃掉，新机体就只剩移动能用
	const AMechCharacter* Def = GetClass()->GetDefaultObject<AMechCharacter>();
	ApplyKeyBindings();  // 键位按设置填进共用的映射（第一次装输入时填；之后键位没变就不动）
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(Def->MechMappingContext, 1);
		}
	}

	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		Input->BindAction(Def->BoostAction, ETriggerEvent::Started, this, &AMechCharacter::BoostStarted);
		Input->BindAction(Def->BoostAction, ETriggerEvent::Completed, this, &AMechCharacter::BoostEnded);
		Input->BindAction(Def->QuickBoostAction, ETriggerEvent::Started, this, &AMechCharacter::QuickBoost);
		Input->BindAction(Def->JumpHoldAction, ETriggerEvent::Started, this, &AMechCharacter::JumpPressed);
		Input->BindAction(Def->JumpHoldAction, ETriggerEvent::Completed, this, &AMechCharacter::JumpReleased);
		Input->BindAction(Def->FireAction, ETriggerEvent::Started, this, &AMechCharacter::FirePressed);
		Input->BindAction(Def->FireAction, ETriggerEvent::Completed, this, &AMechCharacter::FireReleased);
		Input->BindAction(Def->MissileAction, ETriggerEvent::Started, this, &AMechCharacter::MissilePressed);
		Input->BindAction(Def->MissileAction, ETriggerEvent::Completed, this, &AMechCharacter::MissileReleased);
		Input->BindAction(Def->SlashAction, ETriggerEvent::Started, this, &AMechCharacter::SlashPressed);
		Input->BindAction(Def->SlashAction, ETriggerEvent::Completed, this, &AMechCharacter::AltReleased);
		Input->BindAction(Def->StanceAction, ETriggerEvent::Started, this, &AMechCharacter::StancePressed);
		Input->BindAction(Def->ReloadAction, ETriggerEvent::Started, this, &AMechCharacter::ReloadPressed);
	}
}

void AMechCharacter::ApplyKeyBindings()
{
	// 只在键位变了时重建：重建会让增强输入重算映射，正按着的键可能被当成松开
	static TArray<FKey> Built;
	TArray<FKey> Want;
	for (int32 i = 0; i < MechKeys::Num; ++i)
	{
		Want.Add(MechKeys::Key(static_cast<EMechKey>(i)));
	}
	if (Want == Built)
	{
		return;
	}
	Built = Want;
	GetMutableDefault<AMechCharacter>()->BuildKeyMappings();
	FString Report;
	for (int32 i = 0; i < MechKeys::Num; ++i)
	{
		Report += FString::Printf(TEXT(" %s=%s"), *MechKeys::Label(static_cast<EMechKey>(i)).ToString(), *Want[i].ToString());
	}
	UE_LOG(LogTemp, Display, TEXT("[keys] 键位：%s"), *Report);
}

void AMechCharacter::BuildKeyMappings()
{
	UInputMappingContext* IMC = MechMappingContext;
	if (!IMC)
	{
		return;
	}
	// MapKey / UnmapAll 会让正在用这份映射的本机玩家自动重建（增强输入的 RequestRebuildControlMappingsUsingContext）
	IMC->UnmapAll();
	auto Map = [IMC](const UInputAction* Action, EMechKey K, const FKey& Pad) {
		IMC->MapKey(Action, MechKeys::Key(K));
		IMC->MapKey(Action, Pad);  // 手柄固定
	};
	Map(BoostAction, EMechKey::Boost, EKeys::Gamepad_LeftShoulder);
	Map(QuickBoostAction, EMechKey::QuickBoost, EKeys::Gamepad_FaceButton_Right);
	Map(StanceAction, EMechKey::Stance, EKeys::Gamepad_FaceButton_Top);
	Map(JumpHoldAction, EMechKey::Jump, EKeys::Gamepad_FaceButton_Bottom);
	Map(FireAction, EMechKey::Fire, EKeys::Gamepad_RightTrigger);
	Map(SlashAction, EMechKey::Alt, EKeys::Gamepad_LeftTrigger);
	Map(MissileAction, EMechKey::Shoulder, EKeys::Gamepad_RightShoulder);
	Map(ReloadAction, EMechKey::Reload, EKeys::Gamepad_FaceButton_Left);
	// 移动：模板的二维 IA_Move（X 右、Y 前）。前 = 交换 XY，后 = 交换再取反，左 = 取反，右 = 原样（和模板 IMC_Default 里 WASD 的做法一样）。
	// 放在这份优先级更高的映射里；左摇杆仍走 IMC_Default
	if (MoveAction)
	{
		// 这份映射挂在类默认对象上；打包版里默认对象在「永不回收」区（DisregardForGC），运行时 NewObject 出来的修饰器
		// 只被它引用、自己不在根集里 → 回收检查直接致命退出（10-31 用户打包版连续闪退，日志 GarbageCollectionVerification.cpp:156）。
		// 编辑器里没有这个区，测不出来。修饰器没有状态，整个进程各建一个、加进根集，改键重建时重复用
		static UInputModifierSwizzleAxis* const SwizzleMod = [] {
			UInputModifierSwizzleAxis* S = NewObject<UInputModifierSwizzleAxis>(GetTransientPackage());
			S->Order = EInputAxisSwizzle::YXZ;
			S->AddToRoot();
			return S;
		}();
		static UInputModifierNegate* const NegateMod = [] {
			UInputModifierNegate* N = NewObject<UInputModifierNegate>(GetTransientPackage());
			N->AddToRoot();
			return N;
		}();
		auto Swizzle = []() { return SwizzleMod; };
		auto Negate = []() { return NegateMod; };
		IMC->MapKey(MoveAction, MechKeys::Key(EMechKey::MoveForward)).Modifiers.Add(Swizzle());
		FEnhancedActionKeyMapping& Back = IMC->MapKey(MoveAction, MechKeys::Key(EMechKey::MoveBack));
		Back.Modifiers.Add(Swizzle());
		Back.Modifiers.Add(Negate());
		IMC->MapKey(MoveAction, MechKeys::Key(EMechKey::MoveLeft)).Modifiers.Add(Negate());
		IMC->MapKey(MoveAction, MechKeys::Key(EMechKey::MoveRight));
	}
	// 模板 IMC_Default 里键盘的移动（WASD、方向键）和跳跃（空格）拿掉：不然改了键以后旧键照样能走、能跳。
	// 只改内存里加载的这份（不存盘），手柄摇杆、手柄跳跃留着
	if (UInputMappingContext* Base = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default")))
	{
		TArray<TPair<const UInputAction*, FKey>> Strip;
		for (const FEnhancedActionKeyMapping& M : Base->GetMappings())
		{
			if ((M.Action == MoveAction || M.Action == JumpAction) && !M.Key.IsGamepadKey())
			{
				Strip.Emplace(M.Action, M.Key);
			}
		}
		for (const auto& S : Strip)
		{
			Base->UnmapKey(S.Key, S.Value);
		}
	}
}

void AMechCharacter::BoostStarted()
{
	GetMechMovement()->bWantsBoost = true;
}

void AMechCharacter::BoostEnded()
{
	GetMechMovement()->bWantsBoost = false;
}

void AMechCharacter::JumpPressed()
{
	// 空格：起跳，并开始计按住时间（按住超过 0.18 秒在空中就喷射）；被网住跳不起来
	if (IsNetted())
	{
		return;
	}
	Jump();
	GetMechMovement()->bWantsThrust = true;
}

void AMechCharacter::JumpReleased()
{
	StopJumping();
	GetMechMovement()->bWantsThrust = false;
}

void AMechCharacter::QuickBoost()
{
	// 真正的闪避在移动组件里执行（随移动包发给服务器）
	GetMechMovement()->bWantsQuickBoost = true;
}

void AMechCharacter::Tick(float DeltaSeconds)
{
	{
		// -MechLookCalib：灵敏度实测。开局 3 秒后给本机玩家注入 100 个鼠标计数（走真实输入路径 PlayerInput → 增强输入 → DoLook → 控制器），量视角转了多少度
		static const bool bCalib = FParse::Param(FCommandLine::Get(), TEXT("MechLookCalib"));
		static int32 CalibStage = 0;
		static double CalibStart = 0.0;
		static float CalibYaw0 = 0.f;
		APlayerController* CalibPC = bCalib ? Cast<APlayerController>(GetController()) : nullptr;
		if (CalibPC && CalibPC->IsLocalController() && CalibPC->PlayerInput)
		{
			const double CalibNow = GetWorld()->GetTimeSeconds();
			if (CalibStage == 0 && CalibNow > 4.0)
			{
				CalibYaw0 = CalibPC->GetControlRotation().Yaw;
				CalibStart = CalibNow;
				CalibPC->PlayerInput->InputKey(FInputKeyParams(EKeys::MouseX, 100.0, 0.016f, 1));
				CalibStage = 1;
			}
			else if (CalibStage == 1 && CalibNow > CalibStart + 0.5)
			{
				const float Turned = FRotator::NormalizeAxis(CalibPC->GetControlRotation().Yaw - CalibYaw0);
				UE_LOG(LogTemp, Display, TEXT("[look] 注入 100 计数，灵敏度 %.3f，视角转了 %.3f 度（%.5f 度/计数；CS2 应为 %.5f）"), FMechSettings::Get().MouseSensitivity, Turned,
					Turned / 100.f, FMechSettings::Get().MouseSensitivity * FMechSettings::DegreesPerCount * AimFriction);
				CalibStage = 2;
			}
		}
	}
	Super::Tick(DeltaSeconds);
	// 房主机器上别人（客户端）的机体：引擎默认「收到一个移动包才走一下动画」（为了根运动和动画事件和客户端对齐），
	// 包扎堆到达时动作一顿一跳，枪、挂件跟着手骨跳。机体动作不用根运动，交回网格组件每帧走（移动组件里也不再按包走，见 TickCharacterPose）
	if (HasAuthority() && GetMesh()->bOnlyAllowAutonomousTickPose)
	{
		GetMesh()->bOnlyAllowAutonomousTickPose = false;
	}
	if (bCarryingCore)
	{
		// 带芯拿不了刀：手上是刀就收回去换枪；玩家按 G 把核心扔出去
		if (HasAuthority() && Combat->GetStance() == EMechStance::Blade && !Combat->IsMeleeActive() && Combat->GetSwitchProgress() < 0.f)
		{
			Combat->SwitchStance();
		}
		if (IsLocallyControlled() && IsPlayerControlled())
		{
			APlayerController* PC = Cast<APlayerController>(GetController());
			if (MechKeys::JustPressed(PC, EMechKey::ThrowCore))
			{
				ServerThrowCore(GetControlRotation().Vector());
			}
		}
	}
	if (bDemo)
	{
		DemoTick(DeltaSeconds);
	}
	if (bAnimTest)
	{
		AnimTestTick(DeltaSeconds);
	}
	if (bAirTest)
	{
		AirTestTick(DeltaSeconds);
	}
	if (bWallTest)
	{
		WallTestTick(DeltaSeconds);
	}
	FMechVisualTest::Tick(this, DeltaSeconds);  // -MechFallTest / -MechTracerTest（画面问题复现）
	FMechFrameLineup::Tick(this, DeltaSeconds);  // -MechFrameLineup（10-31 阵营机体九台排一排截图）
	if (bViewer)
	{
		ViewerTick(DeltaSeconds);
	}
	if (bBeauty)
	{
		BeautyTick(DeltaSeconds);
	}

	UpdateFirstPerson(DeltaSeconds);
	UpdateAimFriction();
	UpdateCameraShake(DeltaSeconds);
	// 狙击枪开镜：自己的机体和武器在自己的画面里不画（挡视线），子弹曳光、飞刀照常；骨骼照常更新（枪口位置要跟着手）
	const bool bScoped = IsLocallyControlled() && IsPlayerControlled() && !bViewer
		&& ((Combat->IsAiming() && Combat->GetGun().EnergyPerShot > 0.f) || Shoulder->GetArtilleryDeploy() >= 1.f);  // 火炮架好开镜也算
	if (bScoped != bScopeHidden)
	{
		bScopeHidden = bScoped;
		TInlineComponentArray<UPrimitiveComponent*> Prims(this);
		for (UPrimitiveComponent* P : Prims)
		{
			if (P->GetFName() != TEXT("Tracers") && P->GetFName() != TEXT("KnifeMeshes"))
			{
				P->SetOwnerNoSee(bScoped);
			}
		}
		if (bScoped)
		{
			TickOptionBeforeScope = GetMesh()->VisibilityBasedAnimTickOption;
			GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
		else
		{
			GetMesh()->VisibilityBasedAnimTickOption = TickOptionBeforeScope;
		}
	}
	if (bWrecked)
	{
		return;
	}
	if (bTrainingDummy && HasAuthority() && Health->GetSinceHit() > 4.f &&
		(Health->GetAPFraction() < 1.f || Health->GetStaggerFraction() > 0.f || FVector::Dist(GetActorLocation(), DummyHome.GetLocation()) > 50.f))
	{
		Health->ResetHealth();
		GetCharacterMovement()->StopMovementImmediately();
		TeleportTo(DummyHome.GetLocation(), DummyHome.Rotator());
	}
	UMechMovementComponent* Move = GetMechMovement();
	if (HasAuthority())
	{
		NetEnergy = Move->Energy;
		MobilityFX = (Move->bBoostActive ? 1 : 0) | (Move->bThrusting ? 2 : 0);
	}
	else if (IsLocallyControlled() && FMath::Abs(NetEnergy - Move->Energy) > 8.f)
	{
		Move->Energy = NetEnergy;
	}
	RemoteQuickFlash = FMath::Max(0.f, RemoteQuickFlash - DeltaSeconds * 4.f);
	if (IsLocallyControlled())
	{
		Move->bWantsSlow = Combat->IsAiming() || Combat->IsGuarding() || Shoulder->IsArtilleryHeld();  // 瞄准 / 格挡 / 架炮时慢走、不能冲刺（随移动包发给服务器）
	}

	// 速度越快视野越宽；右键瞄准时收窄。玩家的基础视野按设置（成图、演示、查看模式用各自固定的视野）
	float FovBase = BaseFOV;
	float FovBoost = BoostFOV;
	if (IsPlayerControlled() && !bViewer && !bBeauty && !bDemo && !bAnimTest)
	{
		FovBase = FMechSettings::Get().FieldOfView;
		FovBoost = FovBase + (BoostFOV - BaseFOV);
	}
	const float Speed01 = FMath::Clamp((Move->Velocity.Size2D() - Move->WalkSpeed) / (Move->BoostSpeed - Move->WalkSpeed), 0.f, 1.f);
	const float AimZoom = bViewer ? 1.f : Combat->GetGunType() == EMechShooter::Sniper ? 0.25f : Combat->GetGunType() == EMechShooter::Marksman ? 0.45f : 0.6f;  // 模型查看不放大
	const float ArtyZoom = FMath::Lerp(1.f, 0.42f, Shoulder->GetArtilleryDeploy());  // 火炮架好开镜
	const float TargetFOV = Combat->IsAiming() ? FovBase * AimZoom
		: Shoulder->IsArtilleryHeld() ? FovBase * ArtyZoom : FMath::Lerp(FovBase, FovBoost, FMath::Max(Speed01, Move->QuickBoostFlash));
	FovPunch = FMath::FInterpTo(FovPunch, 0.f, DeltaSeconds, 9.f);
	const float SmoothFOV = FMath::FInterpTo(GetFollowCamera()->FieldOfView + AppliedFovPunch, TargetFOV, DeltaSeconds, 6.f);
	GetFollowCamera()->SetFieldOfView(SmoothFOV - FovPunch);
	AppliedFovPunch = FovPunch;
	UpdateFlash(DeltaSeconds);

	UpdateThrusterFX(DeltaSeconds);

	// 交战中机体转向瞄准方向（AI 有目标就一直对着），否则朝移动方向
	const bool bFace = Combat->IsEngaged() || (!IsPlayerControlled() && Combat->GetTarget());
	Move->bOrientRotationToMovement = !bFace;
	Move->bUseControllerDesiredRotation = bFace;
	UpdatePose(DeltaSeconds);

	if (ThrustFX > 0.05f)
	{
		if (!BoostAudio->IsPlaying())
		{
			BoostAudio->Play();
		}
		BoostAudio->SetVolumeMultiplier(0.16f + ThrustFX * 0.34f);
		BoostAudio->SetPitchMultiplier(0.9f + ThrustFX * 0.25f);
	}
	else if (BoostAudio->IsPlaying())
	{
		BoostAudio->FadeOut(0.25f, 0.f);
	}
}

void AMechCharacter::UpdatePose(float DeltaSeconds)
{
	UMechMeshComponent* M = GetMechMesh();
	// 按画面上（网格）的朝向摆：别人的机体胶囊会随网络修正一跳一跳，网格是平滑的
	const FQuat Vis = M->VisualActorQuat();
	const FVector Fwd = Vis.GetForwardVector();
	const FVector Right = Vis.GetRightVector();
	const FVector Up = FVector::UpVector;
	const float Blend = FMath::Clamp(DeltaSeconds * 14.f, 0.f, 1.f);
	auto Toward = [](const FVector& From, const FVector& To, float Alpha) {
		return FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(From, To), Alpha).RotateVector(From).GetSafeNormal();
	};

	// 右臂（枪）：交战时指向瞄准方向，否则枪口朝前下方低姿端着；换弹时枪口压低、枪身翻过来
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bReload = Combat->IsReloading();
	const float ReloadP = bReload ? Combat->GetReloadProgress() : 0.f;
	const float ReloadBlend = bReload ? FMath::Sin(FMath::Clamp(ReloadP, 0.f, 1.f) * PI) : 0.f;
	// 10-31 展示姿势（结算 / 主菜单）不算交战：不按瞄准点端枪、上身不在模型空间立住
	const bool bEngaged = !bDisplayPose && (Combat->IsEngaged() || (!IsPlayerControlled() && Combat->GetTarget()));
	// 本机操控的跟得紧（甩鼠标时枪管不拖在准星后面）；别人的机体瞄准点是网络同步来的、一段一段，慢一点抹平
	// 10-31 联机抖动：限速转（每秒 900 度）对一包一包的小跳变不起作用（每帧 15 度以内的跳原样过去），按插值缓冲回放的别人的机体
	// 改成按 0.08 秒指数追（方向连续；回放本来就比最新的包晚约 0.1 秒，追的这点滞后和画面上的机体差不多同步）
	if (GetMechMovement()->IsInterpActive())
	{
		PoseAim = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(PoseAim, Combat->GetPoseAimDirection()),
			1.f - FMath::Exp(-DeltaSeconds / 0.08f)).RotateVector(PoseAim).GetSafeNormal();
	}
	else
	{
		PoseAim = FMath::VInterpNormalRotationTo(PoseAim, Combat->GetPoseAimDirection(), DeltaSeconds, IsLocallyControlled() ? 1800.f : 900.f);
	}
	M->AimOffset = PoseAim * FMath::Max(FVector::Dist(Combat->GetPoseAimPoint(), Combat->GetPoseAimPivot()), 500.f);
	M->GunMuzzleLocal = Combat->GetGun().Muzzle;
	FVector GunWant = bEngaged ? PoseAim : (Fwd * 0.75f - Up * 0.6f + Right * 0.1f).GetSafeNormal();
	if (bReload)
	{
		GunWant = Toward(GunWant, (Fwd * 0.55f - Up * 0.55f - Right * 0.35f).GetSafeNormal(), FMath::Min(1.f, ReloadBlend * 1.4f));
	}
	// 后坐：每发子弹枪口上跳一下、枪身往后坐，很快回位（重的枪跳得多、回得慢；双持只跳开枪的那只手）
	const MechWeapons::FGun& Gun = Combat->GetGun();
	const float KickScale = Gun.Kick / 16.f;
	const float Kick = FMath::Clamp(1.f - (Now - Combat->GetLastShotTime()) / (0.09f * FMath::Max(1.f, KickScale)), 0.f, 1.f);
	const float KickR = Gun.bDual && Combat->GetLastShotHand() == 1 ? 0.f : Kick;
	const float KickL = Gun.bDual && Combat->GetLastShotHand() == 1 ? Kick : 0.f;
	const FVector AimWant = GunWant;
	GunWant = Toward(AimWant, FVector::UpVector, KickR * 0.06f * KickScale);
	M->ArmDir[1] = Toward(M->ArmDir[1].GetSafeNormal(), GunWant, bEngaged ? FMath::Clamp(DeltaSeconds * 22.f, 0.f, 1.f) : Blend);
	// 10-31 握持：枪的原点相对手腕按标准握法算（握把在手心里），以前固定在手腕前 5 厘米（RifleGrip）
	const FVector GunGrip = OldGrip() ? RifleGrip : M->GripPropOffset(1, GunGripShape(Gun));
	M->PropOffset[1] = GunGrip - FVector(KickR * 9.f * FMath::Min(KickScale, 2.f), 0.f, 0.f);
	M->PropRoll[1] = ReloadBlend * 70.f;
	M->PropOffset[3] = (OldGrip() ? RifleGrip : M->GripPropOffset(0, GunGripShape(Gun))) - FVector(KickL * 9.f, 0.f, 0.f);
	M->PropRoll[3] = -ReloadBlend * 70.f;
	// 导弹巢：齐射时抬头，打完慢慢放平；钩爪 / 网发射器：发射时往上一跳（后坐）
	if (Loadout.Shoulder == EMechShoulder::Missiles)
	{
		const float Salvo = FMath::Clamp(1.f - (Now - Combat->GetLastMissileTime() - 0.3f) / 0.6f, 0.f, 1.f);
		M->PropPitch[2] = FMath::FInterpTo(M->PropPitch[2], Salvo * 28.f, DeltaSeconds, 12.f);
	}
	else
	{
		const float Since = Now - Shoulder->GetLastFireTime();
		const float Aim = Shoulder->IsArtilleryDeployed() ? FMath::Clamp(Combat->GetPoseAimDirection().Rotation().Pitch, -15.f, 40.f) : 0.f;
		M->PropPitch[2] = Since < 0.05f ? Aim + 14.f : FMath::FInterpTo(M->PropPitch[2], Aim, DeltaSeconds, 7.f);
	}

	// 左臂：射击姿态下平时垂着，换弹时伸到枪下面换弹匣；双持手枪左手也端一把、跟着瞄
	const FVector Rest = (Fwd * 0.45f - Up * 0.85f - Right * 0.2f).GetSafeNormal();
	if (Gun.bDual)
	{
		FVector LeftWant = bEngaged ? AimWant : (Fwd * 0.75f - Up * 0.6f - Right * 0.1f).GetSafeNormal();
		if (bReload)
		{
			LeftWant = Toward(LeftWant, (Fwd * 0.55f - Up * 0.55f + Right * 0.35f).GetSafeNormal(), FMath::Min(1.f, ReloadBlend * 1.4f));
		}
		LeftWant = Toward(LeftWant, FVector::UpVector, KickL * 0.06f * KickScale);
		M->ArmDir[0] = Toward(M->ArmDir[0].GetSafeNormal(), LeftWant, bEngaged ? FMath::Clamp(DeltaSeconds * 22.f, 0.f, 1.f) : Blend);
	}
	else if (bReload && ReloadP > 0.2f && ReloadP < 0.8f)
	{
		M->ArmDir[0] = Toward(M->ArmDir[0].GetSafeNormal(), (Fwd * 0.6f + Right * 0.55f - Up * 0.45f).GetSafeNormal(), Blend);
	}
	else
	{
		M->ArmDir[0] = Toward(M->ArmDir[0].GetSafeNormal(), Rest, Blend);
	}

	// 姿态：刀在手上时两条手臂交给这把刀的动作，程序手臂淡出；武器换挂点（拔刀 / 收刀过半时换）。
	// 长枪有步枪包动作时手臂也交给动作（双手端，枪按两只手摆）；双持手枪平时程序瞄准，换弹时交给动作
	const bool bBladeHand = Combat->IsBladeInHand();
	DrawBlend = FMath::FInterpTo(DrawBlend, bBladeHand ? 1.f : 0.f, DeltaSeconds, 12.f);
	const UMechAnimInstance* Anim = Cast<UMechAnimInstance>(M->GetAnimInstance());
	const bool bRifleAnim = !Gun.bDual && Anim && Anim->HasClip(MechClip::RifleAim);
	const bool bDualReloadAnim = Gun.bDual && bReload && Anim && Anim->HasClip(MechClip::DualReload);
	TwoHandBlend = FMath::FInterpTo(TwoHandBlend, bRifleAnim ? 1.f : 0.f, DeltaSeconds, 10.f);
	const bool bAimLock = bEngaged && !bBladeHand && !bReload && !Health->IsStaggered() && Combat->GetSwitchProgress() < 0.f;
	GunAimLock = FMath::FInterpTo(GunAimLock, bAimLock ? 1.f : 0.f, DeltaSeconds, bAimLock ? 16.f : 6.f);
	const float GunArms = bRifleAnim ? (1.f - TwoHandBlend) : bDualReloadAnim ? 1.f - ReloadBlend : 1.f;
	M->ArmWeight[0] = M->ArmWeight[1] = (1.f - DrawBlend) * GunArms;
	// 端在前臂上的枪交战时按「从肩看瞄准点」修视差（双持两只手都修，单手枪只修右手；换弹时不修）
	M->ArmPointWeight[1] = FMath::FInterpTo(M->ArmPointWeight[1], bEngaged && !bReload ? 1.f : 0.f, DeltaSeconds, 12.f);
	M->ArmPointWeight[0] = Gun.bDual ? M->ArmPointWeight[1] : 0.f;
	// 左手握到这把枪的护木上（换弹时左手要去换弹匣，松开）；-MechNoGrip 关掉（对比用）
	// 刀在手上（双手刀）时左手握刀柄
	static const bool bNoGrip = FParse::Param(FCommandLine::Get(), TEXT("MechNoGrip"));
	const MechWeapons::FBlade& HeldBlade = Combat->GetBlade();
	M->LeftGripProp = bBladeHand ? 0 : 1;
	M->bLeftGripAdaptive = bBladeHand;
	M->LeftGripLocal = bNoGrip ? FVector::ZeroVector : bBladeHand ? HeldBlade.LeftGrip : Gun.ForeGrip;
	// 10-31 握持：双手刀按 LeftAt（左手手心对准柄上那一点）、长枪按护木（ForeCenter），以前按 LeftGrip / ForeGrip（手腕拉过去）
	const bool bLeftHold = OldGrip() ? (bBladeHand ? !HeldBlade.LeftGrip.IsZero() : true)
		: (bBladeHand ? HeldBlade.LeftAt != 0.f : !Gun.ForeCenter.IsZero());
	const float GripWant = bNoGrip || !bLeftHold ? 0.f : bBladeHand ? DrawBlend : bRifleAnim && !bReload ? TwoHandBlend : 0.f;
	M->LeftGripWeight = FMath::FInterpTo(M->LeftGripWeight, GripWant, DeltaSeconds, 12.f);
	// 长枪（双手端、手臂交给动作）：上身转到枪管对准准星；交战时全对准，平时也大致朝着准星；换弹时放开
	const float GunAimWant = (bRifleAnim && !bBladeHand && !bReload ? (bEngaged ? 1.f : 0.6f) : 0.f) * TwoHandBlend;
	// 开火时马上端正（原来 0.6 → 1 要 0.2 秒，头几发枪管还歪着），放下时慢一点
	M->GunAimWeight = FMath::FInterpTo(M->GunAimWeight, GunAimWant, DeltaSeconds, GunAimWant > M->GunAimWeight ? 24.f : 8.f);
	if (bDisplayPose)
	{
		// 10-31 展示姿势：上身不去对枪管（以前没交战时也按六成去对，上身跟着枪拧过去）；端着的长枪由网格组件 ShowGunDown 低姿压枪口
		M->GunAimWeight = 0.f;
	}
	if (bResultsStow)
	{
		// 结算展示竖大拇指：枪收在背上，上身不再去对枪管、左手不去握护木
		M->GunAimWeight = 0.f;
		M->LeftGripWeight = 0.f;
	}
	const MechWeapons::FBlade& Blade = Combat->GetBlade();
	const bool bLongBlade = Blade.Reach >= 420.f;
	if (bBladeHand)
	{
		// 10-31 握持：刀按标准握法挂（柄心线穿过手心空腔、刀身方向照动作包），左手那把（双刀）按左手自己的手算
		if (!OldGrip())
		{
			const FMechGripShape Shape = BladeGripShape(Blade, false);
			M->SetPropOnBoneLocal(0, TEXT("hand_r"), M->GripPropInHand(1, Shape));
			M->SetPropOnBoneLocal(4, TEXT("hand_l"), M->GripPropInHand(0, Shape));
		}
		// 以前：按这把刀用的动作包的握法挂（大剑、锤子、长剑……各不相同）；左手那把（双刀）镜像：位置取反、刀身和刀背方向取反
		else if (Blade.bMount)
		{
			const FTransform RightMount(Blade.MountRot, Blade.MountPos);
			const FQuat LeftRot = FRotationMatrix::MakeFromXZ(-Blade.MountRot.GetAxisX(), -Blade.MountRot.GetAxisZ()).ToQuat();
			M->SetPropOnBoneLocal(0, TEXT("hand_r"), RightMount);
			M->SetPropOnBoneLocal(4, TEXT("hand_l"), FTransform(LeftRot, -Blade.MountPos));
		}
		else
		{
			M->SetPropOnBoneLocal(0, TEXT("hand_r"), SwordInHand);
			M->SetPropOnBoneLocal(4, TEXT("hand_l"), SwordInLeftHand);
		}
		if (Gun.bDual)
		{
			M->SetPropOnBone(1, TEXT("pelvis"), PistolStowOffset, PistolStowRot);
			M->SetPropOnBone(3, TEXT("pelvis"), PistolStowOffset * FVector(1.f, -1.f, 1.f), PistolStowRot);
		}
		else
		{
			M->SetPropOnBone(1, TEXT("spine_03"), RifleStowOffset, RifleStowRot);
		}
	}
	else
	{
		if (bResultsStow)
		{
			// 结算展示竖大拇指（MechResultsStage）：枪按近战姿态的挂法收起来，刀照旧挂着——配装照样看得见
			if (Gun.bDual)
			{
				M->SetPropOnBone(1, TEXT("pelvis"), PistolStowOffset, PistolStowRot);
				M->SetPropOnBone(3, TEXT("pelvis"), PistolStowOffset * FVector(1.f, -1.f, 1.f), PistolStowRot);
			}
			else
			{
				M->SetPropOnBone(1, TEXT("spine_03"), RifleStowOffset, RifleStowRot);
			}
		}
		else if (bRifleAnim)
		{
			M->SetPropTwoHand(1, bReload);
		}
		else
		{
			M->SetPropOnForearm(1, 1);
		}
		if (!bResultsStow)
		{
			M->SetPropOnForearm(3, 0);
		}
		if (bLongBlade)
		{
			M->SetPropOnBone(0, TEXT("spine_03"), LongBladeStowOffset, LongBladeStowRot);
		}
		else
		{
			M->SetPropOnBone(0, TEXT("pelvis"), SwordStowOffset, SwordStowRot);
		}
		M->SetPropOnBone(4, TEXT("pelvis"), SwordStowOffsetR, SwordStowRot);
	}
	// 投掷匕首：手上的那把投光了就不显示，回来一把再出现
	BladeMesh->SetVisibility(Blade.Knives <= 0 || Combat->GetKnives() > 0 || !bBladeHand);

	// 10-31 握持：两只手怎么握（网格组件在骨骼定稿时按这个转手、弯手指）。切姿态时手松开（武器换手那一刻完全松开），
	// 匕首投光时右手松开；格挡时刀还在手里，照样握着；阵亡（布娃娃）时网格组件不做程序姿势，手指照动作
	{
		FMechHandGrip& R = M->Grip[1];
		FMechHandGrip& L = M->Grip[0];
		if (OldGrip())
		{
			R.Mode = L.Mode = EMechGripMode::None;
		}
		else
		{
			const float Switch = Combat->GetSwitchProgress();
			const float Hold = Switch < 0.f ? 1.f : FMath::Abs(2.f * FMath::Clamp(Switch, 0.f, 1.f) - 1.f);
			L.Mode = EMechGripMode::None;
			L.Weight = Hold;
			R.Weight = Hold;
			if (bBladeHand)
			{
				R.Mode = EMechGripMode::Prop;
				R.Prop = 0;
				R.Shape = BladeGripShape(Blade, false);
				R.Weight = Blade.Knives > 0 && Combat->GetKnives() <= 0 ? 0.f : Hold;
				if (Blade.bTwin)
				{
					L.Mode = EMechGripMode::Prop;
					L.Prop = 4;
					L.Shape = R.Shape;
				}
				else if (Blade.LeftAt != 0.f)
				{
					L.Mode = EMechGripMode::Support;
					L.Prop = 0;
					L.Shape = BladeGripShape(Blade, true);
					L.bKeepRoll = true;
				}
			}
			else
			{
				R.Mode = EMechGripMode::HandToProp;
				R.Prop = 1;
				R.Shape = GunGripShape(Gun);
				if (Gun.bDual)
				{
					L.Mode = EMechGripMode::HandToProp;
					L.Prop = 3;
					L.Shape = R.Shape;
				}
				else if (bRifleAnim && !Gun.ForeCenter.IsZero())
				{
					L.Mode = EMechGripMode::Support;
					L.Prop = 1;
					L.Shape = ForeGripShape(Gun);
					L.bKeepRoll = false;
				}
			}
		}
		if (bResultsStow)
		{
			R.Mode = L.Mode = EMechGripMode::None;  // 枪收起来了：右手竖大拇指（网格组件握拳），左手照动作
		}
	}

	// 开枪后坐：每发上身往后一顶，带一点左右扭和侧倾（弹簧回位，冲锋枪连发会一直颤）；
	// 霰弹、精准步枪、狙击这种重的整台机体以脚为支点往后晃一下。幅度按枪的后坐（Kick）分档，各端按同步来的开枪时间各自算
	const float ShotTime = Combat->GetLastShotTime();
	if (ShotTime > LastRecoilShot)
	{
		const bool bFresh = Now - ShotTime < 0.2f;  // 刚进视野 / 刚换武器时不补播旧的
		LastRecoilShot = ShotTime;
		if (bFresh)
		{
			const float V0 = Gun.Kick * 10.f;  // 角速度（度/秒）：步枪约顶 3 度，狙击约 10 度
			const float Side = Gun.bDual ? (Combat->GetLastShotHand() == 1 ? 1.f : -1.f) : FMath::FRandRange(-1.f, 1.f);
			RecoilVel += Right * -V0 + Up * (V0 * 0.3f * Side) + Fwd * (V0 * FMath::FRandRange(-0.2f, 0.2f));
			BodyRockVel += FMath::Max(0.f, Gun.Kick - 20.f) * 4.f;
		}
	}
	const float SpringDt = FMath::Min(DeltaSeconds, 0.033f);
	RecoilVel += (-Recoil * 400.f - RecoilVel * 26.f) * SpringDt;
	Recoil += RecoilVel * SpringDt;
	BodyRockVel += (-BodyRock * 150.f - BodyRockVel * 16.f) * SpringDt;
	BodyRock += BodyRockVel * SpringDt;

	// 上身：失衡后仰（有失衡动作时只留一点）、中弹一抖（衰减很快）、开枪后坐
	StaggerBlend = FMath::FInterpTo(StaggerBlend, Health->IsStaggered() ? 1.f : 0.f, DeltaSeconds, 6.f);
	Flinch = FMath::VInterpTo(Flinch, FVector::ZeroVector, DeltaSeconds, 12.f);
	M->TorsoTwist = Right * (-6.f * StaggerBlend + ResultsLean) + Flinch + Recoil;  // ResultsLean：结算展示（败方前倾、胜方挺胸）
	// 10-31 展示姿势：只留前倾 / 挺胸（没有后坐、中弹、失衡），上身转正；手里端着长枪（不竖拇指、不拿刀）时低姿压枪口
	M->ShowSquare = bDisplayPose ? 1.f : 0.f;
	M->ShowGunDown = bDisplayPose && !bResultsStow && !bBladeHand && bRifleAnim ? ShowGunDown : -1.f;
	if (bDisplayPose)
	{
		M->TorsoTwist = Right * ResultsLean;
	}
}

// ---------------------------------------------------------------- 动作层（给 UMechAnimInstance）
void AMechCharacter::StartDodgeAnim(const FVector& WorldDir)
{
	const FVector L = GetActorTransform().InverseTransformVectorNoScale(WorldDir.GetSafeNormal2D());
	const float Deg = FMath::RadiansToDegrees(FMath::Atan2(L.Y, L.X));  // 0 前，90 右
	DodgeDir = (FMath::RoundToInt(Deg / 45.f) % 8 + 8) % 8;
	DodgeStart = GetWorld()->GetTimeSeconds();
}

void AMechCharacter::PlayClashRecoil(const FVector& WorldFrom)
{
	StartStaggerAnim(WorldFrom);
	StaggerAnimLen = MechWeapons::ClashRecoilAnim;
}

void AMechCharacter::StartStaggerAnim(const FVector& WorldFrom)
{
	StaggerAnimLen = 1.2f;
	const FVector L = GetActorTransform().InverseTransformVectorNoScale(WorldFrom.GetSafeNormal2D());
	const float Deg = FMath::RadiansToDegrees(FMath::Atan2(L.Y, L.X));  // 受击来自：0 前，90 右
	StaggerDir = (FMath::RoundToInt(Deg / 90.f) % 4 + 4) % 4;
	StaggerStart = GetWorld()->GetTimeSeconds();
}

void AMechCharacter::DebugDodge(const FVector& LocalDir)
{
	StartDodgeAnim(GetActorTransform().TransformVectorNoScale(LocalDir));
}

void AMechCharacter::DebugStagger(const FVector& LocalFrom)
{
	StartStaggerAnim(GetActorTransform().TransformVectorNoScale(LocalFrom));
}

void AMechCharacter::GetActionAnim(FMechActionAnim& Out) const
{
	const float Now = GetWorld()->GetTimeSeconds();
	auto Fade = [](float T, float In, float Len, float OutTime) { return FMath::Clamp(FMath::Min(T / In, (Len - T) / OutTime), 0.f, 1.f); };
	// 这把刀的片段：0 号双手刀用武士刀那组，其他刀各一组（缺的动画实例里已经用武士刀的补上）
	const int32 BladeIndex = static_cast<int32>(Combat->GetBladeType());
	auto BladeClip = [BladeIndex](int32 Slot) {
		static const int32 Katana[MechClip::BladeStride] = {MechClip::KIdle, MechClip::KLight, MechClip::KLight + 1, MechClip::KLight + 2, MechClip::KLight + 3,
			MechClip::KHeavy, MechClip::KBlockStart, MechClip::KBlockLoop, MechClip::KBlockHit, MechClip::KBlockEnd};
		return BladeIndex == 0 ? Katana[Slot] : MechClip::BladeBase + (BladeIndex - 1) * MechClip::BladeStride + Slot;
	};
	Out.StanceClip = BladeClip(MechClip::BIdle);
	Out.StanceWeight = DrawBlend;
	// 枪在手上：长枪换步枪包的瞄准姿势（按俯仰混瞄上 / 瞄下），双持手枪换双枪待机
	const MechWeapons::FGun& Gun = Combat->GetGun();
	Out.GunClip = Gun.bDual ? MechClip::DualAim : MechClip::RifleAim;
	Out.GunWeight = 1.f - DrawBlend;
	Out.AimPitch = PoseAim.Rotation().Pitch;
	Out.GunAim = GunAimLock;
	Out.RecoilLean = -GetMechMesh()->VisualActorQuat().GetForwardVector().GetSafeNormal2D() * BodyRock;
	if (bDisplayPose)
	{
		// 10-31 展示姿势：上身用待机动作、不叠端枪片段（片段是侧身抵肩瞄准，枪管比胸口朝向偏左将近 90 度，叠上去不是拧着身子就是枪横着）；
		// 竖拇指由网格组件摆右臂，端着长枪的低姿持枪由网格组件两条手臂 IK 摆（ShowGunDown），双持手枪照旧程序手臂；
		// 不混瞄上 / 瞄下片段、不在模型空间立住、没有开火后仰
		Out.GunWeight = 0.f;
		Out.AimPitch = 0.f;
		Out.GunAim = 0.f;
		Out.RecoilLean = FVector::ZeroVector;
	}
	Out.KickTime = GetSinceWallKick();
	// 动作层跳变时的过渡时长（10-30 刀剑手感）：刀在手上按这把刀的快慢（双刀 0.08 … 双手刀 0.11 … 钝器 0.15），枪 0.1
	Out.BlendTime = Combat->IsBladeInHand() ? FMath::Clamp(0.11f * Combat->GetBlade().TimeScale, 0.08f, 0.15f) : 0.1f;

	// 上身：拔刀 / 收刀，格挡（起手、持续、挡住一下、收手）
	const float Sw = Combat->GetSwitchProgress();
	if (Sw >= 0.f)
	{
		Out.UpperClip = Combat->GetStance() == EMechStance::Blade ? MechClip::KDraw : MechClip::KSheathe;
		Out.UpperTime = Sw;
		Out.UpperDuration = 1.f;
		Out.UpperWeight = FMath::Min(1.f, FMath::Sin(Sw * PI) * 1.6f);
	}
	else if (Combat->IsGuarding())
	{
		const float G = Combat->GetGuardTime();
		if (Combat->GetSinceBlock() < 0.43f)
		{
			Out.UpperClip = BladeClip(MechClip::BBlockHit);
			Out.UpperTime = Combat->GetSinceBlock();
		}
		else if (G < 0.33f)
		{
			Out.UpperClip = BladeClip(MechClip::BBlockStart);
			Out.UpperTime = G;
		}
		else
		{
			Out.UpperClip = BladeClip(MechClip::BBlockLoop);
			Out.UpperTime = G - 0.33f;
			Out.bUpperLoop = true;
		}
		Out.UpperWeight = FMath::Clamp(G / 0.08f, 0.f, 1.f);
	}
	else if (Combat->GetSinceGuardEnd() < 0.4f)
	{
		Out.UpperClip = BladeClip(MechClip::BBlockEnd);
		Out.UpperTime = Combat->GetSinceGuardEnd();
		Out.UpperWeight = 1.f - Combat->GetSinceGuardEnd() / 0.4f;
	}
	else if (Combat->IsReloading() && !Combat->IsBladeInHand())
	{
		// 换弹：整段拉到这把枪的换弹时间
		Out.UpperClip = Gun.bDual ? MechClip::DualReload : MechClip::RifleReload;
		Out.UpperTime = Combat->GetReloadProgress();
		Out.UpperDuration = 1.f;
		Out.UpperWeight = FMath::Clamp(FMath::Min(Out.UpperTime / 0.08f, (1.f - Out.UpperTime) / 0.12f), 0.f, 1.f);
	}
	else if (Now - Combat->GetLastThrowTime() < 0.55f && Combat->IsBladeInHand())
	{
		// 投掷匕首：出手那一下（片段从抬手播到甩出，拉到 0.55 秒）
		const float T = Now - Combat->GetLastThrowTime();
		Out.UpperClip = MechClip::Throw;
		Out.UpperTime = T / 0.55f;
		Out.UpperDuration = 1.f;
		Out.UpperWeight = Fade(T, 0.04f, 0.55f, 0.15f);
	}

	// 全身：失衡 > 出招 > 闪避
	const float SinceStagger = Now - StaggerStart;
	const float SinceDodge = Now - DodgeStart;
	// 11-01 拼刀弹开的那段（比 1.2 秒短）：僵直只有 0.3 秒，马上能出刀，出刀就让位，不然新的一刀被受击动作盖着
	if (SinceStagger < StaggerAnimLen && !bWrecked && !(StaggerAnimLen < 1.2f && Combat->IsMeleeActive()))
	{
		Out.FullClip = MechClip::KStagger + StaggerDir;
		Out.FullTime = SinceStagger;
		Out.FullWeight = Fade(SinceStagger, 0.05f, StaggerAnimLen, 0.3f);
		Out.bFullGroundLegs = true;
	}
	else if (Combat->IsMeleeActive())
	{
		const int32 Step = Combat->GetComboStep();
		Out.FullClip = Step > 0 ? BladeClip(MechClip::BLight1 + Step - 1) : BladeClip(MechClip::BHeavy);
		Out.FullTime = Combat->GetMeleeTime();
		Out.FullStrikeAt = Combat->GetMeleeStrikeAt();
		Out.FullDuration = Combat->GetMeleeDuration();
		Out.FullWeight = Fade(Out.FullTime, 0.05f, Out.FullDuration, 0.18f);
		static const bool bMeleeOld = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeOld"));
		if (!bMeleeOld)
		{
			// 10-30 刀剑手感：起手不再 0.05 秒线性淡入（从当时画面上的姿势缓入片段第一帧，由动画里的过渡做，连招下一段也一样）；
			// 收招按平滑曲线淡回移动 / 待机（快刀 0.16 秒 … 钝器 0.28 秒），腿怎么混见 FMechAnimProxy::MeleeLayer
			Out.bFullMelee = true;
			Out.FullCancel = Combat->GetMeleeCancel();
			const float X = FMath::Clamp((Out.FullDuration - Out.FullTime) / FMath::Clamp(Out.BlendTime * 1.9f, 0.16f, 0.28f), 0.f, 1.f);
			Out.FullWeight = X * X * (3.f - 2.f * X);
		}
	}
	else if (SinceDodge < 0.5f)
	{
		Out.FullClip = MechClip::KDodge + DodgeDir;
		Out.FullTime = SinceDodge;
		Out.FullDuration = 0.5f;
		Out.FullWeight = Fade(SinceDodge, 0.05f, 0.5f, 0.12f);
		Out.bFullGroundLegs = true;
	}
	if (Out.FullClip >= 0 && !Out.bFullMelee)
	{
		Out.BlendTime = 0.08f;  // 闪避、失衡要快
	}
}

void AMechCharacter::UpdateCameraShake(float DeltaSeconds)
{
	if (bBeauty || !IsLocallyControlled())
	{
		return;
	}
	Trauma = FMath::Max(0.f, Trauma - DeltaSeconds * 1.8f);
	const float Shake = Trauma * Trauma;
	const float T = GetWorld()->GetRealTimeSeconds() * 28.f;
	// 冲击弹簧（略欠阻尼：推出去、弹回来稍微过一点）；顿帧时这台机体的时间几乎停住，镜头也停在推出去的位置
	const float Dt = FMath::Min(DeltaSeconds, 0.033f);
	KickOffsetVel += (-KickOffset * 320.f - KickOffsetVel * 24.f) * Dt;
	KickOffset += KickOffsetVel * Dt;
	KickRotVel += (-KickRot * 260.f - KickRotVel * 22.f) * Dt;
	KickRot += KickRotVel * Dt;
	const float ShakeMove = bFirstPerson ? 0.35f : 1.f;
	GetCameraBoom()->SocketOffset = BaseSocketOffset + (KickOffset +
		FVector(0.f, FMath::PerlinNoise1D(T) * 22.f, FMath::PerlinNoise1D(T + 37.f) * 16.f) * Shake) * ShakeMove;
	GetFollowCamera()->SetRelativeRotation(FRotator(FMath::PerlinNoise1D(T + 71.f) * 2.5f, FMath::PerlinNoise1D(T + 13.f) * 2.5f,
		FMath::PerlinNoise1D(T + 5.f) * 4.f) * Shake + FRotator(KickRot.X, KickRot.Y, KickRot.Z));
	Fringe = FMath::Max(0.f, Fringe - DeltaSeconds * 5.f);
	FPostProcessSettings& PP = GetFollowCamera()->PostProcessSettings;
	// 动态模糊按设置开关（每帧读，菜单里改了立即生效；镜头后期覆盖关卡里的后期体积，设 0 就是全关）
	PP.MotionBlurAmount = FMechSettings::Get().bMotionBlur ? 0.2f : 0.f;
	PP.bOverride_SceneFringeIntensity = Fringe > 0.f;
	PP.SceneFringeIntensity = Fringe * 4.f;
}

void AMechCharacter::OnHealthHit(float Damage, float Impact, AActor* Attacker, FVector At, bool bDirect)
{
	LastHitFrom = At - GetActorLocation();
	LastHitAttacker = Attacker;
	if (bTrainingDummy)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now - DummyLastHitTime > 3.f)
		{
			DummyStreakDamage = 0.f;
			DummyStreakStart = Now;
		}
		DummyStreakDamage += Damage;
		DummyLastHit = Damage;
		DummyLastHitTime = Now;
	}
	// 上身朝受力方向一抖，冲击越大抖得越狠
	const FVector From = (At - GetActorLocation()).GetSafeNormal2D();
	const FVector Axis = FVector::CrossProduct(FVector::UpVector, From).GetSafeNormal();
	Flinch += Axis * FMath::Clamp(Impact / 35.f, 1.2f, 14.f);
	LastDamagedTime = GetWorld()->GetTimeSeconds();
	AddRecoil(FMath::Clamp(Impact / 700.f, 0.05f, 0.8f));
	// 打击感：导弹、刀这种重的打中会顿一下（冲击越大顿得越久），镜头往受力方向被推开，整台机体闪一下（直击 / 失衡中更亮、偏白）
	if (Impact >= 150.f)
	{
		HitStop(FMath::GetMappedRangeValueClamped(FVector2D(150.f, 950.f), FVector2D(0.03f, 0.09f), Impact));  // 原 0.05–0.16，用户嫌粘
	}
	CameraKick(-From, FMath::Clamp(Impact / 900.f, 0.08f, 1.f));
	// 击退：按受击点方向推开（刀的受击点在两台中间，导弹是爆点），冲击越大推得越远，重的还带一点离地；格挡后的冲击只剩两成，挡住时只被推一小步。
	// 服务器和操控这台的客户端各推一次（客户端先预测），只在服务器推的话客户端不知道被推了，位置对不上会被一次次拉回
	// 化境（双手刀）没有击退：攻击者是双手刀并且正在出招
	const AMechCharacter* HitBy = Cast<AMechCharacter>(Attacker);
	const bool bNoKnock = HitBy && HitBy->GetCombat() && HitBy->GetCombat()->GetBladeType() == EMechMeleeWeapon::Katana && HitBy->GetCombat()->IsMeleeActive();
	if (Impact >= 150.f && !bNoKnock && !Health->IsDead() && (HasAuthority() || IsLocallyControlled()))
	{
		const float Speed = FMath::GetMappedRangeValueClamped(FVector2D(150.f, 950.f), FVector2D(320.f, 1500.f), Impact) * KnockbackScale;
		LaunchCharacter(-From * Speed + FVector(0.f, 0.f, Impact >= 600.f ? 260.f : 90.f), false, false);
		if (KnifePushFrame == GFrameCounter && GetCharacterMovement())
		{
			GetCharacterMovement()->Launch(GetCharacterMovement()->PendingLaunchVelocity + KnifePush);  // 同一帧先到的匕首近距离击退（AddKnifePush）被上面这句盖掉了，叠回去
		}
	}
	const float FlashStrength = FMath::GetMappedRangeValueClamped(FVector2D(20.f, 950.f), FVector2D(0.35f, 1.6f), Impact) * (bDirect ? 1.5f : 1.f);  // 太亮整台会变成一团白
	FlashBody(bDirect ? FLinearColor(1.f, 0.85f, 0.75f) : FLinearColor(1.f, 0.5f, 0.2f), FlashStrength);
	// -MechHitShots：本机玩家被打中（重的每次、子弹每 2 秒一张）截图
	static const bool bHitShots = FParse::Param(FCommandLine::Get(), TEXT("MechHitShots"));
	static int32 HurtShotIndex = 0;
	static double LastHurtShot = -100.0;
	if (bHitShots && IsLocallyControlled() && IsPlayerControlled() && (Impact >= 150.f || FPlatformTime::Seconds() - LastHurtShot > 2.0))
	{
		LastHurtShot = FPlatformTime::Seconds();
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Hits") /
			FString::Printf(TEXT("被打_%03d_冲击%.0f.png"), HurtShotIndex++, Impact), true, false);
	}
}

float AMechCharacter::GetDummyDPS() const
{
	return DummyStreakDamage / FMath::Max(1.f, DummyLastHitTime - DummyStreakStart);
}

void AMechCharacter::MakeTrainingDummy()
{
	bTrainingDummy = true;
	Health->bNoDeath = true;
	DummyHome = GetActorTransform();
}

void AMechCharacter::OnRep_Loadout()
{
	ApplyLoadout();
}

void AMechCharacter::SetLoadout(const FMechLoadout& InLoadout, bool bRefill)
{
	Loadout = InLoadout;
	ApplyLoadout();
	if (bRefill)
	{
		Health->ResetHealth();
		GetMechMovement()->Energy = GetMechMovement()->MaxEnergy;
	}
}

void AMechCharacter::ApplyLoadout()
{
	// 都从默认值（中型框架 + 均衡型推进器）算起，换几次都不会叠加
	const UMechMovementComponent* Def = GetDefault<UMechMovementComponent>();
	UMechMovementComponent* M = GetMechMovement();
	// 框架 = 档位 × 制造商特色（10-31 用户：三家框架各有特色），数值都在 MechCatalog::FrameSpec（配装界面的参数也读它）
	const MechCatalog::FFrameSpec Spec = MechCatalog::FrameSpec(Loadout.Frame);
	const float Speed = Spec.Speed, Regen = Spec.Regen;
	KnockbackScale = Spec.Knockback;
	float Boost = Def->BoostSpeed;
	float Quick = Def->QuickBoostSpeed;
	M->QuickBoostCost = Def->QuickBoostCost;
	M->QuickBoostCooldown = Def->QuickBoostCooldown;
	M->BoostDrain = Def->BoostDrain;
	M->ThrustUpAccel = Def->ThrustUpAccel;
	M->ThrustDrain = Def->ThrustDrain;
	M->MaxRiseSpeed = Def->MaxRiseSpeed;
	M->AirBoostAccel = Def->AirBoostAccel;
	M->AirBoostGravity = Def->AirBoostGravity;
	M->bCanThrust = true;
	M->bCanAirBoost = true;
	M->GroundRegenScale = 1.f;
	M->AirRegenScale = Def->AirRegenScale;
	// 11-02 推进器固定的图（石林）：玩家存的推进器不算数，人人都从均衡型的底子起，飞行手感在下面交给移动组件
	const EMechBooster EffectiveBooster = MechMapRules::FlightFor(GetWorld()).bFixedBooster ? EMechBooster::Balanced : Loadout.Booster;
	switch (EffectiveBooster)
	{
	case EMechBooster::Ground:  // 不能飞；地面冲刺 75 km/h，闪避更快更省、冷却短（能连闪两次），着地回能 +40%
		Boost = 2100.f, Quick = 3050.f;
		M->QuickBoostCost = 15.f;
		M->QuickBoostCooldown = 0.16f;
		M->bCanThrust = false;
		M->bCanAirBoost = false;
		M->GroundRegenScale = 1.4f;
		break;
	case EMechBooster::Flight:  // 喷射更强、空中滑翔更快更省、空中也回能；地面冲刺 45 km/h，闪避更慢更费
		Boost = 1260.f, Quick = 2380.f;
		M->QuickBoostCost = 26.f;
		M->ThrustUpAccel = 3000.f;
		M->ThrustDrain = 13.f;
		M->MaxRiseSpeed = 900.f;
		M->AirBoostAccel = 3200.f;
		M->AirBoostGravity = 0.25f;
		M->AirRegenScale = 0.8f;
		M->GroundRegenScale = 0.9f;
		break;
	default:
		break;
	}
	M->WalkSpeed = Def->WalkSpeed * Speed;
	M->BoostSpeed = Boost * Speed;
	M->QuickBoostSpeed = Quick * Speed;
	M->RegenPerSecond = Def->RegenPerSecond * Regen;
	M->QuickBoostCost *= Spec.QuickCost;  // 相位电子：快速闪避省能量
	// 11-02 按地图的飞行规则（石柱山水，数值都在 MechMapRules.cpp；别的图都是默认）：服务器和客户端按地图名算的一样，移动预测不会被拉回
	const MechMapRules::FFlight& Flight = MechMapRules::FlightFor(GetWorld());
	if (Flight.bFixedBooster)
	{
		M->GliderRules = &Flight;
		M->GlideSpeedScale = Spec.Speed;
		M->AirRegenScale *= Flight.AirRegen;
		M->bRegenWhileFlying = true;
	}
	else
	{
		M->GliderRules = nullptr;
		M->bRegenWhileFlying = false;
	}
	Health->SetMaxValues(Spec.AP, Spec.Stagger, false);
	Health->StaggerTimeScale = Spec.Disrupt;
	Health->bFieldRepair = Spec.bRepair;
	Shoulder->NetTimeScale = Spec.Disrupt;
	Shoulder->EmpTimeScale = Spec.Emp;
	Combat->FrameMagScale = Spec.Magazine;
	Combat->FrameSpreadScale = Spec.Spread;
	Combat->FrameLockScale = Spec.Lock;

	// 武器：换模型（双持 / 双刀才显示副手），战斗组件按新武器的数值打
	const MechWeapons::FGun& Gun = MechWeapons::Gun(Loadout.Shooter);
	const MechWeapons::FBlade& Blade = MechWeapons::Blade(Loadout.Melee);
	auto SetMesh = [](UStaticMeshComponent* W, const TCHAR* Path) {
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		if (Mesh && W->GetStaticMesh() != Mesh)
		{
			W->SetStaticMesh(Mesh);
		}
	};
	SetMesh(RifleMesh, Gun.Mesh);
	SetMesh(BladeMesh, Blade.Mesh);
	SetMesh(OffGunMesh, Gun.Mesh);
	SetMesh(OffBladeMesh, Blade.Mesh);
	OffGunMesh->SetVisibility(Gun.bDual);
	OffBladeMesh->SetVisibility(Blade.bTwin);
	Combat->SetWeapons(Loadout.Shooter, Loadout.Melee);
	// 肩部挂件：换模型（材质槽不一样，旧的动态材质要清掉，下面重新上队伍色和涂装）
	UStaticMesh* PodWant = ShoulderMeshes[FMath::Clamp(static_cast<int32>(Loadout.Shoulder), 0, int32(UE_ARRAY_COUNT(ShoulderMeshes)) - 1)];
	if (PodWant && PodMesh->GetStaticMesh() != PodWant)
	{
		PodMesh->SetStaticMesh(PodWant);
		PodMesh->EmptyOverrideMaterials();
		ApplyPaint();
	}
	ClawMesh->SetVisibility(Loadout.Shoulder == EMechShoulder::Grapple);
	Shoulder->OnLoadoutChanged();
	Tactical->OnLoadoutChanged();  // 紧急脱离等四件的冷却清零（机身模型上面已经换了）
	ApplyFrameMesh();
	ApplyTeamColor();
}

void AMechCharacter::AddKnifePush(const FVector& Push)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move)
	{
		return;
	}
	KnifePush = Push;
	KnifePushFrame = GFrameCounter;
	// 普通击退已经排上了就加在上面；还没到（客户端组播后到）就先按当前速度排上，OnHealthHit 那边再叠
	Move->Launch((Move->PendingLaunchVelocity.IsZero() ? GetVelocity() : Move->PendingLaunchVelocity) + Push);
}

bool AMechCharacter::IsOwnMaker(int32 Category) const
{
	return MechCatalog::SameMaker(Loadout, Category);
}

EMechMaker AMechCharacter::GetFrameMaker() const
{
	return MechCatalog::FrameMaker(static_cast<int32>(Loadout.Frame));
}

void AMechCharacter::ApplyFrameMesh()
{
	// 10-31 阵营机体：框架 = 企业 × 轻中重，每种一台网格。骨架九台完全一样（SK_Mannequin + 拇指 + 减震骨头共 98 根，绑定姿势相同），
	// 动画、命中判定骨段、道具挂点都照旧；不同的是外形、喷口、材质（维克托主漆槽是迷彩）、头 / 踝限位（动画代理按网格查 MechCatalog::FrameBodyForMesh）。
	// 联机：配装是复制的（OnRep_Loadout → ApplyLoadout），各端各自换
	static const FString V3Path(TEXT("/Game/Robot/Meshes/SKM_Robot_v3"));
	const int32 FrameIndex = static_cast<int32>(Loadout.Frame);
	USkeletalMesh* Want = LoadObject<USkeletalMesh>(nullptr, MechCatalog::FrameMesh(FrameIndex));
	if (!Want)
	{
		static TSet<int32> Warned;
		if (!Warned.Contains(FrameIndex))
		{
			Warned.Add(FrameIndex);
			UE_LOG(LogTemp, Warning, TEXT("[frame] 框架 %d 的网格 %s 还没导入，先用 v3（tools/ue_import_factions.py）"), FrameIndex, MechCatalog::FrameMesh(FrameIndex));
		}
		Want = LoadObject<USkeletalMesh>(nullptr, *V3Path);
	}
	USkeletalMeshComponent* SK = GetMesh();
	if (!Want || !SK || (Want == AppliedFrameMesh && SK->GetSkeletalMeshAsset() == Want))
	{
		return;
	}
	if (SK->GetSkeletalMeshAsset() != Want)
	{
		// 引擎会重新初始化动画实例（同一个 UMechAnimInstance 类，片段是静态缓存的）和骨骼
		SK->SetSkeletalMeshAsset(Want);
	}
	AppliedFrameMesh = Want;
	// 旧网格上按槽号留着的动态材质（涂装、队伍色）清掉：九台的槽名、顺序一样，但维克托主漆槽的底材质是迷彩，不清会串
	SK->EmptyOverrideMaterials();
	if (UMechMeshComponent* M = GetMechMesh())
	{
		M->OnFrameMeshChanged();
	}
	SetupThrusterNozzles();
	ApplyPaint();
	UE_LOG(LogTemp, Verbose, TEXT("[frame] %s 换成 %s"), *GetName(), *Want->GetName());
}

void AMechCharacter::SetupThrusterNozzles()
{
	// 喷焰和推进器灯挂到推进包所在的骨头 spine_05 上（10-30 玩家：「飞行火焰的位置还是不对」）。以前按胶囊里估的一个固定点摆，
	// 只跟着网格整体走：平飞时身体前倾、悬停、闪避、失衡时背包跟着胸口动了，火焰还在原地；那个点本身也比喷口高约 35 厘米。
	// 现在按网格绑定姿势算出喷口在 spine_05 坐标里的位置 / 方向，挂上去以后任何姿势都从喷口沿喷口轴喷出来。
	// 骨头坐标按网格缩放（RobotScale）一起放大，相对位置给未缩放的厘米；锥体缩放仍按世界（UpdateThrusterFX）。
	// 10-31：喷口按这台机体的网格取（MechCatalog::FrameBody，数据来自 lookdev/rig/factions/manifest.json；维克托三台和 v3 同位），换网格后重挂
	bThrusterOnBone = false;
	const USkeletalMesh* Asset = GetMesh()->GetSkeletalMeshAsset();
	if (FParse::Param(FCommandLine::Get(), TEXT("MechThrusterOld")) || !Asset)
	{
		return;
	}
	const MechCatalog::FFrameBody* Found = MechCatalog::FrameBodyForMesh(Asset);
	const MechCatalog::FFrameBody& Body = Found ? *Found : MechCatalog::FrameBody(static_cast<int32>(EMechFrame::Light));  // 不认识的网格按 v3
	const FVector Mouth = Body.NozzleMouth;
	const FVector Dir = Body.NozzleDir.GetSafeNormal();
	FlameWidth = 0.10f * Body.NozzleRadius / 4.1f;
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	const int32 Bone = Ref.FindBoneIndex(ThrusterBone);
	if (Bone == INDEX_NONE)
	{
		return;
	}
	// 绑定姿势下这根骨头在网格组件空间的变换：自己的局部变换一路乘到根骨
	FTransform BoneCS = FTransform::Identity;
	for (int32 i = Bone; i != INDEX_NONE; i = Ref.GetParentIndex(i))
	{
		BoneCS = BoneCS * Ref.GetRefBonePose()[i];
	}
	const FVector MouthMid(0.f, Mouth.Y, Mouth.Z);
	for (int32 k = 0; k < 2; ++k)
	{
		const float Side = k == 0 ? 1.f : -1.f;  // 左边那个在网格 +X
		NozzleOnBone[k] = BoneCS.InverseTransformPosition(FVector(Mouth.X * Side, Mouth.Y, Mouth.Z));
		NozzleDirOnBone[k] = BoneCS.InverseTransformVectorNoScale(FVector(Dir.X * Side, Dir.Y, Dir.Z)).GetSafeNormal();
	}
	for (int32 k = 0; k < 2; ++k)
	{
		UStaticMeshComponent* Flame = k == 0 ? ThrusterLeft.Get() : ThrusterRight.Get();
		Flame->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, ThrusterBone);
		// 锥体尖端朝 +Z：转到朝向喷射方向（位置随长度在 UpdateThrusterFX 里摆）
		Flame->SetRelativeLocationAndRotation(NozzleOnBone[k], FRotationMatrix::MakeFromZ(NozzleDirOnBone[k]).Rotator());
	}
	// 灯在两个喷口中间、沿喷射方向出去约 40 厘米（世界）
	const FVector MidDir = BoneCS.InverseTransformVectorNoScale(FVector(0.f, Dir.Y, Dir.Z)).GetSafeNormal();
	ThrusterLight->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, ThrusterBone);
	ThrusterLight->SetRelativeLocation(BoneCS.InverseTransformPosition(MouthMid) + MidDir * (40.f / RobotScale));
	if (const UStaticMesh* Cone = ThrusterLeft->GetStaticMesh())
	{
		FlameBaseZ = Cone->GetBoundingBox().Min.Z;
	}
	bThrusterOnBone = true;
	if (FlameMaterial && !FlameMID)
	{
		FlameMID = UMaterialInstanceDynamic::Create(FlameMaterial, this);
		FlameMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 0.78f, 1.f));
		ThrusterLeft->SetMaterial(0, FlameMID);
		ThrusterRight->SetMaterial(0, FlameMID);
	}
	static TSet<FName> Logged;
	if (!Logged.Contains(Asset->GetFName()))
	{
		Logged.Add(Asset->GetFName());
		// 核对坐标换算：spine_05 绑定姿势应约在 (0, 0.5, 141)；ball_l 在前面（+Y）、左边（+X）
		const int32 Ball = Ref.FindBoneIndex(TEXT("ball_l"));
		FTransform BallCS = FTransform::Identity;
		for (int32 i = Ball; i != INDEX_NONE; i = Ref.GetParentIndex(i))
		{
			BallCS = BallCS * Ref.GetRefBonePose()[i];
		}
		UE_LOG(LogTemp, Display, TEXT("[thruster] %s（%s）喷焰挂 %s：骨头绑定位置 %s，ball_l %s，左喷口 %s → 骨头坐标 %s 方向 %s，粗细 %.3f，圆锥底面 Z %.1f"),
			*Asset->GetName(), Body.Key, *ThrusterBone.ToString(), *BoneCS.GetLocation().ToString(), *BallCS.GetLocation().ToString(), *Mouth.ToString(),
			*NozzleOnBone[0].ToString(), *NozzleDirOnBone[0].ToString(), FlameWidth, FlameBaseZ);
	}
}

void AMechCharacter::CameraKick(const FVector& Dir, float Strength, float Pitch)
{
	if (!IsLocallyControlled() || !IsPlayerControlled() || bBeauty || bViewer)
	{
		return;
	}
	// 弹簧臂末端的坐标系：X 沿镜头朝向、Y 右、Z 上
	const FVector Local = FRotator(0.f, GetControlRotation().Yaw, 0.f).UnrotateVector(Dir.GetSafeNormal());
	KickOffsetVel += Local * Strength * 1100.f;
	KickRotVel += FVector(Pitch + Strength * 28.f, Local.Y * Strength * 22.f, FMath::RandRange(-1.f, 1.f) * Strength * 60.f);
	if (Strength >= 0.3f)
	{
		FovPunch = FMath::Max(FovPunch, Strength * 6.f);
	}
	if (Strength >= 0.55f)
	{
		Fringe = FMath::Max(Fringe, Strength);
	}
}

void AMechCharacter::FlashBody(const FLinearColor& Color, float Strength)
{
	if (!HitFlashMaterial || bWrecked)
	{
		return;
	}
	if (!HitFlashMID)
	{
		HitFlashMID = UMaterialInstanceDynamic::Create(HitFlashMaterial, this);
	}
	HitFlashMID->SetVectorParameterValue(TEXT("FlashColor"), Color);
	FlashLevel = FMath::Max(FlashLevel, Strength);
	HitFlashMID->SetScalarParameterValue(TEXT("Flash"), FlashLevel);
	if (GetMesh()->GetOverlayMaterial() != HitFlashMID)
	{
		GetMesh()->SetOverlayMaterial(HitFlashMID);
	}
}

void AMechCharacter::UpdateFlash(float DeltaSeconds)
{
	if (FlashLevel <= 0.f)
	{
		return;
	}
	FlashLevel *= FMath::Exp(-DeltaSeconds * 20.f);
	if (FlashLevel < 0.03f || bWrecked)
	{
		FlashLevel = 0.f;
		GetMesh()->SetOverlayMaterial(nullptr);  // 不闪的时候不多画一层
		return;
	}
	HitFlashMID->SetScalarParameterValue(TEXT("Flash"), FlashLevel);
}

void AMechCharacter::OnHealthStaggered()
{
	StartStaggerAnim(LastHitFrom);
	if (StaggerFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, StaggerFX, GetActorLocation() + FVector(0.f, 0.f, 90.f), FRotator(90.f, 0.f, 0.f), FVector(2.4f), true, true, ENCPoolMethod::AutoRelease);
	}
	if (StaggerSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, StaggerSound, GetActorLocation(), FRotator::ZeroRotator, 0.9f, 1.f, 0.f, Attenuation);
	}
	AddRecoil(0.5f);
}

void AMechCharacter::OnHealthDied(AActor* Killer)
{
	if (bWrecked)
	{
		return;
	}
	bWrecked = true;
	Combat->SetTrigger(false);
	GetMechMovement()->bWantsBoost = false;
	GetMechMovement()->bWantsThrust = false;
	const FVector At = GetActorLocation();
	if (DeathFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, DeathFX, At, FRotator::ZeroRotator, FVector(2.4f), true, true, ENCPoolMethod::AutoRelease);
	}
	if (DeathShockFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, DeathShockFX, At - FVector(0.f, 0.f, 110.f), FRotator::ZeroRotator, FVector(1.3f), true, true, ENCPoolMethod::AutoRelease);
	}
	if (DeathSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, DeathSound, At, FRotator::ZeroRotator, 1.f, 1.f, 0.f, Attenuation);
	}
	// 11-02 落水损毁：在水面上溅一下（AAAVFX 的水花；各端都走这里）
	if (UMechWater* Water = GetWorld()->GetSubsystem<UMechWater>())
	{
		const FMechWaterZone* Zone = Water->ZoneAt(At);
		if (Zone && At.Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight() < Zone->Z + 150.f)
		{
			if (UNiagaraSystem* Splash = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/AAAVFX/Course_06/Particles/Ex_02_WaterSplash.Ex_02_WaterSplash"), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Splash, FVector(At.X, At.Y, Zone->Z), FRotator::ZeroRotator, FVector(3.f), true, true, ENCPoolMethod::AutoRelease);
			}
		}
	}
	AddRecoil(1.f);
	// 机体倒下：关掉程序姿势，整副骨架交给物理（布娃娃），朝被打的方向带一点冲量
	GetMechMesh()->bProcedural = false;
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	GetMesh()->SetAllBodiesSimulatePhysics(true);
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->WakeAllRigidBodies();
	const FVector Push = Killer ? (At - Killer->GetActorLocation()).GetSafeNormal2D() : -GetActorForwardVector();
	GetMesh()->AddImpulseToAllBodiesBelow(Push * 600.f + FVector(0.f, 0.f, 250.f), TEXT("pelvis"), true, true);
	ThrusterLight->SetIntensity(0.f);
	ThrusterLeft->SetVisibility(false);
	ThrusterRight->SetVisibility(false);
	BoostAudio->Stop();
}

void AMechCharacter::UpdateThrusterFX(float DeltaSeconds)
{
	float Target = 0.f;
	const UMechMovementComponent* Move = GetMechMovement();
	if (IsThrustingUp())
	{
		Target = 1.f;
	}
	else if (IsBoosting() && Move->Velocity.Size2D() > Move->WalkSpeed * 1.1f)
	{
		Target = Move->IsFalling() ? 0.9f : 0.6f;
	}
	Target = FMath::Max(Target, FMath::Max(Move->QuickBoostFlash, RemoteQuickFlash) * 1.4f);
	if (bAnimOverride && bAnimOverrideAir && (bAnimOverrideBoost || AnimOverrideVelocity.Z > 0.f))
	{
		Target = FMath::Max(Target, bAnimOverrideBoost ? 0.9f : 1.f);  // 模型查看的飞行 / 悬停：机体不真动，按动作开喷焰
	}
	if (bViewerForceThrust)
	{
		Target = 1.f;
	}
	ThrustFX = FMath::FInterpTo(ThrustFX, Target, DeltaSeconds, 14.f);

	const bool bVisible = ThrustFX > 0.04f;
	const float Flicker = 0.9f + 0.1f * FMath::Sin(GetWorld()->GetTimeSeconds() * 60.f);
	const float MeshScale = FMath::Max(GetMesh()->GetComponentScale().Z, 0.01f);
	for (int32 k = 0; k < 2; ++k)
	{
		UStaticMeshComponent* Flame = k == 0 ? ThrusterLeft.Get() : ThrusterRight.Get();
		Flame->SetVisibility(bVisible);
		if (!bVisible)
		{
			continue;
		}
		// 锥体 1 米高：长度 0.2–0.6 米（快速闪避闪一下更长）
		const float Len = (0.2f + 0.4f * ThrustFX) * Flicker;
		if (!bThrusterOnBone)
		{
			Flame->SetRelativeScale3D(FVector(0.12f, 0.12f, Len));  // -MechThrusterOld：旧挂法，锥体中心放在估的点上
			continue;
		}
		// 底面直径 10 厘米，比喇叭口内径（约 11 厘米）略小，底面缩进喷口 2 厘米：从喇叭口里面喷出来，看不到底面的边。
		// 锥体原点在中间，底面在 FlameBaseZ × 长度：原点 = 喷口 + 方向 ×（底面到原点 − 缩进），骨头坐标要除以网格缩放
		constexpr float Inset = 2.f;
		Flame->SetRelativeScale3D(FVector(FlameWidth, FlameWidth, Len));  // 10-31：粗细按这台机体的喇叭口半径（v3 = 0.10）
		Flame->SetRelativeLocation(NozzleOnBone[k] + NozzleDirOnBone[k] * ((-FlameBaseZ * Len - Inset) / MeshScale));
	}
	if (FlameMID && bVisible)
	{
		const MechMapRules::FFlight& FlightRule = MechMapRules::FlightFor(GetWorld());
		if (FlightRule.bFixedBooster)
		{
			FlameMID->SetVectorParameterValue(TEXT("Color"), FlightRule.FlameColor);  // 石林飞行器的喷焰换个颜色（模型没有单独的推进器件，喷焰就是它的外观）
		}
		// 白天实测（10-30 测试场）：1.5–4 时泛光糊成一大团白、盖住半个机体 → 降一个数量级
		FlameMID->SetScalarParameterValue(TEXT("Intensity"), (0.15f + 0.25f * FMath::Min(ThrustFX, 1.f)) * Flicker);
	}
	ThrusterLight->SetIntensity(ThrustFX * 2500.f * Flicker);
}

void AMechCharacter::DemoTick(float DeltaSeconds)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}
	if (DemoTime == 0.f)
	{
		DemoYaw = GetActorRotation().Yaw;
	}
	const float T0 = DemoTime;
	DemoTime += DeltaSeconds;
	auto Crossed = [&](float At) { return T0 < At && DemoTime >= At; };

	// 镜头一直从机体斜后方看
	PC->SetControlRotation(FRotator(-10.f, DemoYaw + 14.f, 0.f));
	const FVector Forward = FRotator(0.f, DemoYaw, 0.f).Vector();
	const FVector Right = FRotator(0.f, DemoYaw + 90.f, 0.f).Vector();

	if (DemoTime > 1.5f && DemoTime < 10.f)
	{
		AddMovementInput(DemoTime > 8.9f && DemoTime < 9.3f ? Right : Forward, 1.f);
	}
	GetMechMovement()->bWantsBoost = DemoTime > 4.f && DemoTime < 8.6f;
	if (Crossed(6.5f))
	{
		Jump();
		JumpPressed();
	}
	if (Crossed(8.5f))
	{
		JumpReleased();
		StopJumping();
	}
	if (Crossed(9.2f))
	{
		QuickBoost();
	}

	static const TPair<float, const TCHAR*> Shots[] = {
		{1.2f, TEXT("01_idle")}, {3.5f, TEXT("02_walk")}, {5.8f, TEXT("03_boost")},
		{8.0f, TEXT("04_thrust")}, {9.35f, TEXT("05_quickboost")}, {11.5f, TEXT("06_landed")}};
	for (const auto& Shot : Shots)
	{
		if (Crossed(Shot.Key))
		{
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Demo") / FString(Shot.Value) + TEXT(".png");
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			UE_LOG(LogTemp, Display, TEXT("[demo] %s 速度 %.0f km/h 高度 %.0f 能量 %.0f 推进 %d 喷射 %d"), Shot.Value,
				GetCharacterMovement()->Velocity.Size() * 0.036f, GetActorLocation().Z, GetEnergy(), IsBoosting() ? 1 : 0, IsThrustingUp() ? 1 : 0);
		}
	}
	if (Crossed(12.5f))
	{
		PC->ConsoleCommand(TEXT("quit"));
	}
}

void AMechCharacter::AnimTestTick(float DeltaSeconds)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}
	const float Per = 3.f;
	if (DemoTime == 0.f)
	{
		DemoYaw = GetActorRotation().Yaw;
		int32 From = 1;
		FParse::Value(FCommandLine::Get(), TEXT("MechAnimFrom="), From);
		DemoTime = (FMath::Max(From, 1) - 1) * Per + KINDA_SMALL_NUMBER;
		GetCharacterMovement()->RotationRate = FRotator::ZeroRotator;  // 朝向锁死，好看横移和后退
		GetCameraBoom()->bDoCollisionTest = true;  // 来回走时镜头别穿进墙里
		GetCameraBoom()->bEnableCameraLag = false;
		GetCameraBoom()->SocketOffset = BaseSocketOffset = FVector::ZeroVector;  // 镜头震动每帧按 BaseSocketOffset 重设
		GetCameraBoom()->TargetArmLength = 1000.f;
		BaseFOV = BoostFOV = 50.f;
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
	}
	const float T0 = DemoTime;
	DemoTime += DeltaSeconds;
	auto Crossed = [&](float At) { return T0 < At && DemoTime >= At; };

	enum class EAct : uint8 { None, Jump, Light, Heavy, Fire };
	struct FSeg
	{
		const TCHAR* Name;
		float Fwd, Right, Scale;
		bool bBoost;
		EAct Act;
		float CamYaw;   // 镜头朝向相对机体朝向（90 = 镜头在机体左侧看它的右边，220 = 镜头在右前方看回来）
		float Arm;      // 镜头距离
		float ShotAt;   // 第一张的时刻（段内秒数），连拍 4 张
		float ShotGap;
		FVector Socket = FVector::ZeroVector;  // 镜头臂末端偏移（开火那段从右肩后面看，像游戏里的镜头）
	};
	static const FSeg Segs[] = {
		{TEXT("01_站立"), 0.f, 0.f, 0.f, false, EAct::None, 90.f, 1000.f, 1.7f, 0.15f},
		{TEXT("02_慢走"), 1.f, 0.f, 0.3f, false, EAct::None, 90.f, 1000.f, 1.7f, 0.15f},
		{TEXT("03_跑"), 1.f, 0.f, 1.f, false, EAct::None, 90.f, 1000.f, 1.7f, 0.15f},
		{TEXT("04_右横移"), 0.f, 1.f, 1.f, false, EAct::None, 90.f, 1000.f, 1.7f, 0.15f},
		{TEXT("05_左横移"), 0.f, -1.f, 1.f, false, EAct::None, 90.f, 1000.f, 1.7f, 0.15f},
		{TEXT("06_后退"), -1.f, 0.f, 1.f, false, EAct::None, 90.f, 1000.f, 1.7f, 0.15f},
		{TEXT("07_右前斜"), 1.f, 1.f, 1.f, false, EAct::None, 90.f, 1000.f, 1.7f, 0.15f},
		{TEXT("08_冲刺滑行"), 1.f, 0.f, 1.f, true, EAct::None, 90.f, 1000.f, 1.7f, 0.15f},
		{TEXT("09_跳跃下落"), 0.3f, 0.f, 1.f, false, EAct::Jump, 90.f, 1000.f, 1.7f, 0.15f},
		{TEXT("10_落地"), 0.f, 0.f, 0.f, false, EAct::None, 90.f, 1000.f, 0.05f, 0.15f},
		// 武器：握法特写（右前、左前）、挥刀过程、端枪连射
		{TEXT("11_持械右前"), 0.f, 0.f, 0.f, false, EAct::None, 220.f, 480.f, 1.5f, 0.3f},
		{TEXT("12_持械左前"), 0.f, 0.f, 0.f, false, EAct::None, 140.f, 480.f, 1.5f, 0.3f},
		// 刀：段开头切近战姿态，1.0 秒按左键（轻击 0.05 秒松开；重斩按住 0.6 秒，0.22 秒时从轻击收成重斩），按下后连拍看出刀前摇和砍下去
		{TEXT("13_轻击"), 0.f, 0.f, 0.f, false, EAct::Light, 215.f, 750.f, 1.f, 0.06f},
		{TEXT("14_重斩"), 0.f, 0.f, 0.f, false, EAct::Heavy, 215.f, 750.f, 1.22f, 0.08f},
		{TEXT("15_开火"), 0.f, 0.f, 0.f, false, EAct::Fire, 10.f, 380.f, 1.5f, 0.15f, FVector(0.f, 150.f, 50.f)},
	};
	const int32 Index = FMath::FloorToInt(DemoTime / Per);
	if (Index >= UE_ARRAY_COUNT(Segs))
	{
		bAnimTest = false;
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	const FSeg& S = Segs[Index];
	const float Local = DemoTime - Index * Per;
	// 每段机体掉头 180 度，来回走不会离开街道；镜头始终在机体左侧
	const FRotator Facing(0.f, DemoYaw + (Index % 2 ? 180.f : 0.f), 0.f);
	SetActorRotation(Facing);
	PC->SetControlRotation(FRotator(-6.f, Facing.Yaw + S.CamYaw, 0.f));
	GetCameraBoom()->TargetArmLength = S.Arm;
	GetCameraBoom()->SocketOffset = BaseSocketOffset = S.Socket;
	const FVector Dir = Facing.RotateVector(FVector(S.Fwd, S.Right, 0.f)).GetSafeNormal();
	if (S.Scale > 0.f && Local < 2.6f)
	{
		AddMovementInput(Dir, S.Scale);
	}
	GetMechMovement()->bWantsBoost = S.bBoost && Local < 2.6f;
	if (S.Act == EAct::Jump && Crossed(Index * Per + 0.3f))
	{
		Jump();
		JumpPressed();
	}
	if (S.Act == EAct::Jump && Crossed(Index * Per + 1.6f))
	{
		JumpReleased();
		StopJumping();
	}
	const bool bBladeAct = S.Act == EAct::Light || S.Act == EAct::Heavy;
	if (bBladeAct && Crossed(Index * Per + 0.1f) && Combat->GetStance() != EMechStance::Blade)
	{
		Combat->SwitchStance();
	}
	if (bBladeAct && Crossed(Index * Per + 1.f))
	{
		Combat->MeleePressed();
	}
	if (bBladeAct && Crossed(Index * Per + (S.Act == EAct::Heavy ? 1.6f : 1.05f)))
	{
		Combat->MeleeReleased();
	}
	if (S.Act == EAct::Fire && Crossed(Index * Per + 0.1f) && Combat->GetStance() != EMechStance::Gun)
	{
		Combat->SwitchStance();  // 前面刀那两段切到了近战
	}
	if (S.Act == EAct::Fire && Crossed(Index * Per + 0.9f))
	{
		FirePressed();
	}
	if (S.Act == EAct::Fire && Crossed(Index * Per + 2.6f))
	{
		FireReleased();
	}
	// 走跑类在 1.7 秒起连拍 4 帧（间隔 0.15 秒，跑步差不多四分之三个步伐周期）；落地从刚落地开始；刀那两段连拍 8 帧看完整挥刀
	// （截图请求同一时间只能挂一个，间隔太密会丢帧：拍刀时加 -ExecCmds="slomo 0.25" 放慢）
	for (int32 k = 0; k < (bBladeAct ? 8 : 4); ++k)
	{
		if (Crossed(Index * Per + S.ShotAt + k * S.ShotGap))
		{
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Anim") / FString::Printf(TEXT("%s_%d.png"), S.Name, k);
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			if (k == 0)
			{
				const FVector V = GetActorTransform().InverseTransformVectorNoScale(GetVelocity());
				UE_LOG(LogTemp, Display, TEXT("[animtest] %s 速度 前 %.0f 右 %.0f 上 %.0f 厘米/秒 空中 %d 冲刺 %d"), S.Name, V.X, V.Y, V.Z,
					GetCharacterMovement()->IsFalling() ? 1 : 0, IsBoosting() ? 1 : 0);
			}
		}
	}
}

void AMechCharacter::AirTestTick(float DeltaSeconds)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !IsLocallyControlled() || !CanAct())
	{
		return;
	}
	UMechMovementComponent* Move = GetMechMovement();
	static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("MechAnimOldAir"));
	if (DemoTime == 0.f)
	{
		GetCameraBoom()->bEnableCameraLag = false;
		GetCameraBoom()->bDoCollisionTest = false;
		GetCameraBoom()->TargetArmLength = 1300.f;
		GetCameraBoom()->SocketOffset = BaseSocketOffset = FVector::ZeroVector;
		BaseFOV = BoostFOV = 55.f;
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		UE_LOG(LogTemp, Display, TEXT("[airtest] 开始（%s）"), bOld ? TEXT("旧混法") : TEXT("新混法"));
	}
	const float T0 = DemoTime;
	DemoTime += DeltaSeconds;
	auto Crossed = [&](float At) { return T0 < At && DemoTime >= At; };
	// 镜头固定从 -X 往 +X 看（不跟机体转），机体沿 ±Y 来回飞，掉头、急停都看得清；场地 x=0 这条线上没有掩体和木桩
	PC->SetControlRotation(FRotator(-6.f, 0.f, 0.f));

	// 每段：起跳时刻、方向（+1 = +Y）、是否冲刺、按住跳跃多久（超过 0.18 秒就喷射升高）、ActAt 时刻做什么（0 松方向键 / 1 反向 / 2 反向 + 快速闪避）
	// 能量：冲刺段喷 0.5 秒升到六七米再滑翔，一段约耗 45，段间 5 秒回得过来
	struct FPhase
	{
		const TCHAR* Name;
		float Start;
		float Side;
		bool bBoost;
		float JumpHold;
		float ActAt;   // 段内秒数
		int32 Act;
	};
	static const FPhase Phases[] = {
		{TEXT("A_滑翔松手"), 1.f, 1.f, true, 0.7f, 1.8f, 0},
		{TEXT("B_滑翔反向"), 6.f, -1.f, true, 0.7f, 1.8f, 1},
		{TEXT("C_下落反向"), 11.f, 1.f, false, 0.12f, 0.45f, 1},
		{TEXT("D_喷射松手"), 14.f, -1.f, false, 2.4f, 1.2f, 0},
		{TEXT("E_反向闪避"), 21.f, 1.f, true, 0.9f, 1.1f, 2},  // D 段升得高，落地要到 19 秒多
	};
	static const float ShotOffsets[] = {-0.1f, 0.05f, 0.2f, 0.35f, 0.5f, 0.7f, 0.95f};
	for (const FPhase& P : Phases)
	{
		const float Local = DemoTime - P.Start;
		if (Local < 0.f || Local > 4.f)
		{
			continue;
		}
		const FVector Side = FVector(0.f, P.Side, 0.f);
		if (Crossed(P.Start))
		{
			JumpPressed();
		}
		if (Crossed(P.Start + P.JumpHold))
		{
			JumpReleased();
		}
		// 冲刺段离地 0.5 秒后才开始往一边飞；到 ActAt 换动作（松手 = 之后不再给方向）
		const bool bBefore = Local < P.ActAt;
		if (bBefore && (Local > 0.5f || !P.bBoost))
		{
			AddMovementInput(Side, 1.f);
		}
		if (!bBefore && Local < P.ActAt + 1.2f && P.Act >= 1)
		{
			AddMovementInput(-Side, 1.f);
		}
		Move->bWantsBoost = P.bBoost && Local > 0.5f && Local < P.ActAt + 1.2f;
		if (P.Act == 2 && Crossed(P.Start + P.ActAt + 0.03f))
		{
			QuickBoost();
		}
		if (Crossed(P.Start + P.ActAt + 1.2f))
		{
			JumpReleased();
			Move->bWantsBoost = false;
		}
		for (int32 k = 0; k < UE_ARRAY_COUNT(ShotOffsets); ++k)
		{
			if (Crossed(P.Start + P.ActAt + ShotOffsets[k]))
			{
				const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Air") / FString::Printf(TEXT("%s%s_%d.png"), bOld ? TEXT("旧_") : TEXT(""), P.Name, k);
				FScreenshotRequest::RequestScreenshot(Path, false, false);
				const FVector V = GetActorTransform().InverseTransformVectorNoScale(GetVelocity());
				UE_LOG(LogTemp, Display, TEXT("[airtest] %s_%d 段内 %.2f 秒 速度 前 %.0f 右 %.0f 上 %.0f 空中 %d 冲刺 %d 喷射 %d 朝向 %.0f"), P.Name, k,
					Local, V.X, V.Y, V.Z, Move->IsFalling() ? 1 : 0, IsBoosting() ? 1 : 0, IsThrustingUp() ? 1 : 0, GetActorRotation().Yaw);
			}
		}
	}
	if (DemoTime > 25.5f)
	{
		bAirTest = false;
		UE_LOG(LogTemp, Display, TEXT("[airtest] 结束"));
		PC->ConsoleCommand(TEXT("quit"));
	}
}

void AMechCharacter::WallTestTick(float DeltaSeconds)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !IsLocallyControlled() || !CanAct())
	{
		return;
	}
	UMechMovementComponent* Move = GetMechMovement();
	WallTestClock += DeltaSeconds;
	const float Clock = WallTestClock;
	auto Crossed = [&](float At) { return Clock - DeltaSeconds < At && Clock >= At; };
	// 测试场的背景墙：中心 (105, 0, 6) 米、厚 2 米，迎面那一面在 x = 104 米；沿 y = -25 米这条线过去中间没有掩体和木桩
	const float WallX = 10400.f;
	const FVector Me = GetActorLocation();
	const FVector ToWall(1.f, 0.f, 0.f);
	auto Log = [&](const FString& S) { UE_LOG(LogTemp, Display, TEXT("[walltest] %.2f %s"), GetWorld()->GetTimeSeconds(), *S); };
	auto Check = [&](const TCHAR* Name, bool bOk, const FString& Detail) {
		UE_LOG(LogTemp, Display, TEXT("[walltest] %.2f %s %s（%s，累计纠正 %d 次）"), GetWorld()->GetTimeSeconds(), bOk ? TEXT("通过") : TEXT("失败"), Name, *Detail,
			Move->CorrectionCount - WallTestCorrections0);
	};
	auto Shot = [&](const FString& Name) {
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Wall") / (Name + TEXT(".png")), false, false);
	};
	// 镜头在机体右后侧斜看墙面（蹬出去的方向和腿都看得到）
	PC->SetControlRotation(FRotator(-8.f, 50.f, 0.f));
	auto Next = [&]() { ++WallTestStep; WallTestClock = 0.f; };
	// 走到墙前 y = -25 米那条线上的某一点（远了冲刺）；到了返回真
	auto ApproachTo = [&](float StopX) {
		// 先横到 y = -25 米那条线（出生点直接斜着过去会擦到 42 米处的掩体），再沿线走
		const FVector Goal = Me.Y > -2300.f ? FVector(Me.X + 300.f, -2500.f, Me.Z) : FVector(StopX, -2500.f, Me.Z);
		AddMovementInput((Goal - Me).GetSafeNormal2D(), 1.f);
		Move->bWantsBoost = FVector::Dist2D(Goal, Me) > 1500.f;
		return FVector::Dist2D(Goal, Me) < 150.f;
	};
	static int32 Laps = 0;
	switch (WallTestStep)
	{
	case 0:
		GetCameraBoom()->bEnableCameraLag = false;
		GetCameraBoom()->TargetArmLength = 1100.f;
		GetCameraBoom()->SocketOffset = BaseSocketOffset = FVector::ZeroVector;
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		WallTestCorrections0 = Move->CorrectionCount;
		Log(FString::Printf(TEXT("开始：%s，位置 %s"), HasAuthority() ? TEXT("单机 / 主机") : TEXT("联机客户端"), *Me.ToCompactString()));
		Next();
		break;
	case 1:  // 冲到离墙 3 米处、落地站稳
		if ((ApproachTo(WallX - 300.f) && !Move->IsFalling()) || Clock > 30.f)
		{
			Move->bWantsBoost = false;
			Log(FString::Printf(TEXT("到墙前 %.1f 米"), (WallX - Me.X) / 100.f));
			Next();
		}
		break;
	case 2:  // 朝墙起跳（按住前进），贴墙后按跳跃蹬
		AddMovementInput(ToWall, 1.f);
		if (Crossed(0.02f))
		{
			JumpPressed();
		}
		if (Crossed(0.14f))
		{
			JumpReleased();
		}
		if (Clock > 0.3f && Move->CanWallJump())
		{
			Log(FString::Printf(TEXT("贴墙（胶囊离墙 %.0f 厘米，上下速度 %.0f），按跳跃"), WallX - Me.X - GetCapsuleComponent()->GetScaledCapsuleRadius(), Move->Velocity.Z));
			WallTestKicks = Move->WallJumps;
			JumpPressed();
			Shot(FString::Printf(TEXT("%d_蹬墙_0"), Laps));
			Next();
		}
		else if (Clock > 3.f)
		{
			Check(TEXT("贴墙蹬墙跳"), false, TEXT("3 秒内没贴上墙"));
			WallTestStep = 6;
			WallTestClock = 0.f;
		}
		break;
	case 3:  // 蹬出去：按着往墙里也要能蹬开（往墙里的那部分不算）
		AddMovementInput(ToWall, 1.f);
		if (Crossed(0.1f))
		{
			JumpReleased();
			Shot(FString::Printf(TEXT("%d_蹬墙_1"), Laps));
		}
		if (Crossed(0.22f))
		{
			Shot(FString::Printf(TEXT("%d_蹬墙_2"), Laps));
			Check(TEXT("贴墙蹬墙跳"), Move->WallJumps == WallTestKicks + 1 && Move->Velocity.X < -300.f && Move->Velocity.Z > 300.f,
				FString::Printf(TEXT("次数 %d→%d，速度 离墙 %.0f 上 %.0f"), WallTestKicks, Move->WallJumps, -Move->Velocity.X, Move->Velocity.Z));
		}
		if (Crossed(0.36f))
		{
			Shot(FString::Printf(TEXT("%d_蹬墙_3"), Laps));
		}
		if (Clock > 0.5f)
		{
			WallTestKicks = Move->WallJumps;
			Next();
		}
		break;
	case 4:  // 还在空中：按着前进飘回同一面墙，再按跳跃（应当蹬不了：同一面墙不能连蹬）
		AddMovementInput(ToWall, 1.f);
		if (WallTestKicks >= 0 && Move->IsFalling() && WallX - Me.X < 140.f)  // 胶囊离墙不到 85 厘米：换一面墙就能蹬的距离
		{
			JumpPressed();
			WallTestKicks = -1 - WallTestKicks;  // 负数 = 已经按过（原值 = -1 - 它）
			WallTestClock = 0.f;
		}
		else if (WallTestKicks < 0)
		{
			if (Clock > 0.1f)
			{
				JumpReleased();
			}
			if (Clock > 0.4f)
			{
				const int32 Before = -1 - WallTestKicks;
				Check(TEXT("同一面墙不能连蹬"), Move->WallJumps == Before, FString::Printf(TEXT("次数 %d→%d"), Before, Move->WallJumps));
				Next();
			}
		}
		else if (!Move->IsFalling() || Clock > 3.f)
		{
			Log(TEXT("没飘回墙边就落地了，跳过「同一面墙」这项"));
			Next();
		}
		break;
	case 5:  // 落地后计数清零；退回 3 米处再来一轮（共三轮）
		if (!Move->IsFalling() && Clock > 0.3f)
		{
			Check(TEXT("落地清零"), Move->WallJumps == 0, FString::Printf(TEXT("次数 %d"), Move->WallJumps));
			WallTestStep = ++Laps >= 3 ? 6 : 1;
			WallTestClock = 0.f;
		}
		break;
	case 6:  // 退到离墙 15 米站稳，起跳后在空中再按住跳跃：不贴墙，应当照常喷射、不算蹬墙
		if (!(ApproachTo(WallX - 1500.f) && !Move->IsFalling()) && Clock < 8.f)
		{
			break;
		}
		Move->bWantsBoost = false;
		WallTestKicks = Move->WallJumps;
		Next();
		break;
	case 7:
		if (Crossed(0.02f))
		{
			JumpPressed();
		}
		if (Crossed(0.14f))
		{
			JumpReleased();
		}
		if (Crossed(0.5f))
		{
			JumpPressed();  // 空中再按住
		}
		if (Clock > 1.2f)
		{
			Check(TEXT("离墙远：照常喷射、不蹬墙"), Move->WallJumps == WallTestKicks && IsThrustingUp(),
				FString::Printf(TEXT("次数 %d→%d，喷射 %d"), WallTestKicks, Move->WallJumps, IsThrustingUp() ? 1 : 0));
			JumpReleased();
			Next();
		}
		break;
	case 8:
		if (Clock > 3.f)
		{
			Log(FString::Printf(TEXT("结束：收到服务器移动纠正 %d 次（%s）"), Move->CorrectionCount - WallTestCorrections0,
				HasAuthority() ? TEXT("单机 / 主机") : TEXT("客户端")));
			bWallTest = false;
			PC->ConsoleCommand(TEXT("quit"));
		}
		break;
	default:
		break;
	}
}

namespace
{
	// 模型查看的动作：名字、假速度（机体坐标：前、右、上，厘米/秒）、空中、冲刺、
	// 0 无 / 1 轻击连招 / 2 开火 / 3 重斩 / 4 格挡 / 5 闪避（八方向轮换）/ 6 失衡（四方向轮换）/ 7 反复拔刀收刀 / 8 持刀站着
	struct FViewerAction
	{
		const TCHAR* Name;
		FVector Velocity;
		bool bAir;
		bool bBoost;
		int32 Act;
	};
	const FViewerAction ViewerActions[] = {
		{TEXT("站立"), FVector::ZeroVector, false, false, 0},
		{TEXT("慢走"), FVector(280.f, 0.f, 0.f), false, false, 0},
		{TEXT("跑"), FVector(680.f, 0.f, 0.f), false, false, 0},
		{TEXT("左横移"), FVector(0.f, -680.f, 0.f), false, false, 0},
		{TEXT("右横移"), FVector(0.f, 680.f, 0.f), false, false, 0},
		{TEXT("后退"), FVector(-680.f, 0.f, 0.f), false, false, 0},
		{TEXT("冲刺滑行"), FVector(1500.f, 0.f, 0.f), false, true, 0},
		{TEXT("下落"), FVector(300.f, 0.f, -600.f), true, false, 0},
		{TEXT("轻击连招"), FVector::ZeroVector, false, false, 1},
		{TEXT("开火"), FVector::ZeroVector, false, false, 2},
		{TEXT("重斩"), FVector::ZeroVector, false, false, 3},
		{TEXT("格挡"), FVector::ZeroVector, false, false, 4},
		{TEXT("闪避"), FVector::ZeroVector, false, false, 5},
		{TEXT("失衡"), FVector::ZeroVector, false, false, 6},
		{TEXT("拔刀收刀"), FVector::ZeroVector, false, false, 7},
		{TEXT("持刀站立"), FVector::ZeroVector, false, false, 8},
		{TEXT("换弹"), FVector::ZeroVector, false, false, 9},
		{TEXT("飞行"), FVector(1300.f, 0.f, 0.f), true, true, 0},
		{TEXT("空中悬停"), FVector(0.f, 0.f, 150.f), true, false, 10},
		{TEXT("起步急停"), FVector::ZeroVector, false, false, 11},
		{TEXT("重落地"), FVector::ZeroVector, false, false, 12},
		{TEXT("空中闪避"), FVector(0.f, 0.f, -100.f), true, false, 5},
	};
	// -MechViewerShots=showcase：轮流展示各把枪、各把刀、飞行和起步急停（加 -MechViewerVideo 每帧截图，配 -benchmark -fps=20 合成视频）
	// 动作编号（ViewerActions 的下标）、枪 / 刀（-1 不换）、时长、镜头方向（相对机体）、距离
	struct FShowcase
	{
		int32 Action;
		int32 Gun;
		int32 Blade;
		float Duration;
		float CamYaw;
		float Arm;
	};
	const FShowcase Showcase[] = {
		{9, 0, -1, 3.f, 215.f, 560.f}, {16, 0, -1, 2.6f, 240.f, 520.f}, {9, 1, -1, 2.5f, 215.f, 560.f}, {9, 2, -1, 2.5f, 215.f, 560.f},
		{9, 3, -1, 3.f, 215.f, 560.f}, {9, 4, -1, 3.f, 215.f, 560.f}, {9, 5, -1, 2.5f, 200.f, 520.f}, {16, 5, -1, 2.2f, 200.f, 520.f},
		{8, -1, 0, 3.4f, 215.f, 720.f}, {10, -1, 0, 1.8f, 215.f, 720.f},
		{8, -1, 1, 2.9f, 215.f, 720.f}, {10, -1, 1, 1.6f, 215.f, 720.f},
		{8, -1, 2, 3.1f, 215.f, 720.f}, {10, -1, 2, 1.7f, 215.f, 720.f},
		{8, -1, 3, 4.2f, 215.f, 760.f}, {10, -1, 3, 2.1f, 215.f, 760.f},
		{8, -1, 4, 2.5f, 215.f, 720.f}, {10, -1, 4, 1.4f, 215.f, 720.f},
		{8, -1, 5, 4.8f, 215.f, 760.f}, {10, -1, 5, 2.3f, 215.f, 760.f},
		{8, -1, 6, 2.6f, 200.f, 620.f}, {10, -1, 6, 1.5f, 200.f, 620.f},
		{11, -1, 0, 2.f, 200.f, 620.f},
		{17, -1, -1, 3.f, 250.f, 760.f}, {18, -1, -1, 2.5f, 215.f, 700.f}, {20, -1, -1, 2.6f, 215.f, 760.f}, {19, -1, -1, 3.4f, 270.f, 760.f},
	};
	// -MechViewerShots=grip：五把长枪端着站立，上身特写从左前、右前各拍一张（看左手握没握住护木）
	const FShowcase GripShots[] = {
		{0, 0, -1, 1.6f, 215.f, 340.f}, {0, 0, -1, 1.6f, 165.f, 340.f}, {0, 1, -1, 1.6f, 215.f, 340.f}, {0, 1, -1, 1.6f, 165.f, 340.f},
		{0, 2, -1, 1.6f, 215.f, 340.f}, {0, 2, -1, 1.6f, 165.f, 340.f}, {0, 3, -1, 1.6f, 215.f, 340.f}, {0, 3, -1, 1.6f, 165.f, 340.f},
		{0, 4, -1, 1.6f, 215.f, 340.f}, {0, 4, -1, 1.6f, 165.f, 340.f},
	};
	// -MechViewerShots=blades：七把刀持刀站立，从右前、左前各拍一张（看握持姿势；215 度那边是背面）
	const FShowcase BladeShots[] = {
		{15, -1, 0, 1.8f, 215.f, 420.f}, {15, -1, 0, 1.4f, 145.f, 420.f}, {15, -1, 1, 1.8f, 215.f, 420.f}, {15, -1, 1, 1.4f, 145.f, 420.f},
		{15, -1, 2, 1.8f, 215.f, 420.f}, {15, -1, 2, 1.4f, 145.f, 420.f}, {15, -1, 3, 1.8f, 215.f, 480.f}, {15, -1, 3, 1.4f, 145.f, 480.f},
		{15, -1, 4, 1.8f, 215.f, 420.f}, {15, -1, 4, 1.4f, 145.f, 420.f}, {15, -1, 5, 1.8f, 215.f, 480.f}, {15, -1, 5, 1.4f, 145.f, 480.f},
		{15, -1, 6, 1.8f, 215.f, 420.f}, {15, -1, 6, 1.4f, 145.f, 420.f},
	};
	// -MechViewerShots=hands（10-31 握持）：13 把武器（先 7 把刀、再 6 把枪）各拍待机、出招 / 开火，右手、左手特写各一张（镜头对准手骨），
	// 截图在 Saved/Screenshots/Hands/<标签>/，标签 -MechHandsTag=改前（默认「改后」）；配 -benchmark -fps=30 两次跑的帧对得上
	struct FHandShot
	{
		int32 Action = 0;     // ViewerActions 下标
		int32 Gun = -1;       // 换成这把枪（-1 不换）
		int32 Blade = -1;     // 换成这把刀（-1 不换）
		int32 Hand = 1;       // 0 左手 1 右手
		float CamYaw = 240.f; // 镜头方向（相对机体：215 右前、145 左前）
		float Duration = 1.6f;
		const TCHAR* Pose = TEXT("");
	};
	TArray<FHandShot> MakeHandShots()
	{
		TArray<FHandShot> Out;
		// -MechHandsWeapons=3,5,7：只拍这几把（0–6 刀、7–12 枪，顺序同上），按框架逐台查时省时间
		FString Only;
		TArray<FString> OnlyList;
		if (FParse::Value(FCommandLine::Get(), TEXT("MechHandsWeapons="), Only, false))
		{
			Only.ParseIntoArray(OnlyList, TEXT(","));
		}
		for (int32 w = 0; w < 13; ++w)
		{
			if (OnlyList.Num() > 0 && !OnlyList.Contains(FString::FromInt(w)))
			{
				continue;
			}
			const bool bBlade = w < 7;
			for (int32 Pose = 0; Pose < 2; ++Pose)
			{
				for (int32 Hand = 1; Hand >= 0; --Hand)
				{
					FHandShot S;
					S.Action = bBlade ? (Pose == 0 ? 15 : 8) : (Pose == 0 ? 0 : 9);
					S.Gun = bBlade ? -1 : w - 7;
					S.Blade = bBlade ? w : -1;
					S.Hand = Hand;
					S.CamYaw = Hand == 1 ? 240.f : 120.f;
					S.Duration = Pose == 0 && Hand == 1 ? 2.8f : 1.7f;  // 每把武器第一张多等一会儿（拔刀 / 换枪）
					S.Pose = Pose == 0 ? TEXT("待机") : bBlade ? TEXT("出招") : TEXT("开火");
					Out.Add(S);
				}
			}
		}
		return Out;
	}
	int32 ViewerHandFocus = -1;  // 镜头对准哪只手（-1 不管）
	FVector ViewerHandFocusShift = FVector::ZeroVector;  // 对准点相对手骨的世界偏移（-MechHandsThumbCam 对准手心空腔）
	// -MechViewerShots=dampers：腿部特写从右后、左后看膝盖后、脚踝后的跨关节减震（跑、重落地、起步急停、重斩时活塞杆伸缩）
	const FShowcase DamperShots[] = {
		{2, -1, -1, 3.f, 300.f, 380.f}, {20, -1, -1, 2.6f, 300.f, 380.f}, {19, -1, -1, 3.4f, 35.f, 380.f}, {10, -1, 0, 1.8f, 300.f, 420.f},
	};
	// -MechViewerShots=stance：固定机位看两种姿态下武器挂在哪（动作编号、镜头朝向相对机体、距离、文件名）
	struct FStanceShot
	{
		int32 Action;
		float CamYaw;
		float Arm;
		const TCHAR* Name;
		int32 Frame = 0;            // 取景：0 全身 1 上身 2 腿
		bool bWeaponsOnly = false;  // 只看武器（机体隐藏）
		float Pitch = -8.f;
		int32 Shoulder = -1;        // 换成这种肩部武器再拍（-1 不换）
		int32 MechFrame = -1;       // 换成这种框架再拍（-1 不换；-MechViewerShots=frames）
	};
	const FStanceShot StanceShots[] = {
		{0, 215.f, 520.f, TEXT("射击_右前")},
		{0, 35.f, 520.f, TEXT("射击_左后")},
		{0, 130.f, 480.f, TEXT("射击_左前")},
		{15, 215.f, 520.f, TEXT("近战_右前")},
		{15, 35.f, 520.f, TEXT("近战_左后")},
		{15, 300.f, 480.f, TEXT("近战_右后")},
		{11, 200.f, 520.f, TEXT("格挡")},
		{8, 200.f, 700.f, TEXT("连招")},
		{10, 200.f, 700.f, TEXT("重斩")},
	};
	// -MechViewerShots=catalog：配装表用的部件图（白天）：整台机体、背后推进器（冲刺喷火）、只看武器（射击 / 近战姿态）、导弹巢
	const FStanceShot CatalogShots[] = {
		{0, 150.f, 640.f, TEXT("框架_中型_A"), 0, false, 6.f},
		{0, 240.f, 640.f, TEXT("框架_中型_B"), 0, false, 6.f},
		{6, 15.f, 430.f, TEXT("推进器_均衡型"), 1, false, -12.f},
		{0, 300.f, 300.f, TEXT("射击_步枪"), 1, true, 14.f},
		{15, 300.f, 360.f, TEXT("近战_刀"), 1, true, 14.f},
		{0, 60.f, 260.f, TEXT("肩部_导弹"), 1, true, 10.f},
	};
	// -MechViewerShots=shoulder：肩部武器的浮空无人机（10-30 起在左肩外上方）从左后、左前看，再各拍一张第三人称正后方
	const FStanceShot ShoulderShots[] = {
		{0, 40.f, 520.f, TEXT("导弹_左后"), 1, false, -8.f, 0},
		{0, 130.f, 480.f, TEXT("导弹_左前"), 1, false, -4.f, 0},
		{0, 0.f, 560.f, TEXT("导弹_正后_第三人称视角"), 0, false, -10.f, 0},
		{0, 40.f, 520.f, TEXT("钩爪_左后"), 1, false, -8.f, 3},
		{0, 130.f, 480.f, TEXT("钩爪_左前"), 1, false, -4.f, 3},
		{0, 0.f, 560.f, TEXT("钩爪_正后_第三人称视角"), 0, false, -10.f, 3},
		{0, 40.f, 520.f, TEXT("网_左后"), 1, false, -8.f, 4},
		{0, 130.f, 480.f, TEXT("网_左前"), 1, false, -4.f, 4},
		{0, 40.f, 520.f, TEXT("火炮_左后"), 1, false, -8.f, 1},
		{0, 130.f, 480.f, TEXT("火炮_左前"), 1, false, -4.f, 1},
		{0, 40.f, 520.f, TEXT("烟雾弹_左后"), 1, false, -8.f, 5},
		{0, 130.f, 480.f, TEXT("烟雾弹_左前"), 1, false, -4.f, 5},
		{0, 40.f, 520.f, TEXT("EMP_左后"), 1, false, -8.f, 6},
		{0, 130.f, 480.f, TEXT("EMP_左前"), 1, false, -4.f, 6},
		{0, 0.f, 560.f, TEXT("EMP_正后_第三人称视角"), 0, false, -10.f, 6},
		{0, 40.f, 520.f, TEXT("护盾_左后"), 1, false, -8.f, 2},
		{0, 130.f, 480.f, TEXT("护盾_左前"), 1, false, -4.f, 2},
		{0, 180.f, 620.f, TEXT("护盾_正前_对手视角"), 0, false, -4.f, 2},
		{0, 40.f, 520.f, TEXT("紧急脱离_左后"), 1, false, -8.f, 7},
		{0, 130.f, 480.f, TEXT("紧急脱离_左前"), 1, false, -4.f, 7},
		{0, 40.f, 520.f, TEXT("弹射升空_左后"), 1, false, -8.f, 8},
		{0, 130.f, 480.f, TEXT("弹射升空_左前"), 1, false, -4.f, 8},
		{0, 40.f, 520.f, TEXT("震荡波_左后"), 1, false, -8.f, 9},
		{0, 130.f, 480.f, TEXT("震荡波_左前"), 1, false, -4.f, 9},
		{0, 40.f, 520.f, TEXT("地雷_左后"), 1, false, -8.f, 10},
		{0, 130.f, 480.f, TEXT("地雷_左前"), 1, false, -4.f, 10},
	};
	// -MechViewerShots=thrust：背后推进器喷焰贴不贴喷口（喷焰一直开着）：站立、平飞、悬停、空中闪避、下落，上身 / 全身从右后、右侧看
	const FStanceShot ThrustShots[] = {
		{0, 330.f, 300.f, TEXT("站立_右后"), 1, false, -6.f},
		{0, 270.f, 300.f, TEXT("站立_右侧"), 1, false, -6.f},
		{17, 270.f, 460.f, TEXT("平飞_右侧"), 0, false, -6.f},
		{17, 320.f, 460.f, TEXT("平飞_右后"), 0, false, 10.f},
		{18, 330.f, 360.f, TEXT("悬停_右后"), 1, false, -6.f},
		{18, 270.f, 360.f, TEXT("悬停_右侧"), 1, false, -6.f},
		{21, 330.f, 460.f, TEXT("空中闪避_右后"), 0, false, -6.f},
		{13, 300.f, 460.f, TEXT("失衡_右后"), 0, false, -6.f},
	};
	// -MechViewerShots=aimfire：一边做各种动作一边开火，从右侧看枪管有没有对准瞄准方向（动作编号、枪、瞄准俯仰、镜头方向、名字）；
	// 日志 [aimfire] 记每段后半的枪管偏差，截图在 Saved/Screenshots/AimFire
	struct FAimShot
	{
		int32 Action;
		int32 Gun;
		float Pitch;
		float CamYaw;
		const TCHAR* Name;
	};
	const FAimShot AimShots[] = {
		{0, 0, 0.f, 270.f, TEXT("站立")}, {0, 0, 30.f, 270.f, TEXT("站立_瞄上")}, {0, 0, -30.f, 270.f, TEXT("站立_瞄下")},
		{2, 0, 0.f, 270.f, TEXT("跑")}, {3, 0, 0.f, 270.f, TEXT("左横移")}, {4, 0, 0.f, 270.f, TEXT("右横移")}, {5, 0, 0.f, 270.f, TEXT("后退")},
		{6, 0, 0.f, 270.f, TEXT("冲刺滑行")}, {7, 0, 0.f, 270.f, TEXT("下落")}, {17, 0, 0.f, 270.f, TEXT("飞行")}, {17, 0, -35.f, 270.f, TEXT("飞行_瞄下")},
		{18, 0, 0.f, 270.f, TEXT("空中悬停")}, {12, 0, 0.f, 270.f, TEXT("闪避")}, {13, 0, 0.f, 270.f, TEXT("失衡")}, {19, 0, 0.f, 270.f, TEXT("起步急停")},
		{20, 0, 0.f, 270.f, TEXT("重落地")},
		{0, 1, 0.f, 270.f, TEXT("冲锋枪")}, {0, 2, 0.f, 270.f, TEXT("霰弹枪")}, {0, 3, 0.f, 270.f, TEXT("充能步枪")}, {0, 4, 0.f, 270.f, TEXT("狙击枪")},
		{0, 5, 0.f, 215.f, TEXT("双持手枪")}, {17, 5, 0.f, 270.f, TEXT("双持手枪_飞行")}, {3, 5, 0.f, 270.f, TEXT("双持手枪_左横移")},
	};
	float ViewerAimPitch = 0.f;
	bool bViewerAimFire = false;
	const float ViewerSpeeds[] = {0.1f, 0.25f, 0.5f, 1.f};
	const TCHAR* ViewerFrames[] = {TEXT("全身"), TEXT("上身"), TEXT("腿")};
	const float ViewerFrameZ[] = {0.f, 70.f, -90.f};
}

FString AMechCharacter::GetViewerStatus() const
{
	return FString::Printf(TEXT("%s   %d %s（%s：%s）   速度 ×%g%s   取景 %s   %s   补光%s%s"),
		MechLoadout::Item(MechLoadout::Frame, static_cast<int32>(Loadout.Frame)).Name, ViewerAction + 1, ViewerActions[ViewerAction].Name,
		Combat->GetStance() == EMechStance::Blade ? TEXT("近战") : TEXT("射击"),
		Combat->GetStance() == EMechStance::Blade ? Combat->GetBlade().Name : Combat->GetGun().Name,
		ViewerSpeeds[ViewerSpeedIndex], bViewerPaused ? TEXT("（暂停）") : TEXT(""), ViewerFrames[ViewerFrame], bViewerNight ? TEXT("夜晚") : TEXT("白天"),
		bViewerFill ? TEXT("开") : TEXT("关"), bViewerWeaponsOnly ? TEXT("   只看武器") : TEXT(""));
}

void AMechCharacter::ViewerTick(float DeltaSeconds)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}
	const float Dt = GetWorld()->GetDeltaSeconds();  // 不受本机体的时间缩放（暂停 / 慢放）影响
	auto ApplyLight = [this]() {
		ULightingBlueprintLibrary::SetTimeOfDay(this, bViewerNight ? 21.5f : 13.f);
		ULightingBlueprintLibrary::SetWeather(this, bViewerNight ? ELightingWeatherType::LightRain : ELightingWeatherType::Clear, 0.f);
	};
	if (ViewerClock == 0.f)
	{
		// 街道图的出生点挨着墙，镜头绕不开；挪到路中间的空地（往下找地面）
		if (GetWorld()->GetMapName().EndsWith(TEXT("L_Street")))
		{
			FHitResult Hit;
			const FVector Spot(900.f, 6200.f, 0.f);
			if (GetWorld()->LineTraceSingleByChannel(Hit, Spot + FVector(0.f, 0.f, 2000.f), Spot - FVector(0.f, 0.f, 2000.f), ECC_WorldStatic))
			{
				SetActorLocation(Hit.ImpactPoint + FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), false, nullptr,
					ETeleportType::TeleportPhysics);
			}
		}
		ViewerYaw = GetActorRotation().Yaw;
		bViewerNight = FParse::Param(FCommandLine::Get(), TEXT("MechViewerNight"));  // 自动截图看夜晚效果（平时按 N 切）
		GetCharacterMovement()->RotationRate = FRotator::ZeroRotator;
		GetCameraBoom()->bDoCollisionTest = false;
		GetCameraBoom()->bEnableCameraLag = false;
		GetCameraBoom()->SocketOffset = BaseSocketOffset = FVector::ZeroVector;
		BaseFOV = BoostFOV = 55.f;
		// 只看武器时机体网格隐藏，但骨骼要照常更新（武器按手的位置摆）
		GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		PC->SetControlRotation(FRotator(-8.f, ViewerYaw + 215.f, 0.f));  // 从右前方看
	}
	const float T0 = ViewerClock;
	ViewerClock += Dt;
	auto Crossed = [&](float At) { return T0 < At && ViewerClock >= At; };
	if (Crossed(0.6f))
	{
		GetCharacterMovement()->DisableMovement();  // 落稳后锁住，按 WASD、空格也不会走开
	}
	if (Crossed(1.2f))
	{
		ApplyLight();  // 等游戏模式按地图设完灯光再改
	}

	// ---- 按键
	auto Pressed = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	static const FKey Digits[10] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight,
		EKeys::Nine, EKeys::Zero};
	for (int32 i = 0; i < 10; ++i)
	{
		if (Pressed(Digits[i]))
		{
			ViewerAction = i;
		}
	}
	const int32 NumActions = UE_ARRAY_COUNT(ViewerActions);
	if (Pressed(EKeys::Right))
	{
		ViewerAction = (ViewerAction + 1) % NumActions;
	}
	if (Pressed(EKeys::Left))
	{
		ViewerAction = (ViewerAction + NumActions - 1) % NumActions;
	}
	if (Pressed(EKeys::MouseScrollUp))
	{
		ViewerArm = FMath::Max(150.f, ViewerArm * 0.88f);
	}
	if (Pressed(EKeys::MouseScrollDown))
	{
		ViewerArm = FMath::Min(2500.f, ViewerArm / 0.88f);
	}
	if (Pressed(EKeys::F))
	{
		ViewerFrame = (ViewerFrame + 1) % UE_ARRAY_COUNT(ViewerFrames);
	}
	if (Pressed(EKeys::P))
	{
		bViewerPaused = !bViewerPaused;
	}
	if (Pressed(EKeys::LeftBracket))
	{
		ViewerSpeedIndex = FMath::Max(0, ViewerSpeedIndex - 1);
	}
	if (Pressed(EKeys::RightBracket))
	{
		ViewerSpeedIndex = FMath::Min((int32)UE_ARRAY_COUNT(ViewerSpeeds) - 1, ViewerSpeedIndex + 1);
	}
	if (Pressed(EKeys::N))
	{
		bViewerNight = !bViewerNight;
		ApplyLight();
	}
	if (Pressed(EKeys::B))
	{
		bViewerFill = !bViewerFill;
	}
	if (Pressed(EKeys::G))
	{
		bViewerWeaponsOnly = !bViewerWeaponsOnly;
	}
	if (Pressed(EKeys::H))
	{
		bViewerHelp = !bViewerHelp;
	}
	// Z / X：换枪、换刀（直接改配装，换完自动切到对应姿态）
	if (Pressed(EKeys::Z) || Pressed(EKeys::X))
	{
		FMechLoadout L = Loadout;
		if (Pressed(EKeys::Z))
		{
			L.Shooter = static_cast<EMechShooter>((static_cast<int32>(L.Shooter) + 1) % MechWeapons::NumGuns());
			ViewerAction = 9;  // 开火
		}
		else
		{
			L.Melee = static_cast<EMechMeleeWeapon>((static_cast<int32>(L.Melee) + 1) % MechWeapons::NumBlades());
			ViewerAction = 8;  // 轻击连招
		}
		SetLoadout(L, true);
	}
	// C：换框架（10-31 阵营机体，按配装界面的顺序：新纪 / 维克托 / 相位 × 轻中重，Shift+C 倒着换）
	if (Pressed(EKeys::C))
	{
		TArray<int32> Order;
		MechLoadout::DisplayOrder(MechLoadout::Frame, Order);
		const int32 At = FMath::Max(0, Order.IndexOfByKey(static_cast<int32>(Loadout.Frame)));
		const bool bBack = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
		FMechLoadout L = Loadout;
		L.Frame = static_cast<EMechFrame>(Order[(At + (bBack ? Order.Num() - 1 : 1)) % Order.Num()]);
		SetLoadout(L, true);
	}

	// ---- 自动截图：每个动作 2.5 秒，镜头慢慢绕一圈，第 2 秒截一张；最后再拍上身、只看武器各一张
	FString ShotList;
	const bool bShotList = bViewerAuto && FParse::Value(FCommandLine::Get(), TEXT("MechViewerShots="), ShotList) &&
		(ShotList == TEXT("stance") || ShotList == TEXT("catalog") || ShotList == TEXT("shoulder") || ShotList == TEXT("thrust") || ShotList == TEXT("frames"));
	const bool bGripShots = bViewerAuto && ShotList == TEXT("grip");
	const bool bBladeShots = bViewerAuto && ShotList == TEXT("blades");
	const bool bDamperShots = bViewerAuto && ShotList == TEXT("dampers");
	const bool bShowcase = bViewerAuto && (ShotList == TEXT("showcase") || bGripShots || bBladeShots || bDamperShots);
	bViewerAimFire = bViewerAuto && ShotList == TEXT("aimfire");
	const bool bHandShots = bViewerAuto && ShotList == TEXT("hands");
	ViewerHandFocus = -1;
	if (bHandShots)
	{
		static const TArray<FHandShot> Hands = MakeHandShots();
		static const FString Tag = [] {
			FString T = TEXT("改后");
			FParse::Value(FCommandLine::Get(), TEXT("MechHandsTag="), T);
			return T;
		}();
		const float Lead = 1.5f;
		float Begin = Lead;
		int32 Seg = 0;
		while (Seg < Hands.Num() && ViewerClock >= Begin + Hands[Seg].Duration)
		{
			Begin += Hands[Seg].Duration;
			++Seg;
		}
		bViewerHelp = false;
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		if (Seg >= Hands.Num())
		{
			PC->ConsoleCommand(TEXT("quit"));
			bViewerAuto = false;
		}
		else if (ViewerClock >= Lead)
		{
			const FHandShot& S = Hands[Seg];
			// -MechHandsFrame=N：换成这种框架拍（EMechFrame 编号，10-31 阵营机体九台的手都要查）
			static const int32 HandsFrame = [] {
				int32 FrameArg = -1;
				FParse::Value(FCommandLine::Get(), TEXT("MechHandsFrame="), FrameArg);
				return FrameArg;
			}();
			const bool bFrameWrong = HandsFrame >= 0 && static_cast<int32>(Loadout.Frame) != HandsFrame;
			if (T0 < Begin && (bFrameWrong || ((S.Gun >= 0 || S.Blade >= 0) &&
				(S.Gun >= 0 ? static_cast<int32>(Loadout.Shooter) != S.Gun : static_cast<int32>(Loadout.Melee) != S.Blade))))
			{
				FMechLoadout L = Loadout;
				if (S.Gun >= 0)
				{
					L.Shooter = static_cast<EMechShooter>(S.Gun);
				}
				if (S.Blade >= 0)
				{
					L.Melee = static_cast<EMechMeleeWeapon>(S.Blade);
				}
				if (bFrameWrong)
				{
					L.Frame = static_cast<EMechFrame>(HandsFrame);
				}
				SetLoadout(L, true);
			}
			ViewerAction = S.Action;
			ViewerArm = 95.f;
			ViewerFrame = 1;
			ViewerHandFocus = S.Hand;
			// 枪的待机（站立）不会自己切回射击姿态
			if (S.Gun >= 0 && Combat->GetStance() != EMechStance::Gun && Combat->GetSwitchProgress() < 0.f && !Combat->IsMeleeActive())
			{
				Combat->SwitchStance();
			}
			Combat->DebugRefill();  // 开火那几张一直打不换弹
			PC->SetControlRotation(FRotator(-14.f, ViewerYaw + S.CamYaw, 0.f));
			// -MechHandsThumbCam（10-31 握持第二版）：镜头从「手心那一侧、偏拇指」看过去——柄的另一面，拇指绕过来压在四指上的地方
			// （默认镜头从手背看，拇指被手背挡住；新纪中 / 重的手背还有一大块铲斗）
			static const bool bThumbCam = FParse::Param(FCommandLine::Get(), TEXT("MechHandsThumbCam"));
			FVector HandK, HandN, HandD;
			ViewerHandFocusShift = FVector::ZeroVector;
			if (bThumbCam && GetMechMesh()->GetHandAxesWorld(S.Hand, HandK, HandN, HandD))
			{
				PC->SetControlRotation((-(HandN + HandK * 0.55f - HandD * 0.2f)).GetSafeNormal().Rotation());
				// 拉近到 55、对准手心空腔（手坐标 d 6 / n 3 厘米，机体放大 1.35 倍）
				ViewerArm = 55.f;
				ViewerHandFocusShift = (HandD * 6.f + HandN * 3.f) * GetMesh()->GetComponentScale().X;
			}
			// 10-31 握持第二版：双手刀这一段里动作的左手离刀柄多远（逐帧计数：25 内全拉 / 25–40 / 40–60 拉一部分 / 60 外松开）
			static int32 LeftHist[4] = {0, 0, 0, 0};
			static FVector LeftDeltaSum = FVector::ZeroVector;
			if (T0 < Begin)
			{
				LeftHist[0] = LeftHist[1] = LeftHist[2] = LeftHist[3] = 0;
				LeftDeltaSum = FVector::ZeroVector;
			}
			if (const float LeftDist = GetMechMesh()->DebugLeftGripDist; LeftDist >= 0.f)
			{
				++LeftHist[LeftDist < 25.f ? 0 : LeftDist < 40.f ? 1 : LeftDist < 60.f ? 2 : 3];
				LeftDeltaSum += GetMechMesh()->DebugLeftGripDelta;
			}
			const int32 LeftFrames = LeftHist[0] + LeftHist[1] + LeftHist[2] + LeftHist[3];
			const FVector LeftDeltaAvg = LeftFrames > 0 ? LeftDeltaSum / LeftFrames : FVector::ZeroVector;
			if (Crossed(Begin + S.Duration - 0.25f))
			{
				const FString Weapon = S.Blade >= 0 ? Combat->GetBlade().Name : Combat->GetGun().Name;
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Hands") / Tag /
					FString::Printf(TEXT("%02d_%s_%s_%s.png"), Seg + 1, *Weapon, S.Pose, S.Hand == 1 ? TEXT("右手") : TEXT("左手")), false, false);
				const UMechMeshComponent* MM = GetMechMesh();
				UE_LOG(LogTemp, Display, TEXT("[hands] %02d %s %s %s：握法 %d 权重 %.2f（左手托 %.2f）弯曲 %s；左手离柄 <25/25-40/40-60/>60 厘米 %d/%d/%d/%d 帧，平均差（顺柄 %.0f，横 %.0f / %.0f）"),
					Seg + 1, *Weapon, S.Pose, S.Hand == 1 ? TEXT("右手") : TEXT("左手"), static_cast<int32>(MM->Grip[S.Hand].Mode), MM->Grip[S.Hand].Weight,
					MM->LeftGripWeight, *MM->DebugGripText(S.Hand), LeftHist[0], LeftHist[1], LeftHist[2], LeftHist[3], LeftDeltaAvg.X, LeftDeltaAvg.Y,
					LeftDeltaAvg.Z);
			}
		}
	}
	else if (bViewerAimFire)
	{
		const float Per = 2.6f;
		const int32 Step = FMath::FloorToInt(FMath::Max(0.f, ViewerClock - 1.5f) / Per);
		const float Begin = 1.5f + Step * Per;
		bViewerHelp = false;
		static float ErrSum = 0.f, ErrMax = 0.f;
		static int32 ErrN = 0;
		if (Step >= UE_ARRAY_COUNT(AimShots))
		{
			PC->ConsoleCommand(TEXT("quit"));
			bViewerAuto = false;
		}
		else if (ViewerClock >= 1.5f)
		{
			const FAimShot& S = AimShots[Step];
			if (T0 < Begin && static_cast<int32>(Loadout.Shooter) != S.Gun)
			{
				FMechLoadout L = Loadout;
				L.Shooter = static_cast<EMechShooter>(S.Gun);
				SetLoadout(L, true);
			}
			Combat->DebugRefill();  // 一直开火不换弹
			ViewerAction = S.Action;
			ViewerAimPitch = S.Pitch;
			ViewerArm = 520.f;
			ViewerFrame = 1;
			PC->SetControlRotation(FRotator(-6.f, ViewerYaw + S.CamYaw, 0.f));
			// 后半段量枪管（枪模型 +X）偏离「枪口 → 瞄准点」多少度（换弹时不算）
			const UMechMeshComponent* M = GetMechMesh();
			if (ViewerClock > Begin + 1.f && !Combat->IsReloading() && M->Props[1] && !M->AimOffset.IsNearlyZero())
			{
				const FVector Muzzle = M->GetPropPoint(1, Combat->GetGun().Muzzle);
				const float Err = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(M->Props[1]->GetForwardVector() |
					(M->VisualPivot() + M->AimOffset - Muzzle).GetSafeNormal(), -1.f, 1.f)));
				ErrSum += Err;
				ErrMax = FMath::Max(ErrMax, Err);
				++ErrN;
			}
			if (Crossed(Begin + 1.9f))
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/AimFire") / FString::Printf(TEXT("%02d_%s.png"), Step + 1, S.Name),
					false, false);
			}
			if (Crossed(Begin + Per - 0.05f))
			{
				UE_LOG(LogTemp, Display, TEXT("[aimfire] %02d %s（%s，瞄准俯仰 %.0f）：枪管偏 平均 %.1f 最大 %.1f 度（%d 帧），枪重 %.2f 臂重 %.2f，上身转 %.0f 度"), Step + 1, S.Name,
					Combat->GetGun().Name, S.Pitch, ErrN ? ErrSum / ErrN : -1.f, ErrMax, ErrN, M->GunAimWeight, M->ArmWeight[1], M->DebugGunTurn);
				ErrSum = ErrMax = 0.f;
				ErrN = 0;
			}
		}
	}
	else if (bShowcase)
	{
		const FShowcase* Shows = bGripShots ? GripShots : bBladeShots ? BladeShots : bDamperShots ? DamperShots : Showcase;
		const int32 NumShowcase = bGripShots ? UE_ARRAY_COUNT(GripShots) : bBladeShots ? UE_ARRAY_COUNT(BladeShots) :
			bDamperShots ? UE_ARRAY_COUNT(DamperShots) : UE_ARRAY_COUNT(Showcase);
		// 轮流展示：每段换上那把武器、播那个动作，镜头慢慢绕；录像时每帧一张图，否则每段中间一张
		static const bool bVideo = FParse::Param(FCommandLine::Get(), TEXT("MechViewerVideo"));
		static int32 FrameIndex = 0;
		const float Lead = 1.5f;
		float Begin = Lead;
		int32 Seg = 0;
		while (Seg < NumShowcase && ViewerClock >= Begin + Shows[Seg].Duration)
		{
			Begin += Shows[Seg].Duration;
			++Seg;
		}
		bViewerHelp = false;
		if (Seg >= NumShowcase)
		{
			PC->ConsoleCommand(TEXT("quit"));
			bViewerAuto = false;
		}
		else if (ViewerClock >= Lead)
		{
			const FShowcase& S = Shows[Seg];
			if (T0 < Begin)
			{
				// 这一段开始：换武器（换完回满；近战、射击姿态由下面按动作自动切）
				FMechLoadout L = Loadout;
				if (S.Gun >= 0)
				{
					L.Shooter = static_cast<EMechShooter>(S.Gun);
				}
				if (S.Blade >= 0)
				{
					L.Melee = static_cast<EMechMeleeWeapon>(S.Blade);
				}
				if (S.Gun >= 0 || S.Blade >= 0)
				{
					SetLoadout(L, true);
				}
				ViewerAction = S.Action;
				UE_LOG(LogTemp, Display, TEXT("[showcase] %d %s"), Seg, *GetViewerStatus());
			}
			ViewerAction = S.Action;
			ViewerArm = S.Arm;
			ViewerFrame = bDamperShots ? 2 : bGripShots || bBladeShots ? 1 : 0;
			if (bDamperShots && (Crossed(Begin + S.Duration * 0.3f) || Crossed(Begin + S.Duration * 0.7f)))
			{
				// 两个销孔的距离：关节一弯就变，说明活塞杆在缸筒里伸缩
				const USkeletalMeshComponent* SK = GetMesh();
				UE_LOG(LogTemp, Display, TEXT("[damper] %s 左膝减震销距 %.1f 左踝减震销距 %.1f 厘米"), ViewerActions[S.Action].Name,
					FVector::Dist(SK->GetBoneLocation(TEXT("damper_knee_a_l")), SK->GetBoneLocation(TEXT("damper_knee_b_l"))),
					FVector::Dist(SK->GetBoneLocation(TEXT("damper_ankle_a_l")), SK->GetBoneLocation(TEXT("damper_ankle_b_l"))));
			}
			PC->SetControlRotation(FRotator(-8.f, ViewerYaw + S.CamYaw + (ViewerClock - Begin) * 6.f, 0.f));
			const FString Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots/Showcase");
			if (bVideo)
			{
				FScreenshotRequest::RequestScreenshot(Dir / FString::Printf(TEXT("%05d.png"), FrameIndex++), true, false);
			}
			else if (Crossed(Begin + S.Duration * 0.7f))
			{
				FScreenshotRequest::RequestScreenshot(Dir / FString::Printf(TEXT("%02d_%s.png"), Seg, bGripShots ? Combat->GetGun().Name :
					bBladeShots ? *FString::Printf(TEXT("%s_%s"), Combat->GetBlade().Name, ViewerActions[S.Action].Name) : ViewerActions[S.Action].Name),
					true, false);
				if (bGripShots && RifleMesh)
				{
					// 左手离握点（护木）还差多少（厘米，世界尺寸）
					const FVector Grip = RifleMesh->GetComponentTransform().TransformPosition(Combat->GetGun().ForeGrip);
					UE_LOG(LogTemp, Display, TEXT("[grip] %s 左手离握点 %.1f 厘米（IK 权重 %.2f）"), Combat->GetGun().Name,
						FVector::Dist(GetMesh()->GetBoneLocation(TEXT("hand_l")), Grip), GetMechMesh()->LeftGripWeight);
				}
			}
		}
	}
	else if (bShotList)
	{
		const bool bCatalog = ShotList == TEXT("catalog");
		const bool bShoulderShots = ShotList == TEXT("shoulder");
		const bool bThrustShots = ShotList == TEXT("thrust");
		// 10-31 阵营机体 -MechViewerShots=frames：九台（按企业、轻中重）各拍 端枪开火（右前）/ 挥刀连招（右前）/ 飞行（右后，喷焰贴不贴喷口）/
		// 重斩（左前上身，头和手）四张，截图 Saved/Screenshots/FrameActions/<序号>_<台>_<动作>.png
		const bool bFrameShots = ShotList == TEXT("frames");
		static const TArray<FStanceShot> FrameShots = [] {
			static TArray<FString> Names;
			TArray<int32> Order;
			MechLoadout::DisplayOrder(MechLoadout::Frame, Order);
			struct FPose { int32 Action; float CamYaw; float Arm; const TCHAR* Name; int32 Framing; float Pitch; };
			const FPose Poses[] = {{9, 215.f, 560.f, TEXT("开火_右前"), 0, -8.f}, {8, 215.f, 700.f, TEXT("挥刀_右前"), 0, -8.f},
				{17, 300.f, 520.f, TEXT("飞行_右后"), 0, -4.f}, {10, 140.f, 360.f, TEXT("重斩_左前上身"), 1, -6.f}};
			Names.Reserve(Order.Num() * UE_ARRAY_COUNT(Poses));
			TArray<FStanceShot> Out;
			for (const int32 F : Order)
			{
				for (const FPose& P : Poses)
				{
					Names.Add(FString::Printf(TEXT("%s_%s"), MechCatalog::FrameBody(F).Key, P.Name));
					FStanceShot S{P.Action, P.CamYaw, P.Arm, *Names.Last(), P.Framing, false, P.Pitch};
					S.MechFrame = F;
					Out.Add(S);
				}
			}
			return Out;
		}();
		bViewerForceThrust = bThrustShots;
		const FStanceShot* Shots = bFrameShots ? FrameShots.GetData() : bCatalog ? CatalogShots : bShoulderShots ? ShoulderShots : bThrustShots ? ThrustShots : StanceShots;
		const int32 NumShots = bFrameShots ? FrameShots.Num() : bCatalog ? UE_ARRAY_COUNT(CatalogShots) : bShoulderShots ? UE_ARRAY_COUNT(ShoulderShots)
			: bThrustShots ? UE_ARRAY_COUNT(ThrustShots) : UE_ARRAY_COUNT(StanceShots);
		const float Per = 3.f;
		const int32 Step = FMath::FloorToInt(FMath::Max(0.f, ViewerClock - 1.5f) / Per);
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = !bCatalog;  // 配装图不要说明面板
		}
		if (Step >= NumShots)
		{
			PC->ConsoleCommand(TEXT("quit"));
			bViewerAuto = false;
		}
		else
		{
			const FStanceShot& S = Shots[Step];
			if (S.Shoulder >= 0 && static_cast<int32>(Loadout.Shoulder) != S.Shoulder && HasAuthority())
			{
				FMechLoadout L = Loadout;
				L.Shoulder = static_cast<EMechShoulder>(S.Shoulder);
				SetLoadout(L, false);
			}
			if (S.MechFrame >= 0 && static_cast<int32>(Loadout.Frame) != S.MechFrame && HasAuthority())
			{
				FMechLoadout L = Loadout;
				L.Frame = static_cast<EMechFrame>(S.MechFrame);
				SetLoadout(L, true);
			}
			// 护盾那几张开着盾拍（每张开头开一次，不管冷却）
			if (bShoulderShots && S.Shoulder == static_cast<int32>(EMechShoulder::Shield) && Crossed(1.5f + Step * Per + 0.3f))
			{
				Shoulder->DebugShield(1.f);  // 要对比亮度用 -MechShieldGain=
			}
			ViewerAction = S.Action;
			ViewerArm = S.Arm;
			ViewerFrame = S.Frame;
			bViewerWeaponsOnly = S.bWeaponsOnly;
			PC->SetControlRotation(FRotator(S.Pitch, ViewerYaw + S.CamYaw, 0.f));
			if (Crossed(1.5f + Step * Per + 2.5f))
			{
				static const bool bThrusterOld = FParse::Param(FCommandLine::Get(), TEXT("MechThrusterOld"));
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / (bCatalog ? TEXT("Screenshots/Catalog") : bShoulderShots ? TEXT("Screenshots/ShoulderViewer")
					: bThrustShots ? TEXT("Screenshots/Thrust") : bFrameShots ? TEXT("Screenshots/FrameActions") : TEXT("Screenshots/Stance")) /
					FString::Printf(TEXT("%s%02d_%s.png"), bThrustShots && bThrusterOld ? TEXT("旧_") : TEXT(""), Step + 1, S.Name), false, false);
				UE_LOG(LogTemp, Display, TEXT("[viewer] %s %s"), S.Name, *GetViewerStatus());
			}
		}
	}
	else if (bViewerAuto)
	{
		const float Per = 2.5f;
		const int32 Step = FMath::FloorToInt(FMath::Max(0.f, ViewerClock - 1.5f) / Per);
		ViewerAction = FMath::Min(Step, NumActions - 1);
		ViewerFrame = Step == NumActions ? 1 : 0;
		bViewerWeaponsOnly = Step == NumActions + 1;
		ViewerArm = Step >= NumActions ? 380.f : 650.f;
		PC->SetControlRotation(FRotator(-8.f, ViewerYaw + 215.f + ViewerClock * 30.f, 0.f));
		if (Crossed(1.5f + Step * Per + 2.f))
		{
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Viewer") / FString::Printf(TEXT("%02d_%s.png"), Step + 1,
				Step < NumActions ? ViewerActions[ViewerAction].Name : (Step == NumActions ? TEXT("上身") : TEXT("只看武器")));
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			UE_LOG(LogTemp, Display, TEXT("[viewer] %s"), *GetViewerStatus());
		}
		if (Step >= NumActions + 2)
		{
			PC->ConsoleCommand(TEXT("quit"));
			bViewerAuto = false;
		}
	}

	// ---- 应用
	const FViewerAction& A = ViewerActions[ViewerAction];
	SetActorRotation(FRotator(0.f, ViewerYaw, 0.f));
	bAnimOverride = true;
	AnimOverrideVelocity = GetActorTransform().TransformVectorNoScale(A.Velocity);
	bAnimOverrideAir = A.bAir;
	bAnimOverrideBoost = A.bBoost;
	if (A.Act == 11)
	{
		// 起步急停：跑 1.6 秒、停 1.6 秒轮流
		AnimOverrideVelocity = FMath::Fmod(GetWorld()->GetTimeSeconds(), 3.2f) < 1.6f ? GetActorForwardVector() * 680.f : FVector::ZeroVector;
	}
	if (A.Act == 12)
	{
		// 重落地：高速下落 0.8 秒、着地 1.8 秒轮流
		const bool bFalling = FMath::Fmod(GetWorld()->GetTimeSeconds(), 2.6f) < 0.8f;
		bAnimOverrideAir = bFalling;
		AnimOverrideVelocity = bFalling ? FVector(0.f, 0.f, -2600.f) : FVector::ZeroVector;
	}
	Combat->AimOverride = bViewerAimFire ? FRotator(ViewerAimPitch, ViewerYaw, 0.f).Vector() : GetActorForwardVector();
	// 需要刀的动作自动拔刀，开火 / 换弹自动收刀
	const bool bNeedBlade = A.Act == 1 || A.Act == 3 || A.Act == 4 || A.Act == 8;
	const bool bNeedGun = A.Act == 2 || A.Act == 9 || bViewerAimFire;
	if (Combat->GetSwitchProgress() < 0.f && !Combat->IsMeleeActive() &&
		((bNeedBlade && Combat->GetStance() != EMechStance::Blade) || (bNeedGun && Combat->GetStance() != EMechStance::Gun)))
	{
		Combat->SwitchStance();
	}
	// 开火：精准步枪按住 1 秒松开一次，其余一直按着
	const bool bChargeGun = Combat->GetGun().ChargeTime > 0.f;
	const bool bFire = A.Act == 2 || bViewerAimFire;
	Combat->SetTrigger(bFire && (!bChargeGun || FMath::Fmod(GetWorld()->GetTimeSeconds(), 1.4f) < 1.f));
	Combat->SetAltHeld(A.Act == 4 || (bFire && Combat->GetGun().EnergyPerShot > 0.f));
	if (A.Act == 9 && !Combat->IsReloading() && Combat->GetGun().Magazine > 0)
	{
		Combat->DebugReload();
	}
	if (A.Act == 1 && Combat->GetComboStep() < 4)
	{
		Combat->RequestLight();
	}
	if (A.Act == 3 && !Combat->IsMeleeActive() && Combat->GetHeavyReady() >= 1.f)
	{
		Combat->RequestHeavy();
	}
	const float Clock = GetWorld()->GetTimeSeconds();
	static const FVector Dirs8[8] = {FVector(1, 0, 0), FVector(1, 1, 0), FVector(0, 1, 0), FVector(-1, 1, 0), FVector(-1, 0, 0), FVector(-1, -1, 0),
		FVector(0, -1, 0), FVector(1, -1, 0)};
	if (A.Act == 5 && Clock - DodgeStart > 1.f)
	{
		DebugDodge(Dirs8[(DodgeDir + 1) % 8]);
	}
	if (A.Act == 6 && Clock - StaggerStart > 1.6f)
	{
		DebugStagger(Dirs8[((StaggerDir + 1) % 4) * 2]);
	}
	if (A.Act == 7 && Combat->GetSwitchProgress() < 0.f && FMath::Fmod(Clock, 1.5f) < Dt)
	{
		Combat->SwitchStance();
	}
	CustomTimeDilation = bViewerPaused ? 0.f : ViewerSpeeds[ViewerSpeedIndex];
	GetMesh()->SetVisibility(!bViewerWeaponsOnly);
	FillLight->SetVisibility(bViewerFill);
	GetCameraBoom()->TargetArmLength = FMath::FInterpTo(GetCameraBoom()->TargetArmLength, ViewerArm, Dt, 10.f);
	GetCameraBoom()->TargetOffset = FMath::VInterpTo(GetCameraBoom()->TargetOffset, FVector(0.f, 0.f, ViewerFrameZ[ViewerFrame]), Dt, 8.f);
	if (ViewerHandFocus >= 0)
	{
		// 手部特写：镜头臂直接对准这只手（手在动，每帧跟上，不插值）
		GetCameraBoom()->TargetArmLength = ViewerArm;
		GetCameraBoom()->TargetOffset = GetMesh()->GetBoneLocation(ViewerHandFocus == 1 ? TEXT("hand_r") : TEXT("hand_l")) + ViewerHandFocusShift -
			GetCameraBoom()->GetComponentLocation();
	}
}

void AMechCharacter::BeautyTick(float DeltaSeconds)
{
	struct FShot
	{
		const TCHAR* Name;
		FVector Robot;   // 机体脚下位置（米）
		float Yaw;       // 机体朝向
		FVector Cam;     // 机位（米）
		FVector Look;    // 看向（米）
		float Fov;
	};
	// 机位都放在机体斜前方 6–9 米、略低于胸口，机体占画面三分之一左右；每张地图一组
	static const FShot AlleyShots[] = {
		{TEXT("b1_street"), {-38.f, 1.0f, 0.15f}, 205.f, {-45.f, -2.0f, 0.9f}, {-38.f, 1.0f, 2.0f}, 50.f},
		{TEXT("b2_bridge"), {-16.f, -2.f, 0.15f}, 190.f, {-24.f, 1.5f, 0.8f}, {-15.f, -2.5f, 3.2f}, 55.f},
		{TEXT("b3_neon"), {-30.f, 4.5f, 0.15f}, 230.f, {-35.5f, -1.5f, 1.2f}, {-29.f, 5.5f, 3.0f}, 50.f},
		{TEXT("b4_lamp"), {-3.f, -6.f, 0.15f}, 120.f, {-9.f, -1.f, 1.0f}, {-2.f, -6.f, 2.2f}, 50.f},
		{TEXT("b5_overview"), {-18.f, 0.f, 7.4f}, 0.f, {-23.5f, -1.f, 9.0f}, {4.f, 1.f, 5.0f}, 62.f},
		{TEXT("b6_rear"), {-30.f, 0.f, 0.15f}, 0.f, {-38.f, 0.5f, 1.6f}, {-20.f, 0.f, 3.0f}, 60.f},
	};
	// 赛博朋克街道：路沿 Y 轴，路中线 X≈9 米，Y 从 11 到 112 米，深处在 -Y；左侧（-X）是高架路
	static const FShot StreetShots[] = {
		{TEXT("s1_street"), {9.f, 80.f, 0.15f}, 90.f, {7.f, 88.f, 1.0f}, {}, 50.f},
		{TEXT("s2_highway"), {-6.f, 58.f, 0.15f}, 50.f, {2.f, 66.f, 1.5f}, {}, 55.f},
		{TEXT("s3_skybridge"), {5.f, -5.f, 9.4f}, 200.f, {-3.f, -1.f, 11.f}, {}, 55.f},
		{TEXT("s4_rear"), {9.f, 70.f, 0.15f}, -90.f, {9.5f, 79.f, 2.4f}, {}, 60.f},
		{TEXT("s5_overview"), {9.f, 50.f, 0.15f}, 120.f, {17.f, 72.f, 12.f}, {}, 50.f},
		{TEXT("s6_neon"), {13.5f, 45.f, 0.15f}, 200.f, {8.f, 52.f, 1.4f}, {}, 50.f},
	};
	// 其他地图：机体站在出生点，机位按出生朝向（前 +X、右 +Y）相对摆放
	static const FShot SpawnShots[] = {
		{TEXT("g1_front"), {0.f, 0.f, 0.f}, 180.f, {-8.f, -2.5f, 1.2f}, {}, 50.f},
		{TEXT("g2_side"), {0.f, 0.f, 0.f}, 120.f, {-3.f, 7.f, 1.5f}, {}, 55.f},
		{TEXT("g3_rear"), {0.f, 0.f, 0.f}, 0.f, {-7.f, 1.5f, 2.5f}, {}, 65.f},
		{TEXT("g4_high"), {0.f, 0.f, 0.f}, 30.f, {-25.f, -12.f, 14.f}, {}, 55.f},
		{TEXT("g5_wide"), {0.f, 0.f, 0.f}, 0.f, {-45.f, 6.f, 8.f}, {}, 70.f},
	};
	const FString MapName = GetWorld()->GetMapName();
	const bool bStreet = MapName.EndsWith(TEXT("L_Street"));
	const bool bAlley = MapName.Contains(TEXT("Alley"));
	const FShot* Shots = bStreet ? StreetShots : bAlley ? AlleyShots : SpawnShots;
	const int32 NumShots = bStreet ? UE_ARRAY_COUNT(StreetShots) : bAlley ? UE_ARRAY_COUNT(AlleyShots) : UE_ARRAY_COUNT(SpawnShots);
	const bool bRelative = !bStreet && !bAlley;
	if (BeautyIndex < 0)
	{
		BeautySpawn = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());  // 脚底
		BeautySpawnYaw = GetActorRotation().Yaw;
	}
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}
	BeautyTime += DeltaSeconds;
	const float Per = 2.6f;
	const int32 Index = FMath::FloorToInt(BeautyTime / Per);
	if (Index >= NumShots)
	{
		bBeauty = false;
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	if (Index != BeautyIndex)
	{
		BeautyIndex = Index;
		bBeautyShotTaken = false;
		const FShot& S = Shots[Index];
		const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FRotator Base(0.f, bRelative ? BeautySpawnYaw : 0.f, 0.f);
		const FVector Origin = bRelative ? BeautySpawn : FVector::ZeroVector;
		SetActorLocationAndRotation(Origin + Base.RotateVector(S.Robot * 100.f) + FVector(0.f, 0.f, Half + 5.f),
			FRotator(0.f, Base.Yaw + S.Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		GetCharacterMovement()->StopMovementImmediately();
		// 用机体自己的跟随镜头构图：镜头臂从机体中心指向想要的机位，长度就是两者距离（另放摄像机时机体拍不到）
		const FVector Center = GetActorLocation();
		const FVector CamLoc = Origin + Base.RotateVector(S.Cam * 100.f);
		GetCameraBoom()->bDoCollisionTest = false;
		GetCameraBoom()->bEnableCameraLag = false;
		GetCameraBoom()->SocketOffset = FVector::ZeroVector;
		GetCameraBoom()->TargetArmLength = FVector::Dist(Center, CamLoc);
		PC->SetControlRotation((Center - CamLoc).Rotation());
		BaseFOV = BoostFOV = S.Fov;
		GetFollowCamera()->SetFieldOfView(S.Fov);
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
	}
	if (!bBeautyShotTaken && BeautyTime - Index * Per > 2.1f)
	{
		bBeautyShotTaken = true;
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Beauty") / FString(Shots[Index].Name) + TEXT(".png");  // 文件名前缀区分地图
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogTemp, Display, TEXT("[beauty] %s 机体位置 %s 包围盒中心 %s 半径 %.0f 最近渲染 %d"), Shots[Index].Name,
			*GetActorLocation().ToString(), *GetMesh()->Bounds.Origin.ToString(), GetMesh()->Bounds.SphereRadius,
			GetMesh()->WasRecentlyRendered(0.5f) ? 1 : 0);
	}
}

// ---------------------------------------------------------------- 夺芯：带芯
void AMechCharacter::SetCarryingCore(bool bCarrying)
{
	if (bCarryingCore == bCarrying)
	{
		return;
	}
	bCarryingCore = bCarrying;
	ApplyCarryRestrictions();
}

void AMechCharacter::OnRep_CarryingCore()
{
	ApplyCarryRestrictions();
}

void AMechCharacter::ApplyCarryRestrictions()
{
	GetMechMovement()->bCarryLock = bCarryingCore;
	if (bCarryingCore)
	{
		GetMechMovement()->bWantsThrust = false;
	}
}

void AMechCharacter::ServerThrowCore_Implementation(FVector_NetQuantizeNormal Aim)
{
	for (TActorIterator<AMechCoreDirector> It(GetWorld()); It; ++It)
	{
		It->ThrowCore(this, Aim);
		break;
	}
}

// ---------------------------------------------------------------- 第一人称：机体自己的眼睛（V 切换）
float AMechCharacter::GetBootTime() const
{
	if (!bFirstPerson || BootStart < 0.f)
	{
		return -1.f;
	}
	const float T = GetWorld()->GetRealTimeSeconds() - BootStart;
	return T < BootLength ? T : -1.f;
}

void AMechCharacter::SetFirstPerson(bool bOn, bool bReboot)
{
	bFirstPerson = bOn;
	USpringArmComponent* Boom = GetCameraBoom();
	if (bOn)
	{
		Boom->TargetArmLength = 0.f;
		Boom->bEnableCameraLag = false;
		Boom->bDoCollisionTest = false;
		BaseSocketOffset = FVector(30.f, 0.f, 0.f);  // 眼睛在头的前面一点，不会看到自己的头
		if (bReboot)
		{
			// 开打前的倒计时放完整的启动（3 秒），中途重生、切视角放短的
			const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
			BootLength = GS && GS->Phase == EMechRoundPhase::Countdown ? 3.f : 1.8f;
			BootStart = GetWorld()->GetRealTimeSeconds();
		}
	}
	else
	{
		Boom->TargetArmLength = 560.f;
		Boom->TargetOffset = FVector::ZeroVector;
		Boom->bEnableCameraLag = true;
		Boom->bDoCollisionTest = true;
		BaseSocketOffset = FVector(0.f, 70.f, 110.f);
		BootStart = -100.f;
	}
}

void AMechCharacter::UpdateFirstPerson(float DeltaSeconds)
{
	if (!IsLocallyControlled() || !IsPlayerControlled() || bViewer || bBeauty || bDemo || bAnimTest)
	{
		return;
	}
	if (!bViewInit)
	{
		bViewInit = true;
		SetFirstPerson(FMechSettings::Get().bFirstPerson || FParse::Param(FCommandLine::Get(), TEXT("MechFirstPerson")), true);  // 出生 / 重生：第一人称就放启动画面
	}
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (MechKeys::JustPressed(PC, EMechKey::FirstPerson) && !Health->IsDead())
	{
		FMechSettings::Get().bFirstPerson = !bFirstPerson;
		FMechSettings::Get().Save();
		SetFirstPerson(!bFirstPerson, true);
	}
	// 自己的机体和枪在自己的画面里不画（端在眼前会挡掉半个屏幕）；挥刀时显示刀，看得到砍击。
	// 开镜时由开镜那段统一藏；阵亡后恢复（观战时看得到残骸）
	const bool bHideBody = bFirstPerson && !Health->IsDead();
	if (bHideBody && !bScopeHidden)
	{
		const bool bSwing = Combat->IsMeleeActive();
		GetMesh()->SetOwnerNoSee(true);
		for (int32 i = 0; i < UMechMeshComponent::NumProps; ++i)
		{
			if (UPrimitiveComponent* Prop = Cast<UPrimitiveComponent>(GetMechMesh()->Props[i].Get()))
			{
				Prop->SetOwnerNoSee(!(bSwing && (i == 0 || i == 4)));
			}
		}
	}
	else if (!bHideBody && bHeadHidden && !bScopeHidden)
	{
		GetMesh()->SetOwnerNoSee(false);
		for (int32 i = 0; i < UMechMeshComponent::NumProps; ++i)
		{
			if (UPrimitiveComponent* Prop = Cast<UPrimitiveComponent>(GetMechMesh()->Props[i].Get()))
			{
				Prop->SetOwnerNoSee(false);
			}
		}
	}
	bHeadHidden = bHideBody;
	FPostProcessSettings& PP = GetFollowCamera()->PostProcessSettings;
	if (!bFirstPerson)
	{
		PP.bOverride_VignetteIntensity = false;
		PP.bOverride_FilmGrainIntensity = false;
		PP.bOverride_ColorSaturation = false;
		return;
	}
	// 眼睛高度按机体身高固定（跟着头部骨骼走的话，失衡、落地这些动作会把镜头压到地上）；走路时按步频轻微上下
	EyeLocal = FVector(20.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.72f);
	const float Speed = GetVelocity().Size2D();
	const bool bGround = !GetCharacterMovement()->IsFalling();
	StepPhase += DeltaSeconds * (bGround ? FMath::Clamp(Speed / 220.f, 0.f, 6.f) : 0.f);
	const float Bob = bGround ? FMath::Sin(StepPhase * PI) * FMath::Clamp(Speed / 700.f, 0.f, 1.f) * 4.f : 0.f;
	GetCameraBoom()->TargetOffset = FRotator(0.f, GetActorRotation().Yaw, 0.f).RotateVector(EyeLocal) + FVector(0.f, 0.f, Bob);
	// 传感器画面：暗角、颗粒；启动时先是低饱和、颗粒很重，再慢慢正常
	const float Boot = GetBootTime();
	const float S = Boot >= 0.f ? FMath::Clamp((Boot / BootLength * 3.f - 0.25f) / 2.2f, 0.f, 1.f) : 1.f;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = FMath::Lerp(1.8f, 0.65f, S);
	PP.bOverride_FilmGrainIntensity = true;
	PP.FilmGrainIntensity = FMath::Lerp(1.f, 0.12f, S);
	PP.bOverride_ColorSaturation = S < 1.f;
	PP.ColorSaturation = FVector4(1.f, 1.f, 1.f, FMath::Lerp(0.1f, 1.f, S));
}

// ---------------------------------------------------------------- 瞄准粘滞：准星压在敌人身上时转视角变慢，边缘一圈渐变回原速
void AMechCharacter::UpdateAimFriction()
{
	AimFriction = 1.f;
	if (!IsLocallyControlled() || !IsPlayerControlled() || bViewer || Health->IsDead())
	{
		return;
	}
	const FVector Eye = GetFollowCamera()->GetComponentLocation();
	const FVector Fwd = GetFollowCamera()->GetForwardVector();
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (*It == this || It->GetTeam() == Team || It->GetHealth()->IsDead())
		{
			continue;
		}
		const FVector To = It->GetActorLocation() - Eye;
		const float Dist = To.Size();
		if (Dist < 1.f || Dist > 15000.f)
		{
			continue;
		}
		const float Ang = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Fwd | (To / Dist), -1.f, 1.f)));
		const float R = FMath::RadiansToDegrees(FMath::Atan(170.f / Dist));  // 机体半宽约 1.7 米
		const float F = Ang < R ? MechWeapons::AimFriction : Ang < R * 2.f ? FMath::Lerp(MechWeapons::AimFriction, 1.f, (Ang - R) / R) : 1.f;
		AimFriction = FMath::Min(AimFriction, F);
	}
}

void AMechCharacter::SetRun(UMechRunComponent* InRun)
{
	Run = InRun;
}

float AMechCharacter::EffectScale(EMechStat Stat) const
{
	return Effects ? Effects->GetScale(Stat) : 1.f;
}

void AMechCharacter::SetPaint(const FMechPaint& InPaint)
{
	Paint = InPaint;
	ApplyPaint();
}

void AMechCharacter::OnRep_Paint()
{
	ApplyPaint();
}

void AMechCharacter::ApplyPaint()
{
	const TPair<const TCHAR*, FColor> Slots[] = {{TEXT("MI_Robot_Paint"), Paint.Primary}, {TEXT("MI_Robot_PaintDark"), Paint.Secondary},
		{TEXT("MI_Robot_Accent"), Paint.Accent}};
	// 机体和背后的肩部挂件一起刷
	for (UMeshComponent* Target : {static_cast<UMeshComponent*>(GetMesh()), static_cast<UMeshComponent*>(PodMesh.Get())})
	{
		for (const TPair<const TCHAR*, FColor>& S : Slots)
		{
			const int32 Index = Target ? Target->GetMaterialIndex(S.Key) : INDEX_NONE;
			if (Index == INDEX_NONE)
			{
				continue;
			}
			if (UMaterialInstanceDynamic* MID = Target->CreateAndSetMaterialInstanceDynamic(Index))
			{
				MID->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(S.Value));
				// 维克托三台的主漆槽是物体空间折线迷彩（M_RobotCamo）：三色 = 主漆、副漆、主漆派生的灰；别的材质没有这个参数，设了也没事
				MID->SetVectorParameterValue(TEXT("CamoDark"), FLinearColor(Paint.Secondary));
			}
		}
	}
}

void AMechCharacter::SetShowcasePose(bool bOn)
{
	RifleMesh->SetHiddenInGame(bOn);
	OffGunMesh->SetHiddenInGame(bOn);
	// 10-31：和结算的竖拇指走同一套展示姿势（以前只开了竖拇指，上身照旧叠端枪片段、还按六成去对枪管，拧着身子）
	bDisplayPose = bOn;
	bResultsStow = bOn;
	ResultsLean = 0.f;
	if (UMechMeshComponent* M = GetMechMesh())
	{
		M->ThumbsUp = bOn ? 1.f : 0.f;
	}
}

void AMechCharacter::SetResultsPose(bool bThumbs, float Lean, float GunDown)
{
	bDisplayPose = true;
	bResultsStow = bThumbs;
	ResultsLean = Lean;
	ShowGunDown = GunDown;
	if (UMechMeshComponent* M = GetMechMesh())
	{
		M->ThumbsUp = bThumbs ? 1.f : 0.f;
	}
}

#include "MechItemStudio.h"
#include "MechWeaponGlow.h"

#include "Algo/Sort.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MechAnimInstance.h"
#include "MechCatalog.h"
#include "MechCharacter.h"
#include "MechLoadout.h"
#include "MechMeshComponent.h"
#include "MechSettings.h"
#include "MechWeapons.h"
#include "UObject/ConstructorHelpers.h"

const FVector AMechItemStudio::StudioLocation(0.f, 0.f, -250000.f);

namespace MechStudioPriv  // 不用匿名命名空间：合并编译时会和别的文件里同名的东西撞
{
	// 贴图尺寸：小图（卡片）、详细视角（缩略条和点开后的大图）。实时转台的大小跟着界面上照片的像素大小（LiveMaxW 封顶）
	constexpr int32 ThumbW = 560, ThumbH = 350;
	constexpr int32 ViewW = 1600, ViewH = 900;  // 11-01：880 → 1600：点开后铺满大图，用户的窗口里约 1440 像素宽
	/** 实时转台渲染目标最大宽度（像素）：用户 2880×1620 窗口里测试场的照片约 1440×810，选配约 1050×590；再乘 LiveSuperSample */
	constexpr int32 LiveMaxW = 2200;
	/** 实时转台按屏幕像素的几倍拍（缩小显示 = 超采样抗锯齿；一次性捕获没有可靠的时间抗锯齿） */
	constexpr float LiveSuperSample = 1.5f;
	/** 分身棚离本棚多远（棚里的灯衰减半径 2500，隔开就互不照到） */
	const FVector LiveTwinOffset(0.f, 20000.f, 0.f);
	/** 转台的槽位 */
	constexpr int32 SpinSlot = 0;
	/** 每帧最多拍几张（分帧拍，进配装页 / 选配时不卡） */
	constexpr int32 ShotsPerFrame = 5;
	/** 贴图强制流送多久后补拍（第一次拍时多半还是低精度的） */
	constexpr double StreamDelay = 2.5;
	constexpr float FOV = 28.f;
	/** 推进器照片里喷焰材质（M_MechBeam）的亮度：游戏里 0.15–0.4 是配自动曝光的，棚里曝光固定，同样的值整根锥体冲成白的 */
	constexpr float FlameIntensity = 0.02f;
	/** 推进器照片里喷口冷光的亮度（坎德拉，乘喷焰长度） */
	constexpr float JetCandela = 60.f;
	/** 幕布灯亮度（坎德拉 / 平方米）：灯到幕布的距离按取景变，亮度跟着距离平方走，幕布中心亮度就和物件大小无关 */
	constexpr float BackCandelaPerSqM = 55.f;
	/** 槽位：-1 小图、0 转台、100.. 详细视角 */
	constexpr int32 ViewSlot = 100;
	/** 浮空无人机在机体左肩外上方的位置（机体坐标：前 X 右 Y 上 Z，厘米，相对胶囊中心）——UMechShoulderComponent::DroneOffset 的默认值 */
	const FVector DroneOffset(-20.f, -66.f, 98.f);
	// 背上两个主推进器的喷口和喷射方向（网格绑定姿势坐标，未乘 RobotScale）：10-31 起每台机体不同，和 AMechCharacter 一样从
	// MechCatalog::FrameBody 取（喷焰挂 spine_05）
	const FName ThrusterBone(TEXT("spine_05"));

	int32 KeyOf(int32 Category, int32 Index)
	{
		return Category * 100 + Index;
	}

	int32 ShotKey(int32 Key, int32 Slot)
	{
		return Key * 1000 + Slot + 1;
	}

	UTextureRenderTarget2D* MakeTarget(UObject* Outer, int32 W, int32 H)
	{
		UTextureRenderTarget2D* T = NewObject<UTextureRenderTarget2D>(Outer);
		// 11-01：RGBA8_SRGB → RGBA16F（见文件头「噪点」）：线性存、不抖动、暗部没有色阶；Slate 采样后照常做伽马
		T->RenderTargetFormat = RTF_RGBA16f;
		T->ClearColor = FLinearColor(0.f, 0.f, 0.f, 1.f);
		T->InitAutoFormat(W, H);
		T->UpdateResourceImmediate(true);
		return T;
	}

	UPointLightComponent* MakeLight(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, const FVector& At, const FLinearColor& Color, float Candela, float SourceRadius)
	{
		UPointLightComponent* L = Owner->CreateDefaultSubobject<UPointLightComponent>(Name);
		L->SetupAttachment(Parent);
		L->SetRelativeLocation(At);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(Candela);
		L->SetLightColor(Color);
		L->SetAttenuationRadius(2500.f);
		L->SetSourceRadius(SourceRadius);  // 面积大一点，金属面上的高光是一片而不是一个点，读得出形体
		L->SetCastShadows(false);
		L->SetLightingChannels(false, true, false);
		return L;
	}

	/** 棚里的物件：只在场景捕获里看得见、只吃灯光通道 1、不进距离场 / 全局光照 */
	void StudioPrimitive(UPrimitiveComponent* P)
	{
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P->SetLightingChannels(false, true, false);
		P->bAffectDistanceFieldLighting = false;
		P->bAffectDynamicIndirectLighting = false;
		P->SetCastShadow(false);
		P->bVisibleInSceneCaptureOnly = true;
		P->bVisibleInRayTracing = false;
	}

	/** 镜头要退多远才能把整个盒子装进画面（盒子按 Center 量，Margin 留边） */
	float FitDistance(const FBox& Box, const FVector& Center, const FRotator& Rot, float Aspect, float Margin)
	{
		const FMatrix R = FRotationMatrix(Rot);
		const FVector Fw = R.GetScaledAxis(EAxis::X), Rt = R.GetScaledAxis(EAxis::Y), Up = R.GetScaledAxis(EAxis::Z);
		const float TanH = FMath::Tan(FMath::DegreesToRadians(FOV * 0.5f));
		const float TanV = TanH / FMath::Max(Aspect, 0.1f);
		float D = 10.f;
		for (int32 i = 0; i < 8; ++i)
		{
			const FVector C((i & 1) ? Box.Max.X : Box.Min.X, (i & 2) ? Box.Max.Y : Box.Min.Y, (i & 4) ? Box.Max.Z : Box.Min.Z);
			const FVector V = C - Center;
			const float X = FVector::DotProduct(V, Fw);
			D = FMath::Max(D, FMath::Abs(FVector::DotProduct(V, Rt)) * Margin / TanH - X);
			D = FMath::Max(D, FMath::Abs(FVector::DotProduct(V, Up)) * Margin / TanV - X);
		}
		return D;
	}
}

AMechItemStudio::AMechItemStudio()
{
	using namespace MechStudioPriv;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;  // 测试场的配装页是在暂停菜单里开的
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;  // 机体动画算完以后再拍
	SetReplicates(false);
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeAsset(TEXT("/Engine/BasicShapes/Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FrameMat(TEXT("/Game/Robot/Materials/MI_Robot_Frame"));

	// ---- 武器 / 无人机机身
	Item = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Item"));
	Item->SetupAttachment(Root);
	StudioPrimitive(Item);
	Item->ForcedLodModel = 1;  // 一直用最精细的一级（棚里不在主画面里，引擎按主画面算 LOD 会给最粗的）
	// 无人机的推进口和光点：0、1 给无人机本体，2、3 给挂在机体肩边那架
	for (int32 i = 0; i < 4; ++i)
	{
		UStaticMeshComponent* Pad = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("DronePad%d"), i));
		Pad->SetupAttachment(Root);
		StudioPrimitive(Pad);
		Pad->SetStaticMesh(CylinderAsset.Object);
		if (FrameMat.Succeeded())
		{
			Pad->SetMaterial(0, FrameMat.Object);
		}
		DronePads[i] = Pad;
		UStaticMeshComponent* Glow = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("DroneGlow%d"), i));
		Glow->SetupAttachment(Root);
		StudioPrimitive(Glow);
		Glow->SetStaticMesh(SphereAsset.Object);
		DroneGlows[i] = Glow;
	}

	// ---- 机体：MechRoot 相当于机体胶囊中心（整体缩进棚里），下面的网格和游戏里一样摆（往下 96×缩放、转 -90 度）
	MechRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MechRoot"));
	MechRoot->SetupAttachment(Root);
	MechRoot->SetRelativeScale3D(FVector(MechStudioScale));
	Mech = CreateDefaultSubobject<UMechMeshComponent>(TEXT("Mech"));
	Mech->SetupAttachment(MechRoot);
	StudioPrimitive(Mech);
	Mech->SetAnimInstanceClass(UMechAnimInstance::StaticClass());  // 没有机体角色时只放站立待机（FMechAnimProxy 里 bValid = false）
	Mech->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mech->bEnableUpdateRateOptimizations = false;
	Mech->bComponentUseFixedSkelBounds = true;
	Mech->SetForcedLOD(1);
	Mech->PrimaryComponentTick.bTickEvenWhenPaused = true;
	Mech->PrimaryComponentTick.bStartWithTickEnabled = false;  // 第一次拍机体时才开始算动画
	ShoulderDrone = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShoulderDrone"));
	ShoulderDrone->SetupAttachment(MechRoot);
	StudioPrimitive(ShoulderDrone);
	ShoulderDrone->ForcedLodModel = 1;
	Platform = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Platform"));
	Platform->SetupAttachment(MechRoot);
	StudioPrimitive(Platform);
	Platform->SetStaticMesh(CylinderAsset.Object);
	if (FrameMat.Succeeded())
	{
		Platform->SetMaterial(0, FrameMat.Object);
	}
	for (int32 k = 0; k < 2; ++k)
	{
		UStaticMeshComponent* Flame = CreateDefaultSubobject<UStaticMeshComponent>(k == 0 ? TEXT("FlameL") : TEXT("FlameR"));
		Flame->SetupAttachment(Mech);
		StudioPrimitive(Flame);
		Flame->SetStaticMesh(ConeAsset.Object);
		Flame->SetUsingAbsoluteScale(true);
		Flames[k] = Flame;
	}
	if (ConeAsset.Succeeded())
	{
		FlameBaseZ = ConeAsset.Object->GetBoundingBox().Min.Z;
	}
	StageLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StageLight"));
	StageLight->SetupAttachment(MechRoot);
	StageLight->SetRelativeLocation(FVector(-60.f, 0.f, 260.f));
	StageLight->SetIntensityUnits(ELightUnits::Candelas);
	StageLight->SetIntensity(420.f);  // 11-01 1600 → 420：展台和幕布同在灯光通道 2，太亮会把幕布打灰
	StageLight->SetLightColor(FLinearColor(0.85f, 0.9f, 1.f));
	StageLight->SetAttenuationRadius(900.f);
	StageLight->SetSourceRadius(80.f);
	StageLight->SetCastShadows(false);
	StageLight->SetLightingChannels(false, false, true);
	JetLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("JetLight"));
	JetLight->SetupAttachment(Mech);
	JetLight->SetIntensityUnits(ELightUnits::Candelas);
	JetLight->SetIntensity(0.f);
	JetLight->SetLightColor(FLinearColor(0.75f, 0.88f, 1.f));
	JetLight->SetAttenuationRadius(160.f);
	JetLight->SetCastShadows(false);
	JetLight->SetLightingChannels(false, true, false);

	// ---- 机位：镜头和灯一起绕物件转。主光（镜头左上）、补光（右下，冷一点）、镜头旁的大柔光（正对镜头的平面靠它反光）、
	// 轮廓光（物件背后两侧，勾出外形；11-01 从青色改成中性白：照片不跟菜单配色重拍，青色放到别的配色里不搭）、顶光。
	// 面积都放大（金属上的高光是一大片、读得出形体，不是一个过曝的亮点），亮度比以前低（以前亮面冲白）
	Rig = CreateDefaultSubobject<USceneComponent>(TEXT("Rig"));
	Rig->SetupAttachment(Root);
	Key = MakeLight(this, Rig, TEXT("Key"), FVector(-170.f, -150.f, 130.f), FLinearColor(1.f, 0.96f, 0.91f), 620.f, 90.f);
	Fill = MakeLight(this, Rig, TEXT("Fill"), FVector(-150.f, 190.f, -50.f), FLinearColor(0.78f, 0.85f, 1.f), 210.f, 110.f);
	Front = MakeLight(this, Rig, TEXT("Front"), FVector(-300.f, 30.f, 40.f), FLinearColor(0.96f, 0.97f, 1.f), 340.f, 150.f);
	Rim = MakeLight(this, Rig, TEXT("Rim"), FVector(170.f, 120.f, 110.f), FLinearColor(0.88f, 0.93f, 1.f), 900.f, 60.f);
	Top = MakeLight(this, Rig, TEXT("Top"), FVector(120.f, -130.f, 180.f), FLinearColor(0.92f, 0.95f, 1.f), 620.f, 70.f);

	// ---- 背景幕布：引擎的平面（100×100，法线 +Z），Pitch 90 让法线朝向镜头；大小和位置 Shoot 里按取景算
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneAsset(TEXT("/Engine/BasicShapes/Plane"));
	Backdrop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Backdrop"));
	Backdrop->SetupAttachment(Rig);
	StudioPrimitive(Backdrop);
	Backdrop->SetStaticMesh(PlaneAsset.Object);
	Backdrop->SetLightingChannels(false, false, true);
	BackLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BackLight"));
	BackLight->SetupAttachment(Rig);
	BackLight->SetIntensityUnits(ELightUnits::Candelas);
	BackLight->SetLightColor(FLinearColor(0.86f, 0.9f, 1.f));
	BackLight->SetCastShadows(false);
	BackLight->SetLightingChannels(false, false, true);

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Rig);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;  // 11-01 见文件头「噪点」
	Capture->ShowFlags.SetTemporalAA(false);  // 一次性捕获没有历史帧：退回 FXAA，配合超采样
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetCloud(false);
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetSkyLighting(false);  // 不吃地图的天光（白天的图发白、夜里的图发黑），只用棚里的灯
	// 10-31 转台改成每帧实时拍：棚里用不到的功能全关，省渲染线程每次捕获的准备工作（地图的太阳阴影、聚光灯、Lumen、距离场 AO……）。
	// 棚里的灯和物件本来就不投影、灯光通道和地图分开，关掉这些画面不变
	Capture->ShowFlags.SetDynamicShadows(false);
	Capture->ShowFlags.SetContactShadows(false);
	Capture->ShowFlags.SetCapsuleShadows(false);
	Capture->ShowFlags.SetDistanceFieldAO(false);
	Capture->ShowFlags.SetLumenGlobalIllumination(false);
	Capture->ShowFlags.SetLumenReflections(false);
	Capture->ShowFlags.SetScreenSpaceReflections(false);
	Capture->ShowFlags.SetSpotLights(false);
	Capture->ShowFlags.SetRectLights(false);
	Capture->ShowFlags.SetLightShafts(false);
	Capture->ShowFlags.SetDecals(false);
	Capture->ShowFlags.SetParticles(false);
	Capture->ShowFlags.SetLandscape(false);
	Capture->ShowFlags.SetInstancedFoliage(false);
	Capture->ShowFlags.SetInstancedGrass(false);
	// 曝光固定（不跟着黑背景自动调亮）、不用 Lumen（棚里只有自己的灯）
	FPostProcessSettings& PP = Capture->PostProcessSettings;
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = AEM_Manual;
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP.AutoExposureApplyPhysicalCameraExposure = false;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = -5.2f;  // 固定曝光约 1/37：武器发光条（强度 20 左右）不会冲成一片白，灯光按这个亮度配
	PP.bOverride_DynamicGlobalIlluminationMethod = true;
	PP.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	PP.bOverride_ReflectionMethod = true;
	PP.ReflectionMethod = EReflectionMethod::None;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = 0.f;
	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = 0.12f;
	// 环境立方体贴图：给金属一个可以反射的环境（不依赖地图的天光，白天夜里的图拍出来一样）。没有它金属面全黑
	static ConstructorHelpers::FObjectFinder<UTextureCube> AmbientCube(TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap"));
	if (AmbientCube.Succeeded())
	{
		PP.AmbientCubemap = AmbientCube.Object;
		PP.bOverride_AmbientCubemapIntensity = true;
		PP.AmbientCubemapIntensity = 0.85f;
		PP.bOverride_AmbientCubemapTint = true;
		PP.AmbientCubemapTint = FLinearColor(0.9f, 0.94f, 1.f);
	}
	Capture->PostProcessBlendWeight = 1.f;
}

void AMechItemStudio::BeginPlay()
{
	Super::BeginPlay();
	// 动态材质：推进器喷焰（充能步枪光束那个叠加材质，同 AMechCharacter）、无人机光点（EMP 电弧材质，同 UMechShoulderComponent）
	if (UMaterialInterface* Beam = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Weapons/Materials/M_MechBeam")))
	{
		FlameMID = UMaterialInstanceDynamic::Create(Beam, this);
		FlameMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.38f, 0.64f, 1.f));  // 比游戏里（0.55, 0.78, 1）偏蓝一点：棚里固定曝光，芯部容易冲白
		Flames[0]->SetMaterial(0, FlameMID);
		Flames[1]->SetMaterial(0, FlameMID);
	}
	if (UMaterialInterface* Arc = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Weapons/Materials/M_EmpArc")))
	{
		GlowMID = UMaterialInstanceDynamic::Create(Arc, this);
		GlowMID->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(0.35f, 0.75f, 1.f));
		GlowMID->SetScalarParameterValue(TEXT("Strength"), 0.2f);
		for (UStaticMeshComponent* G : DroneGlows)
		{
			G->SetMaterial(0, GlowMID);
		}
	}
	const float S = GetDefault<AMechCharacter>()->GetMesh()->GetRelativeScale3D().X;
	Mech->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -96.f * S), FRotator(0.f, -90.f, 0.f));
	Mech->SetRelativeScale3D(FVector(S));
	// 展台：脚下一块深色圆台（半径 1 米、厚 6 厘米，机体尺寸）。用引擎基本形状的纯色材质调成近黑：
	// 机体的金属 / 漆面 / 橡胶材质带细节纹理，在棚里的大面积灯下会反成一片带花纹的亮青色
	Platform->SetRelativeLocation(FVector(0.f, 0.f, -96.f * S - 3.f));
	Platform->SetRelativeScale3D(FVector(100.f / 50.f, 100.f / 50.f, 6.f / 100.f));
	if (UMaterialInterface* Plain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial")))
	{
		UMaterialInstanceDynamic* StageMID = UMaterialInstanceDynamic::Create(Plain, this);
		StageMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.05f, 0.055f, 0.06f));
		Platform->SetMaterial(0, StageMID);
		UMaterialInstanceDynamic* BackMID = UMaterialInstanceDynamic::Create(Plain, this);
		BackMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.045f, 0.048f, 0.054f));
		Backdrop->SetMaterial(0, BackMID);
	}
	// 展台和无人机的推进口只吃灯光通道 2 的一盏顶灯（机位那四盏灯很大很亮，照在平面上会反成一片青白）
	Platform->SetLightingChannels(false, false, true);
	for (UStaticMeshComponent* Pad : DronePads)
	{
		Pad->SetLightingChannels(false, false, true);
	}
	// 实时转台的分身棚：同一个类，放在旁边 200 米（灯互不照到），只给 ShootLive 用（10-31，替掉预拍 48 个角度）
	if (!bLiveTwin && GetWorld())
	{
		const FTransform At(GetActorRotation(), GetActorLocation() + MechStudioPriv::LiveTwinOffset);
		AMechItemStudio* Twin = GetWorld()->SpawnActorDeferred<AMechItemStudio>(AMechItemStudio::StaticClass(), At, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Twin)
		{
			Twin->bLiveTwin = true;
			Twin->FinishSpawning(At);
			LiveStudio = Twin;
		}
	}
}

void AMechItemStudio::EndPlay(const EEndPlayReason::Type Reason)
{
	if (LiveStudio)
	{
		LiveStudio->Destroy();
		LiveStudio = nullptr;
	}
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------- 目录
UStaticMesh* AMechItemStudio::MeshFor(int32 Category, int32 Index)
{
	if (Index < 0 || Index >= MechLoadout::NumItems(Category))
	{
		return nullptr;
	}
	const TCHAR* Path = nullptr;
	if (Category == MechLoadout::Shooter)
	{
		Path = MechWeapons::Gun(static_cast<EMechShooter>(Index)).Mesh;
	}
	else if (Category == MechLoadout::Melee)
	{
		Path = MechWeapons::Blade(static_cast<EMechMeleeWeapon>(Index)).Mesh;
	}
	else if (Category == MechLoadout::Shoulder)
	{
		// 和 AMechCharacter 构造里的 ShoulderMeshes 一致（浮空无人机的机身）。超出表就返回空：不能夹到别的模型，界面才会走 SMechItemIcon 图标兜底
		static const TCHAR* Shoulders[] = {TEXT("/Game/Weapons/Meshes/SM_MissilePod"), TEXT("/Game/Weapons/Meshes/SM_Artillery"),
			TEXT("/Game/Weapons/Meshes/SM_ShieldEmitter"), TEXT("/Game/Weapons/Meshes/SM_GrappleLauncher"), TEXT("/Game/Weapons/Meshes/SM_NetLauncher"),
			TEXT("/Game/Weapons/Meshes/SM_SmokeLauncher"), TEXT("/Game/Weapons/Meshes/SM_EmpLauncher"), TEXT("/Game/Weapons/Meshes/SM_BurstPack"),
			TEXT("/Game/Weapons/Meshes/SM_LaunchSpring"), TEXT("/Game/Weapons/Meshes/SM_ShockRing"), TEXT("/Game/Weapons/Meshes/SM_MineLayer")};
		Path = Index < int32(UE_ARRAY_COUNT(Shoulders)) ? Shoulders[Index] : nullptr;
	}
	return Path ? LoadObject<UStaticMesh>(nullptr, Path) : nullptr;
}

AMechItemStudio::ESubject AMechItemStudio::SubjectFor(int32 Category)
{
	switch (Category)
	{
	case MechLoadout::Shooter:
	case MechLoadout::Melee:
		return ESubject::Weapon;
	case MechLoadout::Shoulder:
		return ESubject::Drone;
	case MechLoadout::Frame:
	case MechLoadout::Booster:
		return ESubject::Mech;
	default:
		return ESubject::None;
	}
}

bool AMechItemStudio::HasPhoto(int32 Category, int32 Index)
{
	if (Index < 0 || Index >= MechLoadout::NumItems(Category))
	{
		return false;
	}
	switch (SubjectFor(Category))
	{
	case ESubject::Weapon:
	case ESubject::Drone:
		return MeshFor(Category, Index) != nullptr;
	case ESubject::Mech:
		return LoadObject<USkeletalMesh>(nullptr, MechCatalog::FrameMesh(Category == MechLoadout::Frame ? Index : 0)) != nullptr;
	default:
		return false;
	}
}

int32 AMechItemStudio::NumViews(int32 Category)
{
	return SubjectFor(Category) == ESubject::None ? 0 : MaxViews;
}

FText AMechItemStudio::ViewName(int32 Category, int32 View)
{
	switch (Category)
	{
	case MechLoadout::Shooter:
		return View == 0 ? INVTEXT("侧面") : View == 1 ? INVTEXT("斜后") : INVTEXT("枪口特写");
	case MechLoadout::Melee:
		return View == 0 ? INVTEXT("侧面") : View == 1 ? INVTEXT("刀身特写") : INVTEXT("握柄特写");
	case MechLoadout::Shoulder:
		return View == 0 ? INVTEXT("挂在肩边") : View == 1 ? INVTEXT("正面") : INVTEXT("侧面");
	case MechLoadout::Frame:
		return View == 0 ? INVTEXT("正面") : View == 1 ? INVTEXT("背面") : INVTEXT("侧面");
	case MechLoadout::Booster:
		return View == 0 ? INVTEXT("背面点火") : View == 1 ? INVTEXT("侧面点火") : INVTEXT("喷口特写");
	default:
		return FText::GetEmpty();
	}
}

AMechItemStudio::FSetup AMechItemStudio::SetupFor(int32 Category, int32 Index, int32 Slot) const
{
	using namespace MechStudioPriv;
	FSetup S;
	S.Subject = SubjectFor(Category);
	const bool bSpin = Slot == SpinSlot;
	const int32 View = Slot >= ViewSlot ? Slot - ViewSlot : -1;
	S.bSpin = bSpin;
	// 小图和转台角度 0 同一个方位（转台的方位再加界面上的角度，见 LiveShot）
	auto Base = [&S, bSpin](float Az, float El, float SpinEl) {
		S.Az = Az;
		S.El = bSpin ? SpinEl : El;
	};
	switch (Category)
	{
	case MechLoadout::Shooter:
	case MechLoadout::Melee:
	{
		Base(18.f, 8.f, 10.f);
		if (View == 0)
		{
			S.Az = 0.f, S.El = 0.f;  // 正侧面剪影
		}
		else if (View == 1 && Category == MechLoadout::Shooter)
		{
			S.Az = 140.f, S.El = 22.f;  // 斜后上方：机匣顶面、瞄具、枪托
		}
		else if (View == 2 && Category == MechLoadout::Shooter)
		{
			// 枪口特写：对准枪口往回一点（枪口在模型坐标里，原点在握把）
			S.bFocus = true;
			S.Focus = MechWeapons::Gun(static_cast<EMechShooter>(Index)).Muzzle * 0.86f;
			S.Az = 38.f, S.El = 12.f, S.Zoom = 2.6f;
		}
		else if (View == 1)
		{
			// 刀身特写：刀身沿模型 +X（原点在握把），对准刀身七成处
			const UStaticMesh* M = MeshFor(Category, Index);
			const FBox B = M ? M->GetBoundingBox() : FBox(FVector(-10.f), FVector(10.f));
			const float Tip = FMath::Abs(B.Max.X) >= FMath::Abs(B.Min.X) ? B.Max.X : B.Min.X;
			S.bFocus = true;
			S.Focus = FVector(Tip * 0.68f, B.GetCenter().Y, B.GetCenter().Z);
			S.Az = 24.f, S.El = 14.f, S.Zoom = 2.4f;
		}
		else if (View == 2)
		{
			// 握柄特写：对准握持点（MechWeapons 的 Grip，模型坐标），从另一侧看
			S.bFocus = true;
			S.Focus = MechWeapons::Blade(static_cast<EMechMeleeWeapon>(Index)).Grip;
			S.Az = -32.f, S.El = 10.f, S.Zoom = 2.8f;
		}
		break;
	}
	case MechLoadout::Shoulder:
		Base(145.f, 15.f, 14.f);  // 机身朝 +X：方位 145 = 镜头在前方偏左
		if (View == 0)
		{
			// 挂在机体左肩外：镜头在左后上方
			S.Subject = ESubject::Mech;
			S.DroneOnMech = Index;
			S.bUpperBody = true;
			S.Az = 40.f, S.El = 16.f;
		}
		else if (View == 1)
		{
			S.Az = 180.f, S.El = 4.f;
		}
		else if (View == 2)
		{
			S.Az = 90.f, S.El = 6.f;
		}
		break;
	case MechLoadout::Frame:
		S.bThumbs = true;
		Base(210.f, 6.f, 8.f);  // 机体朝 +X：方位 210 = 镜头在右前方（竖大拇指那只手）
		if (View == 0)
		{
			S.Az = 180.f, S.El = 3.f;
		}
		else if (View == 1)
		{
			S.Az = 0.f, S.El = 6.f;
		}
		else if (View == 2)
		{
			S.Az = 270.f, S.El = 3.f;
		}
		break;
	case MechLoadout::Booster:
		S.Flame = MechCatalog::BoosterSpec(static_cast<EMechBooster>(FMath::Clamp(Index, 0, 2))).FlameLength;
		Base(28.f, 12.f, 10.f);  // 背面偏左
		if (View == 0)
		{
			S.Az = 0.f, S.El = 14.f;
		}
		else if (View == 1)
		{
			S.Az = 90.f, S.El = 4.f;
		}
		else if (View == 2)
		{
			S.bFocus = true;  // 对准喷口（Shoot 里按骨头算）
			S.Az = 22.f, S.El = 20.f, S.Zoom = 3.f;
		}
		break;
	default:
		break;
	}
	return S;
}

// ---------------------------------------------------------------- 对外
UTextureRenderTarget2D* AMechItemStudio::GetThumb(int32 Category, int32 Index)
{
	using namespace MechStudioPriv;
	const int32 K = KeyOf(Category, Index);
	if (TObjectPtr<UTextureRenderTarget2D>* Found = Thumbs.Find(K))
	{
		return *Found;
	}
	if (!HasPhoto(Category, Index))
	{
		return nullptr;
	}
	UTextureRenderTarget2D* T = MakeTarget(this, ThumbW, ThumbH);
	Thumbs.Add(K, T);
	Enqueue(K, -1, 1);
	return T;
}

void AMechItemStudio::SetPreview(int32 Category, int32 Index)
{
	using namespace MechStudioPriv;
	const int32 K = KeyOf(Category, Index);
	if (K == PreviewKey)
	{
		return;
	}
	PreviewKey = K;
	// 换了一件：丢掉上一件还没拍的转台 / 视角，已拍的记录作废（贴图留着复用）
	Queue.RemoveAll([](const FJob& J) { return J.Slot >= 0; });
	for (auto It = Shot.CreateIterator(); It; ++It)
	{
		if (*It % 1000 != 0)  // 小图是 Slot -1 → 余数 0
		{
			It.RemoveCurrent();
		}
	}
	if (!HasPhoto(Category, Index))
	{
		return;
	}
	if (Views.Num() != MaxViews)
	{
		Views.Reset();
		for (int32 i = 0; i < MaxViews; ++i)
		{
			Views.Add(MakeTarget(this, ViewW, ViewH));
		}
	}
	GetThumb(Category, Index);
	// 转台不再预拍（实时拍，见 ShootLive），这里只排详细视角
	for (int32 v = 0; v < NumViews(Category); ++v)
	{
		Enqueue(K, ViewSlot + v, 0);
	}
}

UTextureRenderTarget2D* AMechItemStudio::ShootLive(int32 Category, int32 Index, float Angle, FIntPoint Pixels)
{
	if (bLiveTwin)
	{
		return LiveShot(Category, Index, Angle, Pixels) ? LiveTarget.Get() : nullptr;
	}
	if (!LiveStudio || !HasPhoto(Category, Index))
	{
		return nullptr;
	}
	return LiveStudio->ShootLive(Category, Index, Angle, Pixels);
}

bool AMechItemStudio::LiveShot(int32 Category, int32 Index, float Angle, FIntPoint Pixels)
{
	using namespace MechStudioPriv;
	RefreshPaint();
	const double T0 = FPlatformTime::Seconds();
	// 渲染目标跟着照片在屏幕上的像素大小（乘超采样，宽封顶 LiveMaxW，按比例缩高）；差几个像素不重建
	Pixels = FIntPoint(FMath::RoundToInt(Pixels.X * LiveSuperSample), FMath::RoundToInt(Pixels.Y * LiveSuperSample));
	if (Pixels.X > LiveMaxW)
	{
		Pixels.Y = FMath::RoundToInt(Pixels.Y * float(LiveMaxW) / Pixels.X);
		Pixels.X = LiveMaxW;
	}
	Pixels.X = FMath::Max(Pixels.X, 320);
	Pixels.Y = FMath::Max(Pixels.Y, 180);
	if (!LiveTarget)
	{
		LiveTarget = MakeTarget(this, Pixels.X, Pixels.Y);
	}
	else if (FMath::Abs(LiveTarget->SizeX - Pixels.X) > 8 || FMath::Abs(LiveTarget->SizeY - Pixels.Y) > 8)
	{
		LiveTarget->ResizeTarget(Pixels.X, Pixels.Y);
		LiveFrame = FLiveFrame();  // 画幅比例可能变了，重新取景
		bLiveValid = false;
	}
	const int32 K = KeyOf(Category, Index);
	if (K != LiveKey)
	{
		LiveKey = K;
		bLiveValid = false;
		LiveFrame = FLiveFrame();
		LiveResidentAt = 0.0;
	}
	FSetup S = SetupFor(Category, Index, SpinSlot);
	S.Az += Angle;
	if (S.Subject == ESubject::Mech)
	{
		MechIdleSince = T0;  // Tick 里机体闲 2 秒会停动画：实时拍期间一直算着
	}
	else if (bLiveValid && FMath::Abs(Angle - LiveAngle) < 0.01f)
	{
		return true;  // 武器 / 无人机不动，角度也没变（按住不拖、拖完还没开始自动转）：上一张就是对的，不重拍
	}
	LiveAngle = Angle;
	if (!Arrange(Category, Index, S))
	{
		return false;  // 机体刚换网格 / 姿势还没算好（2–4 帧）：这一帧不拍，界面先放小图
	}
	// 一直在拍：贴图强制常驻（Arrange 换物件时给了 30 秒）到期前续上，免得看久了掉成低精度
	if (T0 - LiveResidentAt > 20.0)
	{
		LiveResidentAt = T0;
		if (S.Subject == ESubject::Mech)
		{
			if (USkeletalMesh* SK = Mech->GetSkeletalMeshAsset())
			{
				SK->SetForceMipLevelsToBeResident(30.f);
			}
		}
		else if (UStaticMesh* SM = Item->GetStaticMesh())
		{
			SM->SetForceMipLevelsToBeResident(30.f);
		}
	}
	Shoot(S, LiveTarget, &LiveFrame);
	bLiveValid = true;
	// 耗时（游戏线程：摆物件 + 提交捕获；GPU 另算，见测试日志 [livephoto] 开 / 关对比）
	const double T1 = FPlatformTime::Seconds();
	LiveLastMs = float((T1 - T0) * 1000.0);
	if (LiveStatCount == 0)
	{
		LiveStatSince = T0;
	}
	LiveStatMs += LiveLastMs;
	LiveStatMax = FMath::Max(LiveStatMax, LiveLastMs);
	++LiveStatCount;
	if (T1 - LiveStatSince >= 5.0)
	{
		UE_LOG(LogTemp, Display, TEXT("[livephoto] 实时转台 %s：%.1f 秒拍了 %d 帧（%d×%d），每帧捕获调用平均 %.2f 毫秒、最长 %.2f 毫秒（游戏线程）"),
			*MechCatalog::FullName(Category, Index), T1 - LiveStatSince, LiveStatCount, LiveTarget->SizeX, LiveTarget->SizeY,
			LiveStatMs / LiveStatCount, LiveStatMax);
		LiveStatMs = 0.0;
		LiveStatMax = 0.f;
		LiveStatCount = 0;
	}
	return true;
}

float AMechItemStudio::GetLiveCallMs() const
{
	return bLiveTwin ? LiveLastMs : LiveStudio ? LiveStudio->LiveLastMs : 0.f;
}

UTextureRenderTarget2D* AMechItemStudio::GetView(int32 View) const
{
	using namespace MechStudioPriv;
	return PreviewKey >= 0 && Views.IsValidIndex(View) && Shot.Contains(ShotKey(PreviewKey, ViewSlot + View)) ? Views[View].Get() : nullptr;
}

UTextureRenderTarget2D* AMechItemStudio::GetPreview() const
{
	if (PreviewKey < 0)
	{
		return nullptr;
	}
	if (LiveStudio && LiveStudio->bLiveValid && LiveStudio->LiveKey == PreviewKey)
	{
		return LiveStudio->LiveTarget.Get();
	}
	const TObjectPtr<UTextureRenderTarget2D>* T = Thumbs.Find(PreviewKey);
	return T ? T->Get() : nullptr;
}

float AMechItemStudio::GetPreviewProgress() const
{
	using namespace MechStudioPriv;
	if (PreviewKey < 0)
	{
		return 0.f;
	}
	const int32 Views0 = NumViews(PreviewKey / 100);
	int32 Done = 0;
	auto Pending = [this](int32 Slot) { return Queue.ContainsByPredicate([this, Slot](const FJob& J) { return J.Key == PreviewKey && J.Slot == Slot; }); };
	for (int32 v = 0; v < Views0; ++v)
	{
		Done += Shot.Contains(ShotKey(PreviewKey, ViewSlot + v)) && !Pending(ViewSlot + v) ? 1 : 0;
	}
	const bool bThumb = Shot.Contains(ShotKey(PreviewKey, -1)) && !Pending(-1);
	const bool bLive = LiveStudio && LiveStudio->bLiveValid && LiveStudio->LiveKey == PreviewKey;
	return (Done + (bThumb ? 1 : 0) + (bLive ? 1 : 0)) / float(Views0 + 2);
}

void AMechItemStudio::ReleasePreview()
{
	Queue.RemoveAll([](const FJob& J) { return J.Slot >= 0; });
	for (auto It = Shot.CreateIterator(); It; ++It)
	{
		if (*It % 1000 != 0)
		{
			It.RemoveCurrent();
		}
	}
	Views.Reset();
	PreviewKey = -1;
	if (LiveStudio)
	{
		LiveStudio->ReleasePreview();
	}
	if (bLiveTwin)
	{
		// 分身：放掉实时转台的贴图；机体动画由 Tick 在闲 2 秒后停
		LiveTarget = nullptr;
		LiveKey = -1;
		bLiveValid = false;
		LiveFrame = FLiveFrame();
		LiveStatMs = 0.0;
		LiveStatMax = 0.f;
		LiveStatCount = 0;
	}
}

// ---------------------------------------------------------------- 排队
void AMechItemStudio::Enqueue(int32 InKey, int32 Slot, int32 Priority, double NotBefore)
{
	for (FJob& J : Queue)
	{
		if (J.Key == InKey && J.Slot == Slot)
		{
			J.Priority = FMath::Min(J.Priority, Priority);
			J.NotBefore = FMath::Max(J.NotBefore, NotBefore);
			return;
		}
	}
	FJob J;
	J.Key = InKey;
	J.Slot = Slot;
	J.Priority = Priority;
	J.NotBefore = NotBefore;
	// 按优先级插进去（同优先级先来先拍）
	int32 At = Queue.Num();
	while (At > 0 && Queue[At - 1].Priority > Priority)
	{
		--At;
	}
	Queue.Insert(J, At);
}

UTextureRenderTarget2D* AMechItemStudio::TargetFor(int32 InKey, int32 Slot) const
{
	using namespace MechStudioPriv;
	if (Slot < 0)
	{
		const TObjectPtr<UTextureRenderTarget2D>* T = Thumbs.Find(InKey);
		return T ? T->Get() : nullptr;
	}
	if (InKey != PreviewKey || Slot < ViewSlot)
	{
		return nullptr;
	}
	return Views.IsValidIndex(Slot - ViewSlot) ? Views[Slot - ViewSlot].Get() : nullptr;
}

void AMechItemStudio::Done(int32 InKey, int32 Slot, int32 Priority)
{
	using namespace MechStudioPriv;
	Shot.Add(ShotKey(InKey, Slot));
	// 这件的贴图是刚开始强制流送的：过一会儿补拍一次（补拍时已经过了时间，不会再补）
	const double Now = FPlatformTime::Seconds();
	const double* First = FirstSeen.Find(InKey);
	if (First && Now < *First + StreamDelay)
	{
		Enqueue(InKey, Slot, Priority + 10, *First + StreamDelay);
	}
}

void AMechItemStudio::Tick(float DeltaSeconds)
{
	using namespace MechStudioPriv;
	Super::Tick(DeltaSeconds);
	RefreshPaint();
	const double Now = FPlatformTime::Seconds();
	if (MechSettle > 0)
	{
		--MechSettle;
	}
	int32 Budget = ShotsPerFrame;
	bool bMechBlocked = false;
	bool bMechWanted = false;
	for (int32 i = 0; i < Queue.Num() && Budget > 0;)
	{
		const FJob J = Queue[i];
		if (J.NotBefore > Now)
		{
			++i;
			continue;
		}
		UTextureRenderTarget2D* T = TargetFor(J.Key, J.Slot);
		if (!T)
		{
			Queue.RemoveAt(i);
			continue;
		}
		const int32 Cat = J.Key / 100;
		const int32 Idx = J.Key % 100;
		const FSetup S = SetupFor(Cat, Idx, J.Slot);
		if (S.Subject == ESubject::Mech)
		{
			bMechWanted = true;
			// 机体换了网格 / 姿势要等动画算一次：这一帧别的机体照片都先不拍（免得来回切姿势），先拍武器
			if (bMechBlocked || !Arrange(Cat, Idx, S))
			{
				bMechBlocked = true;
				++i;
				continue;
			}
		}
		else if (!Arrange(Cat, Idx, S))
		{
			Queue.RemoveAt(i);
			continue;
		}
		Shoot(S, T);
		Queue.RemoveAt(i);
		Done(J.Key, J.Slot, J.Priority);
		--Budget;
	}
	// 机体闲了 2 秒就停掉动画（姿势留着，下次直接拍）
	if (bMechWanted)
	{
		MechIdleSince = Now;
	}
	else if (Mech->IsComponentTickEnabled() && Now - MechIdleSince > 2.0)
	{
		Mech->SetComponentTickEnabled(false);
	}
}

// ---------------------------------------------------------------- 摆物件
bool AMechItemStudio::Arrange(int32 Category, int32 Index, const FSetup& S)
{
	using namespace MechStudioPriv;
	const int32 K = KeyOf(Category, Index);
	if (!FirstSeen.Contains(K))
	{
		FirstSeen.Add(K, FPlatformTime::Seconds());
	}
	switch (S.Subject)
	{
	case ESubject::Weapon:
	{
		UStaticMesh* M = MeshFor(Category, Index);
		if (!M)
		{
			return false;
		}
		if (Current != ESubject::Weapon || ItemMeshFor != M)
		{
			ArrangeWeapon(M);
		}
		return true;
	}
	case ESubject::Drone:
	{
		UStaticMesh* M = MeshFor(Category, Index);
		if (!M)
		{
			return false;
		}
		if (Current != ESubject::Drone || ItemMeshFor != M)
		{
			ArrangeDrone(M);
		}
		return true;
	}
	case ESubject::Mech:
		return ArrangeMech(Category == MechLoadout::Frame ? Index : static_cast<int32>(EMechFrame::Medium), S);
	default:
		return false;
	}
}

void AMechItemStudio::ArrangeWeapon(UStaticMesh* Mesh)
{
	Current = ESubject::Weapon;
	ItemMeshFor = Mesh;
	// 先清上一件留下的涂装动态材质再换模型：反过来的话，无人机的动态材质会在 Nanite 枪上过一遍材质检查，
	// 打包版日志报「MI_Drone_*_Paint missing usage flag Nanite」（紧接着就清掉了，画面不受影响，但日志是假警报）
	Item->EmptyOverrideMaterials();
	Item->SetStaticMesh(Mesh);
	ApplyPaint(Item);  // 新武器的主色、副色和点缀色与玩家机体一致。
	Mesh->SetForceMipLevelsToBeResident(30.f);
	Item->PrestreamTextures(30.f, true);
	// 最长的轴横放（棚里 Y）、第二长的竖放（Z）、最短的朝镜头（X），中心放在原点
	const FBox Box = Mesh->GetBoundingBox();
	const FVector Ext = Box.GetExtent();
	int32 Axis[3] = {0, 1, 2};
	Algo::Sort(Axis, [&Ext](int32 A, int32 B) { return Ext[A] > Ext[B]; });
	FVector Image[3];
	Image[Axis[0]] = FVector::RightVector;
	Image[Axis[1]] = FVector::UpVector;
	Image[Axis[2]] = FVector::ForwardVector;
	FMatrix M(Image[0], Image[1], Image[2], FVector::ZeroVector);
	if (M.Determinant() < 0.f)
	{
		Image[Axis[2]] = -FVector::ForwardVector;
		M = FMatrix(Image[0], Image[1], Image[2], FVector::ZeroVector);
	}
	const FQuat Q = M.ToQuat();
	ItemXf = FTransform(Q, -Q.RotateVector(Box.GetCenter()), FVector::OneVector);
	Item->SetRelativeTransform(ItemXf);
	const FVector E(Ext[Axis[2]], Ext[Axis[0]], Ext[Axis[1]]);
	SubjectBox = FBox(-E, E);
}

void AMechItemStudio::ArrangeDrone(UStaticMesh* Mesh)
{
	Current = ESubject::Drone;
	ItemMeshFor = Mesh;
	Item->EmptyOverrideMaterials();
	Item->SetStaticMesh(Mesh);
	ApplyPaint(Item);  // 无人机机身和机体一起刷涂装（同 AMechCharacter::ApplyPaint）
	Mesh->SetForceMipLevelsToBeResident(30.f);
	Item->PrestreamTextures(30.f, true);
	// 和游戏里一样：机身统一到最长边约 58 厘米，朝 +X（发射方向），底下两个推进口
	const FBoxSphereBounds B = Mesh->GetBounds();
	const float DS = FMath::Clamp(58.f / FMath::Max(2.f * B.BoxExtent.GetMax(), 1.f), 0.7f, 1.35f);
	ItemXf = FTransform(FQuat::Identity, -B.Origin * DS, FVector(DS));
	Item->SetRelativeTransform(ItemXf);
	PlaceDronePads(Item, 0, DS);
	SubjectBox = FBox(-B.BoxExtent * DS - FVector(0.f, 0.f, 7.f), B.BoxExtent * DS);
}

void AMechItemStudio::PlaceDronePads(UStaticMeshComponent* Body, int32 Set, float DroneScale)
{
	const UStaticMesh* M = Body->GetStaticMesh();
	if (!M)
	{
		return;
	}
	const FBoxSphereBounds B = M->GetBounds();
	const float S = DroneScale;
	const float Side = FMath::Max(B.BoxExtent.Y * 0.6f, 9.f / S);
	const FVector Bottom(B.Origin.X - B.BoxExtent.X * 0.15f, B.Origin.Y, B.Origin.Z - B.BoxExtent.Z);
	for (int32 i = 0; i < 2; ++i)
	{
		UStaticMeshComponent* Pad = DronePads[Set * 2 + i];
		UStaticMeshComponent* Glow = DroneGlows[Set * 2 + i];
		if (Pad->GetAttachParent() != Body)
		{
			Pad->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform);
			Glow->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform);
		}
		const FVector At = Bottom + FVector(0.f, i == 0 ? -Side : Side, -1.5f / S);
		// 引擎圆柱半径 50、高 100：推进口半径 6.5 厘米、高 3 厘米；光点半径 3 厘米、压扁，嵌在推进口下面（同 UMechShoulderComponent::CacheDroneMesh）
		Pad->SetRelativeLocationAndRotation(At, FRotator::ZeroRotator);
		Pad->SetRelativeScale3D(FVector(6.5f / 50.f, 6.5f / 50.f, 3.f / 100.f) / S);
		Glow->SetRelativeLocationAndRotation(At - FVector(0.f, 0.f, 1.8f / S), FRotator::ZeroRotator);
		Glow->SetRelativeScale3D(FVector(3.f / 50.f, 3.f / 50.f, 1.f / 50.f) / S);
	}
}

bool AMechItemStudio::ArrangeMech(int32 FrameIndex, const FSetup& S)
{
	using namespace MechStudioPriv;
	USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, MechCatalog::FrameMesh(FrameIndex));
	if (!Asset)
	{
		return false;
	}
	Current = ESubject::Mech;
	if (!Mech->IsComponentTickEnabled())
	{
		Mech->SetComponentTickEnabled(true);
	}
	if (MechAssetFor != Asset)
	{
		MechAssetFor = Asset;
		Mech->SetSkeletalMeshAsset(Asset);
		Mech->EmptyOverrideMaterials();  // 10-31：九台的主漆槽底材质不一样（维克托是迷彩），上一台留下的动态材质不能沿用
		Mech->OnFrameMeshChanged();
		Asset->SetForceMipLevelsToBeResident(30.f);
		Mech->PrestreamTextures(30.f, true);
		ApplyPaint(Mech);
		MechSettle = 4;
		// 喷口：按绑定姿势算出在 spine_05 骨头坐标里的位置 / 方向（同 AMechCharacter::BeginPlay）
		bNozzles = false;
		const MechCatalog::FFrameBody& Body = MechCatalog::FrameBody(FrameIndex);
		const FVector NozzleMouthMesh = Body.NozzleMouth;
		const FVector NozzleDirMesh = Body.NozzleDir.GetSafeNormal();
		const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
		const int32 Bone = Ref.FindBoneIndex(ThrusterBone);
		if (Bone != INDEX_NONE)
		{
			FTransform BoneCS = FTransform::Identity;
			for (int32 i = Bone; i != INDEX_NONE; i = Ref.GetParentIndex(i))
			{
				BoneCS = BoneCS * Ref.GetRefBonePose()[i];
			}
			for (int32 k = 0; k < 2; ++k)
			{
				const float Side = k == 0 ? 1.f : -1.f;
				NozzleOnBone[k] = BoneCS.InverseTransformPosition(FVector(NozzleMouthMesh.X * Side, NozzleMouthMesh.Y, NozzleMouthMesh.Z));
				NozzleDirOnBone[k] = BoneCS.InverseTransformVectorNoScale(FVector(NozzleDirMesh.X * Side, NozzleDirMesh.Y, NozzleDirMesh.Z)).GetSafeNormal();
				Flames[k]->AttachToComponent(Mech, FAttachmentTransformRules::KeepRelativeTransform, ThrusterBone);
				Flames[k]->SetRelativeRotation(FRotationMatrix::MakeFromZ(NozzleDirOnBone[k]).Rotator());
			}
			const FVector MidDir = BoneCS.InverseTransformVectorNoScale(FVector(0.f, NozzleDirMesh.Y, NozzleDirMesh.Z)).GetSafeNormal();
			JetLight->AttachToComponent(Mech, FAttachmentTransformRules::KeepRelativeTransform, ThrusterBone);
			JetLight->SetRelativeLocation(BoneCS.InverseTransformPosition(FVector(0.f, NozzleMouthMesh.Y, NozzleMouthMesh.Z)) + MidDir * 30.f);
			bNozzles = true;
		}
	}
	if (bMechThumbs != S.bThumbs)
	{
		bMechThumbs = S.bThumbs;
		Mech->ThumbsUp = S.bThumbs ? 1.f : 0.f;
		MechSettle = FMath::Max(MechSettle, 2);
	}
	// 喷焰：底面缩进喷口 2 厘米，长度按推进器示意（同 AMechCharacter::UpdateThrusterFX，缩放按世界算）
	const float RobotScale = GetDefault<AMechCharacter>()->GetMesh()->GetRelativeScale3D().X;
	const float World = MechStudioScale;
	for (int32 k = 0; k < 2; ++k)
	{
		const bool bOn = S.Flame > 0.f && bNozzles;
		Flames[k]->SetVisibility(bOn);
		if (bOn)
		{
			const float Len = S.Flame;
			Flames[k]->SetRelativeScale3D(FVector(0.10f, 0.10f, Len) * World);
			Flames[k]->SetRelativeLocation(NozzleOnBone[k] + NozzleDirOnBone[k] * ((-FlameBaseZ * Len - 2.f) / RobotScale));
		}
	}
	if (FlameMID)
	{
		FlameMID->SetScalarParameterValue(TEXT("Intensity"), FlameIntensity * (0.4f + 0.6f * FMath::Min(S.Flame / 0.6f, 1.f)));
	}
	// 肩边的无人机
	if (S.DroneOnMech >= 0)
	{
		if (UStaticMesh* DM = MeshFor(MechLoadout::Shoulder, S.DroneOnMech))
		{
			if (ShoulderDrone->GetStaticMesh() != DM)
			{
				ShoulderDrone->SetStaticMesh(DM);
				ShoulderDrone->EmptyOverrideMaterials();
				ApplyPaint(ShoulderDrone);
				DM->SetForceMipLevelsToBeResident(30.f);
				ShoulderDrone->PrestreamTextures(30.f, true);
			}
			const FBoxSphereBounds B = DM->GetBounds();
			const float DS = FMath::Clamp(58.f / FMath::Max(2.f * B.BoxExtent.GetMax(), 1.f), 0.7f, 1.35f);
			ShoulderDrone->SetRelativeTransform(FTransform(FQuat::Identity, DroneOffset - B.Origin * DS, FVector(DS)));
			PlaceDronePads(ShoulderDrone, 1, DS);
		}
	}
	return MechSettle <= 0;
}

void AMechItemStudio::RefreshPaint()
{
	using namespace MechStudioPriv;
	const FMechPaint& SelectedPaint = FMechSettings::Get().Paint;
	if (bPaintCached && CachedPaintPrimary == SelectedPaint.Primary && CachedPaintSecondary == SelectedPaint.Secondary
		&& CachedPaintAccent == SelectedPaint.Accent)
	{
		return;
	}
	bPaintCached = true;
	CachedPaintPrimary = SelectedPaint.Primary;
	CachedPaintSecondary = SelectedPaint.Secondary;
	CachedPaintAccent = SelectedPaint.Accent;
	for (UMeshComponent* Painted : {static_cast<UMeshComponent*>(Item.Get()), static_cast<UMeshComponent*>(Mech.Get()),
		static_cast<UMeshComponent*>(ShoulderDrone.Get())})
	{
		if (Painted) ApplyPaint(Painted);
	}
	Shot.Reset();
	for (const auto& Thumb : Thumbs) Enqueue(Thumb.Key, -1, 1);
	if (PreviewKey >= 0)
	{
		for (int32 ViewIndex = 0; ViewIndex < NumViews(PreviewKey / 100); ++ViewIndex)
		{
			Enqueue(PreviewKey, ViewSlot + ViewIndex, 0);
		}
	}
	bLiveValid = false;
}

void AMechItemStudio::ApplyPaint(UMeshComponent* Target)
{
	// 用玩家自己的涂装（同 AMechCharacter::ApplyPaint；主菜单展示机体也是这样）
	const FMechPaint& Paint = FMechSettings::Get().Paint;
	MechWeaponGlow::Apply(Target, Paint.Accent);
	const TPair<const TCHAR*, FColor> Slots[] = {{TEXT("MI_Robot_Paint"), Paint.Primary}, {TEXT("MI_Robot_PaintDark"), Paint.Secondary},
		{TEXT("MI_Robot_Accent"), Paint.Accent}};
	for (const TPair<const TCHAR*, FColor>& S : Slots)
	{
		const int32 Index = Target->GetMaterialIndex(S.Key);
		if (Index == INDEX_NONE)
		{
			continue;
		}
		if (UMaterialInstanceDynamic* MID = Target->CreateAndSetMaterialInstanceDynamic(Index))
		{
			MID->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(S.Value));
			MID->SetVectorParameterValue(TEXT("CamoDark"), FLinearColor(Paint.Secondary));  // 维克托主漆槽的折线迷彩（别的材质没这个参数）
		}
	}
}

FBox AMechItemStudio::MechBox(bool bUpper) const
{
	FBox Box(ForceInit);
	const TArray<FTransform>& CS = Mech->GetComponentSpaceTransforms();
	if (CS.Num() == 0)
	{
		return FBox(FVector(-60.f, -60.f, -90.f), FVector(60.f, 60.f, 90.f));
	}
	const FTransform ToStudio = Mech->GetComponentTransform().GetRelativeTransform(GetActorTransform());
	const int32 Pelvis = Mech->GetBoneIndex(TEXT("pelvis"));
	const int32 Head = Mech->GetBoneIndex(TEXT("head"));
	float Cut = -BIG_NUMBER;
	if (bUpper && CS.IsValidIndex(Pelvis) && CS.IsValidIndex(Head))
	{
		const float Zp = ToStudio.TransformPosition(CS[Pelvis].GetLocation()).Z;
		const float Zh = ToStudio.TransformPosition(CS[Head].GetLocation()).Z;
		Cut = FMath::Lerp(Zp, Zh, 0.45f);  // 胸口往上
	}
	for (const FTransform& T : CS)
	{
		const FVector P = ToStudio.TransformPosition(T.GetLocation());
		if (P.Z >= Cut)
		{
			Box += P;
		}
	}
	// 骨头在装甲里面：按机体尺寸往外加一圈（四周 16、头顶 14 厘米，乘机体缩放和棚里比例）
	const float S = Mech->GetComponentScale().X;
	Box.Min -= FVector(16.f, 16.f, 0.f) * S;
	Box.Max += FVector(16.f, 16.f, 14.f) * S;
	if (!bUpper)
	{
		Box.Min.Z = FMath::Min(Box.Min.Z, Mech->GetRelativeLocation().Z * MechStudioScale - 6.f * MechStudioScale);  // 脚底（展台面）
	}
	return Box;
}

// ---------------------------------------------------------------- 拍
void AMechItemStudio::Shoot(const FSetup& S, UTextureRenderTarget2D* Target, FLiveFrame* Live)
{
	using namespace MechStudioPriv;
	FBox Box = SubjectBox;
	Capture->ShowOnlyComponents.Reset();
	if (S.Subject == ESubject::Mech)
	{
		Box = MechBox(S.bUpperBody);
		Capture->ShowOnlyComponent(Mech);
		if (!S.bUpperBody)
		{
			Capture->ShowOnlyComponent(Platform);
		}
		if (S.Flame > 0.f)
		{
			Capture->ShowOnlyComponent(Flames[0]);
			Capture->ShowOnlyComponent(Flames[1]);
		}
		if (S.DroneOnMech >= 0)
		{
			Capture->ShowOnlyComponent(ShoulderDrone);
			for (int32 i = 2; i < 4; ++i)
			{
				Capture->ShowOnlyComponent(DronePads[i]);
				Capture->ShowOnlyComponent(DroneGlows[i]);
			}
			// 取景把无人机也框进去
			const FTransform ToStudio = ShoulderDrone->GetComponentTransform().GetRelativeTransform(GetActorTransform());
			if (const UStaticMesh* DM = ShoulderDrone->GetStaticMesh())
			{
				Box += DM->GetBoundingBox().TransformBy(ToStudio);
			}
		}
	}
	else
	{
		Capture->ShowOnlyComponent(Item);
		if (S.Subject == ESubject::Drone)
		{
			for (int32 i = 0; i < 2; ++i)
			{
				Capture->ShowOnlyComponent(DronePads[i]);
				Capture->ShowOnlyComponent(DroneGlows[i]);
			}
		}
	}
	FVector Center = Box.GetCenter();
	if (S.bSpin && S.Subject == ESubject::Mech)
	{
		// 转台的机体绕脚下中心（胶囊轴线，棚里 X = Y = 0，展台也在这里）转，高度取包围盒中间。
		// 以前绕骨头包围盒的中心转：竖大拇指那只手把盒子拉偏十几厘米，转起来机体和展台在画面里左右晃（用户：「旋转很奇怪」）
		Center = FVector(0.f, 0.f, Center.Z);
	}
	if (Live && Live->bSet)
	{
		Center = Live->Focus;
	}
	FVector Focus = Center;
	if (S.bFocus)
	{
		if (S.Subject == ESubject::Mech)
		{
			// 推进器喷口特写：两个喷口中间、往背后挪一点
			const int32 B = Mech->GetBoneIndex(ThrusterBone);
			if (B != INDEX_NONE)
			{
				const FTransform BoneW = Mech->GetBoneTransform(B);
				const FVector Mouth = BoneW.TransformPosition((NozzleOnBone[0] + NozzleOnBone[1]) * 0.5f);
				Focus = GetActorTransform().InverseTransformPosition(Mouth) + FVector(-14.f, 0.f, -10.f) * MechStudioScale;
			}
		}
		else
		{
			Focus = ItemXf.TransformPosition(S.Focus);
		}
	}
	const float Aspect = Target->SizeX / float(FMath::Max(1, Target->SizeY));
	const float Margin = S.Subject == ESubject::Mech ? 1.03f : 1.04f;  // 11-01 武器 1.1 → 1.04：占满画面
	float Dist = 0.f;
	if (S.bSpin)
	{
		// 转台：取一圈里最远的距离，转起来大小不变
		for (float A = 0.f; A < 360.f; A += 30.f)
		{
			Dist = FMath::Max(Dist, FitDistance(Box, Center, FRotator(-S.El, A, 0.f), Aspect, Margin));
		}
		// 实时拍：同一件中心只定一次、距离只增不减（机体待机动画让骨头包围盒每帧微动，不固定的话镜头会跟着一伸一缩）
		if (Live)
		{
			if (!Live->bSet)
			{
				Live->bSet = true;
				Live->Focus = Center;
				Live->Dist = Dist;
			}
			Live->Dist = FMath::Max(Live->Dist, Dist);
			Dist = Live->Dist;
		}
	}
	else
	{
		Dist = FitDistance(Box, Center, FRotator(-S.El, S.Az, 0.f), Aspect, Margin);
	}
	Dist /= FMath::Max(S.Zoom, 0.1f);
	Rig->SetRelativeLocationAndRotation(Focus, FRotator(-S.El, S.Az, 0.f));
	Capture->SetRelativeLocationAndRotation(FVector(-Dist, 0.f, 0.f), FRotator::ZeroRotator);
	Capture->FOVAngle = FOV;
	Capture->TextureTarget = Target;
	// 幕布：放在物件后面（离对准点比物件最远的角再远一点），大到盖满画面；灯在幕布前方，离得越近四周暗得越快
	{
		const float Back = FMath::Max(Box.GetExtent().Size() * 1.3f, 50.f);
		const float TanH = FMath::Tan(FMath::DegreesToRadians(FOV * 0.5f));
		const float HalfW = (Dist + Back) * TanH;
		const float HalfH = HalfW / FMath::Max(Aspect, 0.1f);
		Backdrop->SetRelativeLocationAndRotation(FVector(Back, 0.f, 0.f), FRotator(90.f, 0.f, 0.f));
		Backdrop->SetRelativeScale3D(FVector(HalfW * 2.4f / 100.f, HalfW * 2.4f / 100.f, 1.f));
		Capture->ShowOnlyComponent(Backdrop);
		const float D = FMath::Max(HalfW * 0.82f, 30.f);
		BackLight->SetRelativeLocation(FVector(Back - D, -HalfW * 0.12f, HalfH * 0.22f));
		BackLight->SetAttenuationRadius(D * 4.f);
		BackLight->SetIntensity(BackCandelaPerSqM * FMath::Square(D * 0.01f));
	}
	// 喷口的冷光只在推进器照片里亮（它挂在机体背后，也在棚中央，别的照片里会照到武器）
	JetLight->SetIntensity(S.Subject == ESubject::Mech && S.Flame > 0.f ? JetCandela * S.Flame : 0.f);
	Capture->CaptureScene();
}

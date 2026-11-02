#include "MechMeleeFx.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechEffectComponent.h"
#include "MechHealthComponent.h"
#include "MechMeshComponent.h"
#include "MechWeapons.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace MechMeleeFxLocal
{
	template <typename T>
	T* Load(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<T> Finder(Path);
		return Finder.Object;
	}

	/** 每把刀的样子（顺序同 EMechMeleeWeapon）。颜色按「屏幕上的亮度」写（材质里乘曝光倒数，白天夜里一样亮）；
	 *  配色守美术方向「冷灰 + 一个亮色」：冷白 / 钢蓝 / 琥珀 / 熔橙，不用绿、紫（也避开纯青、纯红的队伍色） */
	struct FLook
	{
		FLinearColor Color;  // 刀光
		FLinearColor Core;   // 刀尖那条心线
		float BaseFrac;      // 刀光从刀身哪儿开始（0 护手 → 1 刀尖）
		float History;       // 刀光拖多久（秒）
		float Strength;
		float CoreWidth;     // 心线宽（占刀光宽的比例）
		float Body;          // 刀光面的亮度
		float Streak;        // 拉丝条纹
		FLinearColor Heat;   // 刀刃热光（黑 = 不发热：钝器）
		float HeatSwing;     // 挥刀时最热多少
		float ScarSize;      // 刀痕半长（厘米，轻击）
	};
	const FLook Looks[] = {
		// 双手刀：冷钢白，中等宽
		{FLinearColor(0.32f, 0.55f, 1.f), FLinearColor(1.6f, 1.75f, 2.f), 0.3f, 0.16f, 1.f, 0.07f, 0.3f, 0.85f, FLinearColor(0.55f, 0.75f, 1.f), 1.f, 30.f},
		// 快刀：淡白、短（连斩层数越多越橙越热，见 FlurryLerp）
		{FLinearColor(0.62f, 0.72f, 0.95f), FLinearColor(1.7f, 1.8f, 2.f), 0.35f, 0.11f, 0.85f, 0.06f, 0.3f, 0.85f, FLinearColor(1.f, 0.55f, 0.2f), 0.7f, 24.f},
		// 单手刀：冷白、细、利落（闪避斩拖得很长）
		{FLinearColor(0.55f, 0.75f, 1.f), FLinearColor(2.f, 2.1f, 2.3f), 0.35f, 0.12f, 0.9f, 0.05f, 0.3f, 0.85f, FLinearColor(0.6f, 0.8f, 1.f), 0.9f, 27.f},
		// 大剑：熔橙、宽、拖得长
		{FLinearColor(1.f, 0.4f, 0.1f), FLinearColor(2.2f, 1.4f, 0.65f), 0.15f, 0.22f, 1.05f, 0.09f, 0.6f, 0.7f, FLinearColor(1.f, 0.42f, 0.1f), 1.6f, 40.f},
		// 双刀：琥珀、细、短
		{FLinearColor(1.f, 0.68f, 0.22f), FLinearColor(2.f, 1.65f, 0.95f), 0.3f, 0.1f, 0.8f, 0.07f, 0.38f, 0.85f, FLinearColor(1.f, 0.6f, 0.2f), 0.8f, 20.f},
		// 钝器：不发光的刀刃，只有一团灰白的气压拖影（砸中靠冲击环、尘土）
		{FLinearColor(0.42f, 0.45f, 0.5f), FLinearColor(0.f, 0.f, 0.f), 0.55f, 0.13f, 0.55f, 0.1f, 1.f, 0.9f, FLinearColor::Black, 0.f, 16.f},
		// 匕首（右键近身挥刀）：冷白、细、短
		{FLinearColor(0.6f, 0.75f, 1.f), FLinearColor(1.7f, 1.8f, 2.f), 0.2f, 0.09f, 0.8f, 0.08f, 0.3f, 0.85f, FLinearColor(0.6f, 0.8f, 1.f), 0.6f, 16.f},
	};
	static_assert(UE_ARRAY_COUNT(Looks) == static_cast<int32>(EMechMeleeWeapon::Dagger) + 1, "近战特效表要和枚举一致");

	/** 第二版刀光（11-01 用户：「目前的刀光总体是过于朴素」）每把刀的样子，顺序同 EMechMeleeWeapon。颜色按「屏幕上的亮度」写。
	 *  一道刀光 = 刚扫过那一截白热（Hot）→ 刀的颜色（Color）→ 快散时变深（Deep），刀尖一条心线（Core），刀尖外侧一圈软辉光（Glow），
	 *  光面上一条条拉丝（Lines 条，LineMix 对比）往刀走的方向流（Flow），尾巴碎成块、碎口带亮边（Breakup）；刀尖沿路洒火花，重斩留残影。
	 *  1 ≈ 屏幕上满白：带颜色的部分最亮的通道别超过 1 左右，不然白天全糊成白的（第一版试过 Hot 2–3，七把刀全是白条）；只有心线、前沿可以过白 */
	struct FArc
	{
		FLinearColor Hot;
		FLinearColor Color;
		FLinearColor Deep;
		FLinearColor Core;  // 黑 = 没有心线（钝器）
		float BaseFrac;     // 刀光从刀身哪儿开始（0 护手 → 1 刀尖）
		float History;      // 拖多久（秒）
		float Glow;         // 刀尖外侧辉光宽（厘米）
		float Bright;       // 整体亮度
		float CoreWidth;    // 心线宽（占刀光宽的比例）
		float Body;         // 光面亮度
		float Lines;        // 拉丝条数
		float LineMix;      // 拉丝对比（0 平的一片 → 1 一条条分开）
		float Flow;         // 纹路往刀的方向流多快
		float Breakup;      // 尾巴碎成块（0 整片一起淡 → 1 碎块 + 亮边）
		float Lead;         // 刚扫过那一截加亮多少
		float LeadSharp;    // 加亮那一截多窄（越大越窄、越利落）
		float HotSpan;      // 白热保持多久（0–1）
		float Ripple;       // 横向的气压波纹（钝器）
		float RippleFreq;   // 波纹每米几道
		float Shade;        // 把刀光底下压暗多少（白天亮地面上颜色才出得来）
		float Sparks;       // 刀尖每走 1 米洒几颗火花 / 光点
		float SparkSpeed;   // 厘米 / 秒
		float SparkGravity; // 厘米 / 秒²（负 = 往下掉）
		float SparkLife;    // 秒
		float SparkStretch; // 按速度拉长（秒）
		float SparkSize;    // 粗细（厘米）
		float SparkTwinkle; // 闪烁（冷光的光点闪，热的火花不闪）
		FLinearColor SparkColor;  // 0–1（亮度在 M_BladeSpark 里乘）
		int32 Ghosts;       // 重斩 / 闪避斩同时留几个残影（0 = 不留）
	};
	const FArc Arcs[] = {
		// 双手刀：利落——细而亮的白心线，窄的钢蓝光面，前沿很锐，尾巴干脆地碎掉；刀尖洒几点冷光，重斩留 3 个残影
		{FLinearColor(1.5f, 1.65f, 1.9f), FLinearColor(0.25f, 0.5f, 1.1f), FLinearColor(0.04f, 0.1f, 0.38f), FLinearColor(2.2f, 2.4f, 2.8f),
			0.3f, 0.15f, 16.f, 1.f, 0.045f, 0.6f, 9.f, 0.75f, 2.5f, 0.9f, 1.8f, 14.f, 0.2f, 0.f, 0.f, 0.35f,
			7.f, 260.f, -150.f, 0.35f, 0.025f, 1.6f, 1.f, FLinearColor(0.6f, 0.8f, 1.f), 3},
		// 快刀：密而细——二十条细拉丝流得快，短；光点多而小（连斩层数越多越橙、火花越多，见 FlurryArc）
		{FLinearColor(1.4f, 1.5f, 1.7f), FLinearColor(0.42f, 0.52f, 0.9f), FLinearColor(0.1f, 0.12f, 0.3f), FLinearColor(2.f, 2.1f, 2.4f),
			0.35f, 0.12f, 10.f, 0.95f, 0.03f, 0.55f, 20.f, 0.95f, 4.5f, 0.8f, 1.4f, 18.f, 0.15f, 0.f, 0.f, 0.3f,
			9.f, 300.f, -100.f, 0.25f, 0.02f, 1.1f, 1.f, FLinearColor(0.7f, 0.78f, 1.f), 0},
		// 单手刀：突进感——前沿最亮最锐、纹路流得最快；闪避斩拖成一长条、光点拉成速度线、留 4 个残影
		{FLinearColor(1.6f, 1.75f, 2.f), FLinearColor(0.35f, 0.6f, 1.05f), FLinearColor(0.06f, 0.14f, 0.38f), FLinearColor(2.4f, 2.5f, 2.8f),
			0.35f, 0.12f, 14.f, 1.f, 0.04f, 0.55f, 12.f, 0.85f, 7.f, 0.75f, 2.2f, 16.f, 0.25f, 0.f, 0.f, 0.35f,
			6.f, 220.f, 0.f, 0.3f, 0.035f, 1.4f, 0.5f, FLinearColor(0.6f, 0.8f, 1.f), 4},
		// 大剑：厚重——熔橙、宽、拖得长，几道粗拉丝，尾巴碎成烧红的碎片；甩出一串往下掉的火星，重斩留 4 个残影
		{FLinearColor(1.8f, 1.1f, 0.45f), FLinearColor(1.1f, 0.32f, 0.05f), FLinearColor(0.45f, 0.05f, 0.01f), FLinearColor(2.4f, 1.6f, 0.7f),
			0.12f, 0.24f, 40.f, 1.05f, 0.07f, 0.8f, 6.f, 0.6f, 2.f, 1.f, 1.3f, 8.f, 0.35f, 0.f, 0.f, 0.45f,
			14.f, 380.f, -900.f, 0.6f, 0.03f, 2.6f, 0.f, FLinearColor(1.f, 0.42f, 0.08f), 4},
		// 双刀：琥珀、细、短，两把各一道——左手那把偏红（Tick 里乘），交错时分得开；火星往下掉。
		// 双刀的招多是直来直去的一划，扫出来是平行四边形：前沿不加亮、心线暗一点，不然四边都亮像一块块玻璃框
		{FLinearColor(1.7f, 1.3f, 0.6f), FLinearColor(1.f, 0.55f, 0.12f), FLinearColor(0.4f, 0.12f, 0.02f), FLinearColor(1.5f, 1.15f, 0.55f),
			0.3f, 0.13f, 12.f, 0.95f, 0.04f, 0.75f, 10.f, 0.7f, 3.5f, 0.85f, 0.5f, 6.f, 0.25f, 0.f, 0.f, 0.35f,
			7.f, 280.f, -600.f, 0.35f, 0.025f, 1.5f, 0.f, FLinearColor(1.f, 0.62f, 0.18f), 0},
		// 钝器：冲击不是切割——没有心线、不发热，一团灰白的气压（底下压得暗，像一股气浪），前沿很宽，一道道横向的气压波纹往前推；
		// 不洒火花，重斩留 3 个残影
		{FLinearColor(1.f, 1.02f, 1.08f), FLinearColor(0.32f, 0.35f, 0.42f), FLinearColor(0.08f, 0.09f, 0.12f), FLinearColor(0.f, 0.f, 0.f),
			0.55f, 0.14f, 32.f, 0.75f, 0.1f, 0.85f, 4.f, 0.35f, 1.5f, 0.55f, 2.6f, 6.f, 0.1f, 1.f, 3.5f, 0.6f,
			0.f, 0.f, 0.f, 0.3f, 0.f, 1.f, 0.f, FLinearColor::White, 3},
		// 匕首（右键近身挥刀）：一道细线——光面很淡，心线粗一点，几点冷光
		{FLinearColor(1.5f, 1.65f, 1.9f), FLinearColor(0.35f, 0.5f, 0.95f), FLinearColor(0.05f, 0.1f, 0.3f), FLinearColor(2.2f, 2.3f, 2.6f),
			0.2f, 0.09f, 6.f, 0.9f, 0.12f, 0.25f, 6.f, 0.5f, 4.f, 0.6f, 1.4f, 16.f, 0.15f, 0.f, 0.f, 0.2f,
			3.f, 200.f, 0.f, 0.25f, 0.02f, 1.f, 1.f, FLinearColor(0.7f, 0.85f, 1.f), 0},
	};
	static_assert(UE_ARRAY_COUNT(Arcs) == static_cast<int32>(EMechMeleeWeapon::Dagger) + 1, "第二版刀光表要和枚举一致");
	/** 弹反后那一刀、快刀连斩满层：第二版刀光换的颜色 */
	struct FArcTint
	{
		FLinearColor Hot;
		FLinearColor Color;
		FLinearColor Deep;
		FLinearColor Core;
		FLinearColor Spark;
	};
	const FArcTint ParryArc = {FLinearColor(2.f, 1.6f, 0.8f), FLinearColor(1.1f, 0.72f, 0.2f), FLinearColor(0.45f, 0.2f, 0.03f),
		FLinearColor(2.4f, 2.f, 1.1f), FLinearColor(1.f, 0.8f, 0.35f)};
	const FArcTint FlurryArc = {FLinearColor(1.8f, 1.2f, 0.5f), FLinearColor(1.05f, 0.36f, 0.08f), FLinearColor(0.4f, 0.08f, 0.01f),
		FLinearColor(2.2f, 1.5f, 0.7f), FLinearColor(1.f, 0.5f, 0.12f)};

	// 双手刀弹反后那一刀：金色
	const FLinearColor ParryColor(1.f, 0.75f, 0.3f);
	const FLinearColor ParryCore(2.4f, 2.f, 1.2f);
	// 快刀连斩满层的样子
	const FLinearColor FlurryColor(1.f, 0.5f, 0.15f);
	const FLinearColor FlurryCore(2.3f, 1.55f, 0.8f);

	/** 身体各段（和 AMechCharacter::ClassifyHit 同一套，半径是模板人形尺寸、乘机体缩放）：刀痕挂在离刀最近的那段上 */
	struct FSeg
	{
		const TCHAR* A;
		const TCHAR* B;  // 空 = 头（从头骨往上一段）
		float Radius;
	};
	const FSeg Segs[] = {
		{TEXT("pelvis"), TEXT("spine_03"), 28.f}, {TEXT("spine_03"), TEXT("spine_05"), 30.f}, {TEXT("spine_05"), TEXT("neck_01"), 11.f},
		{TEXT("head"), nullptr, 15.f},
		{TEXT("upperarm_l"), TEXT("lowerarm_l"), 9.f}, {TEXT("lowerarm_l"), TEXT("hand_l"), 8.f},
		{TEXT("upperarm_r"), TEXT("lowerarm_r"), 9.f}, {TEXT("lowerarm_r"), TEXT("hand_r"), 8.f},
		{TEXT("thigh_l"), TEXT("calf_l"), 13.f}, {TEXT("calf_l"), TEXT("foot_l"), 11.f},
		{TEXT("thigh_r"), TEXT("calf_r"), 13.f}, {TEXT("calf_r"), TEXT("foot_r"), 11.f},
	};

	constexpr int32 MaxScars = 8;
	constexpr int32 MaxRendScars = 5;  // = 割裂最多 5 层
	constexpr int32 MaxSubdiv = 6;
	/** 第二版刀光补点更密：低帧率时双刀转圈重斩一帧走一两米，6 个点补出来是一圈多边形框 */
	constexpr int32 MaxSubdivArc = 16;
	constexpr float SubdivStepArc = 8.f;
	/** 一台机体同时最多几颗火花、几个残影 */
	constexpr int32 MaxSparks = 160;
	constexpr int32 MaxGhosts = 6;
	/** 残影多久留一个（秒） */
	constexpr float GhostEvery = 0.03f;
	/** 刀尖移动多远插一个点（低帧率时挥一刀只有几帧，按曲线补点，不然刀光是几块折面） */
	constexpr float SubdivStep = 10.f;

	FVector CatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float T)
	{
		const float T2 = T * T;
		const float T3 = T2 * T;
		return 0.5f * (2.f * P1 + (P2 - P0) * T + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2 + (3.f * P1 - P0 - 3.f * P2 + P3) * T3);
	}
}

using namespace MechMeleeFxLocal;

UMechMeleeFx::UMechMeleeFx()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 刀的位置在骨骼动画算完以后才摆好（UMechMeshComponent::FinalizeBoneTransform），刀光要取这一帧的最终位置
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	ArcMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_BladeArc"));
	TrailMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_BladeTrail"));
	SparkMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_BladeSpark"));
	GhostMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_BladeGhost"));
	ScarMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_SlashScar"));
	RingMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_ShockRing"));
	SphereMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere"));
	PlaneMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Plane"));
	SparkFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"));
	DustFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_00/Particle/Ex_02_Projectile/Projectile_Exp_01"));
}

AMechCharacter* UMechMeleeFx::GetMech() const
{
	return Cast<AMechCharacter>(GetOwner());
}

void UMechMeleeFx::BeginPlay()
{
	Super::BeginPlay();
	AActor* Owner = GetOwner();
	// -MechTrailOld：第一版刀光（对比用）；第二版的材质没建好时也退回第一版
	bTrailOld = FParse::Param(FCommandLine::Get(), TEXT("MechTrailOld")) || !TrailMaterial;
	UMaterialInterface* TrailMat = bTrailOld ? ArcMaterial.Get() : TrailMaterial.Get();
	for (int32 i = 0; i < 2; ++i)
	{
		UProceduralMeshComponent* M = NewObject<UProceduralMeshComponent>(Owner, i == 0 ? TEXT("BladeArcR") : TEXT("BladeArcL"));
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(false);
		M->bAffectDistanceFieldLighting = false;
		M->bAffectDynamicIndirectLighting = false;
		M->SetUsingAbsoluteLocation(true);
		M->SetUsingAbsoluteRotation(true);
		M->SetUsingAbsoluteScale(true);
		M->SetupAttachment(Owner->GetRootComponent());
		M->RegisterComponent();
		M->SetWorldTransform(FTransform::Identity);  // 顶点直接是世界坐标
		Trails[i].Mesh = M;
		if (TrailMat)
		{
			Trails[i].MID = UMaterialInstanceDynamic::Create(TrailMat, this);
			M->SetMaterial(0, Trails[i].MID);
		}
	}
	// 火花 / 光点：一个程序网格，每颗一个面片（顶点直接是世界坐标）
	if (!bTrailOld && SparkMaterial)
	{
		UProceduralMeshComponent* M = NewObject<UProceduralMeshComponent>(Owner, TEXT("BladeSparks"));
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(false);
		M->bAffectDistanceFieldLighting = false;
		M->bAffectDynamicIndirectLighting = false;
		M->SetUsingAbsoluteLocation(true);
		M->SetUsingAbsoluteRotation(true);
		M->SetUsingAbsoluteScale(true);
		M->SetupAttachment(Owner->GetRootComponent());
		M->RegisterComponent();
		M->SetWorldTransform(FTransform::Identity);
		SparkMesh = M;
		SparkMID = UMaterialInstanceDynamic::Create(SparkMaterial, this);
		SparkMID->SetVectorParameterValue(TEXT("Prm"), FLinearColor(3.f, 0.35f, 0.f, 0.f));  // 亮度倍数、火花头的白热（太亮颜色就没了，全是白点）
		M->SetMaterial(0, SparkMID);
	}
}

void UMechMeleeFx::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearScars();
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------- 出招 / 命中 / 格挡（各端的组播里调）
void UMechMeleeFx::OnSwingStart(EMechMelee Move, bool bDash, bool bParryBonus)
{
	const AMechCharacter* Mech = GetMech();
	const UMechCombatComponent* Combat = Mech ? Mech->GetCombat() : nullptr;
	if (!Combat)
	{
		return;
	}
	const FLook& L = Looks[static_cast<int32>(Combat->GetBladeType())];
	const FArc& A = Arcs[static_cast<int32>(Combat->GetBladeType())];
	const bool bHeavy = Move == EMechMelee::Heavy;
	SwingMove = Move;
	SwingSeed = static_cast<uint8>(FMath::RandRange(0, 255));
	GhostClock = 0.f;
	bSwingDash = bDash;
	bSwingParry = bParryBonus;
	SwingClock = FxClock;
	if (bParryBonus)
	{
		ParryReadyUntil = -100.f;  // 加成这一刀用掉了
	}
	// 重斩宽一点、拖得长一点、亮一点；第四段比前三段亮一点；闪避斩一路冲过去，刀光拖成一长条
	History = (bTrailOld ? L.History : A.History) * (bHeavy ? 1.35f : 1.f) * (bDash ? 3.2f : 1.f);
	SwingStrength = (bHeavy ? 1.35f : Move == EMechMelee::Light4 ? 1.15f : 1.f) * (bDash ? 1.5f : 1.f) * (bParryBonus ? 1.4f : 1.f);
	for (FTrail& T : Trails)
	{
		T.bHasLast = false;  // 新的一刀另起一段，不和上一刀还没散的尾巴连起来
		T.Path = 0.f;
	}
}

void UMechMeleeFx::OnBladeHit(AMechCharacter* Victim, const FVector& At, float Heavy01, bool bHeavy)
{
	AMechCharacter* Mech = GetMech();
	const UMechCombatComponent* Combat = Mech ? Mech->GetCombat() : nullptr;
	if (!Combat || !Victim)
	{
		return;
	}
	const EMechMeleeWeapon W = Combat->GetBladeType();
	const FLook& L = Looks[static_cast<int32>(W)];
	const FVector Toward = (Victim->GetActorLocation() - Mech->GetActorLocation()).GetSafeNormal();
	// 刀痕：刀身这一帧的位置（两点）找离它最近的那段身体；刀走的方向 = 刀尖速度方向
	FVector A = At;
	FVector B = At + Toward * 50.f;
	if (const FTrail& T = Trails[0]; T.Samples.Num())
	{
		A = T.Samples.Last().Base;
		B = T.Samples.Last().Tip;
	}
	const FVector Cut = SwingDir.IsNearlyZero() ? Mech->GetActorRightVector() : SwingDir;
	if (UMechCombatComponent* VC = Victim->GetCombat(); VC && VC->GetMeleeFx())
	{
		const float Size = L.ScarSize * (bHeavy ? 1.5f : 1.f + 0.25f * Heavy01);
		// 钝器不留刀口，砸中那一点烧红一小块
		VC->GetMeleeFx()->AddScar(A, B, Cut, Mech->GetActorLocation(), Size, W == EMechMeleeWeapon::Blunt, W == EMechMeleeWeapon::TwinBlades);
	}
	// 冲击环：重击、钝器每一下、第四段（小）；朝着砍的方向扩开，贴在被砍那台面前
	const FVector RingAt = Victim->GetActorLocation() - Toward * 70.f + FVector(0.f, 0.f, 50.f);
	const bool bBlunt = W == EMechMeleeWeapon::Blunt;
	const FLinearColor RingColor = bBlunt ? FLinearColor(0.75f, 0.8f, 0.85f) : (L.Heat.IsAlmostBlack() ? L.Color : L.Heat);
	if (bHeavy)
	{
		const float R = W == EMechMeleeWeapon::GreatSword ? 230.f : bBlunt ? 270.f : 170.f;
		AddRing(RingAt, Toward, R, 0.07f, 1.3f, RingColor, 0.32f, false);
	}
	else if (bBlunt)
	{
		AddRing(RingAt, Toward, 150.f + 60.f * Heavy01, 0.06f, 0.75f, RingColor, 0.24f, false);
	}
	else if (SwingMove == EMechMelee::Light4)
	{
		AddRing(RingAt, Toward, 110.f, 0.05f, 0.55f, RingColor, 0.2f, false);
	}
	// 大剑、钝器的重击：被砍那台脚下扬起一圈尘土 + 地面一圈很淡的环
	if (bHeavy && (bBlunt || W == EMechMeleeWeapon::GreatSword))
	{
		FHitResult Ground;
		const FVector From = Victim->GetActorLocation();
		FCollisionQueryParams Q(SCENE_QUERY_STAT(MechMeleeDust), false, Victim);
		Q.AddIgnoredActor(Mech);
		if (GetWorld()->LineTraceSingleByChannel(Ground, From, From - FVector(0.f, 0.f, 400.f), ECC_Visibility, Q))
		{
			if (DustFX)
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, DustFX, Ground.ImpactPoint, FRotator::ZeroRotator, FVector(bBlunt ? 0.9f : 0.7f), true, true,
					ENCPoolMethod::AutoRelease);
			}
			AddRing(Ground.ImpactPoint + Ground.ImpactNormal * 4.f, Ground.ImpactNormal, bBlunt ? 340.f : 280.f, 0.05f, 0.5f, FLinearColor(0.8f, 0.75f, 0.68f), 0.4f, false);
		}
	}
}

void UMechMeleeFx::OnBlocked(const FVector& Spark, int32 Kind, AMechCharacter* By)
{
	if (!By)
	{
		// 子弹打在格挡上：一点很小的反光
		AddRing(Spark, FVector::ZeroVector, 45.f, 0.f, 0.6f, FLinearColor(0.75f, 0.85f, 1.f), 0.1f, true);
		return;
	}
	const FVector Toward = (By->GetActorLocation() - GetOwner()->GetActorLocation()).GetSafeNormal();
	if (Kind == 1)
	{
		// 弹反：一道横向的金色反光 + 一圈金环；挡的一方刀刃亮金光（下一刀加成还在），被弹反的一方身上冒电火花（僵直）
		AddRing(Spark, FVector::ZeroVector, 240.f, 0.f, 2.f, FLinearColor(1.f, 0.85f, 0.55f), 0.24f, true);
		AddRing(Spark, Toward, 150.f, 0.06f, 1.1f, FLinearColor(1.f, 0.8f, 0.45f), 0.26f, false);
	}
	else if (Kind == 2)
	{
		// 破防：红橙色的大冲击环 + 一道红光
		AddRing(Spark, Toward, 210.f, 0.08f, 1.4f, FLinearColor(1.f, 0.35f, 0.12f), 0.3f, false);
		AddRing(Spark, FVector::ZeroVector, 150.f, 0.f, 1.2f, FLinearColor(1.f, 0.45f, 0.2f), 0.18f, true);
	}
	else
	{
		// 挡住：冷白的一点反光
		AddRing(Spark, FVector::ZeroVector, 110.f, 0.f, 1.1f, FLinearColor(0.75f, 0.85f, 1.f), 0.14f, true);
	}
}

void UMechMeleeFx::OnClash(const FVector& At, const FVector& Axis)
{
	// 拼刀：比弹反（240）还大的一道正对镜头的反光，钢白偏暖（弹反金、破防橙红、挡住冷白都不一样）；沿两台连线一大一小两圈冲击环
	AddRing(At, FVector::ZeroVector, 340.f, 0.f, 2.6f, FLinearColor(1.f, 0.93f, 0.82f), 0.26f, true);
	AddRing(At, Axis, 280.f, 0.08f, 1.5f, FLinearColor(1.f, 0.82f, 0.6f), 0.3f, false);
	AddRing(At, Axis, 150.f, 0.05f, 1.f, FLinearColor(0.9f, 0.95f, 1.f), 0.2f, false);
	if (!SparkMesh)
	{
		return;
	}
	// 一大蓬火星：两把刀撞在一起是在连线的垂直面上崩开的，所以主要朝四周甩、少量顺着连线，往上偏一点再被重力拉下来
	const FVector Side = FVector::CrossProduct(Axis, FVector::UpVector).GetSafeNormal();
	const FVector Up = FVector::CrossProduct(Side, Axis).GetSafeNormal();
	static const FColor Colors[3] = {FColor(255, 240, 215), FColor(255, 205, 140), FColor(255, 150, 70)};
	const int32 Count = FMath::Min(130, MaxSparks - Sparks.Num());
	for (int32 k = 0; k < Count; ++k)
	{
		const float Ang = FMath::FRand() * 6.2832f;
		const FVector Dir = (Side * FMath::Cos(Ang) + Up * (FMath::Sin(Ang) + 0.35f) + Axis * FMath::FRandRange(-0.35f, 0.35f)).GetSafeNormal();
		FSpark New;
		New.P = At + FMath::VRand() * 18.f;
		New.V = Dir * FMath::Lerp(800.f, 4200.f, FMath::Square(FMath::FRand()));
		New.Born = FxClock;
		New.Life = FMath::FRandRange(0.22f, 0.7f);
		New.Size = FMath::FRandRange(2.4f, 5.5f);
		New.Stretch = 0.028f;
		New.Gravity = -1100.f;
		New.Drag = 3.f;
		New.Color = Colors[FMath::RandHelper(3)];
		Sparks.Add(New);
	}
}

bool UMechMeleeFx::GetBladeNow(FVector& OutBase, FVector& OutTip) const
{
	if (Trails[0].Samples.IsEmpty())
	{
		return false;
	}
	OutBase = Trails[0].Samples.Last().Base;
	OutTip = Trails[0].Samples.Last().Tip;
	return true;
}

void UMechMeleeFx::OnKnifeHit(AMechCharacter* Victim, const FVector& At, const FVector& Dir, bool bPower)
{
	if (!Victim)
	{
		return;
	}
	if (UMechCombatComponent* VC = Victim->GetCombat(); VC && VC->GetMeleeFx())
	{
		VC->GetMeleeFx()->AddScar(At - Dir * 30.f, At + Dir * 30.f, Dir, GetOwner()->GetActorLocation(), bPower ? 16.f : 10.f, true, false);
	}
	if (bPower)
	{
		AddRing(At - Dir * 20.f, -Dir, 130.f, 0.06f, 0.9f, FLinearColor(1.f, 0.5f, 0.15f), 0.22f, false);
	}
}

// ---------------------------------------------------------------- 每帧
void UMechMeleeFx::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	static const bool bTimeLog = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeFxLog"));
	const double TickStart = bTimeLog ? FPlatformTime::Seconds() : 0.0;
	FxClock += DeltaTime;
	AMechCharacter* Mech = GetMech();
	UMechCombatComponent* Combat = Mech ? Mech->GetCombat() : nullptr;
	if (!Combat)
	{
		return;
	}
	if (Mech->GetHealth()->IsDead())
	{
		ClearScars();
	}
	const EMechMeleeWeapon W = Combat->GetBladeType();
	const FLook& L = Looks[static_cast<int32>(W)];
	const bool bActive = Combat->IsMeleeActive();
	const bool bHeavy = Combat->GetMeleeMove() == EMechMelee::Heavy;
	UMechMeshComponent* MM = Mech->GetMechMesh();
	USceneComponent* Right = MM ? MM->Props[0].Get() : nullptr;
	USceneComponent* Left = MM && Combat->GetBlade().bTwin ? MM->Props[4].Get() : nullptr;

	// 这一刀的颜色：快刀按连斩层数往橙色走，弹反后那一刀金色
	const int32 Flurry = W == EMechMeleeWeapon::QuickBlade && Mech->GetEffects() ? FMath::Min(Mech->GetEffects()->GetStacks(TEXT("Flurry")), MechWeapons::FlurryMax) : 0;
	const float FlurryLerp = Flurry / static_cast<float>(FMath::Max(MechWeapons::FlurryMax, 1));
	if (bTrailOld)
	{
		FLinearColor Color = FMath::Lerp(L.Color, FlurryColor, FlurryLerp);
		FLinearColor Core = FMath::Lerp(L.Core, FlurryCore, FlurryLerp);
		const float Strength = L.Strength * SwingStrength * (1.f + 0.35f * FlurryLerp);
		if (bSwingParry)
		{
			Color = ParryColor;
			Core = ParryCore;
		}
		const float CoreW = L.CoreWidth * (bHeavy ? 1.3f : 1.f) * (bSwingDash ? 1.3f : 1.f);
		for (FTrail& T : Trails)
		{
			if (T.MID)
			{
				T.MID->SetVectorParameterValue(TEXT("Color"), Color);
				T.MID->SetVectorParameterValue(TEXT("CoreColor"), Core);
				T.MID->SetVectorParameterValue(TEXT("Prm"), FLinearColor(Strength, CoreW, L.Body, L.Streak));
			}
		}
	}
	else if (bActive || Trails[0].Samples.Num() > 0 || Trails[1].Samples.Num() > 0)
	{
		// 第二版刀光：按刀、这一刀的招式（出招时记下的，刀光散完之前不变）、快刀连斩层数、弹反加成
		const FArc& A = Arcs[static_cast<int32>(W)];
		const bool bSwingHeavy = SwingMove == EMechMelee::Heavy;
		FArcTint Tint{FMath::Lerp(A.Hot, FlurryArc.Hot, FlurryLerp), FMath::Lerp(A.Color, FlurryArc.Color, FlurryLerp), FMath::Lerp(A.Deep, FlurryArc.Deep, FlurryLerp),
			FMath::Lerp(A.Core, FlurryArc.Core, FlurryLerp), FMath::Lerp(A.SparkColor, FlurryArc.Spark, FlurryLerp)};
		if (bSwingParry)
		{
			Tint = ParryArc;
		}
		const float Bright = A.Bright * SwingStrength * (1.f + 0.35f * FlurryLerp);
		const float CoreW = A.CoreWidth * (bSwingHeavy ? 1.3f : 1.f) * (bSwingDash ? 1.3f : 1.f);
		TrailGlow = A.Glow * (bSwingHeavy ? 1.5f : 1.f) * (bSwingDash ? 1.3f : 1.f);
		TrailSparkMul = 1.4f * (bSwingHeavy ? 2.2f : 1.f) * (bSwingDash ? 2.5f : 1.f) * (bSwingParry ? 1.8f : 1.f) * (1.f + 2.f * FlurryLerp);
		TrailSparkColor = Tint.Spark;
		TrailGhostColor = FMath::Lerp(Tint.Color, Tint.Hot, 0.3f);
		const FLinearColor Prm(Bright, CoreW, A.Body, A.LineMix);
		const FLinearColor Prm2(FMath::Fmod(FxClock, 512.f), A.Lines, A.Flow * (bSwingDash ? 2.f : 1.f), A.Breakup);
		const FLinearColor Prm3(0.75f * (bSwingHeavy ? 1.3f : 1.f), A.Lead * (bSwingHeavy ? 1.2f : 1.f), A.LeadSharp, A.HotSpan);
		const FLinearColor Prm4(A.Ripple, A.RippleFreq, A.Shade, 0.f);
		for (int32 i = 0; i < 2; ++i)
		{
			UMaterialInstanceDynamic* MID = Trails[i].MID;
			if (!MID)
			{
				continue;
			}
			// 双刀左手那把偏红一点：两道刀光交错时分得开
			const FLinearColor Side = i == 1 ? FLinearColor(1.f, 0.62f, 0.6f, 1.f) : FLinearColor::White;
			MID->SetVectorParameterValue(TEXT("Hot"), Tint.Hot);
			MID->SetVectorParameterValue(TEXT("Color"), Tint.Color * Side);
			MID->SetVectorParameterValue(TEXT("Deep"), Tint.Deep * Side);
			MID->SetVectorParameterValue(TEXT("CoreColor"), Tint.Core);
			MID->SetVectorParameterValue(TEXT("Prm"), Prm);
			MID->SetVectorParameterValue(TEXT("Prm2"), Prm2);
			MID->SetVectorParameterValue(TEXT("Prm3"), Prm3);
			MID->SetVectorParameterValue(TEXT("Prm4"), Prm4);
		}
	}
	UpdateTrail(0, Right, bActive, DeltaTime);
	UpdateTrail(1, Left, bActive && Left != nullptr, DeltaTime);

	// 刀刃热光：出招前段慢慢烧热（重斩更热——对面看得出来要来重的），砍下时最热，之后很快冷下去；
	// 平时：快刀连斩层数越多越热，双手刀弹反后那几秒刀刃金光
	float Heat = 0.f;
	FLinearColor HeatColor = FMath::Lerp(L.Heat, FLinearColor(1.f, 0.5f, 0.15f), FlurryLerp);
	if (!L.Heat.IsAlmostBlack())
	{
		if (bActive)
		{
			const float StrikeAt = FMath::Max(Combat->GetMeleeStrikeAt(), 0.05f);
			const float Time = Combat->GetMeleeTime();
			const float Peak = L.HeatSwing * (bHeavy ? 1.8f : 1.f) * (bSwingDash ? 1.4f : 1.f) * (bSwingParry ? 1.5f : 1.f);
			Heat = Time < StrikeAt ? Peak * FMath::Square(Time / StrikeAt) : Peak * FMath::Exp(-(Time - StrikeAt) / 0.22f);
		}
		Heat = FMath::Max(Heat, 0.22f * Flurry);
		if (FxClock < ParryReadyUntil && W == EMechMeleeWeapon::Katana)
		{
			Heat = FMath::Max(Heat, 1.3f * (0.8f + 0.2f * FMath::Sin(FxClock * 8.f)));
			HeatColor = ParryColor;
		}
		if (bSwingParry && bActive)
		{
			HeatColor = ParryColor;
		}
	}
	UpdateHeat(Trails[0], Right, Heat, HeatColor);
	UpdateHeat(Trails[1], Left, Heat, HeatColor);

	// 大剑霸体重斩：出招过程中全身一层橙色的热光（不吃失衡、不被打断，对面要看得出来）
	if (Combat->IsSuperArmor())
	{
		Mech->FlashBody(FLinearColor(1.f, 0.4f, 0.1f), 0.22f + 0.08f * FMath::Sin(FxClock * 18.f));
	}
	// 被弹反僵直：身上冒电火花、一闪一闪
	if (FxClock < StunFxUntil)
	{
		StunSparkClock -= DeltaTime;
		if (StunSparkClock <= 0.f)
		{
			StunSparkClock = 0.11f;
			if (SparkFX && MM)
			{
				static const FName Bones[] = {TEXT("spine_03"), TEXT("spine_05"), TEXT("upperarm_l"), TEXT("upperarm_r"), TEXT("head"), TEXT("pelvis")};
				const FVector P = MM->GetBoneLocation(Bones[FMath::RandHelper(UE_ARRAY_COUNT(Bones))]) + FMath::VRand() * 18.f;
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SparkFX, P, FMath::VRand().Rotation(), FVector(0.45f), true, true, ENCPoolMethod::AutoRelease);
			}
			Mech->FlashBody(FLinearColor(0.55f, 0.75f, 1.f), 0.3f);
		}
	}
	UpdateScars(DeltaTime);
	UpdateRings();
	UpdateSparks(DeltaTime);
	UpdateGhosts();
	if (bTimeLog)
	{
		FxTickSeconds += FPlatformTime::Seconds() - TickStart;
		++FxTickFrames;
		if (FxClock - FxLogClock > 5.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[meleefx] %s 每帧平均 %.3f 毫秒（%d 帧，%s 刀光）火花 %d 残影 %d 刀光点 %d+%d"), *GetOwner()->GetName(),
				FxTickSeconds * 1000.0 / FMath::Max(FxTickFrames, 1), FxTickFrames, bTrailOld ? TEXT("第一版") : TEXT("第二版"), Sparks.Num(), Ghosts.Num(),
				Trails[0].Samples.Num(), Trails[1].Samples.Num());
			FxTickSeconds = 0.0;
			FxTickFrames = 0;
			FxLogClock = FxClock;
		}
	}
}

// ---------------------------------------------------------------- 刀光
void UMechMeleeFx::BladeRange(const USceneComponent* Blade, float& OutStart, float& OutTip) const
{
	const UMechCombatComponent* Combat = GetMech()->GetCombat();
	OutStart = Combat->GetBlade().GripRange.Y + 3.f;  // 护手往前一点
	OutTip = OutStart + 100.f;
	if (const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(Blade); SM && SM->GetStaticMesh())
	{
		OutTip = SM->GetStaticMesh()->GetBoundingBox().Max.X;
	}
	OutStart = FMath::Min(OutStart, OutTip - 10.f);
}

void UMechMeleeFx::UpdateTrail(int32 Index, USceneComponent* Blade, bool bOn, float DeltaTime)
{
	FTrail& T = Trails[Index];
	if (!T.Mesh)
	{
		return;
	}
	AMechCharacter* Mech = GetMech();
	if (bOn && Blade && Blade->IsVisible())
	{
		float X0 = 0.f;
		float X1 = 100.f;
		BladeRange(Blade, X0, X1);
		const int32 W = static_cast<int32>(Mech->GetCombat()->GetBladeType());
		const FLook& L = Looks[W];
		const FArc& A = Arcs[W];
		const FTransform X = Blade->GetComponentTransform();
		const FVector Base = X.TransformPosition(FVector(FMath::Lerp(X0, X1, bTrailOld ? L.BaseFrac : A.BaseFrac), 0.f, 0.f));
		const FVector Tip = X.TransformPosition(FVector(X1, 0.f, 0.f));
		// 亮度看刀尖相对机体的速度（突进本身不算挥刀；闪避斩例外，一路冲过去就该拖一长条）
		const FVector Body = Mech->GetVelocity() * (bSwingDash ? 0.f : 1.f);
		float Speed = 0.f;
		FVector TipVel = FVector::ZeroVector;
		if (T.bHasLast && DeltaTime > 1e-5f)
		{
			const FVector Moved = Tip - T.LastTip;
			TipVel = Moved / DeltaTime;
			Speed = (TipVel - Body).Size();
			if (Index == 0 && Moved.SizeSquared() > 0.25f)
			{
				SwingDir = Moved.GetSafeNormal();
			}
			T.Path += Moved.Size() / 100.f;
		}
		const float S = FMath::SmoothStep(350.f, 2000.f, Speed);
		const float C = (bTrailOld ? L.Core : A.Core).IsAlmostBlack() ? 0.f : 1.f;
		FTrailSample New{Base, Tip, FxClock, S, C, T.Path, SwingSeed};
		const int32 N = T.Samples.Num();
		if (T.bHasLast && N >= 1)
		{
			// 低帧率、刀快的时候两帧之间刀尖跳很远：按曲线补点，刀光是一道弧而不是几块折面
			const FTrailSample& P1 = T.Samples[N - 1];
			const FTrailSample& P0 = N >= 2 ? T.Samples[N - 2] : P1;
			const int32 Steps = bTrailOld ? FMath::Clamp(FMath::CeilToInt(FVector::Dist(P1.Tip, Tip) / SubdivStep), 1, MaxSubdiv)
										  : FMath::Clamp(FMath::CeilToInt(FVector::Dist(P1.Tip, Tip) / SubdivStepArc), 1, MaxSubdivArc);
			const FVector B3 = Base + (Base - P1.Base);
			const FVector T3 = Tip + (Tip - P1.Tip);
			const FTrailSample A0 = P0;
			const FTrailSample A1 = P1;
			for (int32 k = 1; k < Steps; ++k)
			{
				const float F = k / static_cast<float>(Steps);
				T.Samples.Add({CatmullRom(A0.Base, A1.Base, Base, B3, F), CatmullRom(A0.Tip, A1.Tip, Tip, T3, F), FMath::Lerp(A1.Time, FxClock, F),
					FMath::Lerp(A1.Strength, S, F), C, FMath::Lerp(A1.Path, T.Path, F), SwingSeed});
			}
			if (!bTrailOld)
			{
				// 火花 / 光点：按刀尖这一帧走过的路撒（刀越快越多）；重斩、闪避斩每 0.03 秒留一个刀身残影
				EmitSparks(A1.Base, A1.Tip, Base, Tip, TipVel, (T.Path - A1.Path) * A.Sparks * TrailSparkMul * S);
				if (Index == 0 && A.Ghosts > 0 && (SwingMove == EMechMelee::Heavy || bSwingDash) && S > 0.25f)
				{
					GhostClock -= DeltaTime;
					if (GhostClock <= 0.f)
					{
						GhostClock = GhostEvery;
						AddGhost(Blade, TrailGhostColor, S, GhostEvery * A.Ghosts * 1.4f);
					}
				}
			}
		}
		else if (N >= 1)
		{
			// 另起一段：先放一个强度为零的点，和上一段之间那两个三角形是全黑的（叠加模式下看不见）
			FTrailSample Gap = New;
			Gap.Strength = 0.f;
			T.Samples.Add(Gap);
		}
		T.Samples.Add(New);
		T.LastTip = Tip;
		T.bHasLast = true;
	}
	else
	{
		T.bHasLast = false;
	}
	// 过期的点去掉（最老那个留着，带子尾巴才是渐渐淡掉的）
	while (T.Samples.Num() > 1 && FxClock - T.Samples[1].Time > History)
	{
		T.Samples.RemoveAt(0);
	}
	if (T.Samples.Num() == 1 && FxClock - T.Samples[0].Time > History)
	{
		T.Samples.Reset();
	}
	BuildTrail(T);
}

void UMechMeleeFx::BuildTrail(FTrail& T)
{
	const int32 N = T.Samples.Num();
	if (N < 2)
	{
		if (T.Mesh->GetNumSections() > 0)
		{
			T.Mesh->ClearAllMeshSections();
		}
		return;
	}
	const float InvHistory = 1.f / FMath::Max(History, 0.01f);
	if (bTrailOld)
	{
		// 第一版：每个点两个顶点（刀根、刀尖）
		TArray<FVector> Verts;
		TArray<int32> Tris;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		Verts.Reserve(N * 2);
		UVs.Reserve(N * 2);
		Colors.Reserve(N * 2);
		Tris.Reserve((N - 1) * 6);
		for (int32 i = 0; i < N; ++i)
		{
			const FTrailSample& S = T.Samples[i];
			const float Age = FMath::Clamp((FxClock - S.Time) * InvHistory, 0.f, 1.f);
			Verts.Add(S.Base);
			Verts.Add(S.Tip);
			UVs.Add(FVector2D(Age, 0.f));
			UVs.Add(FVector2D(Age, 1.f));
			const FLinearColor C(S.Strength, S.Core, 0.f, 1.f);
			Colors.Add(C);
			Colors.Add(C);
			if (i > 0)
			{
				const int32 A = (i - 1) * 2;
				Tris.Append({A, A + 1, A + 2, A + 1, A + 3, A + 2});
			}
		}
		T.Mesh->CreateMeshSection_LinearColor(0, Verts, Tris, TArray<FVector>(), UVs, Colors, TArray<FProcMeshTangent>(), false);
		return;
	}
	// 第二版：每个点三个顶点——刀根（v 0）、刀尖（v 1）、刀尖往外 TrailGlow 厘米（v 2，外侧辉光）；
	// UV1.x = 刀尖走过的路（纹路、碎块贴在路上）；顶点色 R 强度、G 心线、B 这一刀的随机数
	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector2D> UV0;
	TArray<FVector2D> UV1;
	TArray<FColor> Colors;
	Verts.Reserve(N * 3);
	UV0.Reserve(N * 3);
	UV1.Reserve(N * 3);
	Colors.Reserve(N * 3);
	Tris.Reserve((N - 1) * 12);
	for (int32 i = 0; i < N; ++i)
	{
		const FTrailSample& S = T.Samples[i];
		const float Age = FMath::Clamp((FxClock - S.Time) * InvHistory, 0.f, 1.f);
		const FVector Out = S.Tip + (S.Tip - S.Base).GetSafeNormal() * TrailGlow;
		Verts.Append({S.Base, S.Tip, Out});
		UV0.Append({FVector2D(Age, 0.f), FVector2D(Age, 1.f), FVector2D(Age, 2.f)});
		const FVector2D Path(S.Path, 0.f);
		UV1.Append({Path, Path, Path});
		const FColor C(static_cast<uint8>(FMath::Clamp(S.Strength, 0.f, 1.f) * 255.f + 0.5f), static_cast<uint8>(S.Core > 0.5f ? 255 : 0), S.Seed, 255);
		Colors.Append({C, C, C});
		if (i > 0)
		{
			const int32 A = (i - 1) * 3;
			Tris.Append({A, A + 1, A + 3, A + 1, A + 4, A + 3, A + 1, A + 2, A + 4, A + 2, A + 5, A + 4});
		}
	}
	T.Mesh->CreateMeshSection(0, Verts, Tris, TArray<FVector>(), UV0, UV1, TArray<FVector2D>(), TArray<FVector2D>(), Colors, TArray<FProcMeshTangent>(), false);
}

void UMechMeleeFx::UpdateHeat(FTrail& T, USceneComponent* Blade, float Heat, const FLinearColor& Color)
{
	UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(Blade);
	if (!SM)
	{
		return;
	}
	float X0 = 0.f;
	float X1 = 100.f;
	BladeRange(Blade, X0, X1);
	const FVector4f Want(Heat, X0, GetMech()->GetCombat()->GetBlade().EdgeZ, X1);
	if (Want == T.LastHeat && Color == T.LastHeatColor)
	{
		return;
	}
	T.LastHeat = Want;
	T.LastHeatColor = Color;
	// 机体给武器上队伍色时已经把 M_WeaponPBR 的材质换成了动态实例（AMechCharacter::ApplyTeamColor），这里只改参数
	for (int32 Slot = 0; Slot < SM->GetNumMaterials(); ++Slot)
	{
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(SM->GetMaterial(Slot));
		if (MID && MID->GetBaseMaterial() && MID->GetBaseMaterial()->GetName() == TEXT("M_WeaponPBR"))
		{
			MID->SetVectorParameterValue(TEXT("HeatPrm"), FLinearColor(Want.X, Want.Y, Want.Z, Want.W));
			MID->SetVectorParameterValue(TEXT("HeatColor"), Color);
		}
	}
}

// ---------------------------------------------------------------- 火花 / 光点
void UMechMeleeFx::EmitSparks(const FVector& FromBase, const FVector& FromTip, const FVector& ToBase, const FVector& ToTip, const FVector& TipVel, float Count)
{
	if (!SparkMesh || Count <= 0.f)
	{
		return;
	}
	const FArc& A = Arcs[static_cast<int32>(GetMech()->GetCombat()->GetBladeType())];
	SparkDebt += Count;
	const int32 Num = FMath::FloorToInt(SparkDebt);
	SparkDebt -= Num;
	const FColor Col = TrailSparkColor.ToFColor(false);
	const float SizeMul = SwingMove == EMechMelee::Heavy ? 1.25f : 1.f;
	for (int32 k = 0; k < Num; ++k)
	{
		if (Sparks.Num() >= MaxSparks)
		{
			SparkDebt = 0.f;
			break;
		}
		// 这一帧刀扫过的那片面上随便一点，多半靠近刀尖；往刀尖外侧、四周甩出去，带一点刀的速度
		const float R = FMath::FRand();
		const FVector TipP = FMath::Lerp(FromTip, ToTip, R);
		const FVector BaseP = FMath::Lerp(FromBase, ToBase, R);
		const FVector Out = (TipP - BaseP).GetSafeNormal();
		FSpark New;
		New.P = FMath::Lerp(TipP, BaseP, FMath::Square(FMath::FRand()) * 0.45f);
		New.V = TipVel * 0.2f + (Out * 0.7f + FMath::VRand()).GetSafeNormal() * A.SparkSpeed * FMath::FRandRange(0.35f, 1.f);
		New.Born = FxClock;
		New.Life = A.SparkLife * FMath::FRandRange(0.6f, 1.25f);
		New.Size = A.SparkSize * FMath::FRandRange(0.6f, 1.2f) * SizeMul;
		New.Stretch = A.SparkStretch * (bSwingDash ? 2.5f : 1.f);
		New.Gravity = A.SparkGravity;
		New.Drag = 2.5f;
		New.Twinkle = A.SparkTwinkle;
		New.Phase = FMath::FRand() * 6.2832f;
		New.Color = Col;
		Sparks.Add(New);
	}
}

void UMechMeleeFx::UpdateSparks(float DeltaTime)
{
	if (!SparkMesh)
	{
		return;
	}
	for (int32 i = Sparks.Num() - 1; i >= 0; --i)
	{
		FSpark& S = Sparks[i];
		if (FxClock - S.Born > S.Life)
		{
			Sparks.RemoveAtSwap(i, EAllowShrinking::No);
			continue;
		}
		S.V.Z += S.Gravity * DeltaTime;
		S.V *= FMath::Exp(-S.Drag * DeltaTime);
		S.P += S.V * DeltaTime;
	}
	if (Sparks.IsEmpty())
	{
		if (SparkMesh->GetNumSections() > 0)
		{
			SparkMesh->ClearAllMeshSections();
		}
		return;
	}
	// 每颗一个面片：朝着本机镜头，顺着速度方向拉长（头亮尾暗）
	FVector Cam = GetOwner()->GetActorLocation() + FVector(0.f, 0.f, 1000.f);
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		Cam = PC->PlayerCameraManager->GetCameraLocation();
	}
	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;
	const int32 N = Sparks.Num();
	Verts.Reserve(N * 4);
	UVs.Reserve(N * 4);
	Colors.Reserve(N * 4);
	Tris.Reserve(N * 6);
	for (const FSpark& S : Sparks)
	{
		const float T = (FxClock - S.Born) / FMath::Max(S.Life, 0.01f);
		float Lum = FMath::Pow(1.f - T, 1.3f) * FMath::Min(1.f, 0.3f + T * 12.f);
		if (S.Twinkle > 0.f)
		{
			Lum *= 1.f - S.Twinkle * 0.5f * (1.f + FMath::Sin(FxClock * 38.f + S.Phase));
		}
		const float Speed = S.V.Size();
		const FVector Dir = Speed > 1.f ? S.V / Speed : FVector::UpVector;
		const float Len = S.Size * 2.f + Speed * S.Stretch;
		const FVector ToCam = (Cam - S.P).GetSafeNormal();
		FVector Side = FVector::CrossProduct(Dir, ToCam);
		if (Side.SizeSquared() < 1e-4f)
		{
			Side = FVector::CrossProduct(FVector::UpVector, ToCam);
		}
		Side = Side.GetSafeNormal() * (S.Size * 0.5f);
		const FVector Tail = S.P - Dir * Len;
		const int32 V0 = Verts.Num();
		Verts.Append({S.P + Side, S.P - Side, Tail + Side, Tail - Side});
		UVs.Append({FVector2D(0.f, 0.f), FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), FVector2D(1.f, 1.f)});
		FColor C = S.Color;
		C.A = static_cast<uint8>(FMath::Clamp(Lum, 0.f, 1.f) * 255.f + 0.5f);
		Colors.Append({C, C, C, C});
		Tris.Append({V0, V0 + 2, V0 + 1, V0 + 1, V0 + 2, V0 + 3});
	}
	SparkMesh->CreateMeshSection(0, Verts, Tris, TArray<FVector>(), UVs, Colors, TArray<FProcMeshTangent>(), false);
}

// ---------------------------------------------------------------- 残影
void UMechMeleeFx::AddGhost(USceneComponent* Blade, const FLinearColor& Color, float Strength, float Life)
{
	const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(Blade);
	if (!SM || !SM->GetStaticMesh() || !GhostMaterial)
	{
		return;
	}
	// 散完的残影留着复用
	FGhost* G = Ghosts.FindByPredicate([this](const FGhost& X) { return X.Comp && FxClock - X.Born > X.Life; });
	if (!G)
	{
		if (Ghosts.Num() >= MaxGhosts)
		{
			return;
		}
		AActor* Owner = GetOwner();
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner, NAME_None);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->bAffectDistanceFieldLighting = false;
		C->bAffectDynamicIndirectLighting = false;
		C->SetForceDisableNanite(true);  // 叠加材质画不了 Nanite，用回退网格（刀已经减过面）
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetupAttachment(Owner->GetRootComponent());
		C->RegisterComponent();
		FGhost New;
		New.Comp = C;
		New.MID = UMaterialInstanceDynamic::Create(GhostMaterial, this);
		G = &Ghosts.Add_GetRef(New);
	}
	if (G->Comp->GetStaticMesh() != SM->GetStaticMesh())
	{
		G->Comp->SetStaticMesh(SM->GetStaticMesh());
		for (int32 Slot = 0; Slot < G->Comp->GetNumMaterials(); ++Slot)
		{
			G->Comp->SetMaterial(Slot, G->MID);
		}
	}
	G->Comp->SetWorldTransform(SM->GetComponentTransform());
	G->Comp->SetVisibility(true);
	G->Born = FxClock;
	G->Life = Life;
	G->Strength = Strength;
	G->MID->SetVectorParameterValue(TEXT("Color"), Color);
}

void UMechMeleeFx::UpdateGhosts()
{
	for (FGhost& G : Ghosts)
	{
		if (!G.Comp)
		{
			continue;
		}
		const float T = (FxClock - G.Born) / FMath::Max(G.Life, 0.01f);
		if (T > 1.f)
		{
			if (G.Comp->IsVisible())
			{
				G.Comp->SetVisibility(false);
			}
			continue;
		}
		// 一圈轮廓光 + 很淡的填充，越旧越淡
		G.MID->SetVectorParameterValue(TEXT("Prm"), FLinearColor(1.4f * G.Strength * FMath::Square(1.f - T), 2.2f, 0.25f, 0.f));
	}
}

// ---------------------------------------------------------------- 刀痕
void UMechMeleeFx::AddScar(const FVector& NearA, const FVector& NearB, const FVector& Cut, const FVector& From, float Size, bool bSpot, bool bRend)
{
	AMechCharacter* Mech = GetMech();
	if (!Mech || Mech->GetHealth()->IsDead() || !ScarMaterial || !SphereMesh)
	{
		return;
	}
	USkeletalMeshComponent* Body = Mech->GetMesh();
	const float BodyScale = Body->GetComponentScale().X;
	// 离刀最近的那段身体：刀痕中心 = 这段骨头上离刀最近的点往刀那边走一个半径（大致在装甲表面）
	float Best = TNumericLimits<float>::Max();
	FName Bone = NAME_None;
	FVector OnBone = Mech->GetActorLocation();
	FVector OnBlade = From;
	float Radius = 25.f;
	for (const FSeg& S : Segs)
	{
		if (Body->GetBoneIndex(S.A) == INDEX_NONE)
		{
			continue;
		}
		const FVector A = Body->GetBoneLocation(S.A);
		const FVector B = S.B ? Body->GetBoneLocation(S.B) : A + Body->GetUpVector() * (S.Radius * 2.f * BodyScale);
		FVector P1;
		FVector P2;
		FMath::SegmentDistToSegmentSafe(A, B, NearA, NearB, P1, P2);
		const float R = S.Radius * BodyScale;
		const float D = FVector::Dist(P1, P2) - R;
		if (D < Best)
		{
			Best = D;
			Bone = S.A;
			OnBone = P1;
			OnBlade = P2;
			Radius = R;
		}
	}
	FVector Out = OnBlade - OnBone;
	if (Best > 30.f)
	{
		// 刀没真贴到身上（判定是出招时刀前方的一个球，画面上刀可能还差一截）：按刀的高度落在胸口或腰腹，朝着砍的人。
		// 10-31 第一版按「离刀最近」找，刀离身体一两米时会落到不相干的胳膊、小腿上
		const FVector Mid = (NearA + NearB) * 0.5f;
		const FVector Pelvis = Body->GetBoneLocation(TEXT("pelvis"));
		const FVector Chest = Body->GetBoneLocation(TEXT("spine_03"));
		const FVector Neck = Body->GetBoneLocation(TEXT("spine_05"));
		const bool bUpper = Mid.Z > Chest.Z;
		const FVector A = bUpper ? Chest : Pelvis;
		const FVector B = bUpper ? Neck : Chest;
		OnBone = FMath::ClosestPointOnSegment(FVector(Mid.X, Mid.Y, FMath::Clamp(Mid.Z, A.Z, B.Z)), A, B);
		Bone = bUpper ? FName(TEXT("spine_03")) : FName(TEXT("pelvis"));
		Radius = (bUpper ? 30.f : 28.f) * BodyScale;
		Out = (From - OnBone).GetSafeNormal2D();
	}
	if (Out.SizeSquared() < 1e-4f)
	{
		Out = From - OnBone;
	}
	Out = Out.GetSafeNormal();
	FVector Center = OnBone + Out * Radius;
	FVector X = Cut - Out * (Cut | Out);
	if (X.SizeSquared() < 1e-4f)
	{
		X = FVector::CrossProduct(Out, FVector::UpVector);
	}
	X = X.GetSafeNormal();
	const FRotator Rot = FRotationMatrix::MakeFromXY(X, Out).Rotator();
	// 一套连招砍在同一处：新的刀口往旁边错开（沿刀口平面的法线，一道道平行），不叠成一团（10-31 第一版五道叠在一起成了一大片橙色）
	int32 Near = 0;
	for (const FScar& Old : Scars)
	{
		if (Old.Comp && FVector::Dist(Old.Comp->GetComponentLocation(), Center) < 20.f)
		{
			++Near;
		}
	}
	if (Near > 0)
	{
		static const float Offsets[] = {13.f, -13.f, 26.f, -26.f, 6.f, -6.f, 19.f, -19.f};
		Center += FRotationMatrix(Rot).GetUnitAxis(EAxis::Z) * Offsets[(Near - 1) % UE_ARRAY_COUNT(Offsets)];
	}
	// 小球（屏幕空间贴花的范围）：够盖住这段身体朝刀那一面
	const float Sphere = Radius * 1.6f + 14.f;
	const float Scale = Sphere / 50.f;

	// 太多了先去掉最老的（割裂的刀口另算，最多 5 道 = 最多 5 层）
	auto RemoveOldest = [this](bool bRendOnly) {
		for (int32 i = 0; i < Scars.Num(); ++i)
		{
			if (Scars[i].bRend == bRendOnly)
			{
				if (Scars[i].Comp)
				{
					Scars[i].Comp->DestroyComponent();
				}
				Scars.RemoveAt(i);
				return;
			}
		}
	};
	if (bRend && Scars.FilterByPredicate([](const FScar& S) { return S.bRend; }).Num() >= MaxRendScars)
	{
		RemoveOldest(true);
	}
	if (Scars.Num() >= MaxScars)
	{
		RemoveOldest(false);
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Mech, NAME_None);
	C->SetStaticMesh(SphereMesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->bAffectDistanceFieldLighting = false;
	C->bAffectDynamicIndirectLighting = false;
	C->SetupAttachment(Body, Bone);
	C->RegisterComponent();
	C->SetWorldLocationAndRotation(Center, Rot);
	C->SetWorldScale3D(FVector(Scale));
	FScar S;
	S.Comp = C;
	S.MID = UMaterialInstanceDynamic::Create(ScarMaterial, this);
	C->SetMaterial(0, S.MID);
	S.Born = FxClock;
	S.Scale = Scale;
	S.bSpot = bSpot;
	S.bRend = bRend;
	S.HalfLen = Size / Scale;
	// 线宽按机体尺寸：2.4 米的机体在第三人称 5–8 米外，一两厘米宽的线不到一个像素（10-31 第一版就是这样看不见）
	S.Width = (Size > 35.f ? 5.f : 3.5f) / Scale;
	S.Front = -Radius * 0.9f / Scale;
	S.Outer = 20.f / Scale;  // 中心是按骨头估的表面，真表面在它外面 20 厘米以内
	S.Tau = Size > 35.f ? 0.9f : 0.6f;
	S.Life = bSpot ? 1.6f : 2.6f;
	Scars.Add(S);
	static const bool bLog = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeFxLog"));
	if (bLog)
	{
		UE_LOG(LogTemp, Display, TEXT("[meleefx] %s 刀痕：骨头 %s 半径 %.0f 离刀 %.0f 中心 %s 小球 %.0f 半长 %.0f 圆斑 %d 割裂 %d"), *Mech->GetName(), *Bone.ToString(),
			Radius, Best, *Center.ToCompactString(), Sphere, Size, bSpot ? 1 : 0, bRend ? 1 : 0);
	}
}

void UMechMeleeFx::UpdateScars(float DeltaTime)
{
	if (Scars.IsEmpty())
	{
		LastRend = 0;
		return;
	}
	// 割裂层数还在：双刀的刀口一直暗红地烧着；层数没了才冷下去
	const AMechCharacter* Mech = GetMech();
	const int32 Rend = Mech && Mech->GetEffects() ? Mech->GetEffects()->GetStacks(TEXT("Rend")) : 0;
	LastRend = Rend;
	for (int32 i = Scars.Num() - 1; i >= 0; --i)
	{
		FScar& S = Scars[i];
		const float Age = FxClock - S.Born;
		if (S.bRend)
		{
			S.Ember = Rend > 0 ? FMath::Min(1.f, S.Ember + DeltaTime * 4.f) : FMath::Max(0.f, S.Ember - DeltaTime * 1.7f);
			if (Rend > 0)
			{
				S.Life = FMath::Max(S.Life, Age + 0.7f);
			}
		}
		if (Age > S.Life || !S.Comp)
		{
			if (S.Comp)
			{
				S.Comp->DestroyComponent();
			}
			Scars.RemoveAt(i);
			continue;
		}
		// 最后半秒整体淡掉（热度已经很低，只剩暗红）
		const float Fade = FMath::Clamp((S.Life - Age) / 0.5f, 0.f, 1.f);
		const float Hot = FMath::Exp(-Age / S.Tau) * Fade;
		S.MID->SetVectorParameterValue(TEXT("Prm"), FLinearColor(Hot, S.HalfLen, S.Width, S.Front));
		S.MID->SetVectorParameterValue(TEXT("Prm2"), FLinearColor(S.Ember * Fade, FxClock, S.bSpot ? 1.f : 0.f, S.Outer));
	}
}

void UMechMeleeFx::ClearScars()
{
	for (FScar& S : Scars)
	{
		if (S.Comp)
		{
			S.Comp->DestroyComponent();
		}
	}
	Scars.Reset();
}

// ---------------------------------------------------------------- 冲击环 / 闪光
void UMechMeleeFx::AddRing(const FVector& At, const FVector& Normal, float Radius, float Width, float Strength, const FLinearColor& Color, float Life, bool bGlint)
{
	if (!RingMaterial || !PlaneMesh)
	{
		return;
	}
	// 用完的面片留着复用（一台机体同时最多几个）
	FRing* R = Rings.FindByPredicate([this](const FRing& X) { return X.Comp && FxClock - X.Born > X.Life; });
	if (!R)
	{
		if (Rings.Num() >= 8)
		{
			return;
		}
		AActor* Owner = GetOwner();
		FRing New;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner, NAME_None);
		C->SetStaticMesh(PlaneMesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->bAffectDistanceFieldLighting = false;
		C->bAffectDynamicIndirectLighting = false;
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetupAttachment(Owner->GetRootComponent());
		C->RegisterComponent();
		New.Comp = C;
		New.MID = UMaterialInstanceDynamic::Create(RingMaterial, this);
		C->SetMaterial(0, New.MID);
		R = &Rings.Add_GetRef(New);
	}
	R->Born = FxClock;
	R->Life = Life;
	R->Radius = Radius;
	R->Width = Width;
	R->Strength = Strength;
	R->bGlint = bGlint || Normal.IsNearlyZero();
	R->Color = Color;
	R->At = At;
	R->Normal = Normal.GetSafeNormal();
	R->Comp->SetVisibility(true);
	R->MID->SetVectorParameterValue(TEXT("Color"), Color);
}

void UMechMeleeFx::UpdateRings()
{
	if (Rings.IsEmpty())
	{
		return;
	}
	// 闪光正对本机的镜头，横向那道光沿屏幕水平方向
	FVector CamLoc = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	bool bCam = false;
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		CamLoc = PC->PlayerCameraManager->GetCameraLocation();
		CamRot = PC->PlayerCameraManager->GetCameraRotation();
		bCam = true;
	}
	for (FRing& R : Rings)
	{
		const float T = (FxClock - R.Born) / FMath::Max(R.Life, 0.01f);
		if (T > 1.f || !R.Comp)
		{
			if (R.Comp && R.Comp->IsVisible())
			{
				R.Comp->SetVisibility(false);
			}
			continue;
		}
		FRotator Rot;
		if (R.bGlint)
		{
			const FVector ToCam = bCam ? (CamLoc - R.At).GetSafeNormal() : FVector::UpVector;
			Rot = FRotationMatrix::MakeFromZX(ToCam, bCam ? FRotationMatrix(CamRot).GetUnitAxis(EAxis::Y) : FVector::ForwardVector).Rotator();
		}
		else
		{
			Rot = FRotationMatrix::MakeFromZ(R.Normal).Rotator();
		}
		// 面片 100 × 100：冲击环按半径缩放；闪光横向长、竖向也要留够（中心那点和短竖线）
		R.Comp->SetWorldLocationAndRotation(R.At, Rot);
		R.Comp->SetWorldScale3D(FVector(R.Radius * 2.f / 100.f, R.Radius * 2.f / 100.f, 1.f));
		R.MID->SetVectorParameterValue(TEXT("Prm"), FLinearColor(T, R.Width, R.Strength, R.bGlint ? 1.f : 0.f));
	}
}

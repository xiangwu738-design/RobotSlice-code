// 配装目录的「详细信息」（10-31 配装照片）：每件的名字、型号、制造商、重量感，和详细信息面板上的参数行。
// 名字都是草案（用户：「每把武器都有名字和制造商」，完整名单还没定），全表和说明在 docs/武器名单_草案.md，界面先显示草案。
// 命名规则（用户定）：制造商 + 型号 +「两字名」+ 类别，例如 维克托重工 VHI-R36「直线」连射步枪。
// 企业（10-31 用户定的正式设定，docs/世界观/企业设定.md v3）：新纪建设 New Era Consolidated，NEC-（大剑 / 钝器 / 霰弹 / 火炮）、
// 维克托重工 Victor Heavy Industries，VHI-（步枪 / 冲锋枪 / 导弹 / 烟雾）、相位电子 Phase Electronics Corporation，PHASE-（充能步枪 / 狙击 / 双手刀 / EMP）、
// 回收联合（暂不出场、未定名；11-01 用户把它的双持手枪 / 匕首 / 双刀 / 钩爪 / 网分给了三家公司，现在没有东西归它）。主办方合意集团（CONSENT）不造武器、不参赛。
// 参数直接从数值表算（MechWeapons、UMechShoulderComponent、AMechGrenade、AMechSmoke、AMechNet、UMechMovementComponent / UMechHealthComponent 默认值），
// 少数藏在别的 .cpp 里的（刀的招式基准、导弹、框架 / 推进器的改动）抄在 MechCatalog.cpp，旁边写了出处，改那边要一起改。
// 加一类：MechLoadout 加类 → 这里 Info / RawStats 加一段 → 摄影棚 AMechItemStudio 的 Subject / ViewsFor 加拍法。

#pragma once

#include "CoreMinimal.h"
#include "MechTypes.h"

/** 制造商（联赛四家企业） */
enum class EMechMaker : uint8
{
	NewEra,    // 新纪建设 New Era Consolidated（NEC，旧草案「鼎山重工」）：工程机械
	Victor,    // 维克托重工 Victor Heavy Industries（VHI，旧草案「黑铁军工」）：军工
	Phase,     // 相位电子 Phase Electronics Corporation（PHASE，旧草案「白塔科技」）：企业制式
	Salvage,   // 回收联合（暂不出场、未定名，前缀待定）：拼装改装
	Count
};

/** 手感上的分量（「重量感」）：轻 / 中 / 重 / 很重 */
enum class EMechHeft : uint8
{
	Light,
	Medium,
	Heavy,
	VeryHeavy
};

namespace MechCatalog
{
	struct FMaker
	{
		/** 新纪建设 */
		const TCHAR* Name;
		/** New Era Consolidated */
		const TCHAR* English;
		/** 简称 / 型号前缀 NEC */
		const TCHAR* Prefix;
		/** 企业理念（用户定）：「我们建设明天。」 */
		const TCHAR* Slogan;
		/** 一句话：竞技风格和擅长的武器 */
		const TCHAR* Trait;
		/** 名称还没定（回收联合） */
		bool bPending;
		/** 品牌色（占位标识的颜色；名牌色标用它，和以后按制造商分的 HUD 主题同色系） */
		FLinearColor Color;
		/** 标识文件名（Content/Slate/Makers/<Logo>_brand.svg、_white.svg；空 = 没有标识，界面写名字） */
		const TCHAR* Logo;
		/** 标识宽高比 */
		float LogoAspect;
	};
	const FMaker& Maker(EMechMaker M);

	/** 一件的名字（草案） */
	struct FInfo
	{
		EMechMaker Maker;
		/** 型号，例如 VHI-R36 */
		const TCHAR* Model;
		/** 名字（两个字），例如 直线 */
		const TCHAR* Title;
		/** 类别全称，例如 连射步枪 */
		const TCHAR* Kind;
		EMechHeft Heft;
		/** 用户给的例子（其余是草案，等用户审） */
		bool bFromUser;
	};
	const FInfo& Info(int32 Category, int32 Index);
	/** 维克托重工 VHI-R36「直线」连射步枪 */
	FString FullName(int32 Category, int32 Index);
	/** 「直线」 */
	FString Quoted(int32 Category, int32 Index);
	const TCHAR* HeftName(EMechHeft Heft);

	/** 详细信息面板的一行：名字、条（0–1，同一类里比）、数字和单位 */
	struct FStat
	{
		FText Label;
		float Bar = 0.f;
		FString Value;
		/** 原始值（同一类里归一化用；没有这一项时为 0、Value 写「—」） */
		float Raw = 0.f;
		/** 条按平方根画（伤害这种差几十倍的） */
		bool bSqrt = false;
	};
	/** 这一件的参数行（已按同一类里最大值归一化） */
	void Stats(int32 Category, int32 Index, TArray<FStat>& Out);

	/** 框架数值（AMechCharacter::ApplyLoadout 就按这个设；中型从 UMechHealthComponent 默认值取）。
	 *  10-31 起 = 档位（轻中重）× 制造商特色：新纪建设耐用（更硬、现场抢修）、维克托重工可靠（弹匣大、抗干扰）、相位电子精密（更快更准、怕 EMP） */
	struct FFrameSpec
	{
		float AP;
		float Stagger;
		float Speed;
		float Regen;
		float Knockback;
		/** 下面是制造商特色（同一家三档一样）：快速闪避耗能、弹匣、被网和失衡僵直的时间、中 EMP 的时间、导弹锁定时间、射击散布的倍数 */
		float QuickCost = 1.f;
		float Magazine = 1.f;
		float Disrupt = 1.f;
		float Emp = 1.f;
		float Lock = 1.f;
		float Spread = 1.f;
		/** 现场抢修（新纪建设）：见 UMechHealthComponent::RepairDelay 等 */
		bool bRepair = false;
	};
	FFrameSpec FrameSpec(EMechFrame Frame);

	/** 推进器数值（和 AMechCharacter::ApplyLoadout 一致，按中型框架；从 UMechMovementComponent 默认值算起） */
	struct FBoosterSpec
	{
		float Boost;
		float Quick;
		float QuickCost;
		float QuickCooldown;
		float MaxRise;
		float ThrustUp;
		float AirBoost;
		float AirGravity;
		float GroundRegen;
		float AirRegen;
		bool bCanThrust;
		bool bCanAirBoost;
		/** 摄影棚里喷焰锥体多长（米，和游戏里 0.2–0.6 同一量级；只是示意：地面型短促、飞行型长） */
		float FlameLength;
	};
	FBoosterSpec BoosterSpec(EMechBooster Booster);

	/** 框架的机体网格（10-31 起九台各不相同）：AMechCharacter 换网格、摄影棚、结算展示都按这个，照片自动跟着变 */
	const TCHAR* FrameMesh(int32 FrameIndex);

	/** 框架的外形（10-31 阵营机体，数据抄自 lookdev/rig/factions/manifest.json）：网格、喷口、头 / 踝限位、迷彩。
	 *  三家同档的数值一样（FrameSpec），外形各不相同 */
	struct FFrameBody
	{
		/** NEC_M 之类（lookdev/rig/factions/<Key>.fbx） */
		const TCHAR* Key;
		const TCHAR* Mesh;
		/** 左主喷口（喇叭口外沿中心）和喷射方向：网格绑定姿势坐标（厘米，未乘 RobotScale；前 +Y、左 +X、上 +Z），右边 X 取反；喷焰挂 spine_05 */
		FVector NozzleMouth;
		FVector NozzleDir;
		/** 喇叭口半径（厘米，网格尺寸）：喷焰粗细按它和 v3 的 4.1 之比 */
		float NozzleRadius;
		/** 头相对胸（spine_05）的软限位（度，0 = 不限）：维克托中 / 重的下沉炮塔头被颊板 / 战斗室夹着，转头 / 歪头会擦 */
		float HeadYaw;
		float HeadPitch;
		/** 脚尖上翘上限（度）：相位中 / 重的靴口前沿在脚尖上翘时碰脚背，收到 15；其余 35（MechLegLimit 的默认值） */
		float AnkleUp;
		/** 主漆槽是物体空间折线迷彩（维克托三台；导入时挂 MI_Robot_PaintCamo，涂装改 BaseColor + CamoDark） */
		bool bCamo;
	};
	const FFrameBody& FrameBody(int32 FrameIndex);
	/** 按网格资源找是哪一台（摄影棚、结算展示这类没有配装的网格也能拿到限位）；不是这九台返回 nullptr */
	const FFrameBody* FrameBodyForMesh(const UObject* MeshAsset);
	/** 框架是哪家造的（新纪建设 / 维克托重工 / 相位电子） */
	EMechMaker FrameMaker(int32 FrameIndex);
	/** 同厂加成（11-01）：这一类配装（MechLoadout::ECategory，射击 / 近战 / 肩部）和框架是不是同一家造的 */
	bool SameMaker(const struct FMechLoadout& Loadout, int32 Category);
	/** 同厂加成的说明（详细信息面板）；框架、推进器没有 = nullptr */
	const TCHAR* OwnBuffText(int32 Category, int32 Index);
}

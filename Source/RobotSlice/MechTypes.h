// 战斗共用的小类型

#pragma once

#include "CoreMinimal.h"
#include "MechTypes.generated.h"

/** 命中来源：决定受击特效、音效和镜头震动的轻重 */
UENUM()
enum class EMechHitKind : uint8
{
	Bullet,
	Missile,
	Blade
};

/** 子弹 / 飞刀打中的部位（伤害倍数见 MechWeapons::PartScale） */
UENUM()
enum class EMechHitPart : uint8
{
	Torso,
	Head,
	Limb
};

/** 回合阶段 */
UENUM()
enum class EMechRoundPhase : uint8
{
	Waiting,
	Countdown,
	Fight,
	RoundOver,
	MatchOver,
	/** 开打前的禁用 / 选配（MechDraft） */
	Draft,
	/** 房间：真人自己选队，房主定每队几台、AI 强度，然后开始 */
	Lobby
};

/** 手上拿什么：射击（步枪在右手、刀挂在左腰）/ 近战（刀在右手、步枪挂在背上），Q 切换。肩部导弹两种都能用 */
UENUM()
enum class EMechStance : uint8
{
	Gun,
	Blade
};

/** 近战招式：左键点按轻击（最多四连），左键按住重斩 */
UENUM()
enum class EMechMelee : uint8
{
	None,
	Light1,
	Light2,
	Light3,
	Light4,
	Heavy
};

// ---------------------------------------------------------------- 配装（五类，每类第一个是现有的；各项说明和能不能用见 MechLoadout）
// 框架（10-31 阵营机体进游戏）= 三家企业 × 轻 / 中 / 重，共 9 种；编号 = 企业 × 3 + 档位（档位 0 中 1 轻 2 重，和以前三种框架的编号一样），
// 所以旧存档 / 旧设置里的 0 / 1 / 2 正好是新纪建设的同档。数值 = 档位（MechFrameTier）× 制造商特色（10-31 起，MechCatalog::FrameSpec），
// 外形（网格、喷口、头 / 踝限位、迷彩）在 MechCatalog::FrameBody。编号存在设置里，只能往后加。
UENUM()
enum class EMechFrame : uint8
{
	Medium,         // 新纪建设 中型「挖掘机」（名字沿用以前的三种框架，别的代码里 EMechFrame::Medium 照旧 = 默认框架）
	Light,          // 新纪建设 轻型（= 以前一直用的 v3）
	Heavy,          // 新纪建设 重型「矿卡」
	VictorMedium,   // 维克托重工 中型「铁壁」
	VictorLight,    // 维克托重工 轻型「斥候」
	VictorHeavy,    // 维克托重工 重型「突击炮」
	PhaseMedium,    // 相位电子 中型「B2 / 单塔」
	PhaseLight,     // 相位电子 轻型「双塔」
	PhaseHeavy      // 相位电子 重型「门架」
};

/** 框架的数值档（各家再乘自己的特色，见 MechCatalog::FrameSpec；制造商还管外观和 HUD 主题） */
enum class EMechFrameTier : uint8
{
	Medium,
	Light,
	Heavy
};

/** 框架共几种（企业 3 × 档位 3） */
constexpr int32 MechNumFrames = 9;

/** 框架 → 数值档 */
inline EMechFrameTier MechFrameTier(EMechFrame Frame)
{
	return static_cast<EMechFrameTier>(static_cast<uint8>(Frame) % 3);
}

/** 框架 → 第几家企业（0 新纪建设、1 维克托重工、2 相位电子，和 MechCatalog.h 的 EMechMaker 前三个一致） */
inline int32 MechFrameMakerIndex(EMechFrame Frame)
{
	return FMath::Min(static_cast<int32>(Frame) / 3, 2);
}

/** 企业 + 档位 → 框架 */
inline EMechFrame MechMakeFrame(int32 MakerIndex, EMechFrameTier Tier)
{
	return static_cast<EMechFrame>(FMath::Clamp(MakerIndex, 0, 2) * 3 + static_cast<int32>(Tier));
}

UENUM()
enum class EMechBooster : uint8
{
	Balanced,
	Ground,
	Flight
};

/** 射击武器（数值和特色见 MechWeapons；编号存在设置里，只能往后加） */
UENUM()
enum class EMechShooter : uint8
{
	Rifle,       // 步枪
	SMG,         // 冲锋枪
	Shotgun,     // 霰弹枪
	Marksman,    // 充能步枪（原精准步枪，10-30 改）
	Sniper,      // 狙击枪
	DualPistol   // 双持手枪
};

/** 近战武器 */
UENUM()
enum class EMechMeleeWeapon : uint8
{
	Katana,      // 双手刀（素材剑 07）
	QuickBlade,  // 快刀（11）
	OneHand,     // 单手刀（13）
	GreatSword,  // 大剑（14）
	TwinBlades,  // 双刀（16）
	Blunt,       // 钝器（20）
	Dagger       // 投掷匕首（03）
};

/** 肩部装备（都是飘在左肩旁的无人机，一人带一件、按 E 用；编号存在设置里，只能往后加）。
 *  导弹在 UMechCombatComponent，Burst 起那四件在 UMechTacticalComponent（MechIsTacticalShoulder），其余在 UMechShoulderComponent */
UENUM()
enum class EMechShoulder : uint8
{
	Missiles,
	Artillery,  // 火炮（原来占位的「榴弹炮」）
	Shield,     // 护盾
	Grapple,    // 钩爪
	Net,        // 捕获网
	Smoke,      // 烟雾弹
	Emp,        // EMP
	// 11-01 用户「战术装备其实是肩部装备」、选「合并成一格」：原来独立的第六类配装（C 键）这四件并进来，冷却和数值都不变。
	// 前两个是移动型（被围攻时脱身），后两个是攻击型；实现还在 UMechTacticalComponent（地雷本体是 AMechMine）
	Burst,      // 紧急脱离：朝按键方向（没按 = 往后）0.38 秒冲出 22 米，不耗能量、过热也能用（用户 10-31 定：冲完能量清零）
	Launch,     // 弹射升空：原地往上弹 16 米
	Shockwave,  // 震荡波：蓄力 0.3 秒，震开身边 9 米内的敌人、打断出刀
	Mine        // 地雷：抛向准星，敌人靠近 0.35 秒后炸，能被打掉
};

/** 这件肩部装备走不走 UMechTacticalComponent（11-01 并入肩部时只改入口：预测和网络都写在那个组件里，不重写） */
constexpr bool MechIsTacticalShoulder(EMechShoulder Shoulder)
{
	return Shoulder >= EMechShoulder::Burst;
}

/** 机体涂装（sRGB）：主漆、副漆、点缀色；点缀兼武器灯光/刀光，机体识别灯仍是队伍色。 */
USTRUCT()
struct FMechPaint
{
	GENERATED_BODY()

	UPROPERTY()
	FColor Primary = FColor(0x5a, 0x63, 0x6c);

	UPROPERTY()
	FColor Secondary = FColor(0x38, 0x3f, 0x46);

	UPROPERTY()
	FColor Accent = FColor(0xcf, 0x5f, 0x1b);
};

/** 一套配装：框架 + 推进器 + 射击 + 近战 + 肩部 */
USTRUCT()
struct FMechLoadout
{
	GENERATED_BODY()

	UPROPERTY()
	EMechFrame Frame = EMechFrame::Medium;

	UPROPERTY()
	EMechBooster Booster = EMechBooster::Balanced;

	UPROPERTY()
	EMechShooter Shooter = EMechShooter::Rifle;

	UPROPERTY()
	EMechMeleeWeapon Melee = EMechMeleeWeapon::Katana;

	UPROPERTY()
	EMechShoulder Shoulder = EMechShoulder::Missiles;
};

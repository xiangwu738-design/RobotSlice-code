// 武器目录：射击 6 把、近战 7 把，各自的模型、数值和特色。配装选哪把，UMechCombatComponent 就按哪把的数值打。
// 名字和打法来自用户填的素材配装表（刀剑）和口头要求（枪：每把都要有特色）；数值是第一版草案，在测试场里试过再调。

#pragma once

#include "CoreMinimal.h"
#include "MechTypes.h"

namespace MechWeapons
{
	/** 射击武器 */
	struct FGun
	{
		const TCHAR* Name;
		/** 配装页的一句话说明 */
		const TCHAR* Desc;
		const TCHAR* Mesh;
		/** 枪口在模型里的位置（模型坐标，厘米，未缩放；原点在握把） */
		FVector Muzzle;
		/** 弹匣容量；0 = 不用弹匣（冲锋枪靠散热、狙击枪耗推进能量） */
		int32 Magazine;
		/** 两发之间的间隔（秒） */
		float Interval;
		float Damage;
		float Impact;
		/** 子弹速度（厘米/秒）和飞行时间（秒），两者相乘就是射程 */
		float BulletSpeed;
		float BulletLife;
		float ReloadTime;
		/** 腰射散布（度）；右键瞄准时乘 AimSpread */
		float Spread;
		float AimSpread;
		/** 一次打出几颗（霰弹） */
		int32 Pellets;
		/** 曳光粗细倍数 */
		float TracerWidth;
		/** 后坐：镜头上抬（度） */
		float Kick;

		// ---- 特色（0 / false = 没有）
		/** 冲锋枪：每发加多少热量（满 1 过热，过热后 OverheatLock 秒打不了） */
		float HeatPerShot;
		/** 充能步枪（原精准步枪，10-30 改成类似 Apex 旧版充能步枪）：按住左键充能 ChargeTime 秒，充满自动打出一道命中扫描的光束
		 *  （伤害 / 冲击就是上面的 Damage / Impact）；充能期间枪口射出一道激光，照到的敌人每秒受 LaserDPS 伤害、LaserImpact 冲击；
		 *  提前松开 = 按当时的充能打出（10-30 晚玩家：松手就该发射，不要撤回；见 ChargeShotScale） */
		float ChargeTime;
		float LaserDPS;
		float LaserImpact;
		/** 狙击枪：每发耗多少推进能量 */
		float EnergyPerShot;
		/** 霰弹枪：打中造成伤害的多少比例回复成装甲（10-30 替换掉「击毁回 3000」） */
		float Lifesteal;
		/** 步枪：连续命中同一目标每层加成（最多 StreakMax 层，停火 StreakWindow 秒清零） */
		float StreakBonus;
		/** 双持手枪：左右手交替开火（重斩砍中补满弹匣，见 bHeavyRefill） */
		bool bDual;
		/** 长枪的左手握点（护木下面，模型坐标，厘米；是手腕的位置，比护木底面略低、略靠后，手掌刚好托住）；零 = 不用 */
		FVector ForeGrip = FVector::ZeroVector;

		// ---- 10-30 玩家反馈后加的特色（表里按枪单独设，见 MechWeapons.cpp 的 GunTable）
		/** 距离衰减：飞过 FalloffStart 厘米后伤害开始降，到 FalloffEnd 降到 ×FalloffMin（冲锋枪）；FalloffEnd = 0 不衰减 */
		float FalloffStart = 0.f;
		float FalloffEnd = 0.f;
		float FalloffMin = 1.f;
		/** 双持手枪：近战重斩砍中敌人时弹匣立即补满（10-30 晚玩家：去掉「闪避装满」，改成「打出重击补满子弹」，鼓励刀枪切换） */
		bool bHeavyRefill = false;
		/** 双持手枪：快速闪避时弹匣立即装满、没有冷却（11-01 用户：「手枪还是冲刺回弹，没有回弹cd」——改回 10-30 以前那样；重斩补满也留着） */
		bool bDodgeRefill = false;
		/** 霰弹枪：CloseRange 厘米内冲击 ×CloseImpact；bInterruptMelee = 打中正在出刀（刀还没砍下）的人直接打断这一刀 */
		float CloseRange = 0.f;
		float CloseImpact = 1.f;
		bool bInterruptMelee = false;
		/** 狙击枪：开镜时子弹吸附到准星附近敌人身上离准星射线最近的那一点（见 ScopeMagnet*） */
		bool bScopeMagnet = false;

		// ---- 10-31 握持（模型坐标，厘米；在 MechWeapons.cpp 的 GunTable 里按枪设，量法见 README「握持」）
		/** 右手握把：截面中心、往上方向（XZ 平面，指向握把顶）、截面（前后半宽、左右半宽、圆角）、沿轴范围（相对中心：握把底、握把顶） */
		FVector GripCenter = FVector::ZeroVector;
		FVector GripUp = FVector::UpVector;
		FVector GripSize = FVector(3.f, 2.f, 1.5f);
		FVector2D GripAlong = FVector2D(-5.f, 5.f);
		/** 握把在手里斜多少（手坐标 X 往拇指那侧、Z 往指尖：握把轴 = (1, 0, Z)）：一般的枪 0.225（贴着指根线）；
		 *  10-31 第二版：充能步枪的斜杆往后倒，按这个斜着横过手心，手腕少折（见 README「握持」） */
		FVector GripHandDir = FVector(1.f, 0.f, 0.225f);
		/** 长枪左手托的护木：截面中心（左手手心空腔对准这里）、半宽（左右、上下）；中心为零 = 不托 */
		FVector ForeCenter = FVector::ZeroVector;
		FVector2D ForeHalf = FVector2D(2.f, 2.f);

		/** 飞了 Dist 厘米后的伤害倍数（距离衰减） */
		float FalloffAt(float Dist) const
		{
			if (FalloffEnd <= FalloffStart)
			{
				return 1.f;
			}
			return FMath::Lerp(1.f, FalloffMin, FMath::Clamp((Dist - FalloffStart) / (FalloffEnd - FalloffStart), 0.f, 1.f));
		}
	};

	/** 近战武器（招式时长、伤害、冲击在 MechCombatComponent 的基础招式上乘倍数） */
	struct FBlade
	{
		const TCHAR* Name;
		const TCHAR* Desc;
		const TCHAR* Mesh;
		/** 出招快慢：招式各时刻乘这个（越大越慢） */
		float TimeScale;
		float DamageScale;
		float ImpactScale;
		/** 积累失衡按冲击的多少（「耐性削减」） */
		float StaggerScale;
		/** 刀够得着的距离（厘米，机体尺寸） */
		float Reach;
		/** 双刀：左手也拿一把 */
		bool bTwin;

		// ---- 投掷匕首（Knives = 0 就不是）
		int32 Knives;
		/** 回一把要多久（秒） */
		float KnifeRegen;
		float KnifeDamage;
		float KnifeImpact;
		/** 飞行速度（厘米/秒） */
		float KnifeSpeed;
		/** 挂在右手（hand_r）上的位置和朝向：照这把刀用的动作包里武器骨头相对右手的变换算的（tools/animlab_blade_mounts.py）；
		 *  不设就用武士刀的挂法。双刀左手那把按它镜像 */
		bool bMount = false;
		FVector MountPos = FVector::ZeroVector;
		FQuat MountRot = FQuat::Identity;
		/** 双手武器的左手握点（刀柄靠尾的位置，模型坐标，厘米）；零 = 单手或者动作本来就握得住 */
		FVector LeftGrip = FVector::ZeroVector;
		// 以上 bMount / MountPos / MountRot / LeftGrip 是 10-31 以前的挂法（-MechOldGrip 时用）。

		// ---- 10-31 握持（模型坐标，厘米；在 MechWeapons.cpp 的 BladeTable 里按刀设）：右手手心空腔对准柄上 Grip 这一点，
		// 刀身在手里的方向按 HandDir（手坐标：X 往拇指那侧、Y 手心、Z 往指尖；照各动作包里左右手的相对位置量的，见 README「握持」），
		// 刃口（EdgeZ 那一侧）对着手指方向；挂点由 UMechMeshComponent::GripPropInHand 按机体的手算，不再写死
		FVector Grip = FVector::ZeroVector;
		float GripRadius = 1.5f;
		/** 柄沿 X 的范围（护手到柄尾） */
		FVector2D GripRange = FVector2D(-10.f, 10.f);
		FVector HandDir = FVector(0.93f, 0.f, 0.37f);
		/** 刃口朝模型 -Z 的刀是 -1；钝器（锤头）是 +1 */
		float EdgeZ = -1.f;
		/** 双手武器：左手手心空腔对准柄上 X = LeftAt（0 = 单手） */
		float LeftAt = 0.f;
	};

	/** 分部位伤害（子弹、飞刀）：躯干 ×1、头 ×1.5、四肢 ×0.85，顺序同 EMechHitPart */
	constexpr float PartScale[3] = {1.f, 1.5f, 0.85f};

	/** 玩家的瞄准辅助：子弹、枪管都对着准星；准星离锁定目标（按提前量算）在窗口内时往目标那边拉。
	 *  窗口按距离算：目标在 AimAssistRadius 厘米半径里对应的角度，夹在 Min–Max 度之间（近处大，追高速机体最难的时候帮得多）；
	 *  正对时拉 AimAssistStrength，越偏越少（平方衰减，靠近中心那一圈帮得多）。
	 *  10-29 先嫌「锁定后子弹直接飞目标中心」太强改成 5 度 / 0.7，10-30 又嫌太弱（人打不过 AI）调到现在这样 */
	constexpr float AimAssistRadius = 320.f;
	constexpr float AimAssistMinCone = 4.f;
	constexpr float AimAssistMaxCone = 12.f;
	constexpr float AimAssistStrength = 0.9f;
	/** 瞄准粘滞：准星压在敌人身上时转视角的速度倍数（边缘一圈渐变回 1） */
	constexpr float AimFriction = 0.7f;  // 用户嫌战斗手感「粘滞」，粘滞别给太重
	/** AI 瞄准点按距离慢慢晃的幅度（度），远了会打偏 */
	constexpr float AIAimError = 1.3f;

	/** 狙击枪开镜吸附（10-30 玩家：开镜一点不帮太难打；要小范围吸附但不能总拉到胸口）：
	 *  准星射线离敌人身体（按子弹飞到时的位置）在 ScopeMagnetRadius 厘米换算的角度内（夹在 Min–Max 度之间），
	 *  子弹就拐向那台敌人身上离射线最近的点——瞄头附近就打头，不往胸口中心拉 */
	constexpr float ScopeMagnetRadius = 80.f;
	constexpr float ScopeMagnetMin = 0.5f;
	constexpr float ScopeMagnetMax = 2.f;
	/** 开镜时准星压在敌人身上，转视角速度再乘这个（不开镜是 AimFriction 0.7） */
	constexpr float ScopeFriction = 0.5f;
	/** 开镜转视角的粘滞倍数：照着平时的 AimFriction（0.7–1，压在敌人身上时小）换算成 ScopeFriction–1 */
	inline float ScopeLookScale(float InAimFriction)
	{
		const float OnTarget = FMath::Clamp((1.f - InAimFriction) / (1.f - AimFriction), 0.f, 1.f);
		return FMath::Lerp(1.f, ScopeFriction, OnTarget);
	}

	constexpr float StreakWindow = 0.6f;
	/** 投掷匕首近距离击退（11-01 用户：被拉近身几乎没有还手能力，匕首击退再强一点）：这个距离内命中，在普通击退（约 3.3 米/秒）上再加一段（厘米/秒），
	 *  贴脸是 KnifeCloseKnockback，随距离线性减到 KnifeCloseRange 处为 0。
	 *  只加近距离：远处也推的话滞留投掷会更难贴上去。单独加，不改冲击值，免得削失衡和顿帧也变强。对方格挡时只剩两成 */
	constexpr float KnifeCloseRange = 1500.f;
	constexpr float KnifeCloseKnockback = 865.f;  // 用户：贴脸最高 12 米/秒（普通击退约 335 + 865）
	constexpr int32 StreakMax = 10;
	/** 冲锋枪：停火 0.15 秒后每秒散多少热；过热锁几秒（10-30：冲锋枪改成持续输出，散热 0.45→0.6、过热锁 2→1 秒） */
	constexpr float HeatCoolPerSecond = 0.6f;
	constexpr float OverheatLock = 1.f;
	/** 充能步枪：射程（命中扫描，厘米）、充能激光每隔多久结算一次伤害 */
	constexpr float BeamRange = 40000.f;
	constexpr float LaserTick = 0.1f;
	/** 充能步枪提前松开（10-30 晚玩家：松手就发射，不撤回）：充能不到 ChargeMinCharge 按最低一发打（满充伤害 / 冲击的 ChargeMinShot），
	 *  之后按充能线性涨到满充；满充 1 秒仍然自动打出。最低一发 230、半充（0.5）约 610、满充 1150 */
	constexpr float ChargeMinCharge = 0.15f;
	constexpr float ChargeMinShot = 0.2f;
	/** 充能 0–1 → 这一发的威力 0.2–1（伤害、冲击、光束粗细亮度都乘它） */
	inline float ChargeShotScale(float InCharge)
	{
		return FMath::Lerp(ChargeMinShot, 1.f, FMath::Clamp((InCharge - ChargeMinCharge) / (1.f - ChargeMinCharge), 0.f, 1.f));
	}

	// ---------------------------------------------------------------- 各把刀的特殊机制（10-30 玩家：双手刀、单手刀、快刀几乎没区别）
	/** 双手刀「化境」（11-02，替换弹反）：首击伤害 x HuajingFirstMult，每打中一下 -HuajingStep，最低 HuajingMinMult，
	 *  HuajingResetTime 秒没打中人恢复满；打中失衡中的敌人当次伤害再 x HuajingDirect（所有制造商统一，无同厂追加）；击退为 0（AMechCharacter::OnHealthHit） */
	constexpr float HuajingFirstMult = 1.4f;
	constexpr float HuajingStep = 0.1f;
	constexpr float HuajingMinMult = 0.7f;
	constexpr float HuajingResetTime = 2.5f;
	constexpr float HuajingDirect = 1.5f;
	/** 快刀「连斩加速」：每次砍中叠一层（最多 FlurryMax），每层出刀快 FlurryHaste、移速 +2.5%（效果 Flurry，见 MechEffects），
	 *  FlurryTime 秒没砍中就清零 */
	constexpr int32 FlurryMax = 5;
	constexpr float FlurryHaste = 0.06f;
	constexpr float FlurryTime = 2.5f;
	/** 单手刀「闪避斩」：快速闪避后 DashSlashWindow 秒内出轻击 = 远距离突进斩（突进距离、伤害、冲击加成） */
	constexpr float DashSlashWindow = 0.5f;
	constexpr float DashSlashRange = 2800.f;
	constexpr float DashSlashDamage = 1.4f;
	constexpr float DashSlashImpact = 1.3f;
	/** 大剑「霸体重斩」：重斩从出招到可取消时刻不吃失衡、不被打断、不被弹反僵直，受到的伤害 ×SuperArmorDamage */
	constexpr float SuperArmorDamage = 0.6f;
	/** 双刀「割裂」：每次砍中给对方挂一层效果 Rend（受到伤害增加，见 MechEffects），RendTime 秒内再砍中就叠层并刷新 */
	constexpr float RendTime = 5.f;
	/** 钝器「破防」：重斩砍在格挡上直接破防（这一下照吃全额），对方 GuardBreakTime 秒不能再挡；轻击走普通弹刀（11-01 晚 用户：钝器太强） */
	constexpr float GuardBreakTime = 1.6f;

	// ---- 拼刀（11-01 用户：「可以做拼刀」「只会讲两人弹开，没有弱势优势一说（匕首除外），弹开的力度也比较大」）：
	//      服务器结算 A 的刀砍中 B 时（UMechCombatComponent::TryBladeClash），B 也在出刀、B 的刀离落下时刻不到 ClashWindow 秒、
	//      两人面对面（各自正面 ±ClashAngle 度）= 拼刀：两刀都作废（不掉血、不积累失衡），两人都被大力弹开，轻重、大剑霸体都不例外。
	//      匕首近身挥刀和别的刀拼：只有匕首方被弹开，对方这一刀也作废，但不弹开、不僵直、能马上行动；匕首对匕首两人都弹开
	//      11-01 晚 用户试过后（「有点频繁，而且不可控」「右键格挡进行弹刀，并且进一步收紧同时弹刀的时间限制」「不一定要精准格挡，以本身达到格挡架势上也会被弹开」）：
	//      ① 格挡架势正面挨刀 = 弹刀（TryBlock 挡住 → SettleClash）：两人弹开、这一刀作废、对方连招断掉；代价照旧（耗能、掉 GuardDamageScale 的血），不积累失衡；
	//         钝器重斩、能量不够破防不弹开（钝器轻击走普通弹刀）；匕首砍格挡只弹匕首方；子弹、飞刀、导弹打在格挡上不弹开。
	//      ② 同时出刀的窗口 0.12 → 0.05 秒（AI 对战里 100 秒 41 次太频繁）
	constexpr float ClashWindow = 0.05f;
	constexpr float ClashAngle = 70.f;
	/** 弹开多远（厘米）：比任何现有的击退都明显（重斩的击退约 3 米），一眼看得出被震飞 */
	constexpr float ClashKnockDistance = 900.f;
	/** 匕首方被弹开多远（厘米）：单独一个数，好单独调 */
	constexpr float ClashKnockDagger = 900.f;
	/** 弹开的速度曲线有多长（秒）：速度 = 3 × 距离 / 时长 × (1 - t / 时长)²，一开始最猛（9 米时 54 米/秒，突进上限 65），之后收住 */
	constexpr float ClashKnockTime = 0.5f;
	/** 僵直（秒，顿帧之后算起）：很短，弹开大头走完马上能动（9 米里约 8.4 米在这段走完）。僵直一结束速度就交还移动组件 */
	constexpr float ClashStun = 0.3f;
	/** 在空中被弹开时竖直速度设成这个（厘米/秒）：不被下落的速度盖过去，看得出是被震开 */
	constexpr float ClashAirLift = 250.f;
	/** 联机：弹开开始后服务器多少秒内不纠正操控者的位置、照他的算（秒，僵直之后再多这么久）。54 米/秒时开始时刻差一帧就差近 1 米，
	 *  异地联机往返 250–350 毫秒，客户端晚 0.15 秒开始弹，不这样会被拉回好几米 */
	constexpr float ClashNetTrust = 0.6f;
	/** 顿帧（秒）：比重击（0.12）略长；ClashStopSafe 是匕首例外里没被弹开的那一方，只顿一下，表示砍到东西了 */
	constexpr float ClashStop = 0.14f;
	constexpr float ClashStopSafe = 0.05f;
	/** 镜头冲击（重击 0.45，破防 0.5）和抖动 */
	constexpr float ClashKick = 0.7f;
	constexpr float ClashShake = 0.4f;
	/** 被弹开时的受击后退动作放多久（秒）：失衡那段 1.2 秒太长，僵直结束后还盖着新出的招 */
	constexpr float ClashRecoilAnim = 0.7f;

	// ---- 同厂加成（11-01 用户：「各个阵营拿自己武器可以得到略微的增强」「buff种类可以多一些」）：框架和这件是同一家造的才生效
	//      （AMechCharacter::IsOwnMaker），每件一种、都不大。配装详情页的文字在 MechCatalog::OwnBuffText，改数值要一起改
	constexpr float OwnStreakWindow = 1.75f;       // 步枪：连中停火后保持 0.6 → 1.05 秒
	constexpr float OwnFalloffShift = 600.f;       // 冲锋枪：满伤距离 18 → 24 米（衰减整段往后挪）
	constexpr float OwnLifesteal = 0.15f;          // 霰弹枪：吸血 30% → 45%
	constexpr float OwnChargeTime = 0.78f;         // 充能步枪：充能 1 → 0.78 秒
	constexpr float OwnSniperEnergy = 20.f / 28.f;  // 狙击枪：每发耗能 28 → 20
	constexpr float OwnFlurryTime = 1.44f;         // 快刀：连斩保持 2.5 → 3.6 秒
	constexpr float OwnDashSlashWindow = 1.6f;     // 单手刀：闪避后 0.5 → 0.8 秒内能出闪避斩
	constexpr float OwnSuperArmorDamage = 0.45f;   // 大剑：霸体重斩受到的伤害 ×0.6 → ×0.45
	constexpr float OwnGuardBreak = 2.5f / 1.6f;   // 钝器：破防 1.6 → 2.5 秒
	constexpr int32 OwnSalvoExtra = 1;             // 导弹：每轮 4 → 5 枚
	constexpr float OwnArtilleryCooldown = 0.775f; // 火炮：冷却 10 → 7.75 秒
	constexpr float OwnMissileCooldown = 7.f / 8.f;  // 导弹：冷却 8 → 7 秒
	constexpr float OwnShieldTime = 4.5f;         // 护盾：持续 3 → 4.5 秒
	constexpr float OwnSmokeCooldown = 0.7f;       // 烟雾弹：冷却 14 → 9.8 秒
	constexpr float OwnEmpExtra = 1.5f;           // EMP：对方失灵 4 → 5.5 秒
	constexpr float OwnBurstDistance = 28.f / 22.f; // 紧急脱离：距离 22 → 28 米
	constexpr float OwnLaunchHeight = 22.f / 16.f;  // 弹射：高度 16 → 22 米
	constexpr float OwnShockRadius = 12.f / 9.f;    // 震荡波：半径 9 → 12 米
	constexpr int32 OwnMaxMines = 3;               // 地雷：同时 2 → 3 颗
	constexpr float OwnMineCooldown = 0.85f;       // 地雷：冷却 10 → 8.5 秒
	constexpr float OwnPistolReload = 1.8f / 2.4f;  // 双持手枪：换弹 2.4 → 1.8 秒（新纪）
	constexpr float OwnRendTime = 1.45f;           // 双刀：割裂持续 5 → 7.25 秒（维克托）
	constexpr float OwnKnifeRegen = 1.15f / 1.6f;   // 投掷匕首：回一把 1.6 → 1.15 秒（维克托）
	constexpr float OwnGrappleCooldown = 5.5f / 7.f;  // 钩爪：冷却 7 → 5.5 秒（新纪）
	constexpr float OwnNetCooldown = 9.f / 12.f;  // 捕获网：冷却 12 → 9 秒（相位）

	int32 NumGuns();
	int32 NumBlades();
	const FGun& Gun(EMechShooter Shooter);
	const FBlade& Blade(EMechMeleeWeapon Melee);
}

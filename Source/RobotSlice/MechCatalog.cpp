#include "MechCatalog.h"
#include "MechMapRules.h"

#include "MechCombatComponent.h"
#include "MechGrenade.h"
#include "MechHealthComponent.h"
#include "MechLoadout.h"
#include "MechMovementComponent.h"
#include "MechNet.h"
#include "MechShoulderComponent.h"
#include "MechSmoke.h"
#include "MechMine.h"
#include "MechTacticalComponent.h"
#include "MechUITheme.h"
#include "MechWeapons.h"
#include "Misc/Paths.h"

namespace MechCatalogPriv  // 不用匿名命名空间：合并编译时会和别的文件里同名的东西撞
{
	using MechCatalog::FInfo;
	using MechCatalog::FStat;

	constexpr EMechMaker NEC = EMechMaker::NewEra;
	constexpr EMechMaker VHI = EMechMaker::Victor;
	constexpr EMechMaker PE = EMechMaker::Phase;
	constexpr EMechMaker HS = EMechMaker::Salvage;
	constexpr EMechHeft L = EMechHeft::Light;
	constexpr EMechHeft M = EMechHeft::Medium;
	constexpr EMechHeft H = EMechHeft::Heavy;
	constexpr EMechHeft VH = EMechHeft::VeryHeavy;

	// ---------------------------------------------------------------- 名单草案（docs/武器名单_草案.md；顺序和各枚举一致）
	// 型号各家有自己的写法：新纪建设纯数字（NEC-12）、维克托重工字母 + 两位数（VHI-R36）、相位电子字母 + 一位数（PHASE-L1）、
	// 回收联合（11-01 起没有东西归它了）。用户给的四个例子 bFromUser = true，其余是草案
	// EMechFrame（10-31 阵营机体进游戏）：企业 × 3 + 档位（中、轻、重）。新纪三台的型号 / 名字沿用 docs/武器名单_草案.md；
	// 维克托、相位的是这次按「企业 + 档位」起的草案：型号照各家写法（维克托字母 + 两位数，数字取机身喷字 VHI-33 / 41 / 512；相位字母 + 一位数），
	// 名字用设计稿里的外号（维克托「斥候 / 铁壁 / 突击（炮）」，相位「双塔 / 单塔 / 门架」——相位设计文档：轻的双塔、中的单塔、重的门架是「塔」的三种变体）
	const FInfo Frames[] = {
		{NEC, TEXT("NEC-07"), TEXT("主力"), TEXT("中型框架"), M, false},
		{NEC, TEXT("NEC-03"), TEXT("穿梭"), TEXT("轻型框架"), L, false},
		{NEC, TEXT("NEC-15"), TEXT("承重"), TEXT("重型框架"), H, false},
		{VHI, TEXT("VHI-F41"), TEXT("铁壁"), TEXT("中型框架"), M, false},
		{VHI, TEXT("VHI-F33"), TEXT("斥候"), TEXT("轻型框架"), L, false},
		{VHI, TEXT("VHI-F51"), TEXT("突击"), TEXT("重型框架"), H, false},
		{PE, TEXT("PHASE-F2"), TEXT("单塔"), TEXT("中型框架"), M, false},
		{PE, TEXT("PHASE-F1"), TEXT("双塔"), TEXT("轻型框架"), L, false},
		{PE, TEXT("PHASE-F3"), TEXT("门架"), TEXT("重型框架"), H, false},
	};
	static_assert(UE_ARRAY_COUNT(Frames) == MechNumFrames, "名单要和框架枚举一致");

	// 九台的外形（lookdev/rig/factions/manifest.json：喷口 = 主推进器喇叭口外沿中心和喷射方向，UE 网格坐标厘米，左边那个；头部限位；
	// 已知穿模里相位中 / 重的靴口前沿在脚尖上翘时碰脚背 → 踝上翘上限收到 15 度）。顺序同 EMechFrame。
	// 新纪轻型 = 现行 v3（资源名沿用 SKM_Robot_v3，10-31 用 NEC_L.fbx 覆盖重导）；维克托三台和 v3 是同一个喷口
	const FVector V3Mouth(6.15f, -20.47f, 115.65f);
	const FVector V3Dir(0.078f, -0.470f, -0.879f);
	const MechCatalog::FFrameBody Bodies[] = {
		{TEXT("NEC_M"), TEXT("/Game/Robot/Factions/SKM_NEC_M"), FVector(7.41f, -20.67f, 108.29f), FVector(0.f, -0.447f, -0.894f), 3.7f, 0.f, 0.f, 35.f, false},
		{TEXT("NEC_L"), TEXT("/Game/Robot/Meshes/SKM_Robot_v3"), V3Mouth, V3Dir, 4.1f, 0.f, 0.f, 35.f, false},
		{TEXT("NEC_H"), TEXT("/Game/Robot/Factions/SKM_NEC_H"), FVector(8.89f, -22.17f, 106.28f), FVector(0.f, -0.410f, -0.912f), 4.1f, 0.f, 0.f, 35.f, false},
		{TEXT("VHI_M"), TEXT("/Game/Robot/Factions/SKM_VHI_M"), V3Mouth, V3Dir, 4.1f, 25.f, 15.f, 35.f, true},
		{TEXT("VHI_L"), TEXT("/Game/Robot/Factions/SKM_VHI_L"), V3Mouth, V3Dir, 4.1f, 0.f, 0.f, 35.f, true},
		{TEXT("VHI_H"), TEXT("/Game/Robot/Factions/SKM_VHI_H"), V3Mouth, V3Dir, 4.1f, 25.f, 15.f, 35.f, true},
		{TEXT("PHASE_M"), TEXT("/Game/Robot/Factions/SKM_PHASE_M"), FVector(6.57f, -20.07f, 115.39f), FVector(0.080f, -0.470f, -0.879f), 3.6f, 0.f, 0.f, 15.f, false},
		{TEXT("PHASE_L"), TEXT("/Game/Robot/Factions/SKM_PHASE_L"), FVector(8.90f, -18.29f, 115.07f), FVector(0.101f, -0.404f, -0.909f), 4.6f, 0.f, 0.f, 35.f, false},
		{TEXT("PHASE_H"), TEXT("/Game/Robot/Factions/SKM_PHASE_H"), FVector(8.13f, -19.98f, 112.16f), FVector(0.060f, -0.422f, -0.905f), 4.2f, 0.f, 0.f, 15.f, false},
	};
	static_assert(UE_ARRAY_COUNT(Bodies) == MechNumFrames, "外形表要和框架枚举一致");
	const FInfo Boosters[] = {  // 均衡、地面、飞行
		{VHI, TEXT("VHI-J40"), TEXT("标配"), TEXT("均衡型推进器"), M, false},
		{NEC, TEXT("NEC-88"), TEXT("贴地"), TEXT("地面型推进器"), H, false},
		{PE, TEXT("PHASE-J1"), TEXT("升空"), TEXT("飞行型推进器"), L, false},
	};
	const FInfo Shooters[] = {
		{VHI, TEXT("VHI-R36"), TEXT("直线"), TEXT("连射步枪"), M, true},
		{VHI, TEXT("VHI-C22"), TEXT("扫街"), TEXT("冲锋枪"), L, false},
		{NEC, TEXT("NEC-12"), TEXT("破门"), TEXT("霰弹枪"), H, true},
		{PE, TEXT("PHASE-L1"), TEXT("针"), TEXT("充能步枪"), H, true},
		{PE, TEXT("PHASE-S3"), TEXT("定点"), TEXT("狙击枪"), VH, false},
		{NEC, TEXT("NEC-09"), TEXT("一对"), TEXT("双持手枪"), L, true},  // 11-01 用户：回收联合的东西分给三家公司（下面四件同）
	};
	const FInfo Melees[] = {
		{PE, TEXT("PHASE-K1"), TEXT("镜面"), TEXT("化境"), M, false},
		{VHI, TEXT("VHI-B08"), TEXT("快马"), TEXT("快刀"), L, false},
		{PE, TEXT("PHASE-K3"), TEXT("一闪"), TEXT("单手刀"), L, false},
		{NEC, TEXT("NEC-40"), TEXT("开山"), TEXT("大剑"), VH, false},
		{VHI, TEXT("VHI-B14"), TEXT("剪子"), TEXT("双刀"), L, false},
		{NEC, TEXT("NEC-25"), TEXT("打桩"), TEXT("钝器"), VH, false},
		{VHI, TEXT("VHI-D06"), TEXT("顺手"), TEXT("投掷匕首"), L, false},
	};
	const FInfo Shoulders[] = {  // EMechShoulder：导弹、火炮、护盾、钩爪、网、烟雾、EMP、紧急脱离、弹射升空、震荡波、地雷（都是浮空无人机）
		{VHI, TEXT("VHI-M44"), TEXT("齐射"), TEXT("导弹无人机"), M, false},
		{NEC, TEXT("NEC-60"), TEXT("落锤"), TEXT("火炮无人机"), H, false},
		{NEC, TEXT("NEC-30"), TEXT("门板"), TEXT("护盾无人机"), H, false},
		{NEC, TEXT("NEC-33"), TEXT("够着"), TEXT("钩爪无人机"), M, false},
		{PE, TEXT("PHASE-N4"), TEXT("兜住"), TEXT("捕获网无人机"), M, false},
		{VHI, TEXT("VHI-S16"), TEXT("障眼"), TEXT("烟雾弹无人机"), L, false},
		{PE, TEXT("PHASE-E2"), TEXT("断电"), TEXT("EMP 无人机"), L, false},
		// 11-01 原来的战术装备并进来：型号 / 名字是 10-31 合并时起的草案，等用户审。分给哪家照企业设定的风格：
		// 相位电子（高机动）出脱离推进、维克托重工（均衡）出弹射和震荡波、新纪建设（工程机械，爆破）出地雷
		{PE, TEXT("PHASE-T1"), TEXT("抽身"), TEXT("紧急脱离无人机"), L, false},
		{VHI, TEXT("VHI-T20"), TEXT("拔地"), TEXT("弹射无人机"), M, false},
		{VHI, TEXT("VHI-T35"), TEXT("清场"), TEXT("震荡波无人机"), M, false},
		{NEC, TEXT("NEC-45"), TEXT("爆破"), TEXT("布雷无人机"), L, false},
	};
	static_assert(UE_ARRAY_COUNT(Shooters) == static_cast<int32>(EMechShooter::DualPistol) + 1, "名单要和射击武器枚举一致");
	static_assert(UE_ARRAY_COUNT(Melees) == static_cast<int32>(EMechMeleeWeapon::Dagger) + 1, "名单要和近战武器枚举一致");
	static_assert(UE_ARRAY_COUNT(Shoulders) == static_cast<int32>(EMechShoulder::Mine) + 1, "名单要和肩部装备枚举一致");

	const FInfo Unknown = {HS, TEXT("—"), TEXT("未命名"), TEXT(""), M, false};

	template <int32 N>
	const FInfo& Pick(const FInfo (&Table)[N], int32 Index)
	{
		return Index >= 0 && Index < N ? Table[Index] : Unknown;
	}

	// ---------------------------------------------------------------- 抄来的数值（出处在旁边，改那边要一起改）
	// 近战招式基准（UMechCombatComponent.cpp 的 MeleeSpecs，双手刀）：四段轻击伤害 380 + 380 + 420 + 650、重斩 1400；
	// 连按四段的时长 = 前三段可取消时刻 0.36 + 0.28 + 0.30 + 第四段整招 0.80
	constexpr float ComboDamage = 380.f + 380.f + 420.f + 650.f;
	constexpr float HeavyDamage = 1400.f;
	constexpr float ComboTime = 0.36f + 0.28f + 0.30f + 0.80f;
	// 导弹（UMechCombatComponent 的 Mech|Missile 属性默认值）
	constexpr int32 MissileCount = 4;
	constexpr float MissileDamage = 180.f;
	constexpr float MissileImpact = 180.f;
	constexpr float MissileCooldown = 8.f;
	constexpr float MissileRadius = 380.f;
	// 炮弹最长飞 2.5 秒、烟雾弹 / EMP 4 秒（AMechGrenade::Tick）；网最长飞 AMechNet::MaxFlight
	constexpr float ShellFlight = 2.5f;

	FString Num(float V)
	{
		return FString::Printf(TEXT("%d"), FMath::RoundToInt(V));
	}

	FString Meters(float Cm)
	{
		const float Mtr = Cm / 100.f;
		return Mtr < 10.f ? FString::Printf(TEXT("%.1f 米"), Mtr) : FString::Printf(TEXT("%d 米"), FMath::RoundToInt(Mtr));
	}

	FString Seconds(float S)
	{
		return FMath::IsNearlyEqual(S, FMath::RoundToFloat(S), 0.01f) ? FString::Printf(TEXT("%d 秒"), FMath::RoundToInt(S)) : FString::Printf(TEXT("%.3g 秒"), S);
	}

	FString Times(float X)
	{
		return FString::Printf(TEXT("×%.3g"), X);
	}

	FString Percent(float Scale)
	{
		const int32 P = FMath::RoundToInt((Scale - 1.f) * 100.f);
		return P == 0 ? FString(TEXT("标准")) : FString::Printf(TEXT("%+d%%"), P);
	}

	void Add(TArray<FStat>& Out, const FText& Label, float Raw, const FString& Value, bool bSqrt = false)
	{
		FStat S;
		S.Label = Label;
		S.Raw = Raw;
		S.Value = Value;
		S.bSqrt = bSqrt;
		Out.Add(S);
	}

	/** 还没归一化的参数行 */
	void RawStats(int32 Category, int32 Index, TArray<FStat>& Out)
	{
		Out.Reset();
		if (Category == MechLoadout::Shooter && Index >= 0 && Index < MechWeapons::NumGuns())
		{
			const MechWeapons::FGun& G = MechWeapons::Gun(static_cast<EMechShooter>(Index));
			const float Shot = G.Damage * G.Pellets;
			// 一个循环：充能步枪 = 充能 + 间隔；其余 = 间隔
			const float Cycle = G.Interval + G.ChargeTime;
			Add(Out, INVTEXT("单发伤害"), Shot, G.Pellets > 1 ? FString::Printf(TEXT("%s × %d"), *Num(G.Damage), G.Pellets) : Num(Shot), true);
			Add(Out, INVTEXT("每秒伤害"), Shot / FMath::Max(Cycle, 0.01f), Num(Shot / FMath::Max(Cycle, 0.01f)) + TEXT("（不算换弹）"));
			Add(Out, INVTEXT("射速"), 1.f / FMath::Max(Cycle, 0.01f), G.ChargeTime > 0.f ? FString::Printf(TEXT("充能 %s"), *Seconds(G.ChargeTime))
				: FString::Printf(TEXT("%.1f 发/秒"), 1.f / FMath::Max(Cycle, 0.01f)));
			// 弹匣：冲锋枪按过热前能打几发、狙击枪按满能量能打几发
			if (G.Magazine > 0)
			{
				Add(Out, INVTEXT("弹匣"), G.Magazine, Num(G.Magazine));
			}
			else if (G.HeatPerShot > 0.f)
			{
				Add(Out, INVTEXT("弹匣"), 1.f / G.HeatPerShot, FString::Printf(TEXT("不用换弹，%d 发过热"), FMath::FloorToInt(1.f / G.HeatPerShot)));
			}
			else if (G.EnergyPerShot > 0.f)
			{
				const float MaxEnergy = GetDefault<UMechMovementComponent>()->MaxEnergy;
				Add(Out, INVTEXT("弹匣"), MaxEnergy / G.EnergyPerShot, FString::Printf(TEXT("不用弹药，每发 %s 能量"), *Num(G.EnergyPerShot)));
			}
			const float Range = G.ChargeTime > 0.f ? MechWeapons::BeamRange : G.BulletSpeed * G.BulletLife;
			Add(Out, INVTEXT("射程"), Range, G.FalloffEnd > 0.f ? FString::Printf(TEXT("%s（%s 外衰减）"), *Meters(Range), *Meters(G.FalloffStart)) : Meters(Range));
			Add(Out, INVTEXT("换弹"), G.ReloadTime, G.ReloadTime > 0.f ? Seconds(G.ReloadTime) : FString(TEXT("—")));
			Add(Out, INVTEXT("冲击"), G.Impact * G.Pellets, G.Pellets > 1 ? FString::Printf(TEXT("%s × %d"), *Num(G.Impact), G.Pellets) : Num(G.Impact), true);
			return;
		}
		if (Category == MechLoadout::Melee && Index >= 0 && Index < MechWeapons::NumBlades())
		{
			const MechWeapons::FBlade& B = MechWeapons::Blade(static_cast<EMechMeleeWeapon>(Index));
			Add(Out, INVTEXT("四段连击"), ComboDamage * B.DamageScale, Num(ComboDamage * B.DamageScale));
			Add(Out, INVTEXT("重斩"), HeavyDamage * B.DamageScale, Num(HeavyDamage * B.DamageScale));
			Add(Out, INVTEXT("出刀速度"), 1.f / FMath::Max(B.TimeScale, 0.1f), FString::Printf(TEXT("四段 %.1f 秒"), ComboTime * B.TimeScale));
			Add(Out, INVTEXT("攻击距离"), B.Reach, Meters(B.Reach));  // FBlade::Reach 已经是机体尺寸
			Add(Out, INVTEXT("削失衡"), B.ImpactScale * B.StaggerScale, Times(B.ImpactScale * B.StaggerScale));
			Add(Out, INVTEXT("冲击"), B.ImpactScale, Times(B.ImpactScale));
			if (B.Knives > 0)
			{
				Add(Out, INVTEXT("飞刀"), B.KnifeDamage, FString::Printf(TEXT("%s × %d 把，%s 回一把"), *Num(B.KnifeDamage), B.Knives, *Seconds(B.KnifeRegen)));
			}
			return;
		}
		if (Category == MechLoadout::Shoulder)
		{
			using S = UMechShoulderComponent;
			using T = UMechTacticalComponent;
			const float LockRange = GetDefault<UMechCombatComponent>()->GetLockRange();
			switch (static_cast<EMechShoulder>(Index))
			{
			case EMechShoulder::Missiles:
				Add(Out, INVTEXT("伤害"), MissileDamage * MissileCount, FString::Printf(TEXT("%s × %d"), *Num(MissileDamage), MissileCount), true);
				Add(Out, INVTEXT("冲击"), MissileImpact * MissileCount, FString::Printf(TEXT("%s × %d"), *Num(MissileImpact), MissileCount), true);
				Add(Out, INVTEXT("冷却"), MissileCooldown, Seconds(MissileCooldown));
				Add(Out, INVTEXT("射程"), LockRange, FString::Printf(TEXT("锁定 %s"), *Meters(LockRange)));
				Add(Out, INVTEXT("范围"), MissileRadius, FString::Printf(TEXT("半径 %s"), *Meters(MissileRadius)));
				Add(Out, INVTEXT("控制"), 0.f, TEXT("—"));
				break;
			case EMechShoulder::Artillery:
				Add(Out, INVTEXT("伤害"), AMechGrenade::ShellDamage, Num(AMechGrenade::ShellDamage), true);
				Add(Out, INVTEXT("冲击"), AMechGrenade::ShellImpact, Num(AMechGrenade::ShellImpact) + TEXT("（基本一发失衡）"), true);
				Add(Out, INVTEXT("冷却"), S::ArtilleryCooldown, Seconds(S::ArtilleryCooldown));
				Add(Out, INVTEXT("射程"), S::ShellSpeed * ShellFlight, FString::Printf(TEXT("%s，直线弹道"), *Meters(S::ShellSpeed * ShellFlight)));
				Add(Out, INVTEXT("范围"), AMechGrenade::ShellRadius, FString::Printf(TEXT("半径 %s"), *Meters(AMechGrenade::ShellRadius)));
				Add(Out, INVTEXT("控制"), 0.f, FString::Printf(TEXT("架炮 %s"), *Seconds(S::ArtilleryDeployTime)));
				break;
			case EMechShoulder::Shield:
				Add(Out, INVTEXT("伤害"), 0.f, TEXT("—"), true);
				Add(Out, INVTEXT("冲击"), 0.f, TEXT("—"), true);
				Add(Out, INVTEXT("冷却"), S::ShieldCooldown, Seconds(S::ShieldCooldown));
				Add(Out, INVTEXT("射程"), 0.f, TEXT("身前"));
				Add(Out, INVTEXT("范围"), S::ShieldRadius, FString::Printf(TEXT("正面 %d°"), FMath::RoundToInt(S::ShieldAngle)));
				Add(Out, INVTEXT("控制"), S::ShieldDuration, FString::Printf(TEXT("挡 %s"), *Seconds(S::ShieldDuration)));
				break;
			case EMechShoulder::Grapple:
				Add(Out, INVTEXT("伤害"), S::GrappleDamage, Num(S::GrappleDamage), true);
				Add(Out, INVTEXT("冲击"), S::GrappleImpact, Num(S::GrappleImpact), true);
				Add(Out, INVTEXT("冷却"), S::GrappleCooldown, FString::Printf(TEXT("%s（没抓到 %s）"), *Seconds(S::GrappleCooldown), *Seconds(S::GrappleMissCooldown)));
				Add(Out, INVTEXT("射程"), S::GrappleRange, Meters(S::GrappleRange));
				Add(Out, INVTEXT("范围"), 0.f, TEXT("单体"));
				Add(Out, INVTEXT("控制"), 0.f, TEXT("拉到身前"));
				break;
			case EMechShoulder::Net:
				Add(Out, INVTEXT("伤害"), S::NetDamage, Num(S::NetDamage), true);
				Add(Out, INVTEXT("冲击"), 100.f, Num(100.f), true);  // AMechNet 罩住时的冲击
				Add(Out, INVTEXT("冷却"), S::NetCooldown, Seconds(S::NetCooldown));
				Add(Out, INVTEXT("射程"), S::NetSpeed * AMechNet::MaxFlight, Meters(S::NetSpeed * AMechNet::MaxFlight));
				Add(Out, INVTEXT("范围"), 225.f, TEXT("张开约 4.5 米"));
				Add(Out, INVTEXT("控制"), S::NetDuration, FString::Printf(TEXT("定身 %s"), *Seconds(S::NetDuration)));
				break;
			case EMechShoulder::Smoke:
				Add(Out, INVTEXT("伤害"), 0.f, TEXT("—"), true);
				Add(Out, INVTEXT("冲击"), 0.f, TEXT("—"), true);
				Add(Out, INVTEXT("冷却"), S::GrenadeCooldown, Seconds(S::GrenadeCooldown));
				Add(Out, INVTEXT("射程"), S::GrenadeRange, Meters(S::GrenadeRange));
				Add(Out, INVTEXT("范围"), AMechSmoke::Radius, FString::Printf(TEXT("半径 %s"), *Meters(AMechSmoke::Radius)));
				Add(Out, INVTEXT("控制"), AMechSmoke::Duration, FString::Printf(TEXT("烟幕 %s"), *Seconds(AMechSmoke::Duration)));
				break;
			// 11-01 战术装备并入肩部：行名改成和其他肩部装备同一套（条长按同一类同名的行归一化）。
			// 「控制」那行的值是一句说明，Raw 给 0：别的件这行是秒数，给距离会把它们的条全压扁。数值是 UMechTacticalComponent / AMechMine 的常量
			case EMechShoulder::Burst:
				Add(Out, INVTEXT("伤害"), 0.f, TEXT("—"), true);
				Add(Out, INVTEXT("冲击"), 0.f, TEXT("—"), true);
				Add(Out, INVTEXT("冷却"), T::BurstCooldown, Seconds(T::BurstCooldown));
				Add(Out, INVTEXT("射程"), T::BurstDistance, FString::Printf(TEXT("冲出 %s（%s）"), *Meters(T::BurstDistance), *Seconds(T::BurstDistance / T::BurstSpeed)));
				Add(Out, INVTEXT("范围"), 0.f, TEXT("—"));
				Add(Out, INVTEXT("控制"), 0.f, TEXT("不看能量、过热也能用，冲完能量清零"));
				break;
			case EMechShoulder::Launch:
				Add(Out, INVTEXT("伤害"), 0.f, TEXT("—"), true);
				Add(Out, INVTEXT("冲击"), 0.f, TEXT("—"), true);
				Add(Out, INVTEXT("冷却"), T::LaunchCooldown, Seconds(T::LaunchCooldown));
				Add(Out, INVTEXT("射程"), T::LaunchHeight, FString::Printf(TEXT("往上 %s"), *Meters(T::LaunchHeight)));
				Add(Out, INVTEXT("范围"), 0.f, TEXT("—"));
				Add(Out, INVTEXT("控制"), 0.f, FString::Printf(TEXT("%s 弹到顶，之后照常滑翔"), *Seconds(T::LaunchDuration)));
				break;
			case EMechShoulder::Shockwave:
				Add(Out, INVTEXT("伤害"), T::ShockDamage, FString::Printf(TEXT("%s（边上六成）"), *Num(T::ShockDamage)), true);
				Add(Out, INVTEXT("冲击"), T::ShockImpact, Num(T::ShockImpact) + TEXT("（震开、打断出刀）"), true);
				Add(Out, INVTEXT("冷却"), T::ShockCooldown, Seconds(T::ShockCooldown));
				Add(Out, INVTEXT("射程"), 0.f, TEXT("以自己为中心"));
				Add(Out, INVTEXT("范围"), T::ShockRadius, FString::Printf(TEXT("半径 %s"), *Meters(T::ShockRadius)));
				Add(Out, INVTEXT("控制"), 0.f, FString::Printf(TEXT("蓄力 %s"), *Seconds(T::ShockCharge)));
				break;
			case EMechShoulder::Mine:
				Add(Out, INVTEXT("伤害"), AMechMine::BlastDamage, FString::Printf(TEXT("%s（边上四成）"), *Num(AMechMine::BlastDamage)), true);
				Add(Out, INVTEXT("冲击"), AMechMine::BlastImpact, Num(AMechMine::BlastImpact), true);
				Add(Out, INVTEXT("冷却"), T::MineCooldown, Seconds(T::MineCooldown));
				Add(Out, INVTEXT("射程"), T::MineRange, FString::Printf(TEXT("扔出最远 %s"), *Meters(T::MineRange)));
				Add(Out, INVTEXT("范围"), AMechMine::BlastRadius, FString::Printf(TEXT("半径 %s，%s 内触发"), *Meters(AMechMine::BlastRadius), *Meters(AMechMine::TriggerRadius)));
				Add(Out, INVTEXT("控制"), 0.f, FString::Printf(TEXT("最多 %d 颗，%d 耐久"), T::MaxMines, FMath::RoundToInt(AMechMine::MaxHP)));
				break;
			default:  // EMP
				Add(Out, INVTEXT("伤害"), AMechGrenade::EmpDamage, Num(AMechGrenade::EmpDamage), true);
				Add(Out, INVTEXT("冲击"), AMechGrenade::EmpImpact, Num(AMechGrenade::EmpImpact), true);
				Add(Out, INVTEXT("冷却"), S::GrenadeCooldown, Seconds(S::GrenadeCooldown));
				Add(Out, INVTEXT("射程"), S::GrenadeRange, Meters(S::GrenadeRange));
				Add(Out, INVTEXT("范围"), AMechGrenade::EmpRadius, FString::Printf(TEXT("半径 %s"), *Meters(AMechGrenade::EmpRadius)));
				Add(Out, INVTEXT("控制"), AMechGrenade::EmpDuration, FString::Printf(TEXT("断能 %s"), *Seconds(AMechGrenade::EmpDuration)));
				break;
			}
			return;
		}
		if (Category == MechLoadout::Frame)
		{
			const MechCatalog::FFrameSpec F = MechCatalog::FrameSpec(static_cast<EMechFrame>(FMath::Clamp(Index, 0, MechNumFrames - 1)));
			Add(Out, INVTEXT("装甲"), F.AP, Num(F.AP));
			Add(Out, INVTEXT("失衡上限"), F.Stagger, Num(F.Stagger));
			Add(Out, INVTEXT("移动速度"), F.Speed, Times(F.Speed));
			Add(Out, INVTEXT("被击退"), F.Knockback, Times(F.Knockback));
			Add(Out, INVTEXT("能量回复"), F.Regen, Percent(F.Regen));
			return;
		}
		if (Category == MechLoadout::Booster)
		{
			const MechCatalog::FBoosterSpec B = MechCatalog::BoosterSpec(static_cast<EMechBooster>(FMath::Clamp(Index, 0, 2)));
			const float MaxEnergy = GetDefault<UMechMovementComponent>()->MaxEnergy;
			Add(Out, INVTEXT("地面冲刺"), B.Boost, FString::Printf(TEXT("%d km/h"), FMath::RoundToInt(B.Boost * 0.036f)));
			Add(Out, INVTEXT("快速闪避"), B.Quick, FString::Printf(TEXT("%d km/h"), FMath::RoundToInt(B.Quick * 0.036f)));
			Add(Out, INVTEXT("闪避次数"), MaxEnergy / FMath::Max(B.QuickCost, 1.f),
				FString::Printf(TEXT("每次耗 %s，冷却 %s"), *Num(B.QuickCost), *Seconds(B.QuickCooldown)));
			Add(Out, INVTEXT("上升"), B.bCanThrust ? B.MaxRise : 0.f, B.bCanThrust ? FString::Printf(TEXT("%.1f 米/秒"), B.MaxRise / 100.f) : FString(TEXT("不能飞")));
			Add(Out, INVTEXT("空中滑翔"), B.bCanAirBoost ? B.AirBoost * (1.f - B.AirGravity) : 0.f,
				B.bCanAirBoost ? FString::Printf(TEXT("推力 %d，下坠 ×%.2g"), FMath::RoundToInt(B.AirBoost / 100.f), B.AirGravity) : FString(TEXT("不能")));
			Add(Out, INVTEXT("回能"), B.GroundRegen + B.AirRegen, FString::Printf(TEXT("地面 ×%.2g，空中 ×%.2g"), B.GroundRegen, B.AirRegen));
			return;
		}
	}
}

// ---------------------------------------------------------------- 制造商
const MechCatalog::FMaker& MechCatalog::Maker(EMechMaker M)
{
	using MechUITheme::Srgb;
	// 名称、理念、竞技风格照 docs/世界观/企业设定.md v3（用户原文）；颜色 = 占位标识的品牌色（docs/世界观/标识/占位/*_品牌色.svg）；
	// 标识（斜体缩写剪影，占位）：Content/Slate/Makers/<Logo>_brand.svg / _white.svg，宽高比按 SVG 的 viewBox
	static const FMaker Makers[] = {
		{TEXT("新纪建设"), TEXT("New Era Consolidated"), TEXT("NEC"), TEXT("我们建设明天。"),
			TEXT("重视耐久、结构强度与成本效益：大剑、钝器、霰弹枪、双持手枪、火炮、护盾、钩爪"), false, Srgb(0xEE, 0x8A, 0x1C), TEXT("NEC"), 4390.f / 1534.f},
		{TEXT("维克托重工"), TEXT("Victor Heavy Industries"), TEXT("VHI"), TEXT("值得信赖的力量。"),
			TEXT("火力、装甲与机动的均衡设计：步枪、冲锋枪、快刀、双刀、匕首、导弹、烟雾弹"), false, Srgb(0xC2, 0x3A, 0x2E), TEXT("VHI"), 3621.f / 1534.f},
		{TEXT("相位电子"), TEXT("Phase Electronics Corporation"), TEXT("PHASE"), TEXT("使未来成为可能。"),
			TEXT("精密控制、高机动与先进电子系统：充能步枪、狙击枪、化境、单手刀、EMP、捕获网"), false, Srgb(0x3C, 0xC8, 0xDC), TEXT("PHASE"), 7021.f / 1534.f},
		{TEXT("回收联合"), TEXT(""), TEXT("HS"), TEXT(""),
			TEXT("暂不出场、名称待定（11-01 它的武器已分给三家公司）"), true, Srgb(198, 112, 78), TEXT(""), 1.f},
	};
	return Makers[FMath::Clamp(static_cast<int32>(M), 0, int32(UE_ARRAY_COUNT(Makers)) - 1)];
}

const MechCatalog::FInfo& MechCatalog::Info(int32 Category, int32 Index)
{
	using namespace MechCatalogPriv;
	switch (Category)
	{
	case MechLoadout::Frame:
		return Pick(Frames, Index);
	case MechLoadout::Booster:
		if (MechMapRules::IsBoosterLocked())
		{
			// 11-02 石林：配装页这一格是专用的飞行器，人人一样
			static const FInfo StoneForest = {EMechMaker::Phase, TEXT("PHASE-SL1"), TEXT("石林"), TEXT("石林飞行器"), Boosters[2].Heft, false};
			return StoneForest;
		}
		return Pick(Boosters, Index);
	case MechLoadout::Shooter:
		return Pick(Shooters, Index);
	case MechLoadout::Melee:
		return Pick(Melees, Index);
	case MechLoadout::Shoulder:
		return Pick(Shoulders, Index);
	default:
		return Unknown;
	}
}

FString MechCatalog::Quoted(int32 Category, int32 Index)
{
	return FString::Printf(TEXT("「%s」"), Info(Category, Index).Title);
}

FString MechCatalog::FullName(int32 Category, int32 Index)
{
	const FInfo& I = Info(Category, Index);
	return FString::Printf(TEXT("%s %s「%s」%s"), Maker(I.Maker).Name, I.Model, I.Title, I.Kind);
}

const TCHAR* MechCatalog::HeftName(EMechHeft Heft)
{
	switch (Heft)
	{
	case EMechHeft::Light:
		return TEXT("轻");
	case EMechHeft::Heavy:
		return TEXT("重");
	case EMechHeft::VeryHeavy:
		return TEXT("很重");
	default:
		return TEXT("中");
	}
}

// ---------------------------------------------------------------- 参数
void MechCatalog::Stats(int32 Category, int32 Index, TArray<FStat>& Out)
{
	using namespace MechCatalogPriv;
	RawStats(Category, Index, Out);
	const int32 NumOwn = Out.Num();
	// 同一类里每一行的最大值（按行名对上；投掷匕首多一行「飞刀」）
	TMap<FString, float> Max;
	TArray<FStat> Other;
	for (int32 i = 0; i < MechLoadout::NumItems(Category); ++i)
	{
		RawStats(Category, i, Other);
		for (const FStat& S : Other)
		{
			float& Mx = Max.FindOrAdd(S.Label.ToString());
			Mx = FMath::Max(Mx, S.bSqrt ? FMath::Sqrt(FMath::Max(S.Raw, 0.f)) : S.Raw);
		}
	}
	for (FStat& S : Out)
	{
		const float Mx = Max.FindRef(S.Label.ToString());
		const float V = S.bSqrt ? FMath::Sqrt(FMath::Max(S.Raw, 0.f)) : S.Raw;
		S.Bar = Mx > KINDA_SMALL_NUMBER ? FMath::Clamp(V / Mx, 0.f, 1.f) : 0.f;
	}
	// 同厂加成（11-01）：只是一行字，不画条
	if (const TCHAR* Buff = OwnBuffText(Category, Index))
	{
		Add(Out, INVTEXT("同厂加成"), 0.f, FString::Printf(TEXT("配%s框架：%s"), Maker(Info(Category, Index).Maker).Name, Buff));
		Out[NumOwn].Bar = 0.f;
	}
}

bool MechCatalog::SameMaker(const FMechLoadout& Loadout, int32 Category)
{
	int32 Index = 0;
	switch (Category)
	{
	case MechLoadout::Shooter:
		Index = static_cast<int32>(Loadout.Shooter);
		break;
	case MechLoadout::Melee:
		Index = static_cast<int32>(Loadout.Melee);
		break;
	case MechLoadout::Shoulder:
		Index = static_cast<int32>(Loadout.Shoulder);
		break;
	default:
		return false;
	}
	return Info(Category, Index).Maker == FrameMaker(static_cast<int32>(Loadout.Frame));
}

const TCHAR* MechCatalog::OwnBuffText(int32 Category, int32 Index)
{
	// 顺序和配装枚举一致；数值在 MechWeapons.h 的 Own*
	static const TCHAR* const Shooters[] = {TEXT("连中停火后保持 0.6 → 1.05 秒"), TEXT("满伤距离 18 → 24 米"), TEXT("吸血 30% → 45%"), TEXT("充能 1 → 0.78 秒"), TEXT("每发耗能 28 → 20"), TEXT("换弹 2.4 → 1.8 秒")};
	static const TCHAR* const Melees[] = {nullptr, TEXT("连斩保持 2.5 → 3.6 秒"), TEXT("闪避后 0.5 → 0.8 秒内能出闪避斩"), TEXT("霸体重斩受到的伤害 ×0.6 → ×0.45"), TEXT("割裂持续 5 → 7.25 秒"), TEXT("破防 1.6 → 2.5 秒"), TEXT("回一把 1.6 → 1.15 秒")};
	static const TCHAR* const Shoulders[] = {TEXT("每轮 4 → 5 枚，冷却 8 → 7 秒"), TEXT("冷却 10 → 7.75 秒"), TEXT("持续 3 → 4.5 秒"), TEXT("冷却 7 → 5.5 秒"), TEXT("冷却 12 → 9 秒"), TEXT("冷却 14 → 9.8 秒"), TEXT("对方失灵 4 → 5.5 秒"),
		TEXT("距离 22 → 28 米"), TEXT("高度 16 → 22 米"), TEXT("半径 9 → 12 米"), TEXT("同时 2 → 3 颗，冷却 10 → 8.5 秒")};
	auto Pick = [Index](const TCHAR* const* Table, int32 Num) -> const TCHAR* { return Index >= 0 && Index < Num ? Table[Index] : nullptr; };
	switch (Category)
	{
	case MechLoadout::Shooter:
		return Pick(Shooters, UE_ARRAY_COUNT(Shooters));
	case MechLoadout::Melee:
		return Pick(Melees, UE_ARRAY_COUNT(Melees));
	case MechLoadout::Shoulder:
		return Pick(Shoulders, UE_ARRAY_COUNT(Shoulders));
	default:
		return nullptr;
	}
}

MechCatalog::FFrameSpec MechCatalog::FrameSpec(EMechFrame Frame)
{
	// AMechCharacter::ApplyLoadout 就按这个设。先按档位（10-30 装甲 ×0.85 之后的值），维克托重工 = 档位原样（均衡）
	FFrameSpec S = {};
	switch (MechFrameTier(Frame))
	{
	case EMechFrameTier::Light:
		S = {7650.f, 1150.f, 1.12f, 1.2f, 1.3f};
		break;
	case EMechFrameTier::Heavy:
		S = {13200.f, 1750.f, 0.88f, 0.85f, 0.6f};
		break;
	default:
	{
		const UMechHealthComponent* H = GetDefault<UMechHealthComponent>();
		S = {H->MaxAP, H->MaxStagger, 1.f, 1.f, 1.f};
		break;
	}
	}
	// 10-31 用户：三家框架各有特色（企业设定：新纪重耐久 / 结构强度，维克托火力装甲机动均衡、可靠，相位精密控制 / 高机动 / 电子系统）。
	// 装甲取整到 100、失衡上限取整到 10
	auto ScaleBy = [&S](float AP, float Stagger, float Speed, float Regen, float Knockback)
	{
		S.AP = FMath::RoundToFloat(S.AP * AP / 100.f) * 100.f;
		S.Stagger = FMath::RoundToFloat(S.Stagger * Stagger / 10.f) * 10.f;
		S.Speed *= Speed;
		S.Regen *= Regen;
		S.Knockback *= Knockback;
	};
	switch (FrameMaker(static_cast<int32>(Frame)))
	{
	case EMechMaker::NewEra:  // 耐用：更硬、更站得住，慢一点；现场抢修
		ScaleBy(1.06f, 1.15f, 0.97f, 0.92f, 0.85f);
		S.bRepair = true;
		break;
	case EMechMaker::Victor:  // 可靠：数值不变，弹匣 +20%；抗干扰（被网、中 EMP、失衡僵直的时间都 −35%）
		S.Magazine = 1.2f;
		S.Disrupt = 0.65f;
		S.Emp = 0.65f;
		break;
	case EMechMaker::Phase:  // 精密：更快更脆，闪避省能量；导弹锁定快 30%、散布 −25%；代价：中 EMP 的时间 +50%
		ScaleBy(0.94f, 0.9f, 1.04f, 1.f, 1.f);
		S.QuickCost = 0.85f;
		S.Lock = 0.7f;
		S.Spread = 0.75f;
		S.Emp = 1.5f;
		break;
	default:
		break;
	}
	return S;
}

MechCatalog::FBoosterSpec MechCatalog::BoosterSpec(EMechBooster Booster)
{
	// 和 AMechCharacter::ApplyLoadout 的推进器那段一致：从移动组件默认值（均衡型）起，地面型 / 飞行型改其中几项
	const UMechMovementComponent* D = GetDefault<UMechMovementComponent>();
	FBoosterSpec S;
	S.Boost = D->BoostSpeed;
	S.Quick = D->QuickBoostSpeed;
	S.QuickCost = D->QuickBoostCost;
	S.QuickCooldown = D->QuickBoostCooldown;
	S.MaxRise = D->MaxRiseSpeed;
	S.ThrustUp = D->ThrustUpAccel;
	S.AirBoost = D->AirBoostAccel;
	S.AirGravity = D->AirBoostGravity;
	S.GroundRegen = 1.f;
	S.AirRegen = D->AirRegenScale;
	S.bCanThrust = true;
	S.bCanAirBoost = true;
	S.FlameLength = 0.5f;  // 锥体长度（米），游戏里 0.2–0.6
	if (MechMapRules::IsBoosterLocked())
	{
		// 11-02 石林飞行器：详情页按它的数值显示
		const MechMapRules::FFlight& F = MechMapRules::LockedFlight();
		S.Boost = F.Cruise;
		S.AirRegen = D->AirRegenScale * F.AirRegen;
		S.FlameLength = 0.66f;
		return S;
	}
	switch (Booster)
	{
	case EMechBooster::Ground:
		S.Boost = 2100.f;
		S.Quick = 3050.f;
		S.QuickCost = 15.f;
		S.QuickCooldown = 0.16f;
		S.bCanThrust = false;
		S.bCanAirBoost = false;
		S.MaxRise = 0.f;
		S.GroundRegen = 1.4f;
		S.FlameLength = 0.34f;
		break;
	case EMechBooster::Flight:
		S.Boost = 1260.f;
		S.Quick = 2380.f;
		S.QuickCost = 26.f;
		S.ThrustUp = 3000.f;
		S.MaxRise = 900.f;
		S.AirBoost = 3200.f;
		S.AirGravity = 0.25f;
		S.AirRegen = 0.8f;
		S.GroundRegen = 0.9f;
		S.FlameLength = 0.66f;
		break;
	default:
		break;
	}
	return S;
}

const TCHAR* MechCatalog::FrameMesh(int32 FrameIndex)
{
	// 10-31：九台各自的网格（AMechCharacter::ApplyLoadout 换网格、摄影棚、结算展示都按这个）
	return FrameBody(FrameIndex).Mesh;
}

const MechCatalog::FFrameBody& MechCatalog::FrameBody(int32 FrameIndex)
{
	using namespace MechCatalogPriv;
	return Bodies[FMath::Clamp(FrameIndex, 0, MechNumFrames - 1)];
}

const MechCatalog::FFrameBody* MechCatalog::FrameBodyForMesh(const UObject* MeshAsset)
{
	using namespace MechCatalogPriv;
	if (!MeshAsset)
	{
		return nullptr;
	}
	static TMap<FName, int32> ByName = [] {
		TMap<FName, int32> Out;
		for (int32 i = 0; i < MechNumFrames; ++i)
		{
			Out.Add(FName(*FPaths::GetBaseFilename(Bodies[i].Mesh)), i);
		}
		return Out;
	}();
	const int32* Found = ByName.Find(MeshAsset->GetFName());
	return Found ? &Bodies[*Found] : nullptr;
}

EMechMaker MechCatalog::FrameMaker(int32 FrameIndex)
{
	return static_cast<EMechMaker>(MechFrameMakerIndex(static_cast<EMechFrame>(FMath::Clamp(FrameIndex, 0, MechNumFrames - 1))));
}

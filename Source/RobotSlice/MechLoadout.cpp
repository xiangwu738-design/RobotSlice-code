#include "MechLoadout.h"
#include "MechMapRules.h"

#include "MechWeapons.h"

namespace
{
	using MechLoadout::FItem;

	const TCHAR* Categories[] = {TEXT("框架"), TEXT("推进器"), TEXT("射击"), TEXT("近战"), TEXT("肩部")};

	// 装甲 10-30 三种都 ×0.85（玩家：TTK 过长），数值在 UMechHealthComponent::MaxAP（中型）和 MechCatalog::FrameSpec（轻 / 重、三家特色）。
	// 10-31 框架 = 三家企业 × 轻中重（顺序见 EMechFrame：企业 × 3 + 档位，档位 中 / 轻 / 重）。说明 = 外形；档位：装甲、速度。企业特色
	// 数值在 MechCatalog::FrameSpec（档位 × 制造商特色，10-31 用户「三家的框架都变得有自己的特色」），改那边要一起改这里
	const FItem Frames[] = {
		{TEXT("新纪 中型"), TEXT("均衡。不容易失衡和被击退，脱战后自动修装甲，回能略慢"), true},
		{TEXT("新纪 轻型"), TEXT("快但脆。不容易失衡和被击退，脱战后自动修装甲，回能略慢"), true},
		{TEXT("新纪 重型"), TEXT("慢但硬。不容易失衡和被击退，脱战后自动修装甲，回能略慢"), true},
		{TEXT("维克托 中型"), TEXT("均衡。弹匣更大，被网、EMP、失衡影响的时间更短"), true},
		{TEXT("维克托 轻型"), TEXT("快但脆。弹匣更大，被网、EMP、失衡影响的时间更短"), true},
		{TEXT("维克托 重型"), TEXT("慢但硬。弹匣更大，被网、EMP、失衡影响的时间更短"), true},
		{TEXT("相位 中型"), TEXT("均衡。闪避省能量，导弹锁定快、子弹更准；更容易失衡，怕 EMP"), true},
		{TEXT("相位 轻型"), TEXT("快但脆。闪避省能量，导弹锁定快、子弹更准；更容易失衡，怕 EMP"), true},
		{TEXT("相位 重型"), TEXT("慢但硬。闪避省能量，导弹锁定快、子弹更准；更容易失衡，怕 EMP"), true},
	};
	static_assert(UE_ARRAY_COUNT(Frames) == MechNumFrames, "框架目录要和 EMechFrame 一致");
	const FItem Boosters[] = {
		{TEXT("均衡型"), TEXT("能跳、能喷射、能滑翔，什么都不差"), true},
		{TEXT("地面型"), TEXT("不能飞；地面冲刺和闪避最强，能连闪两次，着地回能快"), true},
		{TEXT("飞行型"), TEXT("喷射和滑翔最强，空中也回能；地面慢，闪避更费能量"), true},
	};
	const FItem Shoulders[] = {
		{TEXT("导弹"), TEXT("锁定后打出一轮追踪导弹"), true},
		{TEXT("火炮"), TEXT("按住架炮开镜，松开开炮，基本一发失衡"), true},
		{TEXT("护盾"), TEXT("身前展开能量盾，挡住正面的远程攻击，挡不住刀"), true},
		{TEXT("钩爪"), TEXT("抓地形把自己拉过去，抓敌人拉到身前"), true},
		{TEXT("捕获网"), TEXT("罩住的敌人短时间动不了、不能攻击"), true},
		{TEXT("烟雾弹"), TEXT("放出烟幕，隔着烟锁不定目标"), true},
		{TEXT("EMP"), TEXT("范围内敌人能量清零、推进器失灵、锁定失效"), true},
		// 11-01 战术装备并入肩部（顺序 = EMechShoulder）；数值是 UMechTacticalComponent / AMechMine 里的常量，改了要一起改这里的说明。
		// 图标按这四个名字认（SMechItemIcon），别改名
		{TEXT("紧急脱离"), TEXT("朝按着的方向瞬间冲出一段，能量不够也能用，冲完能量清零"), true},
		{TEXT("弹射升空"), TEXT("原地往上弹，之后可以滑翔"), true},
		{TEXT("震荡波"), TEXT("震开身边的敌人，打断他们出刀"), true},
		{TEXT("地雷"), TEXT("抛向准星，敌人靠近就炸，也会被打掉"), true},
	};
	static_assert(UE_ARRAY_COUNT(Shoulders) == static_cast<int32>(EMechShoulder::Mine) + 1, "目录要和 EMechShoulder 一致");

	/** 各类的选项；射击、近战从武器目录里取 */
	const TArray<FItem>& List(int32 Category)
	{
		static const TArray<FItem> Lists[MechLoadout::NumCategories] = {
			TArray<FItem>(Frames, UE_ARRAY_COUNT(Frames)),
			TArray<FItem>(Boosters, UE_ARRAY_COUNT(Boosters)),
			[] {
				TArray<FItem> Out;
				for (int32 i = 0; i < MechWeapons::NumGuns(); ++i)
				{
					const MechWeapons::FGun& G = MechWeapons::Gun(static_cast<EMechShooter>(i));
					Out.Add({G.Name, G.Desc, true});
				}
				return Out;
			}(),
			[] {
				TArray<FItem> Out;
				for (int32 i = 0; i < MechWeapons::NumBlades(); ++i)
				{
					const MechWeapons::FBlade& B = MechWeapons::Blade(static_cast<EMechMeleeWeapon>(i));
					Out.Add({B.Name, B.Desc, true});
				}
				return Out;
			}(),
			TArray<FItem>(Shoulders, UE_ARRAY_COUNT(Shoulders)),
		};
		return Lists[FMath::Clamp(Category, 0, MechLoadout::NumCategories - 1)];
	}
}

const TCHAR* MechLoadout::CategoryName(int32 Category)
{
	return Categories[FMath::Clamp(Category, 0, NumCategories - 1)];
}

int32 MechLoadout::NumItems(int32 Category)
{
	if (Category == Booster && MechMapRules::IsBoosterLocked())
	{
		return 1;  // 11-02 这张图推进器固定（石林）：配装界面只有专用的这一件
	}
	return List(Category).Num();
}

const MechLoadout::FItem& MechLoadout::Item(int32 Category, int32 Index)
{
	if (Category == Booster && MechMapRules::IsBoosterLocked())
	{
		static FItem Locked;
		const MechMapRules::FFlight& F = MechMapRules::LockedFlight();
		Locked = {F.BoosterName, F.BoosterDesc, true};
		return Locked;
	}
	const TArray<FItem>& L = List(Category);
	return L[FMath::Clamp(Index, 0, L.Num() - 1)];
}

int32 MechLoadout::Get(const FMechLoadout& L, int32 Category)
{
	switch (Category)
	{
	case Frame:
		return static_cast<int32>(L.Frame);
	case Booster:
		return MechMapRules::IsBoosterLocked() ? 0 : static_cast<int32>(L.Booster);
	case Shooter:
		return static_cast<int32>(L.Shooter);
	case Melee:
		return static_cast<int32>(L.Melee);
	default:
		return static_cast<int32>(L.Shoulder);
	}
}

void MechLoadout::Set(FMechLoadout& L, int32 Category, int32 Index)
{
	if (Category == Booster && MechMapRules::IsBoosterLocked())
	{
		return;  // 锁着时不改玩家存的推进器：换回别的图还是原来那件
	}
	const uint8 V = static_cast<uint8>(FMath::Clamp(Index, 0, NumItems(Category) - 1));
	switch (Category)
	{
	case Frame:
		L.Frame = static_cast<EMechFrame>(V);
		break;
	case Booster:
		L.Booster = static_cast<EMechBooster>(V);
		break;
	case Shooter:
		L.Shooter = static_cast<EMechShooter>(V);
		break;
	case Melee:
		L.Melee = static_cast<EMechMeleeWeapon>(V);
		break;
	default:
		L.Shoulder = static_cast<EMechShoulder>(V);
		break;
	}
}

void MechLoadout::DisplayOrder(int32 Category, TArray<int32>& Out)
{
	Out.Reset();
	if (Category == Frame)
	{
		for (int32 m = 0; m < 3; ++m)
		{
			for (const EMechFrameTier T : {EMechFrameTier::Light, EMechFrameTier::Medium, EMechFrameTier::Heavy})
			{
				Out.Add(static_cast<int32>(MechMakeFrame(m, T)));
			}
		}
		return;
	}
	for (int32 i = 0; i < NumItems(Category); ++i)
	{
		Out.Add(i);
	}
}

int32 MechLoadout::GroupOf(int32 Category, int32 Index)
{
	return Category == Frame ? MechFrameMakerIndex(static_cast<EMechFrame>(FMath::Clamp(Index, 0, MechNumFrames - 1))) : -1;
}

FString MechLoadout::Describe(const FMechLoadout& L)
{
	TArray<FString> Parts;
	for (int32 c = 0; c < NumCategories; ++c)
	{
		Parts.Add(Item(c, Get(L, c)).Name);
	}
	return FString::Join(Parts, TEXT(" · "));
}

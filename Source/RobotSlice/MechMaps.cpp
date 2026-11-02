#include "MechMaps.h"

#include "Misc/PackageName.h"

namespace MechMaps
{
	const TArray<FMap>& All()
	{
		// 菜单按这个顺序排；新地图往后加一行（说明见 MechMaps.h）
		static const TArray<FMap> Table = {
			{TEXT("/Game/Maps/L_Street"), TEXT("赛博朋克街道"), Elim | DM},
			{TEXT("/Game/Maps/L_Nature"), TEXT("荒野与废墟"), Elim | DM},
			// 11-02 用户：「现在的夺芯地图可以移除了，暂时只保留玩法，那张图的设计很糟糕」→ L_Volcano 撤出这张表和打包（地图文件还在工程里）。
			// 表里没有 Core 图时房间不显示夺芯这一格；以后加一行 Core 图就回来
			{TEXT("/Game/Maps/L_TechPark"), TEXT("相位电子园区"), Elim | DM},
			// 11-02 用户：电子园区和城区街口定位重合（都讲掩体和团队战术），城区设计略逊，只留电子园区 → L_City 撤出这张表和打包（文件还在工程里）
			// 11-01 进包：用户「等修改好了地图再打包」；蓝方在平台上、红方在 500 米外的草地（TeamSpawn0/1）。
			// 保卫战也接在这张图上（用户：「你先将它接入，然后重新打包」）：指挥所在平台上，敌人从旷野那边来
			{TEXT("/Game/Maps/L_Dreamfield"), TEXT("梦核旷野"), Elim | DM | Defense},
			// 11-02 石柱山水：偏空战的图，飞行增强 + 落水损毁（规则在 MechMapRules.h，只按地图名和地图里的 WaterKill 标记生效）
			{TEXT("/Game/Maps/L_Pillars"), TEXT("石柱山水"), Elim | DM},
			// 11-02 苏联板楼、月球基地进表
			{TEXT("/Game/Maps/L_Soviet"), TEXT("苏联板楼"), Elim | DM},
			{TEXT("/Game/Maps/L_Moon"), TEXT("月球基地"), Elim | DM},
			// 11-02 老旧街道：一维循环的娱乐图（MechLoop.h：沿街无限循环，机体、子弹走出接缝从另一头回来），只开死斗
			{TEXT("/Game/Maps/L_OldStreet"), TEXT("老旧街道"), DM},
			// 11-02 用户：「加入一个训练场联机模式，人们可以在这个训练场开启友伤」：房间里选这张 = 训练场模式
			{TEXT("/Game/Maps/L_Range"), TEXT("训练场"), Training},
		};
		return Table;
	}

	int32 Num()
	{
		return All().Num();
	}

	const FMap& Get(int32 Index)
	{
		return All()[FMath::Clamp(Index, 0, All().Num() - 1)];
	}

	bool AnySupports(uint8 Mode)
	{
		return All().ContainsByPredicate([Mode](const FMap& M) { return M.Supports(Mode); });
	}

	int32 FindByMapName(const FString& MapName)
	{
		for (int32 i = 0; i < All().Num(); ++i)
		{
			const FString Short = FPackageName::GetShortName(All()[i].Path);
			if (MapName.EndsWith(Short))
			{
				return i;
			}
		}
		return INDEX_NONE;
	}
}

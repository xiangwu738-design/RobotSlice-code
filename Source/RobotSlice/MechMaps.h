// 对战地图表（11-01 起房间页的地图按钮按这个表排，房主选，MechRoom.h）。加新地图只在 MechMaps.cpp 的表里加一行：
//   { TEXT("/Game/Maps/L_xxx"), TEXT("菜单上显示的名字"), 能玩的模式（MechMaps::Elim | MechMaps::DM，夺芯图写 MechMaps::Core） },
// 夺芯图开局走 ?Core（场地由地图里带「CoreArena」标签的点定），别的走 ?Match?Mode=Elim|DM；带 MechMaps::Defense 的图房间里能选「保卫战」，走 ?Defense。
// 新地图的出生点（可选，都没有就用第一个 PlayerStart + 正前方 90 米）：
//   带「TeamSpawn0」「TeamSpawn1」标签的 actor = 两队出生区中心（朝向 = 出生朝向，一队的几台沿左右排开，间隔 7 米）；
//   带「DMSpawn」标签的 actor（TargetPoint / PlayerStart 都行）= 死斗重生点候选，越多越好、铺满场地（至少 8 个，互相隔 15 米以上）。
//   没摆 DMSpawn 时死斗用两队出生区 + 场地里自动找的空地 + 开打后机体走过的地面点。
// 打包时还要把地图加进 打包.bat 和 Config/DefaultGame.ini 的烘焙地图列表。

#pragma once

#include "CoreMinimal.h"

namespace MechMaps
{
	enum EModes : uint8
	{
		Elim = 1 << 0,
		DM = 1 << 1,
		Core = 1 << 2,
		/** 保卫战（MechDefense.*：合作守指挥所、打 10 波） */
		Defense = 1 << 3,
		/** 11-02 训练场：走 ?Range（能联机、自由换配装、有木桩、不计分） */
		Training = 1 << 4
	};

	struct FMap
	{
		/** 地图资源路径 */
		const TCHAR* Path;
		/** 菜单上的名字 */
		const TCHAR* Name;
		/** 能玩的模式（EModes 按位或） */
		uint8 Modes;

		bool IsCore() const { return (Modes & Core) != 0; }
		bool IsTraining() const { return (Modes & Training) != 0; }
		bool Supports(uint8 Mode) const { return (Modes & Mode) != 0; }
	};

	const TArray<FMap>& All();
	/** 越界时夹到表里 */
	const FMap& Get(int32 Index);
	int32 Num();
	/** 按地图名（GetMapName，带不带 UEDPIE 前缀都行）找表里的行，找不到 = INDEX_NONE */
	int32 FindByMapName(const FString& MapName);
	/** 表里有没有支持这个模式的图（没有的模式房间里不显示） */
	bool AnySupports(uint8 Mode);
}

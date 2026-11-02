// 配装目录：五类部件各自的名字、一句话说明、做好了没有（没做好的在选配界面里显示「待做」、选不了）。
// 框架和推进器的实际数值在 AMechCharacter::ApplyLoadout，射击和近战武器的在 MechWeapons，肩部的紧急脱离等四件在 UMechTacticalComponent。

#pragma once

#include "CoreMinimal.h"
#include "MechTypes.h"

namespace MechLoadout
{
	enum ECategory : int32
	{
		Frame,
		Booster,
		Shooter,
		Melee,
		Shoulder,
		NumCategories
	};

	struct FItem
	{
		const TCHAR* Name;
		const TCHAR* Desc;
		bool bReady;
	};

	const TCHAR* CategoryName(int32 Category);
	int32 NumItems(int32 Category);
	const FItem& Item(int32 Category, int32 Index);
	int32 Get(const FMechLoadout& Loadout, int32 Category);
	void Set(FMechLoadout& Loadout, int32 Category, int32 Index);
	/** 例如「中型 · 均衡型 · 步枪 · 刀 · 导弹」 */
	FString Describe(const FMechLoadout& Loadout);
	/** 界面里卡片的排列顺序：框架按企业分组（新纪建设、维克托重工、相位电子），每组 轻 / 中 / 重；其余类就是编号顺序 */
	void DisplayOrder(int32 Category, TArray<int32>& Out);
	/** 框架按企业分组时，这一件是第几家（0 新纪建设 1 维克托重工 2 相位电子）；不是框架类返回 -1 */
	int32 GroupOf(int32 Category, int32 Index);
}

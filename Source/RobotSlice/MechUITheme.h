// 界面配色集中在这里（10-31 配装照片）：菜单、选配 / 配装页、房间都从这里取色，不在各处写死。
// 11-01（用户：「配色也可以在设置里面改」）：几套预设（青色 / 琥珀 / 冷白 / 赤红 / 荧绿），设置「界面」页里选，存 FMechSettings::MenuPalette。
// 换一套 = MechKit::ApplyPalette 把预设拷进 Mutable() 那份对象；各处拿的是引用或每帧读（MechUITheme::Get().Accent），换了马上生效。
// 只管菜单这一侧；战斗 HUD 的四套主题在 MechHudTheme（跟着机体制造商走，或在设置里固定一套）。

#pragma once

#include "CoreMinimal.h"

struct FMechUITheme
{
	/** 设置页上的名字 */
	const TCHAR* Name = TEXT("青色");
	/** 正文（亮）、次要文字、很淡的线和框 */
	FLinearColor Ink;
	FLinearColor Dim;
	FLinearColor Faint;
	/** 强调色：自己、选中、条形的填充 */
	FLinearColor Accent;
	/** 实心强调色上的字（深色） */
	FLinearColor AccentInk;
	/** 条形的底槽 */
	FLinearColor Track;
	/** 提醒（倒计时快到、待做、未应用的改动） */
	FLinearColor Warn;
	/** 敌人 / 禁用 / 错误 */
	FLinearColor Bad;
	/** 两队颜色（各套配色都一样：蓝 / 红是队伍的意思，不跟着换） */
	FLinearColor Blue;
	FLinearColor Red;
	/** 整屏半透明暗底、卡片底色、照片底色 */
	FLinearColor Backdrop;
	FLinearColor CardFill;
	FLinearColor PhotoFill;
	/** 菜单左侧面板的底色（不透明，用时再乘透明度；稍微带一点强调色的色相，不是纯黑） */
	FLinearColor Panel;
	/** 细分隔线、控件描边（很淡） */
	FLinearColor Line;
	/** 悬停时铺的一层底 */
	FLinearColor Hover;
};

namespace MechUITheme
{
	inline FLinearColor Srgb(uint8 R, uint8 G, uint8 B, float A = 1.f)
	{
		FLinearColor C(FColor(R, G, B));
		C.A = A;
		return C;
	}

	/** 一套配色：只给几个基准色，其余按同一个规则推出来（各套的层次、透明度一致，只换色相） */
	inline FMechUITheme Make(const TCHAR* Name, const FColor& Accent, const FColor& AccentInk, const FColor& Ink, const FColor& Dim, const FColor& Track, float TrackA,
		const FColor& Panel, const FColor& Warn, const FColor& Bad)
	{
		FMechUITheme T;
		T.Name = Name;
		T.Ink = FLinearColor(Ink);
		T.Dim = FLinearColor(Dim);
		T.Faint = Srgb(Dim.R, Dim.G, Dim.B, 0.35f);
		T.Accent = FLinearColor(Accent);
		T.AccentInk = FLinearColor(AccentInk);
		T.Track = Srgb(Track.R, Track.G, Track.B, TrackA);
		T.Warn = FLinearColor(Warn);
		T.Bad = FLinearColor(Bad);
		T.Blue = Srgb(133, 183, 235);
		T.Red = Srgb(226, 75, 74);
		T.Panel = FLinearColor(Panel);
		T.Backdrop = Srgb(Panel.R, Panel.G, Panel.B, 0.95f);
		T.CardFill = Srgb(FMath::Min(255, Panel.R + 6), FMath::Min(255, Panel.G + 6), FMath::Min(255, Panel.B + 6), 0.9f);
		T.PhotoFill = FLinearColor(0.f, 0.f, 0.f, 1.f);
		T.Line = Srgb(Ink.R, Ink.G, Ink.B, 0.1f);
		T.Hover = Srgb(Ink.R, Ink.G, Ink.B, 0.05f);
		return T;
	}

	/** 几套预设。0 = 原来那套（和 HUD 联赛标准同一个青色），后面四套是 11-01 加的 */
	constexpr int32 NumPresets = 5;
	inline FMechUITheme Preset(int32 Index)
	{
		switch (FMath::Clamp(Index, 0, NumPresets - 1))
		{
		case 1:  // 琥珀：工业仪表的暖橙；提醒色换成偏黄，免得和强调色分不开
			return Make(TEXT("琥珀"), FColor(247, 164, 58), FColor(26, 15, 3), FColor(244, 238, 228), FColor(162, 150, 134), FColor(104, 60, 10), 0.7f,
				FColor(14, 11, 8), FColor(255, 214, 90), FColor(226, 75, 74));
		case 2:  // 冷白：最克制，强调色接近白（选中靠描边和底色区分）
			return Make(TEXT("冷白"), FColor(222, 232, 244), FColor(10, 12, 16), FColor(246, 248, 251), FColor(148, 156, 168), FColor(255, 255, 255), 0.16f,
				FColor(9, 10, 13), FColor(239, 159, 39), FColor(226, 75, 74));
		case 3:  // 赤红：强调色是红，「禁用 / 错误」改用琥珀，不然选中和被禁分不开
			return Make(TEXT("赤红"), FColor(255, 72, 86), FColor(28, 4, 7), FColor(246, 238, 236), FColor(164, 146, 146), FColor(112, 18, 28), 0.7f,
				FColor(14, 8, 9), FColor(255, 196, 64), FColor(255, 150, 52));
		case 4:  // 荧绿：军用荧光屏的绿
			return Make(TEXT("荧绿"), FColor(110, 232, 120), FColor(4, 20, 8), FColor(230, 242, 228), FColor(138, 162, 140), FColor(16, 84, 30), 0.7f,
				FColor(8, 12, 9), FColor(239, 159, 39), FColor(226, 75, 74));
		default:  // 青色（默认）
			return Make(TEXT("青色"), FColor(93, 202, 165), FColor(4, 18, 14), FColor(238, 240, 236), FColor(148, 152, 150), FColor(12, 92, 76), 0.7f,
				FColor(9, 14, 16), FColor(239, 159, 39), FColor(226, 75, 74));
		}
	}

	/** 原来那一套（= 预设 0） */
	inline FMechUITheme Default()
	{
		return Preset(0);
	}

	/** 可改的那一份（函数内静态，静态初始化顺序没问题；各处保存引用即可跟着变） */
	inline FMechUITheme& Mutable()
	{
		static FMechUITheme Theme = Default();
		return Theme;
	}

	inline const FMechUITheme& Get()
	{
		return Mutable();
	}

	/** 换一整套（设置里换配色时由 MechKit::ApplyPalette 调，它还会刷新滚动条、输入框这些缓存了颜色的样式） */
	inline void Set(const FMechUITheme& InTheme)
	{
		Mutable() = InTheme;
	}
}

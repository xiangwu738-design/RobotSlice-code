// 界面字体：菜单和 HUD 共用。中文用系统自带的微软雅黑（比引擎自带的 DroidSansFallback 清楚），找不到就退回引擎默认字体。
// HUD 也按目标像素大小直接栅格化（以前用 UFont 加缩放，是把小号字形贴图放大，汉字发糊）。

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

class UFont;

namespace MechFonts
{
	/** 菜单（Slate）用；Size 是 Slate 字号 */
	FSlateFontInfo Get(float Size, bool bBold = false);

	/** 菜单大标题（11-01 界面改版）：微软雅黑 Light（msyhl.ttc），没有就退回常规体 */
	FSlateFontInfo GetLight(float Size);

	/** 菜单里的数字、英文小标签（11-01）：Bahnschrift（和 HUD 同一款 DIN 风格窄体），中文退回微软雅黑；LetterSpacing 是 Slate 的字距（千分之一 em） */
	FSlateFontInfo GetLatin(float Size, int32 LetterSpacing = 0);

	/** HUD（Canvas）用：Canvas 画字必须挂一个 UFont 对象（只给字体描述的话引擎直接不画），
	 *  所以把同一份微软雅黑包成运行时 UFont，字号仍按 Size 栅格化 */
	FSlateFontInfo GetCanvas(float Size, bool bBold = false);

	/** HUD 主题字体（10-31，MechHudTheme.h 的 EMechHudFace 编号）：同样包成运行时 UFont；字体文件不在时退回 Bahnschrift，中文退回微软雅黑。
	 *  Size 是想要的「Bahnschrift 字号」，按各字体自己的宽窄换算（同一个字号各套字大致一样宽，布局不用跟着改） */
	FSlateFontInfo GetHudFace(int32 Face, float Size);
}

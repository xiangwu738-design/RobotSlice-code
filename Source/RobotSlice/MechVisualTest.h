// 画面问题复现用的脚本测试（10-30 画面修正加的），都在测试场 /Game/Maps/L_Range?Range 跑，本机玩家那台由脚本操作：
// -MechFallTest：起跳 / 跑下高台 / 喷射结束 / 滑翔结束开始下落的那一瞬间，腿部特写逐帧截图到 Saved/Screenshots/Fall，
//   日志 [falltest] 每帧记两腿最近距离、膝盖 / 脚有没有左右交叉、膝盖弯曲、大腿抬起角度（配 -benchmark -fps=30）。
//   加 -MechAnimOldFall 用改之前的混法（截图文件名带「旧_」）。
// -MechTracerTest：步枪、充能步枪对着 25 米木桩边转镜头边开火，逐项关掉动态模糊 / TSR / Lumen / 粒子 / 曳光本身各截几张，
//   找子弹光线被拉长的来源，截图在 Saved/Screenshots/Tracer，日志 [tracertest]。加 -MechTracerOld 用改之前的曳光画法（文件名带「旧_」）。

#pragma once

#include "CoreMinimal.h"

class AMechCharacter;

struct FMechVisualTest
{
	/** 角色 Tick 里每帧调（只有带对应开关、本机玩家操控的那台才做事） */
	static void Tick(AMechCharacter* Me, float DeltaSeconds);

private:
	static void FallTick(AMechCharacter* Me, float DeltaSeconds);
	static void TracerTick(AMechCharacter* Me, float DeltaSeconds);
};

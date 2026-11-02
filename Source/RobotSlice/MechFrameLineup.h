// 10-31 阵营机体进游戏的验证：-MechFrameLineup（测试场 "/Game/Maps/L_Range?Range"，加 -RenderOffscreen -ForceRes -ResX=1920 -ResY=1080 -windowed）。
// 九种框架（三家企业 × 轻中重）按企业、轻中重排成一排（玩家设置里的涂装，端枪站着），先拍整排正面 / 斜侧，再每台单独拍正面 / 斜侧（其余几台藏起来），
// 截图到 Saved/Screenshots/Frames，拍完退出；开头每台网格打一行 [lineup]：骨头数、骨头名 / 父子关系和骨架是否一致、绑定姿势和 v3 差多少、
// 材质槽、三角面数、蒙皮用到的减震骨头。tools/frame_lineup_sheet.py 把截图拼成 docs/阵营机体_游戏内总览.jpg。
// -MechLineupFace=180 让机体朝另一边（逆光时换个方向拍）。

#pragma once

#include "CoreMinimal.h"

class AMechCharacter;

struct FMechFrameLineup
{
	/** 角色 Tick 里每帧调（只有带开关、本机玩家操控的那台才做事） */
	static void Tick(AMechCharacter* Me, float DeltaSeconds);
};

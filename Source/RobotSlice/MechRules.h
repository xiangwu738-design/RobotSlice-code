// 对战规则（房主在房间里定，存进设置记住上次的选择）：模式、团队淘汰的局数、死斗的个人 / 团队、目标击毁数、时间。
// 夺芯是地图决定的（山谷图），不在这里选。
//   团队淘汰：一方全倒输一局、局间不复活，先赢 RoundsToWin 局的队伍获胜（1 局定胜负 / 三局两胜 / 五局三胜 / 七局四胜）。
//   死斗：无限复活，击毁一台 +1（点数就是击毁数，不算助攻；掉出地图等环境阵亡谁都不加分、也不扣分），
//         先到目标击毁数，或时间到时击毁多的一方 / 一人获胜（同分平局）。个人 = 人人为敌按个人击毁数；团队 = 按队伍击毁总数。
// 规则数据放 GameState 复制给各端（房间界面、HUD、结算页照着画），流程在 AMechGameMode。

#pragma once

#include "CoreMinimal.h"
#include "MechRules.generated.h"

UENUM()
enum class EMechMode : uint8
{
	/** 团队淘汰（原来叫「死斗」的那套回合制） */
	Elimination,
	/** 死斗：无限复活、比击毁数 */
	Deathmatch
};

USTRUCT()
struct FMechRules
{
	GENERATED_BODY()

	UPROPERTY()
	EMechMode Mode = EMechMode::Elimination;

	/** 团队淘汰：先赢几局（1 = 一局定胜负，2 = 三局两胜，3 = 五局三胜，4 = 七局四胜） */
	UPROPERTY()
	int32 RoundsToWin = 2;

	/** 死斗：个人（人人为敌）；false = 团队 */
	UPROPERTY()
	bool bSolo = false;

	/** 死斗目标击毁数：团队按队伍总数、个人按自己的（两种分开记，切换时各用各的） */
	UPROPERTY()
	int32 TeamTarget = 30;

	UPROPERTY()
	int32 SoloTarget = 15;

	/** 死斗时间限制（秒） */
	UPROPERTY()
	int32 TimeLimit = 600;

	bool IsDeathmatch() const { return Mode == EMechMode::Deathmatch; }
	int32 KillTarget() const { return bSolo ? SoloTarget : TeamTarget; }
};

namespace MechRules
{
	/** 死斗阵亡后几秒重生（夺芯是 8 秒；死斗节奏快，短一点） */
	constexpr float RespawnDelay = 5.f;
	/** 重生保护：几秒内不掉装甲、不积累失衡；自己打中别人就提前结束 */
	constexpr float SpawnGuardTime = 3.f;
	/** 重生保护的效果名（MechEffects 表里，HUD 插件栏显示剩余时间） */
	inline FName SpawnGuardId() { return FName(TEXT("SpawnGuard")); }

	/** 房间里能选的值 */
	const TArray<int32>& RoundOptions();        // 先赢几局：1 2 3 4
	const TArray<int32>& TeamTargetOptions();   // 团队目标：10 20 30 40 50
	const TArray<int32>& SoloTargetOptions();   // 个人目标：5 10 15 20 25
	const TArray<int32>& TimeOptions();         // 时间（秒）：300 600 900 1200

	/** 「三局两胜」这类说法（菜单 / 房间 / 结算用） */
	FText RoundsName(int32 RoundsToWin);
	FText ModeName(const FMechRules& Rules, bool bCore);
	/** 一句话概括（房间顶栏、结算页）：「死斗 · 团队 · 先到 30 击毁 · 10 分钟」 */
	FString Describe(const FMechRules& Rules, bool bCore);
	/** 夹到合法范围（网络上收到的、ini 里读到的都过一遍） */
	void Sanitize(FMechRules& Rules);
	/** 从地址参数（?Mode=DM / Elim、?Solo、?DMTeam、?Rounds=、?Target=、?Time=）和命令行（-MechMode=DM|Elim、-MechDMSolo、-MechDMTeam、
	 *  -MechDMTarget=、-MechDMTime=、-MechWinRounds=）覆盖 */
	void ApplyOptions(FMechRules& Rules, const FString& Options, const TCHAR* CommandLine);
}

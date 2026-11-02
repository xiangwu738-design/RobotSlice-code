// 结算积分（11-01，用户：「关于结算，我想设一个积分制度，不要每次都让kd高的人当mvp」）。
// MVP 不再看击毁 / 阵亡，而是看整场积分：输出、击毁、助攻、打出失衡、控制、救人、目标（夺芯交芯 / 保卫战杂兵和首领）都算分，阵亡不扣分
// （扣阵亡分就又回到「看 K/D」；分全靠打出来的贡献，不打就没分，苟着拿不到 MVP）。规则表在 docs/结算积分规则.md，数值全在下面这一处调。
// 名次和胜负规则不变：个人死斗照旧按击毁数排名次、定胜者（用户定的「点数就是击杀数」），只是 MVP 改成全场积分最高的人，可以不是第一名。
// 统计只在服务器上记（AMechGameMode 的 Stats，实现在 MechScore.cpp），每秒抄进 AMechGameState::Results 复制给各端；
// 结算场景（MechResultsStage）和 Tab 计分板按复制下来的 Score / Parts 排序、挑 MVP、写 MVP 理由，单机测试的假数据也走这里。
#pragma once

#include "CoreMinimal.h"

class AMechCharacter;
struct FMechResultRow;

/** 得分分项（FMechResultRow::Parts 的下标；新加的往 Num 前面加，日志 / 理由的名字在 MechScore.cpp 的表里） */
enum class EMechScorePart : uint8
{
	Damage,   // 对敌人的伤害（保卫战含杂兵）
	Kill,     // 击毁敌方机体
	Assist,   // 助攻
	Break,    // 打出失衡（失衡槽打满那一下）
	Control,  // 控制命中：网罩住、EMP 命中、钩爪把敌人拉过来、刀弹反
	Rescue,   // 救人：钩爪把被网住的队友拉出来、夺芯捡回队友的核心
	Core,     // 夺芯：交芯得分
	Carrier,  // 夺芯：击毁正带着核心的敌人（截下这一分）
	Grunt,    // 保卫战：自爆 / 机枪无人机、履带炮车
	Mortar,   // 保卫战：拆掉迫击炮（远在 220 米外，要专门跑一趟）
	Boss,     // 保卫战：精英 / 首领机甲的击毁 + 对首领伤害的加成
	Defend,   // 保卫战：在指挥所附近击毁敌人的加成
	Num
};

/** 保卫战击毁的种类（前四个和 EMechGrunt 同序） */
enum class EMechDefenseKill : uint8
{
	Kamikaze,
	Gunner,
	Tank,
	Mortar,
	Elite,
	Boss
};

namespace MechScore
{
	constexpr int32 NumParts = static_cast<int32>(EMechScorePart::Num);

	// ---------------------------------------------------------------- 数值（改了同步改 docs/结算积分规则.md 那张表）
	/** 伤害：每这么多点伤害 1 分（中型装甲 10200 ≈ 102 分，一台的伤害和一个击毁差不多值钱） */
	constexpr float DamagePerPoint = 100.f;
	/** 击毁敌方机体（环境 / 自己 / 队友打死的不算，和击毁数同一个口径） */
	constexpr int32 Kill = 100;
	/** 助攻：击毁前 AssistWindow 秒内对它打过 ≥ AssistMinDamage 伤害，或打出过失衡 / 控制过它（击毁者本人不算） */
	constexpr int32 Assist = 50;
	constexpr float AssistWindow = 15.f;
	constexpr float AssistMinDamage = 600.f;
	/** 打出失衡（失衡槽打满的那一下；被弹反的短僵直不算失衡，算在控制里） */
	constexpr int32 Break = 30;
	/** 控制命中（每台每次）：网罩住、EMP 命中、钩爪拉敌人、刀弹反 */
	constexpr int32 Control = 15;
	/** 钩爪把被网住的队友拉出来 */
	constexpr int32 NetRescue = 40;
	/** 夺芯：交芯得 1 分（= 两个击毁）、击毁带芯的敌人（击毁分之外另加）、捡回队友的核心（他马上重生） */
	constexpr int32 CoreDeliver = 200;
	constexpr int32 CarrierKill = 60;
	constexpr int32 CoreRescue = 60;
	/** 保卫战击毁（下标 EMechDefenseKill：自爆、机枪、炮车、迫击炮、精英、首领） */
	constexpr int32 DefenseKill[6] = {10, 20, 60, 100, 150, 300};
	/** 保卫战：打首领每 DamagePerPoint 点伤害另加这么多分（= 首领伤害按双倍算） */
	constexpr int32 BossDamageBonus = 1;
	/** 保卫战：被击毁的敌人离指挥所这么近（厘米）时，击毁分另加这么多倍（0.5 = 击毁分 ×1.5） */
	constexpr float DefendRadius = 4500.f;
	constexpr float DefendBonus = 0.5f;
	/** 阵亡：每次加这么多分（0 = 不扣；想扣就写负数） */
	constexpr int32 Death = 0;

	// ---------------------------------------------------------------- 服务器：玩法代码里调（找当前的 AMechGameMode，客户端 / 没有就什么都不做）
	/** 控制命中（网、EMP、钩爪拉人、弹反）：记控制分，也算助攻的贡献 */
	void NoteControl(AMechCharacter* By, AMechCharacter* Victim);
	/** 救人：bCore = 夺芯捡回队友核心，否则 = 钩爪把被网住的队友拉出来 */
	void NoteRescue(AMechCharacter* By, bool bCore);
	/** 保卫战：玩家打杂兵的实际伤害（杂兵不走机体的受击，伤害统计在这里补） */
	void NoteGruntDamage(AMechCharacter* By, float Damage);
	/** 保卫战：击毁杂兵 / 精英 / 首领；VictimName 给精英、首领算助攻用（杂兵传空）；bNearCore = 离指挥所 DefendRadius 内 */
	void NoteDefenseKill(AMechCharacter* By, EMechDefenseKill Kind, const FString& VictimName, bool bNearCore);

	// ---------------------------------------------------------------- 各端：显示
	/** 各分项加起来（加上阵亡分，不低于 0）写进 Row.Score */
	void Finish(FMechResultRow& Row);
	/** 只有计数的结算行（结算场景的假数据测试）按上面的数值补齐 Parts 和 Score */
	void FromCounts(FMechResultRow& Row);
	/** MVP：Rows 里队伍号是 Team 的（-1 = 全部）积分最高的下标；同分阵亡少的、再同输出高的、再同排前面的。没有返回 INDEX_NONE */
	int32 PickMVP(const TArray<FMechResultRow>& Rows, int32 Team);
	/** MVP 理由（界面上，英文缩写，和铭牌一个写法）：「SCORE 1840  ·  AST 6  ·  CORE 2」——积分 + 最突出的 Items 项
	 *  （先挑这一项全场最多的，再按这一项的分数） */
	FString Reason(const FMechResultRow& Row, const TArray<FMechResultRow>& All, int32 Items = 2);
	/** 日志用：「积分 1215 = 伤害 500 + 击毁 300 + 助攻 250（5 次）+ …」 */
	FString Breakdown(const FMechResultRow& Row);
	/** 分项的英文缩写（界面）/ 中文名（日志） */
	const TCHAR* PartLabel(EMechScorePart Part);
	const TCHAR* PartName(EMechScorePart Part);
	/** 这一行某分项的分（Parts 不够长就是 0） */
	int32 PartOf(const FMechResultRow& Row, EMechScorePart Part);
}

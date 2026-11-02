// 开打前的禁用 / 选配（死斗和夺芯都用）：两队轮流各禁 BansPerTeam 件对面不能用的装备（射击、近战、肩部），
// 然后蛇形轮流公开选配（A1 B1 B2 A2 A3 B3…），每人一次定下整套配装，整场不变。真人轮到时限时，超时由 AI 帮选；AI 自己挑。
// 数据放在 GameState 里复制给所有人（选配界面照着画），规则在 GameMode（StartDraft / DraftTick / DraftBan / DraftPick）。

#pragma once

#include "CoreMinimal.h"
#include "MechTypes.h"
#include "MechDraft.generated.h"

USTRUCT()
struct FMechDraftSlot
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	int32 Team = 0;

	/** 队内第几台（和回合生成时的出生位一致） */
	UPROPERTY()
	int32 Index = 0;

	/** 真人的 PlayerId（PlayerState），AI 为 -1 */
	UPROPERTY()
	int32 PlayerId = -1;

	UPROPERTY()
	FMechLoadout Loadout;

	/** 已经定下（公开） */
	UPROPERTY()
	bool bLocked = false;
};

USTRUCT()
struct FMechDraftBan
{
	GENERATED_BODY()

	/** 哪队禁的（禁的是对面） */
	UPROPERTY()
	int32 Team = 0;

	UPROPERTY()
	uint8 Category = 0;

	UPROPERTY()
	uint8 Index = 0;
};

USTRUCT()
struct FMechDraftTurn
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Team = 0;

	/** 轮到哪个位置（Slots 下标）；禁用轮是这队的队长（第一个真人，没有就第一台） */
	UPROPERTY()
	int32 Slot = 0;

	UPROPERTY()
	bool bBan = false;
};

USTRUCT()
struct FMechDraft
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FMechDraftSlot> Slots;

	UPROPERTY()
	TArray<FMechDraftBan> Bans;

	UPROPERTY()
	TArray<FMechDraftTurn> Order;

	/** 当前第几轮（= Order.Num() 时全部选完） */
	UPROPERTY()
	int32 Turn = 0;

	/** 这一轮在服务器时间几秒结束 */
	UPROPERTY()
	float TurnEnd = 0.f;

	/** 选完了（整场用 Slots 里的配装） */
	UPROPERTY()
	bool bDone = false;

	const FMechDraftTurn* Current() const { return !bDone && Order.IsValidIndex(Turn) ? &Order[Turn] : nullptr; }
	/** 这个真人在哪个位置，没有 = -1 */
	int32 FindPlayer(int32 PlayerId) const;
	const FMechDraftSlot* FindSlot(int32 Team, int32 Index) const;
};

namespace MechDraft
{
	constexpr int32 BansPerTeam = 2;
	constexpr float BanTime = 20.f;
	constexpr float PickTime = 30.f;
	/** 全部选完后停几秒给大家看对面的配装 */
	constexpr float ReviewTime = 5.f;

	/** 这一类能不能禁（射击、近战、肩部；11-01 战术装备并入肩部后肩部整类都能禁） */
	bool IsBannable(int32 Category);
	/** 被对面禁了 */
	bool IsBannedFor(const FMechDraft& Draft, int32 Team, int32 Category, int32 Index);
	/** Team 这队能不能用这一件（做好了、没被对面禁） */
	bool IsAllowed(const FMechDraft& Draft, int32 Team, int32 Category, int32 Index);
	/** 这队已经禁过这一件 */
	bool HasBanned(const FMechDraft& Draft, int32 Team, int32 Category, int32 Index);
	/** 把配装里被禁 / 没做好的换成能用的 */
	FMechLoadout Sanitize(const FMechDraft& Draft, int32 Team, const FMechLoadout& Wanted);
	/** AI：给这个位置挑一套（武器尽量和队友不重样） */
	FMechLoadout AutoPick(const FMechDraft& Draft, int32 Slot, FRandomStream& Rand);
	/** 测试 -MechForceFrame=258：第 Index 台（选配位 / 生成顺序）按位轮流用这几种框架（EMechFrame 编号 0–8）；没给这个开关返回 -1。
	 *  AI 代选时在 AutoPick 里用，没走选配的测试（-MechPerf、按键测试）在 AMechGameMode 生成机体时用 */
	int32 ForcedFrame(int32 Index);
	/** AI：替 Team 禁一件对面的武器或肩部装备 */
	FMechDraftBan AutoBan(const FMechDraft& Draft, int32 Team, FRandomStream& Rand);
}

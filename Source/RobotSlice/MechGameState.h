// 对战状态（复制给所有客户端）：回合阶段、比分、倒计时、击毁通报、每台机体的统计（Tab 计分板和结算页用）、是否主菜单模式、对战规则。
// 比分 TeamScore：团队淘汰 = 赢的局数，夺芯 = 交芯分，团队死斗 = 队伍击毁总数；个人死斗看 Results 里每人的 Kills。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MechTypes.h"
#include "MechDraft.h"
#include "MechRules.h"
#include "MechRoom.h"
#include "MechGameState.generated.h"

/** 一台机体整场的统计 */
USTRUCT()
struct FMechResultRow
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	int32 Team = 0;

	UPROPERTY()
	int32 Dealt = 0;

	UPROPERTY()
	int32 Taken = 0;

	UPROPERTY()
	int32 Kills = 0;

	/** 阵亡次数（死斗计分板；掉出地图也算一次） */
	UPROPERTY()
	int32 Deaths = 0;

	UPROPERTY()
	int32 Bullets = 0;

	UPROPERTY()
	int32 Missiles = 0;

	UPROPERTY()
	int32 Blades = 0;

	/** 夺芯：交芯次数（结算场景的铭牌、MVP 用） */
	UPROPERTY()
	int32 Cores = 0;

	/** 11-01 积分制（MechScore.h，用户：「不要每次都让kd高的人当mvp」）：整场积分（服务器按 Parts 加起来）。MVP、团队计分板按它排 */
	UPROPERTY()
	int32 Score = 0;

	/** 各分项得分，下标 EMechScorePart（伤害 / 击毁 / 助攻 / 失衡 / 控制 / 救人 / 交芯 / 截杀带芯 / 杂兵 / 迫击炮 / 首领 / 防守）；MVP 理由挑最突出的两项 */
	UPROPERTY()
	TArray<int32> Parts;

	/** 计数（铭牌、MVP 理由显示）：助攻、打出失衡、控制命中（网 / EMP / 钩爪拉人 / 弹反）、救人（钩爪救网 + 捡回队友核心）、
	 *  击毁带芯的敌人（夺芯）、保卫战击毁的杂兵（无人机 / 炮车）和迫击炮 */
	UPROPERTY()
	int32 Assists = 0;

	UPROPERTY()
	int32 Breaks = 0;

	UPROPERTY()
	int32 Controls = 0;

	UPROPERTY()
	int32 Rescues = 0;

	UPROPERTY()
	int32 CarrierKills = 0;

	UPROPERTY()
	int32 Grunts = 0;

	UPROPERTY()
	int32 Mortars = 0;

	/** 真人（掉线交给 AI 的也算） */
	UPROPERTY()
	bool bHuman = false;

	/** 配装和涂装：结算场景（MechResultsStage）在各端本地按这个重新生成展示机体（那时场上的机体可能已经销毁 / 在重生） */
	UPROPERTY()
	FMechLoadout Loadout;

	UPROPERTY()
	FMechPaint Paint;
};

/** 房间里的一个真人 */
USTRUCT()
struct FMechLobbyPlayer
{
	GENERATED_BODY()

	UPROPERTY()
	int32 PlayerId = -1;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	int32 Team = 0;

	/** 房主（能改人数、AI 强度、点开始） */
	UPROPERTY()
	bool bHost = false;

	UPROPERTY()
	FMechPaint Paint;
};

/** 死斗：阵亡、等重生的机体（HUD 画重生读条用） */
USTRUCT()
struct FMechRespawn
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<class AMechCharacter> Mech = nullptr;

	/** 服务器时间 */
	UPROPERTY()
	float DiedAt = 0.f;

	UPROPERTY()
	float RespawnAt = 0.f;
};

UCLASS()
class AMechGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	UPROPERTY(Replicated)
	EMechRoundPhase Phase = EMechRoundPhase::Waiting;

	UPROPERTY(Replicated)
	int32 Round = 0;

	UPROPERTY(Replicated)
	int32 TeamScore[2] = {0, 0};

	/** 当前阶段在服务器时间几秒结束 */
	UPROPERTY(Replicated)
	float PhaseEndTime = 0.f;

	/** 上一回合 / 整场的胜方，-1 平局或未定 */
	UPROPERTY(Replicated)
	int32 RoundWinner = -1;

	UPROPERTY(Replicated)
	int32 MatchWinner = -1;

	UPROPERTY(Replicated)
	int32 TeamSize = 2;

	UPROPERTY(Replicated)
	int32 RoundsToWin = 2;

	/** 主菜单：不开对战，只摆一台机体当背景 */
	UPROPERTY(Replicated)
	bool bMenuMode = false;

	/** 测试场：只有木桩，配装随便换（地址参数 ?Range） */
	UPROPERTY(Replicated)
	bool bRangeMode = false;

	/** 夺芯模式（地址参数 ?Core）：TeamScore 是交芯得分，先到 CoreTarget 分赢 */
	UPROPERTY(Replicated)
	bool bCoreMode = false;

	UPROPERTY(Replicated)
	int32 CoreTarget = 5;

	/** 夺芯到时同分：加时，下一分定胜负 */
	UPROPERTY(Replicated)
	bool bOvertime = false;

	/** 服务器每秒刷新一次 */
	UPROPERTY(Replicated)
	TArray<FMechResultRow> Results;

	/** 禁用 / 选配（整场一次） */
	UPROPERTY(Replicated)
	FMechDraft Draft;

	/** 房间里的真人 */
	UPROPERTY(Replicated)
	TArray<FMechLobbyPlayer> Lobby;

	/** AI 强度：0 新兵、1 普通、2 老练、3 王牌 */
	UPROPERTY(Replicated)
	int32 AILevel = 1;

	/** 开打前有没有禁用阶段（房主在房间里选；关掉就直接蛇形选配） */
	UPROPERTY(Replicated)
	bool bDraftBans = true;

	/** 地图系统（HUD 小地图 / 大地图）用的死斗场地：场地中心、AI 活动半径、两队出生点。客户端没有 GameMode，
	 *  所以 AMechGameMode::ComputeArena 算好后放这里复制给各端；MapExtent 为 0 = 还没设。夺芯用 CoreArena 自己的范围 */
	UPROPERTY(Replicated)
	FVector MapCenter = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FVector2D MapExtent = FVector2D::ZeroVector;

	UPROPERTY(Replicated)
	FVector MapSpawn[2] = {FVector::ZeroVector, FVector::ZeroVector};

	/** 对战规则（模式、团队淘汰局数、死斗个人 / 团队、目标击毁数、时间）：进房间时按房主上次的选择，房主在房间里改，开始后不变 */
	UPROPERTY(Replicated)
	FMechRules Rules;

	/** 死斗：阵亡等重生的机体（重生读条） */
	UPROPERTY(Replicated)
	TArray<FMechRespawn> Respawns;

	/** 死斗整场的胜者名字（个人死斗用；团队看 MatchWinner），空 = 平局 / 没结束 */
	UPROPERTY(Replicated)
	FString WinnerName;

	/** 进入「比赛结束」的服务器时间（-1 = 没结束）：各端的结算场景（MechResultsStage）按它同步开始 */
	UPROPERTY(Replicated)
	float MatchEndTime = -1.f;

	/** 11-01 房间图（地址 ?Room，MechRoom.h）：不开对战，只有房间界面；地图 / 模式 / 规则在 Room 里 */
	UPROPERTY(Replicated)
	bool bRoomMode = false;

	/** 11-01 这一场是从房间开的：不再进房间，结算后回房间（不是「再来一局」）；开局先等房间里的人都进图（RoomArrived / RoomExpected，
	 *  等待的截止时间是 Waiting 阶段的 PhaseEndTime） */
	UPROPERTY(Replicated)
	bool bRoomMatch = false;

	UPROPERTY(Replicated)
	int32 RoomArrived = 0;

	UPROPERTY(Replicated)
	int32 RoomExpected = 0;

	/** 11-01 服务器已经开始换图（房主点了开始 / 回房间）：按钮灰掉，显示「正在进入地图」 */
	UPROPERTY(Replicated)
	bool bRoomTravelling = false;

	/** 11-01 房主当前的选择（地图、模式、每队台数、AI 强度、禁用开关、规则）：房主每改一次服务器马上写这里，
	 *  房间里其他人照着显示（只读）。对战图里的老式房间（-MechLobbyTest 等直接进对战图的）也用它，只是地图定死 */
	UPROPERTY(Replicated)
	FMechRoomSettings Room;

	/** 11-02 这一场两队实际各几台（两队可以不一样，比分条上的小方块按这个画）；TeamSize = 两队里多的那个 */
	UPROPERTY(Replicated)
	int32 TeamCount[2] = {2, 2};

	/** 11-02 挑战倍数现在给哪一队（-1 = 没有）、这一队的装甲和伤害各乘多少；只在服务器结算（UMechHealthComponent::ApplyHit），复制给各端显示 */
	UPROPERTY(Replicated)
	int32 ChallengeTeam = -1;

	UPROPERTY(Replicated)
	float ChallengeArmor = 1.f;

	UPROPERTY(Replicated)
	float ChallengeDamage = 1.f;

	/** 11-02 训练场的友伤开着（玩家之间能互相打）；只有训练场会是 true */
	UPROPERTY(Replicated)
	bool bFriendlyFire = false;

	/** 死斗（夺芯不算） */
	bool IsDeathmatch() const { return !bCoreMode && !bRangeMode && Rules.IsDeathmatch(); }
	/** 个人死斗：人人为敌，每台机体的队伍号各不相同（从 2 开始），按个人击毁数 */
	bool IsSoloDM() const { return IsDeathmatch() && Rules.bSolo; }
	/** 死斗目标击毁数 */
	int32 KillTarget() const { return Rules.KillTarget(); }
	/** 这台机体的重生信息（没在等重生 = 空） */
	const FMechRespawn* RespawnOf(const class AMechCharacter* Mech) const;

	bool CanAct() const { return Phase == EMechRoundPhase::Fight; }
	float GetPhaseRemaining() const;

	/** 击毁通报（各端各自存一份，HUD 显示几秒） */
	struct FFeedLine
	{
		FString Text;
		int32 Team = 0;
		float Time = 0.f;
	};
	TArray<FFeedLine> Feed;

	UFUNCTION(NetMulticast, Reliable)
	void MulticastFeed(const FString& Text, int32 Team);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

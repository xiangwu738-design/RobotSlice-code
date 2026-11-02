// 11-01 房间（用户：「只单独保留开房，地图在界面里面选择。另外除了房主以外其他人看到的都只是当前的结果」）。
// 流程：主菜单「开房」→ 房间图（L_Street?Room?listen，不开对战，只有房间界面，朋友随时能连进来）→ 房主选地图、模式、规则、
//   人机名单、禁用开关，每改一次服务器马上写进 GameState::Room 复制给所有人（其他人只读，只能改自己的名字、队伍、涂装）→
//   房主开始 → ServerTravel 到选好的图（非无缝换图，客户端自动跟着重连）→ 等房间里的人都进图 → 禁用 / 选配（保卫战、训练场直接开打）→
//   回合 → 结算 → ServerTravel 回房间图（队伍、规则、地图选择、人机名单都还在，人都还连着）。
// 为什么房间单独放一张图：选地图本身就要换图，房间要是放在对战图里，房主每换一次地图大家都得重连一次。
// 跨换图要留下的东西（开房的那台）放 UMechRoomSubsystem（GameInstance 子系统，换图不销毁）。
// 每个真人按「房间令牌」认：每个进程启动时生成一个 GUID（子系统里），每进一张图发给服务器一次（AMechPlayerController::ServerRoomHello）。
// 不用 UniqueNetId：工程没开在线子系统，没有可靠的 Id（OnlineSubsystemNull 同一台机器双开时每次随机）；也不用名字：可能重名、可能没填。
// 11-02 用户：「玩家可以自由在房间里任意一队加入人机或者移除人机，双方队伍都是完全高度自定义的，你要是想 1v5 也是可以的」：
//   「每队台数」换成人机名单（FMechRoomSettings::AIs，每台单独的强度），一队的台数 = 这队的真人 + 这队的人机，两队可以不一样。个人战没有队伍。

#pragma once

#include "CoreMinimal.h"
#include "MechRules.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MechRoom.generated.h"

class AMechGameState;

/** 房主加的一台人机 */
USTRUCT()
struct FMechRoomAI
{
	GENERATED_BODY()

	/** 在哪队：0 蓝、1 红（个人战不看） */
	UPROPERTY()
	int32 Team = 0;

	/** 强度：0 新兵、1 普通、2 老练、3 王牌 */
	UPROPERTY()
	int32 Level = 1;
};

/** 房主在房间里的选择（复制给所有人看；开始时整份带到对战图） */
USTRUCT()
struct FMechRoomSettings
{
	GENERATED_BODY()

	/** 地图：MechMaps 表的序号（训练场也是表里一行） */
	UPROPERTY()
	int32 Map = 0;

	/** 保卫战（只在地图带 MechMaps::Defense 时算数）；夺芯、训练场由地图决定，不在这里 */
	UPROPERTY()
	bool bDefense = false;

	/** 11-02 人机名单：两队（个人战不分）各加几台、每台多强。一队的台数 = 这队的真人 + 这队的人机 */
	UPROPERTY()
	TArray<FMechRoomAI> AIs;

	/** 新加的人机默认多强（房主最后一次选的强度）；对局中掉线、机体交给 AI 时也按这个 */
	UPROPERTY()
	int32 AILevel = 1;

	/** 开打前有没有禁用阶段 */
	UPROPERTY()
	bool bBans = true;

	/** 模式（团队淘汰 / 死斗）和各自的数值 */
	UPROPERTY()
	FMechRules Rules;

	/** 11-02 挑战倍数（两队人数不等时给人少的一方加成，默认关）。自定义关着时用推荐值（MechRoom::RecommendedChallenge） */
	UPROPERTY()
	bool bChallenge = false;

	UPROPERTY()
	bool bChallengeCustom = false;

	/** 自定义的倍数：人少一方的装甲、伤害各乘多少 */
	UPROPERTY()
	float ChallengeArmor = 1.f;

	UPROPERTY()
	float ChallengeDamage = 1.f;

	/** 11-02 训练场的友伤开关：打开后玩家之间能互相打（只在训练场有用） */
	UPROPERTY()
	bool bFriendlyFire = false;

	/** 11-02 彩蛋（无人机群、机械狗……，MechEaster.h）：所有模式都有，默认开 */
	UPROPERTY()
	bool bEaster = true;
};

namespace MechRoom
{
	/** 场上最多几台机体（真人 + 人机）：按保卫战压力测试（docs/保卫战_压力测试_20261031.md）「场上一共 10 台」，再多游戏线程吃不消 */
	constexpr int32 MaxMechs = 10;

	/** 房间图（和主菜单同一张；房间里不生成机体、不开回合） */
	inline const TCHAR* RoomMapPath() { return TEXT("/Game/Maps/L_Street"); }
	/** 回房间的 ServerTravel 地址 */
	FString RoomURL();
	/** 开始对战的 ServerTravel 地址：夺芯图 ?Core，保卫战 ?Defense，训练场 ?Range，其余 ?Match?Mode=Elim|DM；都带 ?listen。台数不走地址（子系统里带过去） */
	FString MatchURL(const FMechRoomSettings& S);
	/** 夹到合法范围：地图越界、地图不支持的模式换成它支持的、人机的队伍和强度、规则（网络上收到的都过一遍） */
	void Sanitize(FMechRoomSettings& S);
	/** 选的图是不是夺芯图 / 训练场 */
	bool IsCore(const FMechRoomSettings& S);
	bool IsTraining(const FMechRoomSettings& S);
	/** 这个模式分不分两队、能不能加人机（保卫战都在一边、训练场自由练习，都不能） */
	bool UsesTeams(const FMechRoomSettings& S);
	bool UsesAI(const FMechRoomSettings& S);
	/** 个人战（没有队伍）。bRoomMap = 在房间图里（夺芯看选的图）；不在房间图（老式房间、对战图里）看 bCoreNow */
	bool IsSolo(const FMechRoomSettings& S, bool bRoomMap, bool bCoreNow);
	/** 表里第一张支持这个模式的图（MechMaps::Core 等） */
	int32 FirstMapFor(uint8 Flag);
	/** 现在一队几台：这队的真人（房间名单）+ 这队的人机；Team = -1 两队加起来 */
	int32 CountOf(const FMechRoomSettings& S, const AMechGameState& GS, int32 Team);
	/** 房主现在开不了的原因（某队没人、个人战不够两台）；空 = 能开 */
	FText StartProblem(const FMechRoomSettings& S, const AMechGameState& GS);
	/** 11-02 推荐的挑战倍数（人少一方的装甲、伤害各乘这么多）：(多的人数 / 少的人数)^0.6，取到 0.05。公式只在这一处 */
	float RecommendedChallenge(int32 CountA, int32 CountB);
	/** 现在生效的挑战倍数：OutTeam = 人少的那队（-1 = 没有：开关关着、两队一样多、不分队的模式），OutArmor / OutDamage = 这队的倍数（自定义关着时都是推荐值） */
	void EffectiveChallenge(const FMechRoomSettings& S, bool bSolo, int32 Count0, int32 Count1, int32& OutTeam, float& OutArmor, float& OutDamage);
	/** 模式名（房间界面只读显示、顶栏）：团队淘汰 / 团队死斗 / 个人死斗 / 夺芯 / 保卫战 / 训练场 */
	FText ModeName(const FMechRoomSettings& S);
	/** 只有规则：「团队淘汰 · 三局两胜」 */
	FString DescribeRules(const FMechRoomSettings& S);
	/** 一句话：「赛博朋克街道 · 团队淘汰 · 三局两胜」 */
	FString Describe(const FMechRoomSettings& S);
	/** 主菜单开新房时的初始选择：上次存的（地图 / 保卫战 / 人机名单 / 倍数 / 友伤在 GameUserSettings.ini 的 [MechRoom]，规则和禁用开关在 FMechSettings）；
	 *  命令行带 -Mech 开头的测试开关时用默认值，免得被用户存的影响 */
	FMechRoomSettings LoadSaved();
	/** 房主开始时存下这次的选择（测试开关下不存，免得改掉用户的设置） */
	void Save(const FMechRoomSettings& S);
}

UCLASS()
class UMechRoomSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	static UMechRoomSubsystem* Get(const UObject* WorldContext);

	/** 本进程的房间令牌（每次启动不同；客户端进图后发给服务器认人） */
	FString Token;

	// ---- 下面只在开房的那台（服务器）上有意义 ----

	/** 正在开房：主菜单点「开房」（或直接用 ?Room 地址启动）起，回主菜单 / 去加入别人止 */
	bool bActive = false;
	/** 房主当前的选择（房间图开局时恢复到 GameState，房主每改一次同步回来） */
	FMechRoomSettings Settings;

	struct FMember
	{
		/** 队伍 0 / 1（个人死斗时只是选配分组） */
		int32 Team = 0;
		/** 「玩家 N」的编号：换图重连后还用同一个 */
		int32 Number = 0;
	};
	/** 来过房间的真人（按令牌）：换图重连后按这个恢复队伍 */
	TMap<FString, FMember> Members;
	int32 NextNumber = 0;

	/** 房主点了开始、正在去对战图：对战图开局时读到就清掉（读到 = 这一场从房间开，跳过房间、结算后回房间） */
	bool bMatchPending = false;
	/** 开始那一刻在房间里的真人（令牌）：对战图等他们都进来（或超时）再开禁用 / 选配 */
	TArray<FString> Expected;

	/** 主菜单点开房：清掉上一次的成员，选择按上次存的 */
	void BeginRoom();
	/** 回主菜单 / 去加入别人的房 */
	void EndRoom();
};

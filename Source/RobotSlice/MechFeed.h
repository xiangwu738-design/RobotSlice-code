// 信息区（11-01，类似 CS2 右上角，用户：「一个信息区域，类似于 CS2，可以显示队友使用技能什么的，这个区域可以选择性关闭」）。
// 事件都在服务器产生，按「该看的人」发给各个玩家（UMechCommsComponent::ClientFeed，MechComms.h），各端自己存几行、HUD 画（MechHudFeed.cpp）：
//   击毁：所有人（接管原来 GameState 的击毁播报，名字用敌我色 + 中间一个武器小图标，自己相关的那行高亮）
//   队友用肩部装备、拿到插件、标点：只发给同队（敌人的技能不显示；自己用的也不发给自己——自己知道）
//   夺芯拿起 / 交芯：所有人（核心是公开目标）；保卫战波次开始 / 指挥所被打：所有人（全员一队）
// 用了什么技能不改技能代码：服务器每帧看每台机体的肩部冷却有没有突然变长（= 刚用了一次），核心、保卫战也是轮询状态。
// 击毁的武器：AMechCharacter::ReceiveHit 里记下最后一下的类别（子弹 / 刀 / 爆炸，MechFeed::NoteHit），阵亡时按击毁者的配装细分图标。
// 插件：AMechPluginPod 拿到时调 MechFeed::OnPlugin。设置里关掉信息区（FMechSettings::bShowFeed）只是不画，事件照收。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MechTypes.h"
#include "MechFeed.generated.h"

class AMechCharacter;
class AController;

UENUM()
enum class EMechFeed : uint8
{
	Kill,       // A 击毁了 B（Icon = 武器）
	Skill,      // A 用了肩部装备（Icon）
	Plugin,     // A 拿到插件（Plugin）
	CorePick,   // A 拿起核心
	CoreScore,  // A 交芯（送回核心）
	Ping,       // A 标了点（Icon = EMechPing）
	Wave,       // 保卫战：第 Num 波开始
	PostHit     // 保卫战：指挥所被打（剩 Num%）
};

/** 信息区的小图标编号（击毁的武器、用的技能）：射击武器 / 近战 / 肩部按各自枚举顺序排在一起，后面是几种没有配装的来源 */
namespace MechFeedIcon
{
	constexpr uint8 Shooter0 = 0;    // + EMechShooter（6 种）
	constexpr uint8 Melee0 = 6;      // + EMechMeleeWeapon（7 种）
	constexpr uint8 Shoulder0 = 13;  // + EMechShoulder（11 种；11-01 战术装备并入肩部，编号正好接上原来的战术 20–23）
	static_assert(Shoulder0 + static_cast<int32>(EMechShoulder::Mine) + 1 == 24, "下面的 Blast 要接在肩部后面");
	constexpr uint8 Blast = 24;      // 爆炸（说不清是哪件的导弹类伤害）
	constexpr uint8 Field = 25;      // 场地（掉出地图、没有击毁者）
	constexpr uint8 GruntGun = 26;   // 保卫战：机枪无人机
	constexpr uint8 GruntShell = 27; // 保卫战：炮车 / 迫击炮
	constexpr uint8 GruntBoom = 28;  // 保卫战：自爆无人机

	/** 技能的简称（信息区图标后面那一小行字；击毁不写武器名） */
	const TCHAR* Word(uint8 Icon);
}

/** 一条信息（服务器发给客户端；名字都是显示名，队伍号让各端自己按敌我上色） */
USTRUCT()
struct FMechFeedEvent
{
	GENERATED_BODY()

	UPROPERTY()
	EMechFeed Kind = EMechFeed::Kill;

	/** 武器 / 技能（MechFeedIcon）或标点种类（EMechPing） */
	UPROPERTY()
	uint8 Icon = 0;

	UPROPERTY()
	FName Plugin;

	/** 主角：击毁者 / 用技能的 / 拿插件的 / 标点的（击毁者是场地时为空） */
	UPROPERTY()
	FString A;

	/** 被击毁的 */
	UPROPERTY()
	FString B;

	UPROPERTY()
	int32 TeamA = -1;

	UPROPERTY()
	int32 TeamB = -1;

	/** 波次号、指挥所剩余百分比 */
	UPROPERTY()
	int32 Num = 0;
};

/** 服务器上轮询技能 / 核心 / 保卫战、接阵亡事件，按规则发信息（只在服务器 tick；客户端上它什么都不做） */
UCLASS()
class UMechFeedSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void NoteHit(AMechCharacter* Victim, AActor* Attacker, EMechHitKind Kind);
	void OnPlugin(AMechCharacter* Mech, FName Plugin);
	/** 发给该看的人：OnlyTeam >= 0 只发给这一队的玩家；Except 不发（用技能的本人） */
	void Send(const FMechFeedEvent& E, int32 OnlyTeam, const AController* Except);

	/** 测试统计：发出去几条（按种类） */
	int32 DebugSent[8] = {0, 0, 0, 0, 0, 0, 0, 0};

private:
	void OnMechDied(AMechCharacter* Victim, AActor* Killer);
	void PollCores();
	void PollDefense();

	struct FTrack
	{
		/** 已经接上阵亡事件、记下初始冷却 */
		bool bBound = false;
		float Shoulder = 0.f;
		/** 开始跟踪的时间：刚生成的头一秒不算（有的冷却出生后才设） */
		float Since = 0.f;
		/** 上一次发技能信息的时间（同一台同一件 8 秒内不重复发） */
		float ShoulderSent = -100.f;
		uint8 ShoulderSentIcon = 255;
		/** 最后一下伤害的类别 */
		EMechHitKind LastKind = EMechHitKind::Bullet;
		bool bHit = false;
	};
	TMap<TWeakObjectPtr<AMechCharacter>, FTrack> Tracks;
	float Clock = 0.f;

	/** 夺芯：每颗核心上一帧的状态和带着的人 */
	struct FCoreSeen
	{
		bool bCarried = false;
		TWeakObjectPtr<AMechCharacter> Carrier;
	};
	TMap<int32, FCoreSeen> CoreSeen;
	int32 ScoreSeen[2] = {0, 0};

	/** 保卫战：上一帧是不是在打（准备 → 开打 = 新一波开始）、指挥所血量、上一次发「指挥所被打」的时间 */
	bool bWaveFightSeen = false;
	float PostHPSeen = -1.f;
	float PostSent = -100.f;
};

namespace MechFeed
{
	/** 服务器：AMechCharacter::ReceiveHit 结算前调，记下最后一下是什么打的 */
	void NoteHit(AMechCharacter* Victim, AActor* Attacker, EMechHitKind Kind);
	/** 服务器：插件舱被拿走 */
	void OnPlugin(AMechCharacter* Mech, FName Plugin);
}

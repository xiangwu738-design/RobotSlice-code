// 背景音乐（11-01，用户：「我想加几个音乐」，曲子是用户从自己的 D:\Music 里给的，我挑的位置见 docs/音乐推荐_20261101.md）。
// 每台机器自己放、不联机同步：每帧按世界状态选这会儿该放哪首——主菜单 / 房间 / 禁用选配 / 开场运镜 / 团队淘汰和夺芯的回合 /
// 赛点或只剩最后一台 / 死斗 / 保卫战（准备、普通波、第 5 波和最后一波首领、胜利）/ 结算 / 测试场和模型查看。
// 换曲交叉淡入淡出；曲子都不设循环（原曲有前奏和淡出），快放完时从头交叉淡入一遍。
// 音量 = 设置里的「音乐音量」× 每个位置自己的倍数（主音量由引擎统一乘）；回合里压得低，免得盖住锁定、导弹来袭这些声音提示。
// 放我们的结算曲时把结算场景原来的配乐（mech.Results.Music）压成 0，免得两首叠在一起。
// 关掉：命令行 -MechNoMusic，或控制台 mech.Music 0；mech.Music.Debug 1 每次换曲打日志（换曲本身总会打一行 [music]）。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MechMusic.generated.h"

class UAudioComponent;
class USoundBase;

enum class EMechMusic : uint8
{
	None,
	Menu,     // 主菜单
	Room,     // 房间
	Draft,    // 禁用 / 选配（以及从房间开过来、等大家进图的那几秒）
	Intro,    // 开场运镜
	Round,    // 团队淘汰 / 夺芯的回合（纯器乐、压低）
	Clutch,   // 赛点 / 只剩最后一台
	Battle,   // 死斗、保卫战普通波
	DefPrep,  // 保卫战波次之间的准备
	Boss1,    // 保卫战第 5 波
	Boss2,    // 保卫战最后一波
	Results,  // 结算
	DefWin,   // 保卫战守满
	Range,    // 测试场、模型查看
	Count
};

UCLASS()
class UMechMusic : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }

	/** 这会儿在放哪个位置的曲子（测试 / 日志用） */
	EMechMusic GetCurrent() const { return Current; }

private:
	/** 按世界状态选该放哪个位置 */
	EMechMusic Pick();
	/** 换到这个位置：旧的淡出，新的从头淡入 */
	void Switch(EMechMusic To, float FadeOut, float FadeIn);
	/** 起一个新的音频组件放这个位置的曲子（同一首快放完时也用它从头再来） */
	void StartVoice(EMechMusic Slot, float FadeIn);
	/** 结算场景原来的配乐：放我们的结算曲时压成 0，离开时还原 */
	void MuteStageMusic(bool bMute);

	struct FVoice
	{
		TWeakObjectPtr<UAudioComponent> Comp;
		EMechMusic Slot = EMechMusic::None;
		/** 放了几秒（按真实时间算，暂停也走） */
		float Age = 0.f;
		float Duration = 0.f;
		/** 淡入淡出：当前 0..1、目标、每秒变化多少 */
		float Fade = 0.f;
		float FadeTarget = 1.f;
		float FadeRate = 1.f;
	};
	TArray<FVoice> Voices;

	/** 防 GC：所有还在响的组件 */
	UPROPERTY()
	TArray<TObjectPtr<UAudioComponent>> Comps;

	EMechMusic Current = EMechMusic::None;
	/** 赛点 / 最后一台一旦切进来，这一回合不再切回普通回合曲 */
	bool bClutchHeld = false;
	int32 ClutchRound = -1;
	bool bStageMuted = false;
	float StageMusicBefore = 1.f;
};

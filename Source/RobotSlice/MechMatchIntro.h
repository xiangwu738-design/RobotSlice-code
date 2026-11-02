// 开场预热：整场第一回合开打前，赛事解说员几句开场（中文配音）+ 对应的运镜，镜头飞过地图看点、最后落到双方出生点 / 选手身上。
// 流程：选配结束 → BeginRound 生成机体（倒计时阶段、机体不能动）→ 本段运镜（倒计时往后推运镜时长）→ 正常 3 秒倒计时（解说补一句倒计时）→ 开打。
// 数据驱动（改了不用重新编译）：
//   Content/Intro/MechIntro.txt       每张地图一段：标题、台词套、镜头表（路径关键帧或自动「队伍」镜头），格式见文件开头
//   Content/Intro/MechIntroVoice.txt  每句台词的时长（SourceAssets/voice_intro/tools/analyze.py datafile 生成），声音 /Game/Audio/Intro/S_intro_<编号>
// 联机：服务器挑台词套 / 开场句 / 倒计时句、记下开始的服务器时间，本 actor 复制给各端，各端按服务器时间在本机播镜头和声音。
// 任意键跳过（本机马上回到自己机体）；所有真人都跳过了，服务器提前进倒计时。
// 不播的情况：自动测试（只在正常开局流程 = 走房间时播）、-MechNoIntro；测试 -MechIntroTest=<地图键>（见 README）。
// 11-01 用户：「开场的配音暂时全部删除」→ 配音默认关（运镜、地图标题照播，镜头按标称时长），控制台 mech.Intro.Voice 1 / 命令行 -MechIntroVoice 恢复（MechMatchIntro.cpp）。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechMatchIntro.generated.h"

class ACameraActor;
class AGameModeBase;
class APlayerController;
class UAudioComponent;
class USoundBase;
class SMechIntroOverlay;
class FMechIntroInput;
struct FMechIntroMap;
struct FMechIntroShot;

UCLASS(NotPlaceable)
class AMechMatchIntro : public AActor
{
	GENERATED_BODY()

public:
	AMechMatchIntro();

	/** 服务器：整场第一回合生成完机体后调用。要播就生成一个开场 actor（复制给各端），返回运镜时长（秒，倒计时往后推这么多）；不播返回 0。
	 *  bNormalFlow = 正常开局流程（走了房间）；测试开关 -MechIntroTest 时也播 */
	static float BeginForMatch(AGameModeBase* Mode, bool bNormalFlow);

	/** 服务器：这个真人按键跳过了；所有真人都跳过了就提前进倒计时 */
	void PlayerSkipped(APlayerController* PC);

	/** 本机正在播运镜 */
	static bool IsPlayingLocally(const UWorld* World);

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 本机：按了键（输入预处理器转过来） */
	void LocalSkipRequested();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// ---------------------------------------------------------------- 服务器定、复制给各端
	/** MechIntro.txt 里的地图键（street / nature / valley / core / techpark / city / default） */
	UPROPERTY(Replicated)
	FString MapKey;

	/** 这一场用的台词：开场句、三句地图台词、倒计时句（编号，MechIntroVoice.txt 里的） */
	UPROPERTY(Replicated)
	TArray<FString> Lines;

	UPROPERTY(Replicated)
	FString CountLine;

	/** 运镜开始的服务器时间、总长 */
	UPROPERTY(Replicated)
	float StartTime = 0.f;

	UPROPERTY(Replicated)
	float Length = 0.f;

	/** 所有真人都跳过了：服务器时间几秒结束（-1 = 没跳过） */
	UPROPERTY(Replicated)
	float CutTime = -1.f;

	/** 锚点（镜头表里 S0 / S1 / C 的位置）：两队机体的中心和朝向、场地中心，服务器按刚生成的机体算 */
	UPROPERTY(Replicated)
	FVector Anchor[2];

	UPROPERTY(Replicated)
	float AnchorYaw[2] = {0.f, 180.f};

	UPROPERTY(Replicated)
	FVector Center = FVector::ZeroVector;

	/** 个人死斗（「队伍」镜头改成自己 / 别人） */
	UPROPERTY(Replicated)
	bool bSolo = false;

	TSet<int32> SkippedIds;

	// ---------------------------------------------------------------- 本机播放
	void StartLocal(APlayerController* PC);
	void UpdateLocal(APlayerController* PC, float T);
	void FinishLocal(APlayerController* PC, bool bSkipped);
	/** 按服务器时间算的本段时长（跳过了就短） */
	float EndAt() const;
	float ServerNow() const;
	void BuildTimeline();
	/** 镜头表里的点 → 世界坐标 */
	FVector Resolve(int32 AnchorKind, bool bScaled, const FVector& V) const;
	/** 同上，带 G 前缀的高度按地面算 */
	FVector ResolvePoint(int32 AnchorKind, bool bScaled, bool bGround, const FVector& V) const;
	/** 镜头在地形下面 / 贴得太近就抬上来（只认地形，不管高架桥、树） */
	void KeepAboveLandscape(FVector& Pos) const;
	/** 「队伍」镜头：按本机看到的机体位置取景 */
	void TeamShot(const FMechIntroShot& Shot, float U, FVector& OutPos, FVector& OutLook, float& OutFov) const;
	int32 OwnTeam() const;
	void PlayLine(const FString& Id, float Offset);
	/** -MechIntroTest：逐秒截图、最后退出 */
	void TestTick(float T);

	struct FShotRun
	{
		int32 Shot = 0;
		float Start = 0.f;
		float Duration = 0.f;
		/** 这一镜开始时播的台词（Lines 的下标，-1 没有） */
		int32 Line = -1;
	};
	TArray<FShotRun> Runs;
	TSharedPtr<FMechIntroMap> Map;
	FString Title;
	FString Subtitle;

	bool bLocalStarted = false;
	bool bLocalDone = false;
	bool bCountPlayed = false;
	bool bSkipSent = false;
	bool bSkipRequested = false;
	/** 倒计时句的说话时长 */
	float CountSpeech = 0.f;
	bool bPrevShowHUD = true;
	int32 CurrentShot = -1;
	/** 当前这一镜算好的关键帧点 */
	int32 CachedShot = -1;
	TArray<FVector> CachedPos;
	TArray<FVector> CachedLook;
	TSet<int32> PlayedLines;
	float LocalClock = 0.f;

	UPROPERTY()
	TObjectPtr<ACameraActor> Cam;

	UPROPERTY()
	TArray<TObjectPtr<UAudioComponent>> Voices;

	TSharedPtr<SMechIntroOverlay> Overlay;
	TSharedPtr<FMechIntroInput> Input;

	// 测试
	bool bTest = false;
	int32 TestShot = -1;
	float TestQuitAt = -1.f;
};

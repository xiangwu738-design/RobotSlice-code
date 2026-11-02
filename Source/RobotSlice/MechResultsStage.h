// 比赛结束的实时结算场景（10-31，用户：「游戏结束加一个实时结算渲染，你可以参考 CS2」）。
// 整场结束后不直接弹表，先进一个用玩家相机实时渲染的 3D 场景：每台机体按自己真实的配装（框架、枪、刀、肩部无人机）和涂装站在台上，
// 镜头缓慢推移（先低机位横移全景 → 推到 MVP 环绕 → 拉回正面全景停住）；
//   团队淘汰 / 团队死斗 / 夺芯：胜方前排、站姿昂扬（MVP 居中靠前竖大拇指），败方后排、上身前倾低着；平局两队左右并排；
//   个人死斗：前三名站领奖台（中间最高），其余在后排（MVP 不在前三名时站到领奖台左边地上）。
// MVP（11-01 改成积分制，MechScore.h）：团队 = 胜方里（平局 = 全场）积分最高的；个人死斗 = 全场积分最高（名次 / 胜者照旧按击毁，MVP 可以不是第一名）。
//   单独一盏聚光 + 金色地环 + 光柱；特写字幕多一行理由「SCORE 1840 · AST 6 · CORE 2」（积分 + 最突出的两项）。
// 每台脚下（后排在头顶）一块铭牌：名字、SCORE 积分（大字 + 一道条）、KO / AST / DTH / DMG（夺芯多 CORE）数字 + 条形；没有中文提示句子（只有名字可以是中文）。
// 停留约 10 秒（或按任意键）后打开原来的结算表，场景留在结算表后面（镜头把机体让到面板右边），下一局开始时收掉。
// 联机：服务器进入「比赛结束」时记下服务器时间（AMechGameState::MatchEndTime），各端（含观战的）按这个时间在本机生成场景、
//   跑同一条时间轴；场上的机体那时可能已经销毁 / 在重生，展示机体按 GameState 里复制的结算行（名字、队伍、配装、涂装、统计）重新生成，
//   网格按 MechCatalog::FrameMesh（以后各家外壳从这里换）。
// 场景：场地下方很远处（StageOrigin）单独搭一个封闭的暗场（地面有反射、背景墙淡网格 + 队伍色氛围光、光柱），
//   物件和灯只在灯光通道 2（不吃地图的太阳，也不吃机体自带的补光），固定曝光；结算期间本机视口关掉雾 / 天光 / 大气 / 云 / 平行光 /
//   地形 / 植被（地图光照影响不到，也不白渲地图），结束时恢复。材质 /Game/Results/M_Results*（tools/ue_make_results_fx.py 生成），没有就退回基础材质。
// 测试：-MechResultsTest=Elim|DMTeam|DMSolo|Core（假数据直接进结算场景，截图到 Saved/Screenshots/Results 后退出）；
//   -MechAutoBattle 打完自动截几帧到同一目录；-MechNoResultsStage 关掉（直接弹结算表）。调光用控制台变量 mech.Results.*。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechGameState.h"
#include "MechResultsStage.generated.h"

class ACameraActor;
class AGameModeBase;
class UAudioComponent;
class USoundBase;
class USpotLightComponent;
class AMechCharacter;
class AMechPlayerController;
class FMechResultsInput;
class SMechResultsOverlay;
class ULightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(NotPlaceable, Transient)
class AMechResultsStage : public AActor
{
	GENERATED_BODY()

public:
	AMechResultsStage();

	/** 本机控制器看到「比赛结束」时调：要播就在本机生成结算场景，返回 true（结算表等场景播完再开）；不播返回 false（照旧马上弹表） */
	static bool BeginFor(AMechPlayerController* PC);
	/** 本机正在显示结算场景（阵亡视角、Esc 暂停让开） */
	static bool IsActive(const UWorld* World);
	/** 服务器：比赛结束后比原来多等几秒（结算场景的时长；之后结算表照旧停 MatchOverTime）。没画面的单机逻辑测试不等 */
	static float ExtraMatchOverTime(const UWorld* World);
	/** -MechResultsTest=Elim|DMTeam|DMSolo|Core：GameMode 开局时调，填假数据、直接起结算场景；返回 true = 是测试（GameMode 不再开对战） */
	static bool SetupTest(AGameModeBase* Mode);

	virtual void Tick(float DeltaSeconds) override;

	/** 本机按了键（输入预处理器转过来）：跳过镜头、马上开结算表 */
	void RequestSkip();

	/** 一台展示机体：结算行 + 站位 + 姿势 */
	struct FEntry
	{
		FMechResultRow Row;
		/** 前排（胜方 / 领奖台 / 平局两队）；后排 = 败方、个人死斗第四名起 */
		bool bFront = true;
		bool bMVP = false;
		bool bSelf = false;
		/** 个人死斗名次（1 起）；团队 = 队内名次 */
		int32 Rank = 0;
		/** 领奖台高度（厘米，个人死斗前三名） */
		float Plinth = 0.f;
		/** 脚下的位置（场景坐标：+X 往里、+Y 往右，镜头在 -X 一侧往 +X 看）和朝向 */
		FVector Local = FVector::ZeroVector;
		float Yaw = 180.f;
		/** 姿势：竖大拇指（枪收到背上）、上身前倾（度，负 = 挺胸）、拿刀（打刀多于打枪的）、枪口往下压多少度 */
		bool bThumbs = false;
		float Lean = 0.f;
		bool bBlade = false;
		float AimDown = 35.f;
		/** 机体发光件的队伍色（0 蓝 / 1 红；个人死斗自己蓝、别人红） */
		int32 GlowTeam = 0;
		TWeakObjectPtr<AMechCharacter> Mech;
	};

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	friend class SMechResultsOverlay;

	static AMechResultsStage* Start(AMechPlayerController* PC, bool bTest, const FString& TestTag);

	// ---------------------------------------------------------------- 时间轴（秒）
	/** 比赛结束 → 切到结算场景（这段还看着场上最后一下） */
	static constexpr float PreRoll = 2.f;
	/** 比赛结束后多久开始生成展示机体（一帧一台，切过去之前姿势、无人机、贴图都稳了） */
	static constexpr float SpawnAt = 0.4f;
	/** 切换前压黑、切换后淡入 */
	static constexpr float FadeOutLen = 0.45f;
	static constexpr float FadeInLen = 0.7f;
	/** 镜头路径总长；场景开始后几秒打开结算表 */
	static constexpr float PathLen = 10.5f;
	static constexpr float TableAt = 10.f;
	/** 10-31：MVP 头顶的聚光、光柱在场景第几秒「打亮」（之前只有很暗的一点），配一声大灯合闸 */
	static constexpr float SpotOnAt = 0.65f;
	/** 铭牌：第 i 块在场景第 PlateAt + i × PlateStep 秒滑出来，数字从出来后 CountFrom 秒数到 CountTo 秒（画面和数字跳动的声音共用） */
	static constexpr float PlateAt = 0.8f;
	static constexpr float PlateStep = 0.11f;
	static constexpr float CountFrom = 0.15f;
	static constexpr float CountTo = 1.25f;
	/** 第 i 块铭牌的数字数到了几成（0–1） */
	static float PlateCount(int32 i, float S);

	/** 服务器时间（单机就是本地时间） */
	double ServerNow() const;
	/** 比赛结束后第几秒 */
	float SinceEnd() const;

	// ---------------------------------------------------------------- 布置
	void BuildEntries(const AMechGameState* GS);
	void BuildStage();
	void BuildCamera();
	void SpawnNextMech();
	void SetupMech(FEntry& E);
	void Cut();
	void Finish();
	void OpenTable();
	void UpdateCamera(float S, float DeltaSeconds);
	void UpdateLook();
	void TestTick(float S);
	FVector ToWorld(const FVector& Local) const { return StageOrigin + Local; }

	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, UMaterialInterface* Mat, const FVector& At, const FRotator& Rot, const FVector& Scale);
	UMaterialInstanceDynamic* FloorMID(const FLinearColor& Color, float Roughness, const FLinearColor& Grid, float GridIntensity, float GridSize, float FadeRadius, bool bWall);
	UMaterialInstanceDynamic* GlowMID(const FLinearColor& Color, float Strength, float Radius, float Width, float Disc, float Solid);
	void AddRing(const FVector& Feet, float Size, const FLinearColor& Color, float Strength, float Radius, float Width, float Disc);
	/** 光柱（假体积光）；EdgeExp 越大边越利落、AlongExp 越小越能亮到底；SoftDist：后面这么近（厘米）有东西就淡掉（盖在机体前面的那一层不罩一层白雾）。
	 *  返回材质（MVP 的光柱要跟着「打亮」动画改亮度） */
	UMaterialInstanceDynamic* AddBeam(const FVector& Top, const FVector& Bottom, float BottomRadius, const FLinearColor& Color, float Strength,
		float EdgeExp = 1.6f, float AlongExp = 0.9f, float SoftDist = 70.f);
	class USpotLightComponent* AddSpot(const FVector& At, const FVector& Target, const FLinearColor& Color, float Candela, float Outer, float Inner,
		bool bShadows, float SourceRadius = 30.f, float Radius = 6000.f);

	// ---------------------------------------------------------------- 状态
	TWeakObjectPtr<AMechPlayerController> OwnerPC;
	bool bTest = false;
	FString TestTag;
	/** 本机截几帧（-MechResultsTest / -MechAutoBattle） */
	bool bShots = false;
	TArray<float> ShotTimes;
	int32 NextShot = 0;
	float QuitAt = -1.f;
	/** 比赛结束的服务器时间（GameState 复制来的；没到时先用本机看到的那一刻） */
	double EndTime = 0.0;
	bool bEndFromServer = false;
	FVector StageOrigin = FVector::ZeroVector;

	TArray<FEntry> Entries;
	int32 SpawnCursor = 0;
	int32 MVPIndex = INDEX_NONE;
	/** 11-01 积分制：MVP 理由（「SCORE 1840  ·  AST 6  ·  CORE 2」，MechScore::Reason），特写字幕里显示 */
	FString MVPReason;
	bool bSolo = false;
	bool bCore = false;
	bool bDraw = false;
	int32 Winner = -1;
	FString SelfName;
	int32 SelfTeam = -1;
	/** 场景物件、灯（材质 / 灯光通道统一设） */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Meshes;
	UPROPERTY()
	TArray<TObjectPtr<ULightComponent>> Lights;
	/** 灯原来的亮度（控制台变量 mech.Results.Light 按比例调） */
	TArray<float> LightBase;
	TArray<TPair<TObjectPtr<UMaterialInstanceDynamic>, float>> BeamMIDs;
	UPROPERTY()
	TObjectPtr<ACameraActor> Cam;

	// 资源
	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaneMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FloorMat;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMat;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BeamMat;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlainMat;
	bool bCustomMats = false;

	// 镜头
	struct FCamKey
	{
		float T = 0.f;
		FVector Pos = FVector::ZeroVector;
		FVector Look = FVector::ZeroVector;
		float Fov = 36.f;
	};
	TArray<FCamKey> Keys;
	/** 前排中心、整组半宽、MVP 胸口（场景坐标） */
	FVector GroupCenter = FVector::ZeroVector;
	float GroupHalfW = 400.f;
	/** 全景的视野和镜头距离（按整组宽度算；封闭场景的前墙、侧墙也按它放，镜头不会跑到墙外面） */
	float WideFov = 36.f;
	float WideDist = 2000.f;
	FVector MVPChest = FVector(0.f, 0.f, 165.f);
	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	float CamFov = 36.f;
	/** MVP 特写程度 0–1（铭牌让开、MVP 下方字幕出来） */
	float CloseUp = 0.f;

	bool bCut = false;
	bool bTableOpen = false;
	bool bSkipped = false;
	float SkipS = -1.f;
	/** 结算表打开后 0→1（镜头把机体让到面板右边、铭牌淡出） */
	float TableBlend = 0.f;
	float StageClock = 0.f;
	bool bPrevShowHUD = true;
	/** 场景镜头那 10 秒的真实帧时间（毫秒，截图那两帧不算），开表时打日志 */
	TArray<float> FrameMs;
	uint64 SkipFramesUntil = 0;
	/** 本机视口原来的显示开关（结束时恢复） */
	struct FFlags
	{
		bool bFog = true, bVolFog = true, bSky = true, bAtmo = true, bCloud = true, bDirectional = true, bLandscape = true, bFoliage = true, bGrass = true;
	} SavedFlags;
	bool bFlagsSaved = false;

	TSharedPtr<SMechResultsOverlay> Overlay;
	TSharedPtr<FMechResultsInput> Input;

	// ---------------------------------------------------------------- 10-31 MVP 聚光「打亮」：之前灯和光柱是暗的，SpotOnAt 那一刻合闸（一下冲到头、轻轻回弹一下）
	void UpdateSpot(float S);
	UPROPERTY()
	TObjectPtr<USpotLightComponent> MVPSpot;
	float MVPSpotBase = 0.f;
	float LastSpotKey = -1.f;
	/** MVP 的光柱、地环：材质 + 原来的亮度（在 BeamMIDs 里，这里每帧按打亮动画再乘一次） */
	TArray<TPair<TObjectPtr<UMaterialInstanceDynamic>, float>> MVPGlows;

	// ---------------------------------------------------------------- 10-31 配乐和音效（各端按同一条时间轴在本机放，跟主音量走）
	void LoadAudio();
	void UpdateAudio(float S);
	/** 放一个 2D 声音（界面声音：暂停时也响），打日志「[results] 声音 场景第几秒 名字」 */
	UAudioComponent* Cue(const TCHAR* Name, USoundBase* Sound, float Volume, float Pitch = 1.f, float StartAt = 0.f, bool bLog = true, float FadeIn = 0.f);
	void FadeOutAudio(float Seconds);
	UPROPERTY()
	TObjectPtr<USoundBase> SndMusic;
	UPROPERTY()
	TObjectPtr<USoundBase> SndBed;
	UPROPERTY()
	TObjectPtr<USoundBase> SndWhooshIn;
	UPROPERTY()
	TObjectPtr<USoundBase> SndWhooshOut;
	UPROPERTY()
	TObjectPtr<USoundBase> SndSpot;
	UPROPERTY()
	TObjectPtr<USoundBase> SndTick;
	UPROPERTY()
	TObjectPtr<USoundBase> SndLock;
	UPROPERTY()
	TObjectPtr<USoundBase> SndTable;
	UPROPERTY()
	TObjectPtr<UAudioComponent> Music;
	UPROPERTY()
	TObjectPtr<UAudioComponent> Bed;
	bool bAudioLoaded = false;
	bool bMusicCued = false;
	bool bSpotCued = false;
	bool bWhooshInCued = false;
	bool bWhooshOutCued = false;
	bool bLockCued = false;
	bool bTableCued = false;
	/** 数字跳动：上一次「看得见的数字」的和、上一下的时间、一共响了几下 */
	int64 LastDigits = -1;
	float LastTickS = -10.f;
	float FirstTickS = -1.f;
	int32 TickCount = 0;
	/** -MechResultsRecord：把本机主混音录成 wav（Saved/Audio/Results），证明哪一刻放了什么；-MechResultsMute 时扬声器不出声（录音照录） */
	bool bRecording = false;
	float RecordStartS = 0.f;
	void StopRecording();

	// ---------------------------------------------------------------- 10-31 测试：-MechResultsPoseShots 场景第 3 秒起镜头挨个对准每台机体正面拍一张（看站姿），日志记每台上身拧了多少
	void PoseShotTick(float S);
	void LogPoses(const TCHAR* When) const;
	int32 PoseShot = -1;
	float PoseShotAt = 0.f;
	bool bPoseShots = false;
	bool bPoseLogged = false;
};

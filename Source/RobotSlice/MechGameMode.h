// 对战规则（模式和数值见 MechRules.h，房主在房间里定）。每队 TeamSize 台（2v2 / 3v3），空位由 AI 补。
// 团队淘汰：一方全部被击毁算输一回合，先赢 RoundsToWin 回合的队伍获胜；每回合重新生成所有机体：
//   倒计时 3 秒（不能动）→ 开打（限时，超时按剩余装甲比例判）→ 回合结算 → 下一回合。
// 死斗：只有一回合，阵亡 MechRules::RespawnDelay 秒后在离敌人远的点重生（配装不变、插件效果清空、短暂重生保护），
//   击毁一台 +1，先到目标或时间到分高者赢。个人死斗每台机体的队伍号各不相同（2 起），所以「不同队 = 敌人」的判断各处照旧。
// 夺芯：规则在 MechCoreDirector。
// 开局把灯光切到地图对应的时间天气。命令行：-MechTeam=3（每队台数）、-MechAutoBattle（全 AI 自动打一整场，定时截图后退出）。
// 直接启动（地址里没有 ?Match、命令行没有 -Mech 开头的参数）进主菜单：不开对战，摆一台机体当背景。
// 11-01 起主菜单只有「开房」：进房间图 L_Street?Room?listen（房间流程见 MechRoom.h），房主开始后 ServerTravel 到选好的图
// （地图?Match?Team=N?Mode=…?listen / ?Core / ?Defense），结算后 ServerTravel 回房间。直接用 ?Match 地址进对战图（测试、开始游戏_街道.bat）照旧在图里开老式房间。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RobotSlicePlayerController.h"
#include "MechTypes.h"
#include "MechRules.h"
#include "MechRoom.h"
#include "MechScore.h"
#include "MechUI.h"
#include "MechGameMode.generated.h"

class ACameraActor;
class AMechCharacter;
class AMechGameState;

UCLASS()
class AMechPlayerController : public ARobotSlicePlayerController
{
	GENERATED_BODY()

public:
	AMechPlayerController();

	virtual void PlayerTick(float DeltaTime) override;
	/** 玩家断开时引擎先删他的机体、再通知掉线：在删之前把还活着的机体交给 AI（AI 补位） */
	virtual void PawnLeavingGame() override;

	/** 菜单（主菜单、暂停、设置、结算）：打开时鼠标交给界面，关上回到游戏；单机暂停时整个游戏停住 */
	void OpenMenu(EMechMenuPage Page);
	void CloseMenu();
	bool IsMenuOpen() const { return Menu.IsValid(); }

	// 菜单按钮
	/** 11-01 主菜单「开房」（原来的「开始对战 / 联机对战」合成这一个）：开房间图 L_Street?Room?listen，地图和模式进房间后选（MechRoom.h） */
	void MenuOpenRoom();
	void MenuJoin(const FString& Address);
	/** 11-01 结算页「回房间」：房主带所有人回房间图（非房主点不了，等房主或自己退出）；从房间开的这一场才有 */
	void MenuBackToRoom();
	/** 这一场是从房间开的（结算页显示「回房间」，不是「再来一局」） */
	bool IsRoomMatch() const;
	/** 11-02 暂停菜单的友伤开关：只有房主、只在训练场 */
	bool CanToggleFriendlyFire() const;
	bool IsFriendlyFire() const;
	void MenuSetFriendlyFire(bool bOn);
	/** 测试（-MechRoomTest）：注入真实按键 */
	void TestKey(const FKey& InKey, bool bDown) { Key(InKey, bDown); }
	/** 服务器上：训练场友伤开着时的队伍号（第一次用时分） */
	int32 TrainingTeam = -1;
	void MenuOpenViewer();
	void MenuBackToMain();
	void MenuResume();
	void MenuRestart();
	void MenuQuit();
	void MenuOpenRange();
	/** 配装页「应用」：存进设置，发给服务器在原地换装（只在测试场有效） */
	void MenuApplyLoadout(const FMechLoadout& Loadout);
	bool IsRangeMode() const;
	/** 只有主机（或单机）能开下一局 */
	bool CanRestartMatch() const;
	/** 自己在哪队（机体被击毁、换回合时也记得；个人死斗是自己那台的队伍号） */
	int32 GetMenuTeam() const { return MenuTeam; }
	/** 自己机体的显示名（个人死斗的名次、结算按名字找自己） */
	const FString& GetMenuName() const { return MenuName; }

	/** 阵亡后的视角：停在阵亡点（鼠标绕着看）或观战队友（左键换下一个，右键回阵亡点）；返回正在观战的队友，没有 = 在阵亡点 */
	AMechCharacter* GetSpectated() const { return Spectated.Get(); }
	bool IsDeathCam() const { return bDeathCam; }

	/** 服务器给的队伍（加入时分到人少的那队，整场不变；-1 未分） */
	int32 AssignedTeam = -1;
	/** 第几个加入的真人（显示成「玩家 N」） */
	int32 PlayerNumber = 0;

	// 房间 / 选配界面的按钮（发给服务器）
	void LobbySetTeam(int32 Team);
	/** 房主点开始（用 GameState::Room 里已经同步好的选择；房间图里 = 换到选好的地图） */
	void LobbyStart();
	/** 11-01 房主改房间的选择（地图、模式、规则、每队台数、AI 强度、禁用开关）：马上写进 GameState::Room，房间里所有人都看得到；
	 *  非房主发了服务器也不认（以前台数 / AI / 禁用只存在房主本地界面里，按开始才发，别人一直看不到） */
	void LobbySetRoom(const FMechRoomSettings& InRoom);
	/** 服务器上：这个真人的房间令牌（UMechRoomSubsystem::Token，进图后 ServerRoomHello 发来；空 = 还没收到） */
	FString RoomToken;
	void DraftBan(int32 Category, int32 Index);
	void DraftPick(const FMechLoadout& Loadout, bool bLock);
	/** 把设置里的玩家名称和涂装发给服务器（进图后发一次，房间里改了再发） */
	void SendProfile();
	/** 服务器上：这个真人的名字（空 = 「玩家 N」）和涂装 */
	FString ProfileName;
	FMechPaint ProfilePaint;
	/** 选配界面的摄影棚（只在本机，给武器拍图） */
	class AMechItemStudio* GetStudio();
	/** 开场运镜（MechMatchIntro）：本机按任意键跳过后告诉服务器，所有真人都跳过了就提前进倒计时。实现在 MechMatchIntro.cpp */
	UFUNCTION(Server, Reliable)
	void ServerSkipIntro();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Esc 开暂停、整场结束弹结算、主菜单模式开主菜单 */
	void MenuTick();
	UFUNCTION(Server, Reliable)
	void ServerApplyLoadout(const FMechLoadout& Loadout);
	UFUNCTION(Server, Reliable)
	void ServerSetProfile(const FString& Name, const FMechPaint& Paint);
	UFUNCTION(Server, Reliable)
	void ServerLobbyTeam(int32 Team);
	UFUNCTION(Server, Reliable)
	void ServerLobbyStart();
	UFUNCTION(Server, Reliable)
	void ServerLobbyRoom(const FMechRoomSettings& InRoom);
	/** 11-01 进图后报上本进程的房间令牌（换图重连后服务器按它恢复队伍、编号；在 ServerSetProfile 之前发，可靠 RPC 按顺序到） */
	UFUNCTION(Server, Reliable)
	void ServerRoomHello(const FString& Token);
	UFUNCTION(Server, Reliable)
	void ServerDraftBan(uint8 Category, uint8 Index);
	UFUNCTION(Server, Reliable)
	void ServerDraftPick(const FMechLoadout& Loadout, bool bLock);
	bool bSentProfile = false;
	bool bSentRoomHello = false;
	/** 房间 / 选配整屏界面（MechDraftUI）：阶段是房间或选配时打开，之后收起 */
	void DraftScreenTick();
	/** -MechLobbyTest：房间里截图后按 3v3、老练开始；选配每一轮截图，轮到自己禁第一件能禁的、定下霰弹枪 + 投掷匕首（Saved/Screenshots/Lobby） */
	void LobbyTestTick(float DeltaTime);
	float LobbyTestClock = 0.f;
	float LobbyTestTurnClock = 0.f;
	int32 LobbyTestTurn = -2;
	int32 LobbyTestStep = 0;
	void OpenDraftScreen();
	void CloseDraftScreen();
	TSharedPtr<class SMechDraftScreen> DraftScreen;
	TSharedPtr<SWidget> DraftContainer;
	UPROPERTY()
	TObjectPtr<class AMechItemStudio> Studio;
	/** 进测试场后把设置里存的配装发给服务器一次 */
	bool bSentRangeLoadout = false;
	/** -MechRangeTest：测试场自动走一遍（全景截图 → 对最近的木桩连射 → 配装页截图 → 换两套配装记参数 → 退出） */
	void RangeTestTick(float DeltaTime);
	float RangeTestClock = 0.f;
	/** -MechWeaponTest：测试场里把 6 把枪、7 把刀轮流换上打木桩（每把截图、记读数），打完退出 */
	void WeaponTestTick(float DeltaTime);
	float WeaponTestClock = 0.f;
	/** -MechWeaponTestBlades 拍刀痕近景时临时用的摄像机 */
	UPROPERTY()
	TObjectPtr<class ACameraActor> WeaponTestCam;
	/** -MechShoulderTest（测试场）：钩爪拉木桩、钩爪抓地面拉自己、捕获网罩木桩，截图 + 日志，跑完退出 */
	void ShoulderTestTick(float DeltaTime);
	float ShoulderTestClock = 0.f;
	/** -MechTacticalTest（测试场）：肩部的紧急脱离、弹射升空、震荡波震木桩、地雷炸木桩、地雷被打掉，截图 + 日志 [tacticaltest]，跑完退出；
	 *  作为联机客户端跑时改测脱离 / 弹射的纠正次数 [tacticalnet]。实现在 MechTacticalTest.cpp */
	void TacticalTestTick(float DeltaTime);
	float TacticalTestClock = 0.f;
	/** -MechFrameTraitTest（测试场）：九台框架的数值、新纪现场抢修、维克托抗干扰（EMP / 失衡）、相位怕 EMP，日志 [frametest]，跑完退出。实现在 MechFrameTraitTest.cpp */
	void FrameTraitTestTick(float DeltaTime);
	float FrameTraitTestClock = 0.f;
	/** -MechStressTest（10-31 保卫战压力测试，测试场模式进 L_Defense）：按档位刷敌方 AI 机甲和无人机群，量帧时间，日志 [stress]，跑完退出。实现在 MechStressTest.cpp */
	void StressTestTick(float DeltaTime);
	float StressTestClock = 0.f;
	/** -MechDefenseTest（10-31 保卫战第一版，"/Game/Maps/L_Defense?Defense"）：波次流程、子弹 / 刀打杂兵、杂兵打人和指挥所，截图 + 日志 [defensetest]，跑完退出。实现在 MechDefenseTest.cpp */
	void DefenseTestTick(float DeltaTime);
	float DefenseTestClock = 0.f;
	/** 3D 渲染精度：设置里选「自动」时按窗口高度让 3D 内部约 810p（50%–100%），选了具体比例就按选的；
	 *  显卡开销几乎全在 3D 分辨率上（2880×1620 下 100% 约 54 帧、50% 约 85 帧），引擎自己的「按显示分辨率」实测没降下来 */
	void UpdateRenderScale(float DeltaTime);
	float RenderScaleClock = 0.f;
	/** 菜单截图（地址参数 ?MenuShots，主菜单模式下）：依次切到每一页截一张到 Saved/Screenshots/Menu，截完退出 */
	void MenuShotsTick(float DeltaTime);
	float MenuShotClock = 0.f;
	TSharedPtr<SMechMenu> Menu;
	TSharedPtr<SWidget> MenuContainer;
	int32 MenuTeam = 0;
	FString MenuName;
	EMechRoundPhase MenuSeenPhase = EMechRoundPhase::Waiting;

	void DeathCamTick(float DeltaTime);
	UPROPERTY()
	TObjectPtr<ACameraActor> DeathCam;
	TWeakObjectPtr<AMechCharacter> Spectated;
	FRotator DeathOrbit = FRotator(-20.f, 0.f, 0.f);
	FVector DeathPoint = FVector::ZeroVector;
	bool bDeathCam = false;

	/** 本机第一次放某个特效时要现编译、建渲染管线，开打后第一次开枪 / 爆炸会卡一下；进图后先在地下各放一次 */
	void WarmUpEffects();
	FTimerHandle WarmUpTimer;

	/** 帧时间统计（-MechPerf）：每 2 秒记一次帧率、最慢一帧、游戏线程 / 渲染线程 / GPU 耗时，超过 50 毫秒的帧单独记；退出时汇总 */
	void PerfTick(float DeltaTime);
	void PerfSummary();
	bool bPerf = false;
	float PerfClock = 0.f;
	float PerfAge = 0.f;
	TArray<float> PerfWindow;
	double PerfGT = 0.0, PerfRT = 0.0, PerfGPU = 0.0;
	TArray<float> PerfAll;
	int32 PerfHitches = 0;

	/** 联机抖动统计（-MechJitterLog）：每帧最后（所有东西摆完以后）记下每台机体网格、手里的刀枪、左手刀枪、背后挂件、爪子、喷焰的世界位置和网格朝向，
	 *  按「这一帧偏离前后两帧连线多少」算抖动（匀速、匀加速时接近 0）；另记「挂件脱离网格」（道具帧末在网格局部的位置和摆完时差多少、
	 *  爪子收着时离爪座多远）。每 5 秒每台打两行 [jitter] */
	void JitterTick(UWorld* World, ELevelTick TickType, float DeltaTime);
	struct FJitterTrack
	{
		/** 跟踪的部件：0 网格、1 刀、2 枪、3 背后挂件、4 左手枪、5 左手刀、6 爪子、7 喷焰 */
		static constexpr int32 NumParts = 8;
		FVector P[2][NumParts];
		bool bShown[2][NumParts] = {};
		float Yaw[2] = {0.f, 0.f};
		float Dt = 0.f;
		int32 N = 0;
		/** 0–7 各部件抖动（厘米，部件三帧都显示着才记）；8 网格朝向（度）；交战端枪时：9 枪管偏离瞄准点（度）、10 瞄准方向偏离机体朝向（度） */
		TArray<float> Dev[11];
		/** 挂件脱离网格（厘米）：0–4 五个道具、5 爪子 */
		TArray<float> Bind[6];
		/** 这几秒里骨骼没刷新（没摆道具）的帧数 */
		int32 Stale = 0;
		float OffsetSum = 0.f;
		float OffsetMax = 0.f;
		float SpeedSum = 0.f;
		TWeakObjectPtr<USceneComponent> Flame;
		/** 10-31 联机细查（第三行）：逐帧二阶差分换算成加速度——0 网格位置（米/秒²）、1 网格朝向（度/秒²）、2 动画用的速度每帧变了多少（米/秒）、
		 *  3 瞄准方向（度/秒²）、4 本机镜头位置（米/秒²，只记自己的机体）；回跳 = 网格这一帧水平方向往回走（和上一帧反向，两帧都动了 0.5 厘米以上） */
		TArray<float> Acc[5];
		FVector AnimV = FVector::ZeroVector;
		FVector Aim[2] = {FVector::ZeroVector, FVector::ZeroVector};
		FVector Cam[2] = {FVector::ZeroVector, FVector::ZeroVector};
		int32 Reverse = 0;
		int32 Corrections0 = -1;
	};
	TMap<TWeakObjectPtr<class AMechCharacter>, FJitterTrack> JitterTracks;
	float JitterClock = 0.f;
	FDelegateHandle JitterHandle;

	/** 测试驾驶员（-MechInputTest 单机 / -MechNetTest 联机）：通过输入系统注入真实按键（鼠标左右键、E、R、WASD、Shift、Ctrl、空格），
	 *  逐项检查开火、换弹、冲刺、快速闪避、跳跃喷射、导弹、挥刀有没有生效，之后继续自动打，最后输出结果并退出 */
	void PilotTick(float DeltaTime);
	void Key(const FKey& InKey, bool bDown);
	void Check(const FString& Name, bool bOk);

	bool bPilot = false;
	bool bNetTest = false;
	float FightClock = -1.f;
	float TotalClock = 0.f;
	int32 Step = 0;
	float StepClock = 0.f;
	float Peak = 0.f;
	float StartZ = 0.f;
	bool bSawThrust = false;
	bool bStaggeredDuring = false;
	int32 Retries = 0;
	/** 快速闪避检查挑的方向键（前方有墙就往侧面 / 后面闪） */
	FKey DodgeKey = EKeys::Invalid;
	float DodgeClock = 0.f;
	/** 每回合换新机体后从头再查一遍按键：-MechRounds=N 查满 N 个回合才退出 */
	int32 RoundsWanted = 1;
	int32 RoundsChecked = 0;
	int32 CheckRound = -1;
	TArray<FString> Passed;
	TArray<FString> Failed;
	/** -MechPilotShots：每项检查通过时截一张、0.35 秒后再截一张（看动作和 HUD），存到 Saved/Screenshots/Pilot */
	bool bPilotShots = false;
	TArray<TPair<float, FString>> PendingShots;
	int32 ShotCount = 0;
	TSet<FKey> Down;
};

UCLASS()
class AMechGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMechGameMode();

	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 机体由回合流程统一生成，不走默认的出生 */
	virtual void RestartPlayer(AController* NewPlayer) override {}

	/** AI 补位：真人加入时分到人少的那队，回合进行中就直接接管本队一台 AI 机体；掉线时他的机体交给 AI 接着打 */
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	/** 掉线的人的机体还活着就交给 AI 接着打，返回是否交了 */
	bool HandOverLeaving(AController* Exiting);

	/** 命中统计（机体结算伤害后调用）；bBreak = 这一下把对方打进了失衡（11-01 积分制：打出失衡加分、算助攻） */
	void RecordHit(AMechCharacter* Attacker, AMechCharacter* Victim, float Damage, EMechHitKind Kind, bool bBreak = false);

	/** 11-01 积分制（MechScore.h；实现在 MechScore.cpp，玩法代码走 MechScore::Note* 调过来）：控制命中、救人、保卫战打杂兵的伤害和击毁 */
	void RecordControl(AMechCharacter* By, AMechCharacter* Victim);
	void RecordRescue(AMechCharacter* By, bool bCore);
	void RecordGruntDamage(AMechCharacter* By, float Damage);
	void RecordDefenseKill(AMechCharacter* By, uint8 Kind, const FString& VictimName, bool bNearCore);

	/** 结算页「再来一局」：比分、统计清零，马上开第一回合（从房间开的场不用这个，结算后回房间：ReturnToRoom） */
	void RestartMatchNow();
	/** 测试场换装（还没做好的部件那一类保持原样） */
	void ApplyRangeLoadout(APlayerController* PC, const FMechLoadout& Loadout);

	/** 夺芯：阵亡的机体重生（新生成一台接替，交给原来的控制器，旧的销毁），返回新机体 */
	AMechCharacter* RespawnMech(AMechCharacter* Old, AController* Controller);
	/** 同上，指定重生位置（死斗按离敌人远近挑的点） */
	AMechCharacter* RespawnMechAt(AMechCharacter* Old, AController* Controller, const FTransform& At);
	/** 夺芯：先到目标分 / 加时得分 / 到时分高 → 整场结束（-1 平局） */
	void EndCoreMatch(int32 Winner);
	/** 夺芯：这台交芯得分了（个人统计，结算场景的铭牌 / MVP 用） */
	void RecordCoreScore(AMechCharacter* Carrier);

	// 房间 / 选配（控制器的 RPC 转过来；不合规则的忽略）
	void SetProfile(class AMechPlayerController* PC, const FString& Name, const FMechPaint& Paint);
	void LobbySetTeam(class AMechPlayerController* PC, int32 Team);
	void LobbyStart(class AMechPlayerController* PC);
	void LobbySetRoom(class AMechPlayerController* PC, const FMechRoomSettings& InRoom);
	/** 11-01 收到房间令牌：开房期间按令牌恢复换图前的队伍和「玩家 N」编号（新来的记下来） */
	void RoomHello(class AMechPlayerController* PC, const FString& Token);
	/** 11-02 训练场开关友伤（房主在暂停菜单里点；房间页的开关开局时带过来）：每台玩家机体换队伍号，马上生效 */
	void SetTrainingFriendlyFire(bool bOn);
	/** 11-01 回房间（结算后房主点「回房间」/ 结算表到时 / 保卫战打完）：记下每人的队伍，ServerTravel 回房间图 */
	void ReturnToRoom();
	void DraftBan(class AMechPlayerController* PC, int32 Category, int32 Index);
	void DraftPick(class AMechPlayerController* PC, const FMechLoadout& Loadout, bool bLock);
	/** 房主：单机的玩家、开房的那台机器上的玩家 */
	bool IsHost(const APlayerController* PC) const;

protected:
	/** 开局的时间（小时） */
	UPROPERTY(EditAnywhere, Category="Lighting")
	float StartHour = 21.5f;

	/** 开局冻结时间，测试时画面不随时间变化 */
	UPROPERTY(EditAnywhere, Category="Lighting")
	bool bFreezeTime = true;

	/** 开局天气（ELightingWeatherType 的名字），命令行 -MechWeather= 可覆盖 */
	UPROPERTY(EditAnywhere, Category="Lighting")
	FString StartWeather = TEXT("Fog");

	UPROPERTY(EditAnywhere, Category="Match")
	int32 TeamSize = 2;

	UPROPERTY(EditAnywhere, Category="Match")
	int32 RoundsToWin = 2;

	UPROPERTY(EditAnywhere, Category="Match")
	float CountdownTime = 3.f;

	UPROPERTY(EditAnywhere, Category="Match")
	float RoundTimeLimit = 150.f;

	UPROPERTY(EditAnywhere, Category="Match")
	float RoundOverTime = 4.f;

	/** 结算页停留时间，到点自动开下一局（自动对战时 8 秒后退出） */
	UPROPERTY(EditAnywhere, Category="Match")
	float MatchOverTime = 30.f;

private:
	/** 灯光总控初始化完会用地图里存的天气覆盖，所以开局稍等一下再设一次 */
	void ApplyLighting();

	// ---------------------------------------------------------------- 对战规则 / 死斗（MechRules.h；实现在 MechGameMode.cpp「死斗」一节）
	/** 这一场的规则（开局按设置 + 地址参数 + 命令行，房间里房主可改）；ApplyRules 把它落到局数、限时和 GameState 上 */
	FMechRules Rules;
	void ApplyRules();
	bool IsDM() const { return !bCoreMode && !bRangeMode && Rules.IsDeathmatch(); }
	bool IsSoloDM() const { return IsDM() && Rules.bSolo; }
	/** 团队淘汰每回合的限时（开局时记下 RoundTimeLimit 的默认值，死斗改过后切回团队淘汰用） */
	float ElimTimeLimit = 150.f;
	/** AI 的名字：蓝1 / 红2，个人死斗叫「机体N」 */
	FString SlotName(int32 Team, int32 Index) const;
	/** 个人死斗的队伍号（各不相同，从 2 起；0 / 1 留给两队） */
	static int32 SoloTeam(int32 Group, int32 Index) { return 2 + Index * 2 + Group; }
	/** 死斗：有人阵亡（Killer 已经过滤成「算分的击毁者」，环境 / 自己 / 队友 = 空） */
	void DMOnDied(AMechCharacter* Victim, AMechCharacter* Killer);
	/** 死斗开打中每帧：到点重生、记走过的地面点、掉出地图算阵亡、到时结束 */
	void DMTick(float DeltaSeconds);
	/** 死斗结束（-1 平局）；个人死斗 WinnerTeam 是胜者的队伍号 */
	void EndDMMatch(int32 WinnerTeam, const FString& Winner);
	/** 重生点候选（脚下的位置）：地图里带「DMSpawn」标签的点；没有就用两队出生区 + 场地里自动找的空地 */
	void BuildDMSpawns();
	/** 开打后每 2 秒把机体站过的地面点记成重生点候选（没摆 DMSpawn 的地图用） */
	void LearnDMSpawns();
	/** 这个脚下位置能不能放一台机体（地面平、胶囊不卡墙） */
	bool IsClearSpot(const FVector& Feet) const;
	/** 给这台机体挑重生点：离活着的敌人越远越好、敌人看不见的更好、离别的机体至少 5 米，前三名里随机一个 */
	FTransform PickDMSpawn(const AMechCharacter* For) const;
	/** 个人死斗开局：每台一个尽量互相远的点 */
	TArray<FTransform> PickSpreadStarts(int32 Count) const;
	FTransform SpawnAtFeet(const FVector& Feet) const;
	struct FDMDown
	{
		TWeakObjectPtr<AMechCharacter> Mech;
		TWeakObjectPtr<AController> Controller;
		float RespawnAt = 0.f;
	};
	TArray<FDMDown> DMDowned;
	/** 最近用过的重生点（脚下位置、时间）：6 秒内别让两台在同一处冒出来 */
	TArray<TPair<FVector, float>> DMRecentSpawns;
	/** 统计（结束时打日志）：重生保护挡下的命中、自己打中人提前结束保护的次数 */
	int32 DMGuardBlocks = 0;
	int32 DMGuardBreaks = 0;
	TArray<FVector> DMSpawns;
	TArray<FVector> DMLearned;
	bool bDMSpawnsFromMap = false;
	float DMLearnClock = 0.f;

	void ComputeArena();
	/** 开一整场：房间（选队）→ 禁用 / 选配 → 第一回合；自动对战只走选配（全 AI），按键测试类直接开打 */
	void StartMatchFlow();
	void EnterLobby();
	void RefreshLobby();
	void StartDraft();
	void BeginDraftTurn();
	void NextDraftTurn();
	void DraftTick();
	/** 真人按分好的队伍排好（和回合生成时的出生位一致） */
	void GatherHumans(TArray<APlayerController*> (&Humans)[2]);
	class AMechPlayerController* FindPlayer(int32 PlayerId) const;
	bool bUseLobby = false;

	// ---------------------------------------------------------------- 11-01 房间（MechRoom.h；实现在 MechGameMode.cpp「房间」一节）
	/** 房间图（?Room）：不生成机体、不开回合，阶段一直是 Lobby */
	bool bRoomMode = false;
	void SetupRoom();
	/** 这一场是从房间开的（StartPlay 读到 UMechRoomSubsystem::bMatchPending）：跳过房间，结算后回房间 */
	bool bFromRoom = false;
	/** 从房间开的场：等房间里的人都进图（或到截止时间）再开禁用 / 选配 */
	void RoomWaitTick();
	/** 保卫战打完（守住 / 被摧毁）几秒后回房间（-1 = 还没打完） */
	void RoomDefenseTick();
	float DefenseEndAt = -1.f;
	/** 已经调过 ServerTravel（开始 / 回房间），别再调第二次 */
	bool bRoomTravelling = false;
	/** 对战图里的老式房间进过一次了（「再来一局」再进时禁用开关按上一局的，不再读设置） */
	bool bLegacyLobbyInit = false;

	// ---------------------------------------------------------------- 11-02 阵容（用户：「双方队伍都是完全高度自定义的，你要是想 1v5 也是可以的」）
	/** 这一场的一个位置：蓝 / 红队（个人战时只是选配分组）第 Index 台，谁来开（真人的 PlayerId，-1 = 人机），人机多强（-1 = 按 GameState::AILevel） */
	struct FMechRosterSlot
	{
		int32 Team = 0;
		int32 Index = 0;
		int32 PlayerId = -1;
		int32 AILevel = -1;
	};
	TArray<FMechRosterSlot> Roster;
	bool bRosterBuilt = false;
	/** 阵容按房间的人机名单来（从房间开的场、老式房间开始后）；否则（自动对战、按键测试、直接开打）每队 TeamSize 台、空位 AI 补 */
	bool bCustomRoster = false;
	/** 两队实际各几台（TeamSize = 两队里多的那个，出生点排开用这个） */
	int32 TeamCount[2] = {2, 2};
	/** 开局排一次阵容（StartDraft / 第一回合） */
	void BuildRoster();
	/** 每回合开始前：走了的真人交给人机，新来的真人顶掉本队一个人机（没有就多排一个）；还没排过就排 */
	void RefreshRoster();
	/** 台数抄到 TeamCount、TeamSize 和 GameState */
	void PublishRosterCounts();
	const FMechRosterSlot* RosterAt(int32 Team, int32 Index) const;
	/** 总台数（真人 + 人机）超过上限时去掉多的人机（后加的先去） */
	void TrimRoomAIs(FMechRoomSettings& S);
	/** 算现在生效的挑战倍数写进 GameState（房间里按名单、对局里按阵容） */
	void PublishChallenge();

	// ---------------------------------------------------------------- 11-02 训练场（房间里选「训练场」= 地址 ?Range，能联机、有木桩、不计分）
	/** 这个真人在训练场的队伍号：友伤关 = 0（都是自己人）；友伤开 = 每人一个（2 起，和个人死斗同一个约定，「不同队 = 敌人」的判断各处照旧） */
	int32 RangeTeamFor(APlayerController* PC);
	int32 NextTrainingTeam = 2;
	/** 训练场阵亡：几秒后原地方附近重生，配装、涂装不变 */
	void RangeOnDied(AMechCharacter* Mech, AActor* Killer);
	void RangeRespawnTick();
	struct FRangeDown
	{
		TWeakObjectPtr<APlayerController> PC;
		FMechLoadout Loadout;
		FMechPaint Paint;
		float RespawnAt = 0.f;
	};
	TArray<FRangeDown> RangeDowned;
	int32 RangeSpawnCounter = 0;
	/** 房间的选择 → GameState（Room 整份 + 老字段 Rules / TeamSize / AILevel / bDraftBans，HUD 等照旧读老字段） */
	void ApplyRoomToState(const FMechRoomSettings& S);
	/** 插件舱：开打后定时刷（死斗场地中央一个，夺芯铸造台两侧轮流，同时最多两个）；-MechNoPlugins 关掉 */
	void PluginTick();
	void ClearPluginPods();
	bool bUsePlugins = true;
	float NextPodTime = -1.f;
	int32 PodSide = 0;
	TArray<TWeakObjectPtr<class AMechPluginPod>> Pods;
	bool bUseDraft = false;
	FRandomStream DraftRand;
	float DraftTurnStart = 0.f;
	float DraftAIDelay = 2.f;
	int32 DraftShotTurn = -1;
	/** 一队第 Index 台的出生位置：出生区基准地面同一层、沿左右排开 7 米，落到高处 / 卡墙就就近换位（见实现） */
	FTransform TeamSpawn(int32 Team, int32 Index) const;
	/** 出生区基准地面（从基准点上方 3 米往下找，避开头顶的高架 / 楼顶） */
	FVector SpawnBaseGround(int32 Team) const;
	void BeginRound();
	void StartFight();
	void EndRound(int32 Winner);
	void OnMechDied(AMechCharacter* Victim, AActor* Killer);
	int32 AliveCount(int32 Team) const;
	float TeamAPFraction(int32 Team) const;
	AMechGameState* GetMechGameState() const;
	void AutoBattleTick(float DeltaSeconds);
	/** 统计抄到 GameState 给各端的计分板和结算页 */
	void PublishStats();
	float PublishClock = 0.f;

	/** 测试场：玩家机体 + 木桩（带「Dummy」标签的 TargetPoint 上各一台），不开回合 */
	void SpawnRange();
	void SpawnRangeMech(APlayerController* PC);
	void RangeTick(float DeltaSeconds);
	bool bRangeMode = false;
	/** 保卫战（10-31，地址 ?Defense，按测试场的方式进图：玩家都在蓝队、没有木桩，波次由 AMechDefenseDirector 管） */
	bool bDefenseMode = false;
	friend class AMechDefenseDirector;
	FTransform RangeStart;

	/** 真人接管本队一台还活着的 AI 机体（没有就等下一回合） */
	bool TakeOverAI(class AMechPlayerController* PC);
	/** 机体交给新的 AI 控制器 */
	void HandToAI(AMechCharacter* Mech);
	FString HumanName(const APlayerController* PC);
	int32 JoinCounter = 0;

	/** 主菜单背景：街道中间摆一台机体，镜头在它右前方慢慢来回转，机体落在菜单面板右边 */
	void SpawnShowcase();
	void ShowcaseTick(float DeltaSeconds);
	bool bMenuMode = false;
	float ShowcaseClock = 0.f;

	UPROPERTY()
	TObjectPtr<AMechCharacter> Showcase;

	UPROPERTY()
	TObjectPtr<ACameraActor> ShowcaseCamera;

	/** 背景机体的暖色主光（镜头一侧上方）和青色轮廓光（机体背后），不投影、只照机体附近 */
	UPROPERTY()
	TObjectPtr<class UPointLightComponent> ShowcaseKey;

	UPROPERTY()
	TObjectPtr<class UPointLightComponent> ShowcaseRim;

	/** 模拟玩家测试（-MechPlayerTest）：玩家那台由脚本操作——镜头对准最近的敌人、点射、锁上就放导弹、贴近就砍，
	 *  走的是玩家的输入和锁定路径（镜头中心锁定、HUD、镜头震动），定时截图，打完第一回合退出 */
	void PlayerTestTick(float DeltaSeconds);

	/** 联机测试：等够这么多名玩家进来再开第一回合（-MechWaitPlayers=2） */
	int32 WaitPlayers = 1;
	bool bRoundsStarted = false;

	/** 特效挑选（-MechVfxGallery）：在机体前方依次放候选特效，每个截两张（刚爆开、展开后），放完退出 */
	void VfxGalleryTick(float DeltaSeconds);
	bool bVfxGallery = false;
	float GalleryClock = 0.f;
	int32 GalleryIndex = -1;
	UPROPERTY()
	TObjectPtr<class UNiagaraComponent> GalleryFx;
	void LogStats() const;

	/** 夺芯模式（地址参数 ?Core，?Core=Factory 换装配厂布局）：在测试场上空现搭灰盒地图，规则由 CoreDirector 管 */
	bool bCoreMode = false;
	void SetupCoreMode();

	UPROPERTY()
	TObjectPtr<class AMechCoreArena> CoreArena;

	UPROPERTY()
	TObjectPtr<class AMechCoreDirector> CoreDirector;

	UPROPERTY()
	TArray<TObjectPtr<AMechCharacter>> Mechs;

	FVector SpawnBase[2];
	float SpawnYaw[2] = {0.f, 180.f};
	FVector ArenaCenter = FVector::ZeroVector;
	FVector2D ArenaExtent = FVector2D(6000.f, 6000.f);

	struct FMechStats
	{
		int32 Team = 0;
		float Dealt = 0.f;
		float Taken = 0.f;
		int32 Kills = 0;
		int32 Deaths = 0;
		int32 Hits[3] = {0, 0, 0};
		/** 夺芯交芯次数 */
		int32 Cores = 0;
		/** 最后一次在场上看到这台时的配装、涂装、是不是真人（结算场景按这个重新生成展示机体，见 PublishStats） */
		FMechLoadout Loadout;
		FMechPaint Paint;
		bool bHuman = false;
		/** 11-01 积分制（MechScore.h）：对敌人的伤害（保卫战含杂兵）、对首领的伤害（两项的分发布时按伤害折算）、
		 *  其余各分项的分（下标 EMechScorePart）和计数 */
		float ScoreDamage = 0.f;
		float BossDamage = 0.f;
		int32 Points[static_cast<int32>(EMechScorePart::Num)] = {};
		int32 Assists = 0;
		int32 Breaks = 0;
		int32 Controls = 0;
		int32 Rescues = 0;
		int32 CarrierKills = 0;
		int32 Grunts = 0;
		int32 Mortars = 0;
	};
	TMap<FString, FMechStats> Stats;

	/** 11-01 积分制：助攻判定。受害者名字 → 攻击者名字 → 最近一段贡献（AssistWindow 秒内的伤害累计、最近一次伤害 / 控制的时间）。
	 *  按名字记（死斗 / 夺芯重生是新生成的机体，名字不变）；击毁时结算完就清掉这个受害者的 */
	struct FMechAssistMark
	{
		float Damage = 0.f;
		float DamageTime = -1000.f;
		float ControlTime = -1000.f;
	};
	TMap<FString, TMap<FString, FMechAssistMark>> AssistMarks;
	void NoteContribution(AMechCharacter* By, AMechCharacter* Victim, float Damage, bool bControl);
	/** 击毁时：给击毁者以外、窗口内有贡献的人记助攻 */
	void AwardAssists(const FString& VictimName, int32 VictimTeam, const FString& KillerName);
	/** 11-02 落水损毁（MechMapRules.h）：这一帧落水的机体算阵亡；击毁算给助攻窗口内最后打中它的敌人 */
	void WaterTick();
	AMechCharacter* LastHitterOf(const AMechCharacter* Victim) const;
	/** -MechWaterTest、-MechFlightTest（MechWaterTest.cpp） */
	void WaterTestTick(float DeltaSeconds);
	void FlightTestTick(float DeltaSeconds);
	/** 一行统计 → 结算行的积分字段（Score、Parts、计数） */
	void FillScore(FMechResultRow& Row, const FMechStats& S) const;
	/** 比赛结束时打日志：每人积分分项、MVP 是谁、为什么（LogStats 末尾调） */
	void LogScores() const;

	bool bAutoBattle = false;
	bool bPlayerTest = false;
	float PlayerTestClock = 0.f;
	float AutoShotTimer = 0.f;
	int32 AutoShotIndex = 0;
	float PendingEventShot = -1.f;
	float MatchClock = 0.f;
	float FightStartTime = 0.f;

	float Hour = 21.5f;
	FString WeatherName;
	FTimerHandle LightingTimer;
	FTimerHandle StartTimer;
};

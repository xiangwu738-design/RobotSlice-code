// 玩家之间的「通讯」（11-01）：标点的操作（MechPing.h）和信息区每个玩家收到的那几行（MechFeed.h）。
// 挂在 AMechPlayerController 上（构造函数里建，复制）：
//   本机：每帧看标点键（默认中键）——点一下 / 双击 / 按住 0.22 秒出轮盘；准星射线 + 屏幕上离准星最近的敌人 / 物品决定标什么，
//         发 ServerPing；轮盘开着时鼠标移动交给轮盘（AMechCharacter::DoLook 里先问 MechComms::WheelLook），松开放下、在正中松开取消。
//   服务器：记下这个玩家现在在哪队（机体被击毁、换回合时也记得），标点的复制和信息区按它挑人。
//   ClientFeed：服务器发来的一条信息，本机存起来（最多 12 行，HUD 只画还没过期的前几行）。
// 测试 -MechPingTest（MechPingTest.cpp）也挂在这里跑。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MechFeed.h"
#include "MechPing.h"
#include "MechComms.generated.h"

class AMechCharacter;
class APlayerController;

/** 本机信息区的一行 */
struct FMechFeedLine
{
	FMechFeedEvent E;
	/** 本机收到的时间（世界时间） */
	float Time = 0.f;
	/** HUD 画的时候平滑用：现在画在第几行（小数，新行进来时旧行慢慢往下挪） */
	float Slot = -1.f;
};

UCLASS(ClassGroup = (Mech))
class UMechCommsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMechCommsComponent();

	/** 轮盘光标按「镜头本来会转几度」算：甩过 WheelDead 度才算选中，最远 WheelRange 度（HUD 上对应轮盘半径） */
	static constexpr float WheelDead = 2.5f;
	static constexpr float WheelRange = 12.f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---------------------------------------------------------------- 标点（本机）
	/** 轮盘开着（HUD 画、镜头不转） */
	bool IsWheelOpen() const { return bWheel; }
	/** 轮盘上选中的格（-1 = 在正中，松开就取消） */
	int32 GetWheelSlot() const { return WheelSlot; }
	/** 轮盘里的虚拟光标（像素，按 1080p；向右 X+、向上 Y+） */
	FVector2D GetWheelCursor() const { return WheelCursor; }
	/** 轮盘打开了多久（秒，真实时间） */
	float GetWheelAge() const;
	/** 轮盘打开那一刻准星指着的位置（松开时标在这里） */
	FVector GetWheelPoint() const { return WheelPoint; }
	/** 轮盘开着时的鼠标移动（AMechCharacter::DoLook 转过来，原始值：Yaw 向右为正，Pitch 乘了俯仰缩放后向上为正） */
	void AddWheelInput(float Yaw, float Pitch);

	// ---------------------------------------------------------------- 信息区（本机）
	TArray<FMechFeedLine>& GetLines() { return Lines; }
	/** 本机加一行（服务器发来的，或标点到达时本机自己记的）。同一个人 0.8 秒内的两次标点合成一行（双击） */
	void AddLine(const FMechFeedEvent& E);
	/** 标点到达这一端（AMechPing::BeginPlay 调）：队友标的响一声、记一行；自己标的按键时已经响过 */
	void OnPingArrived(class AMechPing* Ping);
	/** 标点提示音（本机，/Game/Audio/HUD 的 S_hud_thrust_ready 升高音调 = 短促的两声「嘀」；敌情类低一点） */
	void PlayPingSound(bool bThreat);

	UFUNCTION(Client, Reliable)
	void ClientFeed(const FMechFeedEvent& E);

	// ---------------------------------------------------------------- 服务器
	/** 这个玩家的队伍（机体在就按机体，不在就是最后一次的；都没有用分到的队）、显示名 */
	int32 GetServerTeam() const;
	FString GetServerName() const;

	UFUNCTION(Server, Reliable)
	void ServerPing(EMechPing Kind, FVector_NetQuantize At, AActor* Target, int32 CoreId, bool bReplaceRecent);

	UFUNCTION(Server, Reliable)
	void ServerRemovePing(class AMechPing* Ping);

	/** 测试统计 */
	int32 DebugArrived = 0;
	int32 DebugFeedReceived = 0;

private:
	APlayerController* GetPC() const;
	/** 本机：标点键的状态机 */
	void InputTick();
	/** 点一下 / 双击：按准星决定标什么 */
	void Tap(bool bDouble);
	/** 准星指着什么：返回标点种类（Count = 什么都没指着，比如天空），Out 出位置 / 目标 / 核心编号；
	 *  先看有没有对着自己的标点（OutRemove，撤掉） */
	EMechPing Resolve(FVector& OutAt, AActor*& OutTarget, int32& OutCoreId, class AMechPing*& OutRemove) const;
	void OpenWheel();
	void CloseWheel(bool bPlace);
	/** 本机现在能不能标（菜单、房间、选配、模型查看都不行） */
	bool CanPingNow() const;

	bool bHeld = false;
	double PressTime = 0.0;
	double LastTapTime = -100.0;
	bool bLastTapPlaced = false;
	bool bWheel = false;
	double WheelTime = 0.0;
	FVector2D WheelCursor = FVector2D::ZeroVector;
	int32 WheelSlot = -1;
	FVector WheelPoint = FVector::ZeroVector;
	bool bWheelPointValid = false;

	TArray<FMechFeedLine> Lines;

	UPROPERTY()
	TObjectPtr<class USoundBase> PingSound;
	bool bSoundLoaded = false;

	/** 服务器：最近 2 秒收到的标点（防刷） */
	TArray<double> RecentPings;

	/** 最后一次知道的队伍 / 名字（各端：机体在的时候每帧记一次） */
	int32 LastTeam = -1;
	FString LastName;

	// ---------------------------------------------------------------- 测试（-MechPingTest，MechPingTest.cpp）
	void TestTick(float DeltaTime);
	float TestClock = -1.f;
	int32 TestPassed = 0;
	int32 TestFailed = 0;
	int32 TestStep = 0;
	float TestStepClock = 0.f;
	TWeakObjectPtr<AActor> TestEnemy;
	TWeakObjectPtr<AMechCharacter> TestMate;
	FVector TestSpot = FVector::ZeroVector;
	int32 TestFeedBefore = 0;
	bool bTestClient = false;
	void TestCheck(const FString& Name, bool bOk, const FString& Detail);
	void TestShot(const FString& Name);
	void TestKey(bool bDown);
	/** 点几下（排队，一帧一个按下 / 松开） */
	void TestTap(int32 Times);
	TArray<uint8> TestKeyQueue;

public:
	/** 这一端能看到的、自己标的标点有几个（测试用） */
	int32 CountMyPings() const;
};

namespace MechComms
{
	UMechCommsComponent* Get(const APlayerController* PC);
	/** 服务器 / 本机：这个玩家在哪队 */
	int32 TeamOf(const APlayerController* PC);
	/** AMechCharacter::DoLook 开头调：轮盘开着就把鼠标移动交给轮盘、返回 true（镜头不转） */
	bool WheelLook(const AMechCharacter* Mech, float Yaw, float Pitch);
	/** 鼠标输入 → 「镜头本来会转几度」的倍数：灵敏度 × 引擎旧版输入缩放（工程开着 bEnableLegacyInputScales）；俯仰乘完以后向上为正 */
	void LookScale(const APlayerController* PC, float& OutYaw, float& OutPitch);
}

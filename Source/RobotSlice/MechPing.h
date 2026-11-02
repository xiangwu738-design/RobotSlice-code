// 标点（11-01，用户：「加一个中键标点，可以参照 apex 系统」）。操作在 UMechCommsComponent（MechComms.h），画在 MechHudPing.cpp。
//   点一下中键：按准星看指着什么——敌方机体 / 杂兵 = 「敌人」（红，跟着那个目标走 6 秒）；插件舱、核心 = 物品；别的 = 地点「去这里」
//   双击：「危险 / 有敌人在这」；按住：轮盘（8 项，鼠标往方向甩选、松开放下，轮盘开着时镜头不转）
//   对着自己的标点再点一下撤掉；每人同时最多 3 个（多了最旧的消失），每种有寿命。
// 每个标点是一个复制的小 Actor：服务器生成，只复制给同队（IsNetRelevantFor 按队伍挑；个人死斗只给自己），
// 各端 HUD 再按同样的规矩过滤一次（房主那台服务器上什么都有）。到达时（BeginPlay）响一声、往信息区记一行（队友标的）。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechPing.generated.h"

class APlayerController;
class APlayerState;

UENUM()
enum class EMechPing : uint8
{
	Go,            // 去这里（点一下的默认）
	Enemy,         // 敌人（点在敌方机体 / 杂兵上，跟着目标走）
	Item,          // 物品（插件舱、核心，跟着走）
	Danger,        // 危险 / 有敌人在这（双击）
	Attack,        // 进攻这里
	Defend,        // 防守这里
	Regroup,       // 集合
	Watch,         // 注意这里
	EnemyWasHere,  // 有敌人来过
	Help,          // 需要支援（标在自己身上，跟着自己）
	Count UMETA(Hidden)
};

namespace MechPing
{
	/** 每人同时最多几个标点 */
	constexpr int32 MaxPerPlayer = 3;
	/** 轮盘：8 格，从正上方顺时针 */
	constexpr int32 WheelSlots = 8;
	EMechPing WheelItem(int32 Slot);
	/** 短词（信息区、屏幕上标点旁边） */
	const TCHAR* Word(EMechPing Kind);
	/** 轮盘上选中时正中写的 */
	const TCHAR* Label(EMechPing Kind);
	/** 寿命（秒） */
	float Life(EMechPing Kind);
	/** 地点类：钉在地上的（画成图钉：图标在上，细杆连到地面那一点） */
	bool IsPlace(EMechPing Kind);
	/** 敌情类（红色、提示音低一点） */
	bool IsThreat(EMechPing Kind);
	/** 颜色（当前 HUD 主题里的颜色，换主题跟着变） */
	FLinearColor Color(EMechPing Kind);
}

UCLASS()
class AMechPing : public AActor
{
	GENERATED_BODY()

public:
	AMechPing();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;

	/** 服务器：玩家放一个标点。bReplaceRecent（双击）= 把这个人 0.8 秒内刚放的那个换掉。超过每人上限时最旧的消失 */
	static AMechPing* Place(APlayerController* By, EMechPing Kind, const FVector& At, AActor* Target, int32 CoreId, bool bReplaceRecent);
	/** 服务器：不是玩家放的（测试里替 AI 队友标；以后 AI 自己标点也走这里）。Owner 是那台机体 */
	static AMechPing* PlaceFor(AActor* OwnerActor, APlayerState* PS, const FString& Name, int32 InTeam, EMechPing Kind, const FVector& At, AActor* Target, int32 CoreId);

	UPROPERTY(Replicated)
	EMechPing Kind = EMechPing::Go;

	/** 跟着走的目标（敌方机体、杂兵、插件舱、需要支援时的自己）；没有 = 钉在 Anchor */
	UPROPERTY(Replicated)
	TObjectPtr<AActor> Target;

	/** 夺芯核心的编号（标的是核心时；核心不是 Actor，按编号去 CoreDirector 找） */
	UPROPERTY(Replicated)
	int32 CoreId = 0;

	UPROPERTY(Replicated)
	int32 Team = 0;

	/** 个人死斗：只有自己看得到 */
	UPROPERTY(Replicated)
	bool bSolo = false;

	/** 谁标的（玩家的 PlayerState；替 AI 标的为空） */
	UPROPERTY(Replicated)
	TObjectPtr<APlayerState> OwnerPS;

	UPROPERTY(Replicated)
	FString ByName;

	/** 放下的服务器时间、寿命 */
	UPROPERTY(Replicated)
	float BornAt = 0.f;

	UPROPERTY(Replicated)
	float Life = 20.f;

	/** 钉的位置（有目标时服务器每帧更新成目标的位置；客户端看不到目标时用它） */
	UPROPERTY(Replicated)
	FVector_NetQuantize Anchor = FVector::ZeroVector;

	/** 现在该画在哪：跟着目标（机体是画面上的胸口）/ 核心的当前位置 / 钉的那一点 */
	FVector GetPoint() const;
	/** 放下多久了（秒，按服务器时间） */
	float GetAge() const;
	/** 这一端的这个玩家（队伍 ViewerTeam）看不看得到 */
	bool IsVisibleTo(const APlayerController* PC, int32 ViewerTeam) const;
	/** 是这个玩家自己标的 */
	bool IsMine(const APlayerController* PC) const;
	/** 标的是核心 / 插件舱（信息区写「核心」还是「插件」） */
	bool IsCore() const { return CoreId > 0; }

private:
	float ServerNow() const;
};

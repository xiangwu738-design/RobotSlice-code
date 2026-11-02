// 地图系统（所有地图、所有模式）：开局拍一次场地的正交俯视图做底图，HUD 左上角画随视角旋转的圆形小地图，按住地图键画北向上的大地图。
//
// 底图怎么来（UMechMapCapture）：运行时拍，不离线烘——夺芯灰盒是开局现搭在空中的，离线烘不到；以后加地图也不用补资源。
//   - 场地框：夺芯 / 山谷用 CoreArena 的范围（MapMin / MapMax），死斗图用 GameState 里复制的场地中心、AI 活动半径和两队出生点。
//   - 正交 SceneCapture 从正上方往下拍：一块 512×512 一块地拍（大图一次拍整片的话，离镜头远的树会按剔除距离消失、显存要另开一套
//     2048 的场景缓冲），每块拍底色（SCS_BaseColor，跟昼夜灯光无关，夜图也看得清）和深度（换成高度），读回内存拼起来。
//     一帧只拍一块（每块一次 GPU 同步，十几毫秒），开局选配 / 倒计时那几秒就拍完；关掉阴影、Lumen、雾、粒子、半透明、草、机体。
//   - 拼好后在后台线程处理：底色去饱和压暗 + 按高度算的山体明暗 + 陡坎 / 墙边描青线 + 等高线，界外压暗，做成一张 BGRA 贴图
//     （山谷图 2048×1536 约 12 MB），渲染目标和拍摄组件随即释放。之后每帧只是画这张贴图，不再捕获。
//   - 拍不了（-nullrhi、没有场地框）时退回抽象画法：夺芯画 CoreArena 的方块形状，死斗只画网格。
// 坐标约定：贴图向右 = 世界 +X，向下 = 世界 +Y（和编辑器顶视图一样）。大地图北向上 = 世界 −Y 朝上。

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "UObject/Object.h"
#include "MechMinimap.generated.h"

class UTexture2D;
class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
struct FSlateBrush;

/** 地图上的一个图标（小地图、大地图共用；位置是世界坐标，各自换算到屏幕） */
struct FMechMapIcon
{
	enum EType : uint8
	{
		Self,
		Ally,
		AllyDown,     // 阵亡的队友（夺芯里等救）
		Enemy,        // 正被看见的敌人
		EnemyGhost,   // 最后看到的位置（淡出）
		Dummy,        // 测试场木桩
		Core,         // 地上 / 空中的核心
		CoreCarried,  // 队友带着的核心（画在带芯者头上）
		CorePulse,    // 对面带着的核心：每 3 秒闪一次的位置
		Station,      // 回收站（开着）
		StationOff,   // 回收站（关着）
		StationNext,  // 马上要切换到的回收站（倒计时闪）
		Foundry,      // 中央铸造台
		Pod,          // 插件舱
		Spawn,        // 死斗两队出生区
		Ping          // 标点（11-01，自己和队友的；PingKind 是哪种）
	};
	EType Type = Self;
	FVector2D At = FVector2D::ZeroVector;
	/** 朝向（度，世界 yaw）；机体箭头用 */
	float Yaw = 0.f;
	FLinearColor Color = FLinearColor::White;
	float Alpha = 1.f;
	/** 读条（核心被捡、掉地回收倒计时）0–1，<0 不画；读条颜色（被捡时是捡的那一队） */
	float Progress = -1.f;
	FLinearColor ArcColor = FLinearColor::White;
	FName Plugin;
	/** 大地图上写在图标旁边的名字（队友） */
	FString Label;
	/** 小地图上超出范围时贴着边缘显示（目标、队友）；敌人超出范围就不画 */
	bool bEdge = false;
	/** 标点种类（EMechPing） */
	uint8 PingKind = 0;
};

/** 场地俯视图：开局拍一次、处理成贴图（见文件头） */
UCLASS()
class UMechMapCapture : public UObject
{
	GENERATED_BODY()

public:
	/** 开始拍：Owner 用来挂临时的拍摄组件；Play 是活动范围（世界 XY，厘米），RefZ 是参考地面高度，VoidZ 以下算虚空（灰盒悬空时看到的下层地面）。
	 *  机体、核心、插件舱这些会动的东西每块拍之前现找、不拍 */
	void Start(AActor* Owner, const FBox2D& InPlay, float InRefZ, float InVoidZ);
	/** 不拍底图（-nullrhi 等）：只记场地框，HUD 退回抽象画法 */
	void StartWithoutImage(const FBox2D& InPlay);
	/** 每帧调一次：拍一块 / 等后台处理 / 建贴图 */
	void Step();

	bool HasFrame() const { return bHasFrame; }
	bool IsReady() const { return bReady; }
	bool IsBusy() const { return bHasFrame && !bReady && !bFailed && bWantImage; }
	const FBox2D& GetPlay() const { return Play; }
	/** 整张底图覆盖的世界范围（比活动范围大一圈） */
	const FBox2D& GetCovered() const { return Covered; }
	FVector2f ToUV(const FVector2D& World) const;
	const TSharedPtr<FSlateBrush>& GetBrush() const { return Brush; }

private:
	void CaptureTile(int32 Index);
	void Finish();
	void Cleanup();

	FBox2D Play = FBox2D(ForceInit);
	FBox2D Covered = FBox2D(ForceInit);
	bool bHasFrame = false;
	bool bWantImage = false;
	bool bReady = false;
	bool bFailed = false;

	// 拍摄参数
	static constexpr int32 Tile = 512;
	int32 TilesX = 1;
	int32 TilesY = 1;
	float CmPerPx = 10.f;
	float CamZ = 0.f;
	float RefZ = 0.f;
	float VoidZ = -1e9f;
	int32 NextTile = 0;
	double StartSeconds = 0.0;
	double CaptureMs = 0.0;
	/** 拼好的整张底色 / 高度（厘米；虚空记成很小的数） */
	TArray<FColor> Color;
	TArray<float> Height;
	/** 高度校验：拍完每块在块中心打一条射线比对（坐标方向错了会差很多） */
	double CheckErrSum = 0.0;
	int32 CheckCount = 0;

	TFuture<TArray<FColor>> Processing;
	bool bProcessing = false;

	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> ColorRT;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> DepthRT;

	UPROPERTY()
	TObjectPtr<UTexture2D> Texture;

	TSharedPtr<FSlateBrush> Brush;
};

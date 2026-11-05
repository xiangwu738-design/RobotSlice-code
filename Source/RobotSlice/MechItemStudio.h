// 配装照片的摄影棚（只在本机）：放在场地下方很远的地方，物件和灯只在灯光通道 1（不吃地图的太阳），
// 用场景捕获把配装的每一件拍成贴图给 Slate 显示。10-31 用户：「配装界面显示配装照片要详细一点，不是只有枪械有照片」→ 五类都拍：
//  射击 / 近战：武器本体（最长的轴横放）；肩部：浮空无人机（机身 + 底下两个推进口，和游戏里 UMechShoulderComponent 拼的一样），
//  另拍一张「挂在机体左肩外」；框架：整台机体（展示姿势竖大拇指，网格按 MechCatalog::FrameMesh，以后换各家外壳照片自动跟着变）；
//  推进器：机体背面，两个喷口点火（喷焰长短示意地面 / 均衡 / 飞行）。
// 每件三种照片：
//  小图（卡片用，每件一张）、详细视角（正在看的那件，侧面 / 特写 / 挂在肩边……，见 SetupFor）：拍一次缓存在渲染目标里；
//  转台（正在看的那件）：10-31 改成实时拍（ShootLive）——界面显示转台期间每帧按当前角度拍一张到同一张渲染目标。
//    以前是预拍 48 个角度、拖动时在相邻两张之间半透明叠化，用户：「配装界面旋转很奇怪」（叠出重影、刚进页面只有几个角度一跳一跳、
//    拖动方向和手相反、机体绕包围盒中心转所以脚下在画面里左右晃）。实时拍由一个「分身」棚（LiveStudio，同一个类，放在旁边）负责，
//    它有自己的一套物件，不和排队拍小图的这个棚抢机体（机体换网格要等几帧姿势）。
// 拍摄排队、每帧最多拍几张（分帧），正在看的那件优先；贴图第一次拍时多半还是低精度的，2.5 秒后自动补拍一次。
// 11-01 用户：「继续优化选配界面」，照片是最廉价的地方，重拍了一遍：
//  噪点：捕获输出 FinalColorLDR 到 RGBA8_SRGB 贴图时，色调映射最后加 ±1/255 的量化抖动，再被写贴图的 sRGB 编码放大（线性 1/255 → 13/255），
//    黑底上就是满屏 0–13 的灰点（截图量到的就是这个范围）。改成输出 FinalToneCurveHDR 到 RGBA16F：引擎对这种输出不加抖动，也没有 8 位色阶。
//  发黑 + 过曝：武器大多是金属，棚里没有环境反射（关了天光和反射），金属只反射点光源的一个小亮点，其余全黑。
//    加了环境立方体贴图（后期里的 AmbientCubemap，不依赖地图的天光）和大面积的主光 / 补光 / 轮廓光，亮度也降下来。
//  背景：物件后面一块跟着镜头的深色幕布，自己一盏灯打出中间亮、四周暗的渐变；只在灯光通道 2，不影响物件。
//  抗锯齿：一次性捕获没有历史帧，TSR 等于没抗锯齿还带抖动。关掉时间抗锯齿（退回 FXAA），贴图按屏幕像素的 1.5 倍拍。
// 镜头和灯装在一个「机位」组件上绕物件转（物件不动，机体的姿势不用重算）；固定曝光、不吃天光（坑：场景捕获会吃地图天光）。
// 加一类：SubjectFor 里给这一类选拍法（武器 / 无人机 / 机体），ViewsFor 里加它的详细视角。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechItemStudio.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class USceneCaptureComponent2D;
class UPointLightComponent;
class UTextureRenderTarget2D;
class UMechMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UCLASS()
class AMechItemStudio : public AActor
{
	GENERATED_BODY()

public:
	AMechItemStudio();

	static const FVector StudioLocation;
	/** 每件最多几张详细视角（不算转台） */
	static constexpr int32 MaxViews = 3;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** 这一件有没有照片：枪、刀、肩部看模型在不在，框架、推进器拍机体 */
	static bool HasPhoto(int32 Category, int32 Index);
	/** 枪、刀、肩部（无人机机身）的静态网格；框架、推进器返回空 */
	static UStaticMesh* MeshFor(int32 Category, int32 Index);
	/** 这一类有几张详细视角、叫什么（界面上缩略条的名字） */
	static int32 NumViews(int32 Category);
	static FText ViewName(int32 Category, int32 View);

	/** 小图（第一次要时排队去拍，拍好前是黑的）；没有照片返回空 */
	UTextureRenderTarget2D* GetThumb(int32 Category, int32 Index);

	/** 正在看的那件：开始拍它的详细视角（换一件就重新拍，同一件不重拍） */
	void SetPreview(int32 Category, int32 Index);
	/** 转台（实时拍）：这一帧按 Angle（度，0 = 小图的角度，变大 = 镜头往左绕 = 物件往右转）拍一张，Pixels 是照片在屏幕上的像素大小
	 *  （渲染目标跟着它，清晰度和屏幕一致）。返回拍好的贴图；机体换网格后姿势还没算好的那几帧返回空（界面先放小图）。
	 *  界面显示转台期间每帧调（只有配装页 / 选配页打开时才会调到） */
	UTextureRenderTarget2D* ShootLive(int32 Category, int32 Index, float Angle, FIntPoint Pixels);
	/** 第 View 张详细视角（0 起）；还没拍好返回空 */
	UTextureRenderTarget2D* GetView(int32 View) const;
	/** 正在看的那件拍完没有（0–1：小图、详细视角（含贴图流送后的补拍）、实时转台出过第一张；测试截图前等它到 1） */
	float GetPreviewProgress() const;
	/** 界面关掉时：放掉实时转台和详细视角的贴图（小图留着），分身的机体停掉动画 */
	void ReleasePreview();
	/** 兼容旧接口：转台现在那张（还没有就是小图） */
	UTextureRenderTarget2D* GetPreview() const;
	/** 实时拍：最近一次捕获调用在游戏线程上用了多少毫秒（测试日志用；GPU 的另算） */
	float GetLiveCallMs() const;

private:
	/** 拍法：武器（静态网格）、无人机（肩部）、机体（框架 / 推进器 / 无人机挂在肩边） */
	enum class ESubject : uint8
	{
		None,
		Weapon,
		Drone,
		Mech
	};
	/** 一次拍摄的要求：拍什么、机位 */
	struct FSetup
	{
		ESubject Subject = ESubject::None;
		/** 机体：竖大拇指、喷焰长度（0 = 不点火）、肩边挂无人机（肩部编号，-1 = 不挂） */
		bool bThumbs = false;
		float Flame = 0.f;
		int32 DroneOnMech = -1;
		/** 机位：方位角（0 = 镜头在物件 -X 一侧往 +X 看）、仰角（镜头在上方往下看为正）、放大倍数 */
		float Az = 0.f;
		float El = 0.f;
		float Zoom = 1.f;
		/** 对准哪一点（物件坐标）；bFocus = false 时对准包围盒中心 */
		bool bFocus = false;
		FVector Focus = FVector::ZeroVector;
		/** 只按机体上身 + 肩边无人机取景（挂在肩边） */
		bool bUpperBody = false;
		/** 转台：镜头距离取一圈里最远的，转起来不忽大忽小；机体绕脚下中心（胶囊轴线）转，不绕包围盒中心 */
		bool bSpin = false;
	};
	/** 这一件这一张怎么拍（Slot：-1 小图、0 转台（实时拍，方位再加界面的角度）、100+ 详细视角） */
	FSetup SetupFor(int32 Category, int32 Index, int32 Slot) const;
	static ESubject SubjectFor(int32 Category);

	/** 排队的一张 */
	struct FJob
	{
		int32 Key = 0;
		int32 Slot = -1;
		int32 Priority = 0;
		/** 不早于这个时间拍（补拍用，FPlatformTime::Seconds） */
		double NotBefore = 0.0;
	};
	void Enqueue(int32 Key, int32 Slot, int32 Priority, double NotBefore = 0.0);
	UTextureRenderTarget2D* TargetFor(int32 Key, int32 Slot) const;
	/** 摆好物件（机体要等姿势算好：返回 false = 这一帧还不能拍） */
	bool Arrange(int32 Category, int32 Index, const FSetup& S);
	/** 实时转台的取景：同一件只定一次中心、距离只增不减（机体待机呼吸时骨头包围盒微动，镜头不跟着一伸一缩） */
	struct FLiveFrame
	{
		bool bSet = false;
		FVector Focus = FVector::ZeroVector;
		float Dist = 0.f;
	};
	/** 按机位拍到 Target（Live 不为空 = 实时转台，用它固定取景） */
	void Shoot(const FSetup& S, UTextureRenderTarget2D* Target, FLiveFrame* Live = nullptr);
	/** 分身棚里：按角度摆好、拍到 LiveTarget；机体姿势还没算好返回 false */
	bool LiveShot(int32 Category, int32 Index, float Angle, FIntPoint Pixels);
	/** 拍完一张：记下；贴图还没流送完的话排一次补拍 */
	void Done(int32 Key, int32 Slot, int32 Priority);

	// ---- 物件
	void ArrangeWeapon(UStaticMesh* Mesh);
	void ArrangeDrone(UStaticMesh* Mesh);
	/** 机体：换网格 / 换姿势后要等几帧（动画算一次姿势），返回是否已经可以拍 */
	bool ArrangeMech(int32 FrameIndex, const FSetup& S);
	/** 无人机的推进口和光点（摆在机身底面两侧，和 UMechShoulderComponent::CacheDroneMesh 一样） */
	void PlaceDronePads(UStaticMeshComponent* Body, int32 Set, float DroneScale);
	/** 机体骨头的包围盒（棚里坐标，按装甲加一圈）；bUpper 只取胸口往上 */
	FBox MechBox(bool bUpper) const;
	/** 玩家设置里的涂装刷到机体 / 无人机 / 手持武器（统一三路材质槽） */
	void ApplyPaint(class UMeshComponent* Target);
	/** 涂装变了就重新拍缓存照片，并刷新实时转台的动态材质。 */
	void RefreshPaint();
	bool bPaintCached = false;
	FColor CachedPaintPrimary, CachedPaintSecondary, CachedPaintAccent;

	UPROPERTY()
	TObjectPtr<USceneComponent> Rig;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Item;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> DronePads[4];

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> DroneGlows[4];

	UPROPERTY()
	TObjectPtr<USceneComponent> MechRoot;

	UPROPERTY()
	TObjectPtr<UMechMeshComponent> Mech;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Flames[2];

	UPROPERTY()
	TObjectPtr<UPointLightComponent> JetLight;

	/** 展台的顶灯（灯光通道 2，只照展台） */
	UPROPERTY()
	TObjectPtr<UPointLightComponent> StageLight;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Platform;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ShoulderDrone;

	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Key;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Fill;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Rim;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Top;

	/** 镜头旁边的大面积柔光：正对镜头的平面（枪身侧面）靠它反光，不然金属面全黑 */
	UPROPERTY()
	TObjectPtr<UPointLightComponent> Front;

	/** 背景幕布（跟着机位转，永远在物件后面）和打它的灯（灯光通道 2，不照物件） */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Backdrop;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> BackLight;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FlameMID;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GlowMID;

	UPROPERTY()
	TMap<int32, TObjectPtr<UTextureRenderTarget2D>> Thumbs;

	UPROPERTY()
	TArray<TObjectPtr<UTextureRenderTarget2D>> Views;

	/** 实时转台的分身棚（BeginPlay 时生成在旁边，EndPlay 时一起销毁）；分身自己这个指针为空 */
	UPROPERTY()
	TObjectPtr<AMechItemStudio> LiveStudio;

	/** 分身棚：实时转台拍到这张（大小跟着界面上照片的像素大小） */
	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> LiveTarget;

	/** 这个棚是分身（只拍实时转台，不排队、不再生分身） */
	bool bLiveTwin = false;
	/** 分身棚：现在拍的是哪件（Category*100+Index）、拍出过没有、取景 */
	int32 LiveKey = -1;
	bool bLiveValid = false;
	FLiveFrame LiveFrame;
	/** 分身棚：上一张的角度（武器 / 无人机角度不变就不重拍） */
	float LiveAngle = 0.f;
	/** 分身棚：上次强制贴图常驻的时间（实时拍一直在拍，30 秒的常驻到期前续上） */
	double LiveResidentAt = 0.0;
	/** 分身棚：捕获调用的耗时统计（游戏线程，毫秒），每 5 秒打一行 [livephoto] */
	float LiveLastMs = 0.f;
	double LiveStatMs = 0.0;
	float LiveStatMax = 0.f;
	int32 LiveStatCount = 0;
	double LiveStatSince = 0.0;

	TArray<FJob> Queue;
	/** 已拍好的（Key*1000 + Slot+1），换一件预览时清掉那件的转台 / 视角记录 */
	TSet<int32> Shot;
	/** 每件第一次要拍的时间（贴图从这时开始强制流送；2.5 秒内拍的都要补拍） */
	TMap<int32, double> FirstSeen;
	int32 PreviewKey = -1;

	// ---- 物件现在的状态
	ESubject Current = ESubject::None;
	const UStaticMesh* ItemMeshFor = nullptr;
	FBox SubjectBox = FBox(ForceInit);
	/** 武器 / 无人机在棚里的变换（模型坐标 → 棚里），算特写的对准点用 */
	FTransform ItemXf = FTransform::Identity;
	const UObject* MechAssetFor = nullptr;
	bool bMechThumbs = false;
	/** 机体还要等几帧姿势才算好 */
	int32 MechSettle = 0;
	double MechIdleSince = 0.0;
	/** 推进器喷口（spine_05 骨头坐标，从 v3 绑定姿势算，同 AMechCharacter） */
	FVector NozzleOnBone[2];
	FVector NozzleDirOnBone[2];
	bool bNozzles = false;
	float FlameBaseZ = -50.f;
	/** 机体缩进棚里的比例（机体约 2.5 米，缩到和武器差不多的尺度，四盏灯不用另配） */
	float MechStudioScale = 0.68f;
};

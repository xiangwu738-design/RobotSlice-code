// 机体骨骼网格：模板动画算完之后再用程序改几根骨头——右臂端枪指向瞄准方向、左臂挥刀、失衡时上身后仰、中弹时上身一抖。
// 武器不挂在骨头上，而是在骨骼定稿后按手和前臂的位置每帧摆放，所以不会落后一帧，也不用关心各骨头的轴向。

#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "MechMeshComponent.generated.h"

struct FReferenceSkeleton;

/** 握在手里的柄 / 握把（10-31 握持）：道具模型坐标（厘米、未缩放），截面是圆角方形的柱子。
 *  手的「标准握法」：柄心线穿过手心空腔（手掌和四指围成的那个洞）、手指包过去（见 UMechMeshComponent::GripPropInHand） */
struct FMechGripShape
{
	/** 手心空腔对准的点（在柄心线上） */
	FVector Center = FVector::ZeroVector;
	/** 柄轴：对着手的拇指那侧（刀 = 刀身方向，枪 = 握把往上，护木 = 枪口方向） */
	FVector Axis = FVector::ForwardVector;
	/** 手指从手心包过去的方向（手坐标「往指尖」对着它；刀 = 刃口，枪 = 握把正前方，护木 = 枪的右边）；会正交化到和 Axis 垂直 */
	FVector Wrap = -FVector::UpVector;
	/** 截面：沿 Wrap 的半宽、沿手心法线的半宽、圆角半径；沿 Axis 的范围（相对 Center） */
	float HalfD = 1.5f;
	float HalfN = 1.5f;
	float Corner = 1.5f;
	float AlongMin = -10.f;
	float AlongMax = 10.f;
	/** 柄轴在手坐标系里的方向（X 往拇指那侧 k、Y 手心 n、Z 往指尖 d）：刀照动作包里的握法斜一点，枪 / 护木贴着指根线 (1, 0, 0.225) */
	FVector HandDir = FVector(1.f, 0.f, 0.225f);
	/** 托着（长枪左手托护木）：拇指沿护木靠手腕那侧；否则拇指压到柄的另一侧 */
	bool bSupport = false;
	/** 食指伸直搭扳机 */
	bool bTrigger = false;
};

/** 一只手这一帧怎么握（UMechMeshComponent::Grip[0 左 / 1 右]，由 AMechCharacter::UpdatePose 设）：
 *  Prop = 道具按挂点挂在这只手的骨头上（刀），只弯手指；HandToProp = 手转到标准握法握住道具（枪：握把、双持左手枪），道具按手摆；
 *  Support = 左手两段 IK 到柄上（双手刀、长枪护木），手转过去再弯手指；Fist = 握拳（抓无人机）；None = 手指照动作 */
enum class EMechGripMode : uint8 { None, Prop, HandToProp, Support, Fist };
struct FMechHandGrip
{
	EMechGripMode Mode = EMechGripMode::None;
	int32 Prop = -1;
	FMechGripShape Shape;
	/** 0–1，0 手指照动作 */
	float Weight = 0.f;
	/** Support：手只把柄轴对准，绕柄的滚转留动作的（刀柄是圆的）；false = 完全照标准握法（护木） */
	bool bKeepRoll = false;
};

UCLASS()
class UMechMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	/** 手臂（0 左、1 右）前臂要指的世界方向和权重；权重 0 完全按动画 */
	FVector ArmDir[2] = {FVector::ForwardVector, FVector::ForwardVector};
	float ArmWeight[2] = {0.f, 0.f};

	/** 上身绕世界轴的额外旋转（轴 × 角度，度）：失衡后仰、中弹抖动 */
	FVector TorsoTwist = FVector::ZeroVector;

	/** 瞄准点 = 画面上的胸口（VisualPivot）+ 这个世界偏移。长枪在动作把两只手摆好以后，整个上身（胸椎往上）再转一下，
	 *  让枪管（右手指向左手）穿过瞄准点；权重 0 不转。不能只给方向：镜头在肩后偏右，枪管平行于镜头方向的话十几米内差好几度，
	 *  子弹看着从枪侧面斜着出去。按画面上的胸口算（不按胶囊）：别人的机体胶囊会随网络修正瞬移 */
	FVector AimOffset = FVector::ZeroVector;
	float GunAimWeight = 0.f;
	/** 长枪枪口在枪模型里的位置（厘米，原点在握把）：对准时让「枪口沿枪管」这条线穿过瞄准点（枪管比握把高十几厘米，只对两手连线近处会差一两度） */
	FVector GunMuzzleLocal = FVector::ZeroVector;
	/** 调试：这一帧长枪对准时上身一共转了多少度 */
	float DebugGunTurn = 0.f;
	/** 端在前臂上的枪：ArmDir 是从胸口看瞄准点的方向，按权重再修成从肩看（手离胸口几十厘米，近处会偏） */
	float ArmPointWeight[2] = {0.f, 0.f};
	/** 和 UMechCombatComponent::GetPoseAimPivot 对应的点，但按网格（画面）位置算 */
	FVector VisualPivot() const;

	/** 画面上机体的朝向（网格朝向去掉建模时的 -90 度）。别人的机体在本机是网格平滑追着胶囊（网络修正时胶囊瞬移），
	 *  按胶囊（Actor）朝向摆的东西会和网格错开、一跳一跳，所以摆姿势、挂件都按这个 */
	FQuat VisualActorQuat() const;

	/** 被摆放的道具：0 刀、1 枪、2 肩部挂件（10-30 起是浮空无人机，由 UMechShoulderComponent 摆，这里不摆，只登记着给第一人称隐藏、调试统计用）、
	 *  3 左手枪（双持手枪）、4 左手刀（双刀）；3、4 不用时隐藏 */
	static constexpr int32 NumProps = 5;
	TObjectPtr<USceneComponent> Props[NumProps] = {nullptr, nullptr, nullptr, nullptr, nullptr};
	/** 道具相对握持点的偏移（道具自己的坐标，厘米，未缩放） */
	FVector PropOffset[NumProps] = {FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector};

	/** 道具额外的滚转、俯仰（度）：换弹时枪身翻转、导弹巢发射时抬头 */
	float PropRoll[NumProps] = {0.f, 0.f, 0.f, 0.f, 0.f};
	float PropPitch[NumProps] = {0.f, 0.f, 0.f, 0.f, 0.f};

	/** 道具挂法：跟前臂（端枪，前臂方向就是枪管方向）/ 挂在骨头上（收起的武器、近战握在手里的刀）/ 背后导弹巢 /
	 *  双手端（长枪：握把在右手，枪管指向托着护木的左手） */
	enum class EMount : uint8 { Forearm, Bone, Back, TwoHand };
	EMount PropMount[NumProps] = {EMount::Forearm, EMount::Forearm, EMount::Back, EMount::Forearm, EMount::Forearm};
	int32 PropSide[NumProps] = {0, 1, 1, 0, 0};
	/** 跟前臂：Side 0 左手、1 右手 */
	void SetPropOnForearm(int32 Prop, int32 Side);
	/** 挂在骨头上：位置 = 这根骨头在绑定姿势下的位置 + Offset，朝向 Rot；都按绑定姿势下的机体坐标（前 X、右 Y、上 Z，厘米，未缩放），之后跟着骨头动 */
	void SetPropOnBone(int32 Prop, FName Bone, const FVector& Offset, const FRotator& Rot);
	/** 挂在骨头上，直接给骨头空间里的变换（照搬别的动作包里的武器插槽时用） */
	void SetPropOnBoneLocal(int32 Prop, FName Bone, const FTransform& Local);
	/** 双手端；bFreeze 时（换弹，左手离开护木）按冻结那一刻相对右手的位置拿着 */
	void SetPropTwoHand(int32 Prop, bool bFreeze);

	/** 左手握武器：把左手（上臂 + 前臂两段 IK）拉到道具 LeftGripProp（1 长枪、0 双手刀）上的这个点（模型坐标，厘米），权重 0 不拉。
	 *  bLeftGripAdaptive：只在动作里的左手本来就靠近握点时拉（双手刀有的招式会单手挥、左手松开，不硬拽回来） */
	FVector LeftGripLocal = FVector::ZeroVector;
	float LeftGripWeight = 0.f;
	int32 LeftGripProp = 1;
	bool bLeftGripAdaptive = false;

	/** 左手去抓的点（钩爪自拉时抓住浮空无人机，UMechShoulderComponent 设）：按画面上的机体坐标给（原点 VisualPivot、轴 VisualActorQuat：
	 *  前 X 右 Y 上 Z，厘米），骨骼定稿时按这一帧的网格位置换成世界点再做左臂两段 IK，手不会落后一帧；权重 0 不管 */
	FVector LeftReachLocal = FVector::ZeroVector;
	float LeftReachWeight = 0.f;

	/** 关掉程序姿势（死亡布娃娃时） */
	bool bProcedural = true;

	/** 主菜单展示：竖大拇指（0–1）。右前臂抬到胸前、拳头转到拇指那侧朝上、四指握拳、拇指（thumb_01–03；老模型是单独一根骨头 thumb_up_r）竖起来；左臂放下 */
	float ThumbsUp = 0.f;

	/** 10-31 展示姿势（结算场景 / 主菜单，AMechCharacter::bDisplayPose）：0–1，上身转正——两肩连线转到和机体朝向垂直、两肩放平。
	 *  端枪 / 持刀的上身动作只盖 spine_01 往上，片段里的骨盆是侧着的（端枪是侧身站），叠到正着站的腿上就成了拧着的上身（用户：「竖拇指时上身还带着端枪的扭身」） */
	float ShowSquare = 0.f;
	/** 展示姿势里长枪（双手端）低姿持枪：枪管往下压多少度、往左偏多少度（枪斜横在身前，两条手臂 IK 摆到位）；ShowGunDown 小于 0 不管 */
	float ShowGunDown = -1.f;
	float ShowGunYaw = 40.f;
	/** 调试（结算 / 阵营排排站的日志）：这一帧转正之前上身相对机体拧了多少（X 偏航、Y 侧歪，度）、低姿持枪实际用的枪管俯仰（Z，度；左手够不着时比 ShowGunDown 抬高） */
	FVector DebugShowTwist = FVector::ZeroVector;

	/** 10-31 握持：两只手各怎么握（0 左 1 右），见 FMechHandGrip。骨骼定稿时按它弯手指（v3 新模型手指每节挂手指骨） */
	FMechHandGrip Grip[2];
	/** 标准握法下道具相对手骨的变换（道具模型坐标 → 手骨局部）：柄心线穿过手心空腔、柄轴按 Shape.HandDir、Wrap 对着手指方向。
	 *  刀用它当挂点（SetPropOnBoneLocal），枪用它算握把偏移；骨架没缓存好时返回单位变换 */
	FTransform GripPropInHand(int32 Side, const FMechGripShape& Shape);
	/** 手转到标准握法握着道具时，道具原点相对手腕在道具坐标里的偏移（代替以前固定的 RifleGrip） */
	FVector GripPropOffset(int32 Side, const FMechGripShape& Shape);
	/** 调试（-MechViewerShots=hands 日志）：这只手上一次解出来的四指、拇指弯曲角（度）和拇指离目标多远 */
	FString DebugGripText(int32 Side) const;
	/** 调试（hands 日志，10-31 握持第二版）：这一帧握枪时手腕比动作里多折了多少度（握把逼出来的），X = 转手肘之前、Y = 转手肘之后；
	 *  Z = 手肘绕「肩—腕」连线转了多少度（长枪右手，见 ApplyHandGrips） */
	FVector DebugWrist[2] = {FVector::ZeroVector, FVector::ZeroVector};
	/** 调试（hands 日志）：双手刀这一帧动作里的左手离握点多远（厘米，-1 = 这一帧没按「靠近才拉」算） */
	float DebugLeftGripDist = -1.f;
	/** 调试：同一帧动作里的左手腕相对「握到柄上该在的位置」差多少（道具坐标，X 顺着柄 / 刀身方向，厘米） */
	FVector DebugLeftGripDelta = FVector::ZeroVector;
	/** 调试（hands 截图的「拇指侧」镜头）：这只手的手坐标轴（往拇指 K、手心 N、往指尖 D）现在在世界里的方向；手指骨没缓存好时返回 false */
	bool GetHandAxesWorld(int32 Side, FVector& OutK, FVector& OutN, FVector& OutD) const;
	/** 调试（10-31 肩甲擦头，MechShoulderClear.cpp）：这一帧左 / 右肩为了不擦头甩肘（X）、锁骨外转（Y）、歪头（Z）各多少度 */
	FVector DebugShoulderFix[2] = {FVector::ZeroVector, FVector::ZeroVector};
	/** 调试：左 / 右肩上的点插进头球最深多少厘米（X = 推之前，Y = 推之后；-MechNoShoulderClear 时也量，Y = X） */
	FVector2D DebugShoulderDepth[2] = {FVector2D::ZeroVector, FVector2D::ZeroVector};

	/** 右手枪口的世界位置（枪身坐标里的枪口偏移） */
	FVector GetPropPoint(int32 Prop, const FVector& LocalPoint) const;

	/** 10-31 阵营机体：AMechCharacter 按框架换了网格以后调。九台骨架一样，骨头索引多半不变，稳妥起见全部按新网格重取：
	 *  骨头 / 手指 / 减震缓存（下一次骨骼定稿时重取）、道具挂点的骨头、解好的握姿缓存、长枪双手端的相对位置 */
	void OnFrameMeshChanged();

	/** 调试（-MechJitterLog 的「挂件脱离网格」统计）：最近一次摆道具是第几帧（GFrameCounter），摆完时每个道具在网格局部坐标里的位置。
	 *  帧末再算一次道具在网格局部的位置，和这里差多少 = 道具这一帧有没有跟着网格走（网格在摆完以后又动了、或者这一帧骨骼没刷新） */
	uint64 PlacedFrame = 0;
	FVector PlacedLocal[NumProps];

protected:
	virtual void FinalizeBoneTransform() override;

private:
	void CacheBones();
	void RotateChain(TArray<FTransform>& CS, int32 Root, const FQuat& Rot, const FVector& Pivot) const;
	/** 10-31 肩甲擦头（MechShoulderClear.cpp）：按这台机体的简化碰撞体（头几个球、肩上一把采样点）查肩甲插进头没有，
	 *  插进去就甩肘（绕肩—腕连线转手臂，手和武器不动）、还不够再歪头。在所有手臂 IK、握持之后做 */
	void ClearShouldersFromHead(TArray<FTransform>& CS);
	const UObject* ShoulderFor = nullptr;
	int32 ShoulderProxy = INDEX_NONE;
	int32 HeadBone = INDEX_NONE;
	int32 NeckBone = INDEX_NONE;
	/** 头、左锁骨、左上臂、右锁骨、右上臂在绑定姿势下的组件空间变换 */
	FTransform ShoulderRef[5];
	/** 甩肘的方向（每边上一帧用的，肘正对 / 背对头时沿用，免得来回翻） */
	float ShoulderSwivelSign[2] = {0.f, 0.f};
	void PlaceProps();
	void ApplyThumbsUp(TArray<FTransform>& CS);
	/** 展示姿势：上身转正（ShowSquare）、长枪压低（ShowGunDown） */
	void ApplyShowSquare(TArray<FTransform>& CS);
	void ApplyShowGunDown(TArray<FTransform>& CS);
	int32 ThumbBone[2] = {INDEX_NONE, INDEX_NONE};
	int32 Middle1[2] = {INDEX_NONE, INDEX_NONE};

	int32 UpperArm[2] = {INDEX_NONE, INDEX_NONE};
	int32 LowerArm[2] = {INDEX_NONE, INDEX_NONE};
	int32 Hand[2] = {INDEX_NONE, INDEX_NONE};
	int32 Spine1 = INDEX_NONE;
	int32 Spine5 = INDEX_NONE;
	int32 Spine3 = INDEX_NONE;
	int32 Clavicle[2] = {INDEX_NONE, INDEX_NONE};
	FName PropBoneName[NumProps];
	FVector PropBoneOffset[NumProps];
	FRotator PropBoneRot[NumProps];
	int32 PropBone[NumProps] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
	FTransform PropBoneLocal[NumProps];
	bool bPropBoneDirty[NumProps] = {false, false, false, false, false};
	bool bTwoHandFreeze[NumProps] = {false, false, false, false, false};
	bool bTwoHandValid[NumProps] = {false, false, false, false, false};
	FTransform TwoHandRel[NumProps];
	/** 这一帧在 IK 之前按两只手算好的枪（组件空间）：摆枪用它，不用 IK 之后的左手，免得枪跟着左手转、左手又追着枪跑 */
	FTransform TwoHandGunCS;
	bool bTwoHandGunCS = false;
	void SolveLeftGrip(TArray<FTransform>& CS);
	/** 长枪按两只手摆在哪（组件空间）：握把在右手，枪管指向左手，再加换弹翻转和握把偏移 */
	FTransform TwoHandGunFrame(const TArray<FTransform>& CS) const;
	/** 左臂两段 IK 把手腕拉到 GripCS，返回实际用的权重（含「动作里的手离得远就不拉」） */
	float SolveLeftArm(TArray<FTransform>& CS, const FVector& GripCS);

	// ---- 10-31 握持：手指骨（每只手：拇指、食指、中指、无名指、小指 × 三节，四指还有掌骨）
	struct FHandRig
	{
		bool bValid = false;
		int32 Bone[5][3];
		int32 Meta[5] = {INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE};
		/** 手骨局部的单位方向：往指尖 D、手心 N、往拇指那侧 K（按绑定姿势里的骨头位置算，不管骨头轴向） */
		FVector D = FVector::ForwardVector, N = FVector::RightVector, K = FVector::UpVector;
		/** 绑定姿势：各节在手骨局部的变换、相对父骨头的局部变换；四个指根在手坐标系里的 (k, d) */
		FTransform InHand[5][3];
		FTransform RefLocal[5][3];
		FTransform MetaLocal[5];
		FVector2D Knuckle[4];
	};
	FHandRig HandRig[2];
	/** 解好的握姿（各节相对父骨头的局部旋转），按「手坐标里的柄」缓存：同一把武器只解一次 */
	struct FGripSolve
	{
		bool bValid = false;
		FTransform Key;
		FVector KeyParams = FVector::ZeroVector;
		uint8 KeyFlags = 0;
		FQuat Local[5][3];
		float Angle[5][3];
		float ThumbMiss = 0.f;
		/** 10-31 拇指第二版：拇指尖 / 拇指肚（第三节 60% 处）离目标多远（厘米）、第一节扭了多少度、解这一次花了多少毫秒（共用缓存命中时是 0） */
		float ThumbTipMiss = 0.f;
		float ThumbPadMiss = 0.f;
		float ThumbBeta = 0.f;
		float ThumbTwist = 0.f;
		float SolveMs = 0.f;
	};
	FGripSolve Solved[2][2];   // [手][0 握柄 1 握拳]
	/** 这一帧左手 Support 实际拉过去的权重（动作里左手离柄远就不拉，手指也就不握） */
	float LeftSupportWeight = 0.f;
	void CacheHandRig(const FReferenceSkeleton& Ref, const TArray<FTransform>& RefCS);
	/** 手坐标系里柄的样子（标准握法下）：中心 = 手心空腔、轴 = HandDir、包手方向 = 往指尖 */
	void CanonicalInHand(int32 Side, const FMechGripShape& Shape, FVector& OutCenter, FVector& OutAxis, FVector& OutWrap) const;
	const FGripSolve& SolveGrip(int32 Side, const FMechGripShape* Shape);
	void ApplyFingerPose(TArray<FTransform>& CS, int32 Side, const FGripSolve& S, float Weight, bool bThumb = true) const;
	void ApplyHandGrips(TArray<FTransform>& CS);
	/** 端在前臂上的枪（PlaceProps 的规则）在组件空间里的朝向 */
	FQuat ForearmPropRotCS(const TArray<FTransform>& CS, int32 Prop) const;

	/** 跨关节的减震（膝盖后、脚踝后）：缸筒骨头 damper_<knee|ankle>_a_<l|r> 挂上一段，活塞杆骨头 _b_ 挂下一段。
	 *  DirA / DirB 是绑定姿势里指向对方的方向（各自骨头局部）；每帧把两根骨头转到互相对准，关节一弯活塞杆就在缸筒里伸缩 */
	struct FDamperPair
	{
		int32 A = INDEX_NONE;
		int32 B = INDEX_NONE;
		FVector DirA = FVector::ForwardVector;
		FVector DirB = FVector::ForwardVector;
		/** 绑定姿势（腿伸直）时两销孔的距离；骨头局部哪根轴顺着减震（压缩 / 拉长时缩放这根轴，-1 不缩放） */
		float RestLen = 0.f;
		int32 AxisA = INDEX_NONE;
		int32 AxisB = INDEX_NONE;
	};
	TArray<FDamperPair> Dampers;
	void AimDampers(TArray<FTransform>& CS) const;
	void ResolvePropBone(int32 Prop);
	TArray<int32> Parents;
	const UObject* CachedFor = nullptr;
};

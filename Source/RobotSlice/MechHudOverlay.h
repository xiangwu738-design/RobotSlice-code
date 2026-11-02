// HUD 的 Slate 画层。AMechHUD 每帧在 DrawHUD 里把要画的东西（矩形、线、三角形、文字）记进 FMechHudFrame（坐标按视口像素），
// 这个控件在 Slate 绘制时照着画：Slate 的线带抗锯齿、文字按实际像素栅格化。以前直接画在 Canvas 上，
// Canvas 的线和三角形都不抗锯齿，圆弧、斜线、狙击镜框边上全是台阶。

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

/** 一帧要画的东西，按记录顺序画（后记的盖在上面） */
struct FMechHudFrame
{
	enum class EKind : uint8
	{
		Rect,
		Lines,
		Tris,
		Text,
		/** 带贴图的三角形（地图底图）：顶点在 ImgPos / ImgUV / ImgColor 里 */
		Image
	};

	struct FItem
	{
		EKind Kind = EKind::Rect;
		FLinearColor Color = FLinearColor::White;
		float Thick = 1.f;
		/** 线：Points 里的起点和个数；三角形：TriPos / TriColor 里的起点和顶点个数 */
		int32 First = 0;
		int32 Count = 0;
		/** 矩形：左上角和大小；文字：左上角 */
		FVector2f Pos = FVector2f::ZeroVector;
		FVector2f Size = FVector2f::ZeroVector;
		FString Text;
		FSlateFontInfo Font;
		/** 贴图三角形用的笔刷（资源是地图贴图）；共享指针，HUD 换图时这一帧记下的照样画得出来 */
		TSharedPtr<FSlateBrush> Brush;
	};

	TArray<FItem> Items;
	TArray<FVector2f> Points;
	TArray<FVector2f> TriPos;
	TArray<FLinearColor> TriColor;
	TArray<FVector2f> ImgPos;
	TArray<FVector2f> ImgUV;
	TArray<FLinearColor> ImgColor;
	/** 记录时视口的像素大小（画的时候按控件实际大小换算） */
	FVector2f CanvasSize = FVector2f(1920.f, 1080.f);
	/** 记录时的帧号：HUD 没画（菜单里、隐藏 HUD）时不会更新，过期的就不画 */
	uint64 FrameNumber = 0;

	void Reset()
	{
		Items.Reset();
		Points.Reset();
		TriPos.Reset();
		TriColor.Reset();
		ImgPos.Reset();
		ImgUV.Reset();
		ImgColor.Reset();
	}
};

class SMechHudOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMechHudOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FMechHudFrame>& InFrame);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D::ZeroVector; }

private:
	TSharedPtr<FMechHudFrame> Frame;
};

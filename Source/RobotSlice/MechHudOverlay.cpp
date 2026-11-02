#include "MechHudOverlay.h"

#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

void SMechHudOverlay::Construct(const FArguments& InArgs, const TSharedRef<FMechHudFrame>& InFrame)
{
	Frame = InFrame;
	SetVisibility(EVisibility::HitTestInvisible);  // 只画，不接鼠标
}

int32 SMechHudOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FMechHudFrame& F = *Frame;
	if (F.Items.IsEmpty() || GFrameCounter - F.FrameNumber > 1)
	{
		return LayerId;
	}
	// 记录时用的是视口像素坐标；控件的本地单位是像素除以 DPI 缩放，整体乘 S 换过去
	const FVector2f Local = FVector2f(AllottedGeometry.GetLocalSize());
	const float S = Local.X / FMath::Max(1.f, F.CanvasSize.X);
	const FPaintGeometry Whole = AllottedGeometry.ToPaintGeometry(F.CanvasSize, FSlateLayoutTransform(S));
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FSlateRenderTransform& ToRender = AllottedGeometry.GetAccumulatedRenderTransform();
	FSlateResourceHandle WhiteHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
	TArray<FSlateVertex> Verts;
	TArray<SlateIndex> Indices;
	// 每个元素单独一层：同一层里 Slate 会按类型合批，矩形、线、文字的先后就乱了
	int32 Layer = LayerId;
	for (const FMechHudFrame::FItem& It : F.Items)
	{
		++Layer;
		switch (It.Kind)
		{
		case FMechHudFrame::EKind::Rect:
			FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(It.Size * S, FSlateLayoutTransform(It.Pos * S)), White,
				ESlateDrawEffect::None, It.Color);
			break;
		case FMechHudFrame::EKind::Lines:
		{
			TArray<FVector2f> Pts(F.Points.GetData() + It.First, It.Count);
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, Whole, MoveTemp(Pts), ESlateDrawEffect::None, It.Color, true, It.Thick);
			break;
		}
		case FMechHudFrame::EKind::Tris:
		{
			Verts.Reset();
			Indices.Reset();
			for (int32 i = 0; i < It.Count; ++i)
			{
				Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(ToRender, F.TriPos[It.First + i] * S, FVector2f(0.5f, 0.5f),
					F.TriColor[It.First + i].ToFColor(true)));
				Indices.Add(static_cast<SlateIndex>(i));
			}
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, Layer, WhiteHandle, Verts, Indices, nullptr, 0, 0);
			break;
		}
		case FMechHudFrame::EKind::Text:
			FSlateDrawElement::MakeText(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2f(1.f, 1.f), FSlateLayoutTransform(S, It.Pos * S)),
				It.Text, It.Font, ESlateDrawEffect::None, It.Color);
			break;
		case FMechHudFrame::EKind::Image:
		{
			// 地图底图：贴图资源 + 每个顶点自己的 UV（小地图随视角旋转、裁成圆，用不了轴对齐的矩形笔刷）
			if (!It.Brush.IsValid())
			{
				break;
			}
			const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*It.Brush);
			if (!Handle.IsValid())
			{
				break;
			}
			Verts.Reset();
			Indices.Reset();
			for (int32 i = 0; i < It.Count; ++i)
			{
				Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(ToRender, F.ImgPos[It.First + i] * S, F.ImgUV[It.First + i],
					F.ImgColor[It.First + i].ToFColor(true)));
				Indices.Add(static_cast<SlateIndex>(i));
			}
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, Layer, Handle, Verts, Indices, nullptr, 0, 0);
			break;
		}
		}
	}
	return Layer;
}

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/unbounded_panel_widget.h"

#include "third_party/blink/public/mojom/frame/frame.mojom-blink.h"
#include "third_party/blink/public/mojom/page/widget.mojom-blink.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/html/html_panel_element.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/platform/scheduler/public/page_scheduler.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_controller.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"
#include "third_party/blink/renderer/platform/graphics/compositing/paint_artifact_compositor.h"
#include "cc/trees/layer_tree_host.h"
#include "cc/layers/solid_color_layer.h"
#include "ui/display/screen_infos.h"
#include "base/no_destructor.h"

namespace blink {

UnboundedPanelWidget::UnboundedPanelWidget(HTMLPanelElement* owner_element)
    : owner_element_(owner_element),
      popup_widget_host_(owner_element->GetExecutionContext()),
      widget_host_(owner_element->GetExecutionContext()) {}

UnboundedPanelWidget::~UnboundedPanelWidget() = default;

void UnboundedPanelWidget::Trace(Visitor* visitor) const {
  visitor->Trace(owner_element_);
  visitor->Trace(popup_widget_host_);
  visitor->Trace(widget_host_);
  visitor->Trace(paint_controller_persistent_data_);
  visitor->Trace(paint_artifact_compositor_);
}

void UnboundedPanelWidget::Initialize() {
  LocalFrame* frame = owner_element_->GetDocument().GetFrame();
  if (!frame) return;

  mojo::PendingAssociatedRemote<mojom::blink::Widget> widget;
  widget_receiver_ = widget.InitWithNewEndpointAndPassReceiver();

  mojo::PendingAssociatedRemote<mojom::blink::WidgetHost> widget_host_remote;
  mojo::PendingAssociatedReceiver<mojom::blink::WidgetHost> widget_host_receiver = 
      widget_host_remote.InitWithNewEndpointAndPassReceiver();

  mojo::PendingAssociatedReceiver<mojom::blink::PopupWidgetHost> popup_widget_host_receiver = 
      popup_widget_host_.BindNewEndpointAndPassReceiver(
          owner_element_->GetExecutionContext()->GetTaskRunner(TaskType::kInternalDefault));

  frame->GetLocalFrameHostRemote().CreateNewPopupWidget(
      std::move(popup_widget_host_receiver), std::move(widget_host_receiver),
      std::move(widget));

  popup_widget_host_->ShowPopup(gfx::Rect(0, 0, 200, 200), gfx::Rect(0, 0, 200, 200), BindOnce([]() {}));

  widget_base_ = std::make_unique<WidgetBase>(
      /*client=*/this,
      /*widget_host=*/CrossVariantMojoAssociatedRemote<mojom::blink::WidgetHostInterfaceBase>(
          std::move(widget_host_remote)),
      /*widget=*/CrossVariantMojoAssociatedReceiver<mojom::blink::WidgetInterfaceBase>(
          std::move(widget_receiver_)),
      /*task_runner=*/owner_element_->GetExecutionContext()->GetTaskRunner(TaskType::kInternalDefault),
      /*hidden=*/false,
      /*never_composited=*/false,
      /*is_embedded=*/false,
      /*is_for_scalable_page=*/false);

  display::ScreenInfos screen_infos = frame->GetPage()->GetChromeClient().GetScreenInfos(*frame);
  widget_base_->InitializeCompositing(*(frame->GetPage()->GetPageScheduler()),
                                      screen_infos, /*settings=*/nullptr,
                                      /*frame_widget_input_handler=*/nullptr,
                                      /*previous_widget=*/nullptr);

  paint_artifact_compositor_ = MakeGarbageCollected<PaintArtifactCompositor>(nullptr);
  if (widget_base_->LayerTreeHost()) {
    widget_base_->LayerTreeHost()->SetRootLayer(paint_artifact_compositor_->RootLayer());
    widget_base_->LayerTreeHost()->StopDeferringCommits(cc::PaintHoldingCommitTrigger::kWidgetSwapped);
  }
  widget_base_->SetCompositorVisible(true);
}

void UnboundedPanelWidget::Destroy() {
  if (widget_base_) {
    widget_base_.reset();
  }
}

void UnboundedPanelWidget::WidgetHostDisconnected() {
  Destroy();
}

void UnboundedPanelWidget::BeginMainFrame(const viz::BeginFrameArgs& args) {
}

void UnboundedPanelWidget::UpdateLifecycle(WebLifecycleUpdate requested_update, DocumentUpdateReason reason) {
  if (!widget_base_ || !widget_base_->LayerTreeHost()) return;
  if (!paint_artifact_compositor_) return;
  
  if (!paint_controller_persistent_data_) {
    paint_controller_persistent_data_ = MakeGarbageCollected<PaintControllerPersistentData>();
  }
  PaintController paint_controller(false, paint_controller_persistent_data_.Get());
  GraphicsContext graphics_context(paint_controller);
  auto* layout_object = owner_element_->GetLayoutObject();
  if (!layout_object || !layout_object->FirstFragment().HasLocalBorderBoxProperties()) return;
  auto state = layout_object->FirstFragment().LocalBorderBoxProperties();
  
  paint_controller.UpdateCurrentPaintChunkProperties(state);
  
  // Just emit a 400x400 red rect using the widget as client.
  gfx::Rect visual_rect(0, 0, 400, 400);
  if (!DrawingRecorder::UseCachedDrawingIfPossible(graphics_context, *this, DisplayItem::kDocumentBackground)) {
    DrawingRecorder recorder(
        graphics_context, *this, DisplayItem::kDocumentBackground,
        visual_rect);
    graphics_context.FillRect(gfx::RectF(visual_rect), Color::FromRGB(255, 0, 0), AutoDarkMode::Disabled());
  }

  const PaintArtifact& paint_artifact = paint_controller.CommitNewDisplayItems();
  LOG(INFO) << "Paint artifact size: " << paint_artifact.GetDisplayItemList().size();

  PaintArtifactCompositor::ViewportProperties viewport_properties;
  paint_artifact_compositor_->SetNeedsUpdate();
  paint_artifact_compositor_->Update(paint_artifact, viewport_properties, {}, {});
  
  paint_artifact_compositor_->RootLayer()->SetBounds(gfx::Size(400, 400));
  
  widget_base_->LayerTreeHost()->set_background_color(SkColors::kRed);
  widget_base_->LayerTreeHost()->SetNeedsCommit();
}

std::unique_ptr<cc::LayerTreeFrameSink>
UnboundedPanelWidget::AllocateNewLayerTreeFrameSink() {
  return nullptr;
}
WebInputEventResult UnboundedPanelWidget::DispatchBufferedTouchEvents() {
  return WebInputEventResult::kNotHandled;
}
WebInputEventResult UnboundedPanelWidget::HandleInputEvent(
    const WebCoalescedInputEvent&) {
  return WebInputEventResult::kNotHandled;
}
bool UnboundedPanelWidget::SupportsBufferedTouchEvents() {
  return false;
}
void UnboundedPanelWidget::WillHandleGestureEvent(const WebGestureEvent& event,
                                                  bool* suppress) {}
void UnboundedPanelWidget::WillHandleMouseEvent(const WebMouseEvent& event) {}
void UnboundedPanelWidget::ObserveGestureEventAndResult(
    const WebGestureEvent& gesture_event,
    const gfx::Vector2dF& unused_delta,
    const cc::OverscrollBehavior& overscroll_behavior,
    bool event_processed) {}

void UnboundedPanelWidget::UpdateVisualProperties(
    const VisualProperties& visual_properties) {
  if (widget_base_) {
    widget_base_->UpdateSurfaceAndScreenInfo(
        visual_properties.local_surface_id.value_or(viz::LocalSurfaceId()),
        visual_properties.compositor_viewport_pixel_rect,
        visual_properties.screen_infos);
    widget_base_->SetVisibleViewportSize(
        visual_properties.visible_viewport_size_device_px);
    
    // Trigger a paint immediately because we have new dimensions!
    widget_base_->LayerTreeHost()->SetNeedsCommit();
  }
}

const display::ScreenInfos& UnboundedPanelWidget::GetOriginalScreenInfos() {
  static const base::NoDestructor<display::ScreenInfos> empty_infos([] {
    display::ScreenInfos infos;
    infos.screen_infos.push_back(display::ScreenInfo());
    infos.current_display_id = infos.screen_infos[0].display_id;
    return infos;
  }());
  return *empty_infos;
}
gfx::Rect UnboundedPanelWidget::ViewportVisibleRect() {
  return gfx::Rect(0, 0, 100, 100);
}
KURL UnboundedPanelWidget::GetURLForDebugTrace() {
  return KURL();
}

}  // namespace blink

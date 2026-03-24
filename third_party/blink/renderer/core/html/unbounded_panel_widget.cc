// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/unbounded_panel_widget.h"

#include "third_party/blink/public/mojom/frame/frame.mojom-blink.h"
#include "third_party/blink/public/mojom/page/widget.mojom-blink.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/html/html_panel_element.h"
#include "third_party/blink/renderer/core/paint/paint_layer_painter.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/core/layout/layout_box_model_object.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/paint_flags.h"
#include "third_party/blink/renderer/core/paint/paint_layer_painter.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/platform/scheduler/public/page_scheduler.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_controller.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"
#include "cc/layers/picture_layer.h"
#include "cc/layers/solid_color_layer.h"
#include "third_party/blink/renderer/platform/graphics/compositing/paint_artifact_compositor.h"
#include "cc/trees/layer_tree_host.h"
#include "cc/layers/solid_color_layer.h"
#include "ui/display/screen_infos.h"
#include "third_party/blink/renderer/core/paint/paint_info.h"
#include "third_party/blink/renderer/core/paint/object_painter.h"
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
  LOG(ERROR) << "UnboundedPanelWidget::Initialize BEGIN";
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

  popup_widget_host_->ShowPopup(
      gfx::Rect(0, 0, 0, 0), gfx::Rect(0, 0, 0, 0),
      BindOnce([]() {}));

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
      /*is_for_scalable_page=*/true);

  display::ScreenInfos screen_infos = frame->GetPage()->GetChromeClient().GetScreenInfos(*frame);
  widget_base_->InitializeCompositing(*(frame->GetPage()->GetPageScheduler()),
                                      screen_infos, /*settings=*/nullptr,
                                      /*frame_widget_input_handler=*/nullptr,
                                      /*previous_widget=*/nullptr);

  paint_artifact_compositor_ = MakeGarbageCollected<PaintArtifactCompositor>(nullptr);
  if (widget_base_->LayerTreeHost()) {
    widget_base_->LayerTreeHost()->SetRootLayer(paint_artifact_compositor_->RootLayer());
    
    // Do not overwrite LayerTreeHost Viewport properties, they come from the browser.

    PaintController paint_controller(false, nullptr);
    PaintArtifactCompositor::ViewportProperties viewport_properties;
    viewport_properties.page_scale = &TransformPaintPropertyNode::Root();
    viewport_properties.inner_scroll_translation = &TransformPaintPropertyNode::Root();
    viewport_properties.outer_clip = &ClipPaintPropertyNode::Root();
    viewport_properties.outer_scroll_translation = &TransformPaintPropertyNode::Root();
    paint_artifact_compositor_->Update(paint_controller.CommitNewDisplayItems(), viewport_properties, {}, {});
    
    widget_base_->LayerTreeHost()->StopDeferringCommits(cc::PaintHoldingCommitTrigger::kWidgetSwapped);
  }
  widget_base_->SetCompositorVisible(true);
}

void UnboundedPanelWidget::Destroy() {
  if (widget_base_) {
    widget_base_.reset();
  }
}

void UnboundedPanelWidget::SetNeedsCommit() {
  if (widget_base_ && widget_base_->LayerTreeHost()) {
    widget_base_->LayerTreeHost()->SetNeedsCommit();
  }
}

void UnboundedPanelWidget::WidgetHostDisconnected() {
  Destroy();
}

void UnboundedPanelWidget::BeginMainFrame(const viz::BeginFrameArgs& args) {
  LOG(ERROR) << "UnboundedPanelWidget::BeginMainFrame";
  if (widget_base_ && widget_base_->LayerTreeHost()) {
    widget_base_->LayerTreeHost()->SetNeedsCommit();
  }
}

void UnboundedPanelWidget::UpdateLifecycle(WebLifecycleUpdate requested_update, DocumentUpdateReason reason) {
  if (!owner_element_) [[unlikely]] {
    return;
  }

  LOG(ERROR) << "UpdateLifecycle BEGIN. layer_tree_host_viewport=" 
             << widget_base_->LayerTreeHost()->device_viewport_rect().ToString()
             << " is_visible=" << widget_base_->LayerTreeHost()->IsVisible();

  if (!widget_base_ || !widget_base_->LayerTreeHost()) {
    LOG(ERROR) << "UpdateLifecycle: No widget_base_ or LayerTreeHost";
    return;
  }
  if (!paint_artifact_compositor_) {
    LOG(ERROR) << "UpdateLifecycle: No paint_artifact_compositor_";
    return;
  }
  
  auto* layout_object = owner_element_->GetLayoutObject();
  if (!layout_object) {
    LOG(ERROR) << "UpdateLifecycle: No layout_object";
    return;
  }
  
  if (!layout_object->FirstFragment().HasLocalBorderBoxProperties()) {
    LOG(ERROR) << "UpdateLifecycle: No LocalBorderBoxProperties";
    return;
  }
  
  LOG(ERROR) << "UpdateLifecycle: HasLocalBorderBoxProperties TRUE. Setting needs commit";

  // Continuously request commits while waiting for the main frame to be clean.
  widget_base_->LayerTreeHost()->SetNeedsCommit();

  auto& document = owner_element_->GetDocument();
  if (document.NeedsLayoutTreeUpdate() || document.Lifecycle().GetState() < DocumentLifecycle::kPrePaintClean) {
    LOG(ERROR) << "UpdateLifecycle: Forcing main document update";
    if (document.View()) {
      document.View()->UpdateAllLifecyclePhases(DocumentUpdateReason::kUnknown);
    } else {
      LOG(ERROR) << "UpdateLifecycle: No document view!";
      return;
    }
  }

  if (document.Lifecycle().GetState() < DocumentLifecycle::kPrePaintClean) {
    LOG(ERROR) << "UpdateLifecycle: Document failed to reach PrePaintClean!";
    return;
  }
  LOG(ERROR) << "UpdateLifecycle: Document is PrePaintClean!";

  if (!layout_object)
    return;

  auto* panel_layout = To<LayoutBox>(layout_object);

  PaintController paint_controller(false, nullptr);
  paint_controller.UpdateCurrentPaintChunkProperties(PropertyTreeState::Root());

  GraphicsContext context(paint_controller);

  PaintLayerPainter(*panel_layout->Layer())
      .Paint(context, PaintFlag::kPaintingUnboundedPanel);

  const PaintArtifact& artifact = paint_controller.CommitNewDisplayItems();

  PaintArtifactCompositor::ViewportProperties viewport_properties;
  viewport_properties.page_scale = &TransformPaintPropertyNode::Root();
  viewport_properties.inner_scroll_translation = &TransformPaintPropertyNode::Root();
  viewport_properties.outer_clip = &ClipPaintPropertyNode::Root();
  viewport_properties.outer_scroll_translation = &TransformPaintPropertyNode::Root();

  gfx::Rect absolute_rect = panel_layout->AbsoluteBoundingBoxRect();
  gfx::Rect screen_rect = owner_element_->GetDocument().View()->FrameToScreen(absolute_rect);
  int initial_width = screen_rect.width();
  int initial_height = screen_rect.height();

  auto* root = paint_artifact_compositor_->RootLayer();
  root->SetBounds(gfx::Size(initial_width, initial_height));
  popup_widget_host_->SetPopupBounds(
      screen_rect,
      BindOnce([]() {}));

  paint_artifact_compositor_->SetNeedsUpdate();
  paint_artifact_compositor_->Update(
      artifact,
      viewport_properties,
      {}, {});

  if (widget_base_->LayerTreeHost()->IsVisible()) {
    widget_base_->LayerTreeHost()->SetNeedsCommit();
  }
}

std::unique_ptr<cc::LayerTreeFrameSink>
UnboundedPanelWidget::AllocateNewLayerTreeFrameSink() {
  LOG(ERROR) << "UnboundedPanelWidget::AllocateNewLayerTreeFrameSink BEGIN";
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
  LOG(ERROR) << "UnboundedPanelWidget::UpdateVisualProperties BEGIN";
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

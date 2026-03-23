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
#include "third_party/blink/renderer/core/layout/layout_box_model_object.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/paint_layer_painter.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/blink/renderer/platform/wtf/wtf_size_t.h"
#include "third_party/blink/renderer/platform/graphics/compositing/paint_artifact_compositor.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "cc/trees/layer_tree_host.h"
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
  visitor->Trace(paint_controller_persistent_data_);
  visitor->Trace(popup_widget_host_);
  visitor->Trace(widget_host_);
  visitor->Trace(paint_artifact_compositor_);
}
void UnboundedPanelWidget::Initialize() {
  widget_base_ = std::make_unique<WidgetBase>(
      /*client=*/this,
      /*widget_host=*/CrossVariantMojoAssociatedRemote<mojom::blink::WidgetHostInterfaceBase>(),
      /*widget=*/CrossVariantMojoAssociatedReceiver<mojom::blink::WidgetInterfaceBase>(),
      /*task_runner=*/owner_element_->GetExecutionContext()->GetTaskRunner(TaskType::kInternalDefault),
      /*hidden=*/false,
      /*never_composited=*/false,
      /*is_embedded=*/false,
      /*is_for_scalable_page=*/false);
  paint_artifact_compositor_ = MakeGarbageCollected<PaintArtifactCompositor>(nullptr);
  if (widget_base_->LayerTreeHost()) {
    widget_base_->LayerTreeHost()->SetRootLayer(paint_artifact_compositor_->RootLayer());
  }
}

void UnboundedPanelWidget::Destroy() {
  if (widget_base_) {
    widget_base_.reset();
  }
}

void UnboundedPanelWidget::PaintTree() {
  if (!paint_controller_persistent_data_) {
    paint_controller_persistent_data_ = MakeGarbageCollected<PaintControllerPersistentData>();
  }

  LayoutObject* layout_object = owner_element_->GetLayoutObject();
  if (!layout_object || !layout_object->IsBoxModelObject()) return;

  PaintLayer* layer = To<LayoutBoxModelObject>(layout_object)->Layer();
  if (!layer) return;

  PaintController paint_controller(false, paint_controller_persistent_data_.Get());
  GraphicsContext graphics_context(paint_controller);

  PaintLayerPainter(*layer).Paint(graphics_context, PaintFlag::kPaintingUnboundedPanel);

  const PaintArtifact& paint_artifact = paint_controller.CommitNewDisplayItems();

  PaintArtifactCompositor::ViewportProperties viewport_properties;
  paint_artifact_compositor_->Update(paint_artifact, viewport_properties, StackScrollTranslationVector(), Vector<std::unique_ptr<cc::ViewTransitionRequest>>());
  
  // Mathematical coordinate translation:
  // Apply a root translation corresponding to the negative absolute layout position of the panel.
  gfx::Rect layout_bounds = layout_object->AbsoluteBoundingBoxRect();
  paint_artifact_compositor_->RootLayer()->SetOffsetToTransformParent(
      gfx::Vector2dF(-layout_bounds.x(),
                     -layout_bounds.y()));
}

void UnboundedPanelWidget::WidgetHostDisconnected() {
  Destroy();
}

void UnboundedPanelWidget::BeginMainFrame(const viz::BeginFrameArgs& args) { widget_base_->LayerTreeHost()->WillBeginMainFrame(); }
void UnboundedPanelWidget::UpdateLifecycle(WebLifecycleUpdate requested_update, DocumentUpdateReason reason) { if (LocalFrame* frame = owner_element_->GetDocument().GetFrame()) frame->GetPage()->UpdateLifecycle(*frame, requested_update, DocumentUpdateReason::kPagePopup); }
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
    const VisualProperties& visual_properties) {}
const display::ScreenInfos& UnboundedPanelWidget::GetOriginalScreenInfos() {
  static const base::NoDestructor<display::ScreenInfos> empty;
  return *empty;
}
gfx::Rect UnboundedPanelWidget::ViewportVisibleRect() {
  return gfx::Rect(0, 0, 100, 100);
}
KURL UnboundedPanelWidget::GetURLForDebugTrace() {
  return KURL();
}

}  // namespace blink


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
}
void UnboundedPanelWidget::PaintTree() {
  if (!paint_controller_persistent_data_) {
    paint_controller_persistent_data_ =
        MakeGarbageCollected<PaintControllerPersistentData>();
  }

  LayoutObject* layout_object = owner_element_->GetLayoutObject();
  if (!layout_object || !layout_object->IsBoxModelObject()) {
    return;
  }

  PaintLayer* layer = To<LayoutBoxModelObject>(layout_object)->Layer();
  if (!layer) {
    return;
  }

  PaintController paint_controller(false,
                                   paint_controller_persistent_data_.Get());
  GraphicsContext graphics_context(paint_controller);

  PaintLayerPainter(*layer).Paint(graphics_context,
                                  PaintFlag::kPaintingUnboundedPanel);

  paint_controller.CommitNewDisplayItems();
}

bool UnboundedPanelWidget::HasDisplayItemsForTesting() const {
  if (auto* data = paint_controller_persistent_data_.Get()) {
    return !data->GetPaintArtifact().IsEmpty();
  }
  return false;
}

void UnboundedPanelWidget::Initialize() {
  DCHECK(owner_element_);
  DCHECK(!popup_widget_host_.is_bound());

  LocalFrame* frame = owner_element_->GetDocument().GetFrame();
  if (!frame)
    return;

  mojo::PendingAssociatedRemote<mojom::blink::Widget> widget;
  widget_receiver_ = widget.InitWithNewEndpointAndPassReceiver();

  mojo::PendingAssociatedReceiver<mojom::blink::WidgetHost>
      widget_host_receiver = widget_host_.BindNewEndpointAndPassReceiver(
          frame->GetTaskRunner(TaskType::kInternalDefault));

  mojo::PendingAssociatedReceiver<mojom::blink::PopupWidgetHost>
      popup_widget_host_receiver =
          popup_widget_host_.BindNewEndpointAndPassReceiver(
              frame->GetTaskRunner(TaskType::kInternalDefault));

  popup_widget_host_.set_disconnect_handler(BindOnce(
      &UnboundedPanelWidget::WidgetHostDisconnected, WrapWeakPersistent(this)));

  frame->GetLocalFrameHostRemote().CreateNewPopupWidget(
      std::move(popup_widget_host_receiver), std::move(widget_host_receiver),
      std::move(widget));
}

void UnboundedPanelWidget::Destroy() {
  if (popup_widget_host_.is_bound()) {
    popup_widget_host_.reset();
  }
  if (widget_host_.is_bound()) {
    widget_host_.reset();
  }
  widget_receiver_.reset();
}

void UnboundedPanelWidget::WidgetHostDisconnected() {
  Destroy();
}

}  // namespace blink

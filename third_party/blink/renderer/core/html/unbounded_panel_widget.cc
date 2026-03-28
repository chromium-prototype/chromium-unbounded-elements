// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/unbounded_panel_widget.h"
#include "cc/trees/property_ids.h"

#include "third_party/blink/public/mojom/frame/frame.mojom-blink.h"
#include "third_party/blink/public/mojom/page/widget.mojom-blink.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/html_names.h"
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
#include "third_party/blink/renderer/core/page/focus_controller.h"
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
#include "third_party/blink/renderer/platform/graphics/paint/transform_paint_property_node.h"
#include "third_party/blink/renderer/platform/graphics/paint/clip_paint_property_node.h"
#include "ui/gfx/geometry/transform.h"
#include "third_party/blink/renderer/core/frame/web_frame_widget_impl.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/common/input/web_pointer_event.h"
#include "third_party/blink/public/common/input/web_touch_event.h"
#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"

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

  auto screen_info = frame->GetPage()->GetChromeClient().GetScreenInfos(*frame);
  widget_base_->InitializeCompositing(*(frame->GetPage()->GetPageScheduler()),
                                      screen_info, /*settings=*/nullptr,
                                      /*frame_widget_input_handler=*/nullptr,
                                      /*previous_widget=*/nullptr);

  paint_artifact_compositor_ = MakeGarbageCollected<PaintArtifactCompositor>(nullptr);
  if (widget_base_->LayerTreeHost()) {
    widget_base_->LayerTreeHost()->SetRootLayer(paint_artifact_compositor_->RootLayer());
    
    PaintController paint_controller(false, nullptr);
    PaintArtifactCompositor::ViewportProperties viewport_properties;
    viewport_properties.page_scale = &TransformPaintPropertyNode::Root();
    viewport_properties.inner_scroll_translation = &TransformPaintPropertyNode::Root();
    viewport_properties.outer_clip = &ClipPaintPropertyNode::Root();
    viewport_properties.outer_scroll_translation = &TransformPaintPropertyNode::Root();
    paint_artifact_compositor_->Update(paint_controller.CommitNewDisplayItems(), viewport_properties, {}, {});
    
    widget_base_->LayerTreeHost()->StopDeferringCommits(cc::PaintHoldingCommitTrigger::kWidgetSwapped);
  }

  popup_widget_host_->SetPopupCapture(owner_element_->FastHasAttribute(html_names::kCaptureAttr));
  popup_widget_host_->ShowPopup(
      gfx::Rect(0, 0, 1, 1), gfx::Rect(0, 0, 1, 1),
      BindOnce([](UnboundedPanelWidget* widget) {
        if (!widget || !widget->widget_base_) return;
        widget->widget_base_->SetCompositorVisible(true);
      }, WrapWeakPersistent(this)));

}

void UnboundedPanelWidget::Destroy() {
  if (popup_widget_host_.is_bound()) {
    popup_widget_host_->RequestClosePopup();
  }
  if (widget_base_) {
    widget_base_->Shutdown(false);
    widget_base_.reset();
  }
}

void UnboundedPanelWidget::SetNeedsCommit() {
  if (widget_base_ && widget_base_->LayerTreeHost()) {
    widget_base_->LayerTreeHost()->SetNeedsCommit();
  }
}

void UnboundedPanelWidget::SynchronizeBounds() {
  if (!owner_element_) return;
  auto* layout_object = owner_element_->GetLayoutObject();
  if (!layout_object) return;
  auto* panel_layout = To<LayoutBox>(layout_object);

  gfx::Rect absolute_rect = panel_layout->AbsoluteBoundingBoxRect();
  gfx::Rect frame_rect = owner_element_->GetDocument().View()->DocumentToFrame(absolute_rect);
  gfx::Rect screen_rect = owner_element_->GetDocument().View()->FrameToScreen(frame_rect);

  if (popup_widget_host_.is_bound()) {
    popup_widget_host_->SetPopupBounds(screen_rect, BindOnce([]() {}));
  }
}

void UnboundedPanelWidget::WidgetHostDisconnected() {
  Destroy();
}

void UnboundedPanelWidget::BeginMainFrame(const viz::BeginFrameArgs& args) {
}

void UnboundedPanelWidget::UpdateLifecycle(WebLifecycleUpdate requested_update, DocumentUpdateReason reason) {
  if (!owner_element_) [[unlikely]] {
    return;
  }

  if (!widget_base_ || !widget_base_->LayerTreeHost()) {
    return;
  }
  if (!paint_artifact_compositor_) {
    return;
  }
  
  auto* layout_object = owner_element_->GetLayoutObject();
  if (!layout_object) {
    return;
  }
  
  if (!layout_object->FirstFragment().HasLocalBorderBoxProperties()) {
    return;
  }
  
  // Continuously request commits while waiting for the main frame to be clean.
  widget_base_->LayerTreeHost()->SetNeedsCommit();

  auto& document = owner_element_->GetDocument();
  if (document.NeedsLayoutTreeUpdate() || document.Lifecycle().GetState() < DocumentLifecycle::kPrePaintClean) {
    if (document.View()) {
      document.View()->UpdateAllLifecyclePhases(DocumentUpdateReason::kUnknown);
    } else {
      return;
    }
  }

  if (document.Lifecycle().GetState() < DocumentLifecycle::kPrePaintClean) {
    return;
  }

  if (!layout_object)
    return;

  auto* panel_layout = To<LayoutBox>(layout_object);

  PaintController paint_controller(false, nullptr);
  paint_controller.UpdateCurrentPaintChunkProperties(layout_object->FirstFragment().LocalBorderBoxProperties());

  GraphicsContext context(paint_controller);

  PaintLayerPainter(*panel_layout->Layer())
      .Paint(context, PaintFlag::kPaintingUnboundedPanel);

  const PaintArtifact& artifact = paint_controller.CommitNewDisplayItems();

  gfx::Rect absolute_rect = panel_layout->AbsoluteBoundingBoxRect();
  gfx::Rect frame_rect = owner_element_->GetDocument().View()->DocumentToFrame(absolute_rect);
  gfx::Rect screen_rect = owner_element_->GetDocument().View()->FrameToScreen(frame_rect);
  int physical_width = absolute_rect.width();
  int physical_height = absolute_rect.height();

  PaintArtifactCompositor::ViewportProperties viewport_properties;
  viewport_properties.page_scale = &TransformPaintPropertyNode::Root();
  viewport_properties.inner_scroll_translation = &TransformPaintPropertyNode::Root();
  viewport_properties.outer_clip = &ClipPaintPropertyNode::Root();
  viewport_properties.outer_scroll_translation = &TransformPaintPropertyNode::Root();

  auto* root = paint_artifact_compositor_->RootLayer();
  root->SetBounds(gfx::Size(physical_width, physical_height));
  popup_widget_host_->SetPopupCapture(owner_element_->FastHasAttribute(html_names::kCaptureAttr));
  popup_widget_host_->SetPopupBounds(
      screen_rect,
      BindOnce([]() {}));

  paint_artifact_compositor_->SetNeedsUpdate();
  paint_artifact_compositor_->Update(
      artifact,
      viewport_properties,
      {}, {});

  for (auto layer_child : paint_artifact_compositor_->RootLayer()->children()) {
    gfx::Vector2dF current_offset = layer_child->offset_to_transform_parent();
    layer_child->SetOffsetToTransformParent(current_offset - gfx::Vector2dF(absolute_rect.x(), absolute_rect.y()));
  }

  gfx::Rect device_viewport_rect(0, 0, physical_width, physical_height);
  widget_base_->LayerTreeHost()->SetViewportRectAndScale(
      device_viewport_rect,
      widget_base_->GetOriginalDeviceScaleFactor(),
      widget_base_->local_surface_id_from_parent());

  if (widget_base_->LayerTreeHost()->IsVisible()) {
    widget_base_->LayerTreeHost()->SetNeedsCommit();
  }
}

std::unique_ptr<cc::LayerTreeFrameSink>
UnboundedPanelWidget::AllocateNewLayerTreeFrameSink() {
  return nullptr;
}
WebInputEventResult UnboundedPanelWidget::DispatchBufferedTouchEvents() {
  return WebInputEventResult::kNotHandled;
}
WebInputEventResult UnboundedPanelWidget::HandleInputEvent(
    const WebCoalescedInputEvent& coalesced_event) {
  if (!owner_element_) return WebInputEventResult::kNotHandled;
  Document& document = owner_element_->GetDocument();
  LocalFrame* frame = document.GetFrame();
  if (!frame) return WebInputEventResult::kNotHandled;

  auto* layout_object = owner_element_->GetLayoutObject();
  if (!layout_object) return WebInputEventResult::kNotHandled;

  auto* panel_layout = To<LayoutBox>(layout_object);
  gfx::Rect absolute_rect = panel_layout->AbsoluteBoundingBoxRect();
  gfx::PointF offset(absolute_rect.x(), absolute_rect.y());
  
  // Natively force the main document to be active. When the OS focuses the secondary 
  // popup window, the browser process receives a blur for the main WebContents, 
  // preventing carets from blinking and suppressing document.hasFocus(). By intercepting 
  // interaction here, we coerce the Page back into an active/focused state.
  if (auto* page = document.GetPage()) {
    if (!page->GetFocusController().IsActive() || !page->GetFocusController().IsFocused()) {
      page->GetFocusController().SetActive(true);
      page->GetFocusController().SetFocused(true);
    }
  }

  WebCoalescedInputEvent translated_coalesced(coalesced_event);
  WebInputEvent* mutable_event = translated_coalesced.EventPointer();

  if (WebInputEvent::IsMouseEventType(mutable_event->GetType())) {
    WebMouseEvent* mouse_event = static_cast<WebMouseEvent*>(mutable_event);
    if (mouse_event->GetType() == WebInputEvent::Type::kMouseDown) {
      is_mouse_button_down_ = true;
    } else if (mouse_event->GetType() == WebInputEvent::Type::kMouseUp) {
      is_mouse_button_down_ = false;
    }
    mouse_event->SetPositionInWidget(mouse_event->PositionInWidget().x() + offset.x(),
                                     mouse_event->PositionInWidget().y() + offset.y());
  } else if (WebInputEvent::IsPointerEventType(mutable_event->GetType())) {
    WebPointerEvent* pointer_event = static_cast<WebPointerEvent*>(mutable_event);
    pointer_event->SetPositionInWidget(pointer_event->PositionInWidget().x() + offset.x(),
                                       pointer_event->PositionInWidget().y() + offset.y());
  } else if (WebInputEvent::IsTouchEventType(mutable_event->GetType())) {
    WebTouchEvent* touch_event = static_cast<WebTouchEvent*>(mutable_event);
    for (unsigned i = 0; i < touch_event->touches_length; ++i) {
      touch_event->touches[i].SetPositionInWidget(
          touch_event->touches[i].PositionInWidget().x() + offset.x(),
          touch_event->touches[i].PositionInWidget().y() + offset.y());
    }
  }

  if (auto* frame_widget = frame->LocalFrameRoot().GetWidgetForLocalRoot()) {
    auto* main_widget = static_cast<WebFrameWidgetImpl*>(frame_widget);
    return main_widget->HandleInputEvent(translated_coalesced);
  }

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

void UnboundedPanelWidget::FocusChanged(mojom::blink::FocusState focus_state) {
  if (!owner_element_) [[unlikely]] return;
  auto& document = owner_element_->GetDocument();
  auto* page = document.GetPage();
  if (!page) return;

  bool is_active = (focus_state == mojom::blink::FocusState::kFocused) ||
                   (focus_state == mojom::blink::FocusState::kNotFocusedAndActive);
  bool is_focused = (focus_state == mojom::blink::FocusState::kFocused);

  // When the UnboundedPanelWidget gains focus, we MUST force the page's focus controller
  // to believe it is active so the caret can blink within the panel.
  // We do NOT want to blindly blur the page if the panel loses focus, because the main window
  // might still be actively focused without us knowing. The main window's WebFrameWidgetImpl 
  // will correctly receive blur events from the browser process if the entire app is blurred.
  if (is_active) {
    page->GetFocusController().SetActive(true);
  }
  if (is_focused) {
    page->GetFocusController().SetFocused(true);
  } else if (!is_active) {
    // If the widget is neither focused nor active, it means the user clicked outside
    // the OS window (either on the main browser window or another application).
    owner_element_->DispatchEvent(*Event::Create(AtomicString("outsideclick")));
  }
}

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
    if (widget_base_->LayerTreeHost()) {
      widget_base_->LayerTreeHost()->SetNeedsCommit();
    }
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


void UnboundedPanelWidget::SetNeedsMouseCapture(bool capture) {
  if (popup_widget_host_.is_bound()) {
    popup_widget_host_->SetPopupCapture(capture);
  }
}

void UnboundedPanelWidget::DidChangeCursor(const ui::Cursor& cursor) {
  last_cursor_for_testing_ = cursor;
  
  // Suppress cursor updates if the mouse button is down (dragging).
  // This mirrors how the OS suppresses cursor updates for the main
  // browser window when it has implicit mouse capture. Since popups
  // don't always get OS capture automatically, we enforce the visual
  // text-selection caret consistency here.
  if (is_mouse_button_down_) {
    return;
  }

  if (widget_base_) {
    widget_base_->SetCursor(cursor);
  }
}

}  // namespace blink

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_HTML_UNBOUNDED_PANEL_WIDGET_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_HTML_UNBOUNDED_PANEL_WIDGET_H_

#include "third_party/blink/public/mojom/page/widget.mojom-blink.h"
#include "third_party/blink/renderer/platform/widget/widget_base_client.h"
#include "third_party/blink/renderer/platform/widget/widget_base.h"
#include "third_party/blink/public/mojom/widget/platform_widget.mojom-blink.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/heap/prefinalizer.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_associated_remote.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_controller.h"
#include "third_party/blink/renderer/platform/graphics/paint/display_item_client.h"
#include "ui/gfx/geometry/rect.h"

namespace blink {

class HTMLPanelElement;
class PaintControllerPersistentData;
class PaintArtifactCompositor;

class CORE_EXPORT UnboundedPanelWidget final
    : public GarbageCollected<UnboundedPanelWidget>,
      public WidgetBaseClient,
      public DisplayItemClient {
 public:
  explicit UnboundedPanelWidget(HTMLPanelElement* owner_element);
  ~UnboundedPanelWidget() override;

  // DisplayItemClient
  String DebugName() const override { return "UnboundedPanelWidget"; }

  void Trace(Visitor* visitor) const override;

  void PaintTree();

  void Initialize();
  void Destroy();
  void SetNeedsCommit();

  // WidgetBaseClient overrides:
  void BeginMainFrame(const viz::BeginFrameArgs& args) override;
  void UpdateLifecycle(WebLifecycleUpdate requested_update,
                       DocumentUpdateReason reason) override;
  std::unique_ptr<cc::LayerTreeFrameSink> AllocateNewLayerTreeFrameSink()
      override;
  WebInputEventResult DispatchBufferedTouchEvents() override;
  WebInputEventResult HandleInputEvent(
      const WebCoalescedInputEvent&) override;
  bool SupportsBufferedTouchEvents() override;
  void WillHandleGestureEvent(const WebGestureEvent& event,
                              bool* suppress) override;
  void WillHandleMouseEvent(const WebMouseEvent& event) override;
  void ObserveGestureEventAndResult(
      const WebGestureEvent& gesture_event,
      const gfx::Vector2dF& unused_delta,
      const cc::OverscrollBehavior& overscroll_behavior,
      bool event_processed) override;
  void FocusChanged(mojom::blink::FocusState focus_state) override;
  void UpdateVisualProperties(
      const VisualProperties& visual_properties) override;
  const display::ScreenInfos& GetOriginalScreenInfos() override;
  gfx::Rect ViewportVisibleRect() override;
  KURL GetURLForDebugTrace() override;


  bool HasDisplayItemsForTesting() const;

 private:
  void WidgetHostDisconnected();

  Member<HTMLPanelElement> owner_element_;
  Member<PaintControllerPersistentData> paint_controller_persistent_data_;
  Member<PaintArtifactCompositor> paint_artifact_compositor_;

  std::unique_ptr<WidgetBase> widget_base_;

  [[maybe_unused]] HeapMojoAssociatedRemote<mojom::blink::PopupWidgetHost>
      popup_widget_host_;
  [[maybe_unused]] HeapMojoAssociatedRemote<mojom::blink::WidgetHost>
      widget_host_;
  [[maybe_unused]] mojo::PendingAssociatedReceiver<mojom::blink::Widget>
      widget_receiver_;
  bool has_shown_popup_ = false;

  USING_PRE_FINALIZER(UnboundedPanelWidget, Destroy);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_HTML_UNBOUNDED_PANEL_WIDGET_H_

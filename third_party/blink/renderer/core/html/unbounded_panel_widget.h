// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_HTML_UNBOUNDED_PANEL_WIDGET_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_HTML_UNBOUNDED_PANEL_WIDGET_H_

#include "third_party/blink/public/mojom/page/widget.mojom-blink.h"
#include "third_party/blink/public/mojom/widget/platform_widget.mojom-blink.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_associated_remote.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_controller.h"
#include "ui/gfx/geometry/rect.h"

namespace blink {

class HTMLPanelElement;

class CORE_EXPORT UnboundedPanelWidget
    : public GarbageCollected<UnboundedPanelWidget> {
 public:
  explicit UnboundedPanelWidget(HTMLPanelElement* owner_element);
  ~UnboundedPanelWidget();

  void Trace(Visitor* visitor) const;

  void PaintTree();

  void Initialize();
  void Destroy();

  bool HasDisplayItemsForTesting() const;

 private:
  void WidgetHostDisconnected();

  Member<HTMLPanelElement> owner_element_;
  Member<PaintControllerPersistentData> paint_controller_persistent_data_;

  [[maybe_unused]] HeapMojoAssociatedRemote<mojom::blink::PopupWidgetHost>
      popup_widget_host_;
  [[maybe_unused]] HeapMojoAssociatedRemote<mojom::blink::WidgetHost>
      widget_host_;
  [[maybe_unused]] mojo::PendingAssociatedReceiver<mojom::blink::Widget>
      widget_receiver_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_HTML_UNBOUNDED_PANEL_WIDGET_H_

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/html_panel_element.h"

#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/html/unbounded_panel_widget.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/layout/layout_unbounded_panel.h"
#include "third_party/blink/renderer/core/page/page.h"

namespace blink {

HTMLPanelElement::HTMLPanelElement(Document& document)
    : HTMLElement(html_names::kPanelTag, document),
      PageVisibilityObserver(document.GetPage()) {}

LayoutObject* HTMLPanelElement::CreateLayoutObject(const ComputedStyle&) {
  return MakeGarbageCollected<LayoutUnboundedPanel>(this);
}

Node::InsertionNotificationRequest HTMLPanelElement::InsertedInto(
    ContainerNode& insertion_point) {
  Node::InsertionNotificationRequest request =
      HTMLElement::InsertedInto(insertion_point);

  if (insertion_point.isConnected()) {
    GetDocument().UnboundedPanels().insert(this);
    if (!widget_ && GetPage() && GetPage()->IsPageVisible()) {
      widget_ = MakeGarbageCollected<UnboundedPanelWidget>(this);
      widget_->Initialize();
    }
  }
  return request;
}

void HTMLPanelElement::PageVisibilityChanged() {
  if (!GetPage()) {
    return;
  }

  if (GetPage()->IsPageVisible()) {
    if (isConnected() && !widget_) {
      widget_ = MakeGarbageCollected<UnboundedPanelWidget>(this);
      widget_->Initialize();
    }
  } else {
    if (widget_) {
      widget_->Destroy();
      widget_ = nullptr;
    }
  }
}

void HTMLPanelElement::RemovedFrom(ContainerNode& insertion_point) {
  HTMLElement::RemovedFrom(insertion_point);

  if (insertion_point.isConnected() && !isConnected()) {
    GetDocument().UnboundedPanels().erase(this);
    if (widget_) {
      widget_->Destroy();
      widget_ = nullptr;
    }
  }
}

void HTMLPanelElement::show() {
  setAttribute(html_names::kOpenAttr, g_empty_atom);
}

void HTMLPanelElement::close() {
  removeAttribute(html_names::kOpenAttr);
}

void HTMLPanelElement::DefaultEventHandler(Event& event) {
  if (event.type() == AtomicString("outsideclick")) {
    if (FastHasAttribute(html_names::kDismissOnBlurAttr)) {
      close();
      event.SetDefaultHandled();
    }
  }
  HTMLElement::DefaultEventHandler(event);
}

void HTMLPanelElement::Trace(Visitor* visitor) const {
  visitor->Trace(widget_);
  HTMLElement::Trace(visitor);
  PageVisibilityObserver::Trace(visitor);
}

void HTMLPanelElement::AttributeChanged(
    const AttributeModificationParams& params) {
  if (params.name == html_names::kCaptureAttr) {
    if (widget_) {
      widget_->SetNeedsMouseCapture(FastHasAttribute(html_names::kCaptureAttr));
    }
  }
  HTMLElement::AttributeChanged(params);
}
}  // namespace blink

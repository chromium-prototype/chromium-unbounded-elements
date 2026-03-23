// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_HTML_HTML_PANEL_ELEMENT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_HTML_HTML_PANEL_ELEMENT_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/html/html_element.h"

namespace blink {

class UnboundedPanelWidget;

class CORE_EXPORT HTMLPanelElement : public HTMLElement {
  DEFINE_WRAPPERTYPEINFO();

 public:
  explicit HTMLPanelElement(Document&);

  LayoutObject* CreateLayoutObject(const ComputedStyle&) override;

  Node::InsertionNotificationRequest InsertedInto(
      ContainerNode& insertion_point) override;
  void RemovedFrom(ContainerNode& insertion_point) override;

  UnboundedPanelWidget* GetWidgetForTesting() const { return widget_.Get(); }

  void Trace(Visitor* visitor) const override;

 private:
  Member<UnboundedPanelWidget> widget_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_HTML_HTML_PANEL_ELEMENT_H_

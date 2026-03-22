// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_UNBOUNDED_PANEL_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_UNBOUNDED_PANEL_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/layout/layout_block_flow.h"

namespace blink {

class CORE_EXPORT LayoutUnboundedPanel : public LayoutBlockFlow {
 public:
  explicit LayoutUnboundedPanel(Element* element);

  const char* GetName() const override {
    NOT_DESTROYED();
    return "LayoutUnboundedPanel";
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_UNBOUNDED_PANEL_H_

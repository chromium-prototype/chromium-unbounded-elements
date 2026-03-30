// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/layout_unbounded_panel.h"

#include "third_party/blink/renderer/core/html/html_panel_element.h"
#include "third_party/blink/renderer/core/html/unbounded_panel_widget.h"

namespace blink {

LayoutUnboundedPanel::LayoutUnboundedPanel(Element* element)
    : LayoutBlockFlow(element) {}

}  // namespace blink

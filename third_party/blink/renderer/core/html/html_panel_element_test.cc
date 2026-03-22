// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/html_panel_element.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/html/html_body_element.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/testing/page_test_base.h"

namespace blink {

class HTMLPanelElementTest : public PageTestBase {};

TEST_F(HTMLPanelElementTest, LayoutObjectIsUnboundedPanel) {
  auto* panel = MakeGarbageCollected<HTMLPanelElement>(GetDocument());
  GetDocument().FirstBodyElement()->AppendChild(panel);

  UpdateAllLifecyclePhasesForTest();

  ASSERT_TRUE(panel->GetLayoutObject());
  EXPECT_STREQ("LayoutUnboundedPanel", panel->GetLayoutObject()->GetName());
}

}  // namespace blink

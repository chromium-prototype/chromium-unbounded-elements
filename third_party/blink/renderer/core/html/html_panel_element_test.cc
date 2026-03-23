// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/html_panel_element.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/html/html_body_element.h"
#include "third_party/blink/renderer/core/html/unbounded_panel_widget.h"
#include "third_party/blink/renderer/core/html_names.h"
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

TEST_F(HTMLPanelElementTest, WidgetLifecycle) {
  auto* panel = MakeGarbageCollected<HTMLPanelElement>(GetDocument());
  EXPECT_FALSE(panel->GetWidgetForTesting());

  GetDocument().FirstBodyElement()->AppendChild(panel);
  EXPECT_TRUE(panel->GetWidgetForTesting());

  panel->remove();
  EXPECT_FALSE(panel->GetWidgetForTesting());
}

TEST_F(HTMLPanelElementTest, SecondaryWidgetIsPainted) {
  auto* panel = MakeGarbageCollected<HTMLPanelElement>(GetDocument());
  panel->setAttribute(html_names::kStyleAttr, AtomicString("width: 100px; height: 100px; background: red;"));
  GetDocument().FirstBodyElement()->AppendChild(panel);
  UpdateAllLifecyclePhasesForTest();

  auto* widget = panel->GetWidgetForTesting();
  ASSERT_TRUE(widget);
  EXPECT_TRUE(widget->HasDisplayItemsForTesting());
}

}  // namespace blink

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "cc/test/pixel_test_utils.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/browser/render_widget_host_iterator.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/mojom/frame/frame.mojom-test-utils.h"
#include "third_party/blink/public/mojom/page/widget.mojom.h"
#include "mojo/public/cpp/test_support/test_utils.h"
#include "content/public/test/browser_test_utils.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "third_party/skia/include/core/SkColor.h"
namespace content {

class HTMLPanelElementBrowserTest : public ContentBrowserTest {
 public:
  HTMLPanelElementBrowserTest() = default;
  ~HTMLPanelElementBrowserTest() override = default;
};

// Intercepts and captures the popup widget created by an Unbounded Panel.
class PanelCreateNewPopupWidgetInterceptor
    : public blink::mojom::LocalFrameHostInterceptorForTesting {
 public:
  explicit PanelCreateNewPopupWidgetInterceptor(RenderFrameHostImpl* render_frame_host)
      : swapped_impl_(
            render_frame_host->local_frame_host_receiver_for_testing(),
            this) {}

  ~PanelCreateNewPopupWidgetInterceptor() override = default;

  LocalFrameHost* GetForwardingInterface() override {
    return swapped_impl_.old_impl();
  }

  void CreateNewPopupWidget(
      mojo::PendingAssociatedReceiver<blink::mojom::PopupWidgetHost> popup_host,
      mojo::PendingAssociatedReceiver<blink::mojom::WidgetHost> widget_host,
      mojo::PendingAssociatedRemote<blink::mojom::Widget> widget) override {
    GetForwardingInterface()->CreateNewPopupWidget(
        std::move(popup_host), std::move(widget_host), std::move(widget));
    if (run_loop_) {
      run_loop_->Quit();
    }
  }

  void Wait() {
    run_loop_ = std::make_unique<base::RunLoop>();
    run_loop_->Run();
  }

 private:
  std::unique_ptr<base::RunLoop> run_loop_;
  [[maybe_unused]] mojo::test::ScopedSwapImplForTesting<blink::mojom::LocalFrameHost>
      swapped_impl_;
};

// Currently EXPECTED to fail!
// Serves as the TDD Red phase for the "Compositing to the Secondary Widget" milestone.
IN_PROC_BROWSER_TEST_F(HTMLPanelElementBrowserTest, RenderWidgetColorIsRed) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url(
      "data:text/html,<!DOCTYPE html>"
      "<body>"
      "<panel id='my_panel' style='width: 100px; height: 100px; background: red;'>"
      "</panel>"
      "</body>");

  auto* contents = static_cast<WebContentsImpl*>(shell()->web_contents());

  EXPECT_TRUE(NavigateToURL(shell(), test_url));
  WaitForLoadStop(contents);

  RenderFrameHostImpl* root_frame_host =
      contents->GetPrimaryFrameTree().root()->current_frame_host();

  // The panel creates the secondary popup widget over IPC.
  // Wait to intercept it.
  PanelCreateNewPopupWidgetInterceptor popup_interceptor(root_frame_host);

  // We need to trigger the panel's initialization which happens via layout updates.
  // Apppending or styling it causes it to init.
  EXPECT_TRUE(ExecJs(root_frame_host,
                     "document.getElementById('my_panel').style.background = 'red';"));

  // The IPC is triggered...
  popup_interceptor.Wait();

  // Now we need to grab the last created active popup from the process.
  // Iterate through all RenderWidgetHosts for the process.
  RenderProcessHost* process = root_frame_host->GetProcess();
  RenderWidgetHostImpl* popup_widget_host = nullptr;
  std::unique_ptr<RenderWidgetHostIterator> widgets(
      RenderWidgetHost::GetRenderWidgetHosts());
  while (RenderWidgetHost* widget = widgets->GetNextHost()) {
    if (widget->GetProcess()->GetID() == process->GetID() &&
        widget != root_frame_host->GetRenderWidgetHost()) {
      popup_widget_host = static_cast<RenderWidgetHostImpl*>(widget);
      break;
    }
  }

  ASSERT_TRUE(popup_widget_host) << "Secondary RenderWidgetHost was not created!";

  RenderWidgetHostViewBase* popup_view = popup_widget_host->GetView();
  ASSERT_TRUE(popup_view) << "Popup did not create a RenderWidgetHostView!";

  // We're looking at the popup View. Since the plumbing is not done, this will
  // likely either timeout or produce a blank/transparent frame instead of our red pixel.
  base::RunLoop copy_run_loop;
  SkBitmap bitmap;
  popup_view->CopyFromSurface(
      gfx::Rect(), gfx::Size(), base::TimeDelta(),
      base::BindLambdaForTesting([&](const content::CopyFromSurfaceResult& result) {
        if (result.has_value()) {
          bitmap = result->bitmap;
        }
        copy_run_loop.Quit();
      }));

  copy_run_loop.Run();

  ASSERT_FALSE(bitmap.empty()) << "Surface returned empty bitmap - pipeline is likely disconnected!";

  // Validate the center pixel color. Expecting RED.
  SkColor color = bitmap.getColor(bitmap.width() / 2, bitmap.height() / 2);
  EXPECT_EQ(color, SK_ColorRED);
}

}  // namespace content

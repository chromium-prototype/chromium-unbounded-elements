// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/task/single_thread_task_runner.h"
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

  void SetUp() override {
    EnablePixelOutput(2.0f);
    ContentBrowserTest::SetUp();
  }
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
    quit_called_ = true;
    if (run_loop_) {
      run_loop_->Quit();
    }
  }

  void Wait() {
    if (quit_called_) return;
    run_loop_ = std::make_unique<base::RunLoop>();
    run_loop_->Run();
  }

 private:
  bool quit_called_ = false;
  std::unique_ptr<base::RunLoop> run_loop_;
  [[maybe_unused]] mojo::test::ScopedSwapImplForTesting<blink::mojom::LocalFrameHost>
      swapped_impl_;
};

// Currently EXPECTED to fail!
// Serves as the TDD Red phase for the "Compositing to the Secondary Widget" milestone.
IN_PROC_BROWSER_TEST_F(HTMLPanelElementBrowserTest, RenderWidgetColorIsBlue) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url(
      "data:text/html,<!DOCTYPE html>"
      "<body>"
      "<panel id='my_panel' style='position: fixed; inset: 0; margin: 0; padding: 0; border: none; width: 100px; height: 100px; background: white;'>"
      "  <div style='width: 10px; height: 10px; background: blue;'></div>"
      "</panel>"
      "</body>");

  auto* contents = static_cast<WebContentsImpl*>(shell()->web_contents());

  // We need to set up the interceptor before navigating!
  // BUT we can't do it before getting the frame host!
  // Wait, the interceptor is for `root_frame_host`.

  EXPECT_TRUE(NavigateToURL(shell(), test_url));
  WaitForLoadStop(contents);

  RenderFrameHostImpl* root_frame_host =
      contents->GetPrimaryFrameTree().root()->current_frame_host();

  // Trigger relayout to fire the IPC again just in case!
  EXPECT_TRUE(ExecJs(root_frame_host, "document.getElementById('my_panel').style.background = 'red';"));
  auto eval_result = EvalJs(root_frame_host, "new Promise(resolve => requestAnimationFrame(resolve));");

  // By the time EvalJs returns, the IPC should have been processed by the browser.

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

  LOG(INFO) << "Popup view bounds at start of loop: " << popup_view->GetViewBounds().ToString();
  LOG(INFO) << "Popup view is visible? " << popup_view->IsShowing();

  // Force WasShown just in case!
  popup_widget_host->WasShown(blink::mojom::RecordContentToVisibleTimeRequestPtr());

  bool has_red = false;
  SkColor color = SK_ColorTRANSPARENT;
  int retry_count = 0;

  base::RunLoop main_run_loop;
  
  auto copy_callback = [&](const content::CopyFromSurfaceResult& result, auto& self) -> void {
    SkBitmap bitmap;
    if (result.has_value()) {
      bitmap = result->bitmap;
    }

    if (!bitmap.empty()) {
      bool found_red = false;
      SkColor first_non_transparent = SK_ColorTRANSPARENT;
      for (int y = 0; y < bitmap.height() && !found_red; ++y) {
        for (int x = 0; x < bitmap.width(); ++x) {
          SkColor c = bitmap.getColor(x, y);
          if (c != SK_ColorTRANSPARENT && first_non_transparent == SK_ColorTRANSPARENT) {
            first_non_transparent = c;
            LOG(INFO) << "Bitmap captured. Non-transparent colored pixel found at " << x << "," << y << " with color: " << std::hex << c;
          }
          if (c != SK_ColorTRANSPARENT) {
            found_red = true;
            color = c;
            has_red = true;
            main_run_loop.Quit();
            return;
          }
        }
      }
      if (!found_red) {
        LOG(INFO) << "Bitmap captured. size: " << bitmap.width() << "x" << bitmap.height() << ", first_non_transparent: " << std::hex << first_non_transparent;
      }
    } else {
      LOG(INFO) << "Bitmap was empty!";
    }

    // Now test MAIN FRAME copy!
    root_frame_host->GetView()->CopyFromSurface(gfx::Rect(), gfx::Size(), base::TimeDelta(), base::BindOnce([](const content::CopyFromSurfaceResult& r) {
      if (r.has_value() && !r->bitmap.empty()) {
        LOG(INFO) << "MAIN FRAME COPY SUCCESS! size: " << r->bitmap.width() << "x" << r->bitmap.height() << " color at 0,0: " << std::hex << r->bitmap.getColor(0,0);
      } else {
        LOG(INFO) << "MAIN FRAME COPY FAILED or EMPTY!";
      }
    }));

    retry_count++;
    if (retry_count >= 100) {
      main_run_loop.Quit();
      return;
    }

    // Try again!
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindLambdaForTesting([&, self]() {
          if (!popup_view->IsSurfaceAvailableForCopy()) {
            // Re-post if surface goes away somehow, but keep retrying.
            self(content::CopyFromSurfaceResult(), self);
            return;
          }
          popup_view->CopyFromSurface(
              gfx::Rect(0, 0, 200, 200), gfx::Size(200, 200), base::TimeDelta(),
              base::BindLambdaForTesting([&, self](const content::CopyFromSurfaceResult& result) {
                self(result, self);
              }));
        }),
        base::Milliseconds(50));
  };

  // Kick off the first copy
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindLambdaForTesting([&]() {
        if (!popup_view->IsSurfaceAvailableForCopy()) {
          copy_callback(content::CopyFromSurfaceResult(), copy_callback);
          return;
        }
        popup_view->CopyFromSurface(
            gfx::Rect(0, 0, 200, 200), gfx::Size(200, 200), base::TimeDelta(),
            base::BindLambdaForTesting([&](const content::CopyFromSurfaceResult& result) {
              copy_callback(result, copy_callback);
            }));
      }));

  main_run_loop.Run();

  EXPECT_EQ(color, SK_ColorBLUE) << "Timed out waiting for the blue pixel in the secondary popup widget.";
}

IN_PROC_BROWSER_TEST_F(HTMLPanelElementBrowserTest, WindowBoundsSync) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url(
      "data:text/html,<!DOCTYPE html>"
      "<body>"
      "<panel id='my_panel' style='position: fixed; inset: 0; margin: 0; padding: 0; border: none; width: 100px; height: 100px; background: white;'>"
      "</panel>"
      "</body>");

  auto* contents = static_cast<WebContentsImpl*>(shell()->web_contents());

  EXPECT_TRUE(NavigateToURL(shell(), test_url));
  WaitForLoadStop(contents);

  RenderFrameHostImpl* root_frame_host =
      contents->GetPrimaryFrameTree().root()->current_frame_host();

  // Find the popup RenderWidgetHost
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

  popup_widget_host->WasShown(blink::mojom::RecordContentToVisibleTimeRequestPtr());

  // Wait a bit for layout / mojo
  base::RunLoop initial_run_loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, initial_run_loop.QuitClosure(), base::Milliseconds(100));
  initial_run_loop.Run();

  // Initial bounds check
  gfx::Rect initial_bounds = popup_view->GetViewBounds();
  EXPECT_EQ(initial_bounds.size(), gfx::Size(100, 100));

  // Change CSS bounds and position
  EXPECT_TRUE(ExecJs(root_frame_host, "document.getElementById('my_panel').style.width = '300px'; "
                                      "document.getElementById('my_panel').style.height = '150px'; "
                                      "document.getElementById('my_panel').style.left = '50px'; "
                                      "document.getElementById('my_panel').style.top = '75px';"));
  
  // Wait for layout updates
  auto eval_result = EvalJs(root_frame_host, "new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));");

  int retries = 0;
  gfx::Point expected_origin(initial_bounds.x() + 50, initial_bounds.y() + 75);
  while ((popup_view->GetViewBounds().size() != gfx::Size(300, 150) ||
          popup_view->GetViewBounds().origin() != expected_origin) && retries < 50) {
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(50));
    run_loop.Run();
    retries++;
  }

  // New bounds check
  gfx::Rect new_bounds = popup_view->GetViewBounds();
  EXPECT_EQ(new_bounds.size(), gfx::Size(300, 150));
  EXPECT_EQ(new_bounds.x() - initial_bounds.x(), 50);
  EXPECT_EQ(new_bounds.y() - initial_bounds.y(), 75);
}

}  // namespace content

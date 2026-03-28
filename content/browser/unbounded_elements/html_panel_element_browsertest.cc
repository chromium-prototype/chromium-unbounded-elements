#include "ui/aura/window.h"
#include "base/threading/platform_thread.h"
#include "base/run_loop.h"
// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include "content/public/browser/render_widget_host_observer.h"
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
      "<panel open id='my_panel' style='position: fixed; inset: 0; margin: 0; padding: 0; border: none; width: 100px; height: 100px; background: white;'>"
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


IN_PROC_BROWSER_TEST_F(HTMLPanelElementBrowserTest, DISABLED_PanelTextVisibleWithInput) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url(
      "data:text/html,<!DOCTYPE html>"
      "<body style='margin:0;'>"
      "<panel open id='my_panel' style='position: fixed; top: 400px; left: 400px; width: 200px; height: 300px; background: white; margin: 0; padding: 0; border: none;'>"
      "  <input type='text' style='position: absolute; top: 0; left: 0; width: 50px; height: 50px; background: green; border: none;' />"
      "   <p style='position: absolute; top: 10px; left: 100px; margin: 0; width: 50px; height: 50px; background: blue; color: transparent;'>Text!</p>"
      "</panel>"
      "</body>");

  auto* contents = static_cast<WebContentsImpl*>(shell()->web_contents());
  EXPECT_TRUE(NavigateToURL(shell(), test_url));
  WaitForLoadStop(contents);

  RenderFrameHostImpl* root_frame_host =
      contents->GetPrimaryFrameTree().root()->current_frame_host();
  
  auto eval_result = EvalJs(root_frame_host, "new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));");

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

  ASSERT_TRUE(popup_widget_host);
  auto* popup_view = popup_widget_host->GetView();
  ASSERT_TRUE(popup_view);
  popup_widget_host->WasShown(blink::mojom::RecordContentToVisibleTimeRequestPtr());

  bool has_green = false;
  bool has_blue = false;

  int retries = 0;
  while (retries < 50) {
    if (has_green && has_blue) break;
    base::RunLoop copy_loop;
    popup_view->CopyFromSurface(
        gfx::Rect(), gfx::Size(), base::TimeDelta(),
        base::BindOnce(
            [](bool* has_green, bool* has_blue, base::OnceClosure quit_closure,
               const content::CopyFromSurfaceResult& result) {
              if (result.has_value()) {
                const SkBitmap& bitmap = result->bitmap;
                for (int y = 0; y < bitmap.height(); ++y) {
                  for (int x = 0; x < bitmap.width(); ++x) {
                    SkColor c = bitmap.getColor(x, y);
                    // Match Skia green and blue values
                    if (c == 0xFF008000 || c == SK_ColorGREEN) *has_green = true;
                    if (c == 0xFF0000FF || c == SK_ColorBLUE) *has_blue = true;
                  }
                }
              }
              std::move(quit_closure).Run();
            },
            &has_green, &has_blue, copy_loop.QuitClosure()));
    copy_loop.Run();
    
    if (has_green && has_blue) break;
    retries++;
    base::RunLoop wait_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, wait_loop.QuitClosure(), base::Milliseconds(50));
    wait_loop.Run();
  }

  // TODO(crbug.com/NNNNNN): Elements positioned inside an UnboundedPanel 
  // that do not have their own composited layer are currently not painted 
  // correctly when the panel is positioned outside of `CullRect::Infinite()` 
  // or has its offset translated. This includes both `<input>` and `<p>` tags 
  // when the panel is at e.g. (400, 400).
  EXPECT_TRUE(has_green) << "Input layer was not painted!";
  EXPECT_TRUE(has_blue) << "P layer was not painted because of mapping bugs!";
}

IN_PROC_BROWSER_TEST_F(HTMLPanelElementBrowserTest, WindowBoundsSync) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url(
      "data:text/html,<!DOCTYPE html>"
      "<body>"
      "<panel open id='my_panel' style='position: fixed; inset: 0; margin: 0; padding: 0; border: none; width: 100px; height: 100px; background: white;'>"
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

IN_PROC_BROWSER_TEST_F(HTMLPanelElementBrowserTest, InputEventRouting) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url(
      "data:text/html,<!DOCTYPE html>"
      "<body>"
      "<panel open id='my_panel' style='position: fixed; inset: 0; margin: 0; padding: 0; border: none; width: 100px; height: 100px; background: white;'>"
      "</panel>"
      "<script>"
      "  window.clicks = 0;"
      "  document.getElementById('my_panel').addEventListener('mousedown', (e) => {"
      "    console.log('MOUSEDOWN in panel at ', e.clientX, e.clientY);"
      "    window.clicks++;"
      "  });"
      "</script>"
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

  // Change CSS bounds and position
  EXPECT_TRUE(ExecJs(root_frame_host, "document.getElementById('my_panel').style.left = '50px'; "
                                      "document.getElementById('my_panel').style.top = '75px';"));
  
  // Wait for layout updates
  auto eval_result = EvalJs(root_frame_host, "new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));");

  // Send a mouse down event into the secondary widget at local (10, 10)
  blink::WebMouseEvent mouse_down(
      blink::WebInputEvent::Type::kMouseDown,
      blink::WebInputEvent::kNoModifiers,
      base::TimeTicks::Now());
  mouse_down.button = blink::WebPointerProperties::Button::kLeft;
  mouse_down.click_count = 1;
  mouse_down.SetPositionInWidget(10, 10);
  mouse_down.SetPositionInScreen(60, 85); // 50 + 10, 75 + 10
  popup_widget_host->ForwardMouseEvent(mouse_down);

  blink::WebMouseEvent mouse_up(
      blink::WebInputEvent::Type::kMouseUp,
      blink::WebInputEvent::kNoModifiers,
      base::TimeTicks::Now());
  mouse_up.button = blink::WebPointerProperties::Button::kLeft;
  mouse_up.click_count = 1;
  mouse_up.SetPositionInWidget(10, 10);
  mouse_up.SetPositionInScreen(60, 85);
  popup_widget_host->ForwardMouseEvent(mouse_up);

  // Read clicks using EvalJs
  int clicks = EvalJs(root_frame_host, "window.clicks").ExtractInt();
  // We expect the routing to have triggered the javascript event listener.
  EXPECT_EQ(clicks, 1);
}

IN_PROC_BROWSER_TEST_F(HTMLPanelElementBrowserTest, FocusAndActivationRouting) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url(
      "data:text/html,<!DOCTYPE html>"
      "<body>"
      "<panel open id='my_panel' style='position: fixed; inset: 0; margin: 0; padding: 0; border: none; width: 200px; height: 200px; background: white;'>"
      "  <input id='my_input' type='text' style='position: absolute; left: 10px; top: 10px; width: 100px; height: 20px;' />"
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
  auto eval_result = EvalJs(root_frame_host, "new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));");

  // Simulate the OS focusing the secondary window and blurring the main window
  auto* main_rwh = static_cast<RenderWidgetHostImpl*>(root_frame_host->GetRenderWidgetHost());
  main_rwh->Blur();
  main_rwh->SetActive(false);
  
  popup_widget_host->Focus();

  // Send a mouse down event directly onto the input field. 
  // It is located at (10, 10). Let's click at local (20, 20).
  blink::WebMouseEvent mouse_down(
      blink::WebInputEvent::Type::kMouseDown,
      blink::WebInputEvent::kNoModifiers,
      base::TimeTicks::Now());
  mouse_down.button = blink::WebPointerProperties::Button::kLeft;
  mouse_down.click_count = 1;
  mouse_down.SetPositionInWidget(20, 20);
  mouse_down.SetPositionInScreen(20, 20);
  popup_widget_host->ForwardMouseEvent(mouse_down);

  blink::WebMouseEvent mouse_up(
      blink::WebInputEvent::Type::kMouseUp,
      blink::WebInputEvent::kNoModifiers,
      base::TimeTicks::Now());
  mouse_up.button = blink::WebPointerProperties::Button::kLeft;
  mouse_up.click_count = 1;
  mouse_up.SetPositionInWidget(20, 20);
  mouse_up.SetPositionInScreen(20, 20);
  popup_widget_host->ForwardMouseEvent(mouse_up);

  // Check if activeElement is our input.
  bool is_focused = EvalJs(root_frame_host, "document.activeElement === document.getElementById('my_input')").ExtractBool();
  EXPECT_TRUE(is_focused) << "The input element did not receive focus!";
  
  // Also check if the window/document has focus.
  bool has_focus = EvalJs(root_frame_host, "document.hasFocus()").ExtractBool();
  EXPECT_TRUE(has_focus) << "The document does not believe it has focus after clicking the panel!";
}

IN_PROC_BROWSER_TEST_F(HTMLPanelElementBrowserTest, OutsideClickEvent) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url(
      "data:text/html,<!DOCTYPE html>"
      "<body>"
      "<panel open id='my_panel' style='position: fixed; inset: 0; margin: 0; padding: 0; border: none; width: 200px; height: 200px; background: white;'>"
      "</panel>"
      "<script>"
      "  window.outsideClicks = 0;"
      "  document.getElementById('my_panel').addEventListener('outsideclick', () => {"
      "    window.outsideClicks++;"
      "  });"
      "</script>"
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

  popup_widget_host->WasShown(blink::mojom::RecordContentToVisibleTimeRequestPtr());

  auto eval_result = EvalJs(root_frame_host, "new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));");

  // Trigger focus loss (which should trigger outsideclick event).
  popup_widget_host->GetWidgetInputHandler()->SetFocus(blink::mojom::FocusState::kNotFocusedAndNotActive);
  
  // Yield for Mojo run loop
  {
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(100));
    run_loop.Run();
  }

  int retries = 0;
  int clicks = 0;
  while (retries < 50) {
    clicks = EvalJs(root_frame_host, "window.outsideClicks").ExtractInt();
    if (clicks > 0) break;
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(50));
    run_loop.Run();
    retries++;
  }
  
  EXPECT_EQ(clicks, 1);
}


IN_PROC_BROWSER_TEST_F(HTMLPanelElementBrowserTest, DismissOnBlurBehavior) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url(
      "data:text/html,<!DOCTYPE html>"
      "<body>"
      "<panel open dismiss-on-blur id='my_panel' style='position: fixed; inset: 0; margin: 0; padding: 0; border: none; width: 200px; height: 200px; background: white;'>"
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

  popup_widget_host->WasShown(blink::mojom::RecordContentToVisibleTimeRequestPtr());

  auto eval_result = EvalJs(root_frame_host, "new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));");

  // Verify the panel is initially open
  bool is_open = EvalJs(root_frame_host, "document.getElementById('my_panel').hasAttribute('open')").ExtractBool();
  EXPECT_TRUE(is_open);

  // Trigger focus loss (which should trigger outsideclick event).
  popup_widget_host->GetWidgetInputHandler()->SetFocus(blink::mojom::FocusState::kNotFocusedAndNotActive);
  
  // Yield for Mojo run loop
  {
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(200));
    run_loop.Run();
  }

  // Verify the panel dismissed itself
  int retries = 0;
  is_open = true;
  while (retries < 50) {
    is_open = EvalJs(root_frame_host, "document.getElementById('my_panel').hasAttribute('open')").ExtractBool();
    if (!is_open) break;
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(50));
    run_loop.Run();
    retries++;
  }
  
  EXPECT_FALSE(is_open) << "Panel failed to dismiss on blur!";
}

IN_PROC_BROWSER_TEST_F(HTMLPanelElementBrowserTest, MouseCaptureStateSync) {
  EXPECT_TRUE(NavigateToURL(
      shell(),
      GURL("data:text/html,<!DOCTYPE html><html><body></body></html>")));


  WebContentsImpl* contents = static_cast<WebContentsImpl*>(shell()->web_contents());
  WaitForLoadStop(contents);

  RenderFrameHostImpl* root_frame_host =
      contents->GetPrimaryFrameTree().root()->current_frame_host();

  // Create panel WITH capture attribute initially
  EXPECT_TRUE(ExecJs(root_frame_host,
                     "document.body.innerHTML = `" 
                     "<panel open id=\"capture_panel\" capture style=\"position: fixed; width: 10px; height: 10px;\"></panel>`;"));

  // Find the popup RenderWidgetHost
  RenderProcessHost* process = root_frame_host->GetProcess();
  RenderWidgetHostImpl* popup_widget_host = nullptr;
  while (!popup_widget_host) {
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(50));
    run_loop.Run();
    std::unique_ptr<RenderWidgetHostIterator> widgets(
        RenderWidgetHost::GetRenderWidgetHosts());
    while (RenderWidgetHost* widget = widgets->GetNextHost()) {
      if (widget->GetProcess()->GetID() == process->GetID() &&
          widget != root_frame_host->GetRenderWidgetHost()) {
        popup_widget_host = static_cast<RenderWidgetHostImpl*>(widget);
        break;
      }
    }
  }

  ASSERT_TRUE(popup_widget_host);



  RenderWidgetHostView* popup_view = nullptr;
  while (!popup_view || !popup_view->GetNativeView()) {
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(50));
    run_loop.Run();
    popup_view = popup_widget_host->GetView();
  }

  
  // Verify initial state
  EXPECT_TRUE(popup_view->GetNativeView()->HasCapture());

  // Dynamically remove attribute
  EXPECT_TRUE(ExecJs(root_frame_host,
                     "document.getElementById('capture_panel').removeAttribute('capture');"));

  // Verify dynamic update removed capture
  // We need to wait for IPC to arrive (RenderWidgetHostImpl::SetPopupCapture is synchronous locally once it arrives).
  // We can just use base::RunLoop().RunUntilIdle() to flush the Mojo pipe since both are on the UI thread.
  auto wait_capture = [&](bool expected) {
    while (popup_view->GetNativeView()->HasCapture() != expected) {
      base::RunLoop().RunUntilIdle();
      base::PlatformThread::Sleep(base::Milliseconds(10));
    }
  };
  
  wait_capture(false);
  EXPECT_FALSE(popup_view->GetNativeView()->HasCapture());

  
  // Dynamically add attribute
  EXPECT_TRUE(ExecJs(root_frame_host,
                     "document.getElementById('capture_panel').setAttribute('capture', '');"));
  
  wait_capture(true);
  EXPECT_TRUE(popup_view->GetNativeView()->HasCapture());
}

class WidgetDestroyedObserver : public RenderWidgetHostObserver {
 public:
  explicit WidgetDestroyedObserver(RenderWidgetHost* host) : host_(host) {
    host_->AddObserver(this);
  }
  ~WidgetDestroyedObserver() override {
    if (host_) host_->RemoveObserver(this);
  }
  void RenderWidgetHostDestroyed(RenderWidgetHost* host) override {
    host_->RemoveObserver(this);
    host_ = nullptr;
    destroyed_ = true;
    if (run_loop_) run_loop_->Quit();
  }
  void Wait() {
    if (destroyed_) return;
    run_loop_ = std::make_unique<base::RunLoop>();
    run_loop_->Run();
  }
  
  bool destroyed_ = false;
 private:
  raw_ptr<RenderWidgetHost> host_;
  std::unique_ptr<base::RunLoop> run_loop_;
};

IN_PROC_BROWSER_TEST_F(HTMLPanelElementBrowserTest, VisibilityStateHidesPanel) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL test_url(
      "data:text/html,<!DOCTYPE html>"
      "<body>"
      "<panel open id='my_panel' style='position: fixed; inset: 0; margin: 0; padding: 0; border: none; width: 200px; height: 200px; background: white;'>"
      "</panel>"
      "</body>");

  auto* contents = static_cast<WebContentsImpl*>(shell()->web_contents());

  EXPECT_TRUE(NavigateToURL(shell(), test_url));
  WaitForLoadStop(contents);

  RenderFrameHostImpl* root_frame_host =
      contents->GetPrimaryFrameTree().root()->current_frame_host();

  // Find the popup RenderWidgetHost
  auto get_popup_widget = [&]() -> RenderWidgetHostImpl* {
    RenderProcessHost* process = root_frame_host->GetProcess();
    std::unique_ptr<RenderWidgetHostIterator> widgets(
        RenderWidgetHost::GetRenderWidgetHosts());
    while (RenderWidgetHost* widget = widgets->GetNextHost()) {
      if (widget->GetProcess()->GetID() == process->GetID() &&
          widget != root_frame_host->GetRenderWidgetHost()) {
        return static_cast<RenderWidgetHostImpl*>(widget);
      }
    }
    return nullptr;
  };

  RenderWidgetHostImpl* popup_widget_host = nullptr;
  while (!(popup_widget_host = get_popup_widget())) {
    base::RunLoop run_loop;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(50));
    run_loop.Run();
  }

  ASSERT_TRUE(popup_widget_host) << "Secondary RenderWidgetHost was not created!";

  WidgetDestroyedObserver observer(popup_widget_host);

  // Hide the WebContents
  contents->WasHidden();

  // It should be destroyed by IPC processing
  observer.Wait();

  EXPECT_TRUE(observer.destroyed_) << "Secondary RenderWidgetHost was not destroyed on hide!";
}

}  // namespace content

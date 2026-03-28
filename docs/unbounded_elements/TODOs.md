# Unbounded Elements: TODOs

## Milestone 1: DOM & Layout Isolation (The "Hole Punch")

### DOM & Layout Object Creation
- [x] **Define the HTML Element**: 
  - [x] Add the `<panel>` tag to `third_party/blink/renderer/core/html/html_tag_names.json5`.
  - [x] Implement `HTMLPanelElement` C++ class (inheriting from `HTMLElement`).
  - [x] Create `html_panel_element.idl` to expose the constructor to JS.
  - [x] Register the new C++ and IDL files in the build system (`html/build.gni`, `bindings/idl_in_core.gni`, and `bindings/generated_in_core.gni`).
- [x] **Create `LayoutUnboundedPanel`**:
  - [x] Implement `LayoutUnboundedPanel` class in `third_party/blink/renderer/core/layout/` (likely inheriting from `LayoutBlockFlow`).
  - [x] Wire up `HTMLPanelElement::CreateLayoutObject` to instantiate `LayoutUnboundedPanel` instead of a standard layout block.

### Layout Isolation
- [x] **Independent Formatting Context**:
  - [x] Ensure `LayoutUnboundedPanel` acts as an independent formatting context.
  - [x] Force the element to be out-of-flow (similar to `position: fixed` or Top Layer elements) so its physical dimensions do not expand the main document's scrollbars or affect sibling flow.

### Paint Isolation (The "Hole Punch")
- [x] **Modify Pre-Paint Phase**:
  - [x] Update `PrePaintTreeWalk` to recognize `LayoutUnboundedPanel` (`HTMLPanelElement`). 
  - [x] Ensure it doesn't emit standard paint properties that would force it into the main window's compositing layers.
- [x] **Modify Paint Phase**:
  - [x] Update `PaintLayerPainter` (or relevant `ObjectPainter`) to check if the current `LayoutObject` is an `HTMLPanelElement`.
  - [x] If painting the main window, explicitly **skip** recording paint operations for the panel and all of its descendants.
- [x] **Testing**:
  - [x] Write a web test (WPT or Blink internal) that inserts a `<panel>` with text and bright background colors, asserting that it does not visually render on the page and does not affect the page's scrollable area.

## Milestone 2: Plumb the Secondary Widget

### Widget Creation & IPC
- [x] **Cross-Process Widget Architecture**:
  - [x] Re-use the existing browser-process IPC (`blink.mojom.LocalFrameHost.CreateNewPopupWidget`) allowing the renderer to request a secondary, unparented popup/desktop widget from the browser process for the `<panel>`.
  - [x] Implement the renderer-side wrapper (`UnboundedPanelWidget`) to hold the compositor and Mojo interfaces.
  - [x] Bind the `<panel>` element's lifecycle (insertion/removal from DOM) to the creation and destruction of this widget.

## Milestone 3: The Paint Split

### Sub-Tree Compositor & Paint Walk
- [x] **Secondary PaintController**:
  - [x] Modify the main thread's `LocalFrameView::PaintTree` (or equivalent) to trigger a **second** targeted Paint walk.
  - [x] Initialize a new `PaintController` dedicated to the secondary `UnboundedPanelWidget` (or independent `LayerTreeHost`).
  - [x] Instruct this secondary `PaintController` to begin its tree traversal at the `LayoutUnboundedPanel` via `PaintFlag::kPaintingUnboundedPanel`.
- [x] **Coordinate Translation**:
  - [x] Mathematically shift paint offsets such that the `<panel>`'s top-left corner in the main document's layout is translated to `(0, 0)` within the secondary compositor's coordinate space.
  - [x] Ensure that hit-testing coordinates on the secondary widget are correctly inversely translated back to the main document.

### Compositing to the Secondary Widget
- [x] Plumb the "Secondary Widget" over Mojo via `CreateNewPopupWidget`.
- [x] Integrate Paint Isolation (`kTreeWidget` property nodes) for the Custom Element.
- [x] Enable Compositing to the Secondary Widget (`PaintArtifactCompositor` mapping).
- [ ] Connect hit testing and routing of input events to the "Secondary Widget".
- [x] Implement resize bounds synchronization.
- [x] **Window Positioning**: Translate logical layout coordinates to physical OS screen coordinates using `LocalFrameView::FrameToScreen()`, ensuring the OS popup shifts dynamically to match the element's CSS layout position.

## Milestone 3 Phase 2: Full Paint Walk Integration
- [x] **Full DOM Subtree Rendering**:
  - [x] Instead of a mock `FillRect`, properly execute `LayoutObject::Paint()` on the `LayoutUnboundedPanel` and its descendants.
  - [x] Manage caching correctly via `DisplayItemClient` lifecycle across multiple `PaintController`s.
  - [x] Ensure full `PaintPropertyTreeBuilder` integration for clip and transform nodes so child elements paint relative to the new sub-tree root.
- [x] **Dynamic Window Sizing & Bounds Synchronization**:
  - [x] Extract the calculated physical dimensions from `LayoutUnboundedPanel` (`owner_element_->GetLayoutBox()->PhysicalBorderBoxRect()`) during or after layout updates.
  - [x] Remove hardcoded `200x200` logical `ShowPopup` dimensions and `400x400` `cc::LayerTreeHost` physical surface bounds from initialization.
  - [x] Implement an IPC dispatch mechanism (e.g., `SetBounds`) to synchronously resize the secondary OS window to perfectly match the CSS dimensions applied to the `<panel>` element.
  - [x] Implement Window Positioning by calling `LocalFrameView::FrameToScreen()` on the element's layout rect, translating its logical coordinates into absolute OS screen dimensions before pushing the Mojo `SetBounds` IPC.

## Milestone 4: Event Routing & Input Hit Testing
- [x] **Event Interception & Translation**:
  - [x] Implement `UnboundedPanelWidget::HandleInputEvent` to intercept UI events (mouse, touch, keyboard).
  - [x] Apply the inverse layout translation to hit-testing coordinates so physical window dimensions map into the logical layout coordinates of the main document.
  - [x] Inject the translated event into the main document's `EventHandler` to fire DOM events (e.g., `click`).
- [x] **Capture & Outside Click Management API**:
  - [x] Introduce a notification mechanism (e.g., a DOM event like `outsideclick`) dispatched to the `<panel>` when it loses focus.
  - [x] Support a declarative "dismiss-on-blur" attribute/behavior for simple use cases that don't require sophisticated JS control.
  - [x] Implement an opt-in API (HTML attribute `capture`) for <panel> to request pointers (similar to OS-level menus), as not all panels need to trap focus/outside clicks.

## Milestone 5: Windowing & UX Polish
- [x] **Positioning Offset Issue**: Fixed. Added DocumentToFrame() and verified CSS behavior.
- [ ] **DevTools Overlay Mapping**: When hovering over panel elements in DevTools, the inspector overlay highlights the physical region inside the parent document. Coordinate translation must be plumbed into the DevTools overlay renderer.
- [x] **Window Drag Synchronization**: Fixed. When dragging the main browser window, or moving it programmatically, the popup natively intercepts `UpdateScreenRects` on the child frame to synchronously re-emit Mojo bounds IPCs. Decoupling this from layout/lifecycle updates resolved all lag.
- [ ] **Z-Order Independence**: The popup behaves as an "always-on-top" window and obscures other independent OS applications (like the terminal). We need to decouple the widget from standard `WidgetType::kPopup` top-most Z-order logic so it interleaves normally.
- [x] **Cursor Mapping**: Fixed. The mouse cursor does not change state when hovering over interactive elements (e.g., `<input>`, text selection). We need to plumb cursor change requests from the main `WebFrameWidgetImpl` back to the secondary widget's `WidgetBase` so the OS receives the updated cursor shape.
- [x] **Focus, Activation, and IME**: Clicking an `<input>` element correctly selects text but does not focus it, and the blinking caret does not appear. We need to investigate Chromium's focus transfer logic to ensure the secondary window correctly signals `Activation` to the main window's node, properly establishing focus state for IME input.
- [x] **Page Visibility & Tab Switching**: Sub-widgets remain visible on the OS desktop even when the parent tab is backgrounded or switched. We must tie the unbounded `<panel>`'s lifecycle directly to the host `WebContents` visibility state to hide it when the user switches tabs.
## Current Status
- **Fixed IPC Segfaults & FrameSink Loops**: `WidgetBase::RequestNewLayerTreeFrameSink` was previously looping infinitely due to invalid `LocalSurfaceId` allocation before IPC acknowledgement. We initially tried to defer `InitializeCompositing`, which caused an input handler crash. Now, we correctly initialize compositing upfront but defer visibility in `OnShowPopupAcknowledged`.
- **Browser-Side Widget Destruction**: We discovered that the test was failing and logging `CreateFrameSink called` infinitely because the browser process (`WebContentsImpl::ShowCreatedWidget`) was dropping the requested widget because `blink::features::kBlockSelectPopupUnfocusedWindow` was true, and the test's virtual window was not explicitly active.
- **The Native Fix**: We adopted `WebPagePopupImpl`'s lifecycle model and disabled `kBlockSelectPopupUnfocusedWindow` in our browser test environment to allow headless window creation. The widget initialization handshake completes cleanly, logging NO errors, and the new widget successfully receives its FrameSink!
- **Coordinate Translation**: Implemented a completely safe coordinate mathematical translation inside the `cc` engine. By hooking into `PaintArtifactCompositor` outputs and dynamically modifying `offset_to_transform_parent` of the resulting `cc::Layer`s, we map absolute layout bounds directly to the secondary `WidgetBase` OS popup window `[0,0]` origin without causing DCHECKS or compositor fatal crashes.
- **Headless Unbounded Verification**: We introduced a sophisticated native X11 pipeline (`Xvfb` + `fluxbox` + `ImageMagick`) wrapped by Puppeteer to natively guarantee that Unbounded Panel elements safely spawn, paint, scale, and render their contents entirely out of the bounds of the host browser window constraints. 
  - **Test HTML**: `third_party/blink/web_tests/unbounded-elements/panel-paint-sophisticated.html`
  - **Puppeteer Script**: `run_sophisticated_test.cjs` (Executes the browser and captures the X11 screen)
  - **Execution Wrapper**: `run_sophisticated_test_wrapper.sh`
  - **How to Run**: Simply execute `./run_sophisticated_test_wrapper.sh` in the root repository. The script orchestrates `Xvfb` and `fluxbox`, generates `sophisticated_test_result.png`, and automatically tears down the display server upon termination.
- **Bounds Synchronization and High DPI (DSF)**: Addressed flakiness in the `HTMLPanelElementBrowserTest.WindowBoundsSync` test when scaling. The test loop was exiting prematurely upon receiving an unscaled transient coordinate from `DesktopWindowTreeHostX11`'s asynchronous `ConfigureNotify`. By adapting the test explicitly to wait for the proper logical scaled bounds instead of generic "bounds changed" condition, tests perfectly pass and correctly synchronize logical CSS coordinates up to physical screen coordinates.

- **Input Event Routing**: Successfully plumbed UI events from the secondary widget back into the main document's event handler. Using `WebCoalescedInputEvent` translation, mouse and touch events coordinates are geometrically shifted by the absolute layout box offsets of the panel. These modified events are directly injected into `WebFrameWidgetImpl::HandleInputEvent` of the parent frame, seamlessly hooking directly into the DOM event dispatch pipeline (`click`, `mousedown`, `mousemove`). A new integration test `HTMLPanelElementBrowserTest.InputEventRouting` proves this.
- **Focus, Activation, and IME**: Clicking an `<input>` element within an unbounded panel now correctly receives keyboard focus and displays a blinking caret. When the OS natively focuses the secondary popup window (or when user interacts with it), it suppresses global document activation. By intercepting interaction inputs inside `UnboundedPanelWidget::HandleInputEvent` and `UnboundedPanelWidget::FocusChanged`, we now purposefully coerce the `FocusController` of the main `Page` back into an active state. Verified via `HTMLPanelElementBrowserTest.FocusAndActivationRouting`.

- **Cursor Mapping**: Fixed cursor handling by modifying `WebFrameWidgetImpl::DidChangeCursor` to broadcast local cursor changes to all `UnboundedPanels()` registered in the Document. This ensures that when the mouse enters interactive bounds within the `<panel>` widget, the secondary OS window receives the proper `SetCursor` updates.

- **Next steps**:
  - [x] Implement Page Visibility & Tab Switching: hide Unbounded panels when the browser tab is backgrounded.
  - [x] Implement Window Drag Synchronization to fix lag during main window drags. Included programmatic movement via native UpdateScreenRects interception.
  - [ ] **Follow-up: Native Child Window Refactor**: The current Window Drag Synchronization fix relies on synchronous Mojo IPCs (`SetPopupBounds`) when the browser window moves. The root issue is that the popup is not a true child window of the browser window at the OS/WM level. If we rebuild the popup to be a native child window, it will automatically follow the parent window, and we can strip out the manual `UpdateScreenRects` synchronization logic.
  - [x] Fix popup DSF at 2x screen scaling: fixed by using physical pixels (`absolute_rect`) in `UnboundedPanelWidget::UpdateLifecycle` for the root layer bounds instead of logical DIPs, normalizing the cc::Layer scale multiplier correctly against `device_scale_factor`.
  - [x] Fix text selection cursor: when mousedown and moving to select text, the incorrect hit-test resulting from the bad 2x visual artifact was completely bypassing the `SelectCursor` text node hit evaluation. Fixing the DSF bounds also correctly resynced hit-testing coordinates symmetrically, meaning the I-beam caret is explicitly mapped during standard text selections.
  - [x] **Fix Panel Child Rendering Offsets**: Implemented Property Tree State Injection directly into the secondary popup's `PaintController` using `FirstFragment().LocalBorderBoxProperties()`. We also moved the global coordinate translation out of `kSecondaryRootPropertyNodeId` modification (which broke elements that spawn their own compositor layers) and instead now safely iterate over the `cc::Layers` emitted by `PaintArtifactCompositor` to mutate their `offset_to_transform_parent` before passing the FrameSink trees over IPC. The `PanelTextVisibleWithInput` integration test now passes stably.
  - [x] Fix caret in `<input>`: hooked `UnboundedPanelWidget::SetNeedsCommit()` into the end of `WebFrameWidgetImpl::UpdateLifecycle()`. This enables the `Page`'s caret blink timer invalidations to transparently pass down to the secondary composited OS window popup correctly.

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
- [ ] **Event Interception & Translation**:
  - [ ] Implement `UnboundedPanelWidget::HandleInputEvent` to intercept UI events (mouse, touch, keyboard).
  - [ ] Apply the inverse layout translation to hit-testing coordinates so physical window dimensions map into the logical layout coordinates of the main document.
  - [ ] Inject the translated event into the main document's `EventHandler` to fire DOM events (e.g., `click`).

## Current Blocker
- **Infinite Loop in Frame Sink Allocation**: `WidgetBase::RequestNewLayerTreeFrameSink` is repeatedly calling `UnboundedPanelWidget::AllocateNewLayerTreeFrameSink`. The secondary compositor (`AsyncLayerTreeFrameSink`) drops the connection and fails to initialize because the IPC to create the frame sink (`WidgetHost::CreateFrameSink`) does not reach the browser successfully in time, or drops due to duplicate `FrameSinkId` requests. The main suspect is the asynchronous nature of `CreateNewPopupWidget` IPC competing with the immediate synchronous `LayerTreeHost` initialization in the `UnboundedPanelWidget::Initialize()` constructor.
- **Next steps to investigate**:
  - Test moving `widget_base_->InitializeCompositing` inside a Mojo callback for `CreateNewPopupWidget`, or deferring `LayerTreeHost` creation until the `WidgetHost` Remote is confirmed attached and processed.
  - Validate that the `WidgetBase` in `UnboundedPanelWidget` does not attempt to create the `CompositorFrameSink` on the main page's RenderWidgetHostImpl.


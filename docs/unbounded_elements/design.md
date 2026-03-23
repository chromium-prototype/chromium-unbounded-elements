# Unbounded Elements: Sub-Tree Compositor Split Design

**Status**: Exploratory Draft
**Last Updated**: March 2026

*Disclaimer: This document outlines an exploratory architectural direction for "Unbounded Elements" in Chromium (e.g., an HTML `<panel>` or native-style popup). The rendering pipeline changes proposed here break fundamental invariants in Blink. As such, the actual design and implementation plan will likely need significant adjustments as new findings are discovered during prototyping.*

---

## 1. Objective

Provide a web primitive (e.g., `<panel>`) that exists within a document's regular DOM hierarchy and Layout Tree, but is capable of breaking out of the physical boundaries of the OS window (the browser tab). 

Crucially, this element must **not** require duplicating DOM nodes into a secondary `Document` or `Page` (unlike existing HTML-based form control popups like `<input type="date">` or `<select>`). The web developer must be able to seamlessly interact with the popup's nodes as if they were standard elements in the opener's DOM.

## 2. Background

Chromium currently offers two primary ways to create floating/popup content:
1.  **The Top Layer (e.g., `<dialog>`, `popover`)**: The element is hoisted in the layout tree and paints independently of its DOM parent, avoiding `z-index` and clipping issues. However, it is strictly bound by the physical dimensions of the browser viewport.
2.  **`PagePopup` (e.g., desktop `<select>`)**: Chromium spawns a completely new `Page`, `LocalFrame`, and `Document` in the same renderer process, backed by a new OS window via IPC. To make it work, the C++ layer injects a standalone HTML/JS/CSS bundle and synchronizes state manually.

**The Problem**: If a developer wants a true OS-level popup that they can manipulate directly via the main document's DOM, neither approach works. We must architect a way for a single `Document` to paint into *multiple* OS windows.

## 3. Proposed Architecture: "Sub-Tree Compositor Split"

The core of this design breaks the long-standing Blink invariant that `1 LocalFrameView` == `1 cc::LayerTreeHost` (Compositor) == `1 OS Window`.

Instead, we propose a decoupled architecture:
*   **1 Shared DOM Tree**
*   **1 Shared Layout Tree**
*   **N Paint & Compositing Trees** (One for the main window, one for each unbounded `<panel>`)

When an unbounded `<panel>` is shown, Blink will spin up a secondary `cc::LayerTreeHost` tied to a new OS window (via a `WidgetBase`), but will *not* create a new `LocalFrame`. The main thread's lifecycle will coordinate style and layout for both windows simultaneously, but will execute a bifurcated Paint phase.

## 4. Implementation Milestones

### Milestone 1: DOM & Layout Isolation (The "Hole Punch")
Before we can paint to a secondary window, we must stop the main window from painting the unbounded element.
*   Introduce a new layout object, e.g., `LayoutUnboundedPanel`.
*   Establish an independent formatting context so the panel's dimensions do not affect the main document's scrollbars or layout flow.
*   Modify `PrePaintTreeWalk` and `PaintLayerPainter`. When the main document's `PaintController` walks the tree, it must explicitly **skip** any `LayoutUnboundedPanel` and its descendants. Visually, the panel becomes "invisible" to the main window.

### Milestone 2: Plumb the Secondary Widget
We need an OS window to paint into without spinning up a new `LocalFrame`.
*   Create a new primitive (e.g., `UnboundedPanelWidget`) that directly owns a `WidgetBase` and a secondary `cc::LayerTreeHost`.
*   When the `<panel>` is appended to the DOM (`InsertedInto`), invoke the existing browser-process IPC (`blink.mojom.LocalFrameHost.CreateNewPopupWidget`) to request a native OS window and `WidgetBase::InitializeCompositing()`.
*   Bind the Mojo endpoints to the `UnboundedPanelWidget`.

### Milestone 3: The Paint Split
Feed the DOM subtree's pixels into the secondary widget.
*   Modify the main thread's unified `UpdateAllLifecyclePhases`. After the main document finishes its Paint phase, trigger a *second* targeted Paint walk for the unbounded elements.
*   This second walk uses a new `PaintController` associated with the `UnboundedPanelWidget`. It begins its traversal exactly at the `LayoutUnboundedPanel`.
*   **Property Tree State Injection**: To prevent `FATAL` property tree index checks in `cc::LayerTreeHost`, the new `PaintController` cannot start from uninitialized `PropertyTreeState::Root()`. It must properly inherit the DOM-generated physical property mappings via `layout_object->FirstFragment().LocalBorderBoxProperties()`. This maps main document style coordinates directly into valid native compositor properties for the secondary widget.
*   Implement coordinate translation: Paint offsets must be mathematically shifted so that the panel's top-left in the DOM layout translates to `(0, 0)` in the secondary compositor's coordinate space. This is achieved by setting a transformation offset on the secondary `LayerTreeHost`s root layer bounds.
*   **A `HTMLPanelElementBrowserTest` has been created under `content/browser/unbounded_elements/`** to verify that pixels generated by the secondary `PaintController` correctly plumb to the popup's OS window surface using `WidgetBase` and `PaintArtifactCompositor`. Tests assert the expected physical bounding surface sizes when scaling correctly intercepts logical to device pixel mappings.

### Milestone 4: Event Routing & Input Hit Testing
Route raw input events (mouse, keyboard) from the secondary OS window back to the main document's DOM.
*   Intercept input events in `UnboundedPanelWidget`.
*   Translate physical window coordinates back to the logical layout coordinates of the main document.
*   Inject the event into the main document's `EventHandler`, constraining the hit-testing algorithm to *only* consider nodes within the `LayoutUnboundedPanel` subtree.

### Milestone 5: Synchronization and Lifecycle Tying
Coordinate the `BeginMainFrame` lifecycle between the two windows.
*   Ensure the `UnboundedPanelWidget` suppresses its own independent lifecycle updates and instead "piggybacks" on the main `LocalFrameView`. 
*   **Anchor Positioning**: If the `<panel>` is visually anchored to an element in the main window, scrolling the main window changes the physical screen position of the anchor. Implement a mechanism in `PrePaint` or `Layout` that detects anchor movement and synchronously sends a `SetBounds` IPC to the browser process to move the OS window in lockstep.

## 5. Open Questions & Risks

*   **Compositor State and Animations**: Blink's `cc` layer assumes hardware-accelerated CSS animations run on a single `LayerTreeHost`. We must ensure animations inside the `<panel>` are correctly dispatched to the secondary `LayerTreeHost`.
*   **Accessibility (A11y)**: How does the accessibility tree model a single document spanning multiple OS windows? Screen readers generally expect a window to map 1:1 with an accessibility root.
*   **Focus Management**: Focus is traditionally a document-level concept. If focus moves into the `<panel>`, does the main window lose OS-level focus? How does this impact the `:focus` pseudo-class and keyboard event routing?
*   **Intersection Observers**: If the unbounded panel is moved off-screen relative to its secondary OS window, how do Intersection Observers behave?

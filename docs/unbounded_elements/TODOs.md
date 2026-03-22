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
- [ ] **Modify Pre-Paint Phase**:
  - [ ] Update `PrePaintTreeWalk` to recognize `LayoutUnboundedPanel`. 
  - [ ] Ensure it doesn't emit standard paint properties that would force it into the main window's compositing layers.
- [ ] **Modify Paint Phase**:
  - [ ] Update `PaintLayerPainter` (or relevant `ObjectPainter`) to check if the current `LayoutObject` is a `LayoutUnboundedPanel`.
  - [ ] If painting the main window, explicitly **skip** recording paint operations for the panel and all of its descendants.
- [ ] **Testing**:
  - [ ] Write a web test (WPT or Blink internal) that inserts a `<panel>` with text and bright background colors, asserting that it does not visually render on the page and does not affect the page's scrollable area.

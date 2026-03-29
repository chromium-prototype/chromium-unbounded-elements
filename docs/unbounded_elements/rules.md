# Development Rules & Guidelines

When working on the Unbounded Elements project, please observe the following guidelines based on prior iterations and best practices:

## 1. Commit Hygiene
- **Verbose Commit Messages**: Ensure commit messages are highly detailed. Explicitly mention the `Bug: ` number (or `None`), testing details (e.g. `TDD-Coverage: <test file> passes`), and a breakdown of all touched files and the reasoning behind structural or configuration changes.
  - **Documentation Commits**: You may include updates to `TODOs.md` in the same commit as the codebase implementation. There is no need for a separate commit.

## 2. Blink Boilerplate & Build Configurations
- **Adding new HTML Elements**: It is rarely as simple as just adding a `.h` and `.cc` file. 
  - Ensure the element is mapped in `third_party/blink/renderer/core/html/html_tag_names.json5`.
  - An associated `.idl` file must be created to expose the class to JavaScript.
  - The IDL file **must** be registered in `third_party/blink/renderer/bindings/idl_in_core.gni`.
  - The targeted generated bindings (e.g., `v8_html_panel_element.cc / .h`) **must** be explicitly pre-declared under the `interface` target block in `third_party/blink/renderer/bindings/generated_in_core.gni` to prevent `wrapper_type_info_` linker errors.
  - The C++ source files must be appended to the respective directory's GN config, such as `third_party/blink/renderer/core/html/build.gni`.

## 3. Test-Driven Development (TDD)
- Validate every primitive operation with a proper test (e.g., C++ unit tests, or web tests under `third_party/blink/web_tests/unbounded-elements/`) before initiating the implementation. As long as the change is properly verified, it follows best TDD practices.
- Validate that the test fails in the red phase, and explicitly capture the success in the green phase context.

## 4. Documentation Upkeep
- Rigorously update the `TODOs.md` checklist.
- If previously undocumented complexities emerge (e.g., discovering the GN V8 bindings requirement), expand the checklist and documentation to proactively encompass those new steps for future team members and AI assists.

## 5. Preventing Regressions
- When modifying window types, z-ordering, or clipping logic for unbounded elements, ALWAYS run the full suite of integration tests to prevent subtle UI/OS bugs (like lost shadows, always-on-top behavior, cursor mismappings, or drag synchronization failures).
- Ensure you run and pass at least the following test suites before committing:
  - `HTMLPanelElementBrowserTest.UnboundedPanelZOrder`
  - `HTMLPanelElementBrowserTest.WindowBoundsSync`
  - `HTMLPanelElementBrowserTest.InputEventRouting`
  - `third_party/blink/web_tests/unbounded-elements/headless_tester/run_sophisticated_test_wrapper.sh` (for visual clipping and bounds verification)

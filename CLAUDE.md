# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & run

There is no build system. The example is compiled directly:

```sh
clang++ -std=c++23 -Wall -Wextra -Wpedantic -o example example.cpp
./example
```

`.clangd` pins the language to C++23 with `-Wall -Wextra -Wpedantic`, plus `modernize-*` and `performance-*` clang-tidy checks. The header requires C++20 (uses `fixed_string` non-type template parameters and `__VA_OPT__`); `.clangd` is set to C++23 to match the LSP setup.

There are no tests, lint scripts, or CI.

## Architecture

`htpp.hpp` is a single-header HTML DSL that streams directly to a `std::ostream`. There is no AST and no intermediate buffer — RAII tag objects write the opening tag in their constructor and the closing tag in their destructor, so HTML structure is encoded in C++ scope structure.

Three layers compose the DSL:

1. **Tag layer (`htpp::tag` / `htpp::void_tag`).** RAII wrappers that emit `<name attrs>` on construction and `</name>` on destruction (or `<name attrs />` for void tags). The constructor is a function template taking a parameter pack of attribute objects, which it folds into the stream: `(os << ... << attrs)`. The class itself is not templated, so `if (htpp::tag _ht_(os, "div", a, b); true) { ... }` works without CTAD.

2. **Attribute layer.** Every attribute is a streamable value with its own `operator<<` that emits a leading-space ` k="v"` (or nothing). Five flavours:
   - `attr_set<Name>` — produced by `attr_key<Name> = "v"`. Static name baked into the type.
   - `attr_bool_t<Name>` — bare boolean attribute (`disabled`, `checked`, …); streams its name.
   - `attr_dyn` — produced by `"name"_a = "v"`. Dynamic name held as `string_view`.
   - `attr_if_t<Inner>` — emits `Inner` only when its `on` flag is true.
   - User types: anything with a streaming operator that emits a leading-space fragment also works.

   There is no central `html_attrs` struct. Adding an attribute is one `inline constexpr attr_key<"new-attr"> new_attr;` line in `htpp::attr`.

3. **Macro layer.** `HT_*(...) { ... }` wraps `tag` in `if (init; true)` so a trailing block becomes the children. Variadic `__VA_ARGS__` forwards attribute expressions to the constructor pack. `HT_VOID` produces a brace-init expression (statement, not block).

### Invariants that will bite you

- **`os` must be in scope.** Every `HT_*` macro and `HT_TEXT` / `HT_RAW` references a local variable named `os` of type `std::ostream&`. Use `HT_COMPONENT(name, ...)` to declare functions with `std::ostream& os` as the first parameter.
- **Attribute identifiers vs rendered names.** `htpp::attr::class_` renders as `class="…"`, `for_` → `for`, `default_` → `default` (C++ keyword collisions). Hyphenated HTML attributes are transliterated: `aria_label` → `aria-label`, `hx_post` → `hx-post`, `http_equiv` → `http-equiv`. Adding a new attribute means picking the C++ identifier *and* the rendered string; they don't have to match. Don't try to be clever with auto-rewriting underscores — explicit declarations stay grep-able.
- **Long-tail / dynamic attribute names use the `_a` UDL:** `"data-foo-bar"_a = "x"` (requires `using namespace htpp::attr_literals;`). Reserve this for project-specific or genuinely dynamic names; predeclare anything you'll use more than once or twice in `htpp::attr`.
- **Boolean attributes are bare names, not assignments.** `HT_INPUT(type = "checkbox", checked, required);` — `checked` and `required` are `attr_bool_t` objects, not assignments. Mixing forms in one call is fine.
- **Conditional attributes use `attr_if`:** `HT_BUTTON(class_ = "btn", attr_if(submitting, disabled)) { ... }`. The wrapper emits its inner attribute only when the flag is true. Lives in `htpp::attr` so it's in scope after `using namespace htpp::attr;`.
- **Attribute values are `std::string_view`.** They don't own — passing a temporary `std::string` works only because the whole call site is a single full-expression. Don't store an attribute object in a variable for later use if it captures a temporary.
- **Escaping is the caller's responsibility for content.** `HT_TEXT(expr)` runs `htpp::escape` (handles `& < > " '`); `HT_RAW(expr)` and bare `os << "…"` do not. Attribute *values* are escaped automatically by `attr_set` / `attr_dyn`. Dynamic attribute *names* (via `_a` UDL) are validated at runtime against `[a-zA-Z_][a-zA-Z0-9\-:_.]*` and will throw `std::invalid_argument` on violation.
- **Event handler attributes (`on*`) are not predeclared.** Inline event handler values are JavaScript — HTML-entity escaping is the wrong defense for a JS context. Prefer `addEventListener` in a `<script>` block. If you must use an inline handler, the `_a` UDL still works (`"onclick"_a = "..."`), making the choice explicit and visible.
- **Void tags are statements, not blocks.** `HT_BR()`, `HT_HR()`, `HT_IMG(...)`, `HT_INPUT(...)`, `HT_META(...)`, `HT_LINK(...)` expand to a `void_tag{...}` expression and require a trailing `;`. Do not follow them with `{ ... }`.
- **Tag macros without `{ }` consume the next statement.** `HT_DIV(...)` expands to `if (...; true)` — without a block, the next statement becomes its body, which usually produces wrong-nesting HTML. Always pair `HT_DIV(...)` with `{ ... }` (use `{}` if you genuinely want an empty element). The current `example.cpp` uses `HT_TEXTAREA(...) {}` for exactly this reason.
- **Watch for shadowing after `using namespace htpp::attr;`.** Common attribute identifiers (`name`, `id`, `value`, `type`, `size`, `min`, `max`, `start`, `step`, `title`, `label`, `form`, `dir`, `list`, `cite`) become globally visible and will shadow local variables of the same name. The example renames its row variables (`user_name` / `user_role`) for this reason. Either rename locals or scope the `using` to function-level.

### Children-accepting components

`HT_COMPONENT` declares a `void` function with a fixed structure — the call site cannot inject children. To build a component that wraps user-supplied content, write a function (or class) whose returned RAII object closes the wrapping markup, and invoke it with `HT_USE(name, args...) { ...children... }`.

- **Simple wrapper — function returning `htpp::tag`:**
  ```cpp
  auto nav_link(std::ostream& os, std::string_view url) -> htpp::tag {
      return {os, "a", class_ = "px-3 py-2 hover:underline", href = url};
  }
  // call site:
  HT_USE(nav_link, "/about") { HT_TEXT("About"); }
  ```
  The brace-init form `return {os, "a", ...};` triggers mandatory prvalue copy elision (no move needed).

- **Composite (prologue inside the wrapping tag):** declare the wrapping tag as a local, emit prologue, return the local. The named-return path uses the move ctor:
  ```cpp
  auto card(std::ostream& os, std::string_view heading) -> htpp::tag {
      htpp::tag div(os, "div", class_ = "rounded shadow p-4 bg-white");
      HT_H2(class_ = "text-lg font-bold mb-2") { HT_TEXT(heading); }
      return div;   // moves; div destructor on the moved-from instance is a no-op
  }
  ```

- **Class form (alternative):** for components used in many places, a class that inherits from `htpp::tag` makes "this is a children-accepting component" explicit at the type level and sidesteps any NRVO concerns:
  ```cpp
  struct nav_link : htpp::tag {
      nav_link(std::ostream& os, std::string_view url)
        : htpp::tag(os, "a", class_ = "px-3 py-2 hover:underline", href = url) {}
  };
  ```
  Invoked the same way: `HT_USE(nav_link, "/about") { HT_TEXT("About"); }`.

- **`htpp::tag` is movable but not move-assignable.** The move ctor nulls the source's `os_` so only one instance ever emits the closing tag. Don't try to reassign one tag with another — declare a fresh local instead.

- **No "after-children, before-close" hook.** A bare `htpp::tag` return only emits `</name>` after the user's block; there's no place to inject content between children and the closing tag. If a real use case appears, a templated `scoped_emit<Lambda>` could fill the gap allocation-free.

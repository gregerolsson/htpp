# &lt;htpp&gt;

A single-header C++ HTML DSL. RAII tags stream directly to `std::ostream` — no AST, no intermediate buffer. HTML structure is C++ scope structure.

**C++20** | **Header-only** | **Zero allocations** | **htmx-ready**

## Quick start

Copy `htpp.hpp` into your project. There are no dependencies beyond the standard library.

```cpp
// hello.cpp
#include "htpp.hpp"
#include <iostream>

using namespace htpp::attr;

int main() {
    auto& os = std::cout;

    HT_DOCTYPE();
    HT_HTML(lang = "en") {
        HT_HEAD() {
            HT_META(charset = "UTF-8");
            HT_TITLE() { os << "Hello"; }
        }
        HT_BODY() {
            HT_H1() { os << "Hello, world!"; }
        }
    }
}
```

Compile and run:

```sh
clang++ -std=c++23 -o hello hello.cpp
./hello
```

## How it works

htpp has three layers, each building on the one below.

### 1. Tag layer

`htpp::tag` is an RAII wrapper. Its constructor writes `<name attrs>` to the stream; its destructor writes `</name>`. Self-closing elements use `htpp::void_tag` instead, which writes `<name attrs />` in the constructor only.

Because the closing tag is emitted by the destructor, HTML nesting is enforced by C++ scope nesting. You literally cannot forget to close a tag.

### 2. Attribute layer

Attributes are lightweight value objects, each with its own `operator<<` that emits ` key="value"` (with a leading space). The tag constructor folds its parameter pack into the stream:

```cpp
(os << ... << attrs);
```

There is no central attribute struct. Five flavours exist:

| Flavour | Syntax | Example output |
|---|---|---|
| Static key + value | `class_ = "flex"` | ` class="flex"` |
| Boolean | `disabled` | ` disabled` |
| Dynamic key | `"data-x"_a = "1"` | ` data-x="1"` |
| Conditional | `attr_if(flag, disabled)` | ` disabled` or nothing |
| Custom | any streamable type | whatever it emits |

### 3. Macro layer

Macros like `HT_DIV(...)` wrap the tag in an `if (init; true)` statement so a trailing `{ ... }` block becomes the element's children. Void-element macros (`HT_BR()`, `HT_IMG(...)`, etc.) expand to an expression and take a trailing `;` instead.

## Attributes

### Predeclared keys

After `using namespace htpp::attr;` the common HTML attributes are in scope as `constexpr` objects. Use them with `=` to set a value:

```cpp
HT_A(href = "/about", class_ = "link") { os << "About"; }
```

Identifiers that collide with C++ keywords have a trailing underscore:

| C++ name | Rendered as |
|---|---|
| `class_` | `class` |
| `for_` | `for` |
| `default_` | `default` |

Hyphenated HTML names use underscores in C++:

| C++ name | Rendered as |
|---|---|
| `aria_label` | `aria-label` |
| `hx_post` | `hx-post` |
| `http_equiv` | `http-equiv` |

### Boolean attributes

Boolean attributes are bare names, not assignments. Mix them freely with key-value attributes:

```cpp
HT_INPUT(type = "checkbox", checked, required);
```

### Dynamic / long-tail attributes

For one-off or project-specific attribute names, use the `_a` user-defined literal (requires `using namespace htpp::attr_literals;`):

```cpp
HT_BUTTON("data-action"_a = "save",
          "data-confirm"_a = "Are you sure?") {
    os << "Save";
}
```

### Conditional attributes

`attr_if(condition, attribute)` emits the attribute only when the condition is true:

```cpp
HT_BUTTON(class_ = "btn",
          attr_if(is_submitting, disabled)) {
    os << "Submit";
}
```

### Adding a new attribute

One line in `htpp::attr`:

```cpp
inline constexpr attr_key<"my-attr"> my_attr;
```

## Components

`HT_COMPONENT` declares a plain function with `std::ostream& os` as the first parameter. Compose them like regular function calls:

```cpp
HT_COMPONENT(card, std::string_view heading, std::string_view body) {
    HT_DIV(class_ = "rounded shadow p-4") {
        HT_H2(class_ = "font-bold") { HT_TEXT(heading); }
        HT_P()                       { HT_TEXT(body); }
    }
}

// Call site (os must be in scope):
card(os, "Title", "Description text.");
```

Components are just functions. Use loops, conditionals, and other components inside them freely:

```cpp
HT_COMPONENT(user_table,
    const std::vector<std::pair<std::string, std::string>>& users)
{
    HT_TABLE(class_ = "w-full") {
        HT_THEAD() {
            HT_TR() {
                HT_TH() { os << "Name"; }
                HT_TH() { os << "Role"; }
            }
        }
        HT_TBODY() {
            for (auto& [user_name, user_role] : users) {
                HT_TR() {
                    HT_TD() { HT_TEXT(user_name); }
                    HT_TD() { HT_TEXT(user_role); }
                }
            }
        }
    }
}
```

## Components with children

Plain `HT_COMPONENT` functions can't accept a children block — every "slot" has to be a parameter. To wrap user-supplied content, define a function that returns an `htpp::tag` (or a class deriving from it) and invoke it with `HT_USE(name, args...) { ... }`.

### Simple wrapper

For a component that just wraps children in a single tag, return the tag directly:

```cpp
auto nav_link(std::ostream& os, std::string_view url) -> htpp::tag {
    return {os, "a", class_ = "px-3 py-2 hover:underline", href = url};
}

// call site:
HT_USE(nav_link, "/about") { HT_TEXT("About"); }
```

The brace-init `return {os, "a", ...};` is a prvalue, so mandatory copy elision applies — no move, no allocation.

### Composite (prologue inside the wrapping tag)

If the component needs to emit content *before* children but *inside* the wrapping element, declare the wrapping tag as a local, emit the prologue, then return the local:

```cpp
auto card(std::ostream& os, std::string_view heading) -> htpp::tag {
    htpp::tag div(os, "div", class_ = "rounded shadow p-4 bg-white");
    HT_H2(class_ = "text-lg font-bold mb-2") { HT_TEXT(heading); }
    return div;   // moves; the moved-from local's destructor is a no-op
}

// call site:
HT_USE(card, "Posts") {
    HT_P(class_ = "text-gray-600") { HT_TEXT("12 published."); }
}
```

### How `HT_USE` works

`HT_USE(name, args...)` expands to `if (auto _ht_use_ = name(os, args...); true) { ... }`. The returned `htpp::tag` lives until the end of the user's block; its destructor emits the closing markup *after* the children. The user's block writes to the same `os` already in scope — no lambdas, no callbacks.

> **Note:** `htpp::tag` is movable but not move-assignable. The move ctor nulls the source so only one instance ever emits the closing tag. There is no hook for content *between* children and the closing tag — if you need that, the component needs a different RAII type.

## Text & escaping

| Macro | Escapes? | Use for |
|---|---|---|
| `HT_TEXT(expr)` | Yes (`& < > " '`) | User-supplied content |
| `HT_RAW(expr)` | No | Trusted / pre-escaped HTML |
| `os << "..."` | No | String literals you control |

> **Safety note:** Attribute *values* are automatically escaped by `attr_set` and `attr_dyn`. Dynamic attribute *names* (via `_a`) are validated at runtime against `[a-zA-Z_][a-zA-Z0-9\-:_.]*` and will throw `std::invalid_argument` on violation — but validation is not escaping, so don't rely on it to sanitise arbitrary user input as attribute names.
>
> **Event handler attributes** (`onclick`, `onchange`, etc.) are intentionally not predeclared. Their values are JavaScript, not HTML — the entity escaping applied to attribute values is the wrong defence for a JS context. Prefer `addEventListener` in a `<script>` block. If you must use an inline handler, `"onclick"_a = "..."` still works.

## Things to know

> **`os` must be in scope.** Every `HT_*` macro references a local variable named `os` of type `std::ostream&`. Use `HT_COMPONENT` to get it automatically, or declare it yourself.

### Void tags need a semicolon, not braces

```cpp
HT_BR();                // correct
HT_IMG(src = "a.png");  // correct
HT_BR() { }            // WRONG — won't compile
```

### Always use braces after tag macros

Tag macros expand to `if (...; true)`. Without braces, only the next statement becomes the body, which usually produces wrong HTML. Use `{}` for intentionally empty elements:

```cpp
HT_TEXTAREA(name = "msg") {}  // correct: empty <textarea></textarea>
```

### Watch for variable shadowing

After `using namespace htpp::attr;`, common identifiers like `name`, `id`, `value`, `type`, `min`, `max`, `title`, `label`, `form`, `start`, `step` become visible and will shadow local variables. Rename your locals or scope the `using` directive.

### Attribute values are string_view

They don't own memory. Passing a temporary `std::string` is fine because the entire call is a single full-expression, but don't store an attribute object for later use if it captures a temporary.

## htmx support

All common htmx attributes are predeclared:

```cpp
HT_FORM(hx_post = "/api/users",
        hx_target = "#result",
        hx_swap = "innerHTML") {
    HT_INPUT(type = "text", name = "username");
    HT_BUTTON(type = "submit") { os << "Create"; }
}
```

Available: `hx_get`, `hx_post`, `hx_put`, `hx_delete`, `hx_patch`, `hx_target`, `hx_swap`, `hx_trigger`, `hx_include`, `hx_indicator`, `hx_select`, `hx_push_url`, `hx_vals`, `hx_headers`, `hx_confirm`, `hx_boost`.

## Macro reference

### Block elements (use with `{ ... }`)

| Category | Macros |
|---|---|
| Document | `HT_HTML` `HT_HEAD` `HT_BODY` `HT_TITLE` `HT_STYLE` `HT_SCRIPT` |
| Sections | `HT_DIV` `HT_SPAN` `HT_MAIN` `HT_HEADER` `HT_FOOTER` `HT_SECTION` `HT_ARTICLE` `HT_ASIDE` `HT_NAV` |
| Headings | `HT_H1` – `HT_H6` |
| Text | `HT_P` `HT_A` `HT_STRONG` `HT_EM` `HT_CODE` `HT_PRE` `HT_BLOCKQUOTE` `HT_LABEL` `HT_BUTTON` |
| Lists | `HT_UL` `HT_OL` `HT_LI` `HT_DL` `HT_DT` `HT_DD` |
| Tables | `HT_TABLE` `HT_THEAD` `HT_TBODY` `HT_TFOOT` `HT_TR` `HT_TH` `HT_TD` |
| Forms | `HT_FORM` `HT_SELECT` `HT_OPTION` `HT_TEXTAREA` `HT_FIELDSET` `HT_LEGEND` |
| Misc | `HT_FIGURE` `HT_FIGCAPTION` `HT_DETAILS` `HT_SUMMARY` |

### Void elements (use with `;`)

`HT_DOCTYPE()` `HT_META(...)` `HT_LINK(...)` `HT_BR()` `HT_HR()` `HT_IMG(...)` `HT_INPUT(...)`

### Content

`HT_TEXT(expr)` — escaped output. `HT_RAW(expr)` — raw output.

`HT_COMPONENT(name, ...)` — declares a component function.

`HT_USE(name, ...)` — invokes a children-accepting component (one that returns an `htpp::tag`) with a trailing `{ ... }` block.

## Full example

```cpp
#include "htpp.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <utility>

using namespace htpp::attr;
using namespace htpp::attr_literals;

HT_COMPONENT(nav_link, std::string_view url, std::string_view text) {
    HT_A(class_ = "px-3 py-2 hover:underline", href = url) {
        HT_TEXT(text);
    }
}

HT_COMPONENT(card, std::string_view heading, std::string_view body) {
    HT_DIV(class_ = "rounded shadow p-4 bg-white") {
        HT_H2(class_ = "text-lg font-bold mb-2") { HT_TEXT(heading); }
        HT_P (class_ = "text-gray-600")          { HT_TEXT(body); }
    }
}

HT_COMPONENT(page, const std::string& username, bool is_submitting) {
    HT_DOCTYPE();
    HT_HTML(lang = "en") {
        HT_HEAD() {
            HT_META(charset = "UTF-8");
            HT_TITLE() { os << "htpp demo"; }
        }
        HT_BODY(class_ = "bg-gray-50") {
            HT_HEADER(class_ = "bg-blue-600 text-white p-4") {
                nav_link(os, "/", "Home");
                nav_link(os, "/about", "About");
            }
            HT_MAIN(class_ = "max-w-3xl mx-auto mt-8 px-4") {
                HT_H1() {
                    os << "Welcome, ";
                    HT_TEXT(username);  // safely escaped
                }
                HT_DIV(class_ = "grid grid-cols-2 gap-4") {
                    card(os, "Posts", "12 published.");
                    card(os, "Comments", "3 pending.");
                }
                HT_FORM(action = "/send", method = "post") {
                    HT_TEXTAREA(name = "message", rows = "4") {}
                    HT_BUTTON(type = "submit",
                              "data-action"_a = "send",
                              attr_if(is_submitting, disabled)) {
                        os << "Send";
                    }
                }
            }
        }
    }
}

int main() {
    page(std::cout, "Alice & \"friends\"", false);
}
```

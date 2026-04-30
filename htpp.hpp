#pragma once
// htpp.hpp — single-header C++ HTML DSL
// RAII tags + macro-based nesting, writes directly to std::ostream.
// Requires C++20 (fixed_string non-type template parameters, __VA_OPT__).
//
// PRECONDITION: every scope that uses the HT_* macros must have a local
// variable named `os` of type std::ostream& (or compatible).
//
// Usage:
//   using namespace htpp::attr;            // class_, id, href, ...
//   using namespace htpp::attr_literals;   // "data-x"_a = "..."  (long tail)
//
//   HT_COMPONENT(card, std::string_view title) {
//       HT_DIV(class_ = "rounded shadow p-4") {
//           HT_H2(class_ = "font-bold") { HT_TEXT(title); }
//       }
//   }
//
//   // call site (os must be in scope):
//   card(os, "Hello");

#include <array>
#include <cstddef>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace htpp {

// ---------------------------------------------------------------------------
// fixed_string — string usable as a non-type template parameter.
// ---------------------------------------------------------------------------
template <std::size_t N>
struct fixed_string {
    std::array<char, N> data{};
    // NOLINTNEXTLINE(modernize-avoid-c-arrays,google-explicit-constructor,hicpp-explicit-conversions)
    constexpr fixed_string(const char (&s)[N]) {
        for (std::size_t i = 0; i < N; ++i) {
            data[i] = s[i];
        }
    }
    [[nodiscard]] constexpr auto view() const noexcept -> std::string_view {
        return std::string_view{data.data(), N - 1};   // strip trailing '\0'
    }
};
// NOLINTNEXTLINE(modernize-avoid-c-arrays)
template <std::size_t N> fixed_string(const char (&)[N]) -> fixed_string<N>;

// ---------------------------------------------------------------------------
// escape — HTML-encode a string_view into an ostream.
// ---------------------------------------------------------------------------
inline void escape(std::ostream& os, std::string_view s) {
    for (char c : s) {
        switch (c) {
            case '&':  os << "&amp;";  break;
            case '<':  os << "&lt;";   break;
            case '>':  os << "&gt;";   break;
            case '"':  os << "&quot;"; break;
            case '\'': os << "&#39;";  break;
            default:   os << c;        break;
        }
    }
}

// ---------------------------------------------------------------------------
// validate_attr_name — rejects names that could break out of an HTML attribute.
// Allows [a-zA-Z_][a-zA-Z0-9\-:_.]*  (covers data-*, aria-*, xml:lang, etc.)
// ---------------------------------------------------------------------------
inline void validate_attr_name(std::string_view name) {
    if (name.empty()) {
        throw std::invalid_argument("htpp: empty attribute name");
    }
    auto is_start = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    auto is_cont = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '-' || c == ':' || c == '.' || c == '_';
    };
    if (!is_start(name[0])) {
        throw std::invalid_argument("htpp: invalid attribute name start character");
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (!is_cont(name[i])) {
            throw std::invalid_argument("htpp: invalid character in attribute name");
        }
    }
}

// ---------------------------------------------------------------------------
// raw — wrapper for pre-escaped / trusted HTML fragments.
// ---------------------------------------------------------------------------
struct raw {
    std::string_view content;
    friend auto operator<<(std::ostream& os, const raw& r) -> std::ostream& {
        return os << r.content;
    }
};

// ---------------------------------------------------------------------------
// Attribute value types — each one streams its own ` k="v"` (or nothing).
// The tag constructor folds the parameter pack into the stream:
//     (os << ... << attrs)
// so any user type with a streaming operator that emits a leading space
// works as an attribute.
// ---------------------------------------------------------------------------

// Static-name attribute with a value. Produced by `attr_key<"name"> = "value"`.
template <fixed_string Name>
struct attr_set {
    std::string_view value;
    friend auto operator<<(std::ostream& os, const attr_set& a) -> std::ostream& {
        os << ' ' << Name.view() << "=\"";
        escape(os, a.value);
        os << '"';
        return os;
    }
};

// Boolean attribute (e.g. `disabled`, `checked`). Streams its bare name.
template <fixed_string Name>
struct attr_bool_t {
    friend auto operator<<(std::ostream& os, const attr_bool_t&) -> std::ostream& {
        return os << ' ' << Name.view();
    }
};

// Dynamic-name attribute (used by the `_a` UDL).
// The name is validated at stream-time to prevent attribute injection.
struct attr_dyn {
    std::string_view name;
    std::string_view value;
    friend auto operator<<(std::ostream& os, const attr_dyn& a) -> std::ostream& {
        validate_attr_name(a.name);
        os << ' ' << a.name << "=\"";
        escape(os, a.value);
        os << '"';
        return os;
    }
};

// Conditional wrapper — emits the inner attribute only when `on` is true.
template <typename Inner>
struct attr_if_t {
    bool  on;
    Inner inner;
    friend auto operator<<(std::ostream& os, const attr_if_t& a) -> std::ostream& {
        if (a.on) {
            os << a.inner;
        }
        return os;
    }
};

// ---------------------------------------------------------------------------
// Attribute key types — produce a streamable attribute via `operator=`.
// `inline constexpr` predeclared instances live in `htpp::attr` (below).
// ---------------------------------------------------------------------------

template <fixed_string Name>
struct attr_key {
    constexpr auto operator=(std::string_view v) const noexcept -> attr_set<Name> {
        return {.value = v};
    }
};

// Dynamic key — produced by the `_a` UDL.
struct attr_dyn_key {
    std::string_view name;
    constexpr auto operator=(std::string_view v) const noexcept -> attr_dyn {
        return {.name = name, .value = v};
    }
};

// ---------------------------------------------------------------------------
// User-defined literal — `"data-action"_a = "save"` for long-tail / dynamic
// attribute names. Predeclared keys (below) cover the common set.
// ---------------------------------------------------------------------------
namespace attr_literals {

constexpr auto operator""_a(const char* s, std::size_t n) noexcept -> attr_dyn_key {
    return {std::string_view{s, n}};
}

} // namespace attr_literals

// ---------------------------------------------------------------------------
// Predeclared attribute keys.
//
// Empty inline-constexpr objects — no per-attribute cost when unused, no
// fixed list to maintain in two places. Identifiers that collide with C++
// keywords use a trailing underscore (`class_`, `for_`, `default_`);
// identifiers containing characters illegal in C++ names use a transliteration
// (`aria_label` -> `aria-label`).
// ---------------------------------------------------------------------------
namespace attr {

// --- Conditional factory (re-exported for ergonomics) ---------------------
template <typename Inner>
constexpr auto attr_if(bool cond, Inner inner) -> attr_if_t<Inner> {
    return {.on = cond, .inner = std::move(inner)};
}

// --- Global / common ------------------------------------------------------
inline constexpr attr_key<"class">             class_;
inline constexpr attr_key<"id">                id;
inline constexpr attr_key<"style">             style;
inline constexpr attr_key<"title">             title;
inline constexpr attr_key<"lang">              lang;
inline constexpr attr_key<"dir">               dir;
inline constexpr attr_key<"role">              role;
inline constexpr attr_key<"tabindex">          tabindex;
inline constexpr attr_key<"contenteditable">   contenteditable;
inline constexpr attr_key<"spellcheck">        spellcheck;
inline constexpr attr_key<"draggable">         draggable;
inline constexpr attr_key<"translate">         translate;
inline constexpr attr_key<"slot">              slot;
inline constexpr attr_key<"part">              part;

// --- Links / media --------------------------------------------------------
inline constexpr attr_key<"href">              href;
inline constexpr attr_key<"src">               src;
inline constexpr attr_key<"srcset">            srcset;
inline constexpr attr_key<"sizes">             sizes;
inline constexpr attr_key<"alt">               alt;
inline constexpr attr_key<"rel">               rel;
inline constexpr attr_key<"target">            target;
inline constexpr attr_key<"download">          download;
inline constexpr attr_key<"loading">           loading;
inline constexpr attr_key<"decoding">          decoding;
inline constexpr attr_key<"crossorigin">       crossorigin;
inline constexpr attr_key<"integrity">         integrity;
inline constexpr attr_key<"referrerpolicy">    referrerpolicy;
inline constexpr attr_key<"media">             media;
inline constexpr attr_key<"poster">            poster;
inline constexpr attr_key<"preload">           preload;

// --- Document / meta ------------------------------------------------------
inline constexpr attr_key<"charset">           charset;
inline constexpr attr_key<"content">           content;
inline constexpr attr_key<"http-equiv">        http_equiv;

// --- Forms ----------------------------------------------------------------
inline constexpr attr_key<"action">            action;
inline constexpr attr_key<"method">            method;
inline constexpr attr_key<"enctype">           enctype;
inline constexpr attr_key<"name">              name;
inline constexpr attr_key<"value">             value;
inline constexpr attr_key<"type">              type;
inline constexpr attr_key<"placeholder">       placeholder;
inline constexpr attr_key<"for">               for_;
inline constexpr attr_key<"form">              form;
inline constexpr attr_key<"autocomplete">      autocomplete;
inline constexpr attr_key<"autocapitalize">    autocapitalize;
inline constexpr attr_key<"inputmode">         inputmode;
inline constexpr attr_key<"pattern">           pattern;
inline constexpr attr_key<"min">               min;
inline constexpr attr_key<"max">               max;
inline constexpr attr_key<"step">              step;
inline constexpr attr_key<"minlength">         minlength;
inline constexpr attr_key<"maxlength">         maxlength;
inline constexpr attr_key<"size">              size;
inline constexpr attr_key<"accept">            accept;
inline constexpr attr_key<"list">              list;
inline constexpr attr_key<"rows">              rows;
inline constexpr attr_key<"cols">              cols;
inline constexpr attr_key<"wrap">              wrap;
inline constexpr attr_key<"formaction">        formaction;
inline constexpr attr_key<"formmethod">        formmethod;
inline constexpr attr_key<"formenctype">       formenctype;
inline constexpr attr_key<"formtarget">        formtarget;

// --- Tables ---------------------------------------------------------------
inline constexpr attr_key<"colspan">           colspan;
inline constexpr attr_key<"rowspan">           rowspan;
inline constexpr attr_key<"headers">           headers;
inline constexpr attr_key<"scope">             scope;
inline constexpr attr_key<"abbr">              abbr;

// --- Lists / details ------------------------------------------------------
inline constexpr attr_key<"start">             start;
inline constexpr attr_key<"datetime">          datetime;
inline constexpr attr_key<"cite">              cite;
inline constexpr attr_key<"label">             label;

// --- Image / dimension ----------------------------------------------------
inline constexpr attr_key<"width">             width;
inline constexpr attr_key<"height">            height;
inline constexpr attr_key<"usemap">            usemap;

// --- Event handlers (on*) are intentionally NOT predeclared. ---------------
// Inline event handler values are JavaScript, not HTML — the standard
// HTML-entity escaping applied to attribute values is the wrong defense
// and creates a false sense of safety. Prefer addEventListener in a
// <script> block. If you must use an inline handler, the `_a` UDL still
// works (`"onclick"_a = "..."`) — which makes the choice explicit.

// --- ARIA -----------------------------------------------------------------
inline constexpr attr_key<"aria-label">           aria_label;
inline constexpr attr_key<"aria-labelledby">      aria_labelledby;
inline constexpr attr_key<"aria-describedby">     aria_describedby;
inline constexpr attr_key<"aria-hidden">          aria_hidden;
inline constexpr attr_key<"aria-live">            aria_live;
inline constexpr attr_key<"aria-current">         aria_current;
inline constexpr attr_key<"aria-expanded">        aria_expanded;
inline constexpr attr_key<"aria-controls">        aria_controls;
inline constexpr attr_key<"aria-pressed">         aria_pressed;
inline constexpr attr_key<"aria-selected">        aria_selected;
inline constexpr attr_key<"aria-checked">         aria_checked;
inline constexpr attr_key<"aria-disabled">        aria_disabled;
inline constexpr attr_key<"aria-haspopup">        aria_haspopup;
inline constexpr attr_key<"aria-readonly">        aria_readonly;
inline constexpr attr_key<"aria-required">        aria_required;
inline constexpr attr_key<"aria-busy">            aria_busy;
inline constexpr attr_key<"aria-atomic">          aria_atomic;
inline constexpr attr_key<"aria-relevant">        aria_relevant;
inline constexpr attr_key<"aria-roledescription"> aria_roledescription;

// --- htmx -----------------------------------------------------------------
inline constexpr attr_key<"hx-get">            hx_get;
inline constexpr attr_key<"hx-post">           hx_post;
inline constexpr attr_key<"hx-put">            hx_put;
inline constexpr attr_key<"hx-delete">         hx_delete;
inline constexpr attr_key<"hx-patch">          hx_patch;
inline constexpr attr_key<"hx-target">         hx_target;
inline constexpr attr_key<"hx-swap">           hx_swap;
inline constexpr attr_key<"hx-trigger">        hx_trigger;
inline constexpr attr_key<"hx-include">        hx_include;
inline constexpr attr_key<"hx-indicator">      hx_indicator;
inline constexpr attr_key<"hx-select">         hx_select;
inline constexpr attr_key<"hx-push-url">       hx_push_url;
inline constexpr attr_key<"hx-vals">           hx_vals;
inline constexpr attr_key<"hx-headers">        hx_headers;
inline constexpr attr_key<"hx-confirm">        hx_confirm;
inline constexpr attr_key<"hx-boost">          hx_boost;

// --- Boolean attributes (HTML5 — presence implies true). Use without `=`.
inline constexpr attr_bool_t<"disabled">       disabled;
inline constexpr attr_bool_t<"checked">        checked;
inline constexpr attr_bool_t<"selected">       selected;
inline constexpr attr_bool_t<"required">       required;
inline constexpr attr_bool_t<"readonly">       readonly;
inline constexpr attr_bool_t<"multiple">       multiple;
inline constexpr attr_bool_t<"hidden">         hidden;
inline constexpr attr_bool_t<"autofocus">      autofocus;
inline constexpr attr_bool_t<"autoplay">       autoplay;
inline constexpr attr_bool_t<"controls">       controls;
inline constexpr attr_bool_t<"loop">           loop;
inline constexpr attr_bool_t<"muted">          muted;
inline constexpr attr_bool_t<"open">           open;
inline constexpr attr_bool_t<"reversed">       reversed;
inline constexpr attr_bool_t<"async">          async;
inline constexpr attr_bool_t<"defer">          defer;
inline constexpr attr_bool_t<"novalidate">     novalidate;
inline constexpr attr_bool_t<"formnovalidate"> formnovalidate;
inline constexpr attr_bool_t<"playsinline">    playsinline;
inline constexpr attr_bool_t<"ismap">          ismap;
inline constexpr attr_bool_t<"default">        default_;
inline constexpr attr_bool_t<"itemscope">      itemscope;

} // namespace attr

// ---------------------------------------------------------------------------
// tag — RAII element. Constructor is a function template; the class itself
// is not templated, so it can be named directly in the if-init declaration
// without CTAD.
// ---------------------------------------------------------------------------
class tag {
public:
    template <typename... Attrs>
    tag(std::ostream& os, std::string_view name, const Attrs&... attrs)
        : os_(&os), name_(name)
    {
        os << '<' << name_;
        (void)(os << ... << attrs);   // (void) silences -Wunused-value when pack is empty
        os << '>';
    }
    tag(const tag&)                     = delete;
    auto operator=(const tag&) -> tag&  = delete;
    tag(tag&& other) noexcept
        : os_(std::exchange(other.os_, nullptr)), name_(other.name_) {}
    auto operator=(tag&&) -> tag&       = delete;
    ~tag() { if (os_) { *os_ << "</" << name_ << '>'; } }
private:
    std::ostream*    os_;
    std::string_view name_;
};

// ---------------------------------------------------------------------------
// void_tag — self-closing elements (<br>, <hr>, <img>, <input>, <meta>, …).
// ---------------------------------------------------------------------------
struct void_tag {
    template <typename... Attrs>
    void_tag(std::ostream& os, std::string_view name, const Attrs&... attrs) {
        os << '<' << name;
        (void)(os << ... << attrs);
        os << " />";
    }
};

// ---------------------------------------------------------------------------
// doctype — emits <!DOCTYPE html>.
// ---------------------------------------------------------------------------
struct doctype {
    explicit doctype(std::ostream& os) { os << "<!DOCTYPE html>\n"; }
};

// ---------------------------------------------------------------------------
// scope_exit — runs `fn` on destruction. Used by HT_USE to defer the
// component invocation until after the user's children have been buffered.
// ---------------------------------------------------------------------------
template <typename Fn>
struct scope_exit {
    Fn fn;
    ~scope_exit() { fn(); }
};
template <typename Fn> scope_exit(Fn) -> scope_exit<Fn>;

} // namespace htpp

// ---------------------------------------------------------------------------
// Macro layer
//
// PRECONDITION: `os` (std::ostream&) must be in scope.
//
//   HT_DIV(class_ = "flex", id = "main") { ... }
//   expands to:
//   if (::htpp::tag _ht_(os, "div", class_ = "flex", id = "main"); true) { ... }
//
//   No-attribute form:
//   HT_DIV() { ... }   or   HT_SECTION() { ... }
// ---------------------------------------------------------------------------

#define HT_TAG(name, ...) \
    if (::htpp::tag _ht_(os, name __VA_OPT__(,) __VA_ARGS__); true)

#define HT_VOID(name, ...) \
    ::htpp::void_tag{os, name __VA_OPT__(,) __VA_ARGS__}

// Document
#define HT_DOCTYPE()           ::htpp::doctype{os}
#define HT_HTML(...)           HT_TAG("html",       __VA_ARGS__)
#define HT_HEAD(...)           HT_TAG("head",       __VA_ARGS__)
#define HT_BODY(...)           HT_TAG("body",       __VA_ARGS__)
#define HT_TITLE(...)          HT_TAG("title",      __VA_ARGS__)
#define HT_STYLE(...)          HT_TAG("style",      __VA_ARGS__)
#define HT_SCRIPT(...)         HT_TAG("script",     __VA_ARGS__)

// Metadata void tags
#define HT_META(...)           HT_VOID("meta",      __VA_ARGS__)
#define HT_LINK(...)           HT_VOID("link",      __VA_ARGS__)

// Sectioning / grouping
#define HT_DIV(...)            HT_TAG("div",        __VA_ARGS__)
#define HT_SPAN(...)           HT_TAG("span",       __VA_ARGS__)
#define HT_MAIN(...)           HT_TAG("main",       __VA_ARGS__)
#define HT_HEADER(...)         HT_TAG("header",     __VA_ARGS__)
#define HT_FOOTER(...)         HT_TAG("footer",     __VA_ARGS__)
#define HT_SECTION(...)        HT_TAG("section",    __VA_ARGS__)
#define HT_ARTICLE(...)        HT_TAG("article",    __VA_ARGS__)
#define HT_ASIDE(...)          HT_TAG("aside",      __VA_ARGS__)
#define HT_NAV(...)            HT_TAG("nav",        __VA_ARGS__)

// Headings
#define HT_H1(...)             HT_TAG("h1",         __VA_ARGS__)
#define HT_H2(...)             HT_TAG("h2",         __VA_ARGS__)
#define HT_H3(...)             HT_TAG("h3",         __VA_ARGS__)
#define HT_H4(...)             HT_TAG("h4",         __VA_ARGS__)
#define HT_H5(...)             HT_TAG("h5",         __VA_ARGS__)
#define HT_H6(...)             HT_TAG("h6",         __VA_ARGS__)

// Text / inline
#define HT_P(...)              HT_TAG("p",          __VA_ARGS__)
#define HT_A(...)              HT_TAG("a",          __VA_ARGS__)
#define HT_STRONG(...)         HT_TAG("strong",     __VA_ARGS__)
#define HT_EM(...)             HT_TAG("em",         __VA_ARGS__)
#define HT_CODE(...)           HT_TAG("code",       __VA_ARGS__)
#define HT_PRE(...)            HT_TAG("pre",        __VA_ARGS__)
#define HT_BLOCKQUOTE(...)     HT_TAG("blockquote", __VA_ARGS__)
#define HT_LABEL(...)          HT_TAG("label",      __VA_ARGS__)
#define HT_BUTTON(...)         HT_TAG("button",     __VA_ARGS__)

// Void text tags
#define HT_BR()                HT_VOID("br")
#define HT_HR()                HT_VOID("hr")
#define HT_IMG(...)            HT_VOID("img",       __VA_ARGS__)
#define HT_INPUT(...)          HT_VOID("input",     __VA_ARGS__)

// Lists
#define HT_UL(...)             HT_TAG("ul",         __VA_ARGS__)
#define HT_OL(...)             HT_TAG("ol",         __VA_ARGS__)
#define HT_LI(...)             HT_TAG("li",         __VA_ARGS__)
#define HT_DL(...)             HT_TAG("dl",         __VA_ARGS__)
#define HT_DT(...)             HT_TAG("dt",         __VA_ARGS__)
#define HT_DD(...)             HT_TAG("dd",         __VA_ARGS__)

// Tables
#define HT_TABLE(...)          HT_TAG("table",      __VA_ARGS__)
#define HT_THEAD(...)          HT_TAG("thead",      __VA_ARGS__)
#define HT_TBODY(...)          HT_TAG("tbody",      __VA_ARGS__)
#define HT_TFOOT(...)          HT_TAG("tfoot",      __VA_ARGS__)
#define HT_TR(...)             HT_TAG("tr",         __VA_ARGS__)
#define HT_TH(...)             HT_TAG("th",         __VA_ARGS__)
#define HT_TD(...)             HT_TAG("td",         __VA_ARGS__)

// Forms
#define HT_FORM(...)           HT_TAG("form",       __VA_ARGS__)
#define HT_SELECT(...)         HT_TAG("select",     __VA_ARGS__)
#define HT_OPTION(...)         HT_TAG("option",     __VA_ARGS__)
#define HT_TEXTAREA(...)       HT_TAG("textarea",   __VA_ARGS__)
#define HT_FIELDSET(...)       HT_TAG("fieldset",   __VA_ARGS__)
#define HT_LEGEND(...)         HT_TAG("legend",     __VA_ARGS__)

// Misc
#define HT_FIGURE(...)         HT_TAG("figure",     __VA_ARGS__)
#define HT_FIGCAPTION(...)     HT_TAG("figcaption", __VA_ARGS__)
#define HT_DETAILS(...)        HT_TAG("details",    __VA_ARGS__)
#define HT_SUMMARY(...)        HT_TAG("summary",    __VA_ARGS__)

// ---------------------------------------------------------------------------
// Content helpers — HT_TEXT escapes user data; HT_RAW trusts the caller.
// Both require `os` in scope.
// ---------------------------------------------------------------------------
#define HT_TEXT(expr)   ::htpp::escape(os, (expr))
#define HT_RAW(expr)    os << ::htpp::raw{(expr)}

// ---------------------------------------------------------------------------
// HT_COMPONENT_DECL — forward-declares a component in a header. The default
// argument on `_slot` must live on the declaration, so use this macro in
// .hh/.hpp files and HT_COMPONENT for the .cc/.cpp definition.
//
// HT_COMPONENT — defines a component function. Requires a prior
// HT_COMPONENT_DECL (in an included header) so that callers see the default
// argument for `_slot`.
//
//   // foo.hh
//   HT_COMPONENT_DECL(card, std::string_view title);
//
//   // foo.cc
//   HT_COMPONENT(card, std::string_view title) {
//       HT_DIV(class_ = "card") {
//           HT_H2() { HT_TEXT(title); }
//           HT_SLOT();           // children land here, if any
//       }
//   }
//
//   card(os, "Hello");                          // no children — slot is empty
//   HT_USE(card, "Hello") { HT_TEXT("body"); }  // children buffered, spliced
// ---------------------------------------------------------------------------
#define HT_COMPONENT_DECL(name, ...) \
    void name(std::ostream& os __VA_OPT__(,) __VA_ARGS__, ::std::string_view _slot = {})

#define HT_COMPONENT(name, ...) \
    void name(std::ostream& os __VA_OPT__(,) __VA_ARGS__, [[maybe_unused]] ::std::string_view _slot)

// HT_SLOT() — emit captured children at the current point in a HT_COMPONENT.
#define HT_SLOT() (os << _slot)

// ---------------------------------------------------------------------------
// HT_USE — invoke a HT_COMPONENT with a trailing children block.
//
// Captures the user's block into a std::ostringstream by shadowing `os`,
// then on scope-exit calls the component with the buffered string view as
// its trailing _slot argument. Allocates one ostringstream per call.
// ---------------------------------------------------------------------------
#define HT_USE(name, ...) \
    if (::std::ostringstream _slot_buf_; true) \
    if (auto& _outer_os_ = os; true) \
    if (::htpp::scope_exit _se_{[&]() -> void { \
            name(_outer_os_ __VA_OPT__(,) __VA_ARGS__, _slot_buf_.view()); \
        }}; true) \
    if (auto& os = static_cast<::std::ostream&>(_slot_buf_); true)

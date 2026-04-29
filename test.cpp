// test.cpp — doctest suite for htpp.hpp
// Compile & run: make check
//
// Coverage: escape(), attr_set, attr_bool_t, attr_dyn, validate_attr_name,
//           attr_if, tag, void_tag, doctype, raw, HT_* macros, HT_COMPONENT,
//           security / injection scenarios, and edge cases.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "htpp.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

using namespace htpp::attr;
using namespace htpp::attr_literals;

// ─── test helpers ────────────────────────────────────────────────────────────

static std::string escape_str(std::string_view s) {
    std::ostringstream oss;
    htpp::escape(oss, s);
    return oss.str();
}

// ─── htpp::escape ────────────────────────────────────────────────────────────

TEST_CASE("escape — empty string") {
    CHECK(escape_str("") == "");
}

TEST_CASE("escape — plain text passthrough") {
    CHECK(escape_str("Hello, World!")        == "Hello, World!");
    CHECK(escape_str("no special chars 123") == "no special chars 123");
}

TEST_CASE("escape — each special character individually") {
    CHECK(escape_str("&")  == "&amp;");
    CHECK(escape_str("<")  == "&lt;");
    CHECK(escape_str(">")  == "&gt;");
    CHECK(escape_str("\"") == "&quot;");
    CHECK(escape_str("'")  == "&#39;");
}

TEST_CASE("escape — mixed content") {
    CHECK(escape_str("a & b < c > d") == "a &amp; b &lt; c &gt; d");
    CHECK(escape_str("\"quoted\"")     == "&quot;quoted&quot;");
    CHECK(escape_str("it's alive")     == "it&#39;s alive");
    CHECK(escape_str("&<>\"'")         == "&amp;&lt;&gt;&quot;&#39;");
}

TEST_CASE("escape — all five entities back-to-back") {
    CHECK(escape_str("<<<<") == "&lt;&lt;&lt;&lt;");
    CHECK(escape_str("&&&&") == "&amp;&amp;&amp;&amp;");
    CHECK(escape_str(">>>>") == "&gt;&gt;&gt;&gt;");
    CHECK(escape_str("\"\"") == "&quot;&quot;");
    CHECK(escape_str("''''") == "&#39;&#39;&#39;&#39;");
}

TEST_CASE("escape — XSS payloads neutralised") {
    SUBCASE("script tag") {
        CHECK(escape_str("<script>alert(1)</script>")
              == "&lt;script&gt;alert(1)&lt;/script&gt;");
    }
    SUBCASE("img onerror") {
        CHECK(escape_str("<img src=x onerror=alert(1)>")
              == "&lt;img src=x onerror=alert(1)&gt;");
    }
    SUBCASE("double-quote breakout attempt") {
        // If injected into an attribute value, `"` would close the value.
        CHECK(escape_str("\" onmouseover=\"evil\"")
              == "&quot; onmouseover=&quot;evil&quot;");
    }
    SUBCASE("single-quote breakout attempt") {
        CHECK(escape_str("' onmouseover='evil'")
              == "&#39; onmouseover=&#39;evil&#39;");
    }
    SUBCASE("tag close injection via >") {
        CHECK(escape_str("> <script>evil()</script>")
              == "&gt; &lt;script&gt;evil()&lt;/script&gt;");
    }
    SUBCASE("polyglot") {
        CHECK(escape_str("\"><svg/onload=alert(1)>")
              == "&quot;&gt;&lt;svg/onload=alert(1)&gt;");
    }
}

TEST_CASE("escape — UTF-8 / unicode passthrough") {
    // Non-ASCII bytes are not special; they flow through unchanged.
    std::string emoji = "\xF0\x9F\x98\x80"; // U+1F600
    CHECK(escape_str(emoji)          == emoji);
    CHECK(escape_str("Ünïcödé")      == "Ünïcödé");
    CHECK(escape_str("\xE2\x80\x94") == "\xE2\x80\x94"); // em dash
}

TEST_CASE("escape — null byte passes through unmodified") {
    std::string_view content{"ab\x00""cd", 5};
    std::ostringstream oss;
    htpp::escape(oss, content);
    std::string result = oss.str();
    REQUIRE(result.size() == 5);
    CHECK(result[0] == 'a');
    CHECK(result[1] == 'b');
    CHECK(result[2] == '\0');
    CHECK(result[3] == 'c');
    CHECK(result[4] == 'd');
}

// ─── attr_set (static-name attribute) ────────────────────────────────────────

TEST_CASE("attr_set — basic format") {
    std::ostringstream oss;
    oss << (class_ = "foo bar");
    CHECK(oss.str() == R"( class="foo bar")");
}

TEST_CASE("attr_set — has leading space") {
    std::ostringstream oss;
    oss << (id = "x");
    CHECK(oss.str()[0] == ' ');
}

TEST_CASE("attr_set — empty value") {
    std::ostringstream oss;
    oss << (alt = "");
    CHECK(oss.str() == R"( alt="")");
}

TEST_CASE("attr_set — value escaping") {
    SUBCASE("double-quote prevents attribute breakout") {
        std::ostringstream oss;
        oss << (value = "say \"hi\"");
        CHECK(oss.str() == R"( value="say &quot;hi&quot;")");
    }
    SUBCASE("single-quote") {
        std::ostringstream oss;
        oss << (placeholder = "it's here");
        CHECK(oss.str() == R"( placeholder="it&#39;s here")");
    }
    SUBCASE("ampersand in URL") {
        std::ostringstream oss;
        oss << (href = "/a?x=1&y=2");
        CHECK(oss.str() == R"( href="/a?x=1&amp;y=2")");
    }
    SUBCASE("angle brackets") {
        std::ostringstream oss;
        oss << (title = "a<b>c");
        CHECK(oss.str() == R"( title="a&lt;b&gt;c")");
    }
    SUBCASE("full XSS payload cannot break out of attribute") {
        std::ostringstream oss;
        oss << (class_ = "\"><script>alert(1)</script>");
        std::string s = oss.str();
        CHECK(s.find("<script>")  == std::string::npos);
        CHECK(s.find("&quot;&gt;") != std::string::npos);
    }
}

TEST_CASE("attr_set — hyphenated rendered name (http-equiv)") {
    std::ostringstream oss;
    oss << (http_equiv = "refresh");
    CHECK(oss.str() == R"( http-equiv="refresh")");
}

TEST_CASE("attr_set — aria attribute renders with aria- prefix") {
    std::ostringstream oss;
    oss << (aria_label = "close");
    CHECK(oss.str() == R"( aria-label="close")");
}

// ─── attr_bool_t ─────────────────────────────────────────────────────────────

TEST_CASE("attr_bool_t — bare name with leading space") {
    {
        std::ostringstream oss;
        oss << disabled;
        CHECK(oss.str() == " disabled");
    }
    {
        std::ostringstream oss;
        oss << checked;
        CHECK(oss.str() == " checked");
    }
    {
        std::ostringstream oss;
        oss << required;
        CHECK(oss.str() == " required");
    }
    {
        std::ostringstream oss;
        oss << readonly;
        CHECK(oss.str() == " readonly");
    }
}

// ─── validate_attr_name ───────────────────────────────────────────────────────

TEST_CASE("validate_attr_name — valid names") {
    CHECK_NOTHROW(htpp::validate_attr_name("a"));
    CHECK_NOTHROW(htpp::validate_attr_name("Z"));
    CHECK_NOTHROW(htpp::validate_attr_name("_private"));
    CHECK_NOTHROW(htpp::validate_attr_name("data-value"));
    CHECK_NOTHROW(htpp::validate_attr_name("aria-label"));
    CHECK_NOTHROW(htpp::validate_attr_name("xml:lang"));
    CHECK_NOTHROW(htpp::validate_attr_name("v1"));
    CHECK_NOTHROW(htpp::validate_attr_name("A-Za-z0-9")); // hyphens ok in continuation
    CHECK_NOTHROW(htpp::validate_attr_name("hx-post"));
    CHECK_NOTHROW(htpp::validate_attr_name("v-bind.prop"));
    CHECK_NOTHROW(htpp::validate_attr_name("onclick"));   // valid name; value is still escaped
}

TEST_CASE("validate_attr_name — invalid: empty") {
    CHECK_THROWS_AS(htpp::validate_attr_name(""), std::invalid_argument);
}

TEST_CASE("validate_attr_name — invalid: bad start character") {
    CHECK_THROWS_AS(htpp::validate_attr_name("0foo"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("1abc"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("-foo"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name(" foo"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("\"foo"), std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name(">foo"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("<foo"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("/foo"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("=foo"),  std::invalid_argument);
}

TEST_CASE("validate_attr_name — invalid: bad continuation characters") {
    CHECK_THROWS_AS(htpp::validate_attr_name("a b"),  std::invalid_argument); // space — injection vector
    CHECK_THROWS_AS(htpp::validate_attr_name("a=b"),  std::invalid_argument); // equals — attribute injection
    CHECK_THROWS_AS(htpp::validate_attr_name("a\"b"), std::invalid_argument); // quote
    CHECK_THROWS_AS(htpp::validate_attr_name("a>b"),  std::invalid_argument); // tag close
    CHECK_THROWS_AS(htpp::validate_attr_name("a<b"),  std::invalid_argument); // tag open
    CHECK_THROWS_AS(htpp::validate_attr_name("a/b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a!b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a@b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a#b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a$b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a%b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a^b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a&b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a*b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a(b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a)b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a+b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a,b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a;b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a'b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a`b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a~b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a{b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a}b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a[b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a]b"),  std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a\\b"), std::invalid_argument);
    CHECK_THROWS_AS(htpp::validate_attr_name("a|b"),  std::invalid_argument);
}

// ─── attr_dyn ────────────────────────────────────────────────────────────────

TEST_CASE("attr_dyn — valid names render correctly") {
    SUBCASE("simple hyphenated name") {
        std::ostringstream oss;
        oss << ("data-foo"_a = "bar");
        CHECK(oss.str() == R"( data-foo="bar")");
    }
    SUBCASE("aria attribute via UDL") {
        std::ostringstream oss;
        oss << ("aria-label"_a = "close");
        CHECK(oss.str() == R"( aria-label="close")");
    }
    SUBCASE("xml:lang with colon") {
        std::ostringstream oss;
        oss << ("xml:lang"_a = "en");
        CHECK(oss.str() == R"( xml:lang="en")");
    }
    SUBCASE("value is HTML-escaped") {
        std::ostringstream oss;
        oss << ("data-msg"_a = "<b>hi</b>");
        CHECK(oss.str() == R"( data-msg="&lt;b&gt;hi&lt;/b&gt;")");
    }
    SUBCASE("value double-quote escaped") {
        std::ostringstream oss;
        oss << ("data-x"_a = "say \"hi\"");
        CHECK(oss.str() == R"( data-x="say &quot;hi&quot;")");
    }
    SUBCASE("empty value") {
        std::ostringstream oss;
        oss << ("data-empty"_a = "");
        CHECK(oss.str() == R"( data-empty="")");
    }
}

TEST_CASE("attr_dyn — invalid names throw std::invalid_argument") {
    SUBCASE("empty name") {
        std::ostringstream oss;
        CHECK_THROWS_AS((oss << htpp::attr_dyn{.name="",      .value="x"}),
                        std::invalid_argument);
    }
    SUBCASE("starts with digit") {
        std::ostringstream oss;
        CHECK_THROWS_AS((oss << htpp::attr_dyn{.name="1foo",  .value="x"}),
                        std::invalid_argument);
    }
    SUBCASE("starts with hyphen") {
        std::ostringstream oss;
        CHECK_THROWS_AS((oss << htpp::attr_dyn{.name="-foo",  .value="x"}),
                        std::invalid_argument);
    }
    SUBCASE("space in name (attribute injection)") {
        std::ostringstream oss;
        // Would produce ` foo bar="x"` — injecting a second attribute.
        CHECK_THROWS_AS((oss << htpp::attr_dyn{.name="foo bar", .value="x"}),
                        std::invalid_argument);
        CHECK(oss.str().empty()); // nothing emitted before the throw
    }
    SUBCASE("equals in name (attribute injection)") {
        std::ostringstream oss;
        CHECK_THROWS_AS((oss << htpp::attr_dyn{.name="fo=o",  .value="x"}),
                        std::invalid_argument);
    }
    SUBCASE("double-quote in name") {
        std::ostringstream oss;
        CHECK_THROWS_AS((oss << htpp::attr_dyn{.name="fo\"o", .value="x"}),
                        std::invalid_argument);
    }
    SUBCASE("greater-than in name (tag-close injection attempt)") {
        std::ostringstream oss;
        CHECK_THROWS_AS((oss << htpp::attr_dyn{.name="fo>o",  .value="x"}),
                        std::invalid_argument);
    }
    SUBCASE("full breakout attempt as name") {
        // Attacker-controlled attr name: `"> <script>`
        std::ostringstream oss;
        CHECK_THROWS_AS((oss << htpp::attr_dyn{.name="\"><script>", .value="x"}),
                        std::invalid_argument);
        CHECK(oss.str().empty());
    }
    SUBCASE("null byte in name") {
        std::ostringstream oss;
        std::string_view nul_name{"fo\x00""o", 4};
        CHECK_THROWS_AS((oss << htpp::attr_dyn{.name=nul_name, .value="x"}),
                        std::invalid_argument);
    }
}

// ─── attr_if ─────────────────────────────────────────────────────────────────

TEST_CASE("attr_if — bool attribute: emits when true, silent when false") {
    {
        std::ostringstream oss;
        oss << attr_if(true, disabled);
        CHECK(oss.str() == " disabled");
    }
    {
        std::ostringstream oss;
        oss << attr_if(false, disabled);
        CHECK(oss.str() == "");
    }
}

TEST_CASE("attr_if — wrapping attr_set") {
    {
        std::ostringstream oss;
        oss << attr_if(true, class_ = "active");
        CHECK(oss.str() == R"( class="active")");
    }
    {
        std::ostringstream oss;
        oss << attr_if(false, class_ = "active");
        CHECK(oss.str() == "");
    }
}

TEST_CASE("attr_if — wrapping attr_dyn") {
    {
        std::ostringstream oss;
        oss << attr_if(true, "data-foo"_a = "bar");
        CHECK(oss.str() == R"( data-foo="bar")");
    }
    {
        std::ostringstream oss;
        oss << attr_if(false, "data-foo"_a = "bar");
        CHECK(oss.str() == "");
    }
}

// ─── tag ─────────────────────────────────────────────────────────────────────

TEST_CASE("tag — no attributes") {
    std::ostringstream oss;
    { htpp::tag t(oss, "div"); }
    CHECK(oss.str() == "<div></div>");
}

TEST_CASE("tag — one attribute") {
    std::ostringstream oss;
    { htpp::tag t(oss, "span", class_ = "bold"); }
    CHECK(oss.str() == R"(<span class="bold"></span>)");
}

TEST_CASE("tag — multiple attributes in order") {
    std::ostringstream oss;
    { htpp::tag t(oss, "a", href = "/go", class_ = "link", target = "_blank"); }
    CHECK(oss.str() == R"(<a href="/go" class="link" target="_blank"></a>)");
}

TEST_CASE("tag — RAII closes tag on scope exit") {
    std::ostringstream oss;
    oss << "before:";
    {
        htpp::tag t(oss, "p");
        oss << "inside";
    }
    oss << ":after";
    CHECK(oss.str() == "before:<p>inside</p>:after");
}

TEST_CASE("tag — nested tags produce correct nesting") {
    std::ostringstream oss;
    {
        htpp::tag outer(oss, "div");
        {
            htpp::tag inner(oss, "span");
            oss << "text";
        }
    }
    CHECK(oss.str() == "<div><span>text</span></div>");
}

TEST_CASE("tag — attribute value injection is escaped") {
    std::ostringstream oss;
    { htpp::tag t(oss, "div", class_ = "\" onclick=\"evil"); }
    std::string s = oss.str();
    // The " is escaped to &quot;, preserving the attribute boundary.
    // onclick appears in escaped content only — not as a real attribute.
    CHECK(s.find("&quot;")  != std::string::npos);
    CHECK(s == R"(<div class="&quot; onclick=&quot;evil"></div>)");
}

TEST_CASE("tag — bool attribute in pack") {
    std::ostringstream oss;
    { htpp::tag t(oss, "details", open); }
    CHECK(oss.str() == "<details open></details>");
}

TEST_CASE("tag — mixed static, bool, dynamic, and conditional attributes") {
    std::ostringstream oss;
    {
        htpp::tag t(oss, "button",
                    type = "submit",
                    class_ = "btn",
                    disabled,
                    "data-action"_a = "save",
                    attr_if(false, aria_hidden = "true"),
                    attr_if(true,  aria_label  = "Save document"));
    }
    CHECK(oss.str() ==
          R"(<button type="submit" class="btn" disabled data-action="save" aria-label="Save document"></button>)");
}

// ─── void_tag ────────────────────────────────────────────────────────────────

TEST_CASE("void_tag — no attributes") {
    std::ostringstream oss;
    htpp::void_tag{oss, "br"};
    CHECK(oss.str() == "<br />");
}

TEST_CASE("void_tag — with attributes") {
    std::ostringstream oss;
    htpp::void_tag{oss, "input", type = "text", name = "q", required};
    CHECK(oss.str() == R"(<input type="text" name="q" required />)");
}

TEST_CASE("void_tag — XSS in alt attribute is escaped") {
    std::ostringstream oss;
    htpp::void_tag{oss, "img", src = "x.png", alt = "<script>alert(1)</script>"};
    std::string s = oss.str();
    CHECK(s.find("<script>")         == std::string::npos);
    CHECK(s.find("&lt;script&gt;")   != std::string::npos);
}

// ─── doctype ─────────────────────────────────────────────────────────────────

TEST_CASE("doctype — correct string including newline") {
    std::ostringstream oss;
    htpp::doctype{oss};
    CHECK(oss.str() == "<!DOCTYPE html>\n");
}

// ─── raw ─────────────────────────────────────────────────────────────────────

TEST_CASE("raw — passes through without escaping") {
    std::ostringstream oss;
    oss << htpp::raw{"<b>trusted</b>"};
    CHECK(oss.str() == "<b>trusted</b>");
}

TEST_CASE("raw — angle brackets are NOT escaped") {
    std::ostringstream oss;
    oss << htpp::raw{"<em>&amp;</em>"};
    CHECK(oss.str() == "<em>&amp;</em>");
}

// ─── HT_* macros ─────────────────────────────────────────────────────────────

TEST_CASE("HT_DOCTYPE") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_DOCTYPE();
    CHECK(oss.str() == "<!DOCTYPE html>\n");
}

TEST_CASE("HT_DIV — empty body") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_DIV() {}
    CHECK(oss.str() == "<div></div>");
}

TEST_CASE("HT_DIV — with attributes and content") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_DIV(class_ = "container", id = "main") { os << "body"; }
    CHECK(oss.str() == R"(<div class="container" id="main">body</div>)");
}

TEST_CASE("HT_SPAN nested in HT_DIV") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_DIV() {
        HT_SPAN() { os << "inner"; }
    }
    CHECK(oss.str() == "<div><span>inner</span></div>");
}

TEST_CASE("HT_TEXT — escapes content") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_P() { HT_TEXT("<b>bold</b> & more"); }
    CHECK(oss.str() == "<p>&lt;b&gt;bold&lt;/b&gt; &amp; more</p>");
}

TEST_CASE("HT_TEXT — XSS via user-controlled content") {
    std::ostringstream oss;
    std::ostream& os = oss;
    std::string user_input = "<img src=x onerror=alert(document.cookie)>";
    HT_DIV() { HT_TEXT(user_input); }
    std::string s = oss.str();
    // < and > are escaped so the browser cannot parse <img ...> as a tag.
    CHECK(s.find("<img")    == std::string::npos); // no raw tag open
    CHECK(s.find("&lt;img") != std::string::npos); // escaped form present
    CHECK(s.find("&gt;")    != std::string::npos); // closing > escaped too
}

TEST_CASE("HT_RAW — passes through without escaping") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_DIV() { HT_RAW("<em>already safe</em>"); }
    CHECK(oss.str() == "<div><em>already safe</em></div>");
}

TEST_CASE("HT_BR — void element") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_BR();
    CHECK(oss.str() == "<br />");
}

TEST_CASE("HT_HR — void element") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_HR();
    CHECK(oss.str() == "<hr />");
}

TEST_CASE("HT_INPUT — with boolean and value attributes") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_INPUT(type = "checkbox", name = "agree", checked);
    CHECK(oss.str() == R"(<input type="checkbox" name="agree" checked />)");
}

TEST_CASE("HT_INPUT — all form attributes") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_INPUT(type = "text", name = "q", id = "search",
             placeholder = "Search...", required, autofocus);
    CHECK(oss.str() ==
          R"(<input type="text" name="q" id="search" placeholder="Search..." required autofocus />)");
}

TEST_CASE("HT_A — href with query string ampersand escaped") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_A(href = "/search?q=a&b=c") { os << "link"; }
    CHECK(oss.str() == R"(<a href="/search?q=a&amp;b=c">link</a>)");
}

TEST_CASE("HT_DETAILS — bool open attribute") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_DETAILS(open) {
        HT_SUMMARY() { os << "header"; }
        os << "body";
    }
    CHECK(oss.str() == "<details open><summary>header</summary>body</details>");
}

TEST_CASE("HT_TEXTAREA — empty body (requires explicit {})") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_TEXTAREA(id = "msg", rows = "4") {}
    CHECK(oss.str() == R"(<textarea id="msg" rows="4"></textarea>)");
}

TEST_CASE("HT_BUTTON — htmx attributes with hx- prefix") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_BUTTON(type = "submit", hx_post = "/save", hx_target = "#result") {
        os << "Save";
    }
    CHECK(oss.str() ==
          R"(<button type="submit" hx-post="/save" hx-target="#result">Save</button>)");
}

TEST_CASE("HT_META — http-equiv with hyphenated rendered name") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_META(http_equiv = "refresh", content = "30");
    CHECK(oss.str() == R"(<meta http-equiv="refresh" content="30" />)");
}

TEST_CASE("HT_IMG — src and alt rendered") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_IMG(src = "/photo.jpg", alt = "A cat", width = "200", height = "150");
    CHECK(oss.str() == R"(<img src="/photo.jpg" alt="A cat" width="200" height="150" />)");
}

TEST_CASE("HT_TABLE — thead / tbody / tr / th / td") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_TABLE() {
        HT_THEAD() {
            HT_TR() {
                HT_TH() { os << "Name"; }
                HT_TH() { os << "Role"; }
            }
        }
        HT_TBODY() {
            HT_TR() {
                HT_TD() { os << "Alice"; }
                HT_TD() { os << "Admin"; }
            }
        }
    }
    CHECK(oss.str() ==
          "<table><thead><tr><th>Name</th><th>Role</th></tr></thead>"
          "<tbody><tr><td>Alice</td><td>Admin</td></tr></tbody></table>");
}

TEST_CASE("HT_UL / HT_LI") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_UL() {
        HT_LI() { os << "one"; }
        HT_LI() { os << "two"; }
    }
    CHECK(oss.str() == "<ul><li>one</li><li>two</li></ul>");
}

// ─── HT_COMPONENT ─────────────────────────────────────────────────────────────

HT_COMPONENT(test_button, std::string_view label, bool busy) {
    HT_BUTTON(type = "button", attr_if(busy, disabled)) {
        HT_TEXT(label);
    }
}

TEST_CASE("HT_COMPONENT — not busy: no disabled attribute") {
    std::ostringstream oss;
    std::ostream& os = oss;
    test_button(os, "Submit", false);
    CHECK(oss.str() == R"(<button type="button">Submit</button>)");
}

TEST_CASE("HT_COMPONENT — busy: disabled attribute present") {
    std::ostringstream oss;
    std::ostream& os = oss;
    test_button(os, "Submit", true);
    CHECK(oss.str() == R"(<button type="button" disabled>Submit</button>)");
}

TEST_CASE("HT_COMPONENT — XSS in label is escaped") {
    std::ostringstream oss;
    std::ostream& os = oss;
    test_button(os, "<script>steal()</script>", false);
    std::string s = oss.str();
    CHECK(s.find("<script>")        == std::string::npos);
    CHECK(s.find("&lt;script&gt;")  != std::string::npos);
}

// ─── Security: deeper injection scenarios ────────────────────────────────────

TEST_CASE("security — script tag as text content is fully neutralised") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_DIV() { HT_TEXT("<script>alert('xss')</script>"); }
    CHECK(oss.str() ==
          "<div>&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;</div>");
}

TEST_CASE("security — closing-tag injection in text content cannot break nesting") {
    std::ostringstream oss;
    std::ostream& os = oss;
    std::string attack = "</div><script>evil()</script><div>";
    HT_DIV(id = "safe") { HT_TEXT(attack); }
    std::string s = oss.str();
    // Content must be enclosed within our div; no raw injection tag present.
    CHECK(s.substr(s.size() - 6) == "</div>");
    CHECK(s.find("</div><script>") == std::string::npos);
}

TEST_CASE("security — double-quote in attribute value cannot inject new attribute") {
    std::ostringstream oss;
    std::ostream& os = oss;
    std::string attack = R"(" onmouseover="alert(1))";
    HT_INPUT(value = attack);
    std::string s = oss.str();
    // The " is escaped to &quot;, so the attribute boundary is preserved.
    // onmouseover appears in the output but as escaped content, not a real attribute.
    CHECK(s.find("&quot;") != std::string::npos);
    CHECK(s == "<input value=\"&quot; onmouseover=&quot;alert(1)\" />");
}

TEST_CASE("security — angle bracket in attribute value cannot close tag") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_DIV(class_ = "x > y") {}
    CHECK(oss.str() == R"(<div class="x &gt; y"></div>)");
}

TEST_CASE("security — style attribute with embedded single quotes") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_DIV(style = "background: url('img.png')") {}
    std::string s = oss.str();
    CHECK(s.find("&#39;") != std::string::npos);
    CHECK(s.find("'")     == std::string::npos);
}

TEST_CASE("security — dynamic attr name injection blocked before any output") {
    std::ostringstream oss;
    auto bad = htpp::attr_dyn{.name = "x onmouseover", .value = "evil()"};
    CHECK_THROWS_AS(oss << bad, std::invalid_argument);
    CHECK(oss.str().empty()); // stream must be unmodified
}

TEST_CASE("security — valid dynamic attr name still escapes the value") {
    std::ostringstream oss;
    oss << ("data-payload"_a = "<script>evil()</script>");
    CHECK(oss.str() == R"( data-payload="&lt;script&gt;evil()&lt;/script&gt;")");
}

TEST_CASE("security — event handler via _a UDL: value is HTML-escaped") {
    // onclick is a valid attr name. Using _a makes the choice explicit.
    // HTML-escaping is applied, so < and > in the value are neutralised.
    // Note: HTML escaping is NOT JS escaping — do not rely on this for safety
    // in a JS context; the library documents this limitation.
    std::ostringstream oss;
    oss << ("onclick"_a = "alert('<xss>')");
    CHECK(oss.str() == " onclick=\"alert(&#39;&lt;xss&gt;&#39;)\"");
}

TEST_CASE("security — href with javascript: scheme is not filtered (documented behaviour)") {
    // URL scheme filtering is the caller's responsibility, not htpp's.
    // This test documents that javascript: passes through to the output.
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_A(href = "javascript:alert(1)") { os << "click"; }
    CHECK(oss.str() == "<a href=\"javascript:alert(1)\">click</a>");
}

TEST_CASE("security — CRLF in attribute value stays inside quotes (documented)") {
    // CR and LF are not in the escape set. Inside double-quoted attributes,
    // newlines are legal per HTML and do not escape the attribute.
    std::ostringstream oss;
    oss << (value = "foo\r\nbar");
    CHECK(oss.str() == " value=\"foo\r\nbar\"");
}

TEST_CASE("security — user data in table cells escaped via HT_TEXT") {
    std::ostringstream oss;
    std::ostream& os = oss;
    std::string user_data = "<script>alert(1)</script>";
    HT_TABLE() {
        HT_TBODY() {
            HT_TR() {
                HT_TD() { HT_TEXT(user_data); }
            }
        }
    }
    std::string s = oss.str();
    CHECK(s.find("<script>")       == std::string::npos);
    CHECK(s.find("&lt;script&gt;") != std::string::npos);
}

TEST_CASE("security — HT_RAW trusts caller: no escaping performed") {
    // HT_RAW is intentionally unsafe — the caller asserts the content is trusted.
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_DIV() { HT_RAW("<script>var x = 1;</script>"); }
    CHECK(oss.str() == "<div><script>var x = 1;</script></div>");
}

// ─── Edge cases ───────────────────────────────────────────────────────────────

TEST_CASE("edge — deeply nested tags") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_DIV() {
        HT_DIV() {
            HT_DIV() {
                HT_SPAN() { HT_TEXT("deep"); }
            }
        }
    }
    CHECK(oss.str() == "<div><div><div><span>deep</span></div></div></div>");
}

TEST_CASE("edge — empty string attribute value") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_DIV(class_ = "") {}
    CHECK(oss.str() == R"(<div class=""></div>)");
}

TEST_CASE("edge — attr_if false in mixed attr pack emits nothing extra") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_BUTTON(class_ = "btn",
              attr_if(false, disabled),
              attr_if(false, "data-busy"_a = "1"),
              type = "submit") {
        os << "Go";
    }
    CHECK(oss.str() == R"(<button class="btn" type="submit">Go</button>)");
}

TEST_CASE("edge — attr_if true in mixed attr pack emits inner attribute") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_BUTTON(class_ = "btn",
              attr_if(true, disabled),
              type = "button") {
        os << "Wait";
    }
    CHECK(oss.str() == R"(<button class="btn" disabled type="button">Wait</button>)");
}

TEST_CASE("edge — UDL key object reusable") {
    auto key = "data-action"_a;
    {
        std::ostringstream oss;
        oss << (key = "save");
        CHECK(oss.str() == R"( data-action="save")");
    }
    {
        std::ostringstream oss;
        oss << (key = "delete");
        CHECK(oss.str() == R"( data-action="delete")");
    }
}

TEST_CASE("edge — tag renders name containing only ASCII letters") {
    // Smoke test for a custom tag name via the low-level API.
    std::ostringstream oss;
    { htpp::tag t(oss, "custom-element"); }
    CHECK(oss.str() == "<custom-element></custom-element>");
}

TEST_CASE("edge — multiple sibling tags closed in reverse order") {
    std::ostringstream oss;
    {
        htpp::tag a(oss, "a");
        oss << "1";
    }
    {
        htpp::tag b(oss, "b");
        oss << "2";
    }
    CHECK(oss.str() == "<a>1</a><b>2</b>");
}

TEST_CASE("edge — for_ renders as for (keyword collision)") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_LABEL(for_ = "email") { os << "Email"; }
    CHECK(oss.str() == R"(<label for="email">Email</label>)");
}

TEST_CASE("edge — default_ bool attribute renders as default") {
    std::ostringstream oss;
    oss << default_;
    CHECK(oss.str() == " default");
}

TEST_CASE("edge — loop bool attribute (keyword collision)") {
    std::ostringstream oss;
    oss << loop;
    CHECK(oss.str() == " loop");
}

TEST_CASE("edge — async and defer bool attributes") {
    std::ostringstream oss;
    std::ostream& os = oss;
    HT_SCRIPT(src = "/app.js", async, defer) {}
    CHECK(oss.str() == R"(<script src="/app.js" async defer></script>)");
}

TEST_CASE("edge — void_tag with no attributes produces self-close") {
    std::ostringstream oss;
    htpp::void_tag{oss, "hr"};
    CHECK(oss.str() == "<hr />");
}

TEST_CASE("edge — iterating over data with HT_TEXT in loop") {
    std::ostringstream oss;
    std::ostream& os = oss;
    const char* items[] = {"one", "two & three", "<four>"};
    HT_UL() {
        for (auto item : items) {
            HT_LI() { HT_TEXT(item); }
        }
    }
    CHECK(oss.str() ==
          "<ul><li>one</li><li>two &amp; three</li><li>&lt;four&gt;</li></ul>");
}

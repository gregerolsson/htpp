// example.cpp — htpp.hpp with predeclared attribute keys + `_a` UDL fallback.
// Compile: clang++ -std=c++23 -Wall -Wextra -Wpedantic -o example example.cpp

#include "htpp.hpp"
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace htpp::attr;            // class_, id, href, aria_label, ...
using namespace htpp::attr_literals;   // "data-x"_a = "..."

// ---------------------------------------------------------------------------
// Reusable components — plain functions; `os` is the first parameter.
// Attributes can appear in any order at the call site.
// ---------------------------------------------------------------------------

HT_COMPONENT(nav_link, std::string_view url, std::string_view text) {
    HT_A(class_ = "px-3 py-2 hover:underline", href = url) {
        HT_TEXT(text);
    }
}

HT_COMPONENT(card, std::string_view heading, std::string_view body) {
    HT_DIV(class_ = "rounded shadow p-4 bg-white") {
        HT_H2(class_ = "text-lg font-bold mb-2") { HT_TEXT(heading); }
        HT_P (class_ = "text-gray-600")          { HT_TEXT(body);    }
    }
}

HT_COMPONENT(user_table,
             const std::vector<std::pair<std::string, std::string>>& users) {
    HT_TABLE(class_ = "w-full border-collapse text-left") {
        HT_THEAD() {
            HT_TR(class_ = "bg-gray-100") {
                HT_TH(class_ = "p-2 border") { os << "Name"; }
                HT_TH(class_ = "p-2 border") { os << "Role"; }
            }
        }
        HT_TBODY() {
            for (auto& [user_name, user_role] : users) {
                HT_TR(class_ = "hover:bg-gray-50") {
                    HT_TD(class_ = "p-2 border") { HT_TEXT(user_name); }
                    HT_TD(class_ = "p-2 border") { HT_TEXT(user_role); }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Full page template
// ---------------------------------------------------------------------------

HT_COMPONENT(page, const std::string& username, bool is_submitting) {
    std::vector<std::pair<std::string, std::string>> users = {
        {"Alice",                "Admin"},
        {"Bob",                  "Editor"},
        {"<script>xss</script>", "Attacker"},   // safely escaped by HT_TEXT
    };

    HT_DOCTYPE();
    HT_HTML(lang = "en") {
        HT_HEAD() {
            HT_META(charset = "UTF-8");
            HT_META(name = "viewport", content = "width=device-width, initial-scale=1");
            HT_TITLE() { os << "htpp demo"; }
            HT_LINK(rel = "stylesheet", href = "https://cdn.tailwindcss.com");
        }
        HT_BODY(class_ = "bg-gray-50 font-sans") {

            HT_HEADER(class_ = "bg-blue-600 text-white p-4 flex gap-4") {
                nav_link(os, "/",       "Home");
                nav_link(os, "/about",  "About");
                nav_link(os, "/logout", "Logout");
            }

            HT_MAIN(class_ = "max-w-3xl mx-auto mt-8 px-4 space-y-6") {

                HT_H1(class_ = "text-2xl font-bold") {
                    os << "Welcome, ";
                    HT_TEXT(username);
                    os << "!";
                }

                HT_DIV(class_ = "grid grid-cols-2 gap-4") {
                    card(os, "Posts",    "You have 12 published posts.");
                    card(os, "Comments", "3 comments need moderation.");
                }

                HT_SECTION() {
                    HT_H2(class_ = "text-xl font-semibold mb-3") { os << "User List"; }
                    user_table(os, users);
                }

                HT_SECTION(class_ = "mt-6") {
                    HT_H2(class_ = "text-xl font-semibold mb-3") { os << "Send a message"; }
                    HT_FORM(action = "/send", class_ = "space-y-3", method = "post") {
                        HT_LABEL(class_ = "block font-medium", for_ = "msg") {
                            os << "Message";
                        }
                        HT_TEXTAREA(class_ = "w-full border rounded p-2",
                                    id = "msg",
                                    name = "message",
                                    rows = "4") {}
                        HT_BUTTON(class_           = "bg-blue-600 text-white px-4 py-2 rounded",
                                  type             = "submit",
                                  aria_label       = "Submit the message",
                                  "data-action"_a  = "send",
                                  attr_if(is_submitting, disabled)) {
                            os << "Send";
                        }
                    }
                }

                HT_SECTION(class_ = "mt-6 text-sm text-gray-600") {
                    HT_LABEL(for_ = "notify") {
                        HT_INPUT(type = "checkbox", id = "notify", name = "notify", checked);
                        os << " Email me about replies";
                    }
                }
            }
        }
    }
}

auto main() -> int {
    page(std::cout, "Alice & \"friends\"", /*is_submitting=*/false);
    std::cout << '\n';
    return 0;
}

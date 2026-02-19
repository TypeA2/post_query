#ifndef PARSER_H
#define PARSER_H

#include "encoding.h"
#include "ast.h"

#include <string>
#include <vector>
#include <memory>
#include <span>
#include <string_view>
#include <array>

extern VALUE post_query_err;

namespace post_query {
    class parser {
        private:
        std::vector<std::string> _metatags;

        static constexpr std::array<std::string_view, 6> unbalanced_tags {{
            ":)", ":(", ";)", ";(", ">:)", ">:("
        }};

        public:
        parser(std::vector<std::string> metatags)
            : _metatags { std::move(metatags) } {

        }

        ast_ptr parse(std::string_view query) {
            parser_impl impl { *this, query };

            ast_ptr res = impl.parse();

            if (!res) {
                return ast::make_none();
            } else if (!impl.eof()) {
                rb_warn(
                    // post_query_err,
                    "parser did not reach eof, parsed: \"%s\", remaining: \"%s\"",
                    res->to_infix().c_str(),
                    std::string{ impl.remaining() }.c_str()
                );
            }

            if (impl.unclosed_parens != 0) {
                rb_raise(post_query_err, "%zu unclosed parantheses remain", impl.unclosed_parens);
            }

            return res;
        }

        std::span<const std::string> metatags() const {
            return _metatags;
        }

        private:
        struct parser_impl {
            ::post_query::parser& parser;
            std::string_view input;
            std::string_view::iterator cur;
            std::string_view::iterator end;
            ssize_t unclosed_parens = 0;

            parser_impl(::post_query::parser& parser, std::string_view input)
                : parser { parser }, input { input }
                , cur { input.begin() }, end { input.end() } {

            }

            bool eof() {
                return cur == end;
            }

            std::string_view remaining() const {
                return { cur, end };
            }

            ast_ptr parse() {
                /**
                 * root         = or_clause [root]
                 *  -> one or more or clauses
                 * or_clause    = and_clause "or" or_clause | and_clause
                 * and_clause   = factor_list "and" and_clause | factor_list
                 * factor_list  = factor [factor_list]
                 * factor       = "-" expr | "~" expr | expr
                 * expr         = "(" or_clause ")" | term
                 * term         = metatag | tag | wildcard
                 */
                std::vector<ast_ptr> clauses;
                // Zero or more or-clauses
                for (;;) {
                    auto next = backtrack(&parser_impl::or_clause);
                    if (next) {
                        clauses.emplace_back(std::move(next));
                    } else {
                        break;
                    }
                }

                consume_spaces();

                if (clauses.size() == 0) {
                    return ast::make_all();
                } else if (clauses.size() == 1) {
                    return std::move(clauses.front());
                } else {
                    return ast::make_and(std::move(clauses));
                }
            }

            ast_ptr or_clause() {
                auto a = and_clause();
                consume_spaces();

                if (accept("or ")) {
                    consume_spaces();

                    std::vector<ast_ptr> children;
                    children.emplace_back(std::move(a));
                    children.emplace_back(or_clause());
                    return ast::make_or(std::move(children));
                } else {
                    return a;
                }
            }

            ast_ptr and_clause() {
                auto a = factor_list();
                consume_spaces();

                if (accept("and ")) {
                    consume_spaces();

                    std::vector<ast_ptr> children;
                    children.emplace_back(std::move(a));
                    children.emplace_back(and_clause());
                    return ast::make_and(std::move(children));
                } else {
                    return a;
                }
            }

            ast_ptr factor_list() {
                std::vector<ast_ptr> clauses;
                
                // One or more factors
                clauses.emplace_back(factor());

                if (!clauses.back()) {
                    return nullptr;
                }

                for (;;) {
                    auto next = backtrack(&parser_impl::factor);
                    if (next) {
                        clauses.emplace_back(std::move(next));
                    } else {
                        break;
                    }
                }

                return ast::make_and(std::move(clauses));
            }

            ast_ptr factor() {
                consume_spaces();

                if (eof()) {
                    return nullptr;
                } else if (*cur == '-') {
                    ++cur;
                    return ast::make_not(expr());
                } else if (*cur == '~') {
                    ++cur;
                    return ast::make_opt(expr());
                } else {
                    // Normal expression
                    return expr();
                }
            }

            ast_ptr expr() {
                consume_spaces();

                if (eof()) {
                    return nullptr;
                } else if (*cur == '(') {
                    ++cur;

                    unclosed_parens += 1;
                    auto res = or_clause();

                    if (*cur++ != ')') {
                        return nullptr;
                    }

                    unclosed_parens -= 1;
                    return res;
                } else {
                    return term();
                }
            }

            ast_ptr term() {
                // one_of
                backtrack_func funcs[3] {
                    &parser_impl::tag,
                    &parser_impl::metatag,
                    &parser_impl::wildcard,
                };

                for (backtrack_func func : funcs) {
                    if (auto res = backtrack(func)) {
                        return res;
                    }
                }

                // error("expected one of [ tag | metatag | wildcard ]");
                return nullptr;
            }

            ast_ptr tag() {
                /**
                 * A tag starts a character that is not a space, ), ~ or -
                 * A tag cannot start with a metatag name followed by a :
                 */
                if (eof()) {
                    return nullptr;
                }

                if (encoding::unicode_space(cur) || *cur == ')' || *cur == '~' || *cur == '-') {
                    // error("expected tag name");
                    return nullptr;
                }

                // Read until next space
                std::string_view tag = string([this](std::string_view::iterator it) -> bool {
                    if (encoding::unicode_space(it)) {
                        return false;
                    }

                    return true;
                }, true);

                if (tag == "and" || tag == "or" || tag.contains('*')) {
                    // error("Reserved tag name");
                    return nullptr;
                }

                for (const std::string_view sv : parser.metatags()) {
                    if (tag.starts_with(sv) && tag.size() > sv.size() && tag.at(sv.size()) == ':') {
                        // error("Reserved tag name (metatag)");
                        return nullptr;
                    }
                }

                consume_spaces();
                return ast::make_tag(tag);
            }

            ast_ptr metatag() {
                // Ensure start with a metatag
                for (std::string_view name : parser.metatags()) {
                    // Need at least metatag name, :, and one character
                    if (accept(name, ':')) {
                        std::optional<std::pair<bool, std::string>> value = quoted_string();
                        if (!value.has_value()) {
                            // Parsing error
                            return nullptr;
                        }

                        auto [quoted, quoted_value] = value.value();

                        return ast::make_metatag(name, quoted_value, quoted);
                    }
                }

                // No metatag found
                return nullptr;
            }

            ast_ptr wildcard() {
                // XXX: Maybe this can be shared with ::tag() parsing, but maybe that changes parsing results
                if (eof()) {
                    return nullptr;
                }

                if (encoding::unicode_space(cur) || *cur == ')' || *cur == '~' || *cur == '-') {
                    // error("expected tag name");
                    return nullptr;
                }

                bool has_wildcard = false;
                std::string_view tag = string([this, &has_wildcard](std::string_view::iterator ch) -> bool {
                    if (*ch == '*') {
                        has_wildcard = true;
                        return true;
                    } else if (encoding::unicode_space(ch)) {
                        return false;
                    } else {
                        return true;
                    }
                }, true);

                if (!has_wildcard) {
                    return nullptr;
                }

                for (const std::string_view sv : parser.metatags()) {
                    if (tag.starts_with(sv) && tag.size() > sv.size() && tag.at(sv.size()) == ':') {
                        // error("Reserved tag name (metatag)");
                        return nullptr;
                    }
                }

                consume_spaces();
                return ast::make_wildcard(tag);
            }

            private:
            using backtrack_func = ast_ptr(parser_impl::*)();
            ast_ptr backtrack(backtrack_func func) {
                auto old_cur = cur;
                auto old_state = unclosed_parens;
                auto res = (this->*func)();
                if (!res) {
                    // Reset state on failure
                    cur = old_cur;
                    unclosed_parens = old_state;
                }

                return res;
            }

            // Consume leading string if present
            // Interpret spaces as arbitary length unicode spaces
            // Extra suffix character check to make it more versatile
            bool accept(std::string_view pattern, char suffix = 0) {
                auto old_cur = cur;

                for (auto search = pattern.begin(); search != pattern.end();) {
                    // Match space as unicode space
                    if (eof()) {
                        // EOF reached
                        return false;
                    } else if (*search == ' ') {
                        if (int size = encoding::unicode_space(cur); size == 0) {
                            // Not a space while expecting space, reject
                            cur = old_cur;
                            return false;
                        } else {
                            // Skip whole whitespace and don't advance search
                            cur += (size - 1);
                            continue;
                        }
                    } else if (*search != *cur) {
                        // Mismatch, reject
                        cur = old_cur;
                        return false;
                    }

                     ++search;
                     ++cur;
                }

                if (suffix && *cur++ != suffix) {
                    cur = old_cur;
                    return false;
                }

                return true;
            }

            bool accept(char ch) {
                if (cur != end && *cur == ch) {
                    ++cur;
                    return true;
                }

                return false;
            }

            [[noreturn]] void error(const std::string& message) {
                rb_raise(post_query_err, message.c_str());
            }

            void ungetc() {
                if (cur == input.begin()) {
                    rb_raise(post_query_err, "attempted to rewind from start");
                }

                --cur;
            }

            std::optional<std::pair<bool, std::string>> quoted_string() {
                char first = *cur++;
                if (first == '"' || first == '\'') {
                    // Quoted string, consume any character that isn't a quote or part of an escape,
                    // or an escaped quote exactly
                    std::string res;
                    bool escape_next = false;

                    for (;; ++cur) {
                        // No EOF allowed since we require a closing quote
                        if (eof()) {
                            return std::nullopt;
                        }

                        // Escaped quote
                        if (escape_next) {
                            if (*cur == first) {
                                escape_next = false;
                                res.push_back(*cur);
                            } else {
                                // Not an escaped quote, parse error!
                                return std::nullopt;
                            }
                        } else if (*cur == '\\') {
                            escape_next = true;
                        } else if (*cur == first) {
                            // End of string, consume closing quote
                            ++cur;
                            break;
                        } else {
                            // Just pass through
                            res.push_back(*cur);
                        }
                    }

                    return std::pair{ true, res };
                } else {
                    // Unquoted string, only escape spaces
                    --cur;
                    bool escape_next = false;
                    std::string_view sv = string([this, &escape_next](std::string_view::iterator ch) -> bool {
                        if (escape_next) {
                            // XXX: Danbooru's parser lets you "escape" any character in a non-quoted string:
                            // order:a\bc -> order:a\bc
                            // order:"a\bc" -> none
                            escape_next = false;
                            return true;
                        } else if (*ch == '\\') {
                            escape_next = true;
                            return true;
                        } else if (encoding::unicode_space(ch)) {
                            return false;
                        } else {
                            return true;
                        }
                    });

                    // Unescape any escaped spaces, leave escaped non-spaces intact
                    std::string res;
                    res.reserve(sv.size());
                    escape_next = false;
                    for (char ch : sv) {
                        if (escape_next) {
                            escape_next = false;
                            if (encoding::unicode_space(ch)) {
                                // Escaped space
                                res.push_back(ch);
                            } else {
                                // Escaped non-space, retain escape character
                                res.push_back('\\');
                                res.push_back(ch);
                            }
                        } else if (ch == '\\') {
                            escape_next = true;
                        } else {
                            res.push_back(ch);
                        }
                    }

                    return std::pair{ false, res };
                }

                return std::nullopt;
            }

            template <typename Func> requires requires (Func func, std::string_view::iterator it) { { func(it) } -> std::same_as<bool>; }
            std::string_view string(Func func, bool skip_balanced_parens = false) {
                auto start = cur;
                for (; !eof(); ++cur) {
                    if (!func(cur)) {
                        break;
                    }
                }

                std::string_view res { start, cur };

                ssize_t n = unclosed_parens;

                // Remove trailing ) we might've consumed if there's an imbalance and any open ones
                // Consume at most the # of unclosed parens
                while (n > 0 && res.back() == ')') {
                    // Skip once parens are balanced, or it's an allowed imbalance
                    if (skip_balanced_parens) {
                        if (balanced_parens(res) || std::ranges::contains(unbalanced_tags, res)) {
                            break;
                        }
                    }

                    res = res.substr(0, res.size() - 1);
                    --cur;
                    --n;
                }

                return res;
            }

            static bool balanced_parens(std::string_view sv) {
                ssize_t parens = 0;
                for (char ch : sv) {
                    if (ch == '(') {
                        parens += 1;
                    } else if (ch == ')') {
                        parens -= 1;
                        if (parens < 0) {
                            return false;
                        }
                    }
                }
                return true;
            }

            void consume_spaces() {
                for (; !eof();) {
                    if (int size = encoding::unicode_space(cur); size == 0) {
                        break;
                    } else {
                        cur += size;
                    }
                }
            }
        };
    };
}

#endif /* PARSER_H */

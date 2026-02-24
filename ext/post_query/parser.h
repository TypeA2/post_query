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

            ast_ptr res = impl.parse_root();

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

        static constexpr bool case_compare(char c1, char c2, bool case_sensitive) {
            if (case_sensitive) {
                return c1 == c2;
            } else {
                return std::tolower(c1) == std::tolower(c2);
            }
        }

        static constexpr bool case_compare(std::string_view s1, std::string_view s2, bool case_sensitive) {
            if (case_sensitive) {
                return s1 == s2;
            } else {
                return std::ranges::equal(
                    s1, s2,
                    [](char c1, char c2) { return case_compare(c1, c2, false); }
                );
            }
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

            ast_ptr parse_root() {
                /**
                 * root         = or_clause [root]
                 *  -> one or more or clauses
                 * or_clause    = and_clause "or" or_clause | and_clause
                 * and_clause   = factor_list "and" and_clause | factor_list
                 * factor_list  = factor [factor_list]
                 * factor       = "-" expr | "~" expr | expr
                 * expr         = "(" or_clause ")" | term
                 * term         = metatag | tag | wildcard
                 * 
                 * Null-clause means parsing error, which is propagated
                 */

                // This differs from Danbooru's parser:
                // Only whitespace-only queries can produce the `all` query
                // We check this immediately here because it's difficult to return an error
                // from zero_or_more (since it might just be empty)
                consume_spaces();
                if (eof()) {
                    return ast::make_all();
                }

                // As above, this can be a one_or_more now
                std::vector<ast_ptr> clauses = one_or_more(&parser_impl::or_clause);

                consume_spaces();

                if (!eof()) {
                    return nullptr;
                } else if (clauses.size() == 0) {
                    return nullptr;
                } else if (clauses.size() == 1) {
                    return std::move(clauses.front());
                } else {
                    return ast::make_and(std::move(clauses));
                }
            }

            ast_ptr or_clause() {
                auto a = and_clause();

                if (!a) {
                    return nullptr;
                }

                consume_spaces();

                if (accept("or ", false)) {
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

                if (!a) {
                    return nullptr;
                }

                consume_spaces();

                if (accept("and ", false)) {
                    std::vector<ast_ptr> children;
                    children.emplace_back(std::move(a));
                    children.emplace_back(and_clause());
                    return ast::make_and(std::move(children));
                } else {
                    return a;
                }
            }

            ast_ptr factor_list() {
                std::vector<ast_ptr> clauses = one_or_more(&parser_impl::factor);

                // Empty is error condition
                if (clauses.size() == 0) {
                    return nullptr;
                }
                
                return ast::make_and(std::move(clauses));
            }

            ast_ptr factor() {
                consume_spaces();

                if (accept('-')) {
                    auto child = expr();
                    return child ? ast::make_not(std::move(child)) : nullptr;
                } else if (accept('~')) {
                    auto child = expr();
                    return child ? ast::make_opt(std::move(child)) : nullptr;
                } else {
                    // Normal expression
                    return expr();
                }
            }

            ast_ptr expr() {
                consume_spaces();

                if (accept('(')) {
                    unclosed_parens += 1;
                    auto res = or_clause();

                    if (!res || !accept(')')) {
                        return nullptr;
                    }

                    unclosed_parens -= 1;
                    return res;
                } else {
                    return term();
                }
            }

            ast_ptr term() {
                std::array<parse_func, 3> funcs {
                    &parser_impl::tag,
                    &parser_impl::metatag,
                    &parser_impl::wildcard,
                };

                // Errors propagate
                return one_of(funcs);
            }

            ast_ptr tag() {
                /**
                 * A tag starts a character that is not a space, ), ~ or -
                 * A tag cannot start with a metatag name followed by a :
                 */
                if (eof() || encoding::unicode_space(cur) || *cur == ')' || *cur == '~' || *cur == '-') {
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

                if (case_compare(tag, "and", false) || case_compare(tag, "or", false) || tag.contains('*') || is_metatag(tag)) {
                    // error("Reserved tag name");
                    return nullptr;
                }

                consume_spaces();
                return ast::make_tag(tag);
            }

            ast_ptr metatag() {
                // Ensure start with a metatag
                for (std::string_view name : parser.metatags()) {
                    // Need at least metatag name, :, and one character
                    std::string old { remaining() };
                    if (accept(name, ':', false)) {
                        bool quoted;
                        std::string value;
                        if (!quoted_string(quoted, value)) {
                            // Parsing error
                            return nullptr;
                        }

                        return ast::make_metatag(name, value, quoted);
                    }
                }

                // No metatag found
                return nullptr;
            }

            ast_ptr wildcard() {
                // XXX: Maybe this can be shared with ::tag() parsing, but maybe that changes parsing results
                if (eof() || encoding::unicode_space(cur) || *cur == ')' || *cur == '~' || *cur == '-') {
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

                if (!has_wildcard || is_metatag(tag)) {
                    return nullptr;
                }

                consume_spaces();
                return ast::make_wildcard(tag);
            }

            private:
            using parse_func = ast_ptr(parser_impl::*)();
            ast_ptr backtrack(parse_func func) {
                auto old_cur = cur;
                auto old_state = unclosed_parens;
                auto res = (this->*func)();
                if (!res) {
                    auto old = std::string{remaining()};
                    // Reset state on failure
                    cur = old_cur;
                    unclosed_parens = old_state;

                    // Propagate null value
                }

                return res;
            }

            std::vector<ast_ptr> zero_or_more(parse_func func) {
                std::vector<ast_ptr> res;

                for (;;) {
                    auto match = backtrack(func);

                    // Catch all errors
                    if (!match) {
                        return res;
                    }

                    res.emplace_back(std::move(match));
                }
            }

            std::vector<ast_ptr> one_or_more(parse_func func) {
                ast_ptr first = (this->*func)();
                if (!first) {
                    // Propagate error only from first entry
                    return {};
                }

                std::vector<ast_ptr> final;
                final.emplace_back(std::move(first));

                std::vector<ast_ptr> rest = zero_or_more(func);
                final.resize(final.size() + rest.size());
                std::ranges::move(rest, std::next(final.begin()));

                return final;
            }

            ast_ptr one_of(std::span<parse_func> funcs) {
                for (parse_func func : funcs) {
                    auto res = backtrack(func);
                    // "Catch" errors
                    if (res) {
                        return res;
                    }
                }

                return nullptr;
            }

            bool is_metatag(std::string_view sv) const {
                for (std::string_view metatag : parser.metatags()) {
                    if (sv.size() > metatag.size() && sv.at(metatag.size()) == ':'
                        && case_compare(metatag, sv.substr(0, metatag.size()), false)) {
                        return true;
                    }
                }

                return false;
            }

            // Consume leading string if present
            // Interpret spaces as arbitary length unicode spaces
            // Extra suffix character check to make it more versatile
            bool accept(std::string_view pattern, char suffix = 0, bool case_sensitive = true) {
                auto old_cur = cur;

                bool space_matched = false;
                for (auto search = pattern.begin(); search != pattern.end();) {
                    // Match space as unicode space
                    if (eof()) {
                        // EOF reached
                        return false;
                    } else if (*search == ' ') {
                        if (int size = encoding::unicode_space(cur); size == 0) {
                            // Not a space while expecting space, reject if none was matched before
                            if (space_matched) {
                                space_matched = false;
                                ++search;
                                continue;
                            }

                            cur = old_cur;
                            return false;
                        } else {
                            // Skip whole whitespace and don't advance search
                            space_matched = true;
                            cur += size;
                            continue;
                        }
                    } else if (!case_compare(*search, *cur, case_sensitive)) {
                        // Mismatch, reject
                        cur = old_cur;
                        return false;
                    }

                     ++search;
                     ++cur;
                }

                if (suffix) {
                    if (eof()) {
                        return false;
                    } else if (!case_compare(*cur, suffix, case_sensitive)) {
                        cur = old_cur;
                        return false;
                    }

                    ++cur;
                }

                return true;
            }

            bool accept(std::string_view pattern, bool case_sensitive) {
                return accept(pattern, 0, case_sensitive);
            }

            bool accept(char ch, bool case_sensitive = true) {
                if (cur != end && case_compare(*cur, ch, case_sensitive)) {
                    ++cur;
                    return true;
                }

                return false;
            }

            [[noreturn]] void error(const std::string& message) {
                rb_raise(post_query_err, message.c_str());
            }

            bool quoted_string(bool& quoted, std::string& res) {
                char first = *cur;
                if (first == '"' || first == '\'') {
                    // Quoted string, consume any character that isn't a quote or part of an escape,
                    // or an escaped quote exactly
                    ++cur;

                    quoted = true;

                    bool escape_next = false;

                    for (;;) {
                        // No EOF allowed since we require a closing quote
                        if (eof()) {
                            return false;
                        }

                        // Escaped quote
                        if (escape_next) {
                            if (accept(first)) {
                                escape_next = false;
                                res.push_back(first);
                            } else {
                                // Not an escaped quote, parse error!
                                return false;
                            }
                        } else if (accept('\\')) {
                            escape_next = true;
                        } else if (accept(first)) {
                            // End of string, consume closing quote
                            return true;
                        } else {
                            // Just pass through
                            res.push_back(*cur++);
                        }
                    }
                } else {
                    // Unquoted string, only escape spaces
                    quoted = false;
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
                    res.reserve(res.size() + sv.size());
                    escape_next = false;
                    for (auto it = sv.begin(); it != sv.end(); ++it) {
                        if (escape_next) {
                            escape_next = false;
                            if (int size = encoding::unicode_space(it)) {
                                // Escaped space
                                res.push_back(*it);

                                std::advance(it, size - 1);
                            } else {
                                // Escaped non-space, retain escape character
                                res.push_back('\\');
                                res.push_back(*it);
                            }
                        } else if (*it == '\\') {
                            escape_next = true;
                        } else {
                            res.push_back(*it);
                        }
                    }

                    return true;
                }

                return false;
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

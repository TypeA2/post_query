#ifndef PARSER_H
#define PARSER_H

#include "ast.h"

#include <string>
#include <vector>
#include <memory>
#include <span>
#include <string_view>
#include <array>

static constexpr std::array<char32_t, 17> unicode_spaces {
    U'\u0020', // Space (SP)
    U'\u00A0', // No-Break Space (NBSP)
    U'\u1680', // Ogham Space Mark
    U'\u2000', // En Quad 
    U'\u2001', // Em Quad
    U'\u2002', // En Space
    U'\u2003', // Em Space
    U'\u2004', // Three-Per-Em Space
    U'\u2005', // Four-Per-Em Space
    U'\u2006', // Six-Per-Em Space
    U'\u2007', // Figure Space
    U'\u2008', // Punctuation Space
    U'\u2009', // Thin Space
    U'\u200A', // Hair Space
    U'\u202F', // Narrow No-Break Space (NNBSP)
    U'\u205F', // Medium Mathematical Space (MMSP)
    U'\u3000', // Ideographic Space
};

namespace post_query {
    template <typename CharT>
    class parser {
        public:
        using string_type = std::basic_string<CharT>;
        using string_view_type = std::basic_string_view<CharT>;

        private:
        std::vector<string_type> _metatags;
        string_type _input;

        public:
        parser(std::vector<string_type> metatags)
            : _metatags { std::move(metatags) } {

        }

        std::unique_ptr<ast> parse(string_view_type query) {
            return std::make_unique<ast>();
        }
    };
}

#endif /* PARSER_H */

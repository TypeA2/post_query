#ifndef ENCODING_H
#define ENCODING_H

// Some utilities to deal with Ruby strings and encodings

#include <ruby.h>

#include <string>
#include <string_view>
#include <algorithm>
#include <iostream>
#include <format>

#include <cuchar>

#ifndef __STDC_UTF_16__
#   error "__STDC_UTF_16__ not defined, can't convert UTF-16"
#endif

#ifndef __STDC_UTF_32__
#   error "__STDC_UTF_32__ not defined, can't convert UTF-32"
#endif

enum class enc {
    utf8,
    utf16,
    utf32,
};

template <enc type> struct enc_char { };

template<> struct enc_char<enc::utf8> {
    using type = char8_t;
    static constexpr std::size_t(*from_func)(char*, char8_t, std::mbstate_t*) = std::c8rtomb;
    static constexpr std::size_t(*to_func)(char8_t*, const char*, std::size_t, std::mbstate_t*) = std::mbrtoc8;
};

template<> struct enc_char<enc::utf16> {
    using type = char16_t;
    static constexpr std::size_t(*from_func)(char*, char16_t, std::mbstate_t*) = std::c16rtomb;
    static constexpr std::size_t(*to_func)(char16_t*, const char*, std::size_t, std::mbstate_t*) = std::mbrtoc16;
};

template<> struct enc_char<enc::utf32> {
    using type = char32_t;
    static constexpr std::size_t(*from_func)(char*, char32_t, std::mbstate_t*) = std::c32rtomb;
    static constexpr std::size_t(*to_func)(char32_t*, const char*, std::size_t, std::mbstate_t*) = std::mbrtoc32;
};

template <enc type>
using enc_char_t = typename enc_char<type>::type;

namespace encoding {
    template <enc from, enc to>
    std::basic_string<enc_char_t<to>> convert(std::basic_string_view<enc_char_t<from>> sv) {
        if constexpr (from == to) {
            return sv;
        } else {
            std::mbstate_t from_state{};

            // Actually MB_CUR_MAX
            char from_buf[8];
            std::string buf;
            for (auto c : sv) {
                size_t bytes = enc_char<from>::from_func(from_buf, c, &from_state);

                if (bytes == size_t(-1)) {
                    rb_raise(rb_eEncodingError, "failed to convert to multibyte");
                }

                for (size_t i = 0; i < bytes; ++i) {
                    buf.push_back(from_buf[i]);
                }
            }

            std::basic_string<enc_char_t<to>> res;
            std::mbstate_t to_state{};
            enc_char_t<to> next_char;

            auto cur = buf.data();
            auto end = cur + buf.size() + 1;
            for (;;) {
                size_t bytes = enc_char<to>::to_func(&next_char, cur, end - cur, &to_state);

                if (bytes == 0) {
                    break;
                } else if (bytes == size_t(-1)) {
                    rb_raise(rb_eEncodingError, "failed to convert from multibyte");
                } else if (bytes == size_t(-2)) {
                    rb_raise(rb_eEncodingError, "unfinished multi-byte character");
                } else if (bytes == size_t(-3)) {
                    res.push_back(next_char);
                } else  {
                    cur += bytes;
                    res.push_back(next_char);
                } 
            }

            return res;
        }
    }

    std::u8string launder(std::string_view sv) {
        std::u8string res;
        res.reserve(sv.size());
        for (char c : sv) {
            res.push_back(c);
        }
        return res;
    }

    std::string launder(std::u8string_view sv) {
        std::string res;
        res.reserve(sv.size());
        for (char c : sv) {
            res.push_back(c);
        }
        return res;
    }

    template <enc to> std::basic_string<enc_char_t<to>> convert(std::string_view sv) { return convert<enc::utf8, to>(launder(sv)); }
    template <enc to> std::basic_string<enc_char_t<to>> convert(std::u8string_view sv) { return convert<enc::utf8, to>(sv); }
    template <enc to> std::basic_string<enc_char_t<to>> convert(std::u16string_view sv) { return convert<enc::utf16, to>(sv); }
    template <enc to> std::basic_string<enc_char_t<to>> convert(std::u32string_view sv) { return convert<enc::utf32, to>(sv); }
}

template <>
struct std::formatter<std::u8string_view> : std::formatter<std::string_view> {
    auto format(std::u8string_view sv, format_context& ctx) {
        return std::formatter<std::string_view>::format(encoding::launder(sv), ctx);
    }
};

std::ostream& operator<<(std::ostream& os, std::u8string_view sv) {
    return os << encoding::launder(sv);
}

template <>
struct std::formatter<std::u16string_view> : std::formatter<std::u8string_view> {
    auto format(std::u16string_view sv, format_context& ctx) {
        return std::formatter<std::u8string_view>::format(encoding::convert<enc::utf8>(sv), ctx);
    }
};

std::ostream& operator<<(std::ostream& os, std::u16string_view sv) {
    return os << encoding::convert<enc::utf8>(sv);
}

template <>
struct std::formatter<std::u32string_view> : std::formatter<std::u8string_view> {
    auto format(std::u32string_view sv, format_context& ctx) {
        return std::formatter<std::u8string_view>::format(encoding::convert<enc::utf8>(sv), ctx);
    }
};

std::ostream& operator<<(std::ostream& os, std::u32string_view sv) {
    return os << encoding::convert<enc::utf8>(sv);
}

#endif /* ENCODING_H */

#ifndef ENCODING_H
#define ENCODING_H

// Some utilities to deal with Ruby strings and encodings

#include <ruby.h>

#include <string>
#include <string_view>
#include <algorithm>
#include <iostream>
#include <format>
#include <iterator>

#include <cuchar>

#ifndef __STDC_UTF_16__
#   error "__STDC_UTF_16__ not defined, can't convert UTF-16"
#endif

#ifndef __STDC_UTF_32__
#   error "__STDC_UTF_32__ not defined, can't convert UTF-32"
#endif

enum class enc {
    narrow,
    utf8,
    utf16,
    utf32,
};

template <enc type> struct enc_char { };

template<> struct enc_char<enc::narrow> {
    using type = char;

    static constexpr std::size_t(*from_func)(char*, char, std::mbstate_t*)
        = [](char* s, char c8, std::mbstate_t* ps) -> std::size_t {
            return std::c8rtomb(s, char8_t(c8), ps);
        };

    static constexpr std::size_t(*to_func)(char*, const char*, std::size_t, std::mbstate_t*)
        = [](char* pc8, const char* s, std::size_t n, std::mbstate_t* ps) -> std::size_t {
            return std::mbrtoc8(static_cast<char8_t*>(static_cast<void*>(pc8)), s, n, ps);
        };
};

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

template <typename CharT> struct char_enc { };

template<> struct char_enc<char> { static constexpr enc encoding = enc::narrow; };
template<> struct char_enc<char8_t> { static constexpr enc encoding = enc::utf8; };
template<> struct char_enc<char16_t> { static constexpr enc encoding = enc::utf16; };
template<> struct char_enc<char32_t> { static constexpr enc encoding = enc::utf32; };

template <typename CharT>
constexpr enc char_enc_v = char_enc<CharT>::encoding;

template <enc type>
using enc_char_t = typename enc_char<type>::type;

namespace encoding {
    template <enc from, enc to>
    std::basic_string<enc_char_t<to>> convert(std::basic_string_view<enc_char_t<from>> sv) {
        if constexpr (from == to) {
            return std::basic_string<enc_char_t<to>>{ sv };
        } else if constexpr ((from == enc::narrow || from == enc::utf8) && (to == enc::narrow || to == enc::utf8)) {
            /* Direct conversion */
            return std::basic_string<enc_char_t<to>>(reinterpret_cast<const enc_char_t<to>*>(sv.data()), sv.size());
        } else {
            if (sv.size() == 0) {
                return {};
            }

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

    template <enc to> std::basic_string<enc_char_t<to>> convert(std::string_view sv) { return convert<enc::narrow, to>(sv); }
    template <enc to> std::basic_string<enc_char_t<to>> convert(std::u8string_view sv) { return convert<enc::utf8, to>(sv); }
    template <enc to> std::basic_string<enc_char_t<to>> convert(std::u16string_view sv) { return convert<enc::utf16, to>(sv); }
    template <enc to> std::basic_string<enc_char_t<to>> convert(std::u32string_view sv) { return convert<enc::utf32, to>(sv); }

    template <typename CharT> auto convert(std::string_view sv) -> decltype(auto) { return convert<enc::narrow, char_enc_v<CharT>>(sv); }
    template <typename CharT> auto convert(std::u8string_view sv) -> decltype(auto) { return convert<enc::utf8, char_enc_v<CharT>>(sv); }
    template <typename CharT> auto convert(std::u16string_view sv) -> decltype(auto) { return convert<enc::utf16, char_enc_v<CharT>>(sv); }
    template <typename CharT> auto convert(std::u32string_view sv) -> decltype(auto) { return convert<enc::utf32, char_enc_v<CharT>>(sv); }
}

template <>
struct std::formatter<std::u8string_view> : std::formatter<std::string_view> {
    auto format(std::u8string_view sv, format_context& ctx) const {
        return std::formatter<std::string_view>::format(encoding::convert<enc::narrow>(sv), ctx);
    }
};

template <>
struct std::formatter<std::u8string> : std::formatter<std::string> {
    auto format(std::u8string_view sv, format_context& ctx) const {
        return std::formatter<std::string>::format(encoding::convert<enc::narrow>(sv), ctx);
    }
};

template <>
struct std::formatter<std::u16string_view> : std::formatter<std::string_view> {
    auto format(std::u16string_view sv, format_context& ctx) const {
        return std::formatter<std::string_view>::format(encoding::convert<enc::narrow>(sv), ctx);
    }
};

template <>
struct std::formatter<std::u16string> : std::formatter<std::string> {
    auto format(const std::u16string& sv, format_context& ctx) const {
        return std::formatter<std::string>::format(encoding::convert<enc::narrow>(sv), ctx);
    }
};

template <>
struct std::formatter<std::u32string_view> : std::formatter<std::string_view> {
    auto format(std::u32string_view sv, format_context& ctx) const {
        return std::formatter<std::string_view>::format(encoding::convert<enc::narrow>(sv), ctx);
    }
};

template <>
struct std::formatter<std::u32string> : std::formatter<std::string> {
    auto format(const std::u32string& sv, format_context& ctx) const {
        return std::formatter<std::string>::format(encoding::convert<enc::narrow>(sv), ctx);
    }
};

std::ostream& operator<<(std::ostream& os, std::u8string_view sv) {
    return os << encoding::convert<enc::narrow>(sv);
}

std::ostream& operator<<(std::ostream& os, char8_t ch) {
    return os << std::u8string(1, ch);
}

std::ostream& operator<<(std::ostream& os, std::u16string_view sv) {
    return os << encoding::convert<enc::narrow>(sv);
}

std::ostream& operator<<(std::ostream& os, char16_t ch) {
    return os << std::u16string(1, ch);
}

std::ostream& operator<<(std::ostream& os, std::u32string_view sv) {
    return os << encoding::convert<enc::narrow>(sv);
}

std::ostream& operator<<(std::ostream& os, char32_t ch) {
    return os << std::u32string(1, ch);
}

namespace encoding {
    // https://www.compart.com/en/unicode/category/Zs
    static constexpr std::array<char16_t, 17> unicode_spaces {
        u'\u0020', // Space (SP)
        u'\u00A0', // No-Break Space (NBSP)
        u'\u1680', // Ogham Space Mark
        u'\u2000', // En Quad 
        u'\u2001', // Em Quad
        u'\u2002', // En Space
        u'\u2003', // Em Space
        u'\u2004', // Three-Per-Em Space
        u'\u2005', // Four-Per-Em Space
        u'\u2006', // Six-Per-Em Space
        u'\u2007', // Figure Space
        u'\u2008', // Punctuation Space
        u'\u2009', // Thin Space
        u'\u200A', // Hair Space
        u'\u202F', // Narrow No-Break Space (NNBSP)
        u'\u205F', // Medium Mathematical Space (MMSP)
        u'\u3000', // Ideographic Space
    };

    static constexpr std::array<char[4], 17> space_seqs {
        u8"\u0020\0", // Space (SP)
        u8"\u00A0\0", // No-Break Space (NBSP)
        u8"\u1680", // Ogham Space Mark
        u8"\u2000", // En Quad 
        u8"\u2001", // Em Quad
        u8"\u2002", // En Space
        u8"\u2003", // Em Space
        u8"\u2004", // Three-Per-Em Space
        u8"\u2005", // Four-Per-Em Space
        u8"\u2006", // Six-Per-Em Space
        u8"\u2007", // Figure Space
        u8"\u2008", // Punctuation Space
        u8"\u2009", // Thin Space
        u8"\u200A", // Hair Space
        u8"\u202F", // Narrow No-Break Space (NNBSP)
        u8"\u205F", // Medium Mathematical Space (MMSP)
        u8"\u3000", // Ideographic Space
    };

    static constexpr bool unicode_space(char16_t chr) {
        uint8_t lo = chr & 0x00FF;
        uint16_t hi = chr & 0xFF00;

        switch (hi) {
            case 0x0000: return lo == 0x20 || lo == 0xA0;
            case 0x1600: return lo == 0x80;
            case 0x2000: return lo <= 0x0a || lo == 0x2F || lo == 0x5F;
            case 0x3000: return lo == 0;
            default:     return false;
        } 
    }

    // Input must be an iterator into a null-terminated string
    // This is so that we can read one past the end safely
    static constexpr int unicode_space(std::random_access_iterator auto it) {
        switch (uint8_t(it[0])) {
            // ASCII spaces
            case 0x20: // Space (SP)
                return 1;

            case 0b110'000'10: // No-Break Space (NBSP)
                return (uint8_t(it[1]) == 0b10'10'0000) ? 2 : 0;

            // Ogham Space Mark
            case 0b1110'0001:
                return (uint8_t(it[1]) == 0b10'0110'10 && uint8_t(it[2]) == 0b10'00'0000) ? 3 : 0;

            // U+2xxx
            case 0b1110'0010:
                if (uint8_t(it[1]) == 0b10'0000'00) {
                    return ((uint8_t(it[2]) >= 0b10'00'0000 && uint8_t(it[2]) <= 0b10'00'1010)
                            || (uint8_t(it[2]) == 0b10'10'1111)) ? 3 : 0;
                } else {
                    return (uint8_t(it[1]) == 0b10'0000'01 && uint8_t(it[2]) == 0b10'01'1111) ? 3 : 0;
                }

            // U+3xxx
            case 0b1110'0011:
                return (uint8_t(it[1]) == 0b10'0000'00 && uint8_t(it[2]) == 0b10'00'0000) ? 3 : 0;
                
            default:
                return 0;
        }
    }
}

#endif /* ENCODING_H */

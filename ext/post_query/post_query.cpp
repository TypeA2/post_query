#include "parser.h"
#include "encoding.h"

#include <ruby.h>
#include <ruby/encoding.h>

#include <bit>
#include <algorithm>
#include <iostream>
#include <format>

static VALUE post_query_cls = Qnil;
static VALUE post_query_err = Qnil;
static VALUE post_query_ast_cls = Qnil;


/* Ruby type stuff */
static void ast_free(void* data) {
    std::unique_ptr<post_query::ast> ptr(static_cast<post_query::ast*>(data));

    // Automatically deconstruct and free
}

static const rb_data_type_t ast_type {
    .wrap_struct_name = "post_query_ast",
    .function = {
        .dmark = nullptr,
        .dfree = ast_free,
    },
    .flags = RUBY_TYPED_FREE_IMMEDIATELY
};


/* Some utilities */
static std::u8string safe_string(VALUE str) {
    Check_Type(str, T_STRING);

    if (int enc = rb_enc_get_index(str); enc != rb_usascii_encindex() && enc != rb_utf8_encindex()) {
        rb_raise(post_query_cls, "input must be US-ASCII or UTF-8");
    }

    if (rb_enc_str_coderange(str) == RUBY_ENC_CODERANGE_BROKEN ) {
        rb_raise(post_query_cls, "input contains invalid UTF-8");
    }

    // Throws when it encounters a null byte
    return reinterpret_cast<char8_t*>(StringValuePtr(str));
}


/* Ruby implementations */
static VALUE post_query_parse(VALUE self, VALUE _input, VALUE _metatags) {
    // Return nil on nil input, kind of safer
    if (NIL_P(_input)) {
        return Qnil;
    }

    std::u32string parser_input = encoding::convert<enc::utf32>(safe_string(_input));

    Check_Type(_metatags, T_ARRAY);
    std::vector<std::u32string> parser_metatags;
    parser_metatags.reserve(rb_array_len(_metatags));

    for (long i = 0; i < rb_array_len(_metatags); ++i) {
        VALUE tag = rb_ary_entry(_metatags, i);
        Check_Type(tag, T_STRING);

        parser_metatags.emplace_back(encoding::convert<enc::utf32>(safe_string(tag)));
    }

    post_query::parser parser { std::move(parser_metatags) };

    auto ast = parser.parse(parser_input);

    return TypedData_Wrap_Struct(post_query_ast_cls, &ast_type, ast.release());
}

extern "C" void Init_post_query() {
    post_query_cls = rb_define_class("PostQuery", rb_cObject);
    post_query_err = rb_define_class_under(post_query_cls, "Error", rb_eStandardError);
    rb_define_singleton_method(post_query_cls, "parse_raw", post_query_parse, 2);

    // No alloc function, only create it internally
    post_query_ast_cls = rb_define_class_under(post_query_cls, "AST", rb_cObject);
    rb_undef_alloc_func(post_query_ast_cls);
}

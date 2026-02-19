#include "parser.h"
#include "encoding.h"

#include <ruby.h>
#include <ruby/encoding.h>

#include <bit>
#include <algorithm>
#include <iostream>
#include <format>

VALUE post_query_cls = Qnil;
VALUE post_query_err = Qnil;
VALUE post_query_ast_cls = Qnil;


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
};


/* Some utilities */
static std::string safe_string(VALUE str) {
    Check_Type(str, T_STRING);

    if (int enc = rb_enc_get_index(str); enc != rb_usascii_encindex() && enc != rb_utf8_encindex()) {
        rb_raise(post_query_cls, "input must be US-ASCII or UTF-8");
    }

    if (rb_enc_str_coderange(str) == RUBY_ENC_CODERANGE_BROKEN ) {
        rb_raise(post_query_cls, "input contains invalid UTF-8");
    }

    // Throws when it encounters a null byte
    return StringValuePtr(str);
}


/* Ruby implementations */
static VALUE post_query_parse(VALUE self, VALUE _input, VALUE _metatags) {
    // Return nil on nil input, kind of safer
    if (NIL_P(_input)) {
        return Qnil;
    }

    // Use UTF-16 so that all valid space characters are encoded as 1 character
    // Surrogate pairs will never overlap as they live in the surrogate range (\uDxxx)
    std::string parser_input = safe_string(_input);

    Check_Type(_metatags, T_ARRAY);
    std::vector<std::string> parser_metatags;
    parser_metatags.reserve(rb_array_len(_metatags));

    for (long i = 0; i < rb_array_len(_metatags); ++i) {
        VALUE tag = rb_ary_entry(_metatags, i);
        Check_Type(tag, T_STRING);

        parser_metatags.emplace_back(safe_string(tag));
    }

    post_query::parser parser { std::move(parser_metatags) };

    std::unique_ptr<post_query::ast> ast = parser.parse(parser_input);

    return ast ? TypedData_Wrap_Struct(post_query_ast_cls, &ast_type, ast.release()) : Qnil;
}

static VALUE post_query_ast_inspect(VALUE self) {
    post_query::ast* ast;
    TypedData_Get_Struct(self, post_query::ast, &ast_type, ast);

    std::string_view node_type = "Unknown";
    switch (ast->type()) {
        case post_query::node_type::All:
            return rb_external_str_new_cstr("#<PostQuery::AST::All>");

        case post_query::node_type::None:
            return rb_external_str_new_cstr("#<PostQuery::AST::None>");

        case post_query::node_type::Tag:
            return rb_sprintf("#<PostQuery::AST::Tag tag=\"%s\">", ast->to_infix().c_str());

        case post_query::node_type::Metatag:  node_type = "Metatag"; break;
        case post_query::node_type::Wildcard: node_type = "Wildcard"; break;
        case post_query::node_type::And:      node_type = "And";      break;
        case post_query::node_type::Or:       node_type = "Or";       break;
        case post_query::node_type::Not:      node_type = "Not";      break;
        case post_query::node_type::Opt:      node_type = "Opt";      break;
    }

    std::stringstream ss;
    ss << "#<PostQuery::AST::" << node_type << " query=" << std::quoted(ast->to_infix()) << ">";
    return rb_external_str_new_cstr(ss.str().c_str());
}

static VALUE post_query_ast_to_s(VALUE self) {
    post_query::ast* ast;
    TypedData_Get_Struct(self, post_query::ast, &ast_type, ast);

    return rb_external_str_new_cstr(ast->to_infix().c_str());
}

static VALUE post_query_ast_to_sexp(VALUE self) {
    post_query::ast* ast;
    TypedData_Get_Struct(self, post_query::ast, &ast_type, ast);

    return rb_external_str_new_cstr(ast->to_sexp().c_str());
}

static VALUE post_query_ast_to_infix(VALUE self) {
    post_query::ast* ast;
    TypedData_Get_Struct(self, post_query::ast, &ast_type, ast);

    return rb_external_str_new_cstr(ast->to_infix().c_str());
}

static VALUE post_query_ast_to_cnf(VALUE self) {
    post_query::ast* ast;
    TypedData_Get_Struct(self, post_query::ast, &ast_type, ast);

    ast->to_cnf();

    return self;
}

/* Module initializer */
extern "C" void Init_post_query() {
    post_query_cls = rb_define_class("PostQuery", rb_cObject);
    post_query_err = rb_define_class_under(post_query_cls, "Error", rb_eStandardError);
    rb_define_singleton_method(post_query_cls, "parse_raw", post_query_parse, 2);

    // No alloc function, only create it internally
    post_query_ast_cls = rb_define_class_under(post_query_cls, "AST", rb_cObject);
    rb_undef_alloc_func(post_query_ast_cls);

    rb_define_method(post_query_ast_cls, "inspect", post_query_ast_inspect, 0);
    rb_define_method(post_query_ast_cls, "to_s", post_query_ast_to_s, 0);
    rb_define_method(post_query_ast_cls, "to_sexp", post_query_ast_to_sexp, 0);
    rb_define_method(post_query_ast_cls, "to_infix", post_query_ast_to_infix, 0);
    rb_define_method(post_query_ast_cls, "to_cnf", post_query_ast_to_cnf, 0);
}

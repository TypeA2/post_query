#include <ruby.h>

static VALUE post_query_add1(VALUE self, VALUE val) {
    Check_Type(val, T_FIXNUM);

    auto _val = FIX2LONG(val);

    return LONG2FIX(_val + 1);
}

extern "C" void Init_post_query() {
    VALUE cls = rb_define_class("PostQuery", rb_cObject);
    rb_define_singleton_method(cls, "add1", post_query_add1, 1);
}

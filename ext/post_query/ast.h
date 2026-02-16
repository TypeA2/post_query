#ifndef AST_H
#define AST_H

namespace post_query {
    class ast {
        public:
        enum class type {
            All,
            None,
            Tag,
            Metatag,
            Wildcard,
            And,
            Or,
            Not,
            Opt,
        };
    };
}

#endif /* AST_H */

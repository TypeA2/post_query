#ifndef AST_H
#define AST_H

#include "encoding.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <memory>
#include <span>
#include <variant>
#include <array>
#include <ranges>

namespace post_query {
    enum class node_type {
        All,
        None,
        Tag,
        Wildcard,
        Metatag,
        Not,
        Opt,
        And,
        Or,
    };

    static constexpr std::array<std::string_view, 9> node_type_names {
        "all", "none", "tag", "wildcard", "metatag", "not", "opt", "and", "or",
    };

    static constexpr std::string_view node_type_name(node_type type) {
        return node_type_names[static_cast<int>(type)];
    }
}

template <>
struct std::formatter<post_query::node_type> : std::formatter<std::string_view> {
    auto format(post_query::node_type type, format_context& ctx) const {
        return std::formatter<std::string_view>::format(post_query::node_type_name(type), ctx);
    }
};

std::ostream& operator<<(std::ostream& os, post_query::node_type type) {
    return os << post_query::node_type_name(type);
}

namespace post_query {
    using namespace std::literals;
    class ast;

    using ast_ptr = std::unique_ptr<ast>;

    struct metatag_data {
        std::string name;
        std::string value;
        bool quoted;
    };

    std::string format_metatag(const metatag_data& data) {
        std::stringstream ss;
        ss << data.name << ':';

        if (data.quoted) {
            ss << std::quoted(encoding::convert<enc::narrow>(data.value));
        } else {
            ss << data.value;
        }

        return ss.str();
    }

    using ast_data = std::variant<
        std::monostate,      // all, none
        std::string,         // tag, wildcard
        metatag_data,        // metatag
        ast_ptr,             // not, opt
        std::vector<ast_ptr> // and, or
    >;
    
    class ast {
        private:
        node_type _type;
        ast_data _data;

        ast(node_type t, ast_data data) : _type { t }, _data { std::move(data) } { }

        std::string _join_children(std::string(ast::* method)() const, std::string_view with, bool add_parens = false) const {
            std::span<const ast_ptr> children = this->children();
            if (children.size() == 0) {
                return "";
            } else if (children.size() == 1) {
                if (add_parens && children.front()->child_count() > 1) {
                    return '(' + (children.front().get()->*method)() + ')';
                }

                return (children.front().get()->*method)();
            } else {
                std::string res;

                for (const ast_ptr& child : children) {
                    if (add_parens && child->child_count() > 1) {
                        res += '(' + (child.get()->*method)() + ')' + std::string{ with };
                    } else {
                        res += (child.get()->*method)() + std::string{ with };
                    }
                }

                return res.substr(0, res.size() - with.size());
            }
        }

        public:
        ~ast() { }

        node_type type() const { return _type; }

        std::string to_sexp() const {
            switch (_type) {
                case node_type::All:
                case node_type::None:
                    return std::string { node_type_name(_type) };

                case node_type::Tag:
                    return std::get<std::string>(_data);

                case node_type::Wildcard:
                    return std::format("(wildcard {})", std::get<std::string>(_data));

                case node_type::Metatag:
                    return format_metatag(std::get<metatag_data>(_data));

                case node_type::Not:
                case node_type::Opt:
                case node_type::And:
                case node_type::Or:
                    return std::format("({} {})", _type, _join_children(&ast::to_sexp, " "));

                default:
                    return "unknown";
            }
        }

        std::string to_infix() const {
            switch (_type) {
                case node_type::All:
                    return "";

                case node_type::None:
                    return "none";

                case node_type::Tag:
                case node_type::Wildcard:
                    return std::get<std::string>(_data);

                case node_type::Metatag:
                    return format_metatag(std::get<metatag_data>(_data));

                case node_type::Not:
                    return '-' + _join_children(&ast::to_infix, "", true);

                case node_type::Opt:
                    return '~' + _join_children(&ast::to_infix, "", true);

                case node_type::And:
                    return _join_children(&ast::to_infix, " ", true);

                case node_type::Or:
                    return _join_children(&ast::to_infix, " or ", true);

                default:
                    return "unknown";
            }
        }

        size_t child_count() const {
            switch (_type) {
                case node_type::Not:
                case node_type::Opt:
                    return 1;

                case node_type::And:
                case node_type::Or:
                    return std::get<std::vector<ast_ptr>>(_data).size();

                default:
                    return 0;
            }
        }

        std::span<const ast_ptr> children() const {
            switch (_type) {
                case node_type::Not:
                case node_type::Opt:
                    return { &std::get<ast_ptr>(_data), 1 };

                case node_type::And:
                case node_type::Or:
                    return std::get<std::vector<ast_ptr>>(_data);

                default:
                    return {};
            }
        }

        // This operation mutates the AST
        void to_cnf() {
            this->rewrite_opts();
        }

        void rewrite_opts() {
            rewrite([](ast& node) {
                switch (node.type()) {
                    case node_type::Opt: {
                        // Replace with `or` node with single child
                        std::vector<ast_ptr> children;
                        children.emplace_back(std::move(std::get<ast_ptr>(node._data)));
                        node._type = node_type::Or;
                        node._data = std::move(children);
                        break;
                    }

                    case node_type::And:
                    case node_type::Or: {
                        // Gather all opt nodes on the same level and wrap them in a single `or`
                        auto is_opt = [](const auto& c) { return c->type() == node_type::Opt; };
                        std::vector<ast_ptr>& children = std::get<std::vector<ast_ptr>>(node._data);
                        if (std::ranges::find_if(children, is_opt) != children.end()) {
                            auto non_opts = std::ranges::partition(children, is_opt);
                            
                            // Construct an `or` node with the children of each opt node as parent
                            std::vector<ast_ptr> or_children;
                            or_children.reserve(children.size() - non_opts.size());

                            for (auto it = children.begin(); it != non_opts.begin(); ++it) {
                                // Move the child nodes away
                                or_children.emplace_back(std::move(std::get<ast_ptr>((*it)->_data)));
                            }

                            // Remove moved-away children, insert new child
                            if (auto begin = children.begin(); begin != non_opts.begin()) {
                                children.erase(++begin, non_opts.begin());
                            }
                            children[0] = ast::make_or(std::move(or_children));
                        }
                    }

                    default:
                        break;
                }
            });
        }

        template <typename Func> requires requires (Func func, ast& ast) {
            func(ast);
        }
        void rewrite(Func func) {
            // First rewrite self
            func(*this);

            // Then all children, which may have been updated
            for (const ast_ptr& child : children()) {
                child->rewrite(func);
            }
        }

        static ast_ptr make_all() {
            return ast_ptr{ new ast{ node_type::All, std::monostate{} } };
        }

        static ast_ptr make_none() {
            return ast_ptr{ new ast{ node_type::None, std::monostate{} } };
        }

        static ast_ptr make_tag(std::string_view name) {
            std::string _name;
            _name.resize(name.size());
            std::ranges::transform(name, _name.begin(), [](unsigned char ch) { return std::tolower(ch); });

            return ast_ptr{ new ast{ node_type::Tag, std::move(_name) } };
        }

        static ast_ptr make_wildcard(std::string_view name) {
            std::string _name;
            _name.resize(name.size());
            std::ranges::transform(name, _name.begin(), [](unsigned char ch) { return std::tolower(ch); });

            return ast_ptr{ new ast{ node_type::Wildcard, std::move(_name) } };
        }

        static ast_ptr make_metatag(std::string_view name, std::string value, bool quoted) {
            if (!quoted) {
                // Check if it should be quoted regardless of input
                for (auto it = value.begin(); it != value.end(); ++it) {
                    if (encoding::unicode_space(it)) {
                        quoted = true;
                        break;
                    }
                }
            }

            std::string _name;
            _name.resize(name.size());
            std::ranges::transform(name, _name.begin(), [](unsigned char ch) { return std::tolower(ch); });

            return ast_ptr{ new ast{
                node_type::Metatag,
                metatag_data {
                    .name = _name,
                    .value = value,
                    .quoted = quoted
                }
            }};
        }

        static ast_ptr make_not(ast_ptr child) {
            return ast_ptr{ new ast{ node_type::Not, std::move(child) } };
        }

        static ast_ptr make_opt(ast_ptr child) {
            return ast_ptr{ new ast{ node_type::Opt, std::move(child) } };
        }

        static ast_ptr make_and(std::vector<ast_ptr> children) {
            return ast_ptr{ new ast{ node_type::And, std::move(children) } };
        }

        static ast_ptr make_or(std::vector<ast_ptr> children) {
            return ast_ptr{ new ast{ node_type::Or, std::move(children) } };
        }
    };
}

#endif /* AST_H */

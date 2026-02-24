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
    // Sorted alphabetically so we can just compare the integer value for sorting
    enum class node_type {
        All,
        And,
        Metatag,
        None,
        Not,
        Opt,
        Or,
        Tag,
        Wildcard,
    };

    static constexpr std::array<std::string_view, 9> node_type_names {
        "all", "and", "metatag", "none", "not", "opt", "or", "tag", "wildcard",
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

template <>
struct std::formatter<std::strong_ordering> : std::formatter<std::string_view> {
    auto format(std::strong_ordering comp, format_context& ctx) const {
        if (comp < 0) {
            return std::formatter<std::string_view>::format("less", ctx);
        } else if (comp > 0) {
            return std::formatter<std::string_view>::format("greater", ctx);
        } else {
            return std::formatter<std::string_view>::format("equal", ctx);
        }
    }
};

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

    static constexpr std::array<std::pair<std::string_view, std::string_view>, 18> metatag_synonyms {{
        {"comment_count", "comments"},
        {"deleted_comment_count", "deleted_comments"},
        {"active_comment_count", "active_comments"},
        {"note_count", "notes"},
        {"deleted_note_count", "deleted_notes"},
        {"active_note_count", "active_notes"},
        {"flag_count", "flags"},
        {"child_count", "children"},
        {"deleted_child_count", "deleted_children"},
        {"active_child_count", "active_children"},
        {"pool_count", "pools"},
        {"deleted_pool_count", "deleted_pools"},
        {"active_pool_count", "active_pools"},
        {"series_pool_count", "series_pools"},
        {"collection_pool_count", "collection_pools"},
        {"appeal_count", "appeals"},
        {"approval_count", "approvals"},
        {"replacement_count", "replacements"},
    }};

    static constexpr void normalize_metatag(std::string& name) {
        for (auto [normalized, synonym] : metatag_synonyms) {
            if (name == normalized) {
                return;
            } else if (name == synonym) {
                name = normalized;
                return;
            }
        }
    }
    
    class ast {
        private:
        node_type _type;
        ast_data _data;

        std::string _join_children(std::string(ast::* method)() const, std::string_view with, bool disable_parens = false) const {
            std::span<const ast_ptr> children = this->children();
            if (children.size() == 0) {
                return "";
            } else if (children.size() == 1) {
                const ast_ptr& child = children.front();

                if (disable_parens || child->child_count() > 1) {
                    return (child.get()->*method)();
                } else {
                    return '(' + (child.get()->*method)() + ')';
                }
            } else {
                std::string res;

                for (const ast_ptr& child : children) {
                    std::string str = (child.get()->*method)();

                    if (!disable_parens && child->child_count() > 1) {
                        res += '(' + str + ')';
                    } else {
                        res += str;
                    }

                    std::ranges::copy(with, std::back_inserter(res));
                }

                return res.substr(0, res.size() - with.size());
            }
        }

        public:
        ast(node_type t, ast_data data) : _type { t }, _data { std::move(data) } { }

        ~ast() { }

        std::strong_ordering operator<=>(const ast& other) const {
            if (_type == other._type) {
                // Mimic Ruby's array comparison

                switch (_type) {
                    // Single state nodes are equivalent
                    case node_type::All:
                    case node_type::None:
                        return std::strong_ordering::equal;

                    // Just compare the string
                    case node_type::Tag:
                    case node_type::Wildcard:
                        return std::get<std::string>(_data) <=> std::get<std::string>(other._data);

                    // First compare name, then value, ignore quotes
                    case node_type::Metatag: {
                        const metatag_data& lhs = std::get<metatag_data>(_data);
                        const metatag_data& rhs = std::get<metatag_data>(other._data);
                        
                        if (auto comp = lhs.name <=> rhs.name; comp != std::strong_ordering::equal) {
                            return comp;
                        } else {
                            return lhs.value <=> rhs.value;
                        }
                    }

                    // Compare subnode
                    case node_type::Not:
                    case node_type::Opt:
                        return *std::get<ast_ptr>(_data) <=> *std::get<ast_ptr>(other._data);

                    case node_type::And:
                    case node_type::Or: {
                        // First compare all common elements
                        auto lhs = this->children();
                        auto rhs = other.children();
                        for (size_t i = 0; i < std::min(lhs.size(), rhs.size()); ++i) {
                            if (std::strong_ordering comp = (*lhs[i] <=> *rhs[i]); comp != std::strong_ordering::equal) {
                                return comp;
                            }
                        }

                        // If common elements are the same, compare sizes
                        return lhs.size() <=> rhs.size();
                    }

                    default:
                        return std::strong_ordering::equal;
                }
            } else {
                return _type <=> other._type;
            }
        }

        ast_ptr copy() const {
            switch (_type) {
                case node_type::All:
                case node_type::None:
                    return std::make_unique<ast>(_type, std::monostate{});

                case node_type::Tag:
                case node_type::Wildcard:
                    return std::make_unique<ast>(_type, std::get<std::string>(_data));

                case node_type::Metatag:
                    return std::make_unique<ast>(_type, std::get<metatag_data>(_data));

                case node_type::Not:
                case node_type::Opt:
                    return std::make_unique<ast>(_type, std::get<ast_ptr>(_data)->copy());

                case node_type::And:
                case node_type::Or: {
                    return std::make_unique<ast>(_type, ast::copy(std::get<std::vector<ast_ptr>>(_data)));
                }

                default:
                    return nullptr;
            }
        }

        static std::vector<ast_ptr> copy(std::span<const ast_ptr> src) {
            std::vector<ast_ptr> res(src.size());
            std::ranges::transform(src, res.begin(), [](const ast_ptr& ptr) { return ptr->copy(); });
            return res;
        }

        node_type type() const { return _type; }

        bool is_term() const {
            switch (_type) {
                case node_type::All:
                case node_type::None:
                case node_type::Tag:
                case node_type::Metatag:
                case node_type::Wildcard:
                    return true;

                default:
                    return false;
            }
        }

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
                    return std::format("({} {})", _type, _join_children(&ast::to_sexp, " ", true));

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

                case node_type::Not: {
                    const ast_ptr& child = std::get<ast_ptr>(_data);
                    return '-' + (child->is_term() ? child->to_infix() : '(' + child->to_infix() + ')');
                }

                case node_type::Opt: {
                    const ast_ptr& child = std::get<ast_ptr>(_data);
                    return '~' + (child->is_term() ? child->to_infix() : '(' + child->to_infix() + ')');
                }

                case node_type::And:
                    return _join_children(&ast::to_infix, " ");

                case node_type::Or:
                    return _join_children(&ast::to_infix, " or ");

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
            rewrite_opts();
            
            while (simplify()) { }

            sort();
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

        // Return whether anything changed
        bool simplify() {
            // Simplify recursively
            switch (_type) {
                case node_type::All:
                case node_type::None:
                case node_type::Tag:
                case node_type::Metatag:
                case node_type::Wildcard:
                case node_type::Opt:
                    break;

                case node_type::Not: {
                    ast_ptr& child = std::get<ast_ptr>(_data);

                    switch (child->_type) {
                        // Double negation -> replace by subchild
                        case node_type::Not: {
                            ast_ptr subchild = std::move(std::get<ast_ptr>(child->_data));

                            _type = subchild->_type;
                            _data = std::move(subchild->_data);
                            return true;
                        }

                        // DeMorgan: -(A and B) -> -A or -B & -(A or B) -> -A and -B
                        case node_type::And:
                        case node_type::Or: {
                            std::vector<ast_ptr> negated_children;
                            negated_children.reserve(child->child_count());

                            for (ast_ptr& subchild : std::get<std::vector<ast_ptr>>(child->_data)) {
                                negated_children.emplace_back(ast::make_not(std::move(subchild)));
                            }

                            _type = (child->_type == node_type::And) ? node_type::Or : node_type::And;
                            _data = std::move(negated_children);
                            return true;
                        }

                        default: break;
                    }
                    break;
                }
                case node_type::And:
                case node_type::Or: {
                    std::vector<ast_ptr>& children = std::get<std::vector<ast_ptr>>(_data);

                    auto is_and = [](const ast_ptr& child) { return child->type() == node_type::And; };

                    // Single child -> replace by child
                    if (children.size() == 1) {
                        ast_ptr child = std::move(children.front());

                        _type = child->_type;
                        _data = std::move(child->_data);
                        return true;
                    } else if (std::ranges::any_of(children, [this](const ast_ptr& child) { return child->_type == this->_type; })) {
                        // Apply associative law on children of same type, move children to parent
                        std::vector<ast_ptr> new_children;
                        new_children.reserve(children.size());

                        for (ast_ptr& child : children) {
                            if (child->_type == this->_type) {
                                std::ranges::move(std::get<std::vector<ast_ptr>>(child->_data), std::back_inserter(new_children));
                            } else {
                                new_children.emplace_back(std::move(child));
                            }
                        }

                        _data = std::move(new_children);
                        return true;
                    } else if (_type == node_type::Or && std::ranges::any_of(children, is_and)) {
                        // XXX: This is probably easier if all `and` and `or` nodes were binary, but that may require iteration
                        // * Partition out all `and` nodes
                        // * For every `and` child:
                        // ** Create an `or` node for every subchild
                        // ** Set all non-and children as its children, plus one of the subchildren
                        auto rest = std::ranges::partition(children, is_and);
                        std::span<ast_ptr> ands { children.begin(), rest.begin() };

                        std::vector<ast_ptr> res;
                        res.emplace_back(ast::make_or(ast::copy(rest)));

                        for (const ast_ptr& child : ands) {
                            std::vector<ast_ptr> next;
                            next.reserve(res.size() * child->child_count());

                            for (const ast_ptr& subchild : child->children()) {
                                for (const ast_ptr& or_node : res) {
                                    auto copy = or_node->copy();
                                    std::get<std::vector<ast_ptr>>(copy->_data).emplace_back(subchild->copy());
                                    next.emplace_back(std::move(copy));
                                }
                            }

                            res = std::move(next);
                        }

                        _type = node_type::And;
                        _data = std::move(res);

                        return true;
                    }
                    break;
                }
            }

            bool changed = false;
            for (const ast_ptr& child : children()) {
                changed = child->simplify() || changed;
            }
            return changed;
        }

        void sort() {
            switch (_type) {
                case node_type::Opt:
                case node_type::Not:
                    // Sort subnodes
                    std::get<ast_ptr>(_data)->sort();
                    break;

                case node_type::And:
                case node_type::Or: {
                    // First sort subnodes, then sort ourselves
                    std::vector<ast_ptr>& children = std::get<std::vector<ast_ptr>>(_data);
                    for (ast_ptr& child : children) {
                        child->sort();
                    }

                    std::ranges::sort(children, [](const ast_ptr& lhs, const ast_ptr& rhs) { return *lhs < *rhs; });

                    break;
                }

                default:
                    break;
            }
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
            normalize_metatag(_name);

            // This one has to be special
            if (_name == "order") {
                // Normalize order too, with and without "_asc" and "_desc" suffix
                for (std::string_view which : { "_asc", "_desc", "" }) {
                    if (value.ends_with(which)) {
                        value.erase(std::prev(value.end(), which.size()), value.end());
                        normalize_metatag(value);
                        value.insert(value.end(), which.begin(), which.end());
                        break;
                    }
                }
            }

            return ast_ptr{ new ast{
                node_type::Metatag,
                metatag_data {
                    .name = std::move(_name),
                    .value = std::move(value),
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

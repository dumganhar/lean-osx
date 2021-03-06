/*
Copyright (c) 2014 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include "kernel/formatter.h"
#include "kernel/context.h"

namespace lean {
/**
   \brief Very basic printer for expressions.
   It is mainly used when debugging code.
*/
struct print_expr_fn {
    std::ostream & m_out;

    std::ostream & out() { return m_out; }

    bool is_atomic(expr const & a) {
        return ::lean::is_atomic(a) || is_mlocal(a);
    }

    void print_child(expr const & a, context const & c) {
        if (is_atomic(a)) {
            print(a, c);
        } else {
            out() << "("; print(a, c); out() << ")";
        }
    }

    void print_macro(expr const & a, context const & c) {
        if (macro_num_args(a) > 0) out() << "(";
        macro_def(a).display(out());
        for (unsigned i = 0; i < macro_num_args(a); i++) {
            out() << " "; print(macro_arg(a, i), c);
        }
        if (macro_num_args(a) > 0) out() << ")";
    }

    void print_sort(expr const & a) {
        if (is_zero(sort_level(a))) {
            out() << "Bool";
        } else if (is_one(sort_level(a))) {
            out() << "Type";
        } else {
            out() << "Type.{" << sort_level(a) << "}";
        }
    }

    void print_app(expr const & e, context const & c) {
        expr const & f = app_fn(e);
        if (is_app(f))
            print(f, c);
        else
            print_child(f, c);
        out() << " ";
        print_child(app_arg(e), c);
    }

    void print_arrow_body(expr const & a, context const & c) {
        if (is_atomic(a) || is_arrow(a))
            return print(a, c);
        else
            return print_child(a, c);
    }

    void print_binder(char const * bname, expr const & e, context const & c) {
        out() << bname << " " << binder_name(e) << " : ";
        print_child(binder_domain(e), c);
        out() << ", ";
        print_child(binder_body(e), extend(c, binder_name(e), binder_domain(e)));
    }

    void print_const(expr const & a) {
        list<level> const & ls = const_level_params(a);
        out() << const_name(a);
        if (!is_nil(ls)) {
            out() << ".{";
            bool first = true;
            for (auto l : ls) {
                if (first) first = false; else out() << " ";
                out() << l;
            }
            out() << "}";
        }
    }

    void print(expr const & a, context const & c) {
        switch (a.kind()) {
        case expr_kind::Meta: case expr_kind::Local:
            out() << mlocal_name(a);
            break;
        case expr_kind::Var: {
            auto e = find(c, var_idx(a));
            if (e)
                out() << e->first << "#" << var_idx(a);
            else
                out() << "#" << var_idx(a);
            break;
        }
        case expr_kind::Constant:
            print_const(a);
            break;
        case expr_kind::App:
            print_app(a, c);
            break;
        case expr_kind::Lambda:
            print_binder("fun", a, c);
            break;
        case expr_kind::Pi:
            if (!is_arrow(a)) {
                print_binder("Pi", a, c);
            } else {
                print_child(binder_domain(a), c);
                out() << " -> ";
                print_arrow_body(binder_body(a), extend(c, binder_name(a), binder_domain(a)));
            }
            break;
        case expr_kind::Let:
            out() << "let " << let_name(a);
            out() << " : ";
            print(let_type(a), c);
            out() << " := ";
            print(let_value(a), c);
            out() << " in ";
            print_child(let_body(a), extend(c, let_name(a), let_value(a)));
            break;
        case expr_kind::Sort:
            print_sort(a);
            break;
        case expr_kind::Macro:
            print_macro(a, c);
            break;
        }
    }

    print_expr_fn(std::ostream & out):m_out(out) {}

    void operator()(expr const & e, context const & c) {
        print(e, c);
    }
};

std::ostream & operator<<(std::ostream & out, expr const & e) {
    print_expr_fn pr(out);
    pr(e, context());
    return out;
}

class simple_formatter_cell : public formatter_cell {
public:
    virtual format operator()(environment const &, expr const & e, options const &) {
        std::ostringstream s; s << e; return format(s.str());
    }
};
formatter mk_simple_formatter() {
    return mk_formatter(simple_formatter_cell());
}
void print(lean::expr const & a) { std::cout << a << std::endl; }
}

/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include "kernel/occurs.h"
#include "kernel/metavar.h"
#include "kernel/expr_maps.h"
#include "kernel/replace_visitor.h"
#include "library/expr_pair.h"
#include "library/placeholder.h"
#include "library/kernel_bindings.h"

namespace lean {
static name g_placeholder_name("_");
expr mk_placeholder(optional<expr> const & t) {
    return mk_constant(g_placeholder_name, t);
}

bool is_placeholder(expr const & e) {
    return is_constant(e) && const_name(e) == g_placeholder_name;
}

bool has_placeholder(expr const & e) {
    return occurs(mk_placeholder(), e);
}

class replace_placeholders_with_metavars_proc : public replace_visitor {
    metavar_env const & m_menv;
    expr_map<expr> *    m_new2old;
protected:
    expr visit_constant(expr const & e, context const & c) {
        if (is_placeholder(e)) {
            return m_menv->mk_metavar(c, const_type(e));
        } else {
            return e;
        }
    }

    expr visit(expr const & e, context const & c) {
        expr r = replace_visitor::visit(e, c);
        if (!is_eqp(r, e) && m_new2old)
            m_new2old->insert(expr_pair(r, e));
        return r;
    }
public:
    replace_placeholders_with_metavars_proc(metavar_env const & menv, expr_map<expr> * new2old):
        m_menv(menv),
        m_new2old(new2old) {
    }
};

expr replace_placeholders_with_metavars(expr const & e, metavar_env const & menv, expr_map<expr> * new2old) {
    replace_placeholders_with_metavars_proc proc(menv, new2old);
    return proc(e);
}

static int mk_placeholder(lua_State * L) {
    if (lua_gettop(L) == 0) {
        return push_expr(L, mk_placeholder());
    } else {
        return push_expr(L, mk_placeholder(some_expr(to_expr(L, 1))));
    }
}

void open_placeholder(lua_State * L) {
    SET_GLOBAL_FUN(mk_placeholder, "mk_placeholder");
}
}

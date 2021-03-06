/*
Copyright (c) 2013-2014 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
        Soonho Kong
*/
#include <vector>
#include <sstream>
#include <string>
#include <algorithm>
#include <limits>
#include "util/list_fn.h"
#include "util/hash.h"
#include "util/buffer.h"
#include "util/object_serializer.h"
#include "kernel/expr.h"
#include "kernel/expr_eq_fn.h"
#include "kernel/free_vars.h"
#include "kernel/max_sharing.h"

namespace lean {
static expr g_dummy(mk_var(0));
expr::expr():expr(g_dummy) {}

unsigned hash_levels(levels const & ls) {
    unsigned r = 23;
    for (auto const & l : ls)
        r = hash(hash(l), r);
    return r;
}

expr_cell::expr_cell(expr_kind k, unsigned h, bool has_mv, bool has_local, bool has_param_univ):
    m_flags(0),
    m_kind(static_cast<unsigned>(k)),
    m_has_mv(has_mv),
    m_has_local(has_local),
    m_has_param_univ(has_param_univ),
    m_hash(h),
    m_tag(nulltag),
    m_rc(0) {
    // m_hash_alloc does not need to be a unique identifier.
    // We want diverse hash codes because given expr_cell * c1 and expr_cell * c2,
    // if c1 != c2, then there is high probability c1->m_hash_alloc != c2->m_hash_alloc.
    // Remark: using pointer address as a hash code is not a good idea.
    //    - each execution run will behave differently.
    //    - the hash is not diverse enough
    static LEAN_THREAD_LOCAL unsigned g_hash_alloc_counter = 0;
    m_hash_alloc = g_hash_alloc_counter;
    g_hash_alloc_counter++;
}

void expr_cell::dec_ref(expr & e, buffer<expr_cell*> & todelete) {
    if (e.m_ptr) {
        expr_cell * c = e.steal_ptr();
        lean_assert(!(e.m_ptr));
        if (c->dec_ref_core())
            todelete.push_back(c);
    }
}

optional<bool> expr_cell::is_arrow() const {
    // it is stored in bits 0-1
    unsigned r = (m_flags & (1+2));
    if (r == 0) {
        return optional<bool>();
    } else if (r == 1) {
        return optional<bool>(true);
    } else {
        lean_assert(r == 2);
        return optional<bool>(false);
    }
}

void expr_cell::set_is_arrow(bool flag) {
    unsigned mask = flag ? 1 : 2;
    m_flags |= mask;
    lean_assert(is_arrow() && *is_arrow() == flag);
}

void expr_cell::set_tag(tag t) {
    m_tag = t;
}

bool is_meta(expr const & e) {
    expr const * it = &e;
    while (is_app(*it)) {
        it = &(app_fn(*it));
    }
    return is_metavar(*it);
}

// Expr variables
expr_var::expr_var(unsigned idx):
    expr_cell(expr_kind::Var, idx, false, false, false),
    m_vidx(idx) {
    if (idx == std::numeric_limits<unsigned>::max())
        throw exception("invalid free variable index, de Bruijn index is too big");
}

// Expr constants
expr_const::expr_const(name const & n, levels const & ls):
    expr_cell(expr_kind::Constant, ::lean::hash(n.hash(), hash_levels(ls)), has_meta(ls), false, has_param(ls)),
    m_name(n),
    m_levels(ls) {}

// Expr metavariables and local variables
expr_mlocal::expr_mlocal(bool is_meta, name const & n, expr const & t):
    expr_cell(is_meta ? expr_kind::Meta : expr_kind::Local, n.hash(), is_meta || t.has_metavar(), !is_meta || t.has_local(), t.has_param_univ()),
    m_name(n),
    m_type(t) {}
void expr_mlocal::dealloc(buffer<expr_cell*> & todelete) {
    dec_ref(m_type, todelete);
    delete(this);
}

// Composite expressions
expr_composite::expr_composite(expr_kind k, unsigned h, bool has_mv, bool has_local, bool has_param_univ, unsigned d, unsigned fv_range):
    expr_cell(k, h, has_mv, has_local, has_param_univ),
    m_depth(d),
    m_free_var_range(fv_range) {}

// Expr applications
expr_app::expr_app(expr const & fn, expr const & arg):
    expr_composite(expr_kind::App, ::lean::hash(fn.hash(), arg.hash()),
                   fn.has_metavar()    || arg.has_metavar(),
                   fn.has_local()      || arg.has_local(),
                   fn.has_param_univ() || arg.has_param_univ(),
                   std::max(get_depth(fn), get_depth(arg)) + 1,
                   std::max(get_free_var_range(fn), get_free_var_range(arg))),
    m_fn(fn), m_arg(arg) {}
void expr_app::dealloc(buffer<expr_cell*> & todelete) {
    dec_ref(m_fn, todelete);
    dec_ref(m_arg, todelete);
    delete(this);
}

static unsigned dec(unsigned k) { return k == 0 ? 0 : k - 1; }

// Expr binders (Lambda, Pi)
expr_binder::expr_binder(expr_kind k, name const & n, expr const & t, expr const & b, expr_binder_info const & i):
    expr_composite(k, ::lean::hash(t.hash(), b.hash()),
                   t.has_metavar()    || b.has_metavar(),
                   t.has_local()      || b.has_local(),
                   t.has_param_univ() || b.has_param_univ(),
                   std::max(get_depth(t), get_depth(b)) + 1,
                   std::max(get_free_var_range(t), dec(get_free_var_range(b)))),
    m_name(n),
    m_domain(t),
    m_body(b),
    m_info(i) {
    lean_assert(k == expr_kind::Lambda || k == expr_kind::Pi);
}
void expr_binder::dealloc(buffer<expr_cell*> & todelete) {
    dec_ref(m_body, todelete);
    dec_ref(m_domain, todelete);
    delete(this);
}

// Expr Sort
expr_sort::expr_sort(level const & l):
    expr_cell(expr_kind::Sort, ::lean::hash(l), has_meta(l), false, has_param(l)),
    m_level(l) {
}
expr_sort::~expr_sort() {}

// Expr Let
expr_let::expr_let(name const & n, expr const & t, expr const & v, expr const & b):
    expr_composite(expr_kind::Let, ::lean::hash(v.hash(), b.hash()),
                   t.has_metavar()    || v.has_metavar()    || b.has_metavar(),
                   t.has_local()      || v.has_local()      || b.has_local(),
                   t.has_param_univ() || v.has_param_univ() || b.has_param_univ(),
                   std::max({get_depth(t), get_depth(v), get_depth(b)}) + 1,
                   std::max({get_free_var_range(t), dec(get_free_var_range(v)), dec(get_free_var_range(b))})),
    m_name(n),
    m_type(t),
    m_value(v),
    m_body(b) {
}
void expr_let::dealloc(buffer<expr_cell*> & todelete) {
    dec_ref(m_body, todelete);
    dec_ref(m_value, todelete);
    dec_ref(m_type, todelete);
    delete(this);
}
expr_let::~expr_let() {}

// Macro definition
bool macro_definition_cell::lt(macro_definition_cell const &) const { return false; }
bool macro_definition_cell::operator==(macro_definition_cell const & other) const { return typeid(*this) == typeid(other); }
unsigned macro_definition_cell::trust_level() const { return 0; }

format macro_definition_cell::pp(formatter const &, options const &) const { return format(get_name()); }
void macro_definition_cell::display(std::ostream & out) const { out << get_name(); }
bool macro_definition_cell::is_atomic_pp(bool, bool) const { return true; }
unsigned macro_definition_cell::hash() const { return get_name().hash(); }

typedef std::unordered_map<std::string, macro_definition_cell::reader> macro_readers;
static std::unique_ptr<macro_readers> g_macro_readers;
macro_readers & get_macro_readers() {
    if (!g_macro_readers)
        g_macro_readers.reset(new macro_readers());
    return *(g_macro_readers.get());
}

void macro_definition_cell::register_deserializer(std::string const & k, macro_definition_cell::reader rd) {
    macro_readers & readers = get_macro_readers();
    lean_assert(readers.find(k) == readers.end());
    readers[k] = rd;
}
static expr read_macro_definition(deserializer & d, unsigned num, expr const * args) {
    auto k  = d.read_string();
    macro_readers & readers = get_macro_readers();
    auto it = readers.find(k);
    lean_assert(it != readers.end());
    return it->second(d, num, args);
}

macro_definition::macro_definition(macro_definition_cell * ptr):m_ptr(ptr) { lean_assert(m_ptr); m_ptr->inc_ref(); }
macro_definition::macro_definition(macro_definition const & s):m_ptr(s.m_ptr) { if (m_ptr) m_ptr->inc_ref(); }
macro_definition::macro_definition(macro_definition && s):m_ptr(s.m_ptr) { s.m_ptr = nullptr; }
macro_definition::~macro_definition() { if (m_ptr) m_ptr->dec_ref(); }
macro_definition & macro_definition::operator=(macro_definition const & s) { LEAN_COPY_REF(s); }
macro_definition & macro_definition::operator=(macro_definition && s) { LEAN_MOVE_REF(s); }
bool macro_definition::operator<(macro_definition const & other) const {
    if (get_name() == other.get_name())
        return m_ptr->lt(*other.m_ptr);
    else
        return get_name() < other.get_name();
}

static unsigned max_depth(unsigned num, expr const * args) {
    unsigned r = 0;
    for (unsigned i = 0; i < num; i++) {
        unsigned d = get_depth(args[i]);
        if (d > r)
            r = d;
    }
    return r;
}

static unsigned get_free_var_range(unsigned num, expr const * args) {
    unsigned r = 0;
    for (unsigned i = 0; i < num; i++) {
        unsigned d = get_free_var_range(args[i]);
        if (d > r)
            r = d;
    }
    return r;
}

expr_macro::expr_macro(macro_definition const & m, unsigned num, expr const * args):
    expr_composite(expr_kind::Macro,
                   lean::hash(num, [&](unsigned i) { return args[i].hash(); }, m.hash()),
                   std::any_of(args, args+num, [](expr const & e) { return e.has_metavar(); }),
                   std::any_of(args, args+num, [](expr const & e) { return e.has_local(); }),
                   std::any_of(args, args+num, [](expr const & e) { return e.has_param_univ(); }),
                   max_depth(num, args) + 1,
                   get_free_var_range(num, args)),
    m_definition(m),
    m_num_args(num) {
    m_args = new expr[num];
    for (unsigned i = 0; i < m_num_args; i++)
        m_args[i] = args[i];
}
void expr_macro::dealloc(buffer<expr_cell*> & todelete) {
    for (unsigned i = 0; i < m_num_args; i++) dec_ref(m_args[i], todelete);
    delete(this);
}
expr_macro::~expr_macro() {
    delete[] m_args;
}

void expr_cell::dealloc() {
    try {
        buffer<expr_cell*> todo;
        todo.push_back(this);
        while (!todo.empty()) {
            expr_cell * it = todo.back();
            todo.pop_back();
            lean_assert(it->get_rc() == 0);
            switch (it->kind()) {
            case expr_kind::Var:        delete static_cast<expr_var*>(it); break;
            case expr_kind::Macro:      static_cast<expr_macro*>(it)->dealloc(todo); break;
            case expr_kind::Meta:
            case expr_kind::Local:      static_cast<expr_mlocal*>(it)->dealloc(todo); break;
            case expr_kind::Constant:   delete static_cast<expr_const*>(it); break;
            case expr_kind::Sort:       delete static_cast<expr_sort*>(it); break;
            case expr_kind::App:        static_cast<expr_app*>(it)->dealloc(todo); break;
            case expr_kind::Lambda:
            case expr_kind::Pi:         static_cast<expr_binder*>(it)->dealloc(todo); break;
            case expr_kind::Let:        static_cast<expr_let*>(it)->dealloc(todo); break;
            }
        }
    } catch (std::bad_alloc&) {
        // We need this catch, because push_back may fail when expanding the buffer.
        // In this case, we avoid the crash, and "accept" the memory leak.
    }
}

// Auxiliary constructors
expr mk_app(expr const & f, unsigned num_args, expr const * args) {
    expr r = f;
    for (unsigned i = 0; i < num_args; i++)
        r = mk_app(r, args[i]);
    return r;
}

expr mk_app(unsigned num_args, expr const * args) {
    lean_assert(num_args >= 2);
    return mk_app(mk_app(args[0], args[1]), num_args - 2, args+2);
}

expr mk_rev_app(expr const & f, unsigned num_args, expr const * args) {
    expr r = f;
    unsigned i = num_args;
    while (i > 0) {
        --i;
        r = mk_app(r, args[i]);
    }
    return r;
}

expr mk_rev_app(unsigned num_args, expr const * args) {
    lean_assert(num_args >= 2);
    return mk_rev_app(mk_app(args[num_args-1], args[num_args-2]), num_args-2, args);
}

expr mk_app_vars(expr const & f, unsigned n) {
    expr r = f;
    while (n > 0) {
        --n;
        r = mk_app(r, Var(n));
    }
    return r;
}

void get_app_args(expr const & e, buffer<expr> & args) {
    if (is_app(e)) {
        get_app_args(app_fn(e), args);
        args.push_back(app_arg(e));
    }
}

static name g_default_var_name("a");
bool is_default_var_name(name const & n) { return n == g_default_var_name; }
expr mk_arrow(expr const & t, expr const & e) { return mk_pi(g_default_var_name, t, e); }

expr Bool = mk_sort(mk_level_zero());
expr Type = mk_sort(mk_level_one());
expr mk_Bool() { return Bool; }
expr mk_Type() { return Type; }

unsigned get_depth(expr const & e) {
    switch (e.kind()) {
    case expr_kind::Var:  case expr_kind::Constant: case expr_kind::Sort:
    case expr_kind::Meta: case expr_kind::Local:
        return 1;
    case expr_kind::Lambda: case expr_kind::Pi:  case expr_kind::Macro:
    case expr_kind::App:    case expr_kind::Let:
        return static_cast<expr_composite*>(e.raw())->m_depth;
    }
    lean_unreachable(); // LCOV_EXCL_LINE
}

unsigned get_free_var_range(expr const & e) {
    switch (e.kind()) {
    case expr_kind::Var:
        return var_idx(e) + 1;
    case expr_kind::Constant: case expr_kind::Sort:
        return 0;
    case expr_kind::Meta: case expr_kind::Local:
        return get_free_var_range(mlocal_type(e));
    case expr_kind::Lambda: case expr_kind::Pi:
    case expr_kind::App:    case expr_kind::Let:
    case expr_kind::Macro:
        return static_cast<expr_composite*>(e.raw())->m_free_var_range;
    }
    lean_unreachable(); // LCOV_EXCL_LINE
}

bool operator==(expr const & a, expr const & b) { return expr_eq_fn()(a, b); }

static expr copy_tag(expr const & e, expr && new_e) {
    tag t = e.get_tag();
    if (t != nulltag)
        new_e.set_tag(t);
    return new_e;
}

expr update_app(expr const & e, expr const & new_fn, expr const & new_arg) {
    if (!is_eqp(app_fn(e), new_fn) || !is_eqp(app_arg(e), new_arg))
        return copy_tag(e, mk_app(new_fn, new_arg));
    else
        return e;
}

expr update_rev_app(expr const & e, unsigned num, expr const * new_args) {
    expr const * it = &e;
    for (unsigned i = 0; i < num - 1; i++) {
        if (!is_app(*it) || !is_eqp(app_arg(*it), new_args[i]))
            return copy_tag(e, mk_rev_app(num, new_args));
        it = &app_fn(*it);
    }
    if (!is_eqp(*it, new_args[num - 1]))
        return copy_tag(e, mk_rev_app(num, new_args));
    return e;
}

expr update_binder(expr const & e, expr const & new_domain, expr const & new_body) {
    if (!is_eqp(binder_domain(e), new_domain) || !is_eqp(binder_body(e), new_body))
        return copy_tag(e, mk_binder(e.kind(), binder_name(e), new_domain, new_body, binder_info(e)));
    else
        return e;
}

expr update_let(expr const & e, expr const & new_type, expr const & new_val, expr const & new_body) {
    if (!is_eqp(let_type(e), new_type) || !is_eqp(let_value(e), new_val) || !is_eqp(let_body(e), new_body))
        return copy_tag(e, mk_let(let_name(e), new_type, new_val, new_body));
    else
        return e;
}

expr update_mlocal(expr const & e, expr const & new_type) {
    if (!is_eqp(mlocal_type(e), new_type))
        return copy_tag(e, mk_mlocal(is_metavar(e), mlocal_name(e), new_type));
    else
        return e;
}

expr update_sort(expr const & e, level const & new_level) {
    if (!is_eqp(sort_level(e), new_level))
        return copy_tag(e, mk_sort(new_level));
    else
        return e;
}

expr update_constant(expr const & e, levels const & new_levels) {
    if (!is_eqp(const_level_params(e), new_levels))
        return copy_tag(e, mk_constant(const_name(e), new_levels));
    else
        return e;
}

expr update_macro(expr const & e, unsigned num, expr const * args) {
    if (num == macro_num_args(e)) {
        unsigned i = 0;
        for (i = 0; i < num; i++) {
            if (!is_eqp(macro_arg(e, i), args[i]))
                break;
        }
        if (i == num)
            return e;
    }
    return copy_tag(e, mk_macro(to_macro(e)->m_definition, num, args));
}

bool is_atomic(expr const & e) {
    switch (e.kind()) {
    case expr_kind::Constant: case expr_kind::Sort:
    case expr_kind::Var:
        return true;
    case expr_kind::Macro:
        return to_macro(e)->get_num_args() == 0;
    case expr_kind::App:      case expr_kind::Let:
    case expr_kind::Meta:     case expr_kind::Local:
    case expr_kind::Lambda:   case expr_kind::Pi:
        return false;
    }
    lean_unreachable(); // LCOV_EXCL_LINE
}

bool is_arrow(expr const & t) {
    optional<bool> r = t.raw()->is_arrow();
    if (r) {
        return *r;
    } else {
        bool res = is_pi(t) && !has_free_var(binder_body(t), 0);
        t.raw()->set_is_arrow(res);
        return res;
    }
}

expr copy(expr const & a) {
    switch (a.kind()) {
    case expr_kind::Var:      return mk_var(var_idx(a));
    case expr_kind::Constant: return mk_constant(const_name(a), const_level_params(a));
    case expr_kind::Sort:     return mk_sort(sort_level(a));
    case expr_kind::Macro:    return mk_macro(to_macro(a)->m_definition, macro_num_args(a), macro_args(a));
    case expr_kind::App:      return mk_app(app_fn(a), app_arg(a));
    case expr_kind::Lambda:   return mk_lambda(binder_name(a), binder_domain(a), binder_body(a), binder_info(a));
    case expr_kind::Pi:       return mk_pi(binder_name(a), binder_domain(a), binder_body(a), binder_info(a));
    case expr_kind::Let:      return mk_let(let_name(a), let_type(a), let_value(a), let_body(a));
    case expr_kind::Meta:     return mk_metavar(mlocal_name(a), mlocal_type(a));
    case expr_kind::Local:    return mk_local(mlocal_name(a), mlocal_type(a));
    }
    lean_unreachable(); // LCOV_EXCL_LINE
}

serializer & operator<<(serializer & s, expr_binder_info const & i) {
    s.write_bool(i.is_implicit());
    s.write_bool(i.is_cast());
    return s;
}

static expr_binder_info read_expr_binder_info(deserializer & d) {
    bool imp = d.read_bool();
    bool cast = d.read_bool();
    return expr_binder_info(imp, cast);
}

class expr_serializer : public object_serializer<expr, expr_hash_alloc, expr_eqp> {
    typedef object_serializer<expr, expr_hash_alloc, expr_eqp> super;
    max_sharing_fn m_max_sharing_fn;

    void write_core(expr const & a) {
        auto k = a.kind();
        super::write_core(a, static_cast<char>(k), [&]() {
                serializer & s = get_owner();
                switch (k) {
                case expr_kind::Var:
                    s << var_idx(a);
                    break;
                case expr_kind::Constant:
                    s << const_name(a) << const_level_params(a);
                    break;
                case expr_kind::Sort:
                    s << sort_level(a);
                    break;
                case expr_kind::Macro:
                    s << macro_num_args(a);
                    for (unsigned i = 0; i < macro_num_args(a); i++) {
                        write_core(macro_arg(a, i));
                    }
                    macro_def(a).write(s);
                    break;
                case expr_kind::App:
                    write_core(app_fn(a)); write_core(app_arg(a));
                    break;
                case expr_kind::Lambda: case expr_kind::Pi:
                    s << binder_name(a) << binder_info(a); write_core(binder_domain(a)); write_core(binder_body(a));
                    break;
                case expr_kind::Let:
                    s << let_name(a); write_core(let_type(a)); write_core(let_value(a)); write_core(let_body(a));
                    break;
                case expr_kind::Meta: case expr_kind::Local:
                    s << mlocal_name(a); write_core(mlocal_type(a));
                    break;
                }
            });
    }
public:
    void write(expr const & a) {
        write_core(m_max_sharing_fn(a));
    }
};

class expr_deserializer : public object_deserializer<expr> {
    typedef object_deserializer<expr> super;
public:
    expr read_binder(expr_kind k) {
        deserializer & d = get_owner();
        name n             = read_name(d);
        expr_binder_info i = read_expr_binder_info(d);
        expr t             = read();
        return mk_binder(k, n, t, read(), i);
    }

    expr read() {
        return super::read_core([&](char c) {
                deserializer & d = get_owner();
                auto k = static_cast<expr_kind>(c);
                switch (k) {
                case expr_kind::Var:
                    return mk_var(d.read_unsigned());
                case expr_kind::Constant: {
                    auto n = read_name(d);
                    return mk_constant(n, read_levels(d));
                }
                case expr_kind::Sort:
                    return mk_sort(read_level(d));
                case expr_kind::Macro: {
                    unsigned n = d.read_unsigned();
                    buffer<expr> args;
                    for (unsigned i = 0; i < n; i++) {
                        args.push_back(read());
                    }
                    return read_macro_definition(d, args.size(), args.data());
                }
                case expr_kind::App: {
                    expr f = read();
                    return mk_app(f, read());
                }
                case expr_kind::Lambda: case expr_kind::Pi:
                    return read_binder(k);
                case expr_kind::Let: {
                    name n = read_name(d);
                    expr t = read();
                    expr v = read();
                    return mk_let(n, t, v, read());
                }
                case expr_kind::Meta: {
                    name n = read_name(d);
                    return mk_metavar(n, read());
                }
                case expr_kind::Local: {
                    name n = read_name(d);
                    return mk_local(n, read());
                }}
                throw_corrupted_file(); // LCOV_EXCL_LINE
            });
    }
};

struct expr_sd {
    unsigned m_s_extid;
    unsigned m_d_extid;
    expr_sd() {
        m_s_extid = serializer::register_extension([](){ return std::unique_ptr<serializer::extension>(new expr_serializer()); });
        m_d_extid = deserializer::register_extension([](){ return std::unique_ptr<deserializer::extension>(new expr_deserializer()); });
    }
};
static expr_sd g_expr_sd;

serializer & operator<<(serializer & s, expr const & n) {
    s.get_extension<expr_serializer>(g_expr_sd.m_s_extid).write(n);
    return s;
}

expr read_expr(deserializer & d) {
    return d.get_extension<expr_deserializer>(g_expr_sd.m_d_extid).read();
}
}

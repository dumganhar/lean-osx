local env = empty_environment()
env = add_decl(env, mk_var_decl("f", mk_arrow(Bool, mk_arrow(Bool, Bool))))
local f   = Const("f")
local x   = Const("x")
local y   = Const("y")
local z   = Const("z")
local tc  = type_checker(env)
print(tc:whnf(Fun(x, Bool, f(x))))
print(tc:whnf(Fun(x, Bool, Fun(y, Bool, f(x, y)))))
print(tc:whnf(Fun(x, Bool, Fun(y, Bool, f(Const("a"), y)))))
print(tc:whnf(Fun(z, Bool, Fun(x, Bool, Fun(y, Bool, f(z, y))))))
assert(tc:is_def_eq(f, Fun(x, Bool, f(x))))
assert(tc:is_def_eq(f, Fun(x, Bool, Fun(y, Bool, f(x, y)))))
local a = Const("a")
local A = Const("A")
assert(tc:is_def_eq(Fun(a, A, a)(f), Fun(x, Bool, Fun(y, Bool, f(x, y)))))

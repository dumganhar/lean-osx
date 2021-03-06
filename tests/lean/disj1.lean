import tactic

theorem T1 (a b : Bool) : a \/ b → b \/ a.
    (* disj_hyp_tac() *)
    (* disj_tac() *)
    back
    exact.
    (* disj_tac() *)
    exact.
    done.

(*
simple_tac = Repeat(OrElse(assumption_tac(), disj_hyp_tac(), disj_tac())) .. now_tac()
*)

theorem T2 (a b : Bool) : a \/ b → b \/ a.
    simple_tac.
    done.

print environment 1.
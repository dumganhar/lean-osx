variable a : Bool
variable b : Bool
-- Conjunctions
print a && b
print a && b && a
print a /\ b
print a ∧ b
print (and a b)
print and a b
-- Disjunctions
print a || b
print a \/ b
print a ∨ b
print (or a b)
print or a (or a b)
-- Simple Formulas
print a → b → a
check a → b
eval a → a
eval true → a
-- Simple proof
axiom H1 : a
axiom H2 : a → b

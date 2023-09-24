open Sexplib.Std;

[@deriving sexp]
type t =
  | Truth
  | Falsity
  | Hole
  | Int(int)
  | NotInt(int)
  | Float(float)
  | NotFloat(float)
  | String(string)
  | NotString(string)
  | And(t, t)
  | Or(t, t)
  | InjL(t)
  | InjR(t)
  | Pair(t, t);

let rec constrains = (c: t, ty: Typ.t): bool =>
  switch (c, Typ.weak_head_normalize(Builtins.ctx_init, ty)) {
  | (Truth, _)
  | (Falsity, _)
  | (Hole, _) => true
  | (Int(_) | NotInt(_), Int) => true
  | (Int(_) | NotInt(_), _) => false
  | (Float(_) | NotFloat(_), Float) => true
  | (Float(_) | NotFloat(_), _) => false
  | (String(_) | NotString(_), String) => true
  | (String(_) | NotString(_), _) => false
  | (And(c1, c2), _) => constrains(c1, ty) && constrains(c2, ty)
  | (Or(c1, c2), _) => constrains(c1, ty) && constrains(c2, ty)
  // Treates sum as if it is left associative
  | (InjL(c1), Sum(map)) =>
    switch (List.hd(map)) {
    | (_, Some(ty1)) => constrains(c1, ty1)
    | _ => false
    }
  | (InjL(_), _) => false
  | (InjR(c2), Sum(map)) =>
    switch (List.tl(map)) {
    | [] => false
    | [(_, Some(ty2))] => constrains(c2, ty2)
    | map' => constrains(c2, Sum(map'))
    }
  | (InjR(_), _) => false
  | (Pair(c1, c2), _) => constrains(c1, ty) && constrains(c2, ty)
  };

let rec dual = (c: t): t =>
  switch (c) {
  | Truth => Falsity
  | Falsity => Truth
  | Hole => Hole
  | Int(n) => NotInt(n)
  | NotInt(n) => Int(n)
  | Float(n) => NotFloat(n)
  | NotFloat(n) => Float(n)
  | String(n) => NotString(n)
  | NotString(n) => String(n)
  | And(c1, c2) => Or(dual(c1), dual(c2))
  | Or(c1, c2) => And(dual(c1), dual(c2))
  | InjL(c1) => Or(InjL(dual(c1)), InjR(Truth))
  | InjR(c2) => Or(InjR(dual(c2)), InjL(Truth))
  | Pair(c1, c2) =>
    Or(
      Pair(c1, dual(c2)),
      Or(Pair(dual(c1), c2), Pair(dual(c1), dual(c2))),
    )
  };

/** substitute Truth for Hole */
let rec truify = (c: t): t =>
  switch (c) {
  | Hole => Truth
  | Truth
  | Falsity
  | Int(_)
  | NotInt(_)
  | Float(_)
  | NotFloat(_)
  | String(_)
  | NotString(_) => c
  | And(c1, c2) => And(truify(c1), truify(c2))
  | Or(c1, c2) => Or(truify(c1), truify(c2))
  | InjL(c) => InjL(truify(c))
  | InjR(c) => InjR(truify(c))
  | Pair(c1, c2) => Pair(truify(c1), truify(c2))
  };

/** substitute Falsity for Hole */
let rec falsify = (c: t): t =>
  switch (c) {
  | Hole => Falsity
  | Truth
  | Falsity
  | Int(_)
  | NotInt(_)
  | Float(_)
  | NotFloat(_)
  | String(_)
  | NotString(_) => c
  | And(c1, c2) => And(falsify(c1), falsify(c2))
  | Or(c1, c2) => Or(falsify(c1), falsify(c2))
  | InjL(c) => InjL(falsify(c))
  | InjR(c) => InjR(falsify(c))
  | Pair(c1, c2) => Pair(falsify(c1), falsify(c2))
  };

let unwrapL =
  fun
  | InjL(c) => c
  | _ => failwith("input can only be InjL(_)");

let unwrapR =
  fun
  | InjR(c) => c
  | _ => failwith("input can only be InjR(_)");

let unwrap_pair =
  fun
  | Pair(c1, c2) => (c1, c2)
  | _ => failwith("input can only be pair(_, _)");

let rec or_constraints = (lst: list(t)): t =>
  switch (lst) {
  | [] => failwith("should have at least one constraint")
  | [xi] => xi
  | [xi, ...xis] => Or(xi, or_constraints(xis))
  };

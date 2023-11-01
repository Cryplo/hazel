open Util;
open EvaluatorResult;
open PatternMatch;

[@deriving sexp]
type ground_cases =
  | Hole
  | Ground
  | NotGroundOrHole(Typ.t) /* the argument is the corresponding ground type */;

let const_unknown: 'a => Typ.t = _ => Unknown(Internal);

let grounded_Arrow =
  NotGroundOrHole(Arrow(Unknown(Internal), Unknown(Internal)));
let grounded_Prod = length =>
  NotGroundOrHole(Prod(ListUtil.replicate(length, Typ.Unknown(Internal))));
let grounded_Sum = (sm: Typ.sum_map): ground_cases => {
  let sm' = sm |> ConstructorMap.map(Option.map(const_unknown));
  NotGroundOrHole(Sum(sm'));
};
let grounded_List = NotGroundOrHole(List(Unknown(Internal)));

let rec ground_cases_of = (ty: Typ.t): ground_cases => {
  let is_ground_arg: option(Typ.t) => bool =
    fun
    | None
    | Some(Typ.Unknown(_)) => true
    | Some(ty) => ground_cases_of(ty) == Ground;
  switch (ty) {
  | Unknown(_) => Hole
  | Bool
  | Int
  | Float
  | String
  | Var(_)
  | Rec(_)
  | Arrow(Unknown(_), Unknown(_))
  | List(Unknown(_)) => Ground
  | Prod(tys) =>
    if (List.for_all(
          fun
          | Typ.Unknown(_) => true
          | _ => false,
          tys,
        )) {
      Ground;
    } else {
      tys |> List.length |> grounded_Prod;
    }
  | Sum(sm) =>
    sm |> ConstructorMap.is_ground(is_ground_arg) ? Ground : grounded_Sum(sm)
  | Arrow(_, _) => grounded_Arrow
  | List(_) => grounded_List
  };
};

// TODO(Matt): PURGE THE ABOVE

type rule =
  // If anything going into it is indet, the result should be indet
  | Step({
      apply: unit => DHExp.t,
      final: bool,
    })
  | Constructor
  | Indet;

module type EV_MODE = {
  type state;
  type result;
  type requirement('a);
  type requirements('a, 'b);

  let req_value: (DHExp.t => result, int, DHExp.t) => requirement(DHExp.t);
  let req_all_value:
    (DHExp.t => result, int, list(DHExp.t)) => requirement(list(DHExp.t));
  let req_final: (DHExp.t => result, int, DHExp.t) => requirement(DHExp.t);
  let req_all_final:
    (DHExp.t => result, int, list(DHExp.t)) => requirement(list(DHExp.t));
  let do_not_req: (DHExp.t => result, int, DHExp.t) => requirement(DHExp.t);

  let (let.): (requirements('a, DHExp.t), 'a => rule) => result;
  let (and.):
    (requirements('a, 'c => 'b), requirement('c)) =>
    requirements(('a, 'c), 'b);
  let otherwise: 'a => requirements(unit, 'a);

  let update_test: (state, KeywordID.t, TestMap.instance_report) => unit;
  let extend_env:
    (state, ClosureEnvironment.t, Environment.t) => ClosureEnvironment.t;
};

module Transition = (EV: EV_MODE) => {
  open EV;
  open DHExp;
  let (let.match) = ((state, env, match_result), r) =>
    switch (match_result) {
    | IndetMatch
    | DoesNotMatch => Indet
    | Matches(env') => r(EV.extend_env(state, env, env'))
    };

  let transition = (req, state, env, d): 'a =>
    switch (d) {
    | BoundVar(x) =>
      let. _ = otherwise(BoundVar(x));
      let d =
        ClosureEnvironment.lookup(env, x)
        |> OptUtil.get(() => {
             print_endline("FreeInvalidVar:" ++ x);
             raise(EvaluatorError.Exception(FreeInvalidVar(x)));
           });
      Step({
        apply: () => d,
        final: false // TODO(Matt): Can we make this true?
      });
    | Sequence(d1, d2) =>
      let. _ = otherwise((d1, d2) => Sequence(d1, d2))
      and. _ = req_final(req(state, env), 0, d1)
      and. d2' = do_not_req(req(state, env), 1, d2);
      Step({apply: () => d2', final: false});
    | Let(dp, d1, d2) =>
      let. _ = otherwise((d1, d2) => Let(dp, d1, d2))
      and. d1' = req_final(req(state, env), 0, d1)
      and. d2' = do_not_req(req(state, env), 1, d2);
      let.match env' = (state, env, matches(dp, d1'));
      Step({apply: () => Closure(env', d2'), final: false});
    | Fun(_) =>
      let. _ = otherwise(d);
      Step({apply: () => Closure(env, d), final: true});
    | FixF(f, t, d1) =>
      let. _ = otherwise(FixF(f, t, d1));
      // TODO(Matt): Would it be safer to have a fourth argument to FixF?
      // TODO(Matt): is this a step?
      // TODO(Matt): is t needed here?
      Step({
        apply: () =>
          Closure(
            extend_env(state, env, Environment.singleton((f, d1))),
            d1,
          ),
        final: false,
      });
    | Test(id, d) =>
      let. _ = otherwise(d => Test(id, d))
      and. d' = req_final(req(state, env), 0, d);
      Step({
        apply: () =>
          switch (d') {
          | BoolLit(true) =>
            update_test(state, id, (d', Pass));
            d;
          | BoolLit(false) =>
            update_test(state, id, (d', Fail));
            d;
          /* Hack: assume if final and not Bool, then Indet; this won't catch errors in statics */
          | _ =>
            update_test(state, id, (d', Indet));
            d;
          },
        final: true,
      });
    | Ap(d1, d2) =>
      let. _ = otherwise((d1, d2) => Ap(d1, d2))
      and. d1' = req_value(req(state, env), 0, d1)
      and. d2' = req_final(req(state, env), 1, d2);
      switch (d1') {
      | Constructor(_) => Constructor // TODO(Matt): Two different "Constructor" constructors is confusing
      | Closure(env', Fun(dp, _, d3, _)) =>
        let.match env'' = (state, env', matches(dp, d2'));
        Step({apply: () => Closure(env'', d3), final: false});
      | Cast(d3', Arrow(ty1, ty2), Arrow(ty1', ty2')) =>
        Step({
          apply: () => Cast(Ap(d3', Cast(d2', ty1', ty1)), ty2, ty2'),
          final: false,
        })
      | _ =>
        Step({
          apply: () => {
            print_endline("InvalidBoxedFun");
            raise(EvaluatorError.Exception(InvalidBoxedFun(d1')));
          },
          final: true,
        }) // TODO(Matt): Check that when I implement Indet it won't indet a partially-run program
      };
    | ApBuiltin(ident, args) =>
      let. _ = otherwise(args => ApBuiltin(ident, args))
      and. _args' = req_all_final(req(state, env), 0, args);
      Step({
        apply: () => failwith("Builtins not implemented yet"), // TODO(Matt): Add builtins back.
        // VarMap.lookup(Builtins.forms_init, ident)
        // |> OptUtil.get(() => {
        //      print_endline("InvalidBuiltin");
        //      raise(EvaluatorError.Exception(InvalidBuiltin(ident)));
        //    }),
        final: false,
      });
    | BoolLit(_)
    | IntLit(_)
    | FloatLit(_)
    | StringLit(_)
    | Constructor(_) =>
      let. _ = otherwise(d);
      Constructor;
    | BinBoolOp(And, d1, d2) =>
      let. _ = otherwise((d1, d2) => BinBoolOp(And, d1, d2))
      and. d1' = req_value(req(state, env), 0, d1)
      and. d2' = do_not_req(req(state, env), 1, d2); // TODO(Matt): This might lead to some unexpected behaviour with not evaluating o | (5 == 3)
      Step({
        apply: () =>
          switch (d1') {
          | BoolLit(true) => d2'
          | BoolLit(false) => BoolLit(false)
          | _ => raise(EvaluatorError.Exception(InvalidBoxedBoolLit(d1'))) // TODO(Matt): Make it consistent whether we print
          },
        final: false,
      });
    | BinBoolOp(Or, d1, d2) =>
      let. _ = otherwise((d1, d2) => BinBoolOp(Or, d1, d2))
      and. d1' = req_value(req(state, env), 0, d1)
      and. d2' = do_not_req(req(state, env), 1, d2); // TODO(Matt): This might lead to some unexpected behaviour with not evaluating o | (5 == 3)
      Step({
        apply: () =>
          switch (d1') {
          | BoolLit(true) => BoolLit(true)
          | BoolLit(false) => d2'
          | _ => raise(EvaluatorError.Exception(InvalidBoxedBoolLit(d2')))
          },
        final: false,
      });
    | BinIntOp(op, d1, d2) =>
      let. _ = otherwise((d1, d2) => BinIntOp(op, d1, d2))
      and. d1' = req_value(req(state, env), 0, d1)
      and. d2' = req_value(req(state, env), 1, d2);
      Step({
        apply: () =>
          switch (d1', d2') {
          | (IntLit(n1), IntLit(n2)) =>
            switch (op) {
            | Plus => IntLit(n1 + n2)
            | Minus => IntLit(n1 - n2)
            | Power when n2 < 0 =>
              InvalidOperation(
                BinIntOp(op, IntLit(n1), IntLit(n2)),
                NegativeExponent,
              )
            | Power => IntLit(IntUtil.ipow(n1, n2))
            | Times => IntLit(n1 * n2)
            | Divide when n2 == 0 =>
              InvalidOperation(
                BinIntOp(op, IntLit(n1), IntLit(n2)),
                DivideByZero,
              )
            | Divide => IntLit(n1 / n2)
            | LessThan => BoolLit(n1 < n2)
            | LessThanOrEqual => BoolLit(n1 <= n2)
            | GreaterThan => BoolLit(n1 > n2)
            | GreaterThanOrEqual => BoolLit(n1 >= n2)
            | Equals => BoolLit(n1 == n2)
            | NotEquals => BoolLit(n1 != n2)
            }
          | (IntLit(_), _) =>
            raise(EvaluatorError.Exception(InvalidBoxedIntLit(d2')))
          | _ => raise(EvaluatorError.Exception(InvalidBoxedIntLit(d1')))
          },
        final: false // False so that InvalidOperations are caught and made indet
      });
    | BinFloatOp(op, d1, d2) =>
      let. _ = otherwise((d1, d2) => BinFloatOp(op, d1, d2))
      and. d1' = req_value(req(state, env), 0, d1)
      and. d2' = req_value(req(state, env), 1, d2);
      Step({
        apply: () =>
          switch (d1', d2') {
          | (FloatLit(n1), FloatLit(n2)) =>
            switch (op) {
            | Plus => FloatLit(n1 +. n1)
            | Minus => FloatLit(n1 -. n2)
            | Power => FloatLit(n1 ** n2)
            | Times => FloatLit(n1 *. n2)
            | Divide => FloatLit(n1 /. n2) // Do we like that this behavior is not consistent w/ integers?
            | LessThan => BoolLit(n1 < n2)
            | LessThanOrEqual => BoolLit(n1 <= n2)
            | GreaterThan => BoolLit(n1 > n2)
            | GreaterThanOrEqual => BoolLit(n1 >= n2)
            | Equals => BoolLit(n1 == n2)
            | NotEquals => BoolLit(n1 != n2)
            }
          | (FloatLit(_), _) =>
            raise(EvaluatorError.Exception(InvalidBoxedFloatLit(d2')))
          | _ => raise(EvaluatorError.Exception(InvalidBoxedFloatLit(d1')))
          },
        final: true,
      });
    | BinStringOp(op, d1, d2) =>
      let. _ = otherwise((d1, d2) => BinStringOp(op, d1, d2))
      and. d1' = req_value(req(state, env), 0, d1)
      and. d2' = req_value(req(state, env), 1, d2);
      Step({
        apply: () =>
          switch (d1', d2') {
          | (StringLit(s1), StringLit(s2)) =>
            switch (op) {
            | Concat => StringLit(s1 ++ s2)
            | Equals => BoolLit(s1 == s2)
            }
          | (StringLit(_), _) =>
            raise(EvaluatorError.Exception(InvalidBoxedStringLit(d2')))
          | _ => raise(EvaluatorError.Exception(InvalidBoxedStringLit(d1')))
          },
        final: true,
      });
    | Tuple(ds) =>
      let. _ = otherwise(ds => Tuple(ds))
      and. _ = req_all_value(req(state, env), 0, ds);
      Constructor;
    // TODO(Matt): As far as I can tell this case is only used for mutual recursion - could we cut it and replace w/ let?
    | Prj(d1, n) =>
      let. _ = otherwise(d1 => Prj(d1, n))
      and. d1' = req_final(req(state, env), 0, d1);
      Step({
        apply: () =>
          switch (d1') {
          | Tuple(ds) when n < 0 || List.length(ds) <= n =>
            InvalidOperation(d1', InvalidOperationError.InvalidProjection) // TODO(Matt): This is iconsistent - we don't deal with any other static error like this
          | Tuple(ds) => List.nth(ds, n)
          | Cast(_, Prod(ts), Prod(_)) when n < 0 || List.length(ts) <= n =>
            InvalidOperation(d1', InvalidOperationError.InvalidProjection) // TODO(Matt): This is iconsistent - we don't deal with any other static error like this
          | Cast(d2, Prod(ts1), Prod(ts2)) =>
            Cast(Prj(d2, n), List.nth(ts1, n), List.nth(ts2, n))
          | _ =>
            InvalidOperation(d1', InvalidOperationError.InvalidProjection) // TODO(Matt): This is iconsistent - we don't deal with any other static error like this
          },
        final: false,
      });
    // TODO(Matt): Can we do something cleverer when the list structure is complete but the contents aren't?
    | Cons(d1, d2) =>
      let. _ = otherwise((d1, d2) => Cons(d1, d2))
      and. d1' = req_final(req(state, env), 0, d1)
      and. d2' = req_value(req(state, env), 1, d2);
      Step({
        apply: () =>
          switch (d2') {
          | ListLit(u, i, ty, ds) => ListLit(u, i, ty, [d1', ...ds])
          | _ => raise(EvaluatorError.Exception(InvalidBoxedListLit(d2')))
          },
        final: true,
      });
    | ListConcat(d1, d2) =>
      // TODO(Matt): Can we do something cleverer when the list structure is complete but the contents aren't?
      let. _ = otherwise((d1, d2) => ListConcat(d1, d2))
      and. d1' = req_value(req(state, env), 0, d1)
      and. d2' = req_value(req(state, env), 1, d2);
      Step({
        apply: () =>
          switch (d1', d2') {
          | (ListLit(u1, i1, t1, ds1), ListLit(_, _, _, ds2)) =>
            ListLit(u1, i1, t1, ds1 @ ds2)
          | (ListLit(_), _) =>
            raise(EvaluatorError.Exception(InvalidBoxedListLit(d2')))
          | (_, _) =>
            raise(EvaluatorError.Exception(InvalidBoxedListLit(d1')))
          },
        final: true,
      });
    | ListLit(u, i, ty, ds) =>
      let. _ = otherwise(ds => ListLit(u, i, ty, ds))
      and. _ = req_all_final(req(state, env), 0, ds);
      Constructor;
    // TODO(Matt): This will currently re-traverse d1
    | ConsistentCase(Case(d1, rules, n)) =>
      let. _ = otherwise(d1 => ConsistentCase(Case(d1, rules, n)))
      and. d1' = req_final(req(state, env), 0, d1);
      switch (List.nth_opt(rules, n)) {
      | None => Indet // TODO: Why do we need a closure when everything is final?
      | Some(Rule(dp, d2)) =>
        switch (matches(dp, d1')) {
        | Matches(env') =>
          Step({
            apply: () => Closure(extend_env(state, env, env'), d2),
            final: false,
          })
        | DoesNotMatch =>
          Step({
            apply: () => ConsistentCase(Case(d1', rules, n + 1)),
            final: false,
          })
        | IndetMatch => Indet // TODO(Matt): Is Closure necessary?
        }
      };
    | InconsistentBranches(_) as d =>
      let. _ = otherwise(d);
      Indet; // TODO(Matt): Closure
    | Closure(env', d) =>
      let. _ = otherwise(d => Closure(env', d))
      and. d' = req_value(req(state, env'), 0, d);
      Step({apply: () => d', final: true});
    | NonEmptyHole(reason, u, i, d1) =>
      let. _ = otherwise(d1 => NonEmptyHole(reason, u, i, d1))
      and. _ = req_final(req(state, env), 0, d1);
      Indet;
    | EmptyHole(_)
    | FreeVar(_)
    | InvalidText(_)
    | InvalidOperation(_)
    | ExpandingKeyword(_) =>
      let. _ = otherwise(d);
      Indet;
    | Cast(d, t1, t2) =>
      /* Cast calculus */
      let. _ = otherwise(d => Cast(d, t1, t2))
      and. d' = req_final(req(state, env), 0, d);
      switch (ground_cases_of(t1), ground_cases_of(t2)) {
      | (Hole, Hole)
      | (Ground, Ground) =>
        /* if two types are ground and consistent, then they are eq */
        Step({apply: () => d', final: true})
      | (Ground, Hole) =>
        /* can't remove the cast or do anything else here, so we're done */
        Constructor
      | (Hole, Ground) =>
        switch (d') {
        | Cast(d2, t3, Unknown(_)) =>
          /* by canonical forms, d1' must be of the form d<ty'' -> ?> */
          if (Typ.eq(t3, t2)) {
            Step({apply: () => d2, final: true});
          } else {
            Step({apply: () => FailedCast(d', t1, t2), final: false});
          }
        | _ =>
          Step({
            apply: () =>
              raise(EvaluatorError.Exception(CastBVHoleGround(d'))),
            final: true,
          }) // TODO(Matt): I don't like the way this looks
        }
      | (Hole, NotGroundOrHole(t2_grounded)) =>
        /* ITExpand rule */
        Step({
          apply: () =>
            DHExp.Cast(Cast(d', t1, t2_grounded), t2_grounded, t2),
          final: false,
        })
      | (NotGroundOrHole(t1_grounded), Hole) =>
        /* ITGround rule */
        Step({
          apply: () =>
            DHExp.Cast(Cast(d', t1, t1_grounded), t1_grounded, t2),
          final: false,
        })
      | (Ground, NotGroundOrHole(_))
      | (NotGroundOrHole(_), Ground) =>
        /* can't do anything when casting between diseq, non-hole types */
        Constructor
      | (NotGroundOrHole(_), NotGroundOrHole(_)) =>
        /* they might be eq in this case, so remove cast if so */
        if (Typ.eq(t1, t2)) {
          Step({apply: () => d', final: true});
        } else {
          Constructor;
        }
      };
    | FailedCast(d1, t1, t2) =>
      let. _ = otherwise(d1 => FailedCast(d1, t1, t2))
      and. _ = req_final(req(state, env), 0, d1);
      Indet;
    };
};

module Evaluator: {
  type result_unfinished =
    | BoxedValue(DHExp.t)
    | Indet(DHExp.t)
    | Uneval(DHExp.t);
  include
    EV_MODE with
      type state = ref(EvaluatorState.t) and type result = result_unfinished;
} = {
  type reqstate =
    | BoxedReady
    | IndetReady
    | IndetBlocked;

  let (&&) = (x, y) =>
    switch (x, y) {
    | (IndetBlocked, _) => IndetBlocked
    | (_, IndetBlocked) => IndetBlocked
    | (IndetReady, _) => IndetReady
    | (_, IndetReady) => IndetReady
    | (BoxedReady, BoxedReady) => BoxedReady
    };

  type requirement('a) = (reqstate, 'a);

  type requirements('a, 'b) = (reqstate, 'a, 'b); // thing, satisfies, indet, otherwise

  type state = ref(EvaluatorState.t);
  let update_test = (state, id, v) =>
    state := EvaluatorState.add_test(state^, id, v);

  let extend_env = (state, env, env') => {
    let (env'', state') =
      EvaluatorState.with_eig(
        ClosureEnvironment.of_environment(
          Environment.union(env', ClosureEnvironment.map_of(env)),
        ),
        state^,
      );
    state := state';
    env'';
  };

  type result_unfinished =
    | BoxedValue(DHExp.t)
    | Indet(DHExp.t)
    | Uneval(DHExp.t);

  type result = result_unfinished;

  let req_value = (f, _, x) =>
    switch (f(x)) {
    | BoxedValue(x) => (BoxedReady, x)
    | Indet(x) => (IndetBlocked, x)
    | Uneval(_) => failwith("Unexpected Uneval")
    };

  let rec req_all_value = (f, i) =>
    fun
    | [] => (BoxedReady, [])
    | [x, ...xs] => {
        let (r1, x') = req_value(f, i, x);
        let (r2, xs') = req_all_value(f, i + 1, xs);
        (r1 && r2, [x', ...xs']);
      };

  let req_final = (f, _, x) =>
    switch (f(x)) {
    | BoxedValue(x) => (BoxedReady, x)
    | Indet(x) => (IndetReady, x)
    | Uneval(_) => failwith("Unexpected Uneval")
    };

  let rec req_all_final = (f, i) =>
    fun
    | [] => (BoxedReady, [])
    | [x, ...xs] => {
        let (r1, x') = req_final(f, i, x);
        let (r2, xs') = req_all_final(f, i + 1, xs);
        (r1 && r2, [x', ...xs']);
      };

  // TODO(Matt): does it make sense to say this isn't indet?
  let do_not_req = (_, _, x) => (IndetReady, x);

  let otherwise = c => (BoxedReady, (), c);

  let (and.) = ((r1, x1, c1), (r2, x2)) => (r1 && r2, (x1, x2), c1(x2));

  let (let.) = ((r, x, c), s) =>
    switch (r, s(x)) {
    | (BoxedReady, Step({apply, final: true})) => BoxedValue(apply())
    | (IndetReady, Step({apply, final: true})) => Indet(apply())
    | (BoxedReady, Step({apply, final: false}))
    | (IndetReady, Step({apply, final: false})) => Uneval(apply())
    | (BoxedReady, Constructor) => BoxedValue(c)
    | (IndetReady, Constructor) => Indet(c)
    | (IndetBlocked, _) => Indet(c)
    | (_, Indet) => Indet(c)
    };
};
module Eval = Transition(Evaluator);

let rec evaluate = (state, env, d) => {
  let u = Eval.transition(evaluate, state, env, d);
  switch (u) {
  | BoxedValue(x) => BoxedValue(x)
  | Indet(x) => Indet(x)
  | Uneval(x) => evaluate(state, env, x)
  };
};

let evaluate = (env, d): (EvaluatorState.t, EvaluatorResult.t) => {
  let state = EvaluatorState.init;
  let (env, state) =
    EvaluatorState.with_eig(ClosureEnvironment.of_environment(env), state);
  let state = ref(state);
  let result = evaluate(state, env, d);
  let result =
    switch (result) {
    | BoxedValue(x) => BoxedValue(x)
    | Indet(x) => Indet(x)
    | Uneval(x) => Indet(x)
    };
  (state^, result);
};

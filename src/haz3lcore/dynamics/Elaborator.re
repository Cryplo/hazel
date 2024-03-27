/*


 A nice property would be that elaboration is idempotent...
 */

exception MissingTypeInfo;

module Elaboration = {
  [@deriving (show({with_path: false}), sexp, yojson)]
  type t = {
    d: DHExp.t,
    info_map: Statics.Map.t,
  };
};

module ElaborationResult = {
  [@deriving sexp]
  type t =
    | Elaborates(DHExp.t, Typ.t, Delta.t)
    | DoesNotElaborate;
};

let fixed_exp_typ = (m: Statics.Map.t, e: UExp.t): option(Typ.t) =>
  switch (Id.Map.find_opt(UExp.rep_id(e), m)) {
  | Some(InfoExp({ty, _})) => Some(ty)
  | _ => None
  };

let fixed_pat_typ = (m: Statics.Map.t, p: UPat.t): option(Typ.t) =>
  switch (Id.Map.find_opt(UPat.rep_id(p), m)) {
  | Some(InfoPat({ty, _})) => Some(ty)
  | _ => None
  };

/* Adds casts if required.

   When adding a new construct, [TODO: write something helpful here] */
let cast = (ctx: Ctx.t, mode: Mode.t, self_ty: Typ.t, d: DHExp.t) =>
  switch (mode) {
  | Syn => d
  | SynFun =>
    switch (Typ.term_of(self_ty)) {
    | Unknown(_) =>
      DHExp.fresh_cast(d, self_ty, Arrow(self_ty, self_ty) |> Typ.fresh)
    | Arrow(_) => d
    | _ => failwith("Elaborator.wrap: SynFun non-arrow-type")
    }
  | Ana(ana_ty) =>
    let ana_ty = Typ.normalize(ctx, ana_ty);
    /* Forms with special ana rules get cast from their appropriate Matched types */
    switch (DHExp.term_of(d)) {
    | ListLit(_)
    | ListConcat(_)
    | Cons(_) =>
      switch (Typ.term_of(ana_ty)) {
      | Unknown(_) => DHExp.fresh_cast(d, List(ana_ty) |> Typ.fresh, ana_ty)
      | _ => d
      }
    | Fun(_) =>
      /* See regression tests in Documentation/Dynamics */
      let (_, ana_out) = Typ.matched_arrow(ctx, ana_ty);
      let (self_in, _) = Typ.matched_arrow(ctx, self_ty);
      DHExp.fresh_cast(d, Arrow(self_in, ana_out) |> Typ.fresh, ana_ty);
    | Tuple(ds) =>
      switch (Typ.term_of(ana_ty)) {
      | Unknown(prov) =>
        let us =
          List.init(List.length(ds), _ => Typ.Unknown(prov) |> Typ.fresh);
        DHExp.fresh_cast(d, Prod(us) |> Typ.fresh, ana_ty);
      | _ => d
      }
    | Constructor(_) =>
      switch (ana_ty |> Typ.term_of, self_ty |> Typ.term_of) {
      | (Unknown(_), Rec(_, {term: Sum(_), _}))
      | (Unknown(_), Sum(_)) => DHExp.fresh_cast(d, self_ty, ana_ty)
      | _ => d
      }
    | Ap(_, f, _) =>
      switch (DHExp.term_of(f)) {
      | Constructor(_) =>
        switch (ana_ty |> Typ.term_of, self_ty |> Typ.term_of) {
        | (Unknown(_), Rec(_, {term: Sum(_), _}))
        | (Unknown(_), Sum(_)) => DHExp.fresh_cast(d, self_ty, ana_ty)
        | _ => d
        }
      | StaticErrorHole(_, g) =>
        switch (DHExp.term_of(g)) {
        | Constructor(_) =>
          switch (ana_ty |> Typ.term_of, self_ty |> Typ.term_of) {
          | (Unknown(_), Rec(_, {term: Sum(_), _}))
          | (Unknown(_), Sum(_)) => DHExp.fresh_cast(d, self_ty, ana_ty)
          | _ => d
          }
        | _ => DHExp.fresh_cast(d, self_ty, ana_ty)
        }
      | _ => DHExp.fresh_cast(d, self_ty, ana_ty)
      }
    /* Forms with special ana rules but no particular typing requirements */
    | Match(_)
    | If(_)
    | Seq(_)
    | Let(_)
    | FixF(_) => d
    /* Hole-like forms: Don't cast */
    | Invalid(_)
    | EmptyHole
    | MultiHole(_)
    | StaticErrorHole(_) => d
    /* DHExp-specific forms: Don't cast */
    | Cast(_)
    | Closure(_)
    | Filter(_)
    | FailedCast(_)
    | DynamicErrorHole(_) => d
    /* Normal cases: wrap */
    | Var(_)
    | BuiltinFun(_)
    | Parens(_)
    | Bool(_)
    | Int(_)
    | Float(_)
    | String(_)
    | UnOp(_)
    | BinOp(_)
    | TyAlias(_)
    | Test(_) => DHExp.fresh_cast(d, self_ty, ana_ty)
    };
  };

/* Handles cast insertion and non-empty-hole wrapping
   for elaborated expressions */
let wrap = (m, exp: Exp.t): DHExp.t => {
  let (mode, self, ctx) =
    switch (Id.Map.find_opt(Exp.rep_id(exp), m)) {
    | Some(Info.InfoExp({mode, self, ctx, _})) => (mode, self, ctx)
    | _ => raise(MissingTypeInfo)
    };
  switch (Info.status_exp(ctx, mode, self)) {
  | NotInHole(_) =>
    let self_ty =
      switch (Self.typ_of_exp(ctx, self)) {
      | Some(self_ty) => Typ.normalize(ctx, self_ty)
      | None => Unknown(Internal) |> Typ.fresh
      };
    cast(ctx, mode, self_ty, exp);
  | InHole(
      FreeVariable(_) | Common(NoType(_)) |
      Common(Inconsistent(Internal(_))),
    ) => exp
  | InHole(Common(Inconsistent(Expectation(_) | WithArrow(_)))) =>
    DHExp.fresh(StaticErrorHole(Exp.rep_id(exp), exp))
  };
};

/*
  This function converts user-expressions (UExp.t) to dynamic expressions (DHExp.t). They
  have the same datatype but there are some small differences so that UExp.t can be edited
  and DHExp.t can be evaluated.

 Currently, Elaboration does the following things:

   - Insert casts
   - Insert non-empty hole wrappers
   - Annotate functions with names
   - Insert implicit fixpoints
   - Remove parentheses [not strictly necessary]
   - Remove TyAlias [not strictly necessary]

 When adding a new construct you can probably just add it to the default cases.
  */
let rec dexp_of_uexp = (m, uexp, ~in_filter) => {
  Exp.map_term(
    ~f_exp=
      (continue, exp) => {
        let (term, rewrap) = Exp.unwrap(exp);
        switch (term) {
        // Default cases: do not need to change at elaboration
        | Closure(_)
        | Cast(_)
        | Invalid(_)
        | EmptyHole
        | MultiHole(_)
        | StaticErrorHole(_)
        | DynamicErrorHole(_)
        | FailedCast(_)
        | Bool(_)
        | Int(_)
        | Float(_)
        | String(_)
        | ListLit(_)
        | Tuple(_)
        | Cons(_)
        | ListConcat(_)
        | UnOp(Int(_) | Bool(_), _)
        | BinOp(_)
        | BuiltinFun(_)
        | Seq(_)
        | Test(_)
        | Filter(Residue(_), _)
        | Var(_)
        | Constructor(_)
        | Ap(_)
        | If(_)
        | Fun(_)
        | FixF(_)
        | Match(_) => continue(exp) |> wrap(m)

        // Unquote operator: should be turned into constructor if inside filter body.
        | UnOp(Meta(Unquote), e) =>
          switch (e.term) {
          | Var("e") when in_filter =>
            Constructor("$e") |> DHExp.fresh |> wrap(m)
          | Var("v") when in_filter =>
            Constructor("$v") |> DHExp.fresh |> wrap(m)
          | _ => DHExp.EmptyHole |> DHExp.fresh |> wrap(m)
          }
        | Filter(Filter({act, pat}), body) =>
          Filter(
            Filter({act, pat: dexp_of_uexp(m, pat, ~in_filter=true)}),
            dexp_of_uexp(m, body, ~in_filter),
          )
          |> rewrap
          |> wrap(m)

        // Let bindings: insert implicit fixpoints and label functions with their names.
        | Let(p, def, body) =>
          let add_name: (option(string), DHExp.t) => DHExp.t = (
            (name, d) => {
              let (term, rewrap) = DHExp.unwrap(d);
              switch (term) {
              | Fun(p, e, ctx, _) => DHExp.Fun(p, e, ctx, name) |> rewrap
              | _ => d
              };
            }
          );
          let ddef = dexp_of_uexp(m, def, ~in_filter);
          let dbody = dexp_of_uexp(m, body, ~in_filter);
          switch (UPat.get_recursive_bindings(p)) {
          | None =>
            /* not recursive */
            DHExp.Let(p, add_name(UPat.get_var(p), ddef), dbody)
            |> rewrap
            |> wrap(m)
          | Some(b) =>
            DHExp.Let(
              p,
              FixF(p, add_name(Some(String.concat(",", b)), ddef), None)
              |> DHExp.fresh,
              dbody,
            )
            |> rewrap
            |> wrap(m)
          };

        // type alias and parentheses: remove during elaboration
        | TyAlias(_, _, e)
        | Parens(e) => dexp_of_uexp(m, e, ~in_filter)
        };
      },
    uexp,
  );
};

//let dhexp_of_uexp = Core.Memo.general(~cache_size_bound=1000, dhexp_of_uexp);

let uexp_elab = (m: Statics.Map.t, uexp: UExp.t): ElaborationResult.t =>
  switch (dexp_of_uexp(m, uexp, ~in_filter=false)) {
  | exception MissingTypeInfo => DoesNotElaborate
  | d =>
    //let d = uexp_elab_wrap_builtins(d);
    let ty =
      switch (fixed_exp_typ(m, uexp)) {
      | Some(ty) => ty
      | None => Typ.Unknown(Internal) |> Typ.fresh
      };
    Elaborates(d, ty, Delta.empty);
  };

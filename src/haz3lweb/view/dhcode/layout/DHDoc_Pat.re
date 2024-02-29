open Pretty;
open Haz3lcore;

let precedence = (dp: TermBase.UPat.t) =>
  switch (DHPat.term_of(dp)) {
  | EmptyHole
  | MultiHole(_)
  | Wild
  | Invalid(_)
  | Var(_)
  | Int(_)
  | Float(_)
  | Bool(_)
  | String(_)
  | ListLit(_)
  | Constructor(_) => DHDoc_common.precedence_const
  | Tuple(_) => DHDoc_common.precedence_Comma
  | Cons(_) => DHDoc_common.precedence_Cons
  | Ap(_) => DHDoc_common.precedence_Ap
  | Parens(_) => DHDoc_common.precedence_const
  | TypeAnn(_) => DHDoc_common.precedence_Times
  };

let rec mk =
        (
          ~infomap: Statics.Map.t,
          ~parenthesize=false,
          ~enforce_inline: bool,
          dp: TermBase.UPat.t,
        )
        : DHDoc.t => {
  let mk' = mk(~enforce_inline, ~infomap);
  let mk_left_associative_operands = (precedence_op, dp1, dp2) => (
    mk'(~parenthesize=precedence(dp1) > precedence_op, dp1),
    mk'(~parenthesize=precedence(dp2) >= precedence_op, dp2),
  );
  let mk_right_associative_operands = (precedence_op, dp1, dp2) => (
    mk'(~parenthesize=precedence(dp1) >= precedence_op, dp1),
    mk'(~parenthesize=precedence(dp2) > precedence_op, dp2),
  );
  let doc =
    switch (DHPat.term_of(dp)) {
    | MultiHole(_)
    | EmptyHole => DHDoc_common.mk_EmptyHole(ClosureEnvironment.empty)
    | Invalid(t) => DHDoc_common.mk_InvalidText(t)
    | Var(x) => Doc.text(x)
    | Wild => DHDoc_common.Delim.wild
    | Constructor(name) => DHDoc_common.mk_ConstructorLit(name)
    | Int(n) => DHDoc_common.mk_IntLit(n)
    | Float(f) => DHDoc_common.mk_FloatLit(f)
    | Bool(b) => DHDoc_common.mk_BoolLit(b)
    | String(s) => DHDoc_common.mk_StringLit(s)
    | ListLit(d_list) =>
      let ol = List.map(mk', d_list);
      DHDoc_common.mk_ListLit(ol);
    | Cons(dp1, dp2) =>
      let (doc1, doc2) =
        mk_right_associative_operands(DHDoc_common.precedence_Cons, dp1, dp2);
      DHDoc_common.mk_Cons(doc1, doc2);
    | Tuple([]) => DHDoc_common.Delim.triv
    | Tuple(ds) => DHDoc_common.mk_Tuple(List.map(mk', ds))
    // TODO: Print type annotations
    | TypeAnn(dp, _)
    | Parens(dp) => mk(~enforce_inline, ~parenthesize=true, ~infomap, dp)
    | Ap(dp1, dp2) =>
      let (doc1, doc2) =
        mk_left_associative_operands(DHDoc_common.precedence_Ap, dp1, dp2);
      DHDoc_common.mk_Ap(doc1, doc2);
    };
  let doc =
    switch (Statics.get_pat_error_at(infomap, DHPat.rep_id(dp))) {
    | Some(_) => Doc.annot(DHAnnot.NonEmptyHole, doc)
    | None => doc
    };
  parenthesize
    ? Doc.hcats([
        DHDoc_common.Delim.open_Parenthesized,
        doc,
        DHDoc_common.Delim.close_Parenthesized,
      ])
    : doc;
};

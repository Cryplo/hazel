open Sexplib.Std;

[@deriving sexp]
type operator = Operators.Exp.t;

// TODO
// type t =
// /* laid out vertically */
// | V(block)
// /* laid out horizontally */
// | H(opseq)
[@deriving sexp]
type t = block
// TODO
// block = (bool /* user newline */, list(line))
and block = list(line)
and line =
  | EmptyLine
  | LetLine(UHPat.t, option(UHTyp.t), t)
  | ExpLine(opseq)
and opseq = OpSeq.t(operand, operator)
and operand =
  | EmptyHole(MetaVar.t)
  | Var(ErrStatus.t, VarErrStatus.t, Var.t)
  | IntLit(ErrStatus.t, string)
  | FloatLit(ErrStatus.t, string)
  | BoolLit(ErrStatus.t, bool)
  | ListNil(ErrStatus.t)
  | Lam(ErrStatus.t, UHPat.t, option(UHTyp.t), t)
  | Inj(ErrStatus.t, InjSide.t, t)
  | Case(CaseErrStatus.t, t, rules)
  | Parenthesized(t)
  | ApPalette(ErrStatus.t, PaletteName.t, SerializedModel.t, splice_info)
and rules = list(rule)
and rule =
  | Rule(UHPat.t, t)
and splice_info = SpliceInfo.t(t)
and splice_map = SpliceInfo.splice_map(t);

[@deriving sexp]
type skel = OpSeq.skel(operator);
[@deriving sexp]
type seq = OpSeq.seq(operand, operator);

type affix = Seq.affix(operand, operator);

let letline = (p: UHPat.t, ~ann: option(UHTyp.t)=?, def: t): line =>
  LetLine(p, ann, def);

let var =
    (
      ~err: ErrStatus.t=NotInHole,
      ~var_err: VarErrStatus.t=NotInVarHole,
      x: Var.t,
    )
    : operand =>
  Var(err, var_err, x);

let intlit = (~err: ErrStatus.t=NotInHole, n: string): operand =>
  IntLit(err, n);

let floatlit = (~err: ErrStatus.t=NotInHole, f: string): operand =>
  FloatLit(err, f);

let boollit = (~err: ErrStatus.t=NotInHole, b: bool): operand =>
  BoolLit(err, b);

let lam =
    (
      ~err: ErrStatus.t=NotInHole,
      p: UHPat.t,
      ~ann: option(UHTyp.t)=?,
      body: t,
    )
    : operand =>
  Lam(err, p, ann, body);

let case =
    (
      ~err: CaseErrStatus.t=StandardErrStatus(NotInHole),
      scrut: t,
      rules: rules,
    )
    : operand =>
  Case(err, scrut, rules);

let listnil = (~err: ErrStatus.t=NotInHole, ()): operand => ListNil(err);

module Line = {
  let prune_empty_hole = (line: line): line =>
    switch (line) {
    | ExpLine(OpSeq(_, S(EmptyHole(_), E))) => EmptyLine
    | ExpLine(_)
    | EmptyLine
    | LetLine(_) => line
    };

  let get_opseq =
    fun
    | EmptyLine
    | LetLine(_, _, _) => None
    | ExpLine(opseq) => Some(opseq);
  let force_get_opseq = line =>
    line
    |> get_opseq
    |> OptUtil.get(_ => failwith("force_get_opseq: expected ExpLine"));
};

module Lines = {
  let prune_empty_holes = (lines: list(line)): list(line) =>
    lines |> List.map(Line.prune_empty_hole);
};

module Block = {
  let wrap' = (opseq: opseq): block => [ExpLine(opseq)];
  let wrap = (operand: operand): block => wrap'(OpSeq.wrap(operand));

  let num_lines: block => int = List.length;

  let prune_empty_hole_lines = (block: block): block =>
    switch (block |> ListUtil.split_last) {
    | None => block
    | Some((leading, last)) => (leading |> Lines.prune_empty_holes) @ [last]
    };

  let split_conclusion = (block: block): option((list(line), opseq)) =>
    switch (block |> ListUtil.split_last) {
    | None => None
    | Some((leading, last)) =>
      switch (last |> Line.get_opseq) {
      | None => None
      | Some(opseq) => Some((leading, opseq))
      }
    };
  let force_split_conclusion = (block: block): (list(line), opseq) =>
    switch (block |> split_conclusion) {
    | None => failwith("force_split_conclusion: unconcluded block")
    | Some((leading, conclusion)) => (leading, conclusion)
    };

  let join_conclusion = (leading: list(line), conclusion: opseq): block =>
    leading @ [ExpLine(conclusion)];
};

let rec get_tuple_elements: skel => list(skel) =
  fun
  | BinOp(_, Comma, skel1, skel2) =>
    get_tuple_elements(skel1) @ get_tuple_elements(skel2)
  | skel => [skel];

let rec make_tuple =
        (~err: ErrStatus.t=NotInHole, elements: list(skel)): skel =>
  switch (elements) {
  | [] => failwith("make_tuple: expected at least 1 element")
  | [skel] => skel
  | [skel, ...skels] => BinOp(err, Comma, skel, make_tuple(skels))
  };

/* helper function for constructing a new empty hole */
let new_EmptyHole = (u_gen: MetaVarGen.t): (operand, MetaVarGen.t) => {
  let (u, u_gen) = u_gen |> MetaVarGen.next;
  (EmptyHole(u), u_gen);
};

let is_EmptyHole =
  fun
  | EmptyHole(_) => true
  | _ => false;

let empty_rule = (u_gen: MetaVarGen.t): (rule, MetaVarGen.t) => {
  let (p, u_gen) = UHPat.new_EmptyHole(u_gen);
  let (e, u_gen) = new_EmptyHole(u_gen);
  let rule = Rule(OpSeq.wrap(p), Block.wrap(e));
  (rule, u_gen);
};

let rec get_err_status = (e: t): ErrStatus.t => get_err_status_block(e)
and get_err_status_block = block => {
  let (_, conclusion) = block |> Block.force_split_conclusion;
  conclusion |> get_err_status_opseq;
}
and get_err_status_opseq = opseq =>
  OpSeq.get_err_status(~get_err_status_operand, opseq)
and get_err_status_operand =
  fun
  | EmptyHole(_) => NotInHole
  | Var(err, _, _)
  | IntLit(err, _)
  | FloatLit(err, _)
  | BoolLit(err, _)
  | ListNil(err)
  | Lam(err, _, _, _)
  | Inj(err, _, _)
  | Case(StandardErrStatus(err), _, _)
  | ApPalette(err, _, _, _) => err
  | Case(InconsistentBranches(_), _, _) => NotInHole /* TODO: What to do here...? */
  | Parenthesized(e) => get_err_status(e);

/* put e in the specified hole */
let rec set_err_status = (err: ErrStatus.t, e: t): t =>
  e |> set_err_status_block(err)
and set_err_status_block = (err: ErrStatus.t, block: block): block => {
  let (leading, conclusion) = block |> Block.force_split_conclusion;
  Block.join_conclusion(leading, conclusion |> set_err_status_opseq(err));
}
and set_err_status_opseq = (err, opseq) =>
  OpSeq.set_err_status(~set_err_status_operand, err, opseq)
and set_err_status_operand = (err, operand) =>
  switch (operand) {
  | EmptyHole(_) => operand
  | Var(_, var_err, x) => Var(err, var_err, x)
  | IntLit(_, n) => IntLit(err, n)
  | FloatLit(_, f) => FloatLit(err, f)
  | BoolLit(_, b) => BoolLit(err, b)
  | ListNil(_) => ListNil(err)
  | Lam(_, p, ann, def) => Lam(err, p, ann, def)
  | Inj(_, inj_side, body) => Inj(err, inj_side, body)
  | Case(_, scrut, rules) => Case(StandardErrStatus(err), scrut, rules)
  | ApPalette(_, name, model, si) => ApPalette(err, name, model, si)
  | Parenthesized(body) => Parenthesized(body |> set_err_status(err))
  };

let is_inconsistent = operand =>
  switch (operand |> get_err_status_operand) {
  | InHole(TypeInconsistent, _) => true
  | _ => false
  };

/* put e in a new hole, if it is not already in a hole */
let rec make_inconsistent = (u_gen: MetaVarGen.t, e: t): (t, MetaVarGen.t) =>
  make_inconsistent_block(u_gen, e)
and make_inconsistent_block =
    (u_gen: MetaVarGen.t, block: block): (block, MetaVarGen.t) => {
  let (leading, conclusion) = block |> Block.force_split_conclusion;
  let (conclusion, u_gen) = conclusion |> make_inconsistent_opseq(u_gen);
  (Block.join_conclusion(leading, conclusion), u_gen);
}
and make_inconsistent_opseq = (u_gen, opseq) =>
  OpSeq.make_inconsistent(~make_inconsistent_operand, u_gen, opseq)
and make_inconsistent_operand = (u_gen, operand) =>
  switch (operand) {
  /* already in hole */
  | EmptyHole(_)
  | Var(InHole(TypeInconsistent, _), _, _)
  | IntLit(InHole(TypeInconsistent, _), _)
  | FloatLit(InHole(TypeInconsistent, _), _)
  | BoolLit(InHole(TypeInconsistent, _), _)
  | ListNil(InHole(TypeInconsistent, _))
  | Lam(InHole(TypeInconsistent, _), _, _, _)
  | Inj(InHole(TypeInconsistent, _), _, _)
  | Case(StandardErrStatus(InHole(TypeInconsistent, _)), _, _)
  | ApPalette(InHole(TypeInconsistent, _), _, _, _) => (operand, u_gen)
  /* not in hole */
  | Var(NotInHole | InHole(WrongLength, _), _, _)
  | IntLit(NotInHole | InHole(WrongLength, _), _)
  | FloatLit(NotInHole | InHole(WrongLength, _), _)
  | BoolLit(NotInHole | InHole(WrongLength, _), _)
  | ListNil(NotInHole | InHole(WrongLength, _))
  | Lam(NotInHole | InHole(WrongLength, _), _, _, _)
  | Inj(NotInHole | InHole(WrongLength, _), _, _)
  | Case(
      StandardErrStatus(NotInHole | InHole(WrongLength, _)) |
      InconsistentBranches(_, _),
      _,
      _,
    )
  | ApPalette(NotInHole | InHole(WrongLength, _), _, _, _) =>
    let (u, u_gen) = u_gen |> MetaVarGen.next;
    let operand =
      operand |> set_err_status_operand(InHole(TypeInconsistent, u));
    (operand, u_gen);
  /* err in constructor args */
  | Parenthesized(body) =>
    let (body, u_gen) = body |> make_inconsistent(u_gen);
    (Parenthesized(body), u_gen);
  };

let rec drop_outer_parentheses = (operand): t =>
  switch (operand) {
  | Parenthesized([ExpLine(OpSeq(_, S(operand, E)))]) =>
    drop_outer_parentheses(operand)
  | Parenthesized(e) => e
  | _ => Block.wrap(operand)
  };

let text_operand =
    (u_gen: MetaVarGen.t, shape: TextShape.t): (operand, MetaVarGen.t) =>
  switch (shape) {
  | Underscore => (var("_"), u_gen)
  | IntLit(n) => (intlit(n), u_gen)
  | FloatLit(f) => (floatlit(f), u_gen)
  | BoolLit(b) => (boollit(b), u_gen)
  | Var(x) => (var(x), u_gen)
  | ExpandingKeyword(kw) =>
    let (u, u_gen) = u_gen |> MetaVarGen.next;
    (
      var(~var_err=InVarHole(Free, u), kw |> ExpandingKeyword.to_string),
      u_gen,
    );
  };

let associate = (seq: seq) => {
  let skel_str = Skel.make_skel_str(seq, Operators.Exp.to_parse_string);
  let lexbuf = Lexing.from_string(skel_str);
  SkelExprParser.skel_expr(SkelExprLexer.read, lexbuf);
};

let mk_OpSeq = OpSeq.mk(~associate);

let rec is_complete_line = (l: line, check_type_holes: bool): bool => {
  switch (l) {
  | EmptyLine => true
  | LetLine(pat, option_ty, body) =>
    if (check_type_holes) {
      switch (option_ty) {
      | None => UHPat.is_complete(pat) && is_complete(body, check_type_holes)
      | Some(ty) =>
        UHPat.is_complete(pat)
        && is_complete(body, check_type_holes)
        && UHTyp.is_complete(ty)
      };
    } else {
      UHPat.is_complete(pat) && is_complete(body, check_type_holes);
    }
  | ExpLine(body) =>
    OpSeq.is_complete(is_complete_operand, body, check_type_holes)
  };
}
and is_complete_block = (b: block, check_type_holes: bool): bool => {
  b |> List.for_all(l => is_complete_line(l, check_type_holes));
}
and is_complete_operand = (operand: 'operand, check_type_holes: bool): bool => {
  switch (operand) {
  | EmptyHole(_) => false
  | Var(InHole(_), _, _) => false
  | Var(NotInHole, InVarHole(_), _) => false
  | Var(NotInHole, NotInVarHole, _) => true
  | IntLit(InHole(_), _) => false
  | IntLit(NotInHole, _) => true
  | FloatLit(InHole(_), _) => false
  | FloatLit(NotInHole, _) => true
  | BoolLit(InHole(_), _) => false
  | BoolLit(NotInHole, _) => true
  | ListNil(InHole(_)) => false
  | ListNil(NotInHole) => true
  | Lam(InHole(_), _, _, _) => false
  | Lam(NotInHole, pat, option_ty, body) =>
    if (check_type_holes) {
      switch (option_ty) {
      | None => UHPat.is_complete(pat) && is_complete(body, check_type_holes)
      | Some(ty) =>
        UHPat.is_complete(pat)
        && is_complete(body, check_type_holes)
        && UHTyp.is_complete(ty)
      };
    } else {
      UHPat.is_complete(pat) && is_complete(body, check_type_holes);
    }
  | Inj(InHole(_), _, _) => false
  | Inj(NotInHole, _, body) => is_complete(body, check_type_holes)
  | Case(StandardErrStatus(InHole(_)) | InconsistentBranches(_), _, _) =>
    false
  | Case(StandardErrStatus(NotInHole), body, _) =>
    is_complete(body, check_type_holes)
  | Parenthesized(body) => is_complete(body, check_type_holes)
  | ApPalette(InHole(_), _, _, _) => false
  | ApPalette(NotInHole, _, _, _) => failwith("unimplemented")
  };
}
and is_complete = (exp: t, check_type_holes: bool): bool => {
  is_complete_block(exp, check_type_holes);
};
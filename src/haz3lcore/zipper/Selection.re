open Util;
open Sexplib.Std;

[@deriving (show({with_path: false}), sexp, yojson)]
type t = {
  focus: Direction.t,
  content: Segment.t,
  ephemeral: bool,
};

/* NOTE: backpack no longer uses selection focus */
let mk = (~ephemeral=false, ~focus=Direction.Left, content) => {
  focus,
  content,
  ephemeral,
};

let empty = mk(Segment.empty);

let map = (f, sel) => {...sel, content: f(sel.content)};

let toggle_focus = selection => {
  ...selection,
  focus: Util.Direction.toggle(selection.focus),
};

// let is_balanced = sel => Segment.is_balanced(sel.content);

let is_empty = (selection: t) => selection.content == Segment.empty;

let clear = (selection: t) => {
  ...selection,
  ephemeral: false,
  content: Segment.empty,
};

let push = (p: Piece.t, {focus, content, ephemeral}: t): t => {
  let content =
    Segment.reassemble(
      switch (focus) {
      | Left => Segment.cons(p, content)
      | Right => Segment.snoc(content, p)
      },
    );
  {focus, content, ephemeral};
};

let pop = (sel: t): option((Piece.t, t)) =>
  switch (sel.focus, sel.content, ListUtil.split_last_opt(sel.content)) {
  | (_, [], _)
  | (_, _, None) => None
  | (Left, [p, ...content], _) =>
    let (p, rest) = Piece.pop_l(p);
    Some((p, {...sel, content: rest @ content}));
  | (Right, _, Some((content, p))) =>
    let (rest, p) = Piece.pop_r(p);
    Some((p, {...sel, content: content @ rest}));
  };

let split_piece = _: option((Piece.t, t)) => failwith("todo split_piece");

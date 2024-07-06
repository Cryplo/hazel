open Haz3lcore;
open Virtual_dom.Vdom;
open Node;

let mk =
    (_syntax: Piece.t, _model: ZipperBase.fold, ~inject)
    : ProjectorViewModule.t =>
  (module
   {
     [@deriving (show({with_path: false}), sexp, yojson)]
     type action = ZipperBase.fold_action;

     let view = _ =>
       div(
         ~attr=Attr.on_double_click(_ => inject(ProjectorsUpdate.Remove)),
         [text("⋱")],
       );
     let keymap = (_, _): option(ProjectorsUpdate.t) => None;
   });

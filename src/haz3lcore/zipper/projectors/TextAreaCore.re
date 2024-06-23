open ZipperBase;

let of_mono = (syntax: Piece.t): option(string) =>
  switch (syntax) {
  | Tile({label: [l], _}) => Some(l)
  | _ => None
  };

let mk_mono = (sort: Sort.t, string: string): Piece.t =>
  string |> Form.mk_atomic(sort) |> Piece.mk_tile(_, []);

let state_of = (piece: Piece.t): option(string) => piece |> of_mono;

let get = (piece: Piece.t): string =>
  switch (piece |> of_mono) {
  | None => failwith("TextArea: not string literal")
  | Some(s) => s
  };

let put = (s: string): Piece.t => s |> mk_mono(Exp);

let mk = (model): projector_core =>
  (module
   {
     [@deriving (show({with_path: false}), sexp, yojson)]
     type model = ZipperBase.textarea;
     [@deriving (show({with_path: false}), sexp, yojson)]
     type action = ZipperBase.textarea_action;
     let model = model;
     let projector = TextArea(model);
     let can_project = _ => true;
     let placeholder = () => Block(4);
     let auto_update = _: projector => TextArea(model);
     let update = _ => TextArea(model);
   });

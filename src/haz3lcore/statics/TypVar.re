open Sexplib.Std;

[@deriving (show({with_path: false}), sexp, yojson)]
type t = string;

let eq = String.equal;
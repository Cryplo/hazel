open Sexplib.Std;

[@deriving (show({with_path: false}), sexp)]
type t = string;

let eq = String.equal;

let length = String.length;

let valid_regex = Str.regexp("^[_a-z][_a-zA-Z0-9']*$"); // TODO: why was this Js_of_ocaml?
let is_valid = s => Str.string_match(valid_regex, s, 0);

/* helper function for guarding options with is_valid */
let check_valid = (s, result) =>
  if (is_valid(s)) {
    result;
  } else {
    None;
  };

let is_true = s => eq(s, "true");

let is_false = s => eq(s, "false");

let is_let = s => eq(s, "let");

let is_case = s => eq(s, "case");

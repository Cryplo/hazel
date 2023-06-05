open Haz3lcore;

module StringMap = Map.Make(String);

type params = {
  var_mention: list(string),
  recursive: list(string),
};

type fmap = StringMap.t(list(Term.UExp.t));
type vmap = StringMap.t(bool);

let add_flist = (l: list(string), m: fmap): fmap => {
	List.fold_left((m, name) => {StringMap.add(name, [], m)}, m, l);
};

/*let fmap_union = (m1: fmap, m2: fmap): fmap {
	StringMap.union((name: string, l1: list(Term.UExp.t), l2: list(Term.UExp.t)) => {Some(l1 @ l2)}, m1, m2);
};*/

let rec find_funcs(p: Term.UPat.t, def: Term.UExp.t, m: fmap): fmap => {
	switch((p, def)) {
	| (Parens(up), _) => find_func(up, def, m);
	| (TypeAnn(up, _), _) => find_func(up, def, m);
	| (Var(x), Fun(_)) => 
		switch(StringMap.find_opt(x)) {
		| None => m;
		| Some(l) => StringMap.add(x, [def, ...l], m);
		};
	| (Tuple(pl), Tuple(ul)) => 
		if(List.length(pl) != List.length(ul)) {
			None;
		} else {
			List.fold_left2((acc, upat, uexp) => {find_funcs(upat, uexp, acc)} , m, pl, ul);
		};
	| _ => m;
	};
};

let rec mk_fmap(uexp: Term.UExp.t, m: fmap): fmap => {
	switch(uexp.term) {
	| Let(p, def, body) => find_funcs(p, def, m) |> mk_fmap(body);	
	| 
	};
};

let rec find_var = (uexp: Term.UExp.t, name: string): bool => {
  switch (uexp.term) {
  | Var(x) => x == name
  | Fun(_, body) => find_var(body, name)
  | Tuple(l) =>
    List.fold_left((acc, es) => {acc || find_var(es, name)}, false, l)
  | Let(_, def, body) => find_var(def, name) || find_var(body, name)
  | Ap(u1, u2) => find_var(u1, name) || find_var(u2, name)
  | If(u1, u2, u3) =>
    find_var(u1, name) || find_var(u2, name) || find_var(u3, name)
  | Seq(u1, u2) => find_var(u1, name) || find_var(u2, name)
  | Test(u) => find_var(u, name)
  | Parens(u) => find_var(u, name)
  | Cons(u1, u2) => find_var(u1, name) || find_var(u2, name)
  | UnOp(_, u) => find_var(u, name)
  | BinOp(_, u1, u2) => find_var(u1, name) || find_var(u2, name)
  | Match(g, l) =>
    find_var(g, name)
    || List.fold_left(
         (acc, pe) => {
           let (_, u) = pe;
           acc || find_var(u, name);
         },
         false,
         l,
       )
  | _ => false
  };
};

let check = (uexp: Term.UExp.t, p: params): bool => {
	let m = StringMap.empty |> add_flist(p.recursive);
  List.fold_left(
    (acc, name) => {acc || find_var(uexp, name)},
    false,
    p.var_mention,
  );
};

open Virtual_dom.Vdom;
open Node;

let exercise: SchoolExercise.spec = {
  title: "Oddly Recursive",
  point_distribution: {
    test_validation: 10,
    mutation_testing: 40,
    impl_grading: 50,
  },
  prompt:
    div([
      text(
        "Write tests cases for, and then implement, a function odd that recursively determines whether a given integer is odd.",
      ),
    ]),
  prelude: "let not : Bool -> Bool =
fun x ->
if x then false else true in ",
  correct_impl: "let odd:Int->Bool =
fun x ->
if x < 0
then odd(-x)
else if x == 0 then false
else not(odd(x-1)) in ",
  your_tests: {
    minimum: 2,
    num_required: 6,
    tests: "test not(false) end;
test not(not(true)) end; ",
  },
  your_impl: "let odd: Int -> Bool =
fun n -> in",
  hidden_bugs: [
    {
      impl: "let odd: Int -> Bool =
fun x -> false
in ",
      hint: "always returns false",
    },
    {
      impl: "let odd: Int -> Bool =
fun x -> true
in",
      hint: "always returns true",
    },
    {
      impl: "let odd: Int -> Bool =
fun x -> if x < 0 then odd(-x)
else if x == 0 then true
else if x == 1 then true
else odd(x - 1) in
",
      hint: "incorrect base case",
    },
  ],
  hidden_tests: {
    tests: "test not(odd(0)) end;
test odd(1) end;
test not(odd(2)) end;
test odd(42) end",
    hints: ["zero"],
  },
  next_id: 0,
};
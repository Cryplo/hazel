open Virtual_dom.Vdom;
open Node;
open Util.Web;

let context_entry_view = ((name: string, {typ, _}: Core.Ctx.entry)): Node.t =>
  div(
    [clss(["context-entry"])],
    [text(name), text(":"), Type.view(typ)],
  );

let ctxc = "context-entries";

let exp_ctx_view = (ctx: Core.Ctx.t): Node.t =>
  div([clss([ctxc, "exp"])], List.map(context_entry_view, ctx));

let ctx_sorts_view = (ci: Core.Statics.t): Node.t => {
  switch (ci) {
  | Invalid => div([clss([ctxc, "invalid"])], [text("? No Info")])
  | InfoExp({ctx, _}) => exp_ctx_view(ctx)
  | InfoPat(_) =>
    div([clss([ctxc, "pat"])], [text("? Pattern Ctxs TODO")])
  | InfoTyp(_) => div([clss([ctxc, "typ"])], [text("? No Ctx for Type")])
  };
};

let inspector_view = (_id: int, ci: Core.Statics.t): Node.t =>
  div([clss(["context-inspector"])], [ctx_sorts_view(ci)]);

let view = (index': option(int), info_map: Core.Statics.map) => {
  let (index, ci) =
    switch (index') {
    | Some(index) => (index, Core.Id.Map.find_opt(index, info_map))
    | None => ((-1), None)
    };
  switch (ci) {
  | None => div([clss(["context-inspector"])], [text("No Static Data")])
  | Some(ci) => inspector_view(index, ci)
  };
};
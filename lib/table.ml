type palette = {
  bold : string;
  dim : string;
  cyan : string;
  green : string;
  red : string;
  reset : string;
}

let plain =
  { bold = ""; dim = ""; cyan = ""; green = ""; red = ""; reset = "" }

let colored =
  {
    bold = "\027[1m";
    dim = "\027[2m";
    cyan = "\027[36m";
    green = "\027[32m";
    red = "\027[31m";
    reset = "\027[0m";
  }

let make_palette ~tty ~no_color = if tty && not no_color then colored else plain

let palette () =
  make_palette
    ~tty:(Unix.isatty Unix.stdout)
    ~no_color:(Sys.getenv_opt "NO_COLOR" <> None)

let null_text = "-"
let display value = if value = "" then null_text else value

let widths headers rows =
  List.mapi
    (fun index header ->
      List.fold_left
        (fun acc row ->
          match List.nth_opt row index with
          | Some cell -> max acc (String.length cell)
          | None -> acc)
        (String.length header) rows)
    headers

let pad text width = text ^ String.make (max 0 (width - String.length text)) ' '

let render ~palette:p ~headers ~rows ~colorize =
  let widths = widths headers rows in
  let buffer = Buffer.create 256 in
  let line cells =
    let padded =
      List.mapi (fun index cell -> pad cell (List.nth widths index)) cells
    in
    String.concat "  " padded |> String.trim
  in
  Buffer.add_string buffer (p.bold ^ line headers ^ p.reset);
  Buffer.add_char buffer '\n';
  Buffer.add_string buffer
    (p.dim
    ^ (List.map (fun width -> String.make width '-') widths
      |> String.concat "  ")
    ^ p.reset);
  Buffer.add_char buffer '\n';
  List.iter
    (fun row ->
      let padded =
        List.mapi
          (fun index cell ->
            colorize index (pad cell (List.nth widths index)) cell)
          row
      in
      Buffer.add_string buffer (String.concat "  " padded |> String.trim);
      Buffer.add_char buffer '\n')
    rows;
  Buffer.contents buffer

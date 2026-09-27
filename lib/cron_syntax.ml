
let macros =
  [ "@yearly"; "@annually"; "@monthly"; "@weekly"; "@daily"; "@midnight";
    "@hourly" ]

let fields text =
  String.split_on_char ' ' (String.map (function '\t' -> ' ' | c -> c) text)
  |> List.filter (fun field -> field <> "")

let validate text =
  let parts = fields text in
  match parts with
  | [] -> Error "cron expression is empty"
  | [ single ] when String.length single > 0 && single.[0] = '@' ->
      if List.mem single macros then Ok ()
      else
        Error
          (Printf.sprintf "unsupported macro '%s' (known: %s)" single
             (String.concat ", " macros))
  | _ ->
      let count = List.length parts in
      if count = 5 || count = 6 || count = 7 then Ok ()
      else
        Error
          (Printf.sprintf "cron expression needs 5, 6 or 7 fields, got %d"
             count)

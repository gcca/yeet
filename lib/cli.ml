exception Usage of string

type t = { options : (string * string) list; positionals : string list }

let default_db = "data/yeet.db"

let db_flag = [ "--db"; "-d" ]

let find_alias groups flag =
  List.find_opt (fun group -> List.mem flag group) groups
  |> Option.map List.hd

let parse ?(valued = []) ?(boolean = []) args =
  let valued = db_flag :: valued in
  let record name value acc =
    if List.mem_assoc name acc then
      raise (Usage (Printf.sprintf "duplicate option: %s" name));
    (name, value) :: acc
  in
  let rec loop options positionals = function
    | [] -> { options = List.rev options; positionals = List.rev positionals }
    | "--" :: rest ->
        {
          options = List.rev options;
          positionals = List.rev_append positionals rest;
        }
    | arg :: rest when String.length arg > 1 && arg.[0] = '-' -> (
        match find_alias valued arg with
        | Some name -> (
            match rest with
            | value :: tail -> loop (record name value options) positionals tail
            | [] -> raise (Usage (Printf.sprintf "missing value for %s" arg)))
        | None -> (
            match find_alias boolean arg with
            | Some name -> loop (record name "" options) positionals rest
            | None -> raise (Usage (Printf.sprintf "unknown option: %s" arg))))
    | arg :: rest -> loop options (arg :: positionals) rest
  in
  loop [] [] args

let get t name = List.assoc_opt name t.options
let has t name = List.mem_assoc name t.options
let get_or t name fallback = Option.value (get t name) ~default:fallback

let get_int t name fallback =
  match get t name with
  | None -> fallback
  | Some raw -> (
      match int_of_string_opt raw with
      | Some value -> value
      | None -> raise (Usage (Printf.sprintf "%s expects a number" name)))

let db_path t =
  match get t "--db" with
  | Some path -> path
  | None -> Option.value (Sys.getenv_opt "YEET_DB") ~default:default_db

let positionals t = t.positionals

let expect_positionals t count label =
  let actual = List.length t.positionals in
  if actual <> count then
    raise
      (Usage
         (Printf.sprintf "expected %d argument(s) (%s), got %d" count label
            actual));
  t.positionals

let main ~usage action =
  let args = List.tl (Array.to_list Sys.argv) in
  if List.exists (fun arg -> arg = "-h" || arg = "--help") args then (
    print_endline usage;
    exit 0);
  match action args with
  | Ok () -> exit 0
  | Error message ->
      prerr_endline ("error: " ^ message);
      exit 1
  | exception Usage message ->
      Printf.eprintf "error: %s\n%s\n" message usage;
      exit 2

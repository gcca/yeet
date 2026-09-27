let ( let* ) = Result.bind

let describe db action rc =
  Printf.sprintf "%s: %s (%s)" action (Sqlite3.errmsg db)
    (Sqlite3.Rc.to_string rc)

let exec db action sql =
  match Sqlite3.exec db sql with
  | Sqlite3.Rc.OK -> Ok ()
  | rc -> Error (describe db action rc)

let open_existing path =
  match Sys.file_exists path with
  | false ->
      Error (Printf.sprintf "database '%s' does not exist, run yeet-initdb" path)
  | true -> (
      try Ok (Sqlite3.db_open ~mode:`NO_CREATE path)
      with Sqlite3.Error message ->
        Error (Printf.sprintf "cannot open '%s': %s" path message))

let open_or_create path =
  let parent = Filename.dirname path in
  let* () =
    try
      if parent <> "" && not (Sys.file_exists parent) then
        Ok (Unix.mkdir parent 0o755)
      else Ok ()
    with Unix.Unix_error (code, _, _) ->
      Error
        (Printf.sprintf "cannot create directory '%s': %s" parent
           (Unix.error_message code))
  in
  try Ok (Sqlite3.db_open path)
  with Sqlite3.Error message ->
    Error (Printf.sprintf "cannot open '%s': %s" path message)

let close db = ignore (Sqlite3.db_close db)

let with_db opener path action =
  let* db = opener path in
  let result = try action db with exn -> Error (Printexc.to_string exn) in
  close db;
  result

let apply_pragmas db =
  let* () = exec db "enabling foreign keys" "PRAGMA foreign_keys = ON" in
  let* () = exec db "setting WAL mode" "PRAGMA journal_mode = WAL" in
  exec db "setting synchronous mode" "PRAGMA synchronous = NORMAL"

let user_version db =
  let stmt = Sqlite3.prepare db "PRAGMA user_version" in
  let result =
    match Sqlite3.step stmt with
    | Sqlite3.Rc.ROW -> Ok (Sqlite3.column_int stmt 0)
    | rc -> Error (describe db "reading schema version" rc)
  in
  ignore (Sqlite3.finalize stmt);
  result

let set_user_version db version =

  exec db "setting schema version"
    (Printf.sprintf "PRAGMA user_version = %d" version)

let rows db sql decode =
  let stmt = Sqlite3.prepare db sql in
  let rec loop acc =
    match Sqlite3.step stmt with
    | Sqlite3.Rc.ROW -> loop (decode stmt :: acc)
    | Sqlite3.Rc.DONE -> Ok (List.rev acc)
    | rc -> Error (describe db "reading rows" rc)
  in
  let result = loop [] in
  ignore (Sqlite3.finalize stmt);
  result

let text_or stmt index fallback =
  match Sqlite3.column stmt index with
  | Sqlite3.Data.NULL -> fallback
  | value -> Sqlite3.Data.to_string_coerce value

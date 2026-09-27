let ( let* ) = Result.bind

let apply_one db (version, name, sql) =
  let* () = Db.exec db "beginning migration" "BEGIN IMMEDIATE" in
  let attempt =
    let* () =
      Db.exec db (Printf.sprintf "applying migration %d (%s)" version name) sql
    in
    Db.set_user_version db version
  in
  match attempt with
  | Ok () -> Db.exec db "committing migration" "COMMIT"
  | Error message ->
      ignore (Sqlite3.exec db "ROLLBACK");
      Error message

let pending db =
  let* current = Db.user_version db in
  if current > Schema.expected_version then
    Error
      (Printf.sprintf
         "database schema version %d is newer than this build supports (%d)"
         current Schema.expected_version)
  else
    Ok
      (List.filter
         (fun (version, _, _) -> version > current)
         Schema.migrations)

let run db =
  let* todo = pending db in
  let rec loop applied = function
    | [] -> Ok applied
    | migration :: rest ->
        let* () = apply_one db migration in
        loop (applied + 1) rest
  in
  loop 0 todo

let require_current db =
  let* current = Db.user_version db in
  if current = Schema.expected_version then Ok ()
  else if current = 0 then
    Error "database is not initialized, run yeet-initdb"
  else
    Error
      (Printf.sprintf
         "database schema version is %d but %d is required, run yeet-initdb"
         current Schema.expected_version)

let ( let* ) = Result.bind

type t = {
  name : string;
  command : string;
  args : string;
  cron : string;
  enabled : bool;
  timeout_s : int;
  last_fire_at : string;
  last_status : string;
}

let step_done db action stmt =
  let rc = Sqlite3.step stmt in
  ignore (Sqlite3.finalize stmt);
  match rc with
  | Sqlite3.Rc.DONE -> Ok ()
  | rc -> Error (Db.describe db action rc)

let bind_text stmt index value = ignore (Sqlite3.bind_text stmt index value)

let bind_opt_text stmt index value =
  match value with
  | Some text -> ignore (Sqlite3.bind_text stmt index text)
  | None -> ignore (Sqlite3.bind stmt index Sqlite3.Data.NULL)

let insert db ~name ~command ~args ~cron ~enabled ~timeout_s ~cwd =
  let stmt =
    Sqlite3.prepare db
      "INSERT INTO job (name, command, args, cron, enabled, timeout_s, cwd) \
       VALUES (?, ?, ?, ?, ?, ?, ?)"
  in
  bind_text stmt 1 name;
  bind_text stmt 2 command;
  bind_text stmt 3 args;
  bind_opt_text stmt 4 cron;
  ignore (Sqlite3.bind_int stmt 5 (if enabled then 1 else 0));
  ignore (Sqlite3.bind_int stmt 6 timeout_s);
  bind_opt_text stmt 7 cwd;
  step_done db (Printf.sprintf "adding job '%s'" name) stmt

let affecting db action sql name =
  let stmt = Sqlite3.prepare db sql in
  bind_text stmt 1 name;
  let* () = step_done db action stmt in
  Ok (Sqlite3.changes db)

let delete db name =
  affecting db
    (Printf.sprintf "removing job '%s'" name)
    "DELETE FROM job WHERE name = ?" name

let set_enabled db name enabled =
  affecting db
    (Printf.sprintf "updating job '%s'" name)
    (Printf.sprintf "UPDATE job SET enabled = %d, updated_at = \
                     CURRENT_TIMESTAMP WHERE name = ?"
       (if enabled then 1 else 0))
    name

let list db ~all =
  let filter = if all then "" else "WHERE j.enabled = 1 " in
  Db.rows db
    (Printf.sprintf
       "SELECT j.name, j.command, j.args, COALESCE(j.cron, ''), j.enabled, \
        j.timeout_s, COALESCE(s.last_fire_at, ''), \
        COALESCE((SELECT r.status FROM run r WHERE r.job_id = j.id ORDER BY \
        r.started_at DESC LIMIT 1), '') \
        FROM job j LEFT JOIN job_state s ON s.job_id = j.id %sORDER BY j.name"
       filter)
    (fun stmt ->
      {
        name = Sqlite3.column_text stmt 0;
        command = Sqlite3.column_text stmt 1;
        args = Sqlite3.column_text stmt 2;
        cron = Sqlite3.column_text stmt 3;
        enabled = Sqlite3.column_int stmt 4 <> 0;
        timeout_s = Sqlite3.column_int stmt 5;
        last_fire_at = Sqlite3.column_text stmt 6;
        last_status = Sqlite3.column_text stmt 7;
      })

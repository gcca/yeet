type t = {
  id : int;
  job_name : string;
  source : string;
  status : string;
  scheduled_at : string;
  started_at : string;
  duration_ms : string;
  exit_code : string;
  misfire_count : int;
}

type log = {
  log_id : int;
  log_started_at : string;
  log_status : string;
  stdout_text : string;
  stderr_text : string;
}

let filters ~name ~failed =
  let clauses =
    (match name with Some _ -> [ "r.job_name = ?" ] | None -> [])
    @
    if failed then
      [ "r.status IN ('failure', 'timeout', 'spawn_error', 'orphaned')" ]
    else []
  in
  match clauses with
  | [] -> ""
  | clauses -> "WHERE " ^ String.concat " AND " clauses ^ " "

let bind_name stmt name =
  match name with
  | Some value -> ignore (Sqlite3.bind_text stmt 1 value)
  | None -> ()

let query db sql name decode =
  let stmt = Sqlite3.prepare db sql in
  bind_name stmt name;
  let rec loop acc =
    match Sqlite3.step stmt with
    | Sqlite3.Rc.ROW -> loop (decode stmt :: acc)
    | Sqlite3.Rc.DONE -> Ok (List.rev acc)
    | rc -> Error (Db.describe db "reading run history" rc)
  in
  let result = loop [] in
  ignore (Sqlite3.finalize stmt);
  result

let list db ~name ~failed ~limit =
  query db
    (Printf.sprintf
       "SELECT r.id, r.job_name, r.source, r.status, r.scheduled_at, \
        r.started_at, COALESCE(CAST(r.duration_ms AS TEXT), ''), \
        COALESCE(CAST(r.exit_code AS TEXT), ''), r.misfire_count \
        FROM run r %sORDER BY r.started_at DESC, r.id DESC LIMIT %d"
       (filters ~name ~failed) limit)
    name
    (fun stmt ->
      {
        id = Sqlite3.column_int stmt 0;
        job_name = Sqlite3.column_text stmt 1;
        source = Sqlite3.column_text stmt 2;
        status = Sqlite3.column_text stmt 3;
        scheduled_at = Sqlite3.column_text stmt 4;
        started_at = Sqlite3.column_text stmt 5;
        duration_ms = Sqlite3.column_text stmt 6;
        exit_code = Sqlite3.column_text stmt 7;
        misfire_count = Sqlite3.column_int stmt 8;
      })

let logs db ~name ~limit =
  query db
    (Printf.sprintf
       "SELECT r.id, r.started_at, r.status, COALESCE(r.stdout_text, ''), \
        COALESCE(r.stderr_text, '') FROM run r WHERE r.job_name = ? \
        ORDER BY r.started_at DESC, r.id DESC LIMIT %d"
       limit)
    (Some name)
    (fun stmt ->
      {
        log_id = Sqlite3.column_int stmt 0;
        log_started_at = Sqlite3.column_text stmt 1;
        log_status = Sqlite3.column_text stmt 2;
        stdout_text = Sqlite3.column_text stmt 3;
        stderr_text = Sqlite3.column_text stmt 4;
      })

open Yeet_lib

let ( let* ) = Result.bind

let usage =
  "Usage: yeet-logs NAME [-n LIMIT] [-d|--db PATH]\n\n\
  \  Show captured stdout and stderr for a job's recent runs.\n\n\
   Options:\n\
  \  -n, --limit N   how many runs to show [5]\n\
  \  -d, --db PATH   database path [env YEET_DB, default data/yeet.db]\n\
  \  -h, --help      show this message"

let emit p (entry : Run.log) =
  Printf.printf "%s%s  run %d  %s%s\n" p.Table.bold entry.log_started_at
    entry.log_id entry.log_status p.Table.reset;
  if entry.stdout_text <> "" then print_endline entry.stdout_text;
  if entry.stderr_text <> "" then
    Printf.printf "%sstderr:%s %s\n" p.Table.red p.Table.reset
      entry.stderr_text;
  if entry.stdout_text = "" && entry.stderr_text = "" then
    Printf.printf "%s(no output)%s\n" p.Table.dim p.Table.reset;
  print_newline ()

let logs opts =
  match Cli.expect_positionals opts 1 "NAME" with
  | [ name ] ->
      Db.with_db Db.open_existing (Cli.db_path opts) (fun db ->
          let* () = Migrate.require_current db in
          let* entries = Run.logs db ~name ~limit:(Cli.get_int opts "--limit" 5) in
          let p = Table.palette () in
          if entries = [] then (
            print_endline (p.dim ^ "no runs for '" ^ name ^ "'" ^ p.reset);
            Ok ())
          else (
            List.iter (emit p) entries;
            Ok ()))
  | _ -> assert false

let () =
  Cli.main ~usage (fun args ->
      logs (Cli.parse ~valued:[ [ "--limit"; "-n" ] ] args))

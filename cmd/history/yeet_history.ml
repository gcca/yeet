open Yeet_lib

let ( let* ) = Result.bind

let usage =
  "Usage: yeet-history [NAME] [-n LIMIT] [--failed] [-d|--db PATH]\n\n\
  \  Show recent runs, newest first.\n\n\
   Options:\n\
  \  -n, --limit N   how many runs to show [20]\n\
  \      --failed    only failures, timeouts, spawn errors and orphans\n\
  \  -d, --db PATH   database path [env YEET_DB, default data/yeet.db]\n\
  \  -h, --help      show this message"

let headers =
  [ "ID"; "JOB"; "SOURCE"; "STATUS"; "SCHEDULED"; "STARTED"; "MS"; "EXIT" ]

let row (r : Run.t) =
  [
    string_of_int r.id;
    r.job_name;
    r.source;
    (if r.status = "misfire" && r.misfire_count > 1 then
       Printf.sprintf "misfire x%d" r.misfire_count
     else r.status);
    r.scheduled_at;
    r.started_at;
    Table.display r.duration_ms;
    Table.display r.exit_code;
  ]

let ok_status = [ "success" ]

let history opts =
  let name = match Cli.positionals opts with [] -> None | n :: _ -> Some n in
  Db.with_db Db.open_existing (Cli.db_path opts) (fun db ->
      let* () = Migrate.require_current db in
      let* runs =
        Run.list db ~name ~failed:(Cli.has opts "--failed")
          ~limit:(Cli.get_int opts "--limit" 20)
      in
      let p = Table.palette () in
      if runs = [] then (
        print_endline (p.dim ^ "no runs" ^ p.reset);
        Ok ())
      else begin
        let colorize index padded raw =
          match index with
          | 1 -> p.cyan ^ padded ^ p.reset
          | 3 ->
              (if List.mem raw ok_status then p.green else p.red)
              ^ padded ^ p.reset
          | _ -> padded
        in
        print_string
          (Table.render ~palette:p ~headers ~rows:(List.map row runs)
             ~colorize);
        Ok ()
      end)

let () =
  Cli.main ~usage (fun args ->
      history
        (Cli.parse
           ~valued:[ [ "--limit"; "-n" ] ]
           ~boolean:[ [ "--failed" ] ]
           args))

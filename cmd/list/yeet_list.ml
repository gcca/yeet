open Yeet_lib

let ( let* ) = Result.bind

let usage =
  "Usage: yeet-list [--all] [-d|--db PATH]\n\n\
  \  List jobs. Disabled jobs are hidden unless --all is given.\n\n\
   Options:\n\
  \      --all       include disabled jobs\n\
  \  -d, --db PATH   database path [env YEET_DB, default data/yeet.db]\n\
  \  -h, --help      show this message"

let headers =
  [ "NAME"; "COMMAND"; "ARGS"; "CRON"; "ENABLED"; "LAST FIRE"; "LAST STATUS" ]

let row (job : Job.t) =
  [
    job.name;
    job.command;
    Table.display job.args;
    Table.display job.cron;
    (if job.enabled then "yes" else "no");
    Table.display job.last_fire_at;
    Table.display job.last_status;
  ]

let list opts =
  let all = Cli.has opts "--all" in
  Db.with_db Db.open_existing (Cli.db_path opts) (fun db ->
      let* () = Migrate.require_current db in
      let* jobs = Job.list db ~all in
      let p = Table.palette () in
      if jobs = [] then (
        print_endline (p.dim ^ "no jobs" ^ p.reset);
        Ok ())
      else begin
        let colorize index padded raw =
          match index with
          | 0 -> p.cyan ^ padded ^ p.reset
          | 4 -> (if raw = "yes" then p.green else p.red) ^ padded ^ p.reset
          | _ -> padded
        in
        print_string
          (Table.render ~palette:p ~headers ~rows:(List.map row jobs)
             ~colorize);
        let enabled = List.length (List.filter (fun j -> j.Job.enabled) jobs) in
        Printf.printf "%s%d job(s), %d enabled, %d disabled%s\n" p.dim
          (List.length jobs) enabled
          (List.length jobs - enabled)
          p.reset;
        Ok ()
      end)

let () =
  Cli.main ~usage (fun args ->
      list (Cli.parse ~boolean:[ [ "--all" ] ] args))

open Yeet_lib

let ( let* ) = Result.bind

let usage =
  "Usage: yeet-add NAME [OPTIONS] -- COMMAND [ARG...]\n\n\
  \  Add a job. Everything after -- is the program and its arguments,\n\
  \  stored as a JSON array so quoted arguments survive intact.\n\n\
   Options:\n\
  \  -c, --cron EXPR    schedule; omit for a manual-only job\n\
  \      --timeout SEC  per-job timeout in seconds [300]\n\
  \      --cwd DIR      working directory for the child\n\
  \      --disabled     add the job without enabling it\n\
  \  -d, --db PATH      database path [env YEET_DB, default data/yeet.db]\n\
  \  -h, --help         show this message\n\n\
   Example:\n\
  \  yeet-add backup -c '0 3 * * *' -- /usr/bin/rsync -a --delete /src /dst"

let add opts =
  let cron = Cli.get opts "--cron" in
  (match cron with
  | None -> ()
  | Some expr -> (
      match Cron_syntax.validate expr with
      | Ok () -> ()
      | Error message -> raise (Cli.Usage message)));
  match Cli.positionals opts with
  | [] -> raise (Cli.Usage "missing job NAME")
  | [ _ ] -> raise (Cli.Usage "missing command after --")
  | name :: command :: args ->
      Db.with_db Db.open_existing (Cli.db_path opts) (fun db ->
          let* () = Db.apply_pragmas db in
          let* () = Migrate.require_current db in
          let* () =
            Job.insert db ~name ~command ~args:(Json.array args) ~cron
              ~enabled:(not (Cli.has opts "--disabled"))
              ~timeout_s:(Cli.get_int opts "--timeout" 300)
              ~cwd:(Cli.get opts "--cwd")
          in
          Printf.printf "added '%s'\n" name;
          Ok ())

let () =
  Cli.main ~usage (fun args ->
      add
        (Cli.parse
           ~valued:[ [ "--cron"; "-c" ]; [ "--timeout" ]; [ "--cwd" ] ]
           ~boolean:[ [ "--disabled" ] ]
           args))

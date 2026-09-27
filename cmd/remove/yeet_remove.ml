open Yeet_lib

let ( let* ) = Result.bind

let usage =
  "Usage: yeet-remove NAME [-d|--db PATH]\n\n\
  \  Remove a job and its run history.\n\n\
   Options:\n\
  \  -d, --db PATH   database path [env YEET_DB, default data/yeet.db]\n\
  \  -h, --help      show this message"

let remove opts =
  match Cli.expect_positionals opts 1 "NAME" with
  | [ name ] ->
      Db.with_db Db.open_existing (Cli.db_path opts) (fun db ->
          let* () = Db.apply_pragmas db in
          let* () = Migrate.require_current db in
          let* changed = Job.delete db name in
          if changed = 0 then Error (Printf.sprintf "no job named '%s'" name)
          else (
            Printf.printf "removed '%s'\n" name;
            Ok ()))
  | _ -> assert false

let () = Cli.main ~usage (fun args -> remove (Cli.parse args))

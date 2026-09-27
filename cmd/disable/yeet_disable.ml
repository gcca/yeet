open Yeet_lib

let ( let* ) = Result.bind

let usage =
  "Usage: yeet-disable NAME [-d|--db PATH]\n\n  \  Mark a job as disabled. Idempotent.\n\n   Options:\n  \  -d, --db PATH   database path [env YEET_DB, default data/yeet.db]\n  \  -h, --help      show this message"

let apply opts =
  match Cli.expect_positionals opts 1 "NAME" with
  | [ name ] ->
      Db.with_db Db.open_existing (Cli.db_path opts) (fun db ->
          let* () = Db.apply_pragmas db in
          let* () = Migrate.require_current db in
          let* changed = Job.set_enabled db name false in
          if changed = 0 then Error (Printf.sprintf "no job named '%s'" name)
          else (
            Printf.printf "disabled '%s'\n" name;
            Ok ()))
  | _ -> assert false

let () = Cli.main ~usage (fun args -> apply (Cli.parse args))

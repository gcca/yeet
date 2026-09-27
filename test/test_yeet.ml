open Yeet_lib

let failures = ref 0

let check name condition =
  if not condition then begin
    incr failures;
    Printf.eprintf "FAIL %s\n" name
  end

let equal name expected actual =
  if expected <> actual then begin
    incr failures;
    Printf.eprintf "FAIL %s\n  expected: %s\n  actual:   %s\n" name expected
      actual
  end

let is_error name = function
  | Error _ -> ()
  | Ok () ->
      incr failures;
      Printf.eprintf "FAIL %s (expected an error)\n" name

let is_ok name = function
  | Ok () -> ()
  | Error message ->
      incr failures;
      Printf.eprintf "FAIL %s: %s\n" name message

let test_json () =
  equal "json.empty" "[]" (Json.array []);
  equal "json.one" "[\"a\"]" (Json.array [ "a" ]);
  equal "json.spaces" "[\"two words\"]" (Json.array [ "two words" ]);
  equal "json.dash" "[\"-a\",\"--delete\"]" (Json.array [ "-a"; "--delete" ]);
  equal "json.quote" "[\"say \\\"hi\\\"\"]" (Json.array [ "say \"hi\"" ]);
  equal "json.backslash" "[\"a\\\\b\"]" (Json.array [ "a\\b" ]);
  equal "json.newline" "[\"a\\nb\"]" (Json.array [ "a\nb" ]);
  equal "json.control" "[\"\\u0001\"]" (Json.array [ "\001" ])

let test_cron_syntax () =
  is_ok "cron.five" (Cron_syntax.validate "*/5 * * * *");
  is_ok "cron.six" (Cron_syntax.validate "0 */5 * * * *");
  is_ok "cron.seven" (Cron_syntax.validate "0 0 3 * * * 2027");
  is_ok "cron.macro" (Cron_syntax.validate "@daily");
  is_ok "cron.extra_spaces" (Cron_syntax.validate "0   0  3 * * *");
  is_error "cron.empty" (Cron_syntax.validate "");
  is_error "cron.four" (Cron_syntax.validate "* * * *");
  is_error "cron.eight" (Cron_syntax.validate "* * * * * * * *");
  is_error "cron.reboot" (Cron_syntax.validate "@reboot");
  is_error "cron.unknown_macro" (Cron_syntax.validate "@fortnightly")

let test_cli_parse () =
  let opts =
    Cli.parse ~valued:[ [ "--cron"; "-c" ] ] ~boolean:[ [ "--disabled" ] ]
      [ "name"; "-c"; "@daily"; "--disabled"; "--"; "/bin/echo"; "-n"; "hi" ]
  in
  equal "cli.cron" "@daily" (Cli.get_or opts "--cron" "");
  check "cli.boolean" (Cli.has opts "--disabled");
  equal "cli.positionals" "name|/bin/echo|-n|hi"
    (String.concat "|" (Cli.positionals opts));

  let passthrough =
    Cli.parse ~boolean:[ [ "--disabled" ] ]
      [ "n"; "--"; "/bin/x"; "--disabled" ]
  in
  check "cli.passthrough_not_consumed" (not (Cli.has passthrough "--disabled"));
  equal "cli.passthrough" "n|/bin/x|--disabled"
    (String.concat "|" (Cli.positionals passthrough));
  equal "cli.alias" "/tmp/a.db"
    (Cli.db_path (Cli.parse [ "-d"; "/tmp/a.db" ]))

let test_cli_errors () =
  let raises name f =
    match f () with
    | _ ->
        incr failures;
        Printf.eprintf "FAIL %s (expected Usage)\n" name
    | exception Cli.Usage _ -> ()
  in
  raises "cli.unknown" (fun () -> Cli.parse [ "--nope" ]);
  raises "cli.missing_value" (fun () ->
      Cli.parse ~valued:[ [ "--cron" ] ] [ "--cron" ]);
  raises "cli.duplicate" (fun () -> Cli.parse [ "-d"; "a"; "--db"; "b" ]);
  raises "cli.arity" (fun () ->
      Cli.expect_positionals (Cli.parse [ "a"; "b" ]) 1 "NAME");
  raises "cli.not_a_number" (fun () ->
      Cli.get_int (Cli.parse ~valued:[ [ "-n" ] ] [ "-n"; "x" ]) "-n" 0)

let test_table () =
  let plain = Table.plain in
  let rendered =
    Table.render ~palette:plain ~headers:[ "NAME"; "N" ]
      ~rows:[ [ "alpha"; "1" ]; [ "b"; "22" ] ]
      ~colorize:(fun _ padded _ -> padded)
  in
  let lines = String.split_on_char '\n' rendered in
  equal "table.header" "NAME   N" (List.nth lines 0);
  equal "table.rule" "-----  --" (List.nth lines 1);
  equal "table.row0" "alpha  1" (List.nth lines 2);
  equal "table.row1" "b      22" (List.nth lines 3);
  equal "table.null" "-" (Table.display "");
  equal "table.value" "x" (Table.display "x");
  check "table.no_color_when_not_tty"
    (Table.make_palette ~tty:false ~no_color:false = Table.plain);
  check "table.no_color_env"
    (Table.make_palette ~tty:true ~no_color:true = Table.plain);
  check "table.color_when_tty"
    (Table.make_palette ~tty:true ~no_color:false = Table.colored)

let test_schema () =
  check "schema.has_migrations" (Schema.migrations <> []);
  equal "schema.expected_version" "1" (string_of_int Schema.expected_version);
  let versions = List.map (fun (v, _, _) -> v) Schema.migrations in
  check "schema.versions_unique"
    (List.length (List.sort_uniq compare versions) = List.length versions);
  check "schema.versions_positive" (List.for_all (fun v -> v > 0) versions)

let () =
  test_json ();
  test_cron_syntax ();
  test_cli_parse ();
  test_cli_errors ();
  test_table ();
  test_schema ();
  if !failures > 0 then (
    Printf.eprintf "\n%d check(s) failed\n" !failures;
    exit 1)
  else print_endline "all checks passed"

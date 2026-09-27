# yeet

Runs programs from a sqlite3 table on a cron schedule, and records what
happened.

It is a standalone reimplementation of ticketeer's `pulse` module. The
execution mechanics are ported from it; the scheduling, persistence and
lifecycle are not, because pulse loses fires.

## Use

```sh
export YEET_DB=data/yeet.db

yeet-initdb
yeet-add hello -c '*/5 * * * *' -- /bin/echo 'hello world'
yeet-add backup -c '0 3 * * *' -- /usr/bin/rsync -a --delete /src /dst
yeet-list

yeet trigger hello          # run it now, in the foreground
yeet run                    # the scheduler
yeet run --once             # one pass, then exit

yeet-history                # recent runs
yeet-logs hello             # captured output
```

## License

AGPL-3.0. Vendored third-party headers keep their own licenses; see
`3rdparty/README.md`.

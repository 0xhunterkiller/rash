# rash

A small, whitelist-gated command runner for Linux, written in C.

`rash` ("restricted-ash") sits in front of `exec`: given a command, it checks the
name against an allowlist in a root-owned config, sanitizes the environment, runs
the command as one fixed non-root user, and appends an audit line to a log. It is
meant as a narrow gate for constraining *which* commands a caller (for example an
automation account or an agent) may launch on a box.

> **Status: early / hardening in progress.** `rash` today filters the command
> *name*. It does **not** yet isolate what a permitted command can see or touch,
> so it is **not** a sandbox. See [Limitations](#limitations) and
> [SECURITY.md](SECURITY.md) before relying on it for anything. Process isolation
> (namespaces + seccomp) is the next milestone.

## What it does

- **Allowlist by name.** Only commands whose name exactly matches a whitelist
  entry are run; everything else is denied and logged.
- **User anchoring.** Refuses to run unless the real uid/gid match the single
  non-root `sysuser` named in the config (and rejects privileged users, uid/gid
  &lt; 1000). `rash` is *not* setuid — it grants no privilege of its own.
- **Environment reset.** The child starts from an empty environment; only an
  explicit list of variables (and, when `syspath` is set, that `PATH`) is
  re-applied.
- **Explicit binary resolution.** With `syspath` set, the command is looked up on
  that trusted `PATH`. With `syspath` unset, only **absolute-path** commands run
  (a bare name is refused, so nothing resolves against the caller's cwd).
- **Audit log.** Every run and every denial is timestamped and appended to a
  log opened `O_NOFOLLOW | O_CLOEXEC` (symlink-safe, not inherited by the child).
- **Meaningful exit codes.** `rash`'s own failures are distinguishable from the
  command's (see [Exit codes](#exit-codes)).

## Build

Requires `gcc`, `make`, and [tomlc17](https://github.com/cktan/tomlc17) (a TOML
parser; no distro package — build it from source):

```sh
# dependency: tomlc17
git clone --branch R260821 https://github.com/cktan/tomlc17
make -C tomlc17 install            # installs libtomlc17 into /usr/local

# rash
make build                         # -> target/rash  (-Wall -Wextra -Werror -D_GNU_SOURCE)
sudo make install                  # -> /usr/local/bin/rash
```

Prebuilt Linux x86_64 binaries are attached to each
[release](https://github.com/0xhunterkiller/rash/releases).

## Configuration

`rash` reads **`/etc/rash/rash.toml`** (path is fixed). This file governs the
security policy, so it **must be owned by root and not group/world-writable.**

```toml
sysuser = "deploy"                 # the one non-root user allowed to run rash

whitelist = [                      # exact command names/paths that may run
  "/bin/echo",
  "/usr/bin/id",
]

# Optional. If set, commands are resolved via this PATH (execvp).
# If omitted, only absolute-path whitelist entries run (execv), and bare
# names are refused — safer, so this is commented out by default.
# syspath = "/usr/bin:/bin"

logfilepath = "/var/log/rash.log"  # audit log (put it in a root-owned dir)

env = [                            # env var NAMES to forward to the child
  "HOME",
]
```

Notes:
- If every whitelist entry is an absolute path and `syspath` is omitted, `rash`
  runs exactly the file you named — nothing is resolved by search or by cwd.
- `env` forwards variable *values* from the caller. Do not list loader/startup
  variables (`LD_*`, `BASH_ENV`, `PYTHONPATH`, …) — see [Limitations](#limitations).

## Usage

```sh
rash <command> [args...]
```

```
$ rash /bin/echo hello
hello

$ rash rm -rf /            # not on the whitelist
this command is not allowed: rm
```

## Exit codes

| Code      | Meaning                                             |
|-----------|-----------------------------------------------------|
| `0`–`124` | the command's own exit status, passed through       |
| `125`     | `rash` itself failed (bad config, denied command, non-absolute rejection, clone/waitpid failure) |
| `126`     | command found but not executable                    |
| `127`     | command not found                                   |
| `128`+`N` | command killed by signal `N`                        |

This follows the convention used by `env`, `timeout`, and `nohup`. Because Unix
exit codes are 8-bit, `125`–`127` are reserved by convention — a child that
itself exits with one of those values is indistinguishable from `rash`'s.

## Limitations

`rash` gates the command *name*, and that has a hard ceiling:

- **Interpreters and launchers escape the gate.** Whitelisting `bash`, `sh`,
  `python`, `env`, `find`, etc. gives full arbitrary execution (`bash -c …`,
  `env cp …`). Only whitelist programs that do one thing.
- **Arguments are not constrained.** A permitted `cat` reads any file the user
  can read; a permitted `tee` writes anywhere it can. The gate checks the verb,
  never the object.
- **No isolation yet.** No namespaces or seccomp — a permitted command has the
  user's full view of the filesystem, network, and processes.
- **No resource limits or timeout.** A permitted command can spin CPU, exhaust
  memory, or hang the parent.
- **Environment values pass through.** `env` forwards caller-set *values*; a
  bad entry can influence a whitelisted interpreter.
- **Audit-log line injection.** A crafted (denied) command name containing a
  newline can forge an extra log line.

These are tracked and being worked through; the endgame is real isolation, at
which point the name-filter is just a first hurdle. See [SECURITY.md](SECURITY.md).

## License

MIT — see [LICENSE](LICENSE).

# Security

`rash` is a security tool, and an early one. This document is honest about what
it defends, what it does not, and what has been hardened so far. If you are
evaluating `rash`, read this before the README's feature list.

## Threat model

**What `rash` is.** A gate that lets a single, non-root, anchored user launch
only a fixed set of command names, with a reset environment and an audit trail.
It runs as that same user and holds no privilege of its own (it is not setuid).

**What `rash` assumes.**
- `/etc/rash/rash.toml` is owned by root and not group/world-writable. Whoever
  can write the config controls the whitelist and therefore the machine.
- Directories on `syspath` (if used) are root-owned and not writable by the
  anchored user.

**What `rash` does *not* provide (today).**
- **Isolation.** A permitted command has the anchored user's full view of the
  filesystem, network, and process table. `rash` does not contain what a command
  can *see or touch* — only which command *names* may start.
- **Protection against interpreters.** Any whitelisted program that runs
  arbitrary code (`bash`, `sh`, `python`, `env`, `find -exec`, …) defeats the
  gate entirely. Name-filtering has a ceiling; this is it.
- **Argument policy.** A permitted `cat`/`cp`/`tee` still acts on any path the
  user can reach.

`rash` is therefore a *speed bump and an audit point*, not a sandbox. The
roadmap's endgame is process isolation (Linux namespaces + seccomp), after which
the name-filter becomes a first hurdle rather than the whole defense.

## Hardening done so far

This project has been developed with an adversarial, "try to break it, then fix
it" loop. Fixes below were verified with AddressSanitizer and/or direct exploit
reproduction.

**Memory safety (AddressSanitizer-verified):**
- Wild pointer from a non-string whitelist entry — fixed (count only filled slots).
- Heap overflow from an over-long `sysuser` — fixed (`strdup`, no fixed buffer).
- Uninitialized config struct on parse failure — fixed (`{0}` + `toml_free`).
- Leaks in `free_config` — fixed (arrays freed unconditionally).

**Execution integrity:**
- **cwd-hijack closed.** With `syspath` unset, a bare-name whitelist entry used
  to resolve against the caller's working directory (`execv("echo")` → `./echo`).
  A parent-side guard now refuses any non-absolute command, so a planted `./echo`
  can no longer run. Bad config now fails *safe* (deny), not dangerous (execute).
- **Environment reset.** The child starts from an empty environment; only an
  explicit allowlist (and, when `syspath` is set, that `PATH`) is reapplied.

**Audit integrity:**
- **Symlink-safe, non-inherited log.** The log is opened `O_NOFOLLOW | O_CLOEXEC`,
  so it cannot be redirected through a planted symlink and the child does not
  inherit the log file descriptor (which previously allowed a permitted command
  to forge or read the audit trail).
- **Configurable log location** so the admin can place it in a root-owned dir.
- **Denials are logged.** Wrong-user, unknown-command, and non-absolute
  rejections are all recorded, not just successful runs.

**Correctness that affects trust:**
- **Signalled children reported correctly.** A command killed by a signal now
  exits `128 + signum` and is logged as such, instead of silently reporting
  success.
- **`rash`'s failures are distinguishable** from the command's via reserved exit
  codes `125`/`126`/`127` (see the README).

## Known limitations / open issues

These are known and tracked; do not deploy `rash` where they matter.

- **No isolation** — interpreters and argument abuse bypass the gate (see threat
  model). *This is the headline gap.*
- **No resource limits or timeout** — a permitted command can fork-bomb, spin, or
  hang the parent indefinitely.
- **Environment value passthrough** — the `env` allowlist forwards caller-set
  *values*; a dangerous entry (e.g. `LD_PRELOAD`, `BASH_ENV`) can influence a
  whitelisted interpreter. Deferred: a reliable generic denylist isn't feasible;
  the intended fix is fixed values / a small safe allowlist.
- **Audit-log line injection** — a denied command name containing a newline can
  forge an additional log line, because the name is logged without escaping.
- **No runtime config-ownership check** — `rash` trusts that the config is
  root-owned rather than verifying it (`fstat`) at load. The ownership is
  currently correct, but drift would not be caught.

## Reporting a vulnerability

Please report security issues privately via GitHub's
[security advisories](https://github.com/0xhunterkiller/rash/security/advisories/new)
rather than a public issue. Given the project's early status, the limitations
listed above are known — new findings beyond them are very welcome.

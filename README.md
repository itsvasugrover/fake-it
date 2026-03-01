# fake-it

> **For fun & education only.** Please don't use this to misrepresent your actual work.

A blazing-fast C++ replacement for [artiebits/fake-git-history](https://github.com/artiebits/fake-git-history).  
The original Node.js tool works great, but spawning a `git commit` process for every single commit means generating years of history can take **minutes**. `fake-it` uses [libgit2](https://libgit2.org/) directly and creates the same history in **seconds** — no matter how many commits you ask for.

---

## Features

- Generates hundreds or thousands of backdated commits in < 2 seconds
- Flexible date range (`--startDate` / `--endDate`)
- Three activity distributions: `uniform`, `workHours`, `afterWork`
- Per-day commit frequency and min/max control
- Custom author name and email (`--name` / `--email`)
- Optional GPG signing (`--gpg-sign`)
- Auto-creates a `my-history/` directory (or a custom one via `--dir`) if you are not already inside a git repo
- Preview mode — see what would be generated without touching anything

---

## Building

### Requirements

- CMake ≥ 3.15
- A C++17 compiler
- [Conan 2](https://conan.io/) package manager

### Steps

```bash
# 1. Install dependencies and configure
mkdir -p build && cd build
conan install .. --build=missing
. Release/generators/conanbuild.sh
cd ..

# 2. Configure & build
cmake --preset conan-release
cmake --build build/Release --config Release
```

The binary will be at `build/Release/fake-it`.

### Pre-built binaries

Every tagged release ships ready-to-run binaries for all three platforms — no compiler required.

| Platform | File |
|---|---|
| Linux x86-64 | `fake-it-linux-x86_64` |
| Linux ARM64 | `fake-it-linux-arm64` |
| macOS Intel | `fake-it-macos-x86_64` |
| macOS Apple Silicon | `fake-it-macos-arm64` |

Download the latest binary from the [Releases](../../releases/latest) page, make it executable (`chmod +x` on Linux/macOS), and run it.

---

## Usage

```bash
fake-it [options]
```

### Options

| Flag | Default | Description |
|---|---|---|
| `--dir <path>` | `my-history` | Directory to create/use for the repo. Created automatically if it doesn't exist. If you are already inside a git repo, the current directory is used. |
| `--name <name>` | `Fake Author` | Git author/committer name |
| `--email <addr>` | `fake@example.com` | Git author/committer email |
| `--startDate <YYYY/MM/DD>` | One year ago | First day of generated history |
| `--endDate <YYYY/MM/DD>` | Today | Last day of generated history |
| `--commitsPerDay <min,max>` | `0,4` | Random commit count range per active day |
| `--frequency <0-100>` | `80` | Probability (%) that any given day gets commits |
| `--distribution <mode>` | `uniform` | `uniform`, `workHours` (Tue–Thu bias), `afterWork` (weekend/evening bias) |
| `--gpg-sign` | off | GPG-sign every commit (requires a working GPG key) |
| `--new` | off | Force `git init` even if a repo already exists |
| `--preview` | off | Print what would be generated without creating any commits |
| `-h, --help` | | Print help |

### Examples

```bash
# Quick start — generates one year of history in ./my-history/
./fake-it

# Custom directory, custom author
./fake-it --dir my-contributions --name "Jane Dev" --email "jane@example.com"

# Specific date range with work-hours distribution
./fake-it --startDate 2024/01/01 --endDate 2024/12/31 --distribution workHours

# Preview without writing anything
./fake-it --preview

# GPG-signed commits
./fake-it --gpg-sign

> **Note:** GPG signing is **much slower** than unsigned commits because every signature request is handled by `gpg-agent`, which introduces significant overhead. Generating a large number of signed commits can take much longer than unsigned ones. A more efficient approach may be implemented in the future, but for now, this is the current behavior.

# High-volume: 5–15 commits per day, every day
./fake-it --commitsPerDay 5,15 --frequency 100
```

---

## How it works

1. Determines the target directory (auto-creates `my-history/` or `--dir` if needed).
2. Runs `git init` if no repository exists yet.
3. Writes the local git config with the requested `user.name` and `user.email`.
4. Iterates over every day in the date range, draws a random commit count from the configured distribution, and creates each commit **directly via libgit2** with the correct author timestamp — no subprocess per commit, no shell overhead.
5. If `--gpg-sign` is set, each commit buffer is piped through `gpg -bsa` before being written with `git_commit_create_with_signature`.

---

## Why not just use the original?

| | [artiebits/fake-git-history](https://github.com/artiebits/fake-git-history) | **fake-it** |
|---|---|---|
| Language | Node.js | C++ 17 |
| Commits via | `child_process` (`git commit`) | libgit2 (in-process) |
| 1 year of history | ~1–5 minutes | **< 2 seconds** |
| GPG signing | ✗ | ✓ |
| No runtime required | ✗ Node.js needed | ✓ single binary |

---

## Benchmark

A `benchmark.sh` script is included to compare `fake-it` against the original [`fake-git-history`](https://github.com/artiebits/fake-git-history) side-by-side.

### Requirements

- [bun](https://bun.sh/) — used to run `bunx fake-git-history` without a global install.  
  Install it with:
  ```bash
  curl -fsSL https://bun.sh/install | bash
  ```

### Running

```bash
./benchmark.sh
```

The script will:

1. Check that `bun` is available (exits with instructions if not).
2. Check for the `build/Release/fake-it` binary — builds it via `build.sh` automatically if missing.
3. Create an isolated temp directory under `/tmp/` (cleaned up on exit).
4. Time `fake-it` generating one month of history (`2026/02/01` → `2026/03/01`, 4–7 commits/day, 100% frequency).
5. Time `fake-git-history` running the same range.
6. Print a side-by-side results table with the speedup multiplier.

### Sample output

```bash
==> Running fake-it
    Command: /home/itsvasugrover/codinghub/ivg/fake-it/build/Release/fake-it --frequency 100 --startDate 2025/01/01 --endDate 2026/03/01 --commitsPerDay 4,7 --name Vasu Grover --email itsvasugrover@gmail.com --dir .
    fake-it finished in 0.649 s
    Commits created: 2342

==> Running fake-git-history (bunx)
    Command: bunx fake-git-history --frequency 100 --startDate 2025/01/01 --endDate 2026/03/01 --commitsPerDay 4,7
    fake-git-history finished in 28.147 s
    Commits created: 2363

==> Results

  Tool                           Time
  ──────────────────────────── ──────────
  fake-it (C++, libgit2)         0.649 s
  fake-git-history (Node/bun)    28.147 s

  fake-it was 43.4× faster
```

---

## Disclaimer

This tool is intended purely for **learning, experimentation, and fun** — for example, testing GitHub's contribution graph rendering or exploring libgit2. It is not intended to deceive employers, collaborators, or anyone else about your actual contributions.

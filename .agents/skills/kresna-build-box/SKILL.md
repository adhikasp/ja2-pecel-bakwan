---
name: kresna-build-box
description: Use this skill to build, test or run ja2-pecel-bakwan on kresna, the off-site ARM64 cloud box (OCI Ampere A1). Use it when asked to verify something on Linux/aarch64, to run the full test suites on a machine other than the local one, or to hand a long build or test sweep to a remote machine.
---

# kresna — the off-site ARM64 build box

`kresna` is Adhika's Oracle Cloud Always Free instance: **Ampere Altra `VM.Standard.A1.Flex`,
2 OCPU / 12 GB, Ubuntu 24.04 ARM64**, in `ap-singapore-1`. It is reachable as `ssh kresna`
(already in `~/.ssh/config` on the dev machines: user `ubuntu`, key `~/.ssh/id_ed25519`).
It runs the project's data clone and is set up to build and test this repo.

Use it to verify a change **on Linux/aarch64**, to run the **full test suites** on something
that is not the local laptop, or to push a **long build** off the interactive machine.
Do **not** use it as the inner dev loop — see [Why not as the day-to-day loop](#why-not-as-the-day-to-day-loop).

## What is already installed

Do not re-provision. As of 2026-10-05 the box has:

| | |
|---|---|
| Repo | `~/Workspace/ja2-pecel-bakwan` (clone of `adhikasp/ja2-pecel-bakwan`, on `master`) |
| Build dir | `~/Workspace/ja2-pecel-bakwan/build` (Ninja) |
| Game data | `~/Workspace/ja2-gamedir/app` (contains `Data/`, 861 MB) |
| `game_dir` | set in `~/.ja2/ja2.json` → `/home/ubuntu/Workspace/ja2-gamedir/app` |
| Compiler | gcc 13.3, cmake 3.28, ninja 1.11, lld 18 |
| Rust | rustup 1.88.0 (per `min-rust-version`) at `~/.cargo/bin` |
| Python | `uv` at `~/.local/bin`, `.venv` created in the repo |
| Not present | **sccache** (not in Ubuntu 24.04 ARM64 apt) |

`gcc`, `cmake`, `ninja` are on the default `PATH`; **rust and uv are not** — export them:

```sh
export PATH="$HOME/.cargo/bin:$HOME/.local/bin:$PATH"
```

`sudo` works without a password (`sudo -n`), so installing packages is fine.

## Everyday commands

Everything goes through `tools/dev.py`, exactly as on any other machine:

```sh
ssh kresna
export PATH="$HOME/.cargo/bin:$HOME/.local/bin:$PATH"
cd ~/Workspace/ja2-pecel-bakwan

python3 tools/dev.py status                  # what dev.py sees: tools, game_dir, build state
python3 tools/dev.py build -j 2              # the box has only 2 cores - always pass -j 2
python3 tools/dev.py test                    # C++ unit tests
python3 tools/dev.py e2e                     # full e2e suite (ctest -L e2e)
python3 tools/dev.py e2e tests/e2e/<name>.lua --isolated   # one script
python3 tools/dev.py run                     # play (needs a display - see below)
```

Resolution/golden images are not behind a dev.py subcommand; run raw ctest from the build dir:

```sh
cd build && ctest -L resolution --output-on-failure
```

Headless (`test`, `e2e`, and any `ja2ctl` run) needs **no display**: the game initialises
`SDL_INIT_EVENTS` only and creates no window or renderer. For `--show` you would need a real
display — `xvfb` is installed, so `xvfb-run -s "-screen 0 1280x720x24" python3 tools/dev.py run`
works if you want to watch a screen.

## Measured timings (2 OCPU, so everything is slow but finite)

| Operation | Time |
|---|---|
| Clean build (`build -j 2`, 531 targets incl. Rust) | ~13 min |
| Incremental after touching one `.cc` | ~55 s |
| No-op build | ~0.2 s |
| Unit tests (258) | ~17 s |
| **Full e2e suite (38 tests)** | **~4m40s** |
| `ctest -L resolution` (25 tests) | ~8.5 min |

Use `-j 2`. The job semaphore (`JA2_JOBS` / `--jobs`) caps jobs machine-wide, but with only
2 cores leave it alone.

## Why not as the day-to-day loop

The e2e suite is the surprising one: it is only ~1.8x slower here than on a 12-thread
Ryzen 5 5560U (4m41s vs 2m35s of test time), because the game runs on a virtual clock and
those tests are largely I/O-bound on `.slf` reads rather than CPU-bound. **Full test sweeps
scale fine — use kresna for those.**

Compiling does not scale: 55 s per incremental rebuild against ~7 s locally, and 13 min for a
clean build against ~2 min. For edit → build → test, stay on the local machine and use kresna
for "does this hold on Linux/aarch64?" and for the long sweeps.

## Be a good citizen

This is a shared personal box, not build infrastructure. It also serves DNS (AdGuard Home),
a dashboard, Paperless and Vaultwarden, and it has 12 GB of RAM and ~31 GB free disk.

- Do not leave multi-hour builds running unattended.
- Check `free -h` before a parallel sweep; drop to `-j 2` or wait if something else is loaded.
- The repo + build + game data currently use ~4.3 GB of the 48 GB disk.

## Gotchas that will waste your time if you hit them again

**Do not pipe a script from PowerShell into `ssh`** — PowerShell's native-command pipeline
re-adds CRLF even to a multi-line string, so every remote line gets a stray `\r` and you get
mystifying failures like `tail: option used in invalid context -- 2` or
`cannot open '/tmp/x.log'$'\r'`. Base64 the script instead:

```powershell
$script = @'
cd ~/Workspace/ja2-pecel-bakwan
python3 tools/dev.py status
'@
$b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($script))
ssh kresna "echo $b64 | base64 -d | bash"
```

**Do not `tar ... | ssh kresna tar ...` from PowerShell** for binary data — it corrupted the
stream (288 KB of 860 MB arrived, then tar gave up with "Skipping to next header"). Use
`scp -r`, which transfers at full speed:

```powershell
scp -r "C:\...\Jagged Alliance 2 Gold\Data" kresna:~/Workspace/ja2-gamedir/app/
```

**Long jobs need `setsid nohup`** or they die when the SSH session closes:

```sh
setsid nohup env PATH="$HOME/.cargo/bin:$HOME/.local/bin:$PATH" \
  python3 tools/dev.py build -j 2 > /tmp/build.log 2>&1 < /dev/null &
tail -n 5 /tmp/build.log
```

**`git fetch origin <branch>` will not create a local branch** — it only updates `FETCH_HEAD`,
and `git checkout <branch>` then fails with "pathspec did not match". Use an explicit refspec:

```sh
git fetch origin <branch>:<branch> && git checkout <branch>
```

## The resolution goldens pass on this box

`ctest -L resolution` is **27/27**. It used to be 22/25 here, and the three failures
were long blamed on this box rendering text differently. They were not a FreeType or
architecture difference — glyph rasterization is byte-identical everywhere. The pixels
that differed were three strings of *text*: the version label (which carries
`git rev-parse --short HEAD`), the save screen's wall-clock "Today HH:MM", and the
save's UTC-stamped filename. Each is the build machine's business, not the game's.

`check_resolution.py` now pins all three (`-freeze-wall-clock`, `-version-label`), so the
goldens are the same on every machine. If you see a resolution failure here, treat it as a
real failure — do not regenerate goldens on this box to make it go away. See
[tests/e2e/golden/README.md](../../../tests/e2e/golden/README.md#why-a-golden-image-is-portable).

**Do not regenerate goldens on this box anyway**, even when the change is legitimate:
a regeneration here replaces the reference every other machine compares against with an
ARM one. Generate on the local machine and verify here.

## Bringing it up from scratch

Only needed if the box is ever reset. Ubuntu 24.04 has no SDL3 package, so the pinned one is
built from source (that is what `-DBUILD_SDL_LIB=ON` does, and what `dev.py` turns on
automatically when nothing provides SDL3 — see PR #278):

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build lld pkg-config git curl unzip zip \
  ca-certificates python3-venv xvfb libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxfixes-dev libxss-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev \
  libgles2-mesa-dev libdrm-dev libgbm-dev libasound2-dev libpulse-dev libudev-dev \
  libdbus-1-dev libibus-1.0-dev
curl --proto '=https' --tlsv1.2 -sSfL https://sh.rustup.rs \
  | sh -s -- -y --default-toolchain=$(cat min-rust-version) --profile=minimal
curl -LsSf https://astral.sh/uv/install.sh | sh

git clone https://github.com/adhikasp/ja2-pecel-bakwan.git ~/Workspace/ja2-pecel-bakwan
# then copy the Steam install's Data/ to ~/Workspace/ja2-gamedir/app, and:
mkdir -p ~/.ja2 && printf '{"game_dir": "/home/ubuntu/Workspace/ja2-gamedir/app"}\n' > ~/.ja2/ja2.json
```

Note the game data is a licensed Steam install: it must be copied from a machine that has
JA2 Gold, and `game_dir` must point at the **install root** (the folder containing `Data/`),
never at `Data/` itself — pointing it at `Data` makes the VFS look for `Data/data` and fail
with `Error initializing VFS ... os error 3`.
#!/bin/bash
# bundle-dylibs.sh — make a macOS .app self-contained.
#
# Recursively resolves every non-system dynamic library the app's Mach-O
# binaries depend on (re2 + its abseil closure live under /opt/homebrew),
# copies them into Contents/Frameworks/, rewrites all install names to
# @rpath/<lib>, and ad-hoc re-signs every copied dylib plus the app itself.
#
# Without this step the app links /opt/homebrew/opt/re2/lib/libre2.11.dylib
# by absolute path and crashes on any machine without Homebrew + re2
# (GitHub issue #2). The main binary already carries an
# @executable_path/../Frameworks rpath, so populating Frameworks/ is enough.
#
# Idempotent: re-running on an already-bundled app is a no-op for paths that
# are already @rpath-relative.
#
# Runs at package time on the developer machine, where /usr/bin/python3 ships
# with the Command Line Tools. Implemented in Python because macOS /bin/bash
# is 3.2 and lacks associative arrays needed for the dependency-closure BFS.
#
# Usage: bundle-dylibs.sh <path-to.app> [entitlements-path]

set -euo pipefail

APP="${1:?usage: bundle-dylibs.sh <path-to.app> [entitlements]}"
ENTITLEMENTS="${2:-MacEverything/MacEverything.entitlements}"

exec /usr/bin/python3 - "$APP" "$ENTITLEMENTS" <<'PYEOF'
import os, re, shutil, subprocess, sys

app = sys.argv[1]
entitlements = sys.argv[2] if len(sys.argv) > 2 else ""

if not os.path.isdir(app):
    sys.exit(f"error: app bundle not found: {app}")

macos_dir = os.path.join(app, "Contents", "MacOS")
frameworks = os.path.join(app, "Contents", "Frameworks")

EXTERNAL_PREFIXES = ("/opt/homebrew/", "/usr/local/", "/opt/local/")

def is_external(path):
    return path.startswith(EXTERNAL_PREFIXES)

def run(cmd):
    subprocess.run(cmd, check=False, stderr=subprocess.DEVNULL)

def is_macho(path):
    try:
        out = subprocess.check_output(["file", path], text=True)
    except subprocess.CalledProcessError:
        return False
    return "Mach-O" in out

def direct_deps(path):
    """All dependency install-names (excluding the install-id line)."""
    try:
        out = subprocess.check_output(["otool", "-L", path], text=True)
    except subprocess.CalledProcessError:
        return []
    deps = []
    for line in out.splitlines()[1:]:
        m = re.match(r"\s+(\S+)", line)
        if m:
            deps.append(m.group(1))
    return deps

print(f"=== bundle-dylibs: {app} ===")
os.makedirs(frameworks, exist_ok=True)

# Seed BFS with every Mach-O binary in Contents/MacOS.
seeds = []
for name in sorted(os.listdir(macos_dir)):
    p = os.path.join(macos_dir, name)
    if os.path.isfile(p) and is_macho(p):
        seeds.append(p)

# 1. BFS the external dylib closure. closure: realpath -> basename.
seen = set()
closure = {}
queue = list(seeds)
while queue:
    cur = queue.pop(0)
    for dep in direct_deps(cur):
        if not is_external(dep):
            continue
        real = os.path.realpath(dep)
        if not os.path.isfile(real):
            print(f"warn: missing dep {dep}", file=sys.stderr)
            continue
        if real not in seen:
            seen.add(real)
            closure[real] = os.path.basename(dep)
            queue.append(real)

print(f"external dylibs in closure: {len(closure)}")

# Guard against basename collisions (would clobber on copy).
basenames = {}
for real, base in closure.items():
    basenames.setdefault(base, []).append(real)
for base, reals in basenames.items():
    if len(reals) > 1:
        sys.exit(f"error: basename collision for {base}: {reals}")

# 2. Copy each external dylib into Frameworks/.
for real, base in closure.items():
    dest = os.path.join(frameworks, base)
    shutil.copyfile(real, dest)
    os.chmod(dest, 0o644)

# 3. Rewrite install names.
def rewrite_refs(target):
    for dep in direct_deps(target):
        if is_external(dep):
            depbase = os.path.basename(dep)
            run(["install_name_tool", "-change", dep, f"@rpath/{depbase}", target])

for real, base in closure.items():
    dest = os.path.join(frameworks, base)
    run(["install_name_tool", "-id", f"@rpath/{base}", dest])
    rewrite_refs(dest)

for seed in seeds:
    rewrite_refs(seed)

# 4. Ensure each app binary carries the Frameworks rpath.
def has_frameworks_rpath(path):
    try:
        out = subprocess.check_output(["otool", "-l", path], text=True)
    except subprocess.CalledProcessError:
        return False
    return "@executable_path/../Frameworks" in out

for seed in seeds:
    if not has_frameworks_rpath(seed):
        run(["install_name_tool", "-add_rpath",
             "@executable_path/../Frameworks", seed])

# 5. Re-sign: bundled dylibs first, then the whole app (deep, ad-hoc).
#    Mutating install names invalidates prior signatures, so this is required.
for real, base in closure.items():
    run(["codesign", "--force", "--sign", "-", "--timestamp=none",
         os.path.join(frameworks, base)])

ent = entitlements if entitlements and os.path.isfile(entitlements) else ""
if ent:
    r = subprocess.run(["codesign", "--force", "--deep", "--sign", "-",
                        "--entitlements", ent, app], check=False)
    if r.returncode != 0:
        subprocess.run(["codesign", "--force", "--deep", "--sign", "-", app],
                       check=True)
else:
    subprocess.run(["codesign", "--force", "--deep", "--sign", "-", app],
                   check=True)

print(f"=== bundle-dylibs: done ({len(closure)} dylibs bundled) ===")
PYEOF

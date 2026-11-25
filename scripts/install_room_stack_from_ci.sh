#!/usr/bin/env bash
# Install Door / Qucs-S / Xschem / KLayout(+mroom) from the latest successful
# GitHub Actions artifacts for this Linux distro.
#
# Usage (interactive):
#   bash scripts/install_room_stack_from_ci.sh
#
# Non-interactive / CI-ish:
#   DOOR_PREFIX=/opt/door KLAYOUT_PREFIX=/opt/klayout \
#   QUCS_PREFIX=/opt/qucs-s XSCHEM_PREFIX=/opt/xschem \
#   BIN_DIR=/usr/local/bin bash scripts/install_room_stack_from_ci.sh --yes
#
# Options:
#   --yes          no prompts (use env PREFIX_* / defaults)
#   --wait         if CI is running, wait until it finishes (max WAIT_MAX_SEC)
#   --force-ci     install even if a CI run is in progress
#   --flavor F     force artifact flavor: ubuntu24 | rocky8
#
# Requires: gh (authenticated), curl/python3, tar, unzip-capable python.
# PAT needs actions:read on the artifact repos (classic public_repo is enough
# for these public repos).
set -euo pipefail

YES=0
WAIT_CI=0
FORCE_CI=0
FLAVOR_FORCE=""
WAIT_MAX_SEC="${WAIT_MAX_SEC:-3600}"
POLL_SEC="${POLL_SEC:-30}"

REPO_DOOR="${REPO_DOOR:-IHP-GmbH/Door}"
REPO_QUCS="${REPO_QUCS:-adatsuk/Qucs-S-roomdb}"
REPO_XSCHEM="${REPO_XSCHEM:-adatsuk/XSchem-roomdb}"
REPO_KLAYOUT="${REPO_KLAYOUT:-adatsuk/KLayout-roomdb}"

WORKFLOW_DOOR="${WORKFLOW_DOOR:-Build DOOR}"
WORKFLOW_QUCS="${WORKFLOW_QUCS:-CI}"
WORKFLOW_XSCHEM="${WORKFLOW_XSCHEM:-CI}"
WORKFLOW_KLAYOUT="${WORKFLOW_KLAYOUT:-CI}"

usage() {
  sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -y|--yes) YES=1 ;;
    --wait) WAIT_CI=1 ;;
    --force-ci) FORCE_CI=1 ;;
    --flavor) FLAVOR_FORCE="${2:-}"; shift ;;
    -h|--help) usage ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

die() { echo "ERROR: $*" >&2; exit 1; }
info() { echo "==> $*"; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing dependency: $1"; }

need gh
need python3
need tar

detect_flavor() {
  if [[ -n "$FLAVOR_FORCE" ]]; then
    echo "$FLAVOR_FORCE"
    return
  fi
  local id="" ver=""
  if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    id="${ID:-}"
    ver="${VERSION_ID:-}"
  fi
  case "$id" in
    ubuntu|linuxmint|pop)
      case "$ver" in
        24.*|25.*) echo ubuntu24 ;;
        22.*|20.*)
          echo "Host is $id $ver — using rocky8/rhel8 bundles (older glibc)." >&2
          echo rocky8
          ;;
        *)
          echo "Unknown Ubuntu-like $ver — defaulting to ubuntu24." >&2
          echo ubuntu24
          ;;
      esac
      ;;
    rocky|almalinux|rhel|centos|ol)
      echo rocky8
      ;;
    *)
      echo "Unknown distro '${id:-?}' ${ver} — defaulting to ubuntu24." >&2
      echo ubuntu24
      ;;
  esac
}

FLAVOR="$(detect_flavor)"
info "Detected install flavor: $FLAVOR"

# artifact name per tool / flavor
artifact_for() {
  local tool="$1"
  case "$tool/$FLAVOR" in
    door/ubuntu24)    echo door-linux-ubuntu24 ;;
    door/rocky8)      echo door-rhel8 ;;
    qucs/ubuntu24)    echo qucs-s-ubuntu24 ;;
    qucs/rocky8)      echo qucs-s-rocky8 ;;
    xschem/ubuntu24)  echo xschem-ubuntu24 ;;
    xschem/rocky8)    echo xschem-rocky8 ;;
    klayout/ubuntu24) echo klayout-linux-ubuntu24 ;;
    klayout/rocky8)   echo klayout-rocky8 ;;
    *) die "no artifact mapping for $tool / $FLAVOR" ;;
  esac
}

repo_for() {
  case "$1" in
    door) echo "$REPO_DOOR" ;;
    qucs) echo "$REPO_QUCS" ;;
    xschem) echo "$REPO_XSCHEM" ;;
    klayout) echo "$REPO_KLAYOUT" ;;
  esac
}

workflow_for() {
  case "$1" in
    door) echo "$WORKFLOW_DOOR" ;;
    qucs) echo "$WORKFLOW_QUCS" ;;
    xschem) echo "$WORKFLOW_XSCHEM" ;;
    klayout) echo "$WORKFLOW_KLAYOUT" ;;
  esac
}

prompt_dir() {
  local label="$1" default="$2" out
  if [[ "$YES" -eq 1 ]]; then
    echo "$default"
    return
  fi
  read -r -p "$label [$default]: " out || true
  if [[ -z "${out// }" ]]; then
    echo "$default"
  else
    echo "$out"
  fi
}

# Defaults: portable trees under /usr/local/opt + wrappers in /usr/local/bin
DEFAULT_ROOT="${DEFAULT_ROOT:-/usr/local/opt}"
DEFAULT_BIN="${DEFAULT_BIN:-/usr/local/bin}"

DOOR_PREFIX="${DOOR_PREFIX:-$(prompt_dir "Door install directory" "$DEFAULT_ROOT/door")}"
QUCS_PREFIX="${QUCS_PREFIX:-$(prompt_dir "Qucs-S install directory" "$DEFAULT_ROOT/qucs-s")}"
XSCHEM_PREFIX="${XSCHEM_PREFIX:-$(prompt_dir "Xschem install directory" "$DEFAULT_ROOT/xschem")}"
KLAYOUT_PREFIX="${KLAYOUT_PREFIX:-$(prompt_dir "KLayout install directory" "$DEFAULT_ROOT/klayout")}"
BIN_DIR="${BIN_DIR:-$(prompt_dir "Symlink directory (PATH)" "$DEFAULT_BIN")}"

info "Door     -> $DOOR_PREFIX"
info "Qucs-S   -> $QUCS_PREFIX"
info "Xschem   -> $XSCHEM_PREFIX"
info "KLayout  -> $KLAYOUT_PREFIX"
info "PATH bin -> $BIN_DIR"

ci_busy_repos() {
  local tool repo wf json
  for tool in door qucs xschem klayout; do
    repo="$(repo_for "$tool")"
    wf="$(workflow_for "$tool")"
    json="$(gh run list -R "$repo" --workflow "$wf" --branch main --limit 15 \
      --json status,conclusion,databaseId,displayTitle 2>/dev/null || true)"
    python3 - "$tool" "$repo" "$json" <<'PY'
import json, sys
tool, repo, raw = sys.argv[1], sys.argv[2], sys.argv[3]
if not raw.strip():
    sys.exit(0)
try:
    runs = json.loads(raw)
except Exception:
    sys.exit(0)
busy = [r for r in runs if r.get("status") in ("queued", "in_progress", "waiting", "pending")]
if busy:
    r = busy[0]
    print(f"{tool}\t{repo}\t{r.get('status')}\t{r.get('databaseId')}\t{r.get('displayTitle','')}")
PY
  done
}

wait_for_ci() {
  local elapsed=0 lines
  while true; do
    lines="$(ci_busy_repos || true)"
    if [[ -z "${lines// }" ]]; then
      info "No running CI on Door/Qucs/Xschem/KLayout main."
      return 0
    fi
    if [[ "$FORCE_CI" -eq 1 ]]; then
      echo "WARNING: CI still running (continuing because --force-ci):" >&2
      echo "$lines" >&2
      return 0
    fi
    if [[ "$WAIT_CI" -ne 1 ]]; then
      echo "CI is currently running — refuse to install stale/partial builds:" >&2
      echo "$lines" >&2
      echo "Re-run with --wait, or --force-ci to override." >&2
      exit 3
    fi
    if [[ "$elapsed" -ge "$WAIT_MAX_SEC" ]]; then
      die "timed out waiting for CI (${WAIT_MAX_SEC}s)"
    fi
    info "CI busy; waiting ${POLL_SEC}s …"
    echo "$lines"
    sleep "$POLL_SEC"
    elapsed=$((elapsed + POLL_SEC))
  done
}

download_artifact() {
  local repo="$1" artifact="$2" out_dir="$3"
  local stage art_json meta zip tar_path
  mkdir -p "$out_dir"
  stage="$out_dir/.download"
  rm -rf "$stage"
  mkdir -p "$stage"

  info "Fetching artifact '$artifact' from $repo"
  art_json="$stage/artifacts.json"
  gh api "repos/$repo/actions/artifacts?name=$artifact&per_page=20" >"$art_json" \
    || die "cannot list artifacts on $repo (need PAT with actions:read)"

  meta="$stage/meta.env"
  python3 - "$art_json" "$meta" <<'PY'
import json, sys
data = json.load(open(sys.argv[1], encoding="utf-8"))
for a in data.get("artifacts", []):
    if a.get("expired"):
        continue
    art_id = a.get("id")
    if not art_id:
        continue
    run = (a.get("workflow_run") or {}).get("id") or ""
    created = a.get("created_at") or ""
    with open(sys.argv[2], "w", encoding="utf-8") as out:
        out.write(f"ART_ID={art_id}\nCREATED={created}\nRUN_HINT={run}\n")
    sys.exit(0)
sys.exit(2)
PY
  # shellcheck disable=SC1090
  source "$meta"
  [[ -n "${ART_ID:-}" ]] || die "no non-expired artifact '$artifact' on $repo"

  zip="$stage/bundle.zip"
  gh api -H "Accept: application/vnd.github+json" \
    "/repos/$repo/actions/artifacts/$ART_ID/zip" >"$zip"

  python3 - "$zip" "$stage" <<'PY'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
z.extractall(sys.argv[2])
print("zip entries:", ", ".join(z.namelist()[:12]))
PY

  tar_path="$(find "$stage" -type f \( -name '*.tar.gz' -o -name '*.tgz' \) | head -n1 || true)"
  [[ -n "$tar_path" ]] || die "no .tar.gz inside artifact zip for $artifact"

  # Clear previous install contents (keep nothing stale).
  find "$out_dir" -mindepth 1 -maxdepth 1 ! -name '.download' -exec rm -rf {} +
  tar -xzf "$tar_path" -C "$out_dir"
  rm -rf "$stage"

  {
    echo "repo=$repo"
    echo "artifact=$artifact"
    echo "artifact_id=$ART_ID"
    echo "created_at=$CREATED"
    echo "workflow_run=$RUN_HINT"
  } >"$out_dir/BUNDLE_SOURCE.txt"
}

find_exec() {
  local root="$1"
  shift
  local name
  for name in "$@"; do
    local hit
    hit="$(find "$root" -type f -name "$name" 2>/dev/null | head -n1 || true)"
    if [[ -n "$hit" && -x "$hit" ]]; then
      echo "$hit"
      return 0
    fi
    hit="$(find "$root" -type f -name "$name" 2>/dev/null | head -n1 || true)"
    if [[ -n "$hit" ]]; then
      chmod +x "$hit" 2>/dev/null || true
      echo "$hit"
      return 0
    fi
  done
  return 1
}

link_into_bin() {
  local target="$1" link_name="$2"
  mkdir -p "$BIN_DIR"
  ln -sfn "$target" "$BIN_DIR/$link_name"
  info "symlink $BIN_DIR/$link_name -> $target"
}

install_one() {
  local tool="$1" prefix="$2"
  local repo artifact exe
  repo="$(repo_for "$tool")"
  artifact="$(artifact_for "$tool")"
  download_artifact "$repo" "$artifact" "$prefix"

  case "$tool" in
    door)
      exe="$(find_exec "$prefix" door-run.sh door)" \
        || die "door binary not found under $prefix"
      link_into_bin "$exe" door
      ;;
    qucs)
      exe="$(find_exec "$prefix" qucs-s-run.sh qucs-s qucs)" \
        || die "qucs-s binary not found under $prefix"
      link_into_bin "$exe" qucs-s
      ;;
    xschem)
      exe="$(find_exec "$prefix" xschem-run.sh xschem)" \
        || die "xschem binary not found under $prefix"
      link_into_bin "$exe" xschem
      ;;
    klayout)
      exe="$(find_exec "$prefix" klayout-run.sh klayout)" \
        || die "klayout binary not found under $prefix"
      link_into_bin "$exe" klayout
      ;;
  esac
}

wait_for_ci

# May need sudo for /usr/local
ensure_writable() {
  local d="$1"
  mkdir -p "$d" 2>/dev/null && [[ -w "$d" ]] && return 0
  if [[ "$(id -u)" -ne 0 ]]; then
    info "Need root to write $d — re-run prefix of this script with sudo, or choose a home path."
    die "cannot write $d"
  fi
  mkdir -p "$d"
}

ensure_writable "$DOOR_PREFIX"
ensure_writable "$QUCS_PREFIX"
ensure_writable "$XSCHEM_PREFIX"
ensure_writable "$KLAYOUT_PREFIX"
ensure_writable "$BIN_DIR"

install_one door "$DOOR_PREFIX"
install_one qucs "$QUCS_PREFIX"
install_one xschem "$XSCHEM_PREFIX"
install_one klayout "$KLAYOUT_PREFIX"

info "Done."
echo
echo "Flavor:  $FLAVOR"
echo "PATH:    export PATH=\"$BIN_DIR:\$PATH\""
echo "Try:     door --help; qucs-s --version; xschem -v; klayout -v"
echo "Sources: $DOOR_PREFIX/BUNDLE_SOURCE.txt (and siblings)"

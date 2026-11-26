#!/usr/bin/env bash
# Install Door / Qucs-S / Xschem / KLayout(+mroom) from the latest successful
# GitHub Actions artifacts for this Linux distro.
#
# Usage (interactive):
#   bash scripts/install_room_stack_from_ci.sh
#
# Non-interactive:
#   DOOR_PREFIX=/opt/door KLAYOUT_PREFIX=/opt/klayout \
#   QUCS_PREFIX=/opt/qucs-s XSCHEM_PREFIX=/opt/xschem \
#   BIN_DIR=/usr/local/bin bash scripts/install_room_stack_from_ci.sh --yes
#
# Options:
#   --yes            no prompts (use env PREFIX_* / defaults; tools = all unless --tools)
#   --wait           if CI is running, wait until it finishes (max WAIT_MAX_SEC)
#   --force-ci       install even if a CI run is in progress
#   --flavor F       force artifact flavor: ubuntu24 | rocky8
#   --tools LIST     comma list: door,qucs,xschem,klayout (or 'all')
#                    Without --tools: interactive prompt (default all); with --yes: all
#   --dry-run        print plan / CI status; do not download or install
#   --self-test      run offline mapping assertions and exit
#   --from-tar FILE  install one tool from a local .tar.gz (requires --tools with one entry)
#
# No sudo required: missing gh is downloaded into GH_BOOTSTRAP_DIR (~/.local/share/room-stack).
# Auth: GH_TOKEN / GITHUB_TOKEN, else interactive PAT paste (classic public_repo is enough
# for public artifact repos; needs Actions read).
#
# Requires: python3, tar, and curl or wget (to bootstrap gh if needed).
set -euo pipefail

YES=0
WAIT_CI=0
FORCE_CI=0
DRY_RUN=0
SELF_TEST=0
FLAVOR_FORCE=""
TOOLS_CSV="all"
TOOLS_EXPLICIT=0
FROM_TAR=""
WAIT_MAX_SEC="${WAIT_MAX_SEC:-3600}"
POLL_SEC="${POLL_SEC:-30}"
ALL_TOOLS=(door qucs xschem klayout)
# Fallback gh release if api.github.com is rate-limited / unreachable.
GH_VERSION_FALLBACK="${GH_VERSION_FALLBACK:-2.82.1}"

REPO_DOOR="${REPO_DOOR:-IHP-GmbH/Door}"
REPO_QUCS="${REPO_QUCS:-adatsuk/Qucs-S-roomdb}"
REPO_XSCHEM="${REPO_XSCHEM:-adatsuk/XSchem-roomdb}"
REPO_KLAYOUT="${REPO_KLAYOUT:-adatsuk/KLayout-roomdb}"

WORKFLOW_DOOR="${WORKFLOW_DOOR:-Build DOOR}"
WORKFLOW_QUCS="${WORKFLOW_QUCS:-CI}"
WORKFLOW_XSCHEM="${WORKFLOW_XSCHEM:-CI}"
WORKFLOW_KLAYOUT="${WORKFLOW_KLAYOUT:-CI}"

usage() {
  sed -n '2,34p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -y|--yes) YES=1 ;;
    --wait) WAIT_CI=1 ;;
    --force-ci) FORCE_CI=1 ;;
    --dry-run) DRY_RUN=1 ;;
    --self-test) SELF_TEST=1 ;;
    --flavor) FLAVOR_FORCE="${2:-}"; shift ;;
    --tools) TOOLS_CSV="${2:-}"; TOOLS_EXPLICIT=1; shift ;;
    --from-tar) FROM_TAR="${2:-}"; shift ;;
    -h|--help) usage ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

die() { echo "ERROR: $*" >&2; exit 1; }
info() { echo "==> $*"; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing dependency: $1"; }

need python3
need tar

download_url() {
  local url="$1" out="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --retry 3 --retry-delay 1 -o "$out" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -q --tries=3 -O "$out" "$url"
  else
    die "need curl or wget to download (bootstrap gh / artifacts helpers)"
  fi
}

gh_linux_arch() {
  case "$(uname -m)" in
    x86_64|amd64) echo amd64 ;;
    aarch64|arm64) echo arm64 ;;
    *) die "unsupported CPU for gh bootstrap: $(uname -m) (need x86_64 or aarch64)" ;;
  esac
}

resolve_gh_version() {
  if [[ -n "${GH_VERSION:-}" ]]; then
    echo "${GH_VERSION#v}"
    return
  fi
  local tmp ver=""
  tmp="$(mktemp)"
  if download_url "https://api.github.com/repos/cli/cli/releases/latest" "$tmp" 2>/dev/null; then
    ver="$(python3 - "$tmp" <<'PY' 2>/dev/null || true
import json, sys
try:
    print(json.load(open(sys.argv[1], encoding="utf-8"))["tag_name"].lstrip("v"))
except Exception:
    pass
PY
)"
  fi
  rm -f "$tmp"
  if [[ -n "$ver" ]]; then
    echo "$ver"
  else
    echo "$GH_VERSION_FALLBACK"
  fi
}

# Install GitHub CLI into a user directory when system gh is missing (no sudo).
ensure_gh() {
  if command -v gh >/dev/null 2>&1; then
    info "Using gh: $(command -v gh)"
    return
  fi

  local root="${GH_BOOTSTRAP_DIR:-$HOME/.local/share/room-stack}"
  local bindir="$root/bin"
  mkdir -p "$bindir"

  if [[ -x "$bindir/gh" ]]; then
    export PATH="$bindir:$PATH"
    info "Using bootstrapped gh: $bindir/gh"
    return
  fi

  info "gh not on PATH — bootstrapping user-local copy into $bindir (no sudo)"
  local arch ver stage tarball url gh_bin
  arch="$(gh_linux_arch)"
  ver="$(resolve_gh_version)"
  stage="$root/gh-stage-$$"
  rm -rf "$stage"
  mkdir -p "$stage"
  tarball="$stage/gh.tgz"
  url="https://github.com/cli/cli/releases/download/v${ver}/gh_${ver}_linux_${arch}.tar.gz"
  info "Downloading $url"
  download_url "$url" "$tarball" || die "failed to download gh ${ver} (${arch})"
  tar -xzf "$tarball" -C "$stage"
  gh_bin="$(find "$stage" -type f -name gh | head -n1 || true)"
  [[ -n "$gh_bin" ]] || die "gh binary missing inside release tarball"
  cp "$gh_bin" "$bindir/gh"
  chmod 755 "$bindir/gh"
  rm -rf "$stage"
  export PATH="$bindir:$PATH"
  command -v gh >/dev/null 2>&1 || die "failed to put bootstrapped gh on PATH"
  info "Bootstrapped $($bindir/gh --version 2>/dev/null | head -n1)"
}

# Prefer env token; otherwise reuse gh login; otherwise prompt for a PAT.
ensure_gh_auth() {
  local token=""
  if [[ -n "${GH_TOKEN:-}" ]]; then
    token="$GH_TOKEN"
  elif [[ -n "${GITHUB_TOKEN:-}" ]]; then
    token="$GITHUB_TOKEN"
  elif [[ -n "${GH_TOKEN_FILE:-}" ]]; then
    [[ -r "$GH_TOKEN_FILE" ]] || die "GH_TOKEN_FILE not readable: $GH_TOKEN_FILE"
    token="$(tr -d '\r\n' <"$GH_TOKEN_FILE")"
  fi

  if [[ -n "$token" ]]; then
    export GH_TOKEN="$token"
    info "GitHub auth: using token from environment"
    return
  fi

  if gh auth status >/dev/null 2>&1; then
    info "GitHub auth: existing gh login OK"
    return
  fi

  if [[ "$YES" -eq 1 ]]; then
    die "GitHub auth required. Set GH_TOKEN (or GITHUB_TOKEN / GH_TOKEN_FILE), then re-run."
  fi

  echo
  info "GitHub PAT required to download Actions artifacts"
  echo "  Open this link to generate a classic token (public_repo pre-checked):"
  echo "  https://github.com/settings/tokens/new?scopes=public_repo&description=room-stack-ci-install"
  echo "  Click Generate token, copy the ghp_… value, paste below."
  echo "  (Fine-grained alternative: Actions=Read on Door/Qucs/Xschem/KLayout repos.)"
  echo "  Token stays in this shell only (GH_TOKEN); not written to disk."
  # -s hides input; works for PAT paste
  read -r -s -p "Paste PAT: " token || true
  echo
  token="${token#"${token%%[![:space:]]*}"}"
  token="${token%"${token##*[![:space:]]}"}"
  [[ -n "$token" ]] || die "empty PAT"
  export GH_TOKEN="$token"

  # Best-effort persistent login for later runs (ignore keyring failures).
  if printf '%s\n' "$GH_TOKEN" | gh auth login --hostname github.com --with-token >/dev/null 2>&1; then
    info "GitHub auth: saved via gh auth login"
  else
    info "GitHub auth: using GH_TOKEN for this run only"
  fi
}

# Prefer system prefixes when writable; otherwise fall back to $HOME (no sudo).
default_install_root() {
  if [[ -n "${DEFAULT_ROOT:-}" ]]; then
    echo "$DEFAULT_ROOT"
    return
  fi
  if { [[ -d /usr/local/opt ]] && [[ -w /usr/local/opt ]]; } || \
     { [[ ! -e /usr/local/opt ]] && [[ -d /usr/local ]] && [[ -w /usr/local ]]; }; then
    echo "/usr/local/opt"
  else
    echo "$HOME/.local/opt"
  fi
}

default_bin_dir() {
  if [[ -n "${DEFAULT_BIN:-}" ]]; then
    echo "$DEFAULT_BIN"
    return
  fi
  if { [[ -d /usr/local/bin ]] && [[ -w /usr/local/bin ]]; } || \
     { [[ ! -e /usr/local/bin ]] && [[ -d /usr/local ]] && [[ -w /usr/local ]]; }; then
    echo "/usr/local/bin"
  else
    echo "$HOME/.local/bin"
  fi
}

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

# artifact name per tool / flavor (FLAVOR must be set)
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
    *) die "unknown tool: $1" ;;
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

prefix_for() {
  case "$1" in
    door) echo "$DOOR_PREFIX" ;;
    qucs) echo "$QUCS_PREFIX" ;;
    xschem) echo "$XSCHEM_PREFIX" ;;
    klayout) echo "$KLAYOUT_PREFIX" ;;
  esac
}

parse_tools() {
  local raw="$1" t
  TOOLS=()
  # Accept commas and/or whitespace: "door qucs" / "door,qucs" / "all"
  raw="$(echo "$raw" | tr '[:upper:]' '[:lower:]' | tr ',;' '  ')"
  # shellcheck disable=SC2206
  local _parts=($raw)
  for t in "${_parts[@]}"; do
    t="${t//[[:space:]]/}"
    [[ -z "$t" ]] && continue
    case "$t" in
      all|'*')
        TOOLS=("${ALL_TOOLS[@]}")
        break
        ;;
      door|qucs|xschem|klayout) TOOLS+=("$t") ;;
      qucs-s) TOOLS+=("qucs") ;;
      *) die "unknown tool in --tools: $t (want: door,qucs,xschem,klayout,all)" ;;
    esac
  done
  # Deduplicate while preserving order
  if [[ ${#TOOLS[@]} -gt 0 ]]; then
    local -a uniq=()
    local x seen
    for t in "${TOOLS[@]}"; do
      seen=0
      for x in "${uniq[@]+"${uniq[@]}"}"; do
        [[ "$x" == "$t" ]] && { seen=1; break; }
      done
      [[ "$seen" -eq 0 ]] && uniq+=("$t")
    done
    TOOLS=("${uniq[@]}")
  fi
  [[ ${#TOOLS[@]} -gt 0 ]] || die "tools list is empty (use door,qucs,xschem,klayout or all)"
}

# Interactive: default = all; user may edit the list or answer y/n per tool.
prompt_tools_selection() {
  if [[ "$YES" -eq 1 || "$TOOLS_EXPLICIT" -eq 1 ]]; then
    return
  fi
  local default="door,qucs,xschem,klayout" ans mode
  echo
  info "Select tools to install (default: all)"
  echo "  Available: door, qucs, xschem, klayout"
  echo "  Enter a comma/space list, 'all', or 'ask' for per-tool y/n prompts."
  read -r -p "Tools [$default]: " ans || true
  ans="${ans#"${ans%%[![:space:]]*}"}"
  ans="${ans%"${ans##*[![:space:]]}"}"
  if [[ -z "$ans" ]]; then
    parse_tools "$default"
    return
  fi
  mode="$(echo "$ans" | tr '[:upper:]' '[:lower:]' | tr -d '[:space:]')"
  if [[ "$mode" == "ask" || "$mode" == "menu" ]]; then
    local selected=() t yn
    for t in "${ALL_TOOLS[@]}"; do
      read -r -p "  Install ${t}? [Y/n]: " yn || true
      yn="$(echo "${yn:-Y}" | tr '[:upper:]' '[:lower:]' | tr -d '[:space:]')"
      case "$yn" in
        ""|y|yes) selected+=("$t") ;;
        n|no) ;;
        *) die "expected y/n for $t, got: $yn" ;;
      esac
    done
    [[ ${#selected[@]} -gt 0 ]] || die "no tools selected"
    TOOLS=("${selected[@]}")
    return
  fi
  parse_tools "$ans"
}

run_self_test() {
  info "Running offline self-test"
  local fail=0
  assert_eq() {
    local got="$1" want="$2" msg="$3"
    if [[ "$got" != "$want" ]]; then
      echo "FAIL: $msg (got='$got' want='$want')" >&2
      fail=1
    else
      echo "OK: $msg"
    fi
  }

  FLAVOR_FORCE=ubuntu24
  FLAVOR="$(detect_flavor)"
  assert_eq "$FLAVOR" ubuntu24 "force ubuntu24"
  assert_eq "$(artifact_for door)" door-linux-ubuntu24 "door ubuntu24 artifact"
  assert_eq "$(artifact_for qucs)" qucs-s-ubuntu24 "qucs ubuntu24 artifact"
  assert_eq "$(artifact_for xschem)" xschem-ubuntu24 "xschem ubuntu24 artifact"
  assert_eq "$(artifact_for klayout)" klayout-linux-ubuntu24 "klayout ubuntu24 artifact"

  FLAVOR_FORCE=rocky8
  FLAVOR="$(detect_flavor)"
  assert_eq "$FLAVOR" rocky8 "force rocky8"
  assert_eq "$(artifact_for door)" door-rhel8 "door rocky8 artifact"
  assert_eq "$(artifact_for qucs)" qucs-s-rocky8 "qucs rocky8 artifact"
  assert_eq "$(artifact_for xschem)" xschem-rocky8 "xschem rocky8 artifact"
  assert_eq "$(artifact_for klayout)" klayout-rocky8 "klayout rocky8 artifact"

  assert_eq "$(repo_for door)" "$REPO_DOOR" "door repo"
  assert_eq "$(repo_for klayout)" "$REPO_KLAYOUT" "klayout repo"

  parse_tools "all"
  assert_eq "${#TOOLS[@]}" "4" "parse all -> 4 tools"
  assert_eq "${TOOLS[*]}" "door qucs xschem klayout" "parse all order"
  parse_tools "door, qucs-s"
  assert_eq "${TOOLS[*]}" "door qucs" "parse subset + qucs-s alias"
  parse_tools "klayout door door"
  assert_eq "${TOOLS[*]}" "klayout door" "dedupe preserves first order"

  case "$(uname -m)" in
    x86_64|amd64|aarch64|arm64)
      _arch="$(gh_linux_arch)"
      assert_eq "$_arch" "$_arch" "gh_linux_arch resolves ($_arch)"
      ;;
    *)
      echo "SKIP: gh_linux_arch on $(uname -m)"
      ;;
  esac

  bash -n "$0" || { echo "FAIL: bash -n" >&2; fail=1; }
  [[ "$fail" -eq 0 ]] || die "self-test failed"
  info "Self-test passed"
  exit 0
}

if [[ "$SELF_TEST" -eq 1 ]]; then
  # FLAVOR unused until assertions set FLAVOR_FORCE
  FLAVOR=ubuntu24
  run_self_test
fi

ensure_gh
# --from-tar with a local file still uses gh for CI-busy checks unless dry-run exits earlier
ensure_gh_auth
parse_tools "$TOOLS_CSV"
prompt_tools_selection
FLAVOR="$(detect_flavor)"
info "Detected install flavor: $FLAVOR"
info "Tools: ${TOOLS[*]}"

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

DEFAULT_ROOT="$(default_install_root)"
DEFAULT_BIN="$(default_bin_dir)"
info "Default prefixes under: $DEFAULT_ROOT (bin: $DEFAULT_BIN)"

want_tool() {
  local t="$1" x
  for x in "${TOOLS[@]}"; do
    [[ "$x" == "$t" ]] && return 0
  done
  return 1
}

DOOR_PREFIX="${DOOR_PREFIX:-}"
QUCS_PREFIX="${QUCS_PREFIX:-}"
XSCHEM_PREFIX="${XSCHEM_PREFIX:-}"
KLAYOUT_PREFIX="${KLAYOUT_PREFIX:-}"
BIN_DIR="${BIN_DIR:-}"

if want_tool door; then
  DOOR_PREFIX="${DOOR_PREFIX:-$(prompt_dir "Door install directory" "$DEFAULT_ROOT/door")}"
fi
if want_tool qucs; then
  QUCS_PREFIX="${QUCS_PREFIX:-$(prompt_dir "Qucs-S install directory" "$DEFAULT_ROOT/qucs-s")}"
fi
if want_tool xschem; then
  XSCHEM_PREFIX="${XSCHEM_PREFIX:-$(prompt_dir "Xschem install directory" "$DEFAULT_ROOT/xschem")}"
fi
if want_tool klayout; then
  KLAYOUT_PREFIX="${KLAYOUT_PREFIX:-$(prompt_dir "KLayout install directory" "$DEFAULT_ROOT/klayout")}"
fi
BIN_DIR="${BIN_DIR:-$(prompt_dir "Symlink directory (PATH)" "$DEFAULT_BIN")}"

for t in "${TOOLS[@]}"; do
  info "$t -> $(prefix_for "$t")"
done
info "PATH bin -> $BIN_DIR"

if [[ -n "$FROM_TAR" ]]; then
  [[ ${#TOOLS[@]} -eq 1 ]] || die "--from-tar requires exactly one tool in --tools"
  [[ -f "$FROM_TAR" ]] || die "--from-tar file not found: $FROM_TAR"
fi

ci_busy_repos() {
  local tool repo wf json
  for tool in "${TOOLS[@]}"; do
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
      info "No running CI for selected tools on main."
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

install_from_tar() {
  local out_dir="$1" tar_path="$2"
  mkdir -p "$out_dir"
  find "$out_dir" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
  tar -xzf "$tar_path" -C "$out_dir"
  {
    echo "source=local-tar"
    echo "tar=$tar_path"
  } >"$out_dir/BUNDLE_SOURCE.txt"
}

find_exec() {
  local root="$1"
  shift
  local name hit
  for name in "$@"; do
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
  if [[ -n "$FROM_TAR" ]]; then
    install_from_tar "$prefix" "$FROM_TAR"
  else
    repo="$(repo_for "$tool")"
    artifact="$(artifact_for "$tool")"
    download_artifact "$repo" "$artifact" "$prefix"
  fi

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

if [[ "$DRY_RUN" -eq 1 ]]; then
  info "Dry-run plan:"
  for t in "${TOOLS[@]}"; do
    echo "  $t: repo=$(repo_for "$t") artifact=$(artifact_for "$t") prefix=$(prefix_for "$t")"
  done
  echo "  BIN_DIR=$BIN_DIR"
  if [[ -n "$FROM_TAR" ]]; then
    echo "  FROM_TAR=$FROM_TAR"
  fi
  lines="$(ci_busy_repos || true)"
  if [[ -n "${lines// }" ]]; then
    echo "CI busy:"
    echo "$lines"
  else
    echo "CI busy: (none for selected tools)"
  fi
  info "Dry-run OK"
  exit 0
fi

wait_for_ci

ensure_writable() {
  local d="$1"
  mkdir -p "$d" 2>/dev/null && [[ -w "$d" ]] && return 0
  if [[ "$(id -u)" -ne 0 ]]; then
    info "Need root to write $d — choose a home path or re-run with sudo."
    die "cannot write $d"
  fi
  mkdir -p "$d"
}

for t in "${TOOLS[@]}"; do
  ensure_writable "$(prefix_for "$t")"
done
ensure_writable "$BIN_DIR"

for t in "${TOOLS[@]}"; do
  install_one "$t" "$(prefix_for "$t")"
done

info "Done."
echo
echo "Flavor:  $FLAVOR"
echo "PATH:    export PATH=\"$BIN_DIR:\$PATH\""
echo "Tools:   ${TOOLS[*]}"

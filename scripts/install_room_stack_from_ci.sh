#!/usr/bin/env bash
# Install Door / Qucs-S / Xschem / KLayout(+mroom) / EMStudio from the latest
# successful GitHub Actions artifacts for this Linux distro.
#
# Layout (native-style, one prefix — tools do NOT wipe each other):
#   <prefix>/bin/<tool>              -> symlinks on PATH
#   <prefix>/lib/room-stack/<tool>/  -> unpacked CI bundles
#
# Usage (interactive):
#   bash scripts/install_room_stack_from_ci.sh
#
# Non-interactive:
#   ./install_room_stack_from_ci.sh --yes --prefix "$HOME/door"
#   # or per-tool overrides:
#   DOOR_PREFIX=/tmp/door/lib/room-stack/door BIN_DIR=/tmp/door/bin \
#     ./install_room_stack_from_ci.sh --yes --tools door
#
# Options:
#   --yes            no prompts (use env / defaults; tools = all unless --tools)
#   --prefix DIR     shared install root (bin + lib/room-stack); default ~/.local or /usr/local
#   --wait           if CI is running, wait until it finishes (max WAIT_MAX_SEC)
#   --force-ci       install even if a CI run is in progress
#   --flavor F       force artifact flavor: ubuntu24 | rocky8
#   --tools LIST     door,qucs,xschem,klayout,emstudio (or 'all')
#                    Without --tools: interactive prompt (default all); with --yes: all
#   --update         only check/install updates for tools already under --prefix
#                    (or only --tools if given). Skips tools whose CI artifact_id matches
#                    BUNDLE_SOURCE.txt. Implies no interactive tool menu.
#   --no-seed-settings  do not write Door QSettings (~/.config/DOOR.conf) defaults
#   --seed-settings     force writing Door tool paths even if settings already exist
#   --dry-run        print plan / CI status; do not download or install
#   --self-test      run offline mapping assertions and exit
#   --from-tar FILE  install one tool from a local .tar.gz (requires --tools with one entry)
#   --forget-token   delete the encrypted PAT store and exit
#
# No sudo required: missing gh is downloaded into GH_BOOTSTRAP_DIR (~/.local/share/room-stack).
# Auth order: GH_TOKEN / GITHUB_TOKEN / GH_TOKEN_FILE → gh login → encrypted store
# (~/.config/room-stack/github_token.enc, AES-256 + passphrase) → interactive PAT.
# Classic public_repo is enough for public artifact repos.
#
# Requires: python3, tar, curl|wget (gh bootstrap); openssl (to save/load encrypted PAT).
set -euo pipefail

YES=0
WAIT_CI=0
FORCE_CI=0
DRY_RUN=0
SELF_TEST=0
FORGET_TOKEN=0
UPDATE_MODE=0
SEED_SETTINGS=1
FORCE_SEED_SETTINGS=0
FLAVOR_FORCE=""
TOOLS_CSV="all"
TOOLS_EXPLICIT=0
FROM_TAR=""
PREFIX_OPT=""
WAIT_MAX_SEC="${WAIT_MAX_SEC:-3600}"
POLL_SEC="${POLL_SEC:-30}"
ALL_TOOLS=(door qucs xschem klayout emstudio)
# Fallback gh release if api.github.com is rate-limited / unreachable.
GH_VERSION_FALLBACK="${GH_VERSION_FALLBACK:-2.82.1}"
TOKEN_STORE_DIR="${ROOM_STACK_CONFIG_DIR:-$HOME/.config/room-stack}"
TOKEN_STORE_FILE="${ROOM_STACK_TOKEN_FILE:-$TOKEN_STORE_DIR/github_token.enc}"

REPO_DOOR="${REPO_DOOR:-IHP-GmbH/Door}"
REPO_QUCS="${REPO_QUCS:-adatsuk/Qucs-S-roomdb}"
REPO_XSCHEM="${REPO_XSCHEM:-adatsuk/XSchem-roomdb}"
REPO_KLAYOUT="${REPO_KLAYOUT:-adatsuk/KLayout-roomdb}"
REPO_EMSTUDIO="${REPO_EMSTUDIO:-IHP-GmbH/EMStudio}"
REPO_ROOM="${REPO_ROOM:-IHP-GmbH/Room}"

WORKFLOW_DOOR="${WORKFLOW_DOOR:-Build DOOR}"
WORKFLOW_QUCS="${WORKFLOW_QUCS:-CI}"
WORKFLOW_XSCHEM="${WORKFLOW_XSCHEM:-CI}"
WORKFLOW_KLAYOUT="${WORKFLOW_KLAYOUT:-CI}"
WORKFLOW_EMSTUDIO="${WORKFLOW_EMSTUDIO:-Build EMStudio}"

usage() {
  sed -n '2,48p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -y|--yes) YES=1 ;;
    --wait) WAIT_CI=1 ;;
    --force-ci) FORCE_CI=1 ;;
    --dry-run) DRY_RUN=1 ;;
    --self-test) SELF_TEST=1 ;;
    --forget-token) FORGET_TOKEN=1 ;;
    --update) UPDATE_MODE=1 ;;
    --no-seed-settings) SEED_SETTINGS=0 ;;
    --seed-settings) SEED_SETTINGS=1; FORCE_SEED_SETTINGS=1 ;;
    --flavor) FLAVOR_FORCE="${2:-}"; shift ;;
    --prefix) PREFIX_OPT="${2:-}"; shift ;;
    --tools) TOOLS_CSV="${2:-}"; TOOLS_EXPLICIT=1; shift ;;
    --from-tar) FROM_TAR="${2:-}"; shift ;;
    -h|--help) usage ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

die() { echo "ERROR: $*" >&2; exit 1; }
# Always stderr — callers capture stdout from helpers (e.g. token="$(load_…)").
info() { echo "==> $*" >&2; }
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

read_secret() {
  local prompt="$1" value=""
  read -r -s -p "$prompt" value || true
  echo >&2
  # Drop CR from Windows/paste; Enter itself is not part of $value with read -s.
  value="${value//$'\r'/}"
  value="${value#"${value%%[![:space:]]*}"}"
  value="${value%"${value##*[![:space:]]}"}"
  printf '%s' "$value"
}

# Strip CR/LF/BOM/control chars that break HTTP Authorization headers.
sanitize_gh_token() {
  # Prefer python for reliable binary-safe cleanup (openssl decrypt may keep \r).
  ROOM_STACK_RAW_TOKEN="$1" python3 -c 'import os,sys
b=os.environ.get("ROOM_STACK_RAW_TOKEN","").encode("utf-8", "surrogateescape")
if b.startswith(b"\xef\xbb\xbf"):
    b=b[3:]
b=bytes(ch for ch in b if 32 <= ch < 127)
sys.stdout.buffer.write(b.strip())
' 2>/dev/null || printf '%s' "$1" | tr -d '\r\n\000' | tr -d '[:space:]'
}

validate_gh_token_shape() {
  local token="$1"
  case "$token" in
    ghp_*|gho_*|ghu_*|ghs_*|ghr_*|github_pat_*)
      [[ ${#token} -ge 20 ]] || return 1
      return 0
      ;;
    *) return 1 ;;
  esac
}

# Probe that works for classic/fine-grained PATs *and* Actions GITHUB_TOKEN
# (installation tokens often 403 on GET /user).
gh_token_works() {
  gh api rate_limit -q .rate.limit >/dev/null 2>&1
}

gh_token_identity() {
  local login
  login="$(gh api user -q .login 2>/dev/null || true)"
  if [[ -n "$login" ]]; then
    printf '%s' "$login"
    return 0
  fi
  printf '%s' "token"
}

# Ensure GH_TOKEN is clean and accepted by api.github.com before downloads.
apply_gh_token() {
  local raw="$1" source_msg="$2" token
  token="$(sanitize_gh_token "$raw")"
  if [[ -z "$token" ]]; then
    die "GitHub token empty after sanitize ($source_msg). Re-run: $0 --forget-token"
  fi
  if ! validate_gh_token_shape "$token"; then
    die "GitHub token looks invalid ($source_msg). Expected ghp_… / github_pat_… / ghs_…. Re-run: $0 --forget-token"
  fi
  export GH_TOKEN="$token"
  # Prefer GH_TOKEN; keep GITHUB_TOKEN only if it was the source (Actions sets both aliases).
  if [[ "$source_msg" != *GITHUB_TOKEN* ]]; then
    unset GITHUB_TOKEN 2>/dev/null || true
  else
    export GITHUB_TOKEN="$token"
  fi
  if ! gh_token_works; then
    unset GH_TOKEN
    die "GitHub rejected the token ($source_msg). Check scopes (public_repo / Actions:Read) or run: $0 --forget-token"
  fi
  info "GitHub auth OK ($source_msg) as $(gh_token_identity)"
}

# AES-256-CBC + PBKDF2 via openssl; passphrase never stored.
# Use -pass env:… so '$' / '!' in the passphrase cannot be expanded by the shell.
encrypt_token_to_store() {
  local token="$1" pass="$2"
  token="$(sanitize_gh_token "$token")"
  command -v openssl >/dev/null 2>&1 || die "openssl required to encrypt the PAT store"
  mkdir -p "$(dirname "$TOKEN_STORE_FILE")"
  chmod 700 "$(dirname "$TOKEN_STORE_FILE")" 2>/dev/null || true
  local tmp plain
  tmp="$(mktemp)"
  plain="$(mktemp)"
  umask 077
  # Write plaintext token to a temp file (env can mangle exotic bytes on some hosts).
  if ! ROOM_STACK_RAW_TOKEN="$token" python3 -c \
      'import os,sys; sys.stdout.buffer.write(os.environ["ROOM_STACK_RAW_TOKEN"].encode())' \
      >"$plain"; then
    rm -f "$tmp" "$plain"
    die "failed to stage PAT for encryption"
  fi
  if ! ROOM_STACK_PASS="$pass" openssl enc -aes-256-cbc -pbkdf2 -iter 200000 -salt \
      -pass env:ROOM_STACK_PASS -in "$plain" -out "$tmp" 2>/dev/null; then
    rm -f "$tmp" "$plain"
    die "failed to encrypt PAT (openssl enc)"
  fi
  rm -f "$plain"
  mv -f "$tmp" "$TOKEN_STORE_FILE"
  chmod 600 "$TOKEN_STORE_FILE" 2>/dev/null || true
}

decrypt_token_from_store() {
  local pass="$1" plain out=""
  command -v openssl >/dev/null 2>&1 || return 1
  [[ -r "$TOKEN_STORE_FILE" ]] || return 1
  plain="$(mktemp)"
  # Decrypt to a file so we can check openssl's exit status (pipelines + bad
  # passphrase can still emit printable garbage on stdout).
  if ! ROOM_STACK_PASS="$pass" openssl enc -d -aes-256-cbc -pbkdf2 -iter 200000 \
      -pass env:ROOM_STACK_PASS -in "$TOKEN_STORE_FILE" -out "$plain" 2>/dev/null; then
    rm -f "$plain"
    return 1
  fi
  out="$(python3 -c 'import sys
b=open(sys.argv[1],"rb").read()
if b.startswith(b"\xef\xbb\xbf"):
    b=b[3:]
b=bytes(ch for ch in b if 32 <= ch < 127)
sys.stdout.buffer.write(b.strip())
' "$plain")" || { rm -f "$plain"; return 1; }
  rm -f "$plain"
  [[ -n "$out" ]] || return 1
  printf '%s' "$out"
}

prompt_and_save_encrypted_token() {
  local token="$1" ans pass pass2
  if ! command -v openssl >/dev/null 2>&1; then
    echo "WARNING: openssl not found — PAT not saved (install openssl to enable)." >&2
    return
  fi
  if [[ "$YES" -eq 1 ]]; then
    if [[ -n "${ROOM_STACK_PASSPHRASE:-}" ]]; then
      encrypt_token_to_store "$token" "$ROOM_STACK_PASSPHRASE"
      info "Encrypted PAT saved to $TOKEN_STORE_FILE"
    fi
    return
  fi
  read -r -p "Save encrypted PAT for next runs? [Y/n]: " ans || true
  ans="$(echo "${ans:-Y}" | tr '[:upper:]' '[:lower:]' | tr -d '[:space:]')"
  case "$ans" in
    ""|y|yes) ;;
    n|no) info "PAT not saved"; return ;;
    *) die "expected y/n, got: $ans" ;;
  esac
  pass="$(read_secret "Create encryption passphrase: ")"
  [[ -n "$pass" ]] || die "empty passphrase"
  pass2="$(read_secret "Confirm passphrase: ")"
  [[ "$pass" == "$pass2" ]] || die "passphrases do not match"
  encrypt_token_to_store "$token" "$pass"
  info "Encrypted PAT saved to $TOKEN_STORE_FILE"
  echo "  Next time enter this passphrase (or set ROOM_STACK_PASSPHRASE)."
  echo "  Forget with: $0 --forget-token"
}

load_token_from_encrypted_store() {
  [[ -r "$TOKEN_STORE_FILE" ]] || return 1
  local pass token prefix
  if [[ -n "${ROOM_STACK_PASSPHRASE:-}" ]]; then
    pass="$ROOM_STACK_PASSPHRASE"
  elif [[ "$YES" -eq 1 ]]; then
    return 1
  else
    info "Found encrypted PAT store: $TOKEN_STORE_FILE"
    pass="$(read_secret "Passphrase to unlock PAT: ")"
    [[ -n "$pass" ]] || return 1
  fi
  token="$(decrypt_token_from_store "$pass")" || {
    echo "WARNING: could not decrypt PAT store (wrong passphrase?)." >&2
    return 1
  }
  token="$(sanitize_gh_token "$token")"
  # Never rewrite the store with a non-PAT blob — a bad passphrase on some
  # OpenSSL builds can still produce printable junk and would permanently
  # corrupt github_token.enc.
  if ! validate_gh_token_shape "$token"; then
    prefix="$(printf '%s' "$token" | cut -c1-4)"
    echo "WARNING: unlocked blob is not a GitHub PAT (wrong passphrase or corrupt store)." >&2
    echo "  length=${#token} prefix='${prefix}…'  Fix: $0 --forget-token  then paste a new PAT." >&2
    return 1
  fi
  # Rewrite only a validated, sanitized PAT (drops lingering CR from older stores).
  if command -v openssl >/dev/null 2>&1; then
    encrypt_token_to_store "$token" "$pass" 2>/dev/null || true
  fi
  printf '%s' "$token"
}

forget_token_store() {
  if [[ -e "$TOKEN_STORE_FILE" ]]; then
    rm -f "$TOKEN_STORE_FILE"
    info "Removed encrypted PAT store: $TOKEN_STORE_FILE"
  else
    info "No encrypted PAT store at $TOKEN_STORE_FILE"
  fi
  exit 0
}

# Prefer env token; else encrypted store; else gh login; else prompt for a PAT.
ensure_gh_auth() {
  local token=""
  if [[ -n "${GH_TOKEN:-}" ]]; then
    apply_gh_token "$GH_TOKEN" "environment GH_TOKEN"
    return
  fi
  if [[ -n "${GITHUB_TOKEN:-}" ]]; then
    apply_gh_token "$GITHUB_TOKEN" "environment GITHUB_TOKEN"
    return
  fi
  if [[ -n "${GH_TOKEN_FILE:-}" ]]; then
    [[ -r "$GH_TOKEN_FILE" ]] || die "GH_TOKEN_FILE not readable: $GH_TOKEN_FILE"
    token="$(python3 -c 'import sys; sys.stdout.buffer.write(open(sys.argv[1],"rb").read())' "$GH_TOKEN_FILE")"
    apply_gh_token "$token" "GH_TOKEN_FILE"
    return
  fi

  if token="$(load_token_from_encrypted_store)"; then
    apply_gh_token "$token" "encrypted store"
    return
  fi

  # Only trust gh login if it can actually call the API (avoids stale bad headers).
  # Use rate_limit — Actions GITHUB_TOKEN often cannot call GET /user.
  if gh auth status >/dev/null 2>&1 && gh_token_works; then
    info "GitHub auth: existing gh login OK as $(gh_token_identity)"
    return
  fi

  if [[ "$YES" -eq 1 ]]; then
    die "GitHub auth required. Set GH_TOKEN, or create $TOKEN_STORE_FILE and set ROOM_STACK_PASSPHRASE."
  fi

  echo
  info "GitHub PAT required to download Actions artifacts"
  echo "  Open this link to generate a classic token (public_repo pre-checked):"
  echo "  https://github.com/settings/tokens/new?scopes=public_repo&description=room-stack-ci-install"
  echo "  Click Generate token, copy the ghp_… value, paste below."
  echo "  (Fine-grained: Actions=Read on Door/Qucs/Xschem/KLayout/EMStudio repos.)"
  token="$(read_secret "Paste PAT: ")"
  [[ -n "$token" ]] || die "empty PAT"
  apply_gh_token "$token" "pasted PAT"

  prompt_and_save_encrypted_token "$GH_TOKEN"

  # Best-effort gh login for other tools (ignore keyring failures).
  if printf '%s' "$GH_TOKEN" | gh auth login --hostname github.com --with-token >/dev/null 2>&1; then
    info "GitHub auth: also saved via gh auth login"
  fi
}

# Prefer /usr/local when writable; otherwise $HOME/.local (no sudo).
default_install_root() {
  if [[ -n "${DEFAULT_ROOT:-}" ]]; then
    echo "$DEFAULT_ROOT"
    return
  fi
  if [[ -d /usr/local ]] && [[ -w /usr/local ]]; then
    echo "/usr/local"
  else
    echo "$HOME/.local"
  fi
}

# Per-tool payload lives under <prefix>/lib/room-stack/<tool> unless TOOL_PREFIX is set.
tool_payload_dir() {
  local tool="$1"
  echo "$INSTALL_PREFIX/lib/room-stack/$tool"
}

# Guess install root for --update when --prefix is omitted.
guess_install_prefix() {
  local cand
  for cand in \
      "${PREFIX_OPT:-}" \
      "${INSTALL_PREFIX:-}" \
      "${DEFAULT_ROOT:-}" \
      "$HOME/door" \
      "$HOME/.local" \
      "/usr/local"; do
    [[ -n "$cand" ]] || continue
    if [[ -d "$cand/lib/room-stack" ]]; then
      echo "${cand%/}"
      return 0
    fi
  done
  return 1
}

bin_name_for_tool() {
  case "$1" in
    door) echo door ;;
    qucs) echo qucs-s ;;
    xschem) echo xschem ;;
    klayout) echo klayout ;;
    emstudio) echo emstudio ;;
    *) echo "$1" ;;
  esac
}

tool_is_installed() {
  local tool="$1"
  local payload bin
  payload="$(tool_payload_dir "$tool")"
  if [[ -f "$payload/BUNDLE_SOURCE.txt" ]]; then
    return 0
  fi
  # Payload dir with any content (partial install) counts.
  if [[ -d "$payload" ]] && [[ -n "$(find "$payload" -mindepth 1 -maxdepth 1 2>/dev/null | head -n1)" ]]; then
    return 0
  fi
  bin="${BIN_DIR:-$INSTALL_PREFIX/bin}/$(bin_name_for_tool "$tool")"
  [[ -e "$bin" ]] && return 0
  if [[ "$tool" == "emstudio" && -e "${BIN_DIR:-$INSTALL_PREFIX/bin}/EMStudio" ]]; then
    return 0
  fi
  return 1
}

discover_installed_tools() {
  local t
  local -a found=()
  for t in "${ALL_TOOLS[@]}"; do
    if tool_is_installed "$t"; then
      found+=("$t")
    fi
  done
  if [[ ${#found[@]} -eq 0 ]]; then
    return 1
  fi
  TOOLS=("${found[@]}")
  return 0
}

read_bundle_field() {
  local file="$1" key="$2"
  [[ -f "$file" ]] || return 1
  local line
  line="$(grep -E "^${key}=" "$file" 2>/dev/null | head -n1 || true)"
  [[ -n "$line" ]] || return 1
  printf '%s' "${line#*=}"
}

# Resolve newest non-expired Actions artifact → ART_ID / CREATED / RUN_HINT via sourced file.
resolve_latest_artifact_meta() {
  local repo="$1" artifact="$2" meta_out="$3"
  local art_json
  art_json="$(mktemp)"
  gh api "repos/$repo/actions/artifacts?name=$artifact&per_page=20" >"$art_json" \
    || { rm -f "$art_json"; return 1; }
  python3 - "$art_json" "$meta_out" <<'PY'
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
  local rc=$?
  rm -f "$art_json"
  return "$rc"
}

# 0 = needs install/update, 1 = already current. Prints reason on stdout.
tool_update_status() {
  local tool="$1"
  local repo artifact payload meta installed_id latest_id created
  repo="$(repo_for "$tool")"
  artifact="$(artifact_for "$tool")"
  payload="$(prefix_for "$tool")"
  meta="$(mktemp)"
  if ! resolve_latest_artifact_meta "$repo" "$artifact" "$meta"; then
    rm -f "$meta"
    echo "error: cannot query $repo artifact $artifact"
    return 0
  fi
  # shellcheck disable=SC1090
  source "$meta"
  rm -f "$meta"
  latest_id="${ART_ID:-}"
  created="${CREATED:-}"
  if [[ -z "$latest_id" ]]; then
    echo "error: no non-expired artifact $artifact on $repo"
    return 0
  fi
  if [[ ! -f "$payload/BUNDLE_SOURCE.txt" ]]; then
    echo "missing (will install) latest=$latest_id created=$created"
    return 0
  fi
  installed_id="$(read_bundle_field "$payload/BUNDLE_SOURCE.txt" artifact_id || true)"
  if [[ -z "$installed_id" ]]; then
    echo "no artifact_id in BUNDLE_SOURCE (will reinstall) latest=$latest_id"
    return 0
  fi
  if [[ "$installed_id" == "$latest_id" ]]; then
    echo "up-to-date artifact_id=$installed_id created=$created"
    return 1
  fi
  echo "update installed=$installed_id -> latest=$latest_id created=$created"
  return 0
}

# glibc major.minor from the running linker (e.g. 2.28 on Rocky 8, 2.39 on Ubuntu 24).
host_glibc_version() {
  local v
  v="$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}')"
  if [[ -z "$v" ]]; then
    v="$(ldd --version 2>&1 | head -n1 | grep -oE '[0-9]+\.[0-9]+' | head -n1 || true)"
  fi
  printf '%s' "$v"
}

# Compare dotted versions: 0 if $1 < $2, 1 otherwise.
version_lt() {
  local a="$1" b="$2"
  [[ "$(printf '%s\n%s\n' "$a" "$b" | sort -V | head -n1)" == "$a" && "$a" != "$b" ]]
}

detect_flavor() {
  if [[ -n "$FLAVOR_FORCE" ]]; then
    echo "$FLAVOR_FORCE"
    return
  fi
  local id="" id_like="" ver="" glibc=""
  if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    id="${ID:-}"
    id_like="${ID_LIKE:-}"
    ver="${VERSION_ID:-}"
  fi
  glibc="$(host_glibc_version)"

  # Prefer glibc: ubuntu24 bundles need ≥ 2.34; rocky8/ubuntu22 need ≥ 2.28.
  if [[ -n "$glibc" ]] && version_lt "$glibc" "2.34"; then
    echo "Host glibc $glibc < 2.34 — using rocky8 / ubuntu22 bundles." >&2
    echo rocky8
    return
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
    rocky|almalinux|rhel|centos|ol|scientific)
      echo rocky8
      ;;
    *)
      # RHEL-family often sets ID_LIKE="rhel fedora" / "centos rhel fedora".
      if [[ " $id_like " == *" rhel "* || " $id_like " == *" centos "* || " $id_like " == *" fedora "* ]]; then
        echo "Distro '${id}' (ID_LIKE=$id_like) — using rocky8 bundles." >&2
        echo rocky8
        return
      fi
      if [[ -n "$glibc" ]] && ! version_lt "$glibc" "2.34"; then
        echo "Unknown distro '${id:-?}' ${ver} (glibc $glibc) — defaulting to ubuntu24." >&2
        echo ubuntu24
      else
        echo "Unknown distro '${id:-?}' ${ver} — defaulting to rocky8 (safer glibc)." >&2
        echo rocky8
      fi
      ;;
  esac
}

# artifact name per tool / flavor (FLAVOR must be set)
artifact_for() {
  local tool="$1"
  case "$tool/$FLAVOR" in
    door/ubuntu24)       echo door-linux-ubuntu24 ;;
    door/rocky8)         echo door-rhel8 ;;
    qucs/ubuntu24)       echo qucs-s-ubuntu24 ;;
    qucs/rocky8)         echo qucs-s-rocky8 ;;
    xschem/ubuntu24)     echo xschem-ubuntu24 ;;
    xschem/rocky8)       echo xschem-rocky8 ;;
    klayout/ubuntu24)    echo klayout-linux-ubuntu24 ;;
    klayout/rocky8)      echo klayout-rocky8 ;;
    # EMStudio ships portable ubuntu22/ubuntu24 bundles (no rocky8 yet).
    emstudio/ubuntu24)   echo emstudio-linux-ubuntu24 ;;
    emstudio/rocky8)     echo emstudio-linux-ubuntu22 ;;
    *) die "no artifact mapping for $tool / $FLAVOR" ;;
  esac
}

repo_for() {
  case "$1" in
    door) echo "$REPO_DOOR" ;;
    qucs) echo "$REPO_QUCS" ;;
    xschem) echo "$REPO_XSCHEM" ;;
    klayout) echo "$REPO_KLAYOUT" ;;
    emstudio) echo "$REPO_EMSTUDIO" ;;
    *) die "unknown tool: $1" ;;
  esac
}

workflow_for() {
  case "$1" in
    door) echo "$WORKFLOW_DOOR" ;;
    qucs) echo "$WORKFLOW_QUCS" ;;
    xschem) echo "$WORKFLOW_XSCHEM" ;;
    klayout) echo "$WORKFLOW_KLAYOUT" ;;
    emstudio) echo "$WORKFLOW_EMSTUDIO" ;;
  esac
}

prefix_for() {
  case "$1" in
    door) echo "$DOOR_PREFIX" ;;
    qucs) echo "$QUCS_PREFIX" ;;
    xschem) echo "$XSCHEM_PREFIX" ;;
    klayout) echo "$KLAYOUT_PREFIX" ;;
    emstudio) echo "$EMSTUDIO_PREFIX" ;;
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
      door|qucs|xschem|klayout|emstudio) TOOLS+=("$t") ;;
      qucs-s) TOOLS+=("qucs") ;;
      em) TOOLS+=("emstudio") ;;
      *) die "unknown tool in --tools: $t (want: door,qucs,xschem,klayout,emstudio,all)" ;;
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
  [[ ${#TOOLS[@]} -gt 0 ]] || die "tools list is empty (use door,qucs,xschem,klayout,emstudio or all)"
}

# Interactive: default = all; user may edit the list or answer y/n per tool.
prompt_tools_selection() {
  if [[ "$YES" -eq 1 || "$TOOLS_EXPLICIT" -eq 1 ]]; then
    return
  fi
  local default="door,qucs,xschem,klayout,emstudio" ans mode
  echo
  info "Select tools to install (default: all)"
  echo "  Available: door, qucs, xschem, klayout, emstudio"
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

  if version_lt "2.28" "2.34"; then
    echo "OK: version_lt 2.28 < 2.34"
  else
    echo "FAIL: version_lt 2.28 < 2.34" >&2
    fail=1
  fi
  if version_lt "2.34" "2.28"; then
    echo "FAIL: version_lt 2.34 < 2.28 should be false" >&2
    fail=1
  else
    echo "OK: version_lt 2.34 !< 2.28"
  fi

  FLAVOR_FORCE=ubuntu24
  FLAVOR="$(detect_flavor)"
  assert_eq "$FLAVOR" ubuntu24 "force ubuntu24"
  assert_eq "$(artifact_for door)" door-linux-ubuntu24 "door ubuntu24 artifact"
  assert_eq "$(artifact_for qucs)" qucs-s-ubuntu24 "qucs ubuntu24 artifact"
  assert_eq "$(artifact_for xschem)" xschem-ubuntu24 "xschem ubuntu24 artifact"
  assert_eq "$(artifact_for klayout)" klayout-linux-ubuntu24 "klayout ubuntu24 artifact"
  assert_eq "$(artifact_for emstudio)" emstudio-linux-ubuntu24 "emstudio ubuntu24 artifact"

  FLAVOR_FORCE=rocky8
  FLAVOR="$(detect_flavor)"
  assert_eq "$FLAVOR" rocky8 "force rocky8"
  assert_eq "$(artifact_for door)" door-rhel8 "door rocky8 artifact"
  assert_eq "$(artifact_for qucs)" qucs-s-rocky8 "qucs rocky8 artifact"
  assert_eq "$(artifact_for xschem)" xschem-rocky8 "xschem rocky8 artifact"
  assert_eq "$(artifact_for klayout)" klayout-rocky8 "klayout rocky8 artifact"
  assert_eq "$(artifact_for emstudio)" emstudio-linux-ubuntu22 "emstudio rocky8->ubuntu22 artifact"

  assert_eq "$(repo_for door)" "$REPO_DOOR" "door repo"
  assert_eq "$(repo_for klayout)" "$REPO_KLAYOUT" "klayout repo"
  assert_eq "$(repo_for emstudio)" "$REPO_EMSTUDIO" "emstudio repo"

  parse_tools "all"
  assert_eq "${#TOOLS[@]}" "5" "parse all -> 5 tools"
  assert_eq "${TOOLS[*]}" "door qucs xschem klayout emstudio" "parse all order"
  parse_tools "door, qucs-s, em"
  assert_eq "${TOOLS[*]}" "door qucs emstudio" "parse subset + aliases"
  parse_tools "klayout door door"
  assert_eq "${TOOLS[*]}" "klayout door" "dedupe preserves first order"

  INSTALL_PREFIX="/tmp/room-prefix-test"
  assert_eq "$(tool_payload_dir door)" "/tmp/room-prefix-test/lib/room-stack/door" "payload dir layout"
  assert_eq "$(bin_name_for_tool qucs)" "qucs-s" "qucs bin name"
  assert_eq "$(bin_name_for_tool emstudio)" "emstudio" "emstudio bin name"

  # discover_installed_tools: seed fake installs
  rm -rf /tmp/room-prefix-test
  mkdir -p /tmp/room-prefix-test/lib/room-stack/qucs /tmp/room-prefix-test/lib/room-stack/klayout
  echo "artifact_id=111" >/tmp/room-prefix-test/lib/room-stack/qucs/BUNDLE_SOURCE.txt
  echo "artifact_id=222" >/tmp/room-prefix-test/lib/room-stack/klayout/BUNDLE_SOURCE.txt
  BIN_DIR=/tmp/room-prefix-test/bin
  discover_installed_tools || fail=1
  assert_eq "${TOOLS[*]}" "qucs klayout" "discover installed tools"
  assert_eq "$(read_bundle_field /tmp/room-prefix-test/lib/room-stack/qucs/BUNDLE_SOURCE.txt artifact_id)" "111" "read bundle field"
  rm -rf /tmp/room-prefix-test

  case "$(uname -m)" in
    x86_64|amd64|aarch64|arm64)
      _arch="$(gh_linux_arch)"
      assert_eq "$_arch" "$_arch" "gh_linux_arch resolves ($_arch)"
      ;;
    *)
      echo "SKIP: gh_linux_arch on $(uname -m)"
      ;;
  esac

  _san="$(sanitize_gh_token $'ghp_abc123\r\n')"
  assert_eq "$_san" "ghp_abc123" "sanitize strips CR/LF from PAT"

  # info must not pollute stdout (breaks token="$(load_token_from_encrypted_store)").
  _info_out="$(info "probe" 2>/dev/null || true)"
  assert_eq "$_info_out" "" "info writes only to stderr"

  if command -v openssl >/dev/null 2>&1; then
    local _prev_store="$TOKEN_STORE_FILE" _round
    TOKEN_STORE_FILE="$(mktemp)"
    rm -f "$TOKEN_STORE_FILE"
    encrypt_token_to_store $'ghp_test_token_value\r' "test-pass-phrase"
    _round="$(decrypt_token_from_store "test-pass-phrase")"
    assert_eq "$_round" "ghp_test_token_value" "openssl token encrypt/decrypt strips CR"
    encrypt_token_to_store "ghp_special_pass_token_xx" 'p$a:ss!word'
    _round="$(decrypt_token_from_store 'p$a:ss!word')"
    assert_eq "$_round" "ghp_special_pass_token_xx" "openssl pass env handles \$/! in passphrase"
    if _round="$(decrypt_token_from_store "wrong-passphrase")"; then
      echo "FAIL: decrypt with wrong passphrase succeeded unexpectedly" >&2
      fail=1
    else
      echo "OK: wrong passphrase fails decrypt"
    fi
    rm -f "$TOKEN_STORE_FILE"
    TOKEN_STORE_FILE="$_prev_store"
  else
    echo "SKIP: openssl not available for token encrypt test"
  fi

  bash -n "$0" || { echo "FAIL: bash -n" >&2; fail=1; }
  [[ "$fail" -eq 0 ]] || die "self-test failed"
  info "Self-test passed"
  exit 0
}

if [[ "$FORGET_TOKEN" -eq 1 ]]; then
  forget_token_store
fi

if [[ "$SELF_TEST" -eq 1 ]]; then
  # FLAVOR unused until assertions set FLAVOR_FORCE
  FLAVOR=ubuntu24
  run_self_test
fi

ensure_gh
# --from-tar with a local file still uses gh for CI-busy checks unless dry-run exits earlier
ensure_gh_auth

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

want_tool() {
  local t="$1" x
  for x in "${TOOLS[@]}"; do
    [[ "$x" == "$t" ]] && return 0
  done
  return 1
}

# Resolve prefix early — --update needs it to discover installed tools.
INSTALL_PREFIX="${INSTALL_PREFIX:-${PREFIX_OPT:-}}"
if [[ -z "$INSTALL_PREFIX" ]]; then
  if [[ "$UPDATE_MODE" -eq 1 ]]; then
    if INSTALL_PREFIX="$(guess_install_prefix)"; then
      info "Detected existing install prefix: $INSTALL_PREFIX"
    elif [[ "$YES" -eq 1 ]]; then
      INSTALL_PREFIX="$(default_install_root)"
    else
      INSTALL_PREFIX="$(prompt_dir "Install prefix" "$(default_install_root)")"
    fi
  else
    INSTALL_PREFIX="$(prompt_dir "Install prefix" "$(default_install_root)")"
  fi
fi
INSTALL_PREFIX="${INSTALL_PREFIX%/}"
BIN_DIR="${BIN_DIR:-$INSTALL_PREFIX/bin}"

# Tool selection:
#   --update (no --tools) → only tools already present under prefix
#   --update --tools …   → only the listed tools (install if missing / update if newer)
#   otherwise            → interactive / --tools / all as before
if [[ "$UPDATE_MODE" -eq 1 ]]; then
  if [[ "$TOOLS_EXPLICIT" -eq 1 ]]; then
    parse_tools "$TOOLS_CSV"
  else
    if ! discover_installed_tools; then
      die "nothing installed under $INSTALL_PREFIX/lib/room-stack — run without --update first, or pass --tools"
    fi
    info "Update mode: found installed tools: ${TOOLS[*]}"
  fi
else
  parse_tools "$TOOLS_CSV"
  prompt_tools_selection
fi

FLAVOR="$(detect_flavor)"
info "Detected install flavor: $FLAVOR"
info "Tools: ${TOOLS[*]}"

DOOR_PREFIX="${DOOR_PREFIX:-}"
QUCS_PREFIX="${QUCS_PREFIX:-}"
XSCHEM_PREFIX="${XSCHEM_PREFIX:-}"
KLAYOUT_PREFIX="${KLAYOUT_PREFIX:-}"
EMSTUDIO_PREFIX="${EMSTUDIO_PREFIX:-}"

if want_tool door; then
  DOOR_PREFIX="${DOOR_PREFIX:-$(tool_payload_dir door)}"
fi
if want_tool qucs; then
  QUCS_PREFIX="${QUCS_PREFIX:-$(tool_payload_dir qucs)}"
fi
if want_tool xschem; then
  XSCHEM_PREFIX="${XSCHEM_PREFIX:-$(tool_payload_dir xschem)}"
fi
if want_tool klayout; then
  KLAYOUT_PREFIX="${KLAYOUT_PREFIX:-$(tool_payload_dir klayout)}"
fi
if want_tool emstudio; then
  EMSTUDIO_PREFIX="${EMSTUDIO_PREFIX:-$(tool_payload_dir emstudio)}"
fi
BIN_DIR="${BIN_DIR:-$INSTALL_PREFIX/bin}"

info "Install prefix: $INSTALL_PREFIX"
info "Layout: $BIN_DIR/<tool>  and  $INSTALL_PREFIX/lib/room-stack/<tool>/"
for t in "${TOOLS[@]}"; do
  info "$t -> $(prefix_for "$t")"
done
info "PATH bin -> $BIN_DIR"

# --update: drop tools that already match the latest CI artifact_id.
if [[ "$UPDATE_MODE" -eq 1 ]]; then
  _status=""
  _pending=()
  info "Checking CI artifacts for updates…"
  for t in "${TOOLS[@]}"; do
    if _status="$(tool_update_status "$t")"; then
      info "$t: $_status"
      _pending+=("$t")
    else
      info "$t: $_status — skip"
    fi
  done
  if [[ ${#_pending[@]} -eq 0 ]]; then
    info "All selected tools are up to date."
    if [[ "$DRY_RUN" -eq 1 ]]; then
      info "Dry-run OK"
    fi
    exit 0
  fi
  TOOLS=("${_pending[@]}")
  info "Will update/install: ${TOOLS[*]}"
fi

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

# Wipe only this tool's payload dir (never the shared prefix / sibling tools).
clear_tool_dir() {
  local out_dir="$1"
  mkdir -p "$out_dir"
  find "$out_dir" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
}

write_bundle_source() {
  local out_dir="$1"
  shift
  {
    printf '%s\n' "$@"
  } >"$out_dir/BUNDLE_SOURCE.txt"
}

# Copy unzipped artifact payload into out_dir (for artifacts that are a directory, not tar.gz).
copy_stage_payload() {
  local stage="$1" out_dir="$2"
  local item base
  for item in "$stage"/*; do
    [[ -e "$item" ]] || continue
    base="$(basename "$item")"
    case "$base" in
      .download|artifacts.json|bundle.zip|meta.env) continue ;;
    esac
    cp -a "$item" "$out_dir/"
  done
}

download_artifact() {
  local repo="$1" artifact="$2" out_dir="$3"
  local stage meta zip tar_path
  # Stage downloads under the shared prefix cache, not inside the tool dir,
  # so a failed fetch cannot leave a half-wiped tool.
  stage="${INSTALL_PREFIX}/lib/room-stack/.download-$$"
  rm -rf "$stage"
  mkdir -p "$stage" "$out_dir"

  info "Fetching artifact '$artifact' from $repo"
  meta="$stage/meta.env"
  resolve_latest_artifact_meta "$repo" "$artifact" "$meta" \
    || die "cannot list artifacts on $repo (need PAT with actions:read)"
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

  clear_tool_dir "$out_dir"
  if [[ -n "$tar_path" ]]; then
    tar -xzf "$tar_path" -C "$out_dir"
  else
    # EMStudio (and similar) upload a directory tree, not a nested tar.gz.
    copy_stage_payload "$stage" "$out_dir"
    # If the zip had a single top-level folder, flatten one level when it looks like a bundle.
    if [[ "$(find "$out_dir" -mindepth 1 -maxdepth 1 | wc -l)" -eq 1 ]]; then
      local only
      only="$(find "$out_dir" -mindepth 1 -maxdepth 1 -type d | head -n1 || true)"
      if [[ -n "$only" ]] && [[ ! -e "$only/BUNDLE_SOURCE.txt" ]]; then
        # Keep as-is; find_exec searches recursively.
        :
      fi
    fi
  fi
  rm -rf "$stage"

  write_bundle_source "$out_dir" \
    "repo=$repo" \
    "artifact=$artifact" \
    "artifact_id=$ART_ID" \
    "created_at=$CREATED" \
    "workflow_run=$RUN_HINT"
  restore_payload_exec_bits "$out_dir"
}

install_from_tar() {
  local out_dir="$1" tar_path="$2"
  clear_tool_dir "$out_dir"
  tar -xzf "$tar_path" -C "$out_dir"
  write_bundle_source "$out_dir" \
    "source=local-tar" \
    "tar=$tar_path"
  restore_payload_exec_bits "$out_dir"
}

# GitHub Actions artifact zips drop the executable bit; launchers then fail with
# "Permission denied" on EMStudio.bin / door / klayout / *.sh.
restore_payload_exec_bits() {
  local root="$1"
  [[ -d "$root" ]] || return 0
  find "$root" -type f \( \
      -name '*.sh' -o -name '*.bin' -o -name '*-run.sh' \
      -o -name 'door' -o -name 'EMStudio' -o -name 'emstudio' \
      -o -name 'klayout' -o -name 'xschem' -o -name 'qucs-s' -o -name 'qucs' \
      -o -name 'gds_to_*' -o -name 'room_to_*' -o -name '*_to_room' -o -name '*_to_qucs' \
      -o -name '*_to_xschem' -o -name '*_to_gds' -o -name '*_to_json' \
    \) -exec chmod +x {} + 2>/dev/null || true
}

find_exec() {
  local root="$1"
  shift
  local name hit
  for name in "$@"; do
    hit="$(find "$root" -type f -name "$name" 2>/dev/null | head -n1 || true)"
    if [[ -n "$hit" ]]; then
      chmod +x "$hit" 2>/dev/null || true
      # Companion ELF next to a launcher (EMStudio → EMStudio.bin).
      if [[ -f "${hit}.bin" ]]; then
        chmod +x "${hit}.bin" 2>/dev/null || true
      fi
      echo "$hit"
      return 0
    fi
  done
  return 1
}

# Write a small absolute-path launcher (NOT a symlink).
# Symlinks break *-run.sh scripts that use dirname "$0" / BASH_SOURCE (they resolve
# to <prefix>/bin and then look for bin/bin/<tool>).
link_into_bin() {
  local target="$1" link_name="$2" abs
  mkdir -p "$BIN_DIR"
  abs="$(readlink -f "$target" 2>/dev/null || realpath "$target" 2>/dev/null || echo "$target")"
  [[ -e "$abs" ]] || die "launcher target missing: $target"
  # Remove old symlink/file from previous installs.
  rm -f "$BIN_DIR/$link_name"
  cat >"$BIN_DIR/$link_name" <<EOF
#!/usr/bin/env bash
exec "$abs" "\$@"
EOF
  chmod +x "$BIN_DIR/$link_name"
  info "launcher $BIN_DIR/$link_name -> $abs"
}

# Patch older *-run.sh / EMStudio wrappers that resolve DIR via dirname only.
# Skip if they already follow symlinks (while [[ -L ... ]] + readlink).
patch_run_script_symlink_safe() {
  local run_sh="$1"
  [[ -f "$run_sh" ]] || return 0
  if grep -q 'while \[\[ -L' "$run_sh" 2>/dev/null; then
    return 0
  fi
  local tmp
  tmp="$(mktemp)"
  python3 - "$run_sh" "$tmp" <<'PY'
import pathlib, re, sys
src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
text = src.read_text(encoding="utf-8", errors="replace")
repl = '''_src="${BASH_SOURCE[0]}"
while [[ -L "$_src" ]]; do
  _dir="$(cd -P "$(dirname "$_src")" && pwd)"
  _src="$(readlink "$_src")"
  [[ "$_src" != /* ]] && _src="$_dir/$_src"
done
DIR="$(cd -P "$(dirname "$_src")" && pwd)"'''
patterns = [
    # DIR="$(cd "$(dirname "$0")" && pwd)"
    re.compile(r'^DIR="\$\(cd "\$\(dirname "\$0"\)" && pwd\)"\s*$', re.M),
    # DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    re.compile(
        r'^DIR="\$\(cd "\$\(dirname "\$\{BASH_SOURCE\[0\]\}"\)" && pwd\)"\s*$',
        re.M,
    ),
    # DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" with alternate quoting
    re.compile(
        r"^DIR=\"\$\(cd \"\$\(dirname \"\$\{BASH_SOURCE\[0\]\}\"\)\" && pwd\)\"\s*$",
        re.M,
    ),
]
new, n = text, 0
for pat in patterns:
    new, n = pat.subn(repl, new, count=1)
    if n:
        break
if n:
    dst.write_text(new, encoding="utf-8")
    print(f"patched {src}")
else:
    dst.write_text(text, encoding="utf-8")
PY
  mv -f "$tmp" "$run_sh"
  chmod +x "$run_sh"
}

# Drop Room's python package (+ room_c if available) next to door so `import room` works.
attach_room_python() {
  local door_prefix="$1"
  local run_sh bundle stage tarball room_src pyroot so
  run_sh="$(find_exec "$door_prefix" door-run.sh || true)"
  [[ -n "$run_sh" ]] || return 0
  bundle="$(cd "$(dirname "$run_sh")" && pwd)"
  pyroot="$bundle/python"
  if [[ -f "$pyroot/room/__init__.py" ]]; then
    info "Room python already at $pyroot"
  else
    info "Attaching Room python package into $pyroot"
    stage="${INSTALL_PREFIX}/lib/room-stack/.room-py-$$"
    rm -rf "$stage"
    mkdir -p "$stage"
    tarball="$stage/room-src.tgz"
    # Public source tarball — no artifact permission needed beyond normal git clone/curl.
    download_url "https://codeload.github.com/IHP-GmbH/Room/tar.gz/refs/heads/main" "$tarball" \
      || die "failed to download Room source (need network)"
    tar -xzf "$tarball" -C "$stage"
    room_src="$(find "$stage" -type d -path '*/python/room' | head -n1 || true)"
    [[ -n "$room_src" ]] || die "Room python/room not found in source tarball"
    mkdir -p "$pyroot"
    rm -rf "$pyroot/room"
    cp -a "$room_src" "$pyroot/room"
    # Optional pure-python helpers from the same tree.
    if [[ -d "$(dirname "$room_src")" ]]; then
      local pdir
      pdir="$(dirname "$room_src")"
      for f in door_console.py setup.py pyproject.toml; do
        [[ -f "$pdir/$f" ]] || continue
        cp -a "$pdir/$f" "$pyroot/" 2>/dev/null || true
      done
    fi
    rm -rf "$stage"
    info "Installed $pyroot/room"
  fi

  # Best-effort: pull libroom_c.so from Room CI artifact (ubuntu build).
  if ! find "$bundle" \( -name 'libroom_c.so' -o -name 'room_c.so' \) 2>/dev/null | grep -q .; then
    info "Trying to fetch libroom_c.so from Room CI (room-build-ubuntu)"
    stage="${INSTALL_PREFIX}/lib/room-stack/.room-c-$$"
    rm -rf "$stage"
    mkdir -p "$stage"
    # Subshell: download_artifact calls die on failure — do not abort door install.
    if ( download_artifact "$REPO_ROOM" "room-build-ubuntu" "$stage" ); then
      so="$(find "$stage" -type f \( -name 'libroom_c.so' -o -name 'room_c.so' \) | head -n1 || true)"
      if [[ -n "$so" ]]; then
        mkdir -p "$pyroot/room" "$bundle/lib"
        cp -a "$so" "$pyroot/room/"
        cp -a "$so" "$bundle/lib/"
        info "Installed room_c: $pyroot/room/$(basename "$so")"
      else
        echo "WARNING: room-build-ubuntu has no libroom_c.so — pure-python room only." >&2
      fi
    else
      echo "WARNING: could not fetch room-build-ubuntu — import room may lack native room_c." >&2
    fi
    rm -rf "$stage"
  fi

  # Ensure door-run.sh exports DOOR_ROOM_PYTHON / ROOM_C_DLL when possible.
  if [[ -f "$run_sh" ]] && ! grep -q 'DOOR_ROOM_PYTHON' "$run_sh" 2>/dev/null; then
    local tmp
    tmp="$(mktemp)"
    python3 - "$run_sh" "$tmp" "$pyroot" <<'PY'
import pathlib, sys
src, dst, pyroot = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3]
text = src.read_text(encoding="utf-8", errors="replace")
block = f'''
# Injected by install_room_stack_from_ci.sh — Room python next to the bundle.
if [[ -f "{pyroot}/room/__init__.py" ]]; then
  export DOOR_ROOM_PYTHON="${{DOOR_ROOM_PYTHON:-{pyroot}}}"
  export PYTHONPATH="{pyroot}${{PYTHONPATH:+:$PYTHONPATH}}"
fi
if [[ -z "${{ROOM_C_DLL:-}}" ]]; then
  for _c in "{pyroot}/room/libroom_c.so" "{pyroot}/libroom_c.so" "$DIR/lib/libroom_c.so"; do
    [[ -f "$_c" ]] && export ROOM_C_DLL="$_c" && break
  done
fi
'''
# Insert just before the final exec line.
lines = text.splitlines(keepends=True)
out = []
injected = False
for line in lines:
    if (not injected) and line.lstrip().startswith("exec "):
        out.append(block)
        if not block.endswith("\n"):
            out.append("\n")
        injected = True
    out.append(line)
if not injected:
    out.append(block)
dst.write_text("".join(out), encoding="utf-8")
PY
    mv -f "$tmp" "$run_sh"
    chmod +x "$run_sh"
  fi
}

# Older xschem CI bundles shipped room.tcl without room_lock.tcl / primitive_cache.
# Pull the missing integration files from the Xschem-roomdb repo so launch works now.
repair_xschem_integrations() {
  local prefix="$1"
  local integ stage tarball src
  integ="$(find "$prefix" -type d -path '*/integrations' | head -n1 || true)"
  [[ -n "$integ" ]] || return 0

  local need=0
  [[ -f "$integ/room_lock.tcl" ]] || need=1
  [[ -f "$integ/xschem-batch.rc" ]] || need=1
  [[ -d "$integ/primitive_cache" ]] || need=1
  [[ "$need" -eq 1 ]] || {
    info "Xschem integrations OK ($integ)"
    return 0
  }

  info "Repairing Xschem integrations under $integ (fetch from $REPO_XSCHEM)"
  stage="${INSTALL_PREFIX}/lib/room-stack/.xschem-integ-$$"
  rm -rf "$stage"
  mkdir -p "$stage"
  tarball="$stage/xschem-src.tgz"
  download_url "https://codeload.github.com/${REPO_XSCHEM}/tar.gz/refs/heads/main" "$tarball" \
    || die "failed to download Xschem-roomdb source for integrations repair"
  tar -xzf "$tarball" -C "$stage"
  src="$(find "$stage" -type d -path '*/integrations' | head -n1 || true)"
  [[ -n "$src" ]] || die "integrations/ not found in Xschem-roomdb tarball"

  for _f in room_lock.tcl xschem-batch.rc room.tcl; do
    if [[ -f "$src/$_f" ]]; then
      cp -a "$src/$_f" "$integ/"
    fi
  done
  if [[ -d "$src/primitive_cache" && ! -d "$integ/primitive_cache" ]]; then
    cp -a "$src/primitive_cache" "$integ/"
  fi
  if [[ -d "$src/qucs_symbols" && ! -d "$integ/qucs_symbols" ]]; then
    cp -a "$src/qucs_symbols" "$integ/"
  fi
  rm -rf "$stage"
  [[ -f "$integ/room_lock.tcl" ]] \
    || die "repair failed: $integ/room_lock.tcl still missing"
  info "Xschem integrations repaired"
}

install_one() {
  local tool="$1" prefix="$2"
  local repo artifact exe
  info "Installing $tool into $prefix (siblings under lib/room-stack/ are kept)"
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
      patch_run_script_symlink_safe "$exe"
      attach_room_python "$prefix"
      # Re-resolve after possible patch/attach.
      exe="$(find_exec "$prefix" door-run.sh door)"
      link_into_bin "$exe" door
      ;;
    qucs)
      exe="$(find_exec "$prefix" qucs-s-run.sh qucs-s qucs)" \
        || die "qucs-s binary not found under $prefix"
      patch_run_script_symlink_safe "$exe"
      link_into_bin "$exe" qucs-s
      ;;
    xschem)
      exe="$(find_exec "$prefix" xschem-run.sh xschem)" \
        || die "xschem binary not found under $prefix"
      patch_run_script_symlink_safe "$exe"
      repair_xschem_integrations "$prefix"
      link_into_bin "$exe" xschem
      ;;
    klayout)
      exe="$(find_exec "$prefix" klayout-run.sh klayout)" \
        || die "klayout binary not found under $prefix"
      patch_run_script_symlink_safe "$exe"
      link_into_bin "$exe" klayout
      ;;
    emstudio)
      exe="$(find_exec "$prefix" EMStudio emstudio)" \
        || die "EMStudio launcher/binary not found under $prefix"
      restore_payload_exec_bits "$prefix"
      patch_run_script_symlink_safe "$exe"
      # Launcher execs sibling EMStudio.bin — must be +x after Actions zip extract.
      if [[ -f "$(dirname "$exe")/EMStudio.bin" ]]; then
        chmod +x "$(dirname "$exe")/EMStudio.bin"
      fi
      link_into_bin "$exe" emstudio
      if [[ "$(basename "$exe")" != "emstudio" ]]; then
        link_into_bin "$exe" EMStudio
      fi
      ;;
  esac
}

if [[ "$DRY_RUN" -eq 1 ]]; then
  info "Dry-run plan${UPDATE_MODE:+ (update mode)}:"
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

ensure_writable "$INSTALL_PREFIX"
ensure_writable "$INSTALL_PREFIX/lib/room-stack"
for t in "${TOOLS[@]}"; do
  ensure_writable "$(prefix_for "$t")"
done
ensure_writable "$BIN_DIR"

for t in "${TOOLS[@]}"; do
  install_one "$t" "$(prefix_for "$t")"
done

# Migrate legacy ~/.config/LIBAMN.conf → DOOR.conf and seed Tools paths from this install.
door_settings_conf() {
  echo "${XDG_CONFIG_HOME:-$HOME/.config}/DOOR.conf"
}

migrate_libamn_conf_file() {
  local cfg_dir door legacy
  cfg_dir="${XDG_CONFIG_HOME:-$HOME/.config}"
  door="$cfg_dir/DOOR.conf"
  legacy="$cfg_dir/LIBAMN.conf"
  if [[ -f "$legacy" && ! -f "$door" ]]; then
    mv "$legacy" "$door"
    info "Migrated $legacy -> $door"
  fi
}

# Prefer absolute launcher under BIN_DIR, else first match under a tool prefix.
resolve_seed_bin() {
  local name="$1" prefix="${2:-}"
  if [[ -n "${BIN_DIR:-}" && -x "$BIN_DIR/$name" ]]; then
    readlink -f "$BIN_DIR/$name" 2>/dev/null || realpath "$BIN_DIR/$name" 2>/dev/null || echo "$BIN_DIR/$name"
    return 0
  fi
  if [[ -n "$prefix" ]]; then
    local found
    found="$(find_exec "$prefix" "$name" 2>/dev/null || true)"
    if [[ -n "$found" ]]; then
      readlink -f "$found" 2>/dev/null || realpath "$found" 2>/dev/null || echo "$found"
      return 0
    fi
  fi
  return 1
}

seed_door_settings() {
  [[ "${SEED_SETTINGS:-1}" -eq 1 ]] || {
    info "Skipping Door settings seed (--no-seed-settings)"
    return 0
  }

  migrate_libamn_conf_file
  local conf
  conf="$(door_settings_conf)"
  mkdir -p "$(dirname "$conf")"

  local schematic="" layout="" em="" room_path=""
  local qucs_bin="" xschem_bin="" klayout_bin="" em_bin=""
  local door_p qucs_p xschem_p klayout_p em_p

  door_p="${DOOR_PREFIX:-$(prefix_for door 2>/dev/null || true)}"
  qucs_p="${QUCS_PREFIX:-$(prefix_for qucs 2>/dev/null || true)}"
  xschem_p="${XSCHEM_PREFIX:-$(prefix_for xschem 2>/dev/null || true)}"
  klayout_p="${KLAYOUT_PREFIX:-$(prefix_for klayout 2>/dev/null || true)}"
  em_p="${EMSTUDIO_PREFIX:-$(prefix_for emstudio 2>/dev/null || true)}"

  if [[ -n "$door_p" && -f "$door_p/python/room/__init__.py" ]]; then
    room_path="$(cd "$door_p/python" && pwd)"
  else
    # door-run.sh may live deeper than the payload root after unpack.
    local py
    py="$(find "${door_p:-/nonexistent}" -type f -path '*/python/room/__init__.py' 2>/dev/null | head -n1 || true)"
    if [[ -n "$py" ]]; then
      room_path="$(cd "$(dirname "$(dirname "$py")")" && pwd)"
    fi
  fi

  qucs_bin="$(resolve_seed_bin qucs-s "$qucs_p" || true)"
  xschem_bin="$(resolve_seed_bin xschem "$xschem_p" || true)"
  klayout_bin="$(resolve_seed_bin klayout "$klayout_p" || true)"
  em_bin="$(resolve_seed_bin emstudio "$em_p" || true)"
  [[ -n "$em_bin" ]] || em_bin="$(resolve_seed_bin EMStudio "$em_p" || true)"

  # Prefer xschem as primary Schematic path when both exist (matches common Door setups).
  if [[ -n "$xschem_bin" ]]; then
    schematic="$xschem_bin"
  elif [[ -n "$qucs_bin" ]]; then
    schematic="$qucs_bin"
  fi
  [[ -n "$klayout_bin" ]] && layout="$klayout_bin"
  [[ -n "$em_bin" ]] && em="$em_bin"

  if [[ -z "$schematic$layout$em$room_path" ]]; then
    info "No installed tool paths to seed into Door settings"
    return 0
  fi

  # Patch only [Tools] keys so Qt Appearance blobs stay intact.
  FORCE_SEED_SETTINGS="${FORCE_SEED_SETTINGS:-0}" python3 - "$conf" \
    "$schematic" "$layout" "$em" "$room_path" \
    "$xschem_bin" "$qucs_bin" "$klayout_bin" "$em_bin" <<'PY'
import json, os, re, sys

conf, schematic, layout, em, room_path, xschem, qucs, klayout, em_bin = sys.argv[1:10]
force = os.environ.get("FORCE_SEED_SETTINGS", "0") == "1"

text = ""
if os.path.isfile(conf):
    with open(conf, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

def tools_section_body(src: str) -> str:
    m = re.search(r"(?ms)^\[Tools\]\s*\n(.*?)(?=^\[|\Z)", src)
    return m.group(1) if m else ""

body = tools_section_body(text)

def get_key(body: str, key: str) -> str:
    m = re.search(rf"(?m)^{re.escape(key)}=(.*)$", body)
    return (m.group(1).strip().strip('"') if m else "")

already = any(get_key(body, k) for k in ("Schematic", "Layout", "SchematicTools", "LayoutTools", "EM", "EMTools"))
if already and not force:
    print(f"Door settings already present at {conf} — skip seed (use --seed-settings to overwrite)")
    sys.exit(0)

def tool_json(entries):
    arr = [{"name": n, "path": p, "default": d} for n, p, d in entries if p]
    return json.dumps(arr, separators=(",", ":"))

sch_entries = []
if xschem and qucs:
    sch_entries = [("XSchem", xschem, True), ("Qucs-S", qucs, False)]
elif xschem:
    sch_entries = [("XSchem", xschem, True)]
elif qucs:
    sch_entries = [("Qucs-S", qucs, True)]

lay_entries = [("Layout", klayout, True)] if klayout else []
em_entries = [("EMStudio", em_bin, True)] if em_bin else []

updates = {}
tool_list = []
if schematic or sch_entries:
    tool_list.append("Schematic")
if layout or lay_entries:
    tool_list.append("Layout")
if em or em_entries:
    tool_list.append("EM")
if tool_list:
    updates["ToolList"] = ", ".join(tool_list)
if schematic:
    updates["Schematic"] = schematic
if layout:
    updates["Layout"] = layout
if em:
    updates["EM"] = em
if sch_entries:
    updates["SchematicTools"] = tool_json(sch_entries)
    if not get_key(body, "SchematicViews"):
        updates["SchematicViews"] = "cdl,spice,schematic,symbol"
if lay_entries:
    updates["LayoutTools"] = tool_json(lay_entries)
    if not get_key(body, "LayoutViews"):
        updates["LayoutViews"] = "gds,oas,lstr,layout"
if em_entries:
    updates["EMTools"] = tool_json(em_entries)
    if not get_key(body, "EMViews"):
        updates["EMViews"] = "emsetup"
if room_path:
    updates["RoomPath"] = room_path
if not get_key(body, "SchematicSymbolFormat"):
    updates["SchematicSymbolFormat"] = "ROOM"
if not get_key(body, "Editor"):
    updates["Editor"] = "nedit"

def upsert_tools(src: str, kv: dict) -> str:
    if not kv:
        return src
    m = re.search(r"(?ms)^(\[Tools\]\s*\n)(.*?)(?=^\[|\Z)", src)
    if not m:
        block = "[Tools]\n" + "".join(f"{k}={v}\n" for k, v in kv.items())
        if src and not src.endswith("\n"):
            src += "\n"
        if src and not src.endswith("\n\n"):
            src += "\n"
        return src + block + ("\n" if not block.endswith("\n") else "")
    head, sec = m.group(1), m.group(2)
    lines = sec.splitlines(keepends=True)
    keys_done = set()
    out_lines = []
    for line in lines:
        km = re.match(r"^([^=\n]+)=(.*)$", line.rstrip("\n"))
        if km and km.group(1) in kv:
            key = km.group(1)
            out_lines.append(f"{key}={kv[key]}\n")
            keys_done.add(key)
        else:
            out_lines.append(line if line.endswith("\n") else line + "\n")
    for key, val in kv.items():
        if key not in keys_done:
            out_lines.append(f"{key}={val}\n")
    new_sec = "".join(out_lines)
    return src[: m.start()] + head + new_sec + src[m.end() :]

new_text = upsert_tools(text, updates)
os.makedirs(os.path.dirname(conf) or ".", exist_ok=True)
with open(conf, "w", encoding="utf-8", newline="\n") as f:
    f.write(new_text)
print(f"Seeded Door settings -> {conf}")
PY
}

seed_door_settings

info "Done."
echo
echo "Prefix:  $INSTALL_PREFIX"
echo "Flavor:  $FLAVOR"
echo "PATH:    export PATH=\"$BIN_DIR:\$PATH\""
echo "Tools:   ${TOOLS[*]}"
echo "Tree:    $INSTALL_PREFIX/bin/  +  $INSTALL_PREFIX/lib/room-stack/<tool>/"
echo "Settings:$(door_settings_conf)"

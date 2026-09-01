#!/usr/bin/env bash
# Textually merge kernel config fragments into a base .config file.
#
# Usage: merge-fragments.sh <base-config> <fragment> [<fragment>...]
#
# Recognized fragment line formats (fragments are applied in order, later
# fragments win):
#   CONFIG_X=value           set X to value
#   CONFIG_X                 unset X (kernel-configurator *.unset format)
#   "# CONFIG_X is not set"  unset X (kconfig fragment format)
# Blank lines and other comments are ignored.
#
# This replicates the semantics of the OpenGamingCollective
# kernel-configurator action (*.config.set / *.config.unset fragments) and
# additionally accepts regular kconfig fragment files, so both the OGC
# fragments and the repo-local config.fragment can be handled uniformly.
# The final `make olddefconfig` (run by the PKGBUILD / RPM spec) turns the
# textual result into a consistent kconfig.

set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <base-config> <fragment>..." >&2
    exit 2
fi

CFG="$(realpath "$1")"
shift

set_key() {
    local key="$1" value="$2"
    if grep -q "^${key}=" "$CFG"; then
        sed -i "s|^${key}=.*|${key}=${value}|" "$CFG"
    elif grep -q "^# ${key} is not set$" "$CFG"; then
        sed -i "s|^# ${key} is not set\$|${key}=${value}|" "$CFG"
    else
        printf '%s=%s\n' "${key}" "${value}" >> "$CFG"
    fi
}

unset_key() {
    local key="$1"
    if grep -q "^${key}=" "$CFG"; then
        sed -i "s|^${key}=.*|# ${key} is not set|" "$CFG"
    fi
}

for frag in "$@"; do
    frag="$(realpath "$frag")"
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line#"${line%%[![:space:]]*}"}"
        line="${line%"${line##*[![:space:]]}"}"
        [ -z "$line" ] && continue
        if [[ "$line" =~ ^#\ (CONFIG_[A-Za-z0-9_]+)\ is\ not\ set$ ]]; then
            unset_key "${BASH_REMATCH[1]}"
        elif [[ "$line" =~ ^(CONFIG_[A-Za-z0-9_]+)=(.*)$ ]]; then
            set_key "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"
        elif [[ "$line" =~ ^(CONFIG_[A-Za-z0-9_]+)$ ]]; then
            unset_key "${BASH_REMATCH[1]}"
        fi
    done < "$frag"
done
#!/usr/bin/env bash
# end4pC installer (test version). Arch Linux only.
#
#   ./install.sh                 asks before doing anything
#   ./install.sh -y              no questions (what the defaults say)
#   ./install.sh --compositors hyprland,niri,sway   which compositors to set up: any of hyprland, niri, sway, or `all` (default: hyprland)
#   ./install.sh --no-packages   do not install packages (only the files and the build)
#   ./install.sh --no-dots       do not copy the dotfiles
#   ./install.sh --no-build      copy the shell's source but do not build it
#   ./install.sh --skip-aur      do not try the AUR packages
#   ./install.sh --dry-run       show what it would do, change nothing
#
# What it does, in order:
#   0. asks which compositors to set up: Hyprland, niri, sway (SwayFX) or several (or --compositors);
#   1. installs the packages of packages/pacman.txt, those of the compositors you picked (packages/compositors/), the optional programs
#      of packages/apps.txt that you pick and the ones you add (all with `pacman -S --needed`), the Bibata cursor with makepkg
#      (packages/local), and the ones of packages/aur.txt (and SwayFX, for sway) with yay or paru, also with --needed;
#   2. moves the config folders it is going to replace (kitty, btop, fish, geany, gtk-3.0, gtk-4.0, hypr, matugen, kde-material-you-colors, Kvantum) to a backup
#      folder, and copies the ones of this package to ~/.config;
#   3. puts the shell in ~/.config/end4pC (an existing git checkout is left as it is) and builds it;
#   4. sets up each compositor you picked: Hyprland with the hypr folder of step 2; niri and sway with the configs of templates/ in
#      ~/.config/niri and ~/.config/sway (an existing config can be kept: then only the lines that start the shell are added);
#   5. makes a first colour palette and writes the kitty colours (and the sway title-bar colours).
# Nothing is deleted: the old folders are in ~/.local/share/end4pC-installer/backup-<date>/.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}"
STATE_ROOT="${XDG_DATA_HOME:-$HOME/.local/share}/end4pC-installer"
STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="$STATE_ROOT/backup-$STAMP"
PROJECT="$CONFIG/end4pC"
DOTS=(kitty btop fish geany gtk-3.0 gtk-4.0 hypr matugen kde-material-you-colors Kvantum)
FILES=(starship.toml kdeglobals)  # single files of ~/.config

ASSUME_YES=0 DO_PACKAGES=1 DO_DOTS=1 DO_BUILD=1 DO_AUR=1 DRY=0
COMPS_ARG="" COMPS_GIVEN=0
args=("$@"); i=0
while [ "$i" -lt "${#args[@]}" ]; do
    arg="${args[$i]}"; i=$((i + 1))
    case "$arg" in
        --compositors) [ "$i" -lt "${#args[@]}" ] || { echo "--compositors needs a list: hyprland,niri,sway or all"; exit 2; }
                       COMPS_ARG="${args[$i]}"; COMPS_GIVEN=1; i=$((i + 1)) ;;
        --compositors=*) COMPS_ARG="${arg#--compositors=}"; COMPS_GIVEN=1 ;;
        -y|--yes) ASSUME_YES=1 ;;
        --no-packages) DO_PACKAGES=0 ;;
        --no-dots) DO_DOTS=0 ;;
        --no-build) DO_BUILD=0 ;;
        --skip-aur) DO_AUR=0 ;;
        --dry-run) DRY=1 ;;
        -h|--help) sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $arg (try --help)"; exit 2 ;;
    esac
done

if [ -t 1 ]; then
    B=$'\e[1m'; D=$'\e[2m'; G=$'\e[32m'; Y=$'\e[33m'; R=$'\e[31m'; C=$'\e[36m'; N=$'\e[0m'; COLOR=1
else B= D= G= Y= R= C= N=; COLOR=0; fi
STEP=0
say()  { printf '%s  ●%s %s\n' "$G$B" "$N" "$*"; }
info() { printf '    %s%s%s\n' "$D" "$*" "$N"; }
ok()   { printf '    %s✓%s %s\n' "$G" "$N" "$*"; }
warn() { printf '  %s▲%s %s\n' "$Y$B" "$N" "$*"; }
die()  { printf '  %s✗%s %s\n' "$R$B" "$N" "$*" >&2; exit 1; }
run()  { if [ "$DRY" = 1 ]; then printf '    %s[dry-run] %s%s\n' "$D" "$*" "$N"; else "$@"; fi; }
section() { # section "title": numbered header with a rule
    STEP=$((STEP + 1))
    local cols; cols="$(tput cols 2>/dev/null || echo 80)"; [ "$cols" -gt 78 ] && cols=78
    local title="$STEP · $*" rule
    rule="$(printf '─%.0s' $(seq 1 $((cols - ${#title} - 5))))"
    printf '\n  %s╭─ %s%s %s%s%s\n' "$C" "$B" "$title" "$N$C" "$rule" "$N"
}
ask()  { # ask "question" [default y|n]
    [ "$ASSUME_YES" = 1 ] && return 0
    local def="${2:-y}" hint reply
    [ "$def" = y ] && hint="${B}Y${N}${D}/n${N}" || hint="${D}y/${N}${B}N${N}"
    read -r -p "  ${C}?${N} $1 [$hint] " reply || reply=""
    reply="${reply:-$def}"
    [[ "$reply" =~ ^[yY] ]]
}
banner() {
    # end4pC++ in block letters, one colour per row (a gradient: 256-colour codes)
    local rows=(
' ███████╗███╗   ██╗██████╗ ██╗  ██╗██████╗  ██████╗              '
' ██╔════╝████╗  ██║██╔══██╗██║  ██║██╔══██╗██╔════╝  ██╗    ██╗  '
' █████╗  ██╔██╗ ██║██║  ██║███████║██████╔╝██║     ██████╗██████╗'
' ██╔══╝  ██║╚██╗██║██║  ██║╚════██║██╔═══╝ ██║     ╚═██╔═╝╚═██╔═╝'
' ███████╗██║ ╚████║██████╔╝     ██║██║     ╚██████╗  ╚═╝    ╚═╝  '
' ╚══════╝╚═╝  ╚═══╝╚═════╝      ╚═╝╚═╝      ╚═════╝              ')
    local grad=(51 50 49 48 47 46) i
    echo
    for i in "${!rows[@]}"; do
        if [ "$COLOR" = 1 ]; then printf '\e[38;5;%sm%s\e[0m\n' "${grad[$i]}" "${rows[$i]}"; else printf '%s\n' "${rows[$i]}"; fi
    done
    printf '\n  %sa light Wayland shell for Hyprland, niri and sway · installer (test)%s\n' "$D" "$N"
}

# ---------------------------------------------------------------------------------------------------------------- compositors
# COMPOSITORS: the ones to set up, in a fixed order (hyprland niri sway). parse_compositors "list": names separated by commas or spaces,
# or numbers (1 Hyprland, 2 niri, 3 sway, 4 all three), or `all`; returns 1 on a name it does not know.
COMPOSITORS=()
parse_compositors() {
    local want=() tok c lower
    lower="$(tr '[:upper:]' '[:lower:]' <<<"$1" | tr ',' ' ')"
    for tok in $lower; do
        case "$tok" in
            all|4) want=(hyprland niri sway) ;;
            hyprland|hypr|1) want+=(hyprland) ;;
            niri|2) want+=(niri) ;;
            sway|swayfx|3) want+=(sway) ;;
            *) return 1 ;;
        esac
    done
    COMPOSITORS=()
    for c in hyprland niri sway; do
        [[ " ${want[*]:-} " == *" $c "* ]] && COMPOSITORS+=("$c")
    done
    [ "${#COMPOSITORS[@]}" -gt 0 ]
}
has_comp() { [[ " ${COMPOSITORS[*]} " == *" $1 "* ]]; }
comp_title() { case "$1" in hyprland) echo "Hyprland" ;; niri) echo "niri" ;; sway) echo "sway (SwayFX)" ;; esac; }

# ---------------------------------------------------------------------------------------------------------------- checks
[ "$(id -u)" -ne 0 ] || die "do not run this as root: it asks for sudo only where it needs it"
command -v pacman >/dev/null 2>&1 || die "this installer is for Arch Linux (pacman was not found)"
[ -d "$HERE/dots" ] || DO_DOTS=0
if [ -f "$HERE/../meson.build" ]; then  # run from a clone of the repository: the clone has to be ~/.config/end4pC (the Hyprland, niri and sway configs point there)
    [ "$(cd "$HERE/.." && pwd -P)" = "$(readlink -f "$PROJECT" 2>/dev/null || echo "$PROJECT")" ] || die "clone the repository into ~/.config/end4pC:  git clone <repository> ~/.config/end4pC  and run ~/.config/end4pC/installer/install.sh"
elif [ ! -d "$HERE/end4pC" ] && [ ! -d "$PROJECT" ]; then die "the shell's source is not here ($HERE/end4pC) and ~/.config/end4pC does not exist"; fi

if [ "$COMPS_GIVEN" = 1 ]; then
    parse_compositors "$COMPS_ARG" || die "--compositors: use hyprland, niri, sway (separated by commas) or all (got: $COMPS_ARG)"
else
    COMPOSITORS=(hyprland)
fi

banner
printf '\n  %spackages%s %s   %sAUR%s %s   %sdotfiles%s %s   %sbuild%s %s\n' "$B" "$N" "$([ $DO_PACKAGES = 1 ] && echo yes || echo no)" "$B" "$N" "$([ $DO_AUR = 1 ] && echo yes || echo no)" "$B" "$N" "$([ $DO_DOTS = 1 ] && echo yes || echo no)" "$B" "$N" "$([ $DO_BUILD = 1 ] && echo yes || echo no)"
info "dotfiles: ${DOTS[*]}"
info "backup  : $BACKUP"
echo
[ "$DRY" = 1 ] && warn "dry run: nothing will be changed"
ask "Go on?" y || exit 0

# ---------------------------------------------------------------------------------------------------------------- which compositors
# The shell runs on Hyprland, niri and sway: pick the ones you want set up (their packages and their configs). The default is Hyprland.
if [ "$COMPS_GIVEN" != 1 ] && [ "$ASSUME_YES" != 1 ]; then
    section "Compositors"
    echo "    The shell runs on all of them. Which ones do you want set up (packages and config)?"
    printf '      %s1%s  Hyprland          %s(the default)%s\n' "$B" "$N" "$D" "$N"
    printf '      %s2%s  niri\n' "$B" "$N"
    printf '      %s3%s  sway (SwayFX)      %s(SwayFX comes from the AUR)%s\n' "$B" "$N" "$D" "$N"
    printf '      %s4%s  all three\n' "$B" "$N"
    while :; do
        read -r -p "  ${C}?${N} Numbers separated by spaces (e.g. 1 3), ${B}Enter${N} = Hyprland: " reply || reply=""
        [ -n "$reply" ] || reply=1
        parse_compositors "$reply" && break
        echo "    not understood: type numbers (1 2 3 4) or names (hyprland niri sway all)"
    done
fi
names=(); for c in "${COMPOSITORS[@]}"; do names+=("$(comp_title "$c")"); done
info "compositors: ${names[*]}"

# ---------------------------------------------------------------------------------------------------------------- fonts
# Fonts that come with the installer (fonts/<folder>/, with their licence): copied to ~/.local/share/fonts when the family is not there yet.
# A `.family` file in the folder says which family it is (what `fc-list` shows).
if [ "$DO_DOTS" = 1 ] && [ -d "$HERE/fonts" ]; then
    section "Fonts"
    fonts_now="$(fc-list 2>/dev/null || true)"  # (a variable: `fc-list | grep -q` dies of SIGPIPE under pipefail)
    for dir in "$HERE"/fonts/*/; do
        [ -d "$dir" ] || continue
        name="$(basename "$dir")"; family=""
        [ -f "$dir/.family" ] && family="$(head -1 "$dir/.family")"
        if [ -n "$family" ] && grep -qi -- "$family" <<<"$fonts_now"; then echo "    $family: already installed (skipped)"; continue; fi
        dest="${XDG_DATA_HOME:-$HOME/.local/share}/fonts/$name"
        if [ -e "$dest" ]; then echo "    $name: already in $dest (skipped)"; continue; fi
        echo "    copy  : ${family:-$name} -> $dest"
        run mkdir -p "$dest"
        run cp -a "$dir"/. "$dest"/
        run rm -f "$dest/.family"
        command -v fc-cache >/dev/null 2>&1 && run fc-cache -f "$dest" >/dev/null 2>&1 || true
    done
fi

# ---------------------------------------------------------------------------------------------------------------- packages
read_list() { grep -vE '^\s*(#|$)' "$1" | sed 's/[[:space:]]*#.*$//'; }

# pick_apps FILE: shows "package | description" lines numbered; the user picks numbers (Enter = all, n = none). Sets PICKED.
pick_apps() {
    local names=() descs=() line n d i reply
    while IFS= read -r line; do
        n="$(sed 's/[[:space:]]*|.*$//' <<<"$line")"; d="$(sed -n 's/^[^|]*|[[:space:]]*//p' <<<"$line")"
        pacman -T "$n" >/dev/null 2>&1 && continue   # already there
        names+=("$n"); descs+=("$d")
    done < <(read_list "$1")
    PICKED=()
    [ "${#names[@]}" -gt 0 ] || { ok "all the optional programs are already installed"; return; }
    if [ "$ASSUME_YES" = 1 ]; then PICKED=("${names[@]}"); return; fi
    echo "    Optional programs (not installed yet):"
    for i in "${!names[@]}"; do printf '      %s%2d%s  %-16s %s%s%s\n' "$B" $((i + 1)) "$N" "${names[$i]}" "$D" "${descs[$i]}" "$N"; done
    read -r -p "  ${C}?${N} Which ones? numbers separated by spaces, ${B}Enter${N} = all, ${B}n${N} = none: " reply || reply=""
    case "$reply" in
        "") PICKED=("${names[@]}") ;;
        n|N|none) ;;
        *) for i in $reply; do [[ "$i" =~ ^[0-9]+$ ]] && [ "$i" -ge 1 ] && [ "$i" -le "${#names[@]}" ] && PICKED+=("${names[$((i - 1))]}"); done ;;
    esac
}

if [ "$DO_PACKAGES" = 1 ]; then
    section "Packages (pacman, --needed)"
    mapfile -t wanted < <(read_list "$HERE/packages/pacman.txt")
    for c in "${COMPOSITORS[@]}"; do   # what each compositor needs (packages/compositors/<name>.txt)
        [ -f "$HERE/packages/compositors/$c.txt" ] && mapfile -t -O "${#wanted[@]}" wanted < <(read_list "$HERE/packages/compositors/$c.txt")
    done
    pick_apps "$HERE/packages/apps.txt"
    wanted+=("${PICKED[@]}")
    if [ "$ASSUME_YES" != 1 ]; then
        read -r -p "  ${C}?${N} Add other packages? names separated by spaces, ${B}Enter${N} = none: " extra || extra=""
        # shellcheck disable=SC2206
        [ -z "$extra" ] || wanted+=($extra)
    fi
    available=() missing=() provided=()
    for p in "${wanted[@]}"; do
        # `--needed` only knows the exact name: a package another one provides (adw-gtk-theme-git provides adw-gtk-theme) would be
        # installed too, and the two conflict. `pacman -T` knows what is provided, so what is already satisfied is skipped.
        if pacman -T "$p" >/dev/null 2>&1; then provided+=("$p")
        elif pacman -Si "$p" >/dev/null 2>&1; then available+=("$p")
        else missing+=("$p"); fi
    done
    [ "${#provided[@]}" -eq 0 ] || ok "already installed or provided by another package: ${#provided[@]} (skipped)"
    [ "${#missing[@]}" -eq 0 ] || warn "not in your repositories (skipped): ${missing[*]}"
    # sudo refreshes the databases only when asked: a fresh machine needs -Sy once
    if [ "${#available[@]}" -eq 0 ]; then
        info "nothing to install from the repositories"  # (pacman would stop with "no targets specified")
    elif ask "Install ${#available[@]} packages with sudo pacman -S --needed?" y; then
        if [ "$ASSUME_YES" = 1 ]; then run sudo pacman -S --needed --noconfirm "${available[@]}"
        else run sudo pacman -S --needed "${available[@]}"; fi
    fi
    # packages that are not in the AUR: the ones the Quickshell setup (dots-hyprland) builds from its own folder (packages/local/<name>/PKGBUILD)
    # `name|path:...` in packages/local/list.txt: skipped when that is already on the machine, whichever package put it there
    if [ -d "$HERE/packages/local" ]; then
        for pk in "$HERE"/packages/local/*/PKGBUILD; do
            [ -f "$pk" ] || continue
            name="$(basename "$(dirname "$pk")")"
            if pacman -T "$name" >/dev/null 2>&1 || [ -d /usr/share/icons/Bibata-Modern-Classic ] || [ -d "$HOME/.local/share/icons/Bibata-Modern-Classic" ]; then
                ok "$name: Bibata-Modern-Classic is already installed (skipped)"; continue
            fi
            say "Local package: $name (makepkg)"
            if ask "Build and install $name with makepkg -si? (the Bibata cursor theme, as the Quickshell setup does)" y; then
                tmp="$(mktemp -d)"
                cp "$pk" "$tmp/PKGBUILD"
                if [ "$DRY" = 1 ]; then info "[dry-run] (cd $tmp && makepkg -si)"
                elif [ "$ASSUME_YES" = 1 ]; then (cd "$tmp" && makepkg -si --noconfirm) || warn "$name failed: go on without it"
                else (cd "$tmp" && makepkg -si) || warn "$name failed: go on without it"; fi
                rm -rf "$tmp"
            fi
        done
    fi
    if [ "$DO_AUR" = 1 ]; then
        helper=""
        command -v yay >/dev/null 2>&1 && helper=yay
        [ -n "$helper" ] || { command -v paru >/dev/null 2>&1 && helper=paru; }
        if [ -z "$helper" ]; then
            warn "no AUR helper (yay or paru): the AUR packages are skipped (see packages/aur.txt)"
        elif ask "Try the optional AUR packages with $helper --needed?" y; then
            mapfile -t aur < <(read_list "$HERE/packages/aur.txt")
            for c in "${COMPOSITORS[@]}"; do   # SwayFX for sway (packages/compositors/<name>.aur.txt)
                [ -f "$HERE/packages/compositors/$c.aur.txt" ] && mapfile -t -O "${#aur[@]}" aur < <(read_list "$HERE/packages/compositors/$c.aur.txt")
            done
            for entry in "${aur[@]}"; do
                # "name|path:/usr/share/icons/X" or "name|font:Family": skipped when that thing is already on the machine, whichever package put it
                # there (the AUR cursor theme conflicts with the one the illogical-impulse packages install)
                p="${entry%%|*}"; hint=""
                [[ "$entry" == *"|"* ]] && hint="${entry#*|}"
                if pacman -T "$p" >/dev/null 2>&1; then ok "$p: already installed or provided by another package (skipped)"; continue; fi
                case "$hint" in
                    path:*) hp="${hint#path:}"; hp="${hp/#\~/$HOME}"  # (a leading ~ is the home folder)
                            [ -e "$hp" ] && { echo "    $p: $hp is already there (skipped)"; continue; } ;;
                    font:*) fonts="$(fc-list 2>/dev/null || true)"  # (a variable: `fc-list | grep -q` dies of SIGPIPE under pipefail)
                            grep -qi -- "${hint#font:}" <<<"$fonts" && { echo "    $p: the font ${hint#font:} is already there (skipped)"; continue; } ;;
                esac
                say "AUR: $p"
                if [ "$ASSUME_YES" = 1 ]; then run "$helper" -S --needed --noconfirm "$p" || warn "$p failed: go on without it"
                else run "$helper" -S --needed "$p" || warn "$p failed: go on without it"; fi
            done
        fi
    fi
    if has_comp sway && ! pacman -T swayfx >/dev/null 2>&1 && ! { command -v sway >/dev/null 2>&1 && sway --version 2>/dev/null | grep -qi swayfx; }; then
        warn "SwayFX is not installed (it is only in the AUR: yay -S swayfx). The sway config needs it: plain sway reports its shadow, blur and rounded-corner lines as errors."
    fi
fi

# ---------------------------------------------------------------------------------------------------------------- dotfiles
if [ "$DO_DOTS" = 1 ]; then
    section "Dotfiles"
    run mkdir -p "$BACKUP" "$CONFIG"
    for d in "${DOTS[@]}"; do
        # the hypr folder is the Hyprland setup: only when Hyprland is one of the picked compositors (niri and sway get theirs below)
        [ "$d" = hypr ] && ! has_comp hyprland && { echo "    skip  : hypr (Hyprland was not picked)"; continue; }
        [ -d "$HERE/dots/$d" ] || continue
        if [ -e "$CONFIG/$d" ]; then
            echo "    backup: ~/.config/$d -> $BACKUP/$d"
            run mv "$CONFIG/$d" "$BACKUP/$d"
        fi
        echo "    copy  : $d"
        run cp -a "$HERE/dots/$d" "$CONFIG/$d"
    done
    for f in "${FILES[@]}"; do
        [ -f "$HERE/dots/$f" ] || continue
        if [ -e "$CONFIG/$f" ]; then
            echo "    backup: ~/.config/$f -> $BACKUP/$f"
            run mv "$CONFIG/$f" "$BACKUP/$f"
        fi
        echo "    copy  : $f"
        run cp -a "$HERE/dots/$f" "$CONFIG/$f"
    done
    # the shell reads these from the state folder: make sure they are there
    run mkdir -p "$HOME/.local/state/end4pC/generated" "$HOME/.local/share/end4pC" "$HOME/.local/state/quickshell/user/generated/terminal"
    # hypr scripts have to be executable
    [ -d "$CONFIG/hypr" ] && run find "$CONFIG/hypr" -name '*.sh' -exec chmod +x {} +
fi

# ---------------------------------------------------------------------------------------------------------------- niri and sway configs
# Shared helper scripts: the niri and sway configs start apps through ~/.config/hypr/hyprland/scripts (launch_first_available.sh picks the
# first of several programs that is installed; the others start the location agent and the screen-snip search). When Hyprland itself was not
# picked its folder is not installed, so these few are put there (never over a file that exists).
install_helper_scripts() {
    local src="$HERE/dots/hypr/hyprland/scripts" dst="$CONFIG/hypr/hyprland/scripts" f
    [ -d "$src" ] || return 0
    for f in launch_first_available.sh start_geoclue_agent.sh snip_to_search.sh; do
        [ -f "$src/$f" ] || continue
        if [ -e "$dst/$f" ]; then echo "    helper: $f is already there (kept)"; continue; fi
        echo "    helper: $f -> ~/.config/hypr/hyprland/scripts/"
        run mkdir -p "$dst"
        run cp -a "$src/$f" "$dst/$f"
        run chmod +x "$dst/$f"
    done
}

# subst_home FILE...: puts this machine's home folder where a template has @HOME@
subst_home() {
    local f
    for f in "$@"; do
        [ -f "$f" ] && grep -q '@HOME@' "$f" 2>/dev/null && run sed -i "s|@HOME@|$HOME|g" "$f"
    done
    return 0
}

# append_missing FILE "comment" LINE...: adds to the end of FILE the LINEs it does not have yet (with the comment once)
append_missing() {
    local file="$1" comment="$2" line first=1
    shift 2
    for line in "$@"; do
        grep -qxF -- "$line" "$file" 2>/dev/null && continue
        if [ "$first" = 1 ]; then
            [ "$DRY" = 1 ] || printf '\n%s\n' "$comment" >> "$file"
            first=0
        fi
        echo "    add   : $line"
        [ "$DRY" = 1 ] || printf '%s\n' "$line" >> "$file"
    done
    return 0
}

# choose_mode "what exists" -> sets MODE to keep (add only the lines the shell needs) or replace (the template; the old one is backed up)
choose_mode() {
    MODE=keep
    [ "$ASSUME_YES" = 1 ] && return 0
    ask "$1 exists. Keep it and only add the lines the shell needs? ('n' puts the template of this package instead; yours is backed up)" y || MODE=replace
}

install_niri() {
    local dir="$CONFIG/niri" t="$HERE/templates/niri" f
    if [ ! -d "$t" ]; then warn "niri: this package has no templates/niri: skipped"; return 0; fi
    run mkdir -p "$dir"
    if [ ! -e "$dir/config.kdl" ]; then
        echo "    niri  : no config.kdl: the template of this package goes to ~/.config/niri"
        run cp -an "$t"/. "$dir"/ || warn "niri: some files of the template could not be copied"
    else
        choose_mode "~/.config/niri/config.kdl"
        if [ "$MODE" = replace ]; then
            echo "    backup: ~/.config/niri -> $BACKUP/niri"
            run mkdir -p "$BACKUP"
            run mv "$dir" "$BACKUP/niri"
            run mkdir -p "$dir"
            run cp -a "$t"/. "$dir"/
        else
            echo "    niri  : your config.kdl is kept; only the lines that include the shell's files are added"
            run mkdir -p "$BACKUP/niri"
            run cp -a "$dir/config.kdl" "$BACKUP/niri/config.kdl"
            for f in end4pC-colors.kdl end4pC-user.kdl; do   # (niri stops on an include that does not exist)
                [ -e "$dir/$f" ] || run cp -a "$t/$f" "$dir/$f"
            done
            append_missing "$dir/config.kdl" "// end4pC shell (added by its installer): the session environment and services, the look of its windows, the layer rules and the start of the shell, its key binds, the border colours and what Settings -> Niri changes. Later files win." \
                'include "~/.config/end4pC/niri/end4pC-session.kdl"' \
                'include "~/.config/end4pC/niri/end4pC-look.kdl"' \
                'include "~/.config/end4pC/niri/end4pC.kdl"' \
                'include "~/.config/end4pC/niri/end4pC-binds.kdl"' \
                'include "~/.config/niri/end4pC-colors.kdl"' \
                'include "~/.config/niri/end4pC-user.kdl"'
        fi
    fi
    [ "$DRY" = 1 ] || subst_home "$dir"/*.kdl
    # niri checks a config itself: say so when it refuses this one (the shell needs a niri that has `include`)
    if [ "$DRY" != 1 ] && command -v niri >/dev/null 2>&1; then
        if niri validate -c "$dir/config.kdl" >/dev/null 2>&1; then ok "niri accepts ~/.config/niri/config.kdl"
        else warn "niri does not accept ~/.config/niri/config.kdl yet (run: niri validate). The shell's files are included from ~/.config/end4pC/niri: they exist once the shell step is done."; fi
    fi
}

install_sway() {
    local dir="$CONFIG/sway" t="$HERE/templates/sway"
    if [ ! -d "$t" ]; then warn "sway: this package has no templates/sway: skipped"; return 0; fi
    if [ ! -e "$dir/config" ]; then
        echo "    sway  : no config: the template of this package goes to ~/.config/sway"
        run mkdir -p "$dir"
        run cp -an "$t"/. "$dir"/ || warn "sway: some files of the template could not be copied"
    else
        choose_mode "~/.config/sway/config"
        if [ "$MODE" = replace ]; then
            echo "    backup: ~/.config/sway -> $BACKUP/sway"
            run mkdir -p "$BACKUP"
            run mv "$dir" "$BACKUP/sway"
            run mkdir -p "$dir"
            run cp -a "$t"/. "$dir"/
        else
            echo "    sway  : your config is kept; only the lines that start the shell and give it blur are added"
            run mkdir -p "$BACKUP/sway"
            run cp -a "$dir/config" "$BACKUP/sway/config"
            local lines=("include ~/.config/end4pC/sway/end4pC.sway")
            # the shell is started once: a config (or an included file of it) that already runs `end4pC run` needs no second line
            grep -rqE 'end4pC run' "$dir" 2>/dev/null || lines+=("exec \$HOME/.config/end4pC/build/end4pC run")
            append_missing "$dir/config" "# end4pC shell (added by its installer): the blur behind its panels, and its start (\`end4pC run\` does nothing when it already runs)" "${lines[@]}"
        fi
    fi
    if [ "$DRY" != 1 ]; then
        subst_home "$dir"/* "$dir"/scripts/*
        [ -d "$dir/scripts" ] && chmod +x "$dir"/scripts/* 2>/dev/null || true
    else
        echo "    [dry-run] substitute @HOME@ and chmod +x ~/.config/sway/scripts/*"
    fi
    # sway checks a config itself (-C): on a private runtime folder and a headless backend, so it works from a TTY and never touches a running sway
    if [ "$DRY" != 1 ] && command -v sway >/dev/null 2>&1; then
        local rt out; rt="$(mktemp -d)"; chmod 700 "$rt"; out="$rt/out"
        if env -u SWAYSOCK -u WAYLAND_DISPLAY -u DISPLAY XDG_RUNTIME_DIR="$rt" WLR_BACKENDS=headless WLR_RENDERER=pixman sway -C -c "$dir/config" >"$out" 2>&1; then
            ok "sway accepts ~/.config/sway/config"
        elif grep -qE 'Error on line|Error\(s\) loading' "$out"; then
            warn "sway reports errors in ~/.config/sway/config (sway -C -c ~/.config/sway/config). Is it SwayFX? Plain sway does not know shadows, blur or corner_radius:"
            { grep -E 'Error on line' "$out" || grep -E '\[ERROR\]' "$out" | grep -v '\[wlr\]'; } | head -5 | sed 's/^[^ ]* \[ERROR\] \[[^]]*\] /      /'
        else
            info "sway: the config could not be checked from here (run: sway -C -c ~/.config/sway/config inside a session)"
        fi
        rm -rf "$rt"
    fi
}

install_compositor_configs() {
    local c
    for c in "${COMPOSITORS[@]}"; do
        case "$c" in
            hyprland) say "Hyprland: the hypr folder of the Dotfiles step (it loads ~/.config/end4pC/hyprland/end4pC.lua, which starts the shell)" ;;
            niri) say "niri: ~/.config/niri"; install_niri ;;
            sway) say "sway (SwayFX): ~/.config/sway"; install_sway ;;
        esac
    done
    if { has_comp niri || has_comp sway; } && ! has_comp hyprland; then install_helper_scripts; fi
}

# ---------------------------------------------------------------------------------------------------------------- the shell
section "The shell (~/.config/end4pC)"
if [ -d "$PROJECT/.git" ]; then
    echo "    ~/.config/end4pC is a git checkout: its source is left as it is"
elif [ -d "$HERE/end4pC" ]; then
    if [ -e "$PROJECT" ]; then
        run mkdir -p "$BACKUP"
        echo "    backup: ~/.config/end4pC -> $BACKUP/end4pC"
        run mv "$PROJECT" "$BACKUP/end4pC"
    fi
    run cp -a "$HERE/end4pC" "$PROJECT"
fi

if [ "$DO_BUILD" = 1 ]; then
    section "Build (meson + ninja)"
    if [ "$DRY" = 1 ]; then echo "    [dry-run] meson setup build && ninja -C build"
    else
        cd "$PROJECT"
        if [ -d build ]; then meson setup --reconfigure build >/dev/null; else meson setup build >/dev/null; fi
        ninja -C build
        [ -x build/end4pC ] || die "the build did not produce build/end4pC"
        # Play3r (the music player) is built only when ffmpeg, taglib and the PipeWire headers are there: give it its application-menu entry
        if [ -x build/end4pC-play3r ]; then
            "$PROJECT/scripts/install-desktop-entries.sh" >/dev/null && echo "    Play3r, Fil3s, T3xt, Imag3s and Vid3os installed: find them in the launcher, or run  end4pC play3r / fil3s / t3xt / imag3s / vid3os"
        else
            echo "    Play3r (the music player) was not built: install ffmpeg, taglib and pipewire, then run  ninja -C build"
        fi
    fi
fi

# ---------------------------------------------------------------------------------------------------------------- the command
# `end4pC` in the PATH (~/.local/bin, which the fish config adds to the PATH): `end4pC run` starts the shell on its own, `end4pC "settings"`...
if [ -x "$PROJECT/build/end4pC" ] || [ "$DRY" = 1 ]; then
    section "The end4pC command"
    run mkdir -p "$HOME/.local/bin"
    run ln -sfn "$PROJECT/build/end4pC" "$HOME/.local/bin/end4pC"
    case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *) warn "~/.local/bin is not in this shell's PATH yet (fish: it is, in the new config; bash/zsh: add it to ~/.profile)" ;; esac
fi

# ---------------------------------------------------------------------------------------------------------------- compositor configs
if [ "$DO_DOTS" = 1 ] && { has_comp niri || has_comp sway; }; then
    section "Compositor setup"
    run mkdir -p "$BACKUP"
    install_compositor_configs
fi

# ---------------------------------------------------------------------------------------------------------------- first palette
section "First colours"
if [ "$DRY" != 1 ] && command -v matugen >/dev/null 2>&1; then
    wall=""
    for dir in "$HOME/Pictures/Wallpapers" "$HOME/Pictures"; do
        [ -d "$dir" ] || continue
        wall="$(find "$dir" -maxdepth 1 -type f \( -iname '*.jpg' -o -iname '*.jpeg' -o -iname '*.png' -o -iname '*.webp' \) | sort | head -1)"
        [ -z "$wall" ] || break
    done
    if [ -z "$wall" ] && [ -f "$PROJECT/assets/wallpapers/default.jpg" ]; then  # nothing of yours: the picture that comes with the shell
        mkdir -p "$HOME/Pictures/Wallpapers"
        wall="$HOME/Pictures/Wallpapers/end4pC-default.jpg"
        [ -e "$wall" ] || cp "$PROJECT/assets/wallpapers/default.jpg" "$wall"
    fi
    if [ -n "$wall" ] && [ -x "$PROJECT/matugen/apply.sh" ]; then
        echo "    from $wall"
        "$PROJECT/matugen/apply.sh" "$wall" dark scheme-neutral >/dev/null 2>&1 || warn "matugen could not make a palette from that picture"
    else
        echo "    no wallpaper found: a green accent for now (change it in Settings -> Appearance)"
        mkdir -p "$HOME/.local/state/end4pC/generated"
        matugen color hex "#5f8f7f" --mode dark --type scheme-tonal-spot -c "$PROJECT/matugen/config.toml" >/dev/null 2>&1 \
            || warn "matugen failed: the shell will start with its built-in colours"
    fi
    # kitty reads its colours from a file the shell writes: make it now, so the first terminal is already right
    python3 -I "$PROJECT/scripts/templates.py" kitty >/dev/null 2>&1 || true
    # sway: the title bars take the shell's colours from this palette (scripts/sway-palette writes ~/.config/sway/end4pC-colors)
    if has_comp sway && [ -x "$CONFIG/sway/scripts/sway-palette" ]; then
        "$CONFIG/sway/scripts/sway-palette" >/dev/null 2>&1 && ok "sway title-bar colours made from the palette" || true
    fi
else
    echo "    skipped (dry run, or matugen is not installed)"
fi

# ---------------------------------------------------------------------------------------------------------------- done
printf '\n  %s╭─ ✓ Done %s\n\n' "$G$B" "$N"
printf '  %sNext%s\n' "$B" "$N"
n=1
if has_comp hyprland; then
    printf '    %s%s%s  Hyprland: log out and pick the Hyprland session (or start-hyprland from a TTY; inside a session: hyprctl reload).\n' "$C" "$n" "$N"
    printf '       It loads ~/.config/hypr/hyprland.lua, which starts the shell.\n'
    n=$((n + 1))
fi
if has_comp niri; then
    printf '    %s%s%s  niri: log out and pick the niri session (or niri-session from a TTY).\n' "$C" "$n" "$N"
    printf '       ~/.config/niri/config.kdl includes the shell'"'"'s files from ~/.config/end4pC/niri; `niri validate` checks it.\n'
    n=$((n + 1))
fi
if has_comp sway; then
    printf '    %s%s%s  sway (SwayFX): log out and pick the sway session, or from a TTY: ~/.config/sway/scripts/start-sway\n' "$C" "$n" "$N"
    printf '       (the script sets the environment the shell expects and runs sway; SwayFX is a separate package, see above if it was skipped).\n'
    n=$((n + 1))
fi
printf '    %s%s%s  Keys: %sSUPER%s launcher · %sSUPER+W%s wallpapers · %sSUPER+M%s media · %sSUPER+Escape%s Settings · %sSUPER+/%s Tools\n' "$C" "$n" "$N" "$B" "$N" "$B" "$N" "$B" "$N" "$B" "$N" "$B" "$N"
printf '       From a terminal: end4pC run   |   end4pC help   |   end4pC "settings"\n'
n=$((n + 1))
printf '    %s%s%s  Colours for other programs (KDE, GTK, kitty): Settings -> Appearance -> Templates.\n' "$C" "$n" "$N"
printf '\n  %sYour old config folders and files are in: %s\n' "$D" "$BACKUP"
if has_comp hyprland; then
    printf '  Some key binds of the hypr config may call scripts of other setups; they do nothing here.\n'
    printf '  The ones that matter for the shell live in ~/.config/end4pC/hyprland/end4pC.lua.\n'
fi
if has_comp niri || has_comp sway; then
    printf '  To go back to a config you had: copy it from the backup folder over ~/.config/niri or ~/.config/sway (or delete the lines the installer added: they start with "end4pC shell").\n'
fi
printf '  Stop other bars, notification daemons and wallpaper tools before the first run (waybar, mako, swaync, swww...).%s\n\n' "$N"

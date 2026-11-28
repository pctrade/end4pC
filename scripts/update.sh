#!/usr/bin/env bash
# end4pC update: brings the shell (a git checkout of ~/.config/end4pC) to the latest published version and rebuilds it. Only the shell's own
# code and assets change (this folder); the user's settings (~/.config/end4pC-user), the dotfiles and every compositor config are never touched.
#
#   end4pC update              look for a newer version; show what changed and ask before applying it
#   end4pC update --check      only say whether there is one
#   end4pC update -y           apply it without asking
#   end4pC update --rollback   go back to the version and the build that ran before the last update
# It follows the `stable` branch of the checkout's own `origin` (`main` when there is no `stable`); END4PC_BRANCH picks another.
set -euo pipefail

PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$PROJECT/build"
ROLLBACK="$BUILD/.rollback"
BINS=(end4pC end4pC-settings end4pC-tools end4pC-lock end4pC-play3r end4pC-fil3s end4pC-t3xt end4pC-imag3s end4pC-vid3os)

if [ -t 1 ]; then B=$'\e[1m'; G=$'\e[32m'; R=$'\e[31m'; N=$'\e[0m'; else B= G= R= N=; fi
die() { echo "${R}error:${N} $*" >&2; exit 1; }
say() { echo "${B}==>${N} $*"; }
git() { command git --no-pager -C "$PROJECT" "$@"; }  # (no pager: a machine without less would fail on git log)

CHECK=0 YES=0 BACK=0
for a in "$@"; do
    case "$a" in
        --check) CHECK=1 ;;
        -y|--yes) YES=1 ;;
        --rollback) BACK=1 ;;
        -h|--help) sed -n '2,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $a (try --help)" ;;
    esac
done

restore_bins() { for b in "${BINS[@]}"; do [ -f "$ROLLBACK/$b" ] && install -m755 "$ROLLBACK/$b" "$BUILD/$b"; done; return 0; }
restart_hint() { echo "Restart the shell to load it: SUPER + R, or  end4pC restart"; }
version() { tr -d '[:space:]' < "$PROJECT/VERSION" 2>/dev/null || git rev-parse --short HEAD; }

[ -d "$PROJECT/.git" ] || die "$PROJECT is not a git checkout: install with  git clone <repository> ~/.config/end4pC  and run installer/install.sh"
command -v git >/dev/null || die "git is needed"
command -v ninja >/dev/null || die "ninja is needed (the installer sets it up)"

if [ "$BACK" = 1 ]; then
    [ -f "$ROLLBACK/commit" ] || die "there is no previous version to go back to"
    git reset --hard "$(cat "$ROLLBACK/commit")" >/dev/null
    restore_bins
    say "went back to $(version)"
    restart_hint
    exit 0
fi

git remote get-url origin >/dev/null 2>&1 || die "this checkout has no 'origin' remote to update from"
say "installed: $(version). Looking for news on origin ..."
git fetch --quiet --prune origin || die "could not reach the repository (no network, or it is private)"
BRANCH="${END4PC_BRANCH:-}"
if [ -z "$BRANCH" ]; then
    if git rev-parse --verify --quiet "refs/remotes/origin/stable" >/dev/null; then BRANCH=stable; else BRANCH=main; fi
fi
git rev-parse --verify --quiet "refs/remotes/origin/$BRANCH" >/dev/null || die "origin has no branch '$BRANCH'"
TARGET="origin/$BRANCH"

BEHIND="$(git rev-list --count "HEAD..$TARGET")"
AHEAD="$(git rev-list --count "$TARGET..HEAD")"
if [ "$BEHIND" = 0 ]; then
    [ "$AHEAD" = 0 ] && say "${G}you are up to date${N} ($(version))" || say "nothing newer on $TARGET (this checkout has $AHEAD commit(s) of its own)"
    exit 0
fi
[ "$AHEAD" = 0 ] || die "this checkout has $AHEAD commit(s) that $TARGET does not: update it by hand (git pull --rebase) so they are not lost"
say "${G}$BEHIND new commit(s)${N} on $TARGET:"
git log --max-count=20 --format='  %h %s' "HEAD..$TARGET"  # (not piped into head: with pipefail the SIGPIPE ended the whole script when more than 20 commits were new)
[ "$BEHIND" -le 20 ] || echo "  ... and $((BEHIND - 20)) more"
[ "$CHECK" = 0 ] || exit 0

# Files of the shell edited here (for example binds uncommented in hyprland/end4pC.lua) would be overwritten: they are set aside with
# `git stash` once the update is confirmed, and can be brought back afterwards.
CHANGED="$(git status --porcelain --untracked-files=no)"
if [ -n "$CHANGED" ]; then
    echo "${R}These files of the shell were changed here:${N}"
    echo "$CHANGED" | sed 's/^/  /'
    echo "The update would overwrite them, so it sets them aside first (git stash) and keeps going."
fi
if [ "$YES" = 0 ]; then
    [ -t 0 ] || die "it asks before updating: run it in a terminal, or add -y"
    read -r -p "Update now? Only the shell's code and assets change; your settings and dotfiles stay. [y/N] " ans
    case "$ans" in y|Y|yes|s|S|si) ;; *) echo "nothing changed"; exit 0 ;; esac
fi
STASHED=0
if [ -n "$CHANGED" ]; then
    git stash push --quiet -m "end4pC update $(date +%F-%H%M)" || die "could not set the changed files aside (git stash)"
    STASHED=1
fi

say "keeping the current version and build for --rollback"
PREV="$(git rev-parse HEAD)"
rm -rf "$ROLLBACK"; mkdir -p "$ROLLBACK"
echo "$PREV" > "$ROLLBACK/commit"
for b in "${BINS[@]}"; do [ -f "$BUILD/$b" ] && cp -p "$BUILD/$b" "$ROLLBACK/$b"; done

say "pulling"
git merge --ff-only --quiet "$TARGET" || { [ "$STASHED" = 1 ] && git stash pop --quiet; die "the fast-forward was refused"; }

say "building"
cd "$PROJECT"
if ! { [ -d "$BUILD" ] || meson setup "$BUILD"; } || ! ninja -C "$BUILD"; then
    echo "${R}the build failed:${N} going back to the previous version" >&2
    git reset --hard "$PREV" >/dev/null
    [ "$STASHED" = 1 ] && git stash pop --quiet
    restore_bins
    exit 1
fi
say "${G}updated to $(version)${N}"
# Play3r, the music player, if the build made it: its entry in the application menu (the shell also does this by itself at its next start)
if [ -x "$BUILD/end4pC-play3r" ]; then
    "$PROJECT/scripts/install-desktop-entries.sh" >/dev/null 2>&1 && echo "Play3r, Fil3s, T3xt, Imag3s and Vid3os are in your application menu: look for them in the launcher, or run  end4pC play3r / fil3s / t3xt / imag3s / vid3os" || true
else
    echo "Play3r (the music player) was not built: it needs ffmpeg, taglib and the PipeWire headers (Arch: pacman -S ffmpeg taglib pipewire), then  end4pC update  or  ninja -C $BUILD"
fi
if [ "$STASHED" = 1 ]; then
    echo "Your changes to the shell's files were set aside, not lost. To see them:  git -C \"$PROJECT\" stash show -p"
    echo "To bring them back (it may need a hand where the same lines changed):  git -C \"$PROJECT\" stash pop"
fi
restart_hint

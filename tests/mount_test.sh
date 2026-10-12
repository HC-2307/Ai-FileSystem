#!/bin/sh
# Mounts a fresh filesystem and drives it with ordinary tools.
# usage: mount_test.sh <path to ai_fs>
set -eu
bin=$1
[ -w /dev/fuse ] && command -v fusermount3 >/dev/null || { echo "FUSE not available, skipping"; exit 77; }

dir=$(mktemp -d)
mnt=$dir/mnt
cleanup() {
    fusermount3 -u "$mnt" 2>/dev/null || true
    rm -rf "$dir"
}
trap cleanup EXIT
mkdir "$mnt"

# Stays in the foreground (-f) so unmounting can wait for it to finish closing.
mount_fs() {
    "$bin" mount "$dir/fs.img" "$dir/fs.db" "$mnt" -f "$@" &
    pid=$!
    for _ in $(seq 100); do
        mountpoint -q "$mnt" && return
        sleep 0.05
    done
    echo "mount did not appear"; exit 1
}
unmount_fs() {
    fusermount3 -u "$mnt"
    wait $pid
}

"$bin" format "$dir/fs.img" "$dir/fs.db" 2048
head -c 1500000 /dev/urandom > "$dir/big"            # 367 blocks, more than the cache holds

for version in A B C; do
    mount_fs --version $version --cache-mb 1

    work=$mnt/$version
    mkdir "$work" "$work/sub"
    echo hello > "$work/greeting"
    [ "$(cat "$work/greeting")" = hello ]
    echo replaced > "$work/greeting"                 # truncates on open
    echo more >> "$work/greeting"
    [ "$(cat "$work/greeting")" = "replaced
more" ]

    cp "$dir/big" "$work/big"
    cmp "$dir/big" "$work/big"
    cmp "$dir/big" "$work/big"                       # again, now through the cache

    mv "$work/big" "$work/sub/moved"
    [ ! -e "$work/big" ]
    cmp "$dir/big" "$work/sub/moved"
    touch "$work/empty"
    [ "$(ls "$work")" = "empty
greeting
sub" ]
    rm "$work/empty"
    ! rmdir "$work/sub" 2>/dev/null                  # not empty
    ! rm "$work/missing" 2>/dev/null

    unmount_fs
done

# Everything written above must still be there after a remount.
mount_fs
for version in A B C; do
    cmp "$dir/big" "$mnt/$version/sub/moved"
    [ "$(cat "$mnt/$version/greeting")" = "replaced
more" ]
done
rm -r "$mnt/A"
[ ! -e "$mnt/A" ]
unmount_fs
echo "mount test passed"

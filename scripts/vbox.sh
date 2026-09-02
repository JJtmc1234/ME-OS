#!/usr/bin/env bash
# Boots build/me-os.iso in VirtualBox, in a virtual machine of its own.
#
#   scripts/vbox.sh            create or update the machine and start it windowed
#   scripts/vbox.sh capture    start it headless, save a screenshot, stop it
#   scripts/vbox.sh remove     unregister and delete the machine
#
# Nothing here needs root, nothing writes to a real disk, and nothing touches a
# machine other than the one named below. The name is fixed and specific so this
# can never act on somebody else's virtual machine.
#
# The hardware is deliberately plain: no disk, no network, no audio, no USB. A
# device ME OS does not drive is a device that can only add a way to fail.
set -euo pipefail

VM="${VM:-ME-OS}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${ISO:-$ROOT/build/me-os.iso}"
SHOT="${SHOT:-$ROOT/build/vbox-screen.png}"
WAIT="${WAIT:-25}"

fail() { echo "vbox: $*" >&2; exit 1; }

command -v VBoxManage >/dev/null 2>&1 || fail "VBoxManage is not installed"
[ -f "$ISO" ] || fail "no ISO at $ISO. Run make iso first."

# Refuses to act on anything but its own machine. VirtualBox has one namespace
# for every machine on the account, and a typo here would start or delete
# something that has nothing to do with ME OS.
case "$VM" in
    ME-OS|ME-OS-*) ;;
    *) fail "this script only manages machines named ME-OS or ME-OS-something" ;;
esac

exists() { VBoxManage showvminfo "$VM" >/dev/null 2>&1; }

running() {
    VBoxManage list runningvms 2>/dev/null | grep -q "^\"$VM\""
}

stop_it() {
    if running; then
        VBoxManage controlvm "$VM" poweroff >/dev/null 2>&1 || true
        sleep 2
    fi
}

build_it() {
    if ! exists; then
        VBoxManage createvm --name "$VM" --ostype Other_64 --register >/dev/null
        echo "vbox: created the machine $VM"
    fi

    # Conservative on purpose. VBoxVGA is the adapter whose VESA modes the
    # bootloader can set on the BIOS path, which is the path VirtualBox takes
    # unless EFI is switched on. ME OS asks Limine for a linear framebuffer and
    # drives nothing else, so a cleverer adapter would only be a larger surface
    # to go wrong on.
    VBoxManage modifyvm "$VM" \
        --memory 512 \
        --cpus 1 \
        --vram 32 \
        --graphicscontroller vboxvga \
        --firmware bios \
        --nic1 none \
        --audio-driver none \
        --usb off \
        --mouse ps2 \
        --keyboard ps2 \
        --boot1 dvd --boot2 none --boot3 none --boot4 none >/dev/null

    if ! VBoxManage showvminfo "$VM" --machinereadable | grep -q '^storagecontrollername0='; then
        VBoxManage storagectl "$VM" --name IDE --add ide >/dev/null
    fi
    VBoxManage storageattach "$VM" --storagectl IDE --port 0 --device 0 \
        --type dvddrive --medium "$ISO" >/dev/null
    echo "vbox: $VM has 512 MB, one processor, no disk, no network, and $ISO"
}

case "${1:-run}" in
run)
    stop_it
    build_it
    VBoxManage startvm "$VM" --type gui
    ;;

capture)
    stop_it
    build_it
    VBoxManage startvm "$VM" --type headless >/dev/null
    echo "vbox: booting, waiting ${WAIT}s"
    sleep "$WAIT"
    VBoxManage controlvm "$VM" screenshotpng "$SHOT"
    stop_it
    [ -s "$SHOT" ] || fail "no screenshot was produced at $SHOT"
    echo "vbox: screenshot at $SHOT"
    ;;

remove)
    stop_it
    exists || { echo "vbox: $VM does not exist"; exit 0; }
    VBoxManage unregistervm "$VM" --delete >/dev/null
    echo "vbox: removed $VM"
    ;;

*)
    fail "usage: scripts/vbox.sh [run|capture|remove]"
    ;;
esac

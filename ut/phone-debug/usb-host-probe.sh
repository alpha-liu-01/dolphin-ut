#!/bin/sh
# Run in the phone's Terminal app with a USB device (controller, keyboard, stick) plugged in through
# an OTG adapter, since adb cannot be connected at the same time:
#   sh /home/phablet/dolphin-ut-probe/usb-host-probe.sh
# Writes ~/usb-host-probe.txt: Type-C roles, USB devices, input devices and the kernel log tail,
# so the evidence survives until adb is reconnected (the kernel log wraps within minutes).
out="$HOME/usb-host-probe.txt"
sleep "${1:-5}"
{
  echo "== date: $(date)  uptime: $(cut -d' ' -f1 /proc/uptime)"
  for f in /sys/class/typec/port0/data_role /sys/class/typec/port0/power_role \
           /sys/class/typec/port0-partner/accessory_mode /sys/class/typec/port0-partner/usb_power_delivery_revision; do
    [ -r "$f" ] && echo "$f: $(cat "$f")"
  done
  ls -d /sys/class/typec/port0-partner >/dev/null 2>&1 && echo "typec partner: present" || echo "typec partner: none"
  echo "== /sys/bus/usb/devices"
  for d in /sys/bus/usb/devices/*; do
    [ -r "$d/idVendor" ] && echo "$(basename "$d") $(cat "$d/idVendor"):$(cat "$d/idProduct") $(cat "$d/manufacturer" 2>/dev/null) $(cat "$d/product" 2>/dev/null) speed=$(cat "$d/speed" 2>/dev/null)"
  done
  echo "== USB interfaces and their drivers"
  for i in /sys/bus/usb/devices/*:*; do
    [ -r "$i/bInterfaceClass" ] || continue
    drv=none; [ -e "$i/driver" ] && drv=$(basename "$(readlink -f "$i/driver")")
    echo "$(basename "$i") class=$(cat "$i/bInterfaceClass") sub=$(cat "$i/bInterfaceSubClass") proto=$(cat "$i/bInterfaceProtocol") driver=$drv"
  done
  echo "== HID devices"
  for h in /sys/bus/hid/devices/*; do
    [ -e "$h" ] || continue
    drv=none; [ -e "$h/driver" ] && drv=$(basename "$(readlink -f "$h/driver")")
    echo "$(basename "$h") driver=$drv $(ls "$h" 2>/dev/null | grep -E '^(input|hidraw)' | tr '\n' ' ')"
  done
  ls -l /dev/hidraw* 2>/dev/null
  echo "== /proc/bus/input/devices"
  cat /proc/bus/input/devices
  echo "== dmesg (USB, Type-C, HID, input; touchscreen and charger noise removed; last 200)"
  dmesg | grep -vE "FTS_TS|SC-USBPD|batt_chg|sm5602|Get usb type|usb online|power_supply|cacert|PMIC input|real type =" \
        | grep -iE "usb|xhci|dwc3|typec|role|otg|vbus|hid|input|sony|xpad|joystick" | tail -200
} > "$out" 2>&1
echo "written: $out"

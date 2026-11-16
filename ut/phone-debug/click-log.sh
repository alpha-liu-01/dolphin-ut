#!/bin/sh
# The installed click's output (stage 5): its launcher cannot write a log file under confinement,
# so the output is in the session journal. Run on the phone (adb shell):
#   sh click-log.sh            the current or last run
#   sh click-log.sh -f         follow
#   sh click-log.sh --all      every run kept by the journal
export XDG_RUNTIME_DIR=/run/user/$(id -u)
unit='lomiri-app-launch--application-click--dolphin-ut*'
case "${1:-}" in
  -f) exec journalctl --user -u "$unit" -f -o cat ;;
  --all) exec journalctl --user -u "$unit" -o short ;;
  *)
    # From the last "Started" line of the app's unit.
    journalctl --user -u "$unit" -o cat | awk '/^Started lomiri-app-launch/ { n = NR } { line[NR] = $0 } END { for (i = n; i <= NR; i++) print line[i] }'
    ;;
esac

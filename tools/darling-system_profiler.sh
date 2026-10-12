#!/bin/sh
# system_profiler for the Darling prefix: reports the host's real display and
# network hardware for the data types applications query at startup.
# Installed as /usr/sbin/system_profiler by tools/install-darling-system-profiler.py.
host=/Volumes/SystemRoot
xml=0
types=
while [ $# -gt 0 ]; do
 case "$1" in
  -xml) xml=1;;
  -detailLevel|-timeout) shift;;
  -listDataTypes) printf 'Available Datatypes:\nSPAirPortDataType\nSPDisplaysDataType\nSPEthernetDataType\n'; exit 0;;
  SP*DataType) types="$types $1";;
 esac
 shift
done
[ -n "$types" ] || types=" SPDisplaysDataType SPEthernetDataType SPAirPortDataType"

escape() { sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'; }

pci_name() { # vendor device (hex without 0x) -> "Vendor Model"
 for ids in "$host/usr/share/hwdata/pci.ids" "$host/usr/share/misc/pci.ids"; do
  [ -r "$ids" ] || continue
  awk -v v="$1" -v d="$2" '
   /^[0-9a-f][0-9a-f][0-9a-f][0-9a-f]  / { found = ($1 == v); if (found) { vendor = $0; sub(/^[0-9a-f]+  /, "", vendor) } next }
   found && /^\t[0-9a-f][0-9a-f][0-9a-f][0-9a-f]  / && $1 == d { model = $0; sub(/^\t[0-9a-f]+  /, "", model); exit }
   END {
    if (v == "1002") vendor = "AMD"; else if (v == "10de") vendor = "NVIDIA"; else if (v == "8086") vendor = "Intel"
    if (match(model, /\[[^]]*\]/)) model = substr(model, RSTART + 1, RLENGTH - 2)
    if (vendor != "" && model != "") print vendor " " model; else if (vendor != "") print vendor " Graphics"
   }' "$ids"
  return
 done
}

displays() { # one line per GPU: vendor|device|vram MB or empty|name
 for card in "$host"/sys/class/drm/card[0-9] "$host"/sys/class/drm/card[0-9][0-9]; do
  [ -r "$card/device/vendor" ] || continue
  vendor=$(sed 's/^0x//' "$card/device/vendor"); device=$(sed 's/^0x//' "$card/device/device")
  vram=
  if [ -r "$card/device/mem_info_vram_total" ]; then
   vram=$(awk '{ printf "%d", $1 / 1048576 }' "$card/device/mem_info_vram_total")
  fi
  name=$(pci_name "$vendor" "$device"); [ -n "$name" ] || name="Display"
  echo "$vendor|$device|$vram|$name"
 done
}

interfaces() { # $1 = wired|wireless; physical host interfaces only
 for path in "$host"/sys/class/net/*; do
  [ -e "$path/device" ] || continue
  if [ -d "$path/wireless" ] || [ -e "$path/phy80211" ]; then kind=wireless; else kind=wired; fi
  [ "$kind" = "$1" ] && basename "$path"
 done
}

if [ "$xml" = 1 ]; then
 echo '<?xml version="1.0" encoding="UTF-8"?>'
 echo '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'
 echo '<plist version="1.0">'
 echo '<array>'
 for type in $types; do
  echo ' <dict>'
  echo "  <key>_dataType</key><string>$type</string>"
  echo '  <key>_items</key>'
  echo '  <array>'
  case "$type" in
   SPDisplaysDataType)
    displays | while IFS='|' read -r vendor device vram name; do
     name=$(printf '%s' "$name" | escape)
     echo '   <dict>'
     echo "    <key>_name</key><string>$name</string>"
     echo '    <key>spdisplays_bus</key><string>spdisplays_builtin</string>'
     echo "    <key>spdisplays_device-id</key><string>0x$device</string>"
     echo "    <key>spdisplays_vendor</key><string>${name%% *} (0x$vendor)</string>"
     [ -z "$vram" ] || echo "    <key>spdisplays_vram</key><string>$vram MB</string>"
     echo "    <key>sppci_model</key><string>$name</string>"
     echo '    <key>sppci_device_type</key><string>spdisplays_gpu</string>'
     echo '   </dict>'
    done;;
   SPEthernetDataType)
    interfaces wired | while read -r name; do
     echo '   <dict>'
     echo '    <key>_name</key><string>ethernet</string>'
     echo "    <key>spethernet_BSD_Name</key><string>$name</string>"
     echo '   </dict>'
    done;;
   SPAirPortDataType)
    echo '   <dict>'
    echo '    <key>spairport_airport_interfaces</key>'
    echo '    <array>'
    interfaces wireless | while read -r name; do
     echo '     <dict>'
     echo "      <key>_name</key><string>$name</string>"
     echo '      <key>spairport_wireless_card_type</key><string>Wi-Fi</string>'
     echo '     </dict>'
    done
    echo '    </array>'
    echo '   </dict>';;
  esac
  echo '  </array>'
  echo ' </dict>'
 done
 echo '</array>'
 echo '</plist>'
 exit 0
fi

for type in $types; do
 case "$type" in
  SPDisplaysDataType)
   printf 'Graphics/Displays:\n\n'
   displays | while IFS='|' read -r vendor device vram name; do
    printf '    %s:\n\n      Chipset Model: %s\n      Type: GPU\n      Bus: Built-In\n' "$name" "$name"
    [ -z "$vram" ] || printf '      VRAM (Total): %s MB\n' "$vram"
    printf '      Vendor: %s (0x%s)\n      Device ID: 0x%s\n\n' "${name%% *}" "$vendor" "$device"
   done;;
  SPEthernetDataType)
   printf 'Ethernet Cards:\n\n'
   interfaces wired | while read -r name; do
    printf '    ethernet:\n\n      Type: Ethernet Controller\n      BSD name: %s\n\n' "$name"
   done;;
  SPAirPortDataType)
   printf 'Wi-Fi:\n\n      Interfaces:\n'
   interfaces wireless | while read -r name; do
    printf '        %s:\n          Card Type: Wi-Fi\n\n' "$name"
   done;;
 esac
done

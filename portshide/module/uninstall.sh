#!/system/bin/sh
# Called by Magisk/KSU when the module is uninstalled. Removes our
# iptables rules so no dangling REJECT entries survive.

CHAIN4="vpnhide_out"
CHAIN6="vpnhide_out6"

iptables  -D OUTPUT -j "$CHAIN4" 2>/dev/null
iptables  -F "$CHAIN4"         2>/dev/null
iptables  -X "$CHAIN4"         2>/dev/null

ip6tables -D OUTPUT -j "$CHAIN6" 2>/dev/null
ip6tables -F "$CHAIN6"         2>/dev/null
ip6tables -X "$CHAIN6"         2>/dev/null

log -t vpnhide_ports "uninstalled, iptables chains removed"

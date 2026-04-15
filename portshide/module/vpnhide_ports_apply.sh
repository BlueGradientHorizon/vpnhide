#!/system/bin/sh
# Reads /data/adb/vpnhide_ports/observers.txt (one UID per line) and
# installs iptables REJECT rules that block each observer UID from
# reaching any port on 127.0.0.1 / ::1. Used for hiding locally-bound
# VPN/proxy daemons from apps that probe via connect(127.0.0.1, PORT).
#
# Callable from service.sh at boot, and from the VPN Hide app via su.
# Idempotent: flushes our chain and rebuilds atomically via
# iptables-restore --noflush. Jump from OUTPUT is added only if missing.

OBSERVERS_FILE="/data/adb/vpnhide_ports/observers.txt"
CHAIN4="vpnhide_out"
CHAIN6="vpnhide_out6"

# Read + validate UIDs into a newline-separated string.
UIDS=""
if [ -f "$OBSERVERS_FILE" ]; then
    while IFS= read -r line || [ -n "$line" ]; do
        uid="$(echo "$line" | tr -d '[:space:]')"
        [ -z "$uid" ] && continue
        case "$uid" in
            \#*) continue ;;
            *[!0-9]*) continue ;;
        esac
        # Guard against nonsense UIDs (system reserved range or obvious bogus).
        [ "$uid" -lt 10000 ] && continue
        [ "$uid" -gt 2147483647 ] && continue
        if [ -z "$UIDS" ]; then UIDS="$uid"; else UIDS="${UIDS}
${uid}"; fi
    done < "$OBSERVERS_FILE"
fi

# Build an iptables-restore ruleset for a given chain + loopback destination.
# stdout = ready-to-pipe-into-iptables-restore payload.
# UDP reject differs by family: `icmp-port-unreachable` on IPv4 vs
# `icmp6-port-unreachable` on IPv6.
build_ruleset() {
    chain="$1"
    loopback="$2"
    udp_reject="$3"
    echo "*filter"
    echo ":${chain} - [0:0]"
    if [ -n "$UIDS" ]; then
        echo "$UIDS" | while IFS= read -r uid; do
            [ -z "$uid" ] && continue
            echo "-A ${chain} -m owner --uid-owner ${uid} -d ${loopback} -p tcp -j REJECT --reject-with tcp-reset"
            echo "-A ${chain} -m owner --uid-owner ${uid} -d ${loopback} -p udp -j REJECT --reject-with ${udp_reject}"
        done
    fi
    echo "-A ${chain} -j RETURN"
    echo "COMMIT"
}

# Ensure our chains exist before restore tries to replace them (restore
# with :chain - [0:0] requires the chain to already exist, otherwise it
# creates it on some versions but not all — safer to explicitly -N).
iptables  -N "$CHAIN4" 2>/dev/null || true
ip6tables -N "$CHAIN6" 2>/dev/null || true

# Apply IPv4 ruleset.
build_ruleset "$CHAIN4" "127.0.0.1" "icmp-port-unreachable" | iptables-restore --noflush
rc4=$?
# Apply IPv6 ruleset.
build_ruleset "$CHAIN6" "::1" "icmp6-port-unreachable" | ip6tables-restore --noflush
rc6=$?

# Ensure OUTPUT jumps into our chain (exactly once).
iptables  -C OUTPUT -j "$CHAIN4" 2>/dev/null || iptables  -I OUTPUT -j "$CHAIN4"
ip6tables -C OUTPUT -j "$CHAIN6" 2>/dev/null || ip6tables -I OUTPUT -j "$CHAIN6"

count=0
[ -n "$UIDS" ] && count=$(echo "$UIDS" | wc -l)
log -t vpnhide_ports "applied rules: ${count} observer(s), rc4=${rc4} rc6=${rc6}"

# Non-zero exit if either restore failed so the caller (APK) can surface it.
[ "$rc4" = 0 ] && [ "$rc6" = 0 ]

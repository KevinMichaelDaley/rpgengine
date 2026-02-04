#!/usr/bin/env bash
set -euo pipefail

# Creates a veth pair between the root namespace and a network namespace,
# so you can run localhost-style tests under tc-netem.
#
# Default topology:
#   root ns:  veth-host0 10.200.1.1/24
#   netns:    veth-ns0   10.200.1.2/24
#
# Usage:
#   scripts/netem_localhost_ns.sh up
#   scripts/netem_localhost_ns.sh netem delay 30ms 5ms loss 0.1% rate 200mbit
#   scripts/netem_localhost_ns.sh clear
#   scripts/netem_localhost_ns.sh ping
#   scripts/netem_localhost_ns.sh down
#
# Overrides (env):
#   NS, HOST_IF, NS_IF, HOST_CIDR, NS_CIDR

NS="${NS:-netem1}"
HOST_IF="${HOST_IF:-veth-host0}"
NS_IF="${NS_IF:-veth-ns0}"
HOST_CIDR="${HOST_CIDR:-10.200.1.1/24}"
NS_CIDR="${NS_CIDR:-10.200.1.2/24}"

HOST_IP="${HOST_CIDR%/*}"
NS_IP="${NS_CIDR%/*}"

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "error: missing required command: $1" >&2
    exit 1
  }
}

require_root() {
  if [ "${EUID:-$(id -u)}" -ne 0 ]; then
    exec sudo -E "$0" "$@"
  fi
}

netns_exists() {
  ip netns list | awk '{print $1}' | grep -Fxq "$NS"
}

link_exists() {
  ip link show dev "$1" >/dev/null 2>&1
}

cmd_up() {
  require_cmd ip
  require_cmd tc

  modprobe sch_netem >/dev/null 2>&1 || true

  if ! netns_exists; then
    ip netns add "$NS"
  fi

  if ! link_exists "$HOST_IF"; then
    ip link add "$HOST_IF" type veth peer name "$NS_IF"
  fi

  # Move peer into the namespace (safe to retry).
  if link_exists "$NS_IF"; then
    ip link set "$NS_IF" netns "$NS" || true
  fi

  # Addressing + up.
  ip addr show dev "$HOST_IF" | grep -Fq " $HOST_IP" || ip addr add "$HOST_CIDR" dev "$HOST_IF"
  ip link set "$HOST_IF" up

  ip -n "$NS" link set lo up
  ip -n "$NS" addr show dev "$NS_IF" | grep -Fq " $NS_IP" || ip -n "$NS" addr add "$NS_CIDR" dev "$NS_IF"
  ip -n "$NS" link set "$NS_IF" up

  echo "up=1 ns=$NS host_if=$HOST_IF host_ip=$HOST_IP ns_if=$NS_IF ns_ip=$NS_IP"
  echo "client_in_ns_example: ip netns exec $NS <client> $HOST_IP <PORT> ..."
}

cmd_netem() {
  require_cmd ip
  require_cmd tc

  if ! netns_exists; then
    echo "error: netns '$NS' does not exist (run: $0 up)" >&2
    exit 1
  fi
  if ! link_exists "$HOST_IF"; then
    echo "error: host link '$HOST_IF' does not exist (run: $0 up)" >&2
    exit 1
  fi

  if [ "$#" -lt 1 ]; then
    echo "error: netem requires args, e.g.: $0 netem delay 30ms 5ms loss 0.1% rate 200mbit" >&2
    exit 1
  fi

  # Egress shaping both ways.
  tc qdisc replace dev "$HOST_IF" root netem "$@"
  ip netns exec "$NS" tc qdisc replace dev "$NS_IF" root netem "$@"

  echo "netem=1 args=$*"
}

cmd_clear() {
  require_cmd ip
  require_cmd tc

  tc qdisc del dev "$HOST_IF" root 2>/dev/null || true
  if netns_exists; then
    ip netns exec "$NS" tc qdisc del dev "$NS_IF" root 2>/dev/null || true
  fi

  echo "clear=1"
}

cmd_ping() {
  require_cmd ip
  require_cmd ping

  if ! netns_exists; then
    echo "error: netns '$NS' does not exist (run: $0 up)" >&2
    exit 1
  fi

  ping -c 2 "$NS_IP"
  ip netns exec "$NS" ping -c 2 "$HOST_IP"

  echo "ping_ok=1"
}

cmd_down() {
  require_cmd ip

  if netns_exists; then
    # Deleting the namespace removes the veth inside it; the host end disappears too.
    ip netns del "$NS"
  fi

  # If the host end still exists for some reason, delete it.
  if link_exists "$HOST_IF"; then
    ip link del "$HOST_IF" || true
  fi

  echo "down=1"
}

usage() {
  cat <<EOF
Usage:
  $0 up
  $0 netem <tc-netem args...>
  $0 clear
  $0 ping
  $0 down

Examples:
  $0 up
  $0 netem delay 30ms 5ms loss 0.1% rate 200mbit
  $0 ping
  $0 clear
  $0 down
EOF
}

main() {
  local cmd="${1:-}"
  shift || true

  case "$cmd" in
    up|netem|clear|ping|down)
      require_root "$cmd" "$@"
      ;;
  esac

  case "$cmd" in
    up) cmd_up "$@" ;;
    netem) cmd_netem "$@" ;;
    clear) cmd_clear "$@" ;;
    ping) cmd_ping "$@" ;;
    down) cmd_down "$@" ;;
    ""|-h|--help|help) usage ;;
    *)
      echo "error: unknown command: $cmd" >&2
      usage
      exit 2
      ;;
  esac
}

main "$@"

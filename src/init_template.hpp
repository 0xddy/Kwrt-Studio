#pragma once
inline const char* initTemplate = R"AX_INIT(#!/bin/sh
# Local network defaults. No proxy package or proxy-core settings are changed.
. /lib/functions.sh
@@SETTINGS@@

[ "$(cat /tmp/sysinfo/board_name 2>/dev/null)" = 'xiaomi,redmi-router-ax6000-stock' ] || exit 1
[ "$(uci -q get network.wan.device)" = 'wan' ] || exit 1
[ "$(uci -q get network.lan.device)" = 'br-lan' ] || exit 1
[ -f /etc/shadow ] || exit 1

uci set network.lan.proto='static' || exit 1
uci set "network.lan.ipaddr=$LAN_IP" || exit 1
uci set "network.lan.netmask=$LAN_MASK" || exit 1
if [ "$LOCAL_ROUTING_MODE" = 'side' ]; then
    uci set "network.lan.gateway=$SIDE_GATEWAY" || exit 1
    uci -q delete network.lan.dns
    [ -n "$SIDE_DNS" ] || SIDE_DNS="$SIDE_GATEWAY"
    for local_dns in $SIDE_DNS; do uci add_list "network.lan.dns=$local_dns" || exit 1; done
    uci set network.wan.proto='none' || exit 1
    uci set network.wan.auto='0' || exit 1
    uci -q delete network.wan.username
    uci -q delete network.wan.password
    uci -q delete network.lan.ip6assign
    uci set network.wan6='interface' || exit 1
    uci set network.wan6.device='@wan' || exit 1
    uci set network.wan6.proto='dhcpv6' || exit 1
    uci set network.wan6.auto='0' || exit 1
    uci -q get dhcp.lan >/dev/null || exit 1
    if [ "$SIDE_DHCP" = '1' ]; then
        uci set dhcp.lan.ignore='0' || exit 1
        uci set dhcp.lan.force='1' || exit 1
        uci -q delete dhcp.lan.dhcp_option
        uci add_list "dhcp.lan.dhcp_option=3,$LAN_IP" || exit 1
        uci add_list "dhcp.lan.dhcp_option=6,$LAN_IP" || exit 1
    else
        uci set dhcp.lan.ignore='1' || exit 1
    fi
    uci set dhcp.lan.ra='disabled' || exit 1
    uci set dhcp.lan.dhcpv6='disabled' || exit 1
    uci commit dhcp || exit 1
    [ "$(uci -q get firewall.@zone[0].name)" = 'lan' ] || exit 1
    uci set firewall.@zone[0].masq='1' || exit 1
    uci commit firewall || exit 1
else
    if [ "$(uci -q get wizard.default.siderouter)" = '1' ]; then
        uci -q delete network.lan.gateway
        uci -q delete network.lan.dns
        uci -q delete dhcp.lan.ignore
        uci -q delete wizard.default.dhcp
        uci -q delete wizard.default.lan_gateway
        uci -q delete wizard.default.lan_dns
        uci commit dhcp || exit 1
        [ "$(uci -q get firewall.@zone[0].name)" = 'lan' ] || exit 1
        uci -q delete firewall.@zone[0].masq
        uci commit firewall || exit 1
    fi
    uci set network.wan.auto='1' || exit 1
    uci set network.wan.proto='pppoe' || exit 1
    uci set "network.wan.username=$PPPOE_USER" || exit 1
    uci set "network.wan.password=$PPPOE_PASS" || exit 1
    uci set "network.wan.ipv6=$IPV6" || exit 1
    uci set network.wan6='interface' || exit 1
    uci set network.wan6.device='@wan' || exit 1
    uci set network.wan6.proto='dhcpv6' || exit 1
    uci set "network.wan6.auto=$IPV6" || exit 1
fi
uci commit network || exit 1

if [ -n "$LOCAL_HOSTNAME" ]; then
    uci -q get system.@system[0] >/dev/null || exit 1
    uci set "system.@system[0].hostname=$LOCAL_HOSTNAME" || exit 1
    uci commit system || exit 1
fi
if [ "$REMOVE_AUTHOR_LINKS" = '1' ]; then
    uci -q get base_config.@status[0] >/dev/null || exit 1
    uci set base_config.@status[0].links='0' || exit 1
    uci commit base_config || exit 1
fi

sed -i "s|^root:[^:]*|root:$ROOT_HASH|" /etc/shadow || exit 1

if [ "$LOCAL_ROUTING_MODE" = 'router' ]; then
/sbin/wifi config >/dev/null 2>&1 || exit 1
config_load wireless
AP_2G=''
AP_5G=''
find_ap() {
    local section="$1" dev band mode net
    config_get dev "$section" device
    config_get mode "$section" mode
    config_get net "$section" network
    [ "$mode" = 'ap' ] || return 0
    case " $net " in *' lan '*) ;; *) return 0 ;; esac
    config_get band "$dev" band
    case "$band" in
        2g) RADIO_2G="$dev"; AP_2G="$section" ;;
        5g) RADIO_5G="$dev"; AP_5G="$section" ;;
    esac
}
config_foreach find_ap wifi-iface
[ -n "$AP_2G" ] && [ -n "$AP_5G" ] || exit 1
set_ap() {
    uci set "wireless.$1.disabled=0" || return 1
    uci set "wireless.$1.country=$COUNTRY" || return 1
    uci set "wireless.$2.disabled=0" || return 1
    uci set "wireless.$2.ssid=$3" || return 1
    uci set "wireless.$2.encryption=psk2" || return 1
    uci set "wireless.$2.key=$WIFI_KEY" || return 1
}
set_ap "$RADIO_2G" "$AP_2G" "${SSID}_2.4G" || exit 1
set_ap "$RADIO_5G" "$AP_5G" "${SSID}_5G" || exit 1
uci commit wireless || exit 1

fi

# Keep KWRT's setup wizard consistent with these defaults.
if [ -f /etc/config/wizard ]; then
    uci -q get wizard.default >/dev/null || uci set wizard.default=wizard
    if [ "$LOCAL_ROUTING_MODE" = 'side' ]; then
        uci set wizard.default.siderouter='1'
        uci set wizard.default.old_siderouter='1'
        uci set "wizard.default.lan_gateway=$SIDE_GATEWAY"
        uci set "wizard.default.lan_dns=$SIDE_DNS"
        if [ "$SIDE_DHCP" = '1' ]; then uci -q delete wizard.default.dhcp; else uci set wizard.default.dhcp='0'; fi
        uci set wizard.default.wan_proto='none'
        uci -q delete wizard.default.wan_pppoe_user
        uci -q delete wizard.default.wan_pppoe_pass
    else
        uci set wizard.default.siderouter='0'
        uci set wizard.default.old_siderouter='0'
        uci set "wizard.default.wifi_ssid=$SSID"
        uci set "wizard.default.old_wifi_ssid=$SSID"
        uci set "wizard.default.wifi_key=$WIFI_KEY"
        uci set "wizard.default.old_wifi_key=$WIFI_KEY"
        uci set "wizard.default.wan_proto=pppoe"
        uci set "wizard.default.wan_pppoe_user=$PPPOE_USER"
        uci set "wizard.default.wan_pppoe_pass=$PPPOE_PASS"
    fi
    uci set "wizard.default.lan_ipaddr=$LAN_IP"
    uci set "wizard.default.lan_netmask=$LAN_MASK"
    uci set "wizard.default.ipv6=$IPV6"
    uci set "wizard.default.old_ipv6=$IPV6"
    uci commit wizard || exit 1
fi
logger -t local-firstboot 'Local network defaults applied.'
exit 0
)AX_INIT";

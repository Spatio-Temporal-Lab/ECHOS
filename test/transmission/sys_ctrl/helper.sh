#!/bin/bash

INTERFACE="eth0"
LIMIT="8000kbit"

function set_bandwidth_limit {
    sudo tc qdisc add dev $INTERFACE root handle 1: htb default 30
    sudo tc class add dev $INTERFACE parent 1: classid 1:1 htb rate $LIMIT
    sudo tc filter add dev $INTERFACE protocol ip parent 1:0 prio 1 u32 match ip src 0.0.0.0/0 flowid 1:1
    echo "Bandwidth limit set to $LIMIT/s on $INTERFACE"
}

function remove_bandwidth_limit {
    sudo tc qdisc del dev $INTERFACE root
    echo "Bandwidth limit removed from $INTERFACE"
}

case "$1" in
    set)
        set_bandwidth_limit
        ;;
    rm)
        remove_bandwidth_limit
        ;;
    *)
        echo "Usage: $0 {set|rm}"
        exit 1
        ;;
esac

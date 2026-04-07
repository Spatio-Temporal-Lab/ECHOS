# Streaming Transmission Simulation Experiment

This experiment contains two parts:
- System control (Modify network bandwidth)
- Client / Server program

## System Control

Guides on system control are listed below:

1. Check current network bandwidth between two machines.
   1. Run `iperf -s` on the server.
   2. Run `iperf -c ${SERVER_IP_ADDR} -f K` on the client. `${SERVER_IP_ADD}` should be replaced with the real IP 
      address of the server.
   3. Ensure that the network bandwidth is wider than 1000KBytes/s (The upper bound of this experiment)
2. Add a new root queue scheduler. 
   1. Run `sudo tc qdisc add dev $INTERFACE root handle 1: htb default 30`. `$INTERFACE` should be replaced with your 
      transmission device.
3. Create a class for this scheduler to limit bandwidth
    1. Run `sudo tc class add dev $INTERFACE parent 1: classid 1:1 htb rate $LIMIT`. `$INTERFACE` should be replaced 
      with your transmission device. And $LIMIT should be replaced with the bandwidth you want. 
4. Add a filter to network device to apply limit
   1. Run `sudo tc filter add dev $INTERFACE protocol ip parent 1:0 prio 1 u32 match ip src 0.0.0.0/0 flowid 1:1`. 
      `$INTERFACE` should be replaced with your transmission device.

We also offer a helper shell script for you at `sys_ctrl/helper.sh`

## Client / Server Program

Run two programs on each side and you will get the total transmission time.

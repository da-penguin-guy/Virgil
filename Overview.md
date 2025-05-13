# Protocol Overview
Virgil is composed of 5 types of communication. All will be formatted as JSON files.
Virgil will also use mDNS to find other virgil devices.  
The virgil port is 7889.

# mDNS
The service type will be "_virgil._udp.local."
The service name will be the "{dante name}.{service type}"
The service port will be 7889.
Before advertising over mDNS, all slave devices must first scan for virgil devices and choose a unique muticast address.
In the txt, there will be a multicast address without the last number, eg. 244.1.1  
This is used to have multicast IDs unique to each slave device. There is not a specified way to determine the multicast ID. You can do the lowest number, a random link local approach, or something else. 
There will also be a variable called "function" that will be either "master" "slave" or "both"  
This is currently unused but may be displayed in a debug software.

# Commands
Commands are typically sent from a master device (mixer, computer, etc.) to a slave device (Digital stagebox, Preamp, etc.)  
Commands only contain the information being updated in the slave device.  
For example, a mixer changing a stagebox's gain would only contain the gain value for one preamp, instead of containing pad, phantom power, etc.  
Commands are sent using TCP to ensure no packets are dropped.  

# Status Updates
Status updates are sent from a slave device to all subscribed master devices whenever a property in a preamp is updated.  
Status updates contain all information for the preamp the change was made to.  
For example, if a mixer changes the gain on preamp 5 on a digital stagebox, a status update will be sent with all information for preamp 5.  
Status updates are multicast.

# Info Message
Info Messages are similar to status updates, with a few key differences.  
Info messages are requested via an Info Request and sent to a single master device.  
They are also able to give data on all slave preamps, if requested to.  
Info Messages are communicate the capabilities of each preamp, such as gain ranges, precision, etc. 
Info Messages are sent via TCP.

# Error Message
Error Messages are sent whenever a command was unable to be processed.  
This could be if a master attempts to change a fixed gain, sends an unrecognized command, or many other situations.  
Error packets are TCP.
Error packets have 2 values, the error type and error string.  
Currently, the error values are:
- UnrecognizedCommand
- ValueOutOfRange
- InvalidValueType
- UnableToChangeValue

Error Strings are text strings that should be shown to the end user. These do not have specified values.

# Info Requests
Info requests are sent from a master to a slave to request an Info Message.  
They contain an array of the preamp indecies that should be sent.
Info Messages, once requested, are sent to the IP address that requested it.
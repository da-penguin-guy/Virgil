# Protocol Overview
Virgil is composed of 5 types of communication. All will be formatted as JSON files.
Virgil will also use mDNS to find other virgil devices.  
The virgil port is 7889.

# mDNS
The service type will be "_virgil._udp.local."
The service name will be the "{dante name}.{service type}"
The service port will be 7889.
Before advertising over mDNS, all slave devices must first scan for virgil devices and choose a unique multicast address.
In the txt, there will be a multicast address without the last number, eg. 244.1.1  
This is used to have multicast IDs unique to each slave device. There is not a specified way to determine the multicast ID. You can do the lowest number, a random link local approach, or something else. 
There will also be a variable called "function" that will be either "master" "slave" or "both"  
This is currently unused but may be displayed in a debug software.

# Parameters
There are currently several Parameters currently supported by Virgil.  
The only mandatory parameter is gain. If a slave device supports a feature that has been disabled, include the corresponding parameter and lock it.  
Only the value of a parameter can be changed by a master device, not precision, min value, etc.  

Note : If a value type is percentage, the actual type is an int from 0 to 100, the string should be "%", precision is always 1, min value is always 0, and max value is always 100

Note : For precision, Values not equal to 1 will be counted from the min value. 
For example, if the precision is 3dB and the min value is -5dB, the available options are -5, -2, 1, 4, etc.

## Gain
"gain"  
Gain is the analog gain of the preamp (Ignoring pad).  
Unit : dB  
Value Type : Int/Float  
Min : The minimum gain (Ignoring pad)  
Precision : How precise the gain control is in dB (Int/Float).  
This value can be locked if this device does not have variable gain or has been disabled.  
The gain value should be independent of the pad, and the master device should change the visual output to the user.

## Pad
"pad"  
Pad is an optional attenuator.  
Value Type: bool  
Pad Level: The amount in dB that the pad effects the gain. This will most often be negative (Int/Float).

## LowCut
"lowcut"  
LowCut is a control of a low cut/HPF  
Unit : Hz  
Value Type : Int  
Precision: How precise the gain control is in hz (Int).  

## Polarity
"polarity"  
Polarity, if true, inverts the signal.  
Value Type : bool

## Phantom Power
"phantomPower"  
Enables Phantom Power.
Value Type : bool

## RF Power
"rfPower"  
The RF level of a connected RF receiver. Useful for dante connected mic receivers.  
Unit : dB/Percent  
Value Type : int(dB)/int(Percent, 0-100)  
This value is read-only, and should always be locked

## RF Enable
"rfEnable"  
An enable/disable switch for a connected RF transmitter.  
Value Type : bool

## Battery Level
"batteryLevel"  
The detected battery level of the end device. Useful for dante connected mic receivers.  
Unit : Percent  
Value Type : int(0-100)
This value is read-only, and should always be locked


# Formatting Overview
All Messages will be JSON files.  

Messages also have 2 strings dictating the routing for the packet. This is to help devices with routing.  

` "sendingDevice" ` Is a string stating the dante name of the device sending the message.  
All messages should have this unless the device is not Dante, such as a  computer running controller software.  

` "receivingDevice" ` Is a string stating the dante name of the device receiving the message.  
All messages should have this unless they are multicast, such as Status Updates  

` "messages" ` is an array containing all of the messages being sent.  
Several messages can be sent at once.  

The first line of all messages should be  ` "messageType" `

# Commands
Commands are typically sent from a master device (mixer, computer, etc.) to a slave device (Digital stagebox, Preamp, etc.)  
Commands only contain the information being updated in the slave device.  
For example, a mixer changing a stagebox's gain would only contain the gain value for one preamp, instead of containing pad, phantom power, etc.  
Commands are sent using TCP to ensure no packets are dropped.  

Look at the example JSON or the python script for more specific information.

# Status Updates
Status updates are sent from a slave device to all subscribed master devices whenever a property in a preamp is updated.  
Status updates contain all information for the preamp the change was made to.  
This only includes the values for the parameters. If other data changes, such as if a parameter's min value has changed, that should also be sent.
For example, if a mixer changes the gain on preamp 5 on a digital stagebox, a status update will be sent with all information for preamp 5.  
Status updates are multicast.

Look at the example JSON or the python script for more specific information.

# Info Message
Info Messages are similar to status updates, with a few key differences.  
Info messages are requested via an Info Request and sent to a single master device.  
They are also able to give data on all slave preamps, if requested to.  
Info Messages are communicate the capabilities of each preamp, such as gain ranges, precision, etc. 
Info Messages are sent via TCP.

Look at the example JSON or the python script for more specific information.

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
- Custom

Error Strings are text strings that should be shown to the end user. These do not have specified values.

Look at the example JSON or the python script for more specific information.

# Info Requests
Info requests are sent from a master to a slave to request an Info Message.  
They contain an array of the preamp indices that should be sent.
Info Messages, once requested, are sent to the IP address that requested it.

Look at the example JSON or the python script for more specific information.
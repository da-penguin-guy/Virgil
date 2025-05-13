# Protocol Overview
Virgil is composed of 3 types of communication. All will be formatted as JSON files.

# Commands
Commands are typically sent from a master device (mixer, computer, etc.) to a slave device (Digital stagebox, Preamp, etc.)  
Commands only contain the information being updated in the slave device.  
For example, a mixer changing a stagebox's gain would only contain the gain value for one preamp, instead of containing pad, phantom power, etc.  
Commands are sent using TCP to ensure no packets are dropped.  
Status updates should be sent after commands are processed, regardless of the command.

# Status Updates
Status updates are sent from a slave device to all subscribed master devices whenever a property in a preamp is updated.  
Status updates contain all information for the preamp the change was made to.  
For example, if a mixer changes the gain on preamp 5 on a digital stagebox, a status update will be sent with all information for preamp 5.  
Status updates are multicast.

# Information Message
Information Messages are similar to status updates, with a few key differences.  
Information messages are requested via a command and sent to a single master device.  
They are also able to give data on all slave preamps, if requested to.  
Information Messages are communicate the capabilities of each preamp, such as gain ranges, precision, etc. 
Information Messages are sent via TCP.

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
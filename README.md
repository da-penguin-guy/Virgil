# Virgil Protocol 1.1.0

Virgil is a network protocol for controlling audio devices using JSON-formatted messages over UDP. It uses mDNS for device discovery and supports real-time parameter control and status monitoring.

**This is Virgil Protocol 1.1.0**

The rest of this document used the terms "master" and "slave". Masters are typically devices that will be issuing commands, such a mixer. Slaves are typically devices that will be reciving commands, such as digital stageboxes or wireless recivers.
Slaves are almost always dante transmitters, but some devices may not be. Go to [Reciving Slaves] for more information on how to handle them

## General Flow

### Slave Flow

**When booting**
1. Listen for [mDNS Slave Packets](#mdns-overview) for 5 seconds
2. Pick the lowest avaiable [multicast address](#picking-a-multicast-address)
3. Advertise [mDNS](#mdns-overview). You should do this until shutdown.
4. Do all the dante protocol things

**To power off**
1. Send an mDNS goodbye message

**Reciving Slaves**
In some cases, primarily IEM transmitters, a dante reciving channel will be a virgil slave. In this case, the slave must tell their master to connect. The flow for this is the same as general slave flow, except whenever you subscribe to a dante transmitter you should:

1. Listen for [mDNS Master Packets](#mdns-overview) for 5 seconds
2. If the device that you're connecting to shows up, send a [Connect Command](#connectcommand) to the master

When indexing reciving channels, add the number of transmitting channels your device has first. For example, if I had a stagebox with 16 transmitting channels, reciving channel 2 would be channelIndex 17

**This section is not intended for regular outputs (e.g. digital stage boxes)**
Right now, the only supported type of reciving slaves are wireless transmitters

### Master Flow
1. Advertise [mDNS]. You should do this until shutdown.
4. Do all the dante protocol things
2. Whenever you subscribe to a dante transmitter, you should

    1. Listen for [mDNS Master Packets](#mdns-overview) for 5 seconds
    2. If the device that you're connecting to shows up, send a [Parameter Request](#parameterrequest) for the information needed
    3. Subscribe to the [multicast addresses](#picking-a-multicast-address) of the channel

3. If you recive a ConnectCommand, link the provided channel to that device

**To power off**
1. Send an mDNS goodbye message


## mDNS Overview

Virgil uses a typical mDNS configuration. It is advertsed over `224.0.0.251` at port `5353`  

### Service Configuration
- **serviceType**: `_virgil._udp.local.`
- **serviceName**: `{dante name}._virgil._udp.local.`
- **txt**
  + **multicastAddress**: Unique multicast address prefix (e.g., `244.1.1`) See [Multicast Address](#statusupdate) for more information
  + **function**: Device role (`master`, `slave`, or `both`)
  + **model**: The model of the device
  + **deviceType**: The type of virgil device it is. For more information and allowed values, go to [Device Types](#device-types)

**Example mDNS Service Advertisement:**
```jsonc
{
  "serviceType": "_virgil._udp.local.",
  "serviceName": "ExampleSlave._virgil._udp.local.",
  "txt": {
    "multicastAddress": "244.1.1", //Don't include if function is "master"
    "function": "slave",
    "model": "PreampModelName",
    "deviceType": "digitalStageBox"
  }
}
```
mDNS isn't actually json, but this is the best way I could represent it


---

## Message Types

| Type               | Description                                    | Direction             | Protocol   |
|--------------------|------------------------------------------------|-----------------------|------------|
| ParameterCommand   | Set or change a parameter on a device/channel  | Master → Slave        | UDP        |
| StatusUpdate       | Notify parameter or device state change         | Slave → Masters (all) | Multicast  |
| ParameterRequest   | Request device or channel parameter capabilities| Master → Slave        | UDP        |
| ParameterResponse  | Reply with parameter capabilities               | Slave → Master        | UDP        |
| ErrorResponse      | Indicate request error with details             | Slave → Master        | UDP        |

### Message Usage
- **ParameterCommand**: Change gain, pad, phantom power, etc.
- **StatusUpdate**: Automatic notifications when parameters change (sent by slaves)
- **ParameterRequest**: Discover what parameters a device supports
- **ParameterResponse**: Reply to parameter capability requests
- **ErrorResponse**: Report invalid commands, out-of-range values, etc.

---

## Parameters

Virgil supports various audio parameters. Only `gain` is mandatory for all devices to ensure masters can always read the gain value, even on fixed-gain devices.

### Parameter Structure

Each parameter is a JSON object with the following fields:

- `value`: Current parameter value (only field changeable by masters)
- `dataType`: Data type (`number`, `bool`, `string`, `enum`)
- `unit` (Required for number): Unit of measurement (e.g., `dB`, `Hz`, `%`)
- `minValue` (Required for number): Minimum allowed value
- `maxValue` (Required for number): Maximum allowed value  
- `precision` (Required for number): Step size
- `locked`: If true, parameter cannot be changed
- `enumValues` (enum only): Array of valid string values

### Data Type Notes

- **Enum**: Represented as an array of valid string values in `enumValues`
- **Percentage**: Use `%` as unit with `minValue: 0`, `maxValue: 100`, `dataType: "number"`, and `precision : 1`
- **Precision**: Values increment from `minValue` by `precision` steps (e.g., precision 3dB, minValue -5dB = -5, -2, 1, 4...)

The formula to see if a value is valid is:
```python
  isValid = (value - minValue) % precision and value > minValue and value < maxValue
```

### Example: Enum Parameter
```jsonc
{
  "transmitPower": {
    "value": "low",
    "enumValues": ["low", "medium", "high"],
    "dataType": "enum",
    "locked": false
  }
}
```


---

## Supported Parameters

### Control Parameters

#### gain (Required)
Analog gain of the channel (independent of pad)
- **unit**: dB
- **dataType**: number
- **minValue**: Device-specific minimum gain
- **maxValue**: Device-specific maximum gain
- **locked**: true if gain is fixed or disabled

Example:
```jsonc
"gain" : {
  "unit" : "dB",
  "dataType" : "number",
  "minValue" : -10,
  "maxValue" : 50,
  "value" : 10,
  "precision" : 1, // Can be higher or lower, depending on your device's precision. For example, 0.1
  "locked" : false 
}
```  

Alternative example with fixed gain

```jsonc
"gain" : {
  "unit" : "dB",
  "dataType" : "number",
  "minValue" : 30,
  "maxValue" : 30,
  "precision" : 1,
  "value" : 30,
  "locked" : true
}
``` 

#### pad
Input attenuator control
- **dataType**: bool (true = enabled, false = disabled)
- **Additional**: `padLevel` field specifies attenuation amount in dB (usually negative)
- **locked**: true if pad cannot be changed

Example:
```jsonc
"pad" : {
  "dataType" : "bool",
  "value" : false,
  "locked" : false,
  "padLevel" : -10 //This means that enableing the pad drops the input by 10dB
}
``` 

#### lowcut
High-pass filter frequency
- **unit**: hz
- **dataType**: number
- **minValue**: Device-specific minimum frequency
- **maxValue**: Device-specific maximum frequency
- **precision**: Precision of the frequency selection 
- **locked**: true if filter cannot be changed
- **Notes**: Only include this is your device has a physical lowcut circuit. Do not include this if it's a dsp lowcut

Example:
```jsonc
"lowcut" : {
  "dataType" : "number",
  "unit" : "hz",
  "minValue" : 0,
  "maxValue" : 150,
  "precison" : 1,
  "value" : 100,
  "locked" : false,
}
``` 

#### lowcutEnable
Enable/disable high-pass filter
- **dataType**: bool (true = enabled, false = disabled)
- **locked**: true if control cannot be changed
- **Notes**: If your device has lowcut, it must also have lowcutEnable

Example:
```jsonc
"lowcutEnable" : {
  "dataType" : "bool",
  "value" : true,
  "locked" : false,
}
``` 

#### polarity
Signal polarity (phase invert)
- **dataType**: bool (true = inverted, false = normal)
- **locked**: true if polarity cannot be changed

Example:
```jsonc
"polarity" : {
  "dataType" : "bool",
  "value" : true,
  "locked" : false,
}
``` 

#### phantomPower
Phantom power control
- **dataType**: bool (true = enabled, false = disabled)
- **locked**: true if phantom power cannot be changed

Example:
```jsonc
"phantomPower" : {
  "dataType" : "bool",
  "value" : false,
  "locked" : false,
}
``` 

#### rfEnable
RF transmitter/receiver control
- **dataType**: bool (true = enabled, false = disabled)
- **locked**: true if RF cannot be changed

Example:
```jsonc
"rfEnable" : {
  "dataType" : "bool",
  "value" : true,
  "locked" : false,
}
``` 

#### transmitPower
Transmitter power level (typically for IEM systems)
- **dataType**: enum/number
- **unit**: % (only include if dataType is number)
- **locked**: true if transmit power cannot be changed

Example:
```jsonc
"transmitPower" : {
  "dataType" : "enum",
  "enumValues" : ["low", "medium", "high"],
  "value" : true,
  "locked" : false,
}
``` 

Example with percentage:
```jsonc
"transmitPower" : {
  "dataType" : "number",
  "unit" : "%",
  "minValue": 0,
  "maxValue": 100,
  "precision": 1,
  "value" : 100,
  "locked" : false,
}
``` 

#### squelch
Squelch threshold (mutes audio/RF below threshold)
- **unit**: dB or % (device-dependent)
- **dataType**: number
- **minValue**: Device-specific minimum threshold
- **maxValue**: Device-specific maximum threshold
- **locked**: true if squelch cannot be changed

Example:
```jsonc
"squelch" : {
  "dataType" : "number",
  "unit" : "dB",
  "minValue": 30, //I'm sorry, I have no clue what the range would be
  "maxValue": 50,
  "precision": 1,
  "value" : 40,
  "locked" : false,
}
``` 

Example with percentage:
```jsonc
"squelch" : {
  "dataType" : "number",
  "unit" : "%",
  "minValue": 0,
  "maxValue": 100,
  "precision": 1,
  "value" : 100,
  "locked" : false,
}
``` 

### Status Parameters (Read-Only)

#### transmitterConnected
Wireless transmitter connection status
- **dataType**: bool
- **locked**: Always true (read-only)

Example:
```jsonc
"transmitterConnected" : {
  "dataType" : "bool",
  "value" : false,
  "locked" : true,
}
``` 

#### subDevice
Type of device connected to this channel
- **dataType**: string
- **locked**: Always true (read-only)
- **Valid Values**: `handheld`, `beltpack`, `gooseneck`, `iem`, `xlr`, `trs`, `disconnected`, `other`

Example:
```jsonc
"subDevice" : {
  "dataType" : "string",
  "value" : "handheld",
  "locked" : true,
}
``` 

### Continuous Parameters (Real-Time Monitoring)

These parameters change frequently and require status updates every 500ms:

#### audioLevel
Audio signal level
- **unit**: dBFS
- **dataType**: number
- **locked**: Always true (read-only)
- **Note**: Because these are read-only, you don't need to provide precision, minValue, or maxValue

Example:
```jsonc
"audioLevel" : {
  "dataType" : "number",
  "unit" : "dBFS",
  "value" : -18,
  "locked" : true,
}
``` 

#### rfLevel
RF signal strength
- **unit**: dB or %
- **dataType**: number
- **locked**: Always true (read-only)
- **Note**: Because these are read-only, you don't need to provide precision, minValue, or maxValue

Example:
```jsonc
"rfLevel" : {
  "dataType" : "number",
  "unit" : "dB",
  "value" : 40,
  "locked" : true,
}
``` 

Example with percentage:
```jsonc
"rfLevel" : {
  "dataType" : "number",
  "unit" : "%",
  "value" : 100,
  "locked" : true,
}
``` 

#### batteryLevel
Connected device battery level
- **unit**: %
- **dataType**: number
- **locked**: Always true (read-only)
- **Note**: Because these are read-only, you don't need to provide precision, minValue, or maxValue

Example:
```jsonc
"batteryLevel" : {
  "dataType" : "number",
  "unit" : "%",
  "value" : 40,
  "locked" : true,
}
``` 

### Continuous Parameter Status Update Example
```jsonc
{
  "transmittingDevice": "SlaveDanteDeviceName",
  "messages": [
    {
      "messageType": "StatusUpdate",
      "channelIndex": 0,
      "audioLevel": -12,
      "rfLevel": 85 ,
      "batteryLevel": 67
    }
  ]
}
```


---

## Multicast Addressing

### Overview
- Each slave device advertises a unique multicast address prefix (e.g., `244.1.1`) via mDNS and Parameter Responses.
- The slave then broadcasts StatusUpdates over their multicast address (port 7889)
- To receive status updates for a specific channel, append the channel index to the prefix:
  - Channel 0: `244.1.1.0`
  - Channel 1: `244.1.1.1`
  - Channel N: `244.1.1.N`
- There is no multicast address for all channels at once. Masters must subscribe to each channel’s multicast address individually to receive updates for all channels.

### Picking a Multicast address
**It is very important that no 2 devices have the same multicast address.**  
This means that before any slave advertises via mDNS, it must first listen on mDNS for all current slaves for a minimum of 5 seconds  
The slave then takes the lowest valid address that is not taken  
Multicasts are send using ASM (Traditonal multicast). This means that the range of multicast addresses are from `224.1.1` to `239.255.255`

**Summary:**
Slaves broadcast status updates for each channel on its own multicast address. Masters listen to the relevant addresses to receive real-time updates. This design allows efficient, channel-specific monitoring and control.

---

## Device Information

### Device Types
Valid values for the `deviceType` parameter:
- `digitalStageBox`
- `wirelessReceiver`
- `wirelessTransmitter`
- `wirelessCombo`
- `mixer`
- `dsp`
- `computer`

### Device-Level Parameters
These parameters are only present in device-level responses (`channelIndex: -1`):

- **model**: Device model name (string, read-only)
- **deviceType**: Device category (string, read-only)
- **virgilVersion**: Protocol version being used (string, read-only)
- **channelIndices**: Array of available channel indices (array, read-only)
- **multicastAddress**: Unique multicast address prefix for the device (string, read-only)

### Channel Indices
The `channelIndices` array lists all controllable channels. Most devices use sequential numbering (e.g., `[0,1,2,3,4,5,6,7]` for 8 channels).

However, some channels may not be controllable (e.g., auxiliary inputs on mixers). If channel 5 of an 8-channel device is not controllable, the array would be `[0,1,2,3,4,6,7]`.

---

## Message Details

### ParameterCommand
Sent by master to slave to update parameters.
- **Protocol**: UDP
- **Content**: Only includes parameters being changed
- **Target**: Specific slave device


### StatusUpdate
Sent by slave devices to all masters when parameters change or for real-time monitoring.
- **Protocol**: Multicast UDP
- **Content**: All values for affected channels
- **Trigger**: Sent automatically when parameters change or every 500ms for continuous parameters (e.g., audioLevel, rfLevel, batteryLevel)

See [Multicast Addressing](#multicast-addressing) for more information on multicast messages

### ParameterRequest
Sent by master to slave to discover device capabilities.
- **Protocol**: UDP
- **Channel Index Values**:
  - `-1`: Device-level information only
  - `0` to `N-1`: Specific channel information
  - `-2`: Both device and all channel information
- **Response**: ParameterResponse message

### ParameterResponse
Sent by slave to master in response to ParameterRequest.
- **Protocol**: UDP
- **Content**: Complete parameter definitions for requested channels/device
- **Requirements**: Must include all supported parameters with full metadata

### ConnectCommand
Sent from a slave to a master to tell the master to connect
This is only used when some reciving channels on the device are virgil compatable (IEM Transmitters) 
- **Protocol**: UDP
- **Content**: The channels on both ends

### ErrorResponse
Sent when a command cannot be processed.
- **Protocol**: UDP
- **Content**: Error code and human-readable description

#### Error Types
- `UnrecognizedCommand`: Unknown message type
- `ValueOutOfRange`: Parameter value outside allowed range
- `InvalidValueType`: Wrong data type for parameter
- `UnableToChangeValue`: Parameter cannot be modified currently
- `DeviceNotFound`: Target device not available
- `ChannelIndexInvalid`: Channel does not exist
- `ParameterLocked`: Parameter is read-only or disabled
- `ParameterUnsupported`: Parameter not supported by device
- `MalformedMessage`: Invalid JSON or message structure
- `Busy`: Device cannot process request currently
- `Timeout`: Request timed out
- `PermissionDenied`: Insufficient privileges
- `InternalError`: Device internal error
- `OutOfResources`: Device resource exhaustion
- `NetworkError`: Network communication problem
- `Custom:Description`: Custom error (replace Description with specific details)

---

## Implementation Notes

- **Error Handling**: Always provide clear, user-friendly error messages in `errorString`
- **Parameter Discovery**: Masters should query device capabilities before attempting to control parameters
- **Continuous Monitoring**: Implement 500ms update intervals for continuous parameters
- **Network Resilience**: Handle device disconnections and reconnections gracefully
- **Example Files**: Reference the `Example JSON/` directory for complete message examples

---

## See Also

- Example JSON message files in the `Example JSON/` directory
- Master and Slave example implementations in respective directories
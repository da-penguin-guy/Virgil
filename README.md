# Virgil Protocol 2.0.0

Virgil is a network protocol for controlling audio devices using JSON-formatted messages over TCP. It uses mDNS for Virgil Controller and supports real-time parameter control and status monitoring.

**This is Virgil Protocol 2.0.0**

In previous versions of the virgil protocol, devices were categorized in Master/Slave/Server/Client. That no longer exists

# Communication

All networked communication should occur using the UTF-8 encoding.

At the start of every message should be a 4 byte unsigned big endian integer indicating the length of the message in bytes (excluding the integer itself)

TCP sockets should open when the devices connect and only close once one of the devices goes offline

**Watch out for race conditions when managing TCP socket connections.** If two devices attempt to connect simultaneously, ensure your code properly handles conflicting connections.

## TCP Implementation Notes

- Use non-blocking sockets with proper buffering for message reassembly
- Implement proper length-prefixed message framing to handle partial receives
- Handle connection timeouts and disconnections gracefully
- Process complete JSON messages only after receiving the full payload


# mDNS Overview

Virgil uses a typical mDNS configuration. It is advertised over `224.0.0.251` at port `5353`  
The protocol will (almost) fully function on a network that doesn't support mDNS.
mDNS is only used for Virgil Controller.

### Service Configuration
- **serviceType**: `_virgil._tcp_.local.`
- **serviceName**: `{dante name}._virgil._tcp_.local.`
- **txt**
  + **model**: The model of the device (eg. M32)
  + **deviceType**: The type of device it is. For more information and allowed values, go to [Device Types](#device-types)


# Message Types

| Type                | Description                                              | Protocol|
|---------------------|----------------------------------------------------------|---------|
| parameterCommand    | Used to set or change a parameter on a channel           | TCP     |
| statusUpdate        | Used to notify devices of a changed parameter            | TCP     |
| statusRequest       | Used to request a status update                          | TCP     |
| channelLink         | Used to represent that 2 channels are linked in dante    | TCP     |
| channelUnlink       | Used to undo a channelLink                               | TCP     |
| infoRequest		      | Used to request information on a device/channel          | TCP	   |
| infoResponse 		    | Used to respond to an infoRequest					               | TCP     |
| errorResponse       | Used to convey an error				                           | TCP     |
| subscribeMessage    | Used to tell a device to inform the sender of any parameter changes for a given channel         | TCP     |
| unsubscribeMessage  | Used to undo a subscribeMessage                          | TCP     |
| endResponse         | Used to say that the sender has no response              | TCP     |

## Message Flow Patterns

### Connection Establishment
1. Device connects via TCP to another device's Virgil port
2. First message should include `transmittingDevice` to identify sender
3. Devices may exchange `infoRequest`/`infoResponse` messages for discovery
4. Channel linking occurs via `channelLink` messages

### Parameter Control Flow
1. Send `parameterCommand` to change a parameter value
2. Receiving device validates the command and updates the parameter
3. Device sends `statusUpdate` to all subscribed devices (except sender)
4. Use `subscribeMessage`/`unsubscribeMessage` to manage subscriptions

### Communication Session Management
- Messages are exchanged in request-response patterns
- Use `endResponse` to indicate end of communication session
- Handle `errorResponse` messages for error conditions
- Maintain connection state and handle disconnections gracefully

### Example with example devices
Device1 has 2 rx and 2 tx, device2 has 2 rx and 2 tx

1. Device1 sends infoRequest with index -1
2. Device2 sends infoResponse with index -1
3. Device1 sends infoRequest for tx 0 and tx 1
4. Device2 sends infoResponse for tx 0 and tx 1
5. Device1 sends channelLink for Device1 rx 0 -> Device2 tx 0 and Device1 rx 1 -> Device2 tx 1
6. Device2 sends statusUpdate for tx 0 and tx 1 because `linkedChannels` was changed
7. Device1 sends statusUpdate for rx 0 and rx 1 because `linkedChannels` was changed
8. Device2 sends infoRequest with index -1
9. Device1 sends infoResponse with index -1
10. Device2 sends infoRequest for tx 0 and tx 1
11. Device1 sends infoResponse for tx 0 and tx 1
12. Device2 sends an endResponse 


# Channel Types
There are 3 types of channels. They are `tx`, `rx`, and `aux`  

`tx` and `rx` channels correspond with Dante transmitting and receiving channels. Every dante channel must have a corresponding Virgil channel  
For example, if a dante device had 12 transmitting channels and 8 receiving channels, the device would advertise over virgil that it has 12 `tx` channels and 8 `rx` channels.

tx/rx channels can be linked together to represent the flow of audio via Dante. For more information, go to [Linking Channels](#linking-channels)  

Aux channels do not have a Dante equivalent. They are instead used for sending simple values to a device (Think an in-wall dial connected to a mixer)  
Aux channels can be linked to a device, not another channel. For more information, go to [Linking Channels](#linking-channels)  

Channel indices start at 0

## Linking Channels
Channels are linked together to represent the flow of data via Dante. If channels are subscribed in Dante, they must be linked in virgil

Linking to a channel automatically subscribes each device to its corresponding channel. 

Rx channels can only be linked to Tx channels and vice versa. 

Either side can initiate the link by sending a channelLink.  

Aux channels can only be linked to another device. Only the device can initiate the link.

# Parameters

Virgil supports various audio parameters. `gain` is mandatory for all channels involving a preamp, even on fixed-gain preamps.

There is also a `linkedChannels` parameter. This is to be treated as any other parameter, but it is mandatory.  
It is an array containing information on what channels are linked to said channel.  
This is an array to account for tx channels being able to be connected to several rx channels in dante.  
`linkedChannels` should start empty and be added to as channels are linked/unlinked


```jsonc
"linkedChannels" : [
  {
    "deviceName" : "connectedDeviceName",
    "channelIndex" : 0, // The connected channel index
    "channelType" : "rx"
  },
  {
    "deviceName" : "otherConnectedDevice",
    "channelIndex" : 1, // The connected channel index
    "channelType" : "rx"
  },
]
```

If the channel is an aux channel:
```jsonc
"linkedChannels": [
  {
    "deviceName" : "connectedDeviceName"
  }
]
```

### Parameter Structure

Each parameter is a JSON object with the following fields:

- `value`: Current parameter value (only field changeable by servers)
- `dataType`: Data type (`number`, `bool`, `string`, `enum`)
- `unit` (Required for numbers not readonly): Unit of measurement (e.g., `dB`, `Hz`, `%`)
- `minValue` (Required for numbers not readonly): Minimum allowed value
- `maxValue` (Required for numbers not readonly): Maximum allowed value  
- `precision` (Required for numbers not readonly): Step size
- `readOnly`: If true, parameter cannot be changed
- `enumValues` (enum only): Array of valid string values

### Data Type Notes

- **Locked**: Below, I use the word "locked" often. This means if the control of said parameter is disabled for whatever reason. Don't include a parameter that your device doesn't have
- **Enum**: Represented as an array of valid string values in `enumValues`
- **Percentage**: Use `%` as unit with `minValue: 0`, `maxValue: 100`, `dataType: "number"`, and `precision : 1`
- **Precision**: Values increment from `minValue` by `precision` steps (e.g., precision 3dB, minValue -5dB = -5, -2, 1, 4...)

The formula to check if a value is valid is:
```python
isValid = (value - minValue) % precision == 0 and value >= minValue and value <= maxValue
```


Example Enum Parameter:
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
- **readOnly**: true if gain is fixed or disabled

Example:
```jsonc
"gain" : {
  "unit" : "dB",
  "dataType" : "number",
  "minValue" : -10,
  "maxValue" : 50,
  "value" : 10,
  "precision" : 1, // Can be higher or lower, depending on your device's precision. For example, 0.1
  "readOnly" : false 
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
  "readOnly" : true
}
``` 

#### pad
Input attenuator control
- **dataType**: bool (true = enabled, false = disabled)
- **readOnly**: true if pad cannot be changed

Example:
```jsonc
"pad" : {
  "dataType" : "bool",
  "value" : false,
  "readOnly" : false
}
``` 

#### padLevel
The amount enabling the pad lowers the signal
- **dataType**: number (usually negative)
- **unit**: dB
- **readOnly**: Always True (I guess a variable pad is possible, but I've never heard of a preamp with one. Will update if needed)
- **Note**: Required if channel has `pad` variable

Example:
```jsonc
"padLevel" : {
  "dataType" : "number",
  "unit": "dB",
  "value" : -10,
  "readOnly" : true
}
``` 

#### lowcut
High-pass filter frequency
- **unit**: hz
- **dataType**: number
- **minValue**: Device-specific minimum frequency
- **maxValue**: Device-specific maximum frequency
- **precision**: Precision of the frequency selection 
- **readOnly**: true if filter cannot be changed
- **Notes**: Only include this is your device has a physical lowcut circuit. Do not include this if it's a dsp lowcut

Example:
```jsonc
"lowcut" : {
  "dataType" : "number",
  "unit" : "hz",
  "minValue" : 0,
  "maxValue" : 150,
  "precision" : 1,
  "value" : 100,
  "readOnly" : false
}
``` 

#### lowcutEnable
Enable/disable high-pass filter
- **dataType**: bool (true = enabled, false = disabled)
- **readOnly**: true if control cannot be changed
- **Notes**: If your device has lowcut, it must also have lowcutEnable

Example:
```jsonc
"lowcutEnable" : {
  "dataType" : "bool",
  "value" : true,
  "readOnly" : false
}
``` 

#### polarity
Signal polarity (phase invert)
- **dataType**: bool (true = inverted, false = normal)
- **readOnly**: true if polarity is locked (Don't include this if your device can't change polarity)

Example:
```jsonc
"polarity" : {
  "dataType" : "bool",
  "value" : true,
  "readOnly" : false
}
``` 

#### phantomPower
Phantom power control
- **dataType**: bool (true = enabled, false = disabled)
- **readOnly**: true if phantom power is locked

Example:
```jsonc
"phantomPower" : {
  "dataType" : "bool",
  "value" : false,
  "readOnly" : false
}
``` 

#### rfEnable
RF transmitter/receiver control
- **dataType**: bool (true = enabled, false = disabled)
- **readOnly**: true if RF is locked

Example:
```jsonc
"rfEnable" : {
  "dataType" : "bool",
  "value" : true,
  "readOnly" : false
}
``` 

#### transmitPower
Transmitter power level (typically for IEM systems)
- **dataType**: enum/number
- **unit**: % (only include if dataType is number)
- **readOnly**: true if transmit power is locked

Example:
```jsonc
"transmitPower" : {
  "dataType" : "enum",
  "enumValues" : ["low", "medium", "high"],
  "value" : "low",
  "readOnly" : false
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
  "readOnly" : false
}
``` 

#### squelch
Squelch threshold (mutes audio/RF below threshold)
- **unit**: dB or % (device-dependent)
- **dataType**: number
- **minValue**: Device-specific minimum threshold
- **maxValue**: Device-specific maximum threshold
- **readOnly**: true if squelch is locked

Example:
```jsonc
"squelch" : {
  "dataType" : "number",
  "unit" : "dB",
  "minValue": 30, //I'm sorry, I have no clue what the range would be
  "maxValue": 50,
  "precision": 1,
  "value" : 40,
  "readOnly" : false
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
  "readOnly" : false
}
``` 

### Status Parameters (Read-Only)

#### deviceConnected
Wireless device connection status
- **dataType**: bool
- **readOnly**: Always true (read-only)

Example:
```jsonc
"deviceConnected" : {
  "dataType" : "bool",
  "value" : false,
  "readOnly" : true
}
``` 

#### subDevice
Type of device connected to this channel
- **dataType**: string
- **readOnly**: Always true (read-only)
- **Valid Values**: `handheld`, `beltpack`, `gooseneck`, `iem`, `xlr`, `trs`, `disconnected`, `other`

Example:
```jsonc
"subDevice" : {
  "dataType" : "string",
  "value" : "handheld",
  "readOnly" : true
}
``` 

### Continuous Parameters (Real-Time Monitoring)
These parameters change frequently and require status updates every 500ms  

#### audioLevel
Audio signal level
- **unit**: dBFS
- **dataType**: number
- **readOnly**: Always true (read-only)
- **Note**: Because these are read-only, you don't need to provide precision, minValue, or maxValue

audioLevel can be for tx or rx.

Example:
```jsonc
"audioLevel" : {
  "dataType" : "number",
  "unit" : "dBFS",
  "value" : -18,
  "readOnly" : true
}
``` 

#### rfLevel
RF signal strength
- **unit**: dB or %
- **dataType**: number
- **readOnly**: Always true (read-only)
- **Note**: Because these are read-only, you don't need to provide precision, minValue, or maxValue

Example:
```jsonc
"rfLevel" : {
  "dataType" : "number",
  "unit" : "dB",
  "value" : 40,
  "readOnly" : true
}
``` 

Example with percentage:
```jsonc
"rfLevel" : {
  "dataType" : "number",
  "unit" : "%",
  "value" : 100,
  "readOnly" : true
}
``` 

#### batteryLevel
Connected device battery level
- **unit**: %
- **dataType**: number
- **readOnly**: Always true (read-only)
- **Note**: Because these are read-only, you don't need to provide precision, minValue, or maxValue

Example:
```jsonc
"batteryLevel" : {
  "dataType" : "number",
  "unit" : "%",
  "value" : 40,
  "readOnly" : true
}
``` 

### Continuous Parameter Status Update Example
```jsonc
{
  "transmittingDevice": "ClientDanteDeviceName",
  "messages": [
    {
      "messageType": "statusUpdate",
      "channelIndex": 0,
      "channelType": "tx",
      "audioLevel": {
        "value": -12
      },
      "rfLevel": {
        "value": 85
      },
      "batteryLevel": {
        "value": 67
      }
    }
  ]
}
```

# Device Information

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
These parameters are only present in device information responses (channelIndex = -1):

- **model**: Device model name 
- **deviceType**: Device category (string)
- **virgilVersion**: Protocol version being used (string)
- **channelCounts**: The counts for each various types of channels
  - The 3 channel types are `tx`, `rx`, and `aux`


# Message Details

### Status Update
A status update can be triggered by 3 events:
1. A parameter was changed via `parameterCommand`
2. Continuous parameters haven't been broadcasted in 500ms
3. A `statusRequest` was received

In cases 1 and 2, the status update should be sent to all devices subscribed to that channel.
In case 3, the status update should be sent only to the device that requested it.

**Note**: Status updates only include the values that have changed, not the full parameter structure.

#### Error Types
- `UnrecognizedCommand`: Unknown message type
- `ValueOutOfRange`: Parameter value outside allowed range
- `InvalidValueType`: Wrong data type for parameter
- `UnableToChangeValue`: Parameter cannot be modified currently
- `DeviceNotFound`: Target device not available
- `ChannelIndexInvalid`: Channel does not exist
- `ParameterReadOnly`: Parameter is read-only or disabled
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

# Implementation Notes

- **Error Handling**: Always provide clear, user-friendly error messages in `errorString`
- **Parameter Discovery**: Servers should query device capabilities before attempting to control parameters
- **Continuous Monitoring**: Implement 500ms update intervals for continuous parameters
- **Network Resilience**: Handle device disconnections and reconnections gracefully
- **Example Files**: Reference the `Example JSON/` directory for complete message examples

---

# See Also

- Example JSON message files in the `Example JSON/` directory
# Virgil Protocol 2.0.0

Virgil is a network protocol for controlling audio devices using JSON-formatted messages over UDP. It uses mDNS for device discovery and supports real-time parameter control and status monitoring.

**This is Virgil Protocol 2.0.0**

In previous versions of the virgil protocol, devices were categorized 




# mDNS Overview

Virgil uses a typical mDNS configuration. It is advertised over `224.0.0.251` at port `5353`  
The protocol will (almost) fully function on a network that doesn't support mDNS. mDNS is only used for a dante controller like app and detecting accessories 

### Service Configuration
- **serviceType**: `_virgil._udp.local.`
- **serviceName**: `{dante name}._virgil._udp.local.`
- **txt**
  + **model**: The model of the device (eg. M32)
  + **deviceType**: The type of device it is. For more information and allowed values, go to [Device Types](#device-types)

**Example mDNS Service Advertisement:**
```jsonc
{
  "serviceType": "_virgil._udp.local.",
  "serviceName": "ExampleClient._virgil._udp.local.",
  "txt": {
    "model": "DeviceModelName",
    "deviceType": "digitalStageBox"
  }
}
```
mDNS isn't actually json, but this is the best way I could represent it



# Message Types

| Type                | Description                                          | Protocol|
|---------------------|------------------------------------------------------|---------|
| parameterCommand    | Set or change a parameter on a device/channel        | UDP     |
| statusUpdate        | Notify parameter or device state change              | UDP     |
| statusRequest       | Sent to a device to request a status update          | UDP     |
| channelLink         | A message telling a device it's linked channels      | UDP     |
| infoRequest		  | A message requesting information on a device/channel | UDP	   |
| infoResponse 		  | A response to an infoRequest 						 | UDP     |
| errorResponse       | A message containing an error						 | UDP     |
| subscribeMessage    | A message subscribing to a certain channel           | UDP     |


### Message Usage
- **parameterCommand**: Change gain, pad, phantom power, etc.
- **statusUpdate**: Automatic notifications when parameters change (or after a statusRequest)
- **statusRequest**: To request a status update
- **channelLink**: When first connecting to a device
- **infoRequest**: To get what parameters a channel has
- **infoResponse**: To answer an infoRequest
- **errorResponse**: To tell a device an error has occurred
- **subscribeMessage**: This isn't intended to be used by actual devices. You should instead [link channels](#linking-channels). This is meant for a dante-controller type app


# Channel Types
There are 3 types of channels. They are `tx`, `rx`, and `aux`  
`tx` and `rx` channels correspond with Dante transmitting and receiving channels. For every dante channel a device has, it must have a corresponding tx/rx channel  
tx/rx channels can be linked together. For more information, go to [Linking Channels](#linking-channels)  
Aux channels do not have a Dante equivalent. They are instead used for sending simple values to a device (Think an in-wall dial connected to a mixer)  
Aux channels can be linked to a device, not another channel. For more information, go to [Linking Channels](#linking-channels)  

Channel indices start at 0

## Linking Channels
Channels are linked together to represent the flow of data. Linking to a channel automatically subscribes each device to its corresponding channel. Rx channels can only be linked to Tx channels. Either side can initiate the link by sending a channelLink.  

Aux channels can only be linked to another device. Only the device can initiate the link.

# Parameters

Virgil supports various audio parameters. Only `gain` is mandatory for all channels involving a preamp, even on fixed-gain preamps.

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

The formula to see if a value is valid is:
```python
  isValid = (value - minValue) % precision and value > minValue and value < maxValue
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
  "readOnly" : false,
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
  "readOnly" : true,
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
  "readOnly" : false,
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
  "readOnly" : false,
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
  "readOnly" : false,
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
  "readOnly" : false,
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
  "readOnly" : false,
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
  "value" : true,
  "readOnly" : false,
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
  "readOnly" : false,
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
  "readOnly" : false,
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
  "readOnly" : false,
}
``` 

### Status Parameters (Read-Only)

#### transmitterConnected
Wireless transmitter connection status
- **dataType**: bool
- **readOnly**: Always true (read-only)

Example:
```jsonc
"transmitterConnected" : {
  "dataType" : "bool",
  "value" : false,
  "readOnly" : true,
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
  "readOnly" : true,
}
``` 

### Continuous Parameters (Real-Time Monitoring)

These parameters change frequently and require status updates every 500ms:

#### audioLevel
Audio signal level
- **unit**: dBFS
- **dataType**: number
- **readOnly**: Always true (read-only)
- **Note**: Because these are read-only, you don't need to provide precision, minValue, or maxValue

Example:
```jsonc
"audioLevel" : {
  "dataType" : "number",
  "unit" : "dBFS",
  "value" : -18,
  "readOnly" : true,
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
  "readOnly" : true,
}
``` 

Example with percentage:
```jsonc
"rfLevel" : {
  "dataType" : "number",
  "unit" : "%",
  "value" : 100,
  "readOnly" : true,
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
  "readOnly" : true,
}
``` 

### Continuous Parameter Status Update Example
```jsonc
{
  "transmittingDevice": "ClientDanteDeviceName",
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



---

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
These parameters are only present in device information responses:

- **model**: Device model name 
- **deviceType**: Device category (string)
- **virgilVersion**: Protocol version being used (string)
- **channelCounts**: The counts for each various types of channels
	- The 3 channel types are `tx`, `rx`, and `aux`


# Message Details

### Status Update
A status update can be triggered by 3 events.
1. A parameter was changed
2. You have continuous parameters and it has been 500 ms since the last update
3. A statusRequest was received

In cases 1 and 2, the status update should be sent to all devices subscribed to that channel.
In case 3, the status update should be sent to the ip address that requested it

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

# Implementation Notes

- **Error Handling**: Always provide clear, user-friendly error messages in `errorString`
- **Parameter Discovery**: Servers should query device capabilities before attempting to control parameters
- **Continuous Monitoring**: Implement 500ms update intervals for continuous parameters
- **Network Resilience**: Handle device disconnections and reconnections gracefully
- **Example Files**: Reference the `Example JSON/` directory for complete message examples

---

# See Also

- Example JSON message files in the `Example JSON/` directory
- Server and Client example implementations in respective directories
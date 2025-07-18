# Protocol Overview
Virgil is composed of 5 types of communication. All will be formatted as JSON files.
Virgil will also use mDNS to find other virgil devices.  
The Virgil port is 7889.



# mDNS
The service type is "_virgil._udp.local."
The service name is "{dante name}.{service type}"
The service port is 7889.


**Masters must always be actively searching for mDNS advertisements, not just at startup.** This ensures that if a device (slave) boots up after the master, it can still be discovered and added to the system automatically. Masters should continuously listen for new mDNS packets and update their device list as new slaves appear or disappear from the network.


Before advertising over mDNS, all slave devices must first scan for Virgil devices and choose a unique multicast address.
In the TXT record, there will be a multicast address without the last number, e.g., 244.1.1.  
This is used to ensure multicast IDs are unique to each slave device. There is no specified way to determine the multicast ID; you can use the lowest available number, a random link-local approach, or another method. 
There will also be a variable called "function" that will be either "master", "slave", or "both".  
There will also be variables called "model" and "deviceType". More information on these can be found in the parameter section.  
This is currently unused but may be displayed in debug software.
Both masters and slaves should always be advertising mDNS.


# Virgil Protocol Overview
Virgil uses JSON-formatted messages and mDNS for device discovery. The default port is **7889**.

## mDNS Discovery
- **Service Type:** `_virgil._udp.local.`
- **Service Name:** `{dante name}._virgil._udp.local.`
- **Service Port:** 7889
- **Masters:** Must continuously listen for mDNS advertisements to discover new slaves at any time.
- **Slaves:** Before advertising, scan for existing Virgil devices and select a unique multicast address (e.g., `244.1.1`). The method for choosing the last number is flexible (lowest unused, random, etc.).
- **TXT Record Fields:**
  - `multicastAddress` (e.g., `244.1.1`)
  - `function`: `master`, `slave`, or `both`
  - `model`, `deviceType`: See parameter section below
- **Advertising:** Both masters and slaves should always advertise via mDNS.

---
## Parameters
Virgil supports the following parameters. Only `gain` is mandatory. This is so that, even with devices with fixed gain, the master can still see the fixed gain value.  
If a feature is disabled, include its parameter with `locked: true`.  
Only the `value` field is changeable by masters; all other fields are informational.  
If the way something is formatted make zero sense, its to make it easier to have a little icon on the gui

**Parameter Field Format:**
- `name` (string): Parameter name in JSON messages
- `description` (string): What the parameter controls
- `unit` (string, optional): unit of measurement
- `dataType` (string): Data type (`int`, `float`, `bool`, `string`, `enum`)
- `minValue` (int/float, optional): The minimum value of the parameter
- `maxValue` (int/float, optional): The maximum value of the parameter
- `precision` (number, optional): Step size. If not included, assume to be 1
- `locked` (bool): If true, parameter cannot be changed


**Notes**
- For precision ≠ 1, values start from minValue (e.g., precision 3dB, minValue -5dB: options are -5, -2, 1, 4, ...).
- The data type `enum` is not a usual enum. It is instead represented as an array of strings. All the strings in the array are valid values. The values can be found in the `enumValues` field.
- Percent is a unit, not a dataType. If the unit is `%` then the `minValue` should be `0`, the `maxValue` should be `100`, the `precision` should be `1`, and the `dataType` should be `int`

```jsonc
{
  // Example transmitPower parameter as enum
  "transmitPower": {
    "value": "low",        // Current value
    "enumValues": ["low", "medium", "high"], // Valid values
    "dataType": "enum",
    "locked": false
  }
}
```


---
### Supported Parameters

#### gain
- Analog gain of the channel (ignores pad)
- **unit:** dB
- **dataType:** int or float
- **minValue:** Device-specific minimum gain
- **maxValue:** Device-specific maximum gain
- **precision:** Step size in dB
- **locked:** true if gain is fixed or disabled
- **Notes:** Gain is independent of pad. 

#### pad
- Optional input attenuator
- **unit:** dB (for `padLevel`)
- **dataType:** bool (`true` -> on, `false` -> off)
- **padLevel:** Attenuation when enabled (usually negative)
- **locked:** true if pad cannot be changed

#### lowcut
- High-pass filter control
- **unit:** Hz
- **dataType:** int
- **minValue:** Device-specific minimum frequency
- **maxValue:** Device-specific maximum frequency
- **precision:** Step size in Hz
- **locked:** true if lowcut cannot be changed

#### lowcutEnable
- Enables/disabled the low cut
- **dataType:** bool (`true` -> enabled, `false` -> disabled)
- **locked:** true if lowcut enable cannot be changed

#### polarity
- Signal polarity (phase invert)
- **dataType:** bool (true = inverted, false = normal)
- **locked:** true if polarity cannot be changed

#### phantomPower
- Enables/disables phantom power
- **dataType:** bool
- **locked:** true if phantom power cannot be changed

#### rfEnable
- Enables/disables RF transmitter/reciver
- **dataType:** bool
- **locked:** true if RF enable cannot be changed

#### transmitPower
- Sets the transmit power for a connected transmitter (Usually IEM)
- **dataType:** enum
- **locked:** true if transmit power cannot be changed

#### transmitterConnected
- Shows if a wireless transmitter has been found and is connected
- **dataType:** bool
- **locked:** Always True

#### squelch
- Allows for squelch level to be set (threshold below which audio/RF is muted)
- **unit:** dB or % (depends on device; dB for RF level threshold, % for normalized)
- **dataType:** int or float
- **minValue:** Device-specific minimum threshold
- **maxValue:** Device-specific maximum threshold
- **locked:** true if squelch cannot be changed

#### subDevice
- Says what type of device is connected to this channel. Primarily used for wireless recivers
- **dataType:** string
- **locked** Always True
- **Notes:** This string has predefined values. They are `handheld`, `beltpack`, `gooseneck`, `iem`, `xlr`, `trs`, `disconnected` and `other`.

#### audioLevel
- Shows the level of the audio for that channel
- **unit:** dBFS
- **dataType:** int (signed)
- **locked:** Always True
- **Notes:** This is a continuous parameter. See below for more information

#### rfLevel
- Shows the level of RF the reciver is reciving
- **unit:** dB or %
- **dataType:** int (signed)
- **locked:** Always True
- **Notes:** This is a continuous parameter. See below for more information

#### batteryLevel
- Shows the battery level of the connected device
- **unit:** %
- **dataType:** int
- **locked:** Always True
- **Notes:** This is a continuous parameter. See below for more information


### Continuous Parameters
A continuous parameter is a parameter whose value changes often and must be broadcasted frequently. For a continuous parameter, a status update must be sent every half second, reporting the current value of that parameter.  
These will be normal status updates, and can even be combined.  
This is an example of what a status update for every continuous parameter might look like

```jsonc
{
  "transmittingDevice": "SlaveDanteDeviceName",
  "messages": [
    {
      "messageType": "StatusUpdate",
      "channelIndex": 0,
      "audioLevel": {
        "value": -12,      // Current audio level in dBFS
      },
      "rfLevel": {
        "value": 85,       // Current RF level in %
      },
      "batteryLevel": {
        "value": 67,       // Battery level in %
      }
    }
  ]
}
```


---
## Device-Level Parameters
Present only in device-level responses (`channelIndex: -1`):

- **model**: Model name (string, locked)
- **deviceType**: Device type (string, locked. Valid values are: `digitalStageBox`, `wirelessReceiver`,`wirelessTransmitter`, `wirelessCombo`, `mixer`, `dsp`, and `computer`)
- **virgilVersion**: The version of the virgil protocol that the device is running
- **channelIndices**: An array of the available channels.


### Channel Indices Information
The channel indices parameter is a list of the indices of the available channels. For most devices, it will just be a range; e.g., 8 channels would be `[0,1,2,3,4,5,6,7]`.  
However, it is possible for a channel to not be tied to a preamp (for example, a mixer), in which case it would not have any properties to control. If there were 8 channels, but channel 5 was tied to an aux instead of a preamp, the array would be `[0,1,2,3,4,6,7]`.


---
## Message Formatting
All messages are JSON objects with these top-level fields:
- `transmittingDevice`: Dante name of sender
- `messages`: Array of message objects

---
## Message Types
| Type               | Description                                                        | Direction             | Protocol   |
|--------------------|--------------------------------------------------------------------|-----------------------|------------|
| ParameterCommand   | Set/change a parameter on a device/channel                        | Master → Slave        | UDP        |
| StatusRequest      | Request current status of channels                                | Master → Slave        | UDP        |
| StatusUpdate       | Notify parameter/device state change                              | Slave → Masters (all) | Multicast  |
| ParameterRequest   | Request device/channel parameter info/capabilities                | Master → Slave        | UDP        |
| ParameterResponse  | Reply with parameter info/capabilities                            | Slave → Master        | UDP        |
| ErrorResponse      | Indicate request error, with details                              | Slave → Master        | UDP        |

---
## Usage Examples
- Use `ParameterCommand` to change gain, pad, etc.
- Use `StatusRequest` to poll channel/device state.
- Use `StatusUpdate` to inform controllers of changes (sent automatically by slaves).
- Use `ParameterRequest` to discover device capabilities.
- Use `ParameterResponse` to reply to `ParameterRequest`.
- Use `ErrorResponse` for errors (invalid command, out of range, etc.).

---
## Message Details

### ParameterCommand
- Sent by master to slave to update parameters
- Only includes updated values
- Sent via UDP

### StatusUpdate
- Sent by slave to all masters when a channel property changes
- Contains all values for the affected channel
- Multicast (UDP)

### ParameterRequest
- Sent by master to slave to request parameter info
- Can request all channels and device-level info
- Sent via UDP
- `channelIndex: -1` for device-level info; `0..N-1` for channels; `-2` for both

### ParameterResponse
- Sent by slave to master in response to `ParameterRequest`
- Contains requested parameter info
- Device-level: includes `model`, `deviceType`, `channelCount`
- Channel-level: includes `channelIndex` and supported parameters
- All supported parameters for each channel must be included
- Use `locked` to indicate adjustability

### ErrorResponse
- Sent when a command cannot be processed
- Contains `errorValue` and `errorString`
- Error types:
  - UnrecognizedCommand
  - ValueOutOfRange
  - InvalidValueType
  - UnableToChangeValue
  - DeviceNotFound
  - ChannelIndexInvalid
  - ParameterLocked
  - ParameterUnsupported
  - MalformedMessage
  - Busy
  - Timeout
  - PermissionDenied
  - InternalError
  - OutOfResources
  - NetworkError
  - Custom (use `Custom:Description` in `errorValue`)
- `errorString` is user-friendly text

---
## Additional Notes
- For custom errors, document the type and provide a clear `errorString`.
- See example JSON files for exact message structures.
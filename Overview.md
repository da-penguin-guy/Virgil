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
There will also be variables called "model" and "deviceType". More information on these can be found in the parameter section.  
This is currently unused but may be displayed in a debug software.

# Parameters
The following parameters are supported by Virgil. Only `gain` is mandatory. If a slave device supports a feature that is currently disabled, include the corresponding parameter and set its `locked` property to `true`. Only the `value` of a parameter can be changed by a master device; other fields (such as `precision`, `minValue`, etc.) are informational and cannot be changed remotely.

**Parameter Field Format:**
- `name` (string): The parameter name as used in JSON messages.
- `Description`: What the parameter controls.
- `Unit` (string): The unit of measurement, if applicable.
- `Value Type` (string): The data type (e.g., int, float, bool, string).
- `Range` (if applicable): Minimum and maximum values.
- `Precision` (if applicable): Step size for value changes.
- `Locked` (bool): If true, the parameter cannot be changed.

---

### gain
- **Description:** Analog gain of the preamp (ignores pad).
- **Unit:** dB
- **Value Type:** int or float
- **Range:** Device-specific (see `minValue` and `maxValue` fields)
- **Precision:** Step size in dB (see `precision` field)
- **Locked:** true if gain is fixed or disabled
- **Notes:** Gain value should be independent of pad. Master devices should visually indicate pad status to users.

### pad
- **Description:** Optional attenuator for the preamp input.
- **Unit:** dB (for `padLevel`)
- **Value Type:** bool (on/off)
- **padLevel:** Amount of attenuation applied when pad is enabled (int or float, usually negative)
- **Locked:** true if pad cannot be changed

### lowcut
- **Description:** High-pass (low cut) filter control.
- **Unit:** Hz
- **Value Type:** int
- **Range:** Device-specific (see `minValue` and `maxValue` fields)
- **Precision:** Step size in Hz (see `precision` field)
- **Locked:** true if lowcut cannot be changed

### polarity
- **Description:** Signal polarity (phase invert).
- **Value Type:** bool (true = inverted, false = normal)
- **Locked:** true if polarity cannot be changed

### phantomPower
- **Description:** Enables or disables phantom power.
- **Value Type:** bool
- **Locked:** true if phantom power cannot be changed

### rfPower
- **Description:** RF level of a connected RF receiver (read-only). *NOT* Transmitting Power
- **Unit:** dB or %
- **Value Type:** int (dB) or int (percent, 0-100)
- **Locked:** Always true

### rfEnable
- **Description:** Enables or disables a connected RF transmitter.
- **Value Type:** bool
- **Locked:** true if RF enable cannot be changed

### batteryLevel
- **Description:** Battery level of the end device (read-only).
- **Unit:** %
- **Value Type:** int (0-100)
- **Locked:** Always true

---

## Device-Level Parameters
The following parameters are only present in device-level ParameterInfoResponse objects (where `preampIndex` is -1):

### model
- **Description:** Model name of the device.
- **Value Type:** string
- **Locked:** Always true

### deviceType
- **Description:** Type of device (recommended values: digitalStageBox, wirelessReceiver, mixer, dsp, computer).
- **Value Type:** string
- **Locked:** Always true

### preampCount
- **Description:** The number of preamps on the device.
- **Value Type:** int
- **Required:** Yes, only for device-level ParameterInfoResponse (preampIndex: -1)

---

**Notes:**
- If a value type is percentage, use an int from 0 to 100, unit string "%", precision 1, minValue 0, and maxValue 100.
- For precision, values not equal to 1 are counted from the min value. For example, if precision is 3dB and minValue is -5dB, available options are -5, -2, 1, 4, etc.


# Formatting Overview
All messages are JSON objects with the following top-level fields:

- `transmittingDevice`: The Dante name of the device sending the message.
- `receivingDevice`: The Dante name of the device receiving the message (not present for multicast messages).

For most message types, the payload is an array called `messages`, containing one or more message objects. Each object in this array must have a `messageType` field indicating its type.



# Message Types

Virgil uses the following message types for communication between devices. Each message type is identified by the `messageType` field in the `messages` array.

| Message Type         | Description                                                                 | Direction             | Protocol   |
|---------------------|-----------------------------------------------------------------------------|-----------------------|------------|
| ParameterCommand    | Request to set or change a parameter on a device/preamp.                    | Master → Slave        | UDP        |
| StatusRequest       | Request the current status of one or more preamps.                          | Master → Slave        | UDP        |
| StatusUpdate        | Notification that a parameter or device state has changed.                   | Slave → Masters (all) | Multicast  |
| ParameterRequest    | Request detailed information about device/preamp parameters and capabilities.| Master → Slave        | UDP        |
| ParameterResponse   | Response with parameter info/capabilities.                                  | Slave → Master        | UDP        |
| ErrorResponse       | Response indicating a request could not be processed, with error details.    | Slave → Master        | UDP        |

**Usage Examples:**
- Use `ParameterCommand` to change gain, pad, etc.
- Use `StatusRequest` to poll the current state of a preamp or device.
- Use `StatusUpdate` to inform all controllers of a change (sent automatically by slaves).
- Use `ParameterRequest` to discover what parameters and ranges a device supports.
- Use `ParameterResponse` to reply to `ParameterRequest` messages.
- Use `ErrorResponse` to indicate errors (invalid command, out of range, etc.).

**Note:**
- The `messages` array in each packet contains one or more message objects, each with a `messageType` field set to one of the above types. Only `ParameterResponse` uses `parameterResponses`.

# Parameter Commands
Parameter Commands are typically sent from a master device (mixer, computer, etc.) to a slave device (Digital stagebox, Preamp, etc.)  
Parameter Commands only contain the information being updated in the slave device.  
For example, a mixer changing a stagebox's gain would only contain the gain value for one preamp, instead of containing pad, phantom power, etc.  
Parameter Commands are sent using UDP for simplicity and low overhead on embedded devices.  

Look at the example JSON or the example scripts for more specific information.

# Status Updates
Status updates are sent from a slave device to all subscribed master devices whenever a property in a preamp is updated.  
Status updates contain all information for the preamp the change was made to.  
This only includes the values for the parameters. If other data changes, such as if a parameter's min value has changed, that should also be sent.
For example, if a mixer changes the gain on preamp 5 on a digital stagebox, a status update will be sent with all information for preamp 5.  
Status updates are multicast (UDP).

Look at the example JSON or the example scripts for more specific information.

# Parameter Request
Parameter Requests are similar to status updates, with a few key differences.  
Parameter Requests are requested via an Parameter Request and sent to a single master device.  
They are also able to give data on all slave preamps, if requested to.  
Parameter Requests communicate the capabilities of each preamp, such as gain ranges, precision, etc.  
Parameter Requests are sent via UDP.

Look at the example JSON or the example scripts for more specific information.

# Error Response
Error Responses are sent whenever a command was unable to be processed.  
This could be if a master attempts to change a fixed gain, sends an unrecognized command, or many other situations.  
Error packets are UDP.
Error packets have 2 values, the error type and error string.  
Currently, the error values are:
- UnrecognizedCommand
- ValueOutOfRange
- InvalidValueType
- UnableToChangeValue
- DeviceNotFound
- PreampIndexInvalid
- ParameterLocked
- ParameterUnsupported
- MalformedMessage
- Busy
- Timeout
- PermissionDenied
- InternalError
- OutOfResources
- NetworkError
- Custom

For `Custom` errors, set `errorValue` to a string starting with `Custom:` followed by a descriptive identifier (e.g., `Custom:OverTemperature`). The `errorString` should always be a user-friendly explanation. Document any custom error types in your device’s documentation.

Error Strings are text strings that should be shown to the end user. These do not have specified values. ErrorValues can be displayed to the user as a part of the UI (For example, an icon determined by the ErroValue), if the designer chooses to.

Look at the example JSON or the example scripts for more specific information.


# Parameter Requests
`ParameterRequest` messages are sent from a master to a slave to request parameter information.  
They contain an array of the preamp indices that should be sent.
`ParameterResponse` messages, once requested, are sent to the IP address that requested it.
`ParameterRequest` can also request information about the device itself, such as the model number, device type, etc.

**ParameterRequest and preampIndex Handling:**
- When sending `ParameterRequest` messages, you may send requests for all possible preamp indices at once (including -1 for device-level info and 0..N-1 for each preamp).
- A `preampIndex` of -1 should be treated as a request for device-level info (model number, device type, preampCount, etc).
- A `preampIndex` of -2 should be treated as a request for both device-level info (like -1) and for every preamp (0..N-1). If you receive a response with `preampIndex: -2`, treat it as if you received both device-level info and all preamp info.

Look at the example JSON or the example scripts for more specific information.



# Parameter Responses
`ParameterResponse` messages provide detailed information about the parameters supported by a device or preamp. They are sent in response to a `ParameterRequest` message and contain the requested parameter information.

## Message Structure
A `ParameterResponse` message contains the following fields:

- **transmittingDevice**: The Dante name of the device sending the response.
- **receivingDevice**: The Dante name of the device receiving the response.
- **messages** (array, REQUIRED): An array containing the parameter information messages. This field is required for this message type.

## Parameter Response Structure

Each object in the `messages` array of a `ParameterResponse` includes all properties that this device supports (such as gain, pad, lowcut, etc.), using the property names and sub-values defined in the parameter list at the top of this document. Only include the properties present on the device. For the structure and sub-fields of each property, refer to the parameter definitions above.

See the example JSON files for the exact structure for each parameter.

## Notes
- The `messages` field is REQUIRED for all `ParameterResponse` messages.
- The `preampCount` field is only present for device-level responses (where `preampIndex` is -1) and MUST NOT be included for preamp-level responses.
- Include all supported parameters for each preamp in the `messages` array.
- Use the `locked` field to indicate if a parameter is adjustable or fixed.

Look at the example JSON or the example scripts for more specific information.
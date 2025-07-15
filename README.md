# Virgil
An open-source companion protocol for Dante-based preamp control, supporting robust, networked, multi-controller operation.

## Features
- Networked control of gain, phantom power, pad, and more
- Multiple controllers can control the same preamp
- Uses multicast to keep all controllers up-to-date
- Automatic device discovery via mDNS
- Static and dynamic device list support
- Simple, robust UDP-based protocol

---

## Protocol Overview
Virgil uses JSON-formatted messages over UDP (port 7889) for all communication. Device discovery is handled via mDNS using the service type `_virgil._udp.local.`. Both master and slave devices should continuously advertise and listen for mDNS packets to ensure dynamic device discovery.

### mDNS
- Service type: `_virgil._udp.local.`
- Service name: `{dante name}._virgil._udp.local.`
- Service port: 7889
- TXT fields: `multicast`, `function` ("master", "slave", or "both"), `model`, `deviceType`

### Parameters
Virgil supports a range of parameters for preamp and device control. Only `gain` is mandatory; others are optional and may be locked if unsupported. See `Overview.md` for full details.

**Common Parameters:**
- gain (dB, int/float)
- pad (bool)
- lowcut (Hz, int)
- polarity (bool)
- phantomPower (bool)
- rfPower (dB/% int, read-only)
- rfEnable (bool)
- batteryLevel (% int, read-only)

**Device-level Parameters:**
- model (string, read-only)
- deviceType (string, read-only)
- preampCount (int, read-only)

---

## Message Types
Virgil uses the following message types (see `Overview.md` for details):

| Message Type         | Description                                                                 | Direction             | Protocol   |
|---------------------|-----------------------------------------------------------------------------|-----------------------|------------|
| ParameterCommand    | Request to set or change a parameter on a device/preamp.                    | Master → Slave        | UDP        |
| StatusRequest       | Request the current status of one or more preamps.                          | Master → Slave        | UDP        |
| StatusUpdate        | Notification that a parameter or device state has changed.                   | Slave → Masters (all) | Multicast  |
| ParameterRequest    | Request detailed information about device/preamp parameters and capabilities.| Master → Slave        | UDP        |
| ParameterResponse   | Response with parameter info/capabilities.                                  | Slave → Master        | UDP        |
| ErrorResponse       | Response indicating a request could not be processed, with error details.    | Slave → Master        | UDP        |

---

## Example JSON
See the `Example JSON/` directory for sample request and response messages for all protocol types.

---

## Quick Start: Dependency Setup (Windows)

To build or run the Master example, you need [GLFW](https://www.glfw.org/) installed. The included `setup_vcpkg_glfw3.bat` script will:
- Check for vcpkg (C++ package manager)
- Install vcpkg if missing
- Install glfw3
- Add vcpkg to your PATH (system-wide if run as admin, user PATH otherwise)

**Usage:**

1. Open a terminal (PowerShell or Command Prompt)
2. Run the script:
   ```
   setup_vcpkg_glfw3.bat
   ```
3. Follow the prompts. If you just added vcpkg to your PATH, restart your terminal or log out/in.

---

## Building

After running the setup script, you can build the project using your preferred C++ build system.  
There should be windows builds included with the examples, because windows c++ compilers are annoying to install.  
The examples don't use a package manager.  
Both use the [nlohmann/json](https://github.com/nlohmann/json) library to handle JSON files.  
The master also uses [nuklear](https://github.com/Immediate-Mode-UI/Nuklear) to show a GUI. This requires [GLFW](https://www.glfw.org/) (For installation, see above) and OpenGL (Pre-installed on windows and linux)

---

## More Information
- See `Overview.md` for full protocol details, parameter definitions, and message structures.
- See `Example JSON/` for message samples.
- See `Master_Example/` and `Slave_Example/` for example implementations.

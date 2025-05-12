# Virgil
An open-source companion protocol to allow for Dante-based preamp control.

# Features
- Networked control of gain, phantom power, and pad.
- Several controllers can control the same preamp
  -Displays how many devices are controlling a preamp
  -Uses multicast to ensure all controllers have up-to-date information
-Automatically follows Dante subscriptions
-Allows for preamps to communicate the current mode, such as mic, line, high-z, disconnected, etc.

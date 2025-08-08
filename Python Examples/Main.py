import Variables
import os
os.system('cls' if os.name == 'nt' else 'clear')
from zeroconf import Zeroconf, ServiceInfo, ServiceBrowser
import socket
import json
import threading
import os
import sys
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                                QHBoxLayout, QGridLayout, QPushButton, QLabel, QListWidget, 
                                QTextEdit, QGroupBox, QComboBox, QDial)
from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtGui import QFont



# Look, I'm sorry about python. I don't like it, but I like c++ even less

os.chdir(os.path.dirname(os.path.abspath(__file__)))

selfIp = Variables.GetIp()

found_devices = []

class MDNSListener:
    def remove_service(self, zeroconf, type, name):
        # Remove device from found_devices if present
        if name in found_devices:
            found_devices.remove(name)
            Variables.PrintBlue(f"Device removed: {name}")

    def update_service(self, zeroconf, type, name):
        # Handle service updates - can be empty if not needed
        pass

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        # Extract the actually useful name
        base_name = name.split('.')[0] 

        # Check that we didn't find ourselves
        if info and base_name != Variables.selfName:
            # Check if we already found this device via mDNS
            if base_name in found_devices:
                return  
            found_devices.append(base_name)
            Variables.PrintBlue(f"Found device: {base_name}")

            # Check if we already have this device via netListener
            if base_name in Variables.devices:
                Variables.PrintBlue(f"Device already exists: {base_name}")
                return
            
            # Create the device with the found IP address
            CreateDevice(base_name, socket.inet_ntoa(info.addresses[0]))


def CreateDevice(deviceName : str, deviceIp : str, sock : socket.socket = None, startingMessage: bytes = None):
    infoRequests : list[dict] = []
    channelLink : list[dict] = []

    #Loop over every connection for this device and add the needed messages
    for connection in Variables.connections:
        if connection.connectedDevice != deviceName:
            continue
        infoRequests.append(Variables.CreateInfoRequest(connection.channelIndex, connection.channelType))
        channelLink.append(Variables.CreateChannelLink(connection.selfIndex, connection.selfType, connection.channelIndex, connection.channelType))
    #Create the device
    #These messages are split up into 3 because you may want to do check a channel exists first
    #I don't do that, but it would be a good idea
    #It is also fine if you don't because you'll just get an error message
    Variables.devices[deviceName] = Variables.DeviceInfo(
                deviceName=deviceName,
                ip=deviceIp,
                sock=sock,
                startingMessage=startingMessage,
                queue=[
                    Variables.CreateInfoRequest(-1),
                    infoRequests,
                    channelLink
                ]
            )

def NetListener():
    """
    Listens for TCP connections and creates DeviceInfo for each new socket.
    """
    Variables.PrintBlue(f"Listening for TCP messages on port {Variables.virgilPort}...")

    #Create listening socket
    listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listen_sock.bind(("", Variables.virgilPort))
    listen_sock.listen(5)

    try:
        # Accept incoming connections
        while True:
            sock, addr = listen_sock.accept()
            remote_ip = addr[0]
            Variables.PrintGreen(f"Accepted connection from {remote_ip}")
            try:
                #Receive initial data
                data = sock.recv(4096)
                #If data is empty, the other side already closed the connection
                if not data:
                   sock.close()
                   continue
                #parse JSON
                j: dict= json.loads(data.decode('utf-8'))
                deviceName = j.get("transmittingDevice")
                if not deviceName:
                    sock.close()
                    continue
                
                # Check if we already have an active connection with this device
                if deviceName in Variables.devices and Variables.devices[deviceName].isVirgilDevice and Variables.devices[deviceName].sock:
                    Variables.PrintRed(f"Already have active connection with {deviceName}, rejecting new connection")
                    sock.close()
                    continue
                #Create new device, passing it the message we already got and the pre-existing socket
                CreateDevice(deviceName, remote_ip, sock, data)
            except Exception as e:
                # Handle any errors in receiving or processing the initial data
                Variables.PrintRed(f"Error receiving initial data from {remote_ip}: {e}")
                sock.close()
    except KeyboardInterrupt:
        #If program is stopped, stop
        Variables.PrintRed("NetListener shutting down...")
    finally:
        listen_sock.close()

#Search all .config files and let the user choose
configFiles = [f for f in os.listdir('.') if f.endswith('.config')]
print(configFiles)
if not configFiles:
    Variables.PrintRed("No .config files found in the current directory.")
else:
    print("Select a .config file:")
    for idx, fname in enumerate(configFiles):
        print(f"{idx + 1}: {fname}")
    while True:
        choice = input("Enter the number of the file you want to use: ")
        try:
            idx = int(choice) - 1
            if idx < 0 or idx >= len(configFiles):
                Variables.PrintRed("Invalid selection.")
                continue
            selectedFile = configFiles[idx]
            print(f"You selected: {selectedFile}")
            Variables.LoadConfig(selectedFile)
            break
        except ValueError:
            Variables.PrintRed("Invalid selection.")


service_type = "_virgil._tcp.local."
service_name = f"{Variables.selfName}._virgil._tcp.local."

info = ServiceInfo(
    type_=service_type,
    name=service_name,
    port=Variables.virgilPort,
    properties={
        "model": Variables.selfModel,
        "deviceType": Variables.selfType
    },
    addresses=[socket.inet_aton(selfIp)]
)

# Advertise virgil MDNS
# All virgil devices must advertise themselves using this
# However, this will only be used for a dante controller like app, and not for the actual virgil protocol
zeroconf = Zeroconf()
zeroconf.register_service(info)

# Start NetListener in a separate thread
listenerThread = threading.Thread(target=NetListener, daemon=True)
listenerThread.start()

Variables.PrintBlue("Blue for outbound")
Variables.PrintGreen("Green for inbound")
Variables.PrintRed("Red for errors")
print("\n")

# The actual protocol would use Dante's mDNS, but I can't do that
# Instead, this is using virgil mDNS. For a proper implementation, you would use Dante's mDNS.
Variables.PrintBlue("Scanning for mDNS devices...")
browser = ServiceBrowser(zeroconf, "_virgil._tcp.local.", MDNSListener())

# PyQt GUI
class VirgilGUI(QMainWindow):
    selectedConn: Variables.DeviceConnection = None

    def __init__(self):
        super().__init__()
        self.setWindowTitle("Virgil Protocol Monitor")
        self.setGeometry(100, 100, 700, 400)

        # Set up the main widget and horizontal layout
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        h_layout = QHBoxLayout(main_widget)

        # --- LEFT SIDE ---
        left_widget = QWidget()
        left_layout = QVBoxLayout(left_widget)

        # Device info section
        info_group = QGroupBox("Device Information")
        info_layout = QVBoxLayout(info_group)

        self.name_label = QLabel(f"Name: {Variables.selfName}")
        self.model_label = QLabel(f"Model: {Variables.selfModel}")
        self.type_label = QLabel(f"Type: {Variables.selfType}")
        self.port_label = QLabel(f"Port: {Variables.virgilPort}")

        info_layout.addWidget(self.name_label)
        info_layout.addWidget(self.model_label)
        info_layout.addWidget(self.type_label)
        info_layout.addWidget(self.port_label)
        left_layout.addWidget(info_group)

        # Connected devices section
        devices_group = QGroupBox("Connected Devices")
        devices_layout = QVBoxLayout(devices_group)

        self.device_list = QListWidget()
        devices_layout.addWidget(self.device_list)


        left_layout.addWidget(devices_group)

        h_layout.addWidget(left_widget, stretch=1)

        # --- RIGHT SIDE: device dropdown in a group ---
        right_widget = QWidget()
        right_layout = QVBoxLayout(right_widget)


        selector_group = QGroupBox("Channel Control")
        selector_layout = QVBoxLayout(selector_group)
        self.deviceSelector : QComboBox = QComboBox()
        self.deviceSelector.currentIndexChanged.connect(self.ReceiveValues)
        selector_layout.addWidget(QLabel("Select Connection:"))
        selector_layout.addWidget(self.deviceSelector)
        selector_layout.addStretch(1)
        selector_group.setLayout(selector_layout)
        right_layout.addWidget(selector_group)

        # --- 3x3 Grid of Groups ---
        grid_widget = QWidget()
        grid_layout = QGridLayout(grid_widget)

        # Create Gain Section
        gainGroup = QGroupBox("Gain")
        gainLayout = QVBoxLayout(gainGroup)
        self.gainDial : QDial = QDial()
        self.gainDial.setMinimum(0)
        self.gainDial.setMaximum(1000)
        self.gainDial.setValue(0)
        self.gainDial.valueChanged.connect(self.SendValues)
        gainLayout.addWidget(self.gainDial)
        self.gainValueLabel = QLabel(str(self.gainDial.value()))
        self.gainValueLabel.setAlignment(Qt.AlignmentFlag.AlignHCenter)
        self.padButton = QPushButton("Pad")
        self.padButton.setCheckable(True)
        self.padButton.clicked.connect(self.SendValues)
        gainLayout.addWidget(self.gainValueLabel)
        gainLayout.addWidget(self.padButton)
        gainGroup.setLayout(gainLayout)
        grid_layout.addWidget(gainGroup, 0, 0)

        right_layout.addWidget(grid_widget)

        h_layout.addWidget(right_widget, stretch=2)
        self.ReceiveValues()

    def UpdateDeviceList(self):
        # Save current selection for the connection selector
        self.device_list.clear()
        for device_name, device_info in Variables.devices.items():
            status = "Connected" if device_info.isVirgilDevice else "Disconnected"
            self.device_list.addItem(f"{device_name} - {status}")

        # Save current label before clearing
        selectedLabel : str = self.deviceSelector.currentText() if self.deviceSelector.currentIndex() >= 0 else None
        self.selectedConn : Variables.DeviceConnection = None
        self.deviceSelector.clear()
        for conn in Variables.connections:
            if conn.connectedDevice not in Variables.devices or not Variables.devices[conn.connectedDevice].isVirgilDevice:
                continue
            # Build a readable label for each connection
            selfType = conn.selfType if conn.selfType else ""
            selfIndex = conn.selfIndex + 1 if conn.selfIndex is not None else ""
            channelType = conn.channelType if conn.channelType else ""
            channelIndex = conn.channelIndex + 1 if conn.channelIndex is not None else ""
            label = f"{conn.connectedDevice}:{selfType} {selfIndex} -> {channelType} {channelIndex}"
            self.deviceSelector.addItem(label, userData=conn)

            # Restore previous selection by label if possible
            if selectedLabel is label:
                self.deviceSelector.setCurrentIndex(self.deviceSelector.count() - 1)
                self.selectedConn = conn

        if not self.selectedConn or (self.selectedConn.channelIndex, self.selectedConn.channelType) not in Variables.devices[self.selectedConn.connectedDevice].channels:
            self.gainDial.setEnabled(False)
            self.padButton.setEnabled(False)
            self.gainValueLabel.setText("NA")
            return

    def ReceiveValues(self):
        """Update the GUI with current data and persist dropdown selection"""
        self.selectedConn = self.deviceSelector.currentData()
        if not self.selectedConn:
            return
        device = Variables.devices[self.selectedConn.connectedDevice]
        key = (self.selectedConn.channelIndex, self.selectedConn.channelType)
        if key not in device.channels:
            Variables.PrintRed(f"Channel {key} not found in device {device.deviceName}. Refreshing...")
            self.UpdateDeviceList()
            return

        Variables.PrintYellow(f"{key}: {json.dumps(device.channels[key], indent=2)}")

        if "gain" in device.channels[key]:
            step = device.channels[key]["gain"]["precision"]
            self.gainDial.setValue(device.channels[key]["gain"]["value"] * 10)
            self.gainDial.setMinimum(device.channels[key]["gain"]["minValue"] * 10)
            self.gainDial.setMaximum(device.channels[key]["gain"]["maxValue"] * 10)
            padLevel = 0
            if "pad" in device.channels[key] and device.channels[key]["pad"]["value"]:
                padLevel = device.channels[key]["padLevel"]["value"]
            # Format display based on step size
            if round(step) == step:
                self.gainValueLabel.setText(f"{self.gainDial.value() / 10 + padLevel:.0f}")  # No decimal places for whole numbers
            else:
                self.gainValueLabel.setText(f"{self.gainDial.value() / 10 + padLevel:.1f}")  # One decimal place for fractional steps
            if not device.channels[key]["gain"]["readOnly"]:
                self.gainDial.setEnabled(True)
            else:
                self.gainDial.setEnabled(False)
        else:
            self.gainDial.setEnabled(False)

        if "pad" in device.channels[key]:
            self.padButton.setChecked(device.channels[key]["pad"]["value"])
            if not device.channels[key]["pad"]["readOnly"]:
                self.padButton.setEnabled(True)
            else:
                self.padButton.setEnabled(False)
        else:
            self.padButton.setEnabled(False)

        

    def SendValues(self):
        device = Variables.devices[self.selectedConn.connectedDevice]
        key = (self.selectedConn.channelIndex, self.selectedConn.channelType)
        if self.padButton.isEnabled() and device.channels[key]["pad"]["value"] != self.padButton.isChecked():
            device.messageQueue.append(Variables.CreateCommand(self.selectedConn.channelIndex, self.selectedConn.channelType, "pad", self.padButton.isChecked()))

        if self.gainDial.isEnabled():
            step = device.channels[key]["gain"]["precision"]
            value = self.gainDial.value()
            actualValue = round(value / 10,3)

            # For whole number steps, base the step off minValue instead of 0
            minValue = self.gainDial.minimum() / 10
            steps_from_min = round((actualValue - minValue) / step)
            snapped_value = round(minValue + (steps_from_min * step), 3)

            if device.channels[key]["gain"]["value"] != snapped_value:
                device.messageQueue.append(Variables.CreateCommand(self.selectedConn.channelIndex, self.selectedConn.channelType, "gain", snapped_value))


# Create and run the GUI
if not QApplication.instance():
    app = QApplication(sys.argv)
else:
    app = QApplication.instance()

gui = VirgilGUI()
gui.show()
Variables.SetGUIReference(gui)

try:
    app.exec()
except KeyboardInterrupt:
    Variables.PrintRed("Shutting down...")
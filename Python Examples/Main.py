import Variables
from zeroconf import Zeroconf, ServiceInfo, ServiceBrowser
import socket
import json
import threading
import os
import sys
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                                QHBoxLayout, QPushButton, QLabel, QListWidget, 
                                QTextEdit, QGroupBox)
from PyQt6.QtCore import QTimer
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
            print(f"Device removed: {name}")

    def update_service(self, zeroconf, type, name):
        # Handle service updates - can be empty if not needed
        pass

    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        # Extract the base name of the service to compare with selfName
        base_name = name.split('.')[0]  # Assuming the name format is "<base_name>.<type>"
        if info and base_name != Variables.selfName:
            if name not in found_devices:
                found_devices.append(base_name)
                print(f"Found device: {base_name}")
                for conn in Variables.connections:
                    if conn.connectedDevice != base_name:
                        continue
                    if conn.connectedDevice not in Variables.devices:
                        Variables.devices[conn.connectedDevice] = Variables.DeviceInfo(
                            conn.connectedDevice, 
                            socket.inet_ntoa(info.addresses[0]), 
                            queue=[
                                Variables.CreateBase(Variables.CreateInfoRequest(-1, conn.channelType)),
                                Variables.CreateBase(Variables.CreateInfoRequest(conn.channelIndex, conn.channelType)),
                                Variables.CreateBase(Variables.CreateChannelLink(conn.selfIndex, conn.selfType, conn.channelIndex, conn.channelType))
                            ]
                        )
                    else:
                        Variables.devices[conn.connectedDevice].messageQueue.append(
                            Variables.CreateBase(Variables.CreateInfoRequest(conn.channelIndex, conn.channelType))
                        )
                        Variables.devices[conn.connectedDevice].messageQueue.append(
                            Variables.CreateBase(Variables.CreateChannelLink(conn.selfIndex, conn.selfType, conn.channelIndex, conn.channelType))
                        )


def NetListener():
    """
    Listens for TCP connections and creates DeviceInfo for each new socket.
    """
    print(f"Listening for TCP messages on port {Variables.virgilPort}...")

    listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listen_sock.bind(("", Variables.virgilPort))
    listen_sock.listen(5)

    try:
        while True:
            conn, addr = listen_sock.accept()
            remote_ip = addr[0]
            print(f"Accepted connection from {remote_ip}")
            try:
                data = conn.recv(4096)
                if not data:
                   conn.close()
                   continue
                j: dict= json.loads(data.decode('utf-8'))
                deviceName = j.get("transmittingDevice")
                if not deviceName:
                    conn.close()
                    continue
                Variables.devices[deviceName] = Variables.DeviceInfo(deviceName, remote_ip, conn, data)
            except Exception as e:
                print(f"Error receiving initial data from {remote_ip}: {e}")
                conn.close()
    except KeyboardInterrupt:
        print("NetListener shutting down...")
    finally:
        listen_sock.close()


configFiles = [f for f in os.listdir('.') if f.endswith('.config')]
print(configFiles)
if not configFiles:
    print("No .config files found in the current directory.")
else:
    print("Select a .config file:")
    for idx, fname in enumerate(configFiles):
        print(f"{idx + 1}: {fname}")
    while True:
        choice = input("Enter the number of the file you want to use: ")
        try:
            idx = int(choice) - 1
            if idx < 0 or idx >= len(configFiles):
                print("Invalid selection.")
                continue
            selectedFile = configFiles[idx]
            print(f"You selected: {selectedFile}")
            Variables.LoadConfig(selectedFile)
            break
        except ValueError:
            print("Invalid selection.")


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

# The actual protocol would use Dante's mDNS, but I can't do that
# Instead, this is using virgil mDNS. For a proper implementation, you would use Dante's mDNS.
print("Scanning for mDNS devices...")
browser = ServiceBrowser(zeroconf, "_virgil._tcp.local.", MDNSListener())

# PyQt GUI
class VirgilGUI(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Virgil Protocol Monitor")
        self.setGeometry(100, 100, 600, 400)
        
        # Set up the main widget and layout
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        layout = QVBoxLayout(main_widget)
        
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
        layout.addWidget(info_group)
        
        # Connected devices section
        devices_group = QGroupBox("Connected Devices")
        devices_layout = QVBoxLayout(devices_group)
        
        self.device_list = QListWidget()
        devices_layout.addWidget(self.device_list)
        
        refresh_btn = QPushButton("Refresh")
        refresh_btn.clicked.connect(self.refresh_devices)
        devices_layout.addWidget(refresh_btn)
        layout.addWidget(devices_group)
        
        # Log section
        log_group = QGroupBox("Activity Log")
        log_layout = QVBoxLayout(log_group)
        
        self.log_text = QTextEdit()
        self.log_text.setMaximumHeight(100)
        self.log_text.setReadOnly(True)
        log_layout.addWidget(self.log_text)
        layout.addWidget(log_group)
        
        # Set up timer to update the GUI
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_gui)
        self.timer.start(1000)  # Update every second
        
        self.log("Virgil GUI started")
    
    def update_gui(self):
        """Update the GUI with current data"""
        self.device_list.clear()
        for device_name, device_info in Variables.devices.items():
            status = "Connected" if device_info.isVirgilDevice else "Disconnected"
            self.device_list.addItem(f"{device_name} - {status}")
    
    def refresh_devices(self):
        """Refresh the device list"""
        self.update_gui()
        self.log("Device list refreshed")
    
    def log(self, message):
        """Add a message to the log"""
        from datetime import datetime
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.log_text.append(f"[{timestamp}] {message}")

# Create and run the GUI
if not QApplication.instance():
    app = QApplication(sys.argv)
else:
    app = QApplication.instance()

gui = VirgilGUI()
gui.show()

try:
    app.exec()
except KeyboardInterrupt:
    print("Shutting down...")






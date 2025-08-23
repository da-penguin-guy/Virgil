import time
import traceback
import Variables
import os
os.system('cls' if os.name == 'nt' else 'clear')
from zeroconf import Zeroconf, ServiceInfo, ServiceBrowser, ServiceListener
import socket
import json
import threading
import os
import struct
from smbus2 import SMBus


# Look, I'm sorry about python. I don't like it, but I like c++ even less

os.chdir(os.path.dirname(os.path.abspath(__file__)))

selfIp = Variables.GetIp()

deviceAddr = 0x42

found_devices = []

class MDNSListener(ServiceListener):
    def remove_service(self, zeroconf, type, name):
        # Remove device from found_devices if present
        base_name = name.split('.')[0] 
        if base_name in found_devices:
            found_devices.remove(base_name)
            Variables.PrintBlue(f"Device removed: {base_name}")

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
            deviceFound = False
            for conn in Variables.knownConnections:
                if conn.connectedDevice == base_name:
                    deviceFound = True
                    break
            if not deviceFound:
                Variables.PrintBlue(f"Device not found in known connections: {base_name}")
                return
            # Create the device with the found IP address
            CreateDevice(base_name, socket.inet_ntoa(info.addresses[0]))


def CreateDevice(deviceName : str, deviceIp : str, sock : socket.socket | None = None, startingMessage: bytes | None = None):
    infoRequests : list[dict] = []
    channelLink : list[dict] = []

    #Loop over every connection for this device and add the needed messages
    for connection in Variables.knownConnections:
        if connection.connectedDevice != deviceName:
            continue
        if connection.channelIndex is not None and connection.channelType is not None:
            infoRequests.append(Variables.CreateInfoRequest(connection.channelIndex, connection.channelType))
            channelLink.append(Variables.CreateChannelLink(connection.selfIndex, connection.selfType, connection.channelIndex, connection.channelType))
    #Create the device
    #These messages are split up into 3 because you may want to do check a channel exists first
    #I don't do that, but it would be a good idea
    #It is also fine if you don't because you'll just get an error message
    queue_messages = [[Variables.CreateInfoRequest(-1)], infoRequests, channelLink]
    Variables.devices[deviceName] = Variables.DeviceInfo(
                deviceName=deviceName,
                ip=deviceIp,
                sock=sock,
                startingMessage=startingMessage,
                queue=queue_messages
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
                # Receive and parse length-prefixed message
                recv_buffer = bytearray()
                
                # First, get the 4-byte length header
                while len(recv_buffer) < 4:
                    chunk = sock.recv(4 - len(recv_buffer))
                    if not chunk:
                        sock.close()
                        continue
                    recv_buffer.extend(chunk)
                
                # Extract message length
                msg_len = struct.unpack('!I', recv_buffer[:4])[0]
                
                # Now receive the full message payload
                while len(recv_buffer) < 4 + msg_len:
                    chunk = sock.recv(4 + msg_len - len(recv_buffer))
                    if not chunk:
                        sock.close()
                        continue
                    recv_buffer.extend(chunk)
                
                # Extract the JSON message
                message_bytes = bytes(recv_buffer[4:4 + msg_len])
                
                # Parse JSON
                j: dict = json.loads(message_bytes.decode('utf-8'))
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
                CreateDevice(deviceName, remote_ip, sock, message_bytes)
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
                Variables.PrintRed(f"Invalid selection.\n {traceback.format_exc()}")
                continue
            selectedFile = configFiles[idx]
            print(f"You selected: {selectedFile}")
            Variables.LoadConfig(selectedFile)
            break
        except ValueError:
            Variables.PrintRed(f"Invalid selection. \n {traceback.format_exc()}")


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

def Normalize(value, currentMin,currentMax,desiredMin,desiredMax):
    # Normalize a value from one range to another
    currentRange = currentMax - currentMin
    desiredRange = desiredMax - desiredMin
    return round(((value - currentMin) * desiredRange) / currentRange + desiredMin, 1)
selectedChannel = (0, "tx")
lastFaderValue = None
lastLedValue = None
lastUpdateTime = time.time()
POT_TOLERANCE = 0.5  # Tolerance for potentiometer noise
with SMBus(1) as bus:
    while True:
        time.sleep(0.05)
        if selectedChannel not in Variables.channels:
            continue
        currentChannel = Variables.channels[selectedChannel]
        data = []
        for reg in range(5):
            bus.write_byte(deviceAddr, reg)  # Set register address
            time.sleep(0.05)  # Longer wait for the device to process
            # Read twice - first read might be stale
            dummy = bus.read_byte(deviceAddr)  
            time.sleep(0.01)
            data.append(bus.read_byte(deviceAddr))  # Read value from that register
            time.sleep(0.01)

        potValue = (data[1] << 8) | data[0]
        potValue = potValue & 0x0FFF  # Ensure it's 12-bit
        potValue = Normalize(potValue, 0, 4095, 0, 100)  # Normalize to 0-100%
        faderValue = (data[3] << 8) | data[2]
        faderValue = faderValue & 0x0FFF  # Ensure it's 12-bit
        ledValue = bool(data[4] & 0x01)

        paramsUpdated = []
        if lastLedValue != ledValue:
            currentChannel["rfEnable"]["value"] = ledValue
            paramsUpdated.append("rfEnable")
            Variables.PrintYellow(f"Led state changed to {ledValue}")
        elif ledValue != currentChannel["rfEnable"]["value"]:
            ledValue = currentChannel["rfEnable"]["value"]
            Variables.PrintYellow(f"Led should now be {ledValue}")
            bus.write_byte_data(deviceAddr, 4, ledValue)
        lastLedValue = ledValue

        # Check potentiometer with tolerance
        if abs(potValue - currentChannel["rfLevel"]["value"]) > POT_TOLERANCE:
            Variables.PrintYellow(f"Potentiometer value changed to {potValue}")
            currentChannel["rfLevel"]["value"] = potValue
        
        if faderValue != currentChannel["gain"]["value"]:
            currentChannel["gain"]["value"] = faderValue
            paramsUpdated.append("gain")
        elif faderValue != currentChannel["gain"]["value"]:
            bus.write_byte_data(deviceAddr, 0, faderValue)
        lastFaderValue = faderValue

        if paramsUpdated:
            Variables.SendStatusUpdate(selectedChannel[0],selectedChannel[1],"", paramsUpdated)

        if time.time() - lastUpdateTime > .5:
            Variables.SendStatusUpdate(selectedChannel[0],selectedChannel[1],"", ["rfLevel", "batteryLevel", "audioLevel"])
            lastUpdateTime = time.time()
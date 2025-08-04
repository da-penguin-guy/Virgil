from socket import socket
import socket
import threading
import time
import json

virgilPort = 7889

# Global variables for device identity
selfName = None
selfModel = None
selfType = None

class DeviceConnection:
    def __init__(self, connectedDevice: str, selfIndex: int, selfType: str, channelIndex: int = None, channelType: str = None):
        self.connectedDevice = connectedDevice
        self.channelIndex = channelIndex
        self.channelType = channelType
        self.selfIndex = selfIndex
        self.selfType = selfType
        AddSubscription(connectedDevice, selfIndex, selfType)

    def CheckForRemove(self, connectedDevice: str, channelIndex: int, channelType: str, selfIndex: int, selfType: str):
        if(self.connectedDevice == connectedDevice and
                self.channelIndex == channelIndex and
                self.channelType == channelType and
                self.selfIndex == selfIndex and
                self.selfType == selfType):
            self.Remove()

    def Remove(self):
        """
        Remove this connection from the subscriptions.
        """
        RemoveSubscription(self.connectedDevice, self.selfIndex, self.selfType)
        connections.pop(self)

def AddSubscription(connectedDevice: str, selfIndex: int = None, selfType: str = None):
    key = (selfIndex, selfType)
    if key not in subscriptionList:
        subscriptionList[key] = []
    if connectedDevice not in subscriptionList[key]:
        subscriptionList[key].append(connectedDevice)

def RemoveSubscription(connectedDevice: str, selfIndex: int = None, selfType: str = None):
    key = (selfIndex, selfType)
    if key in subscriptionList and connectedDevice in subscriptionList[key]:
        subscriptionList[key].remove(connectedDevice)

    
class DeviceInfo:
    name = ""
    model = ""
    deviceType = ""
    deviceIp = ""
    virgilVersion = ""
    channelCounts : dict[str, int] = {}
    channels : dict[tuple[int,str], dict] = {}
    isVirgilDevice : bool = None
    ongoingCommunication : bool = False
    conn : socket.socket = None
    messageQueue : list[dict] = []
    disabled = False


    def __init__(self, deviceName: str, ip: str, conn : socket.socket = None, startingMessage: bytes = None, queue: list[dict] = []):
        thread = threading.Thread(target=self.Run, args=(deviceName, ip, queue, conn, startingMessage), daemon=True)
        thread.start()

    def SendMessage(self, messages : dict):
        """
        Send a JSON object to the specified IP using TCP.
        """
        # Send via TCP
        jsonInfo = json.dumps(messages)
        print(f"Sending message to {self.deviceIp}: \n {jsonInfo}")
        self.conn.sendall(jsonInfo.encode('utf-8'))


    def ProcessMessage(self, raw : bytes, ip : str):
        # Always trust the latest IP address
        self.deviceIp = ip
        try:
            packet = json.loads(raw.decode('utf-8'))
        except json.JSONDecodeError:
            print(f"Failed to decode JSON from {ip}: {raw}")
            return CreateError("MalformedMessage", "The JSON received is malformed.")
        print(f"Received message from {ip}: \n {raw.decode('utf-8')}")
        if "transmittingDevice" not in packet or not isinstance(packet["transmittingDevice"], str) or not packet["transmittingDevice"]:
            print("Message does not contain 'transmittingDevice'.")
            return CreateError("MalformedMessage", "The JSON received is missing 'transmittingDevice'.")
        if packet["transmittingDevice"] != self.deviceName:
            print(f"Message from {packet['transmittingDevice']} received packet not for it")
            return CreateError("InternalError", f"Device Name mismatch: {packet['transmittingDevice']} != {selfName}")
        name = packet.get("transmittingDevice", "")
        if "messages" not in packet or not isinstance(packet["messages"], list) or not packet["messages"]:
            print("Message does not contain 'messages'.")
            return CreateError("MalformedMessage", "The JSON received is missing 'messages'.")
        returnMessages: list[dict] = []
        self.ongoingCommunication = True
        for msg in packet["messages"]:
            if not isinstance(msg, dict) or not msg:
                print("Invalid message format.")
                returnMessages.append(CreateError("MalformedMessage", "One of the messages is not a valid JSON object."))
                continue

            if "messageType" not in msg or not isinstance(msg["messageType"], str) or not msg["messageType"]:
                print("Message missing 'messageType'.")
                returnMessages.append(CreateError("MalformedMessage", "Message missing 'messageType'."))
                continue
            msgType = msg["messageType"]


            if msgType == "parameterCommand":
                if "channelIndex" not in msg or "channelType" not in msg:
                    print("Parameter command message missing 'channelIndex' or 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Parameter command message missing 'channelIndex' or 'channelType'."))
                    continue
                channelIndex = msg.pop("channelIndex")
                channelType = msg.pop("channelType")
                msg.pop("messageType")
                for key,value in msg.items():
                    returnMessages.append(ProcessParamChange(channelIndex, channelType, key, value))
                
                response = SendStatusUpdate(channelIndex, channelType, name, list(msg.keys()))
                returnMessages.append(response)
            
            elif msgType == "statusUpdate":
                returnMessages.extend(self.Update(ip, msg))

            elif msgType == "statusRequest":
                if "channelIndex" not in msg or "channelType" not in msg:
                    print("Status request message missing 'channelIndex' or 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Status request message missing 'channelIndex' or 'channelType'."))
                    continue
                returnMessages.append(CreateStatusUpdate(msg["channelIndex"], msg["channelType"]))
            
            elif msgType == "channelLink":
                if "channelIndex" not in msg or "channelType" not in msg:
                    print("Channel link message missing 'channelIndex' or 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Channel link message missing 'channelIndex' or 'channelType'."))
                    continue
                connections.append(DeviceConnection(
                    connectedDevice=name,
                    channelIndex=msg.get("selfIndex", None),
                    channelType=msg.get("selfType", None),
                    selfIndex=msg["channelIndex"],
                    selfType=msg["channelType"],
                    deviceIp=ip
                ))
            
            elif msgType == "channelUnlink":
                if "channelIndex" not in msg or "channelType" not in msg:
                    print("Channel unlink message missing 'channelIndex' or 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Channel unlink message missing 'channelIndex' or 'channelType'."))
                    continue
                for conn in connections:
                    conn.CheckForRemove(
                        connectedDevice=name,
                        channelIndex=msg.get("selfIndex", None),
                        channelType=msg.get("selfType", None),
                        selfIndex=msg["channelIndex"],
                        selfType=msg["channelType"],
                    )

            elif msgType == "infoRequest":
                if "channelIndex" not in msg:
                    print("Info request message missing 'channelIndex'.")
                    returnMessages.append(CreateError("MalformedMessage", "Info request message missing 'channelIndex'."))
                    continue
                # Device-level info requests (channelIndex = -1) don't require channelType
                if msg["channelIndex"] != -1 and "channelType" not in msg:
                    print("Info request message missing 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Info request message missing 'channelType'."))
                    continue
                channelType = msg.get("channelType", "")
                returnMessages.append(CreateInfoResponse(msg["channelIndex"], channelType))

            elif msgType == "infoResponse":
                returnMessages.extend(self.Update(ip, msg))

            elif msgType == "errorResponse":
                print(f"Error response received: {msg.get('error', {}).get('errorString', 'Unknown error')}")
            
            elif msgType == "subscribeMessage":
                if "channelIndex" not in msg or "channelType" not in msg:
                    print("Subscribe message missing 'channelIndex' or 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Subscribe message missing 'channelIndex' or 'channelType'."))
                    continue
                AddSubscription(name, msg["channelIndex"], msg["channelType"])
            
            elif msgType == "unsubscribeMessage":
                if "channelIndex" not in msg or "channelType" not in msg:
                    print("Unsubscribe message missing 'channelIndex' or 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Unsubscribe message missing 'channelIndex' or 'channelType'."))
                    continue
                RemoveSubscription(name, msg["channelIndex"], msg["channelType"])

            elif msgType == "endResponse":
                self.ongoingCommunication = False
                return None

            else:
                print(f"Unknown message type: {msgType}")
                returnMessages.append(CreateError("UnrecognizedCommand", f"Unknown message type: {msgType}"))
                continue

        return returnMessages

    def Run(self, deviceName: str, ip: str, queue: list[dict], conn: socket.socket = None, startingMessage: bytes = None):
        self.conn = conn
        self.deviceName = deviceName
        self.deviceIp = ip
        self.messageQueue = queue
        print(f"Device {self.deviceName} started with IP {self.deviceIp}.")
        print(f"Device {self.deviceName} is {'connected' if self.conn else 'not connected'}.")
        try:
            if not self.conn:
                self.conn = socket.socket()
                self.conn.connect((self.deviceIp, virgilPort))
            self.conn.settimeout(2.0)  # 2 second timeout
            self.isVirgilDevice = True
            print("Device Connected")
        except Exception as e:
            self.isVirgilDevice = False
            print(f"Failed to connect to device {self.deviceName} at {self.deviceIp}: {e}")
            return
        if startingMessage:
            try:
                response = self.ProcessMessage(startingMessage, self.deviceIp)
                if response:
                    self.SendMessage(CreateBase(response))
            except Exception as e:
                print(f"Error processing starting message: {e}")
                return
        elif self.messageQueue:
            self.ongoingCommunication = True
            self.SendMessage(self.messageQueue.pop(0))
        while not self.disabled:
            try:
                try:
                    data = self.conn.recv(4096)
                except TimeoutError:
                    continue
                except OSError as e:
                    if getattr(e, 'errno', None) == 10060:  # Windows timeout
                        continue
                    raise
                if not data:
                    print(f"Connection closed by remote device {self.deviceIp}")
                    self.End()
                response = self.ProcessMessage(data, self.deviceIp)
                if not response:
                    if self.messageQueue:
                        self.ongoingCommunication = True
                        response.append(self.messageQueue.pop(0))
                    else:
                        self.ongoingCommunication = False
                        response.append(CreateEndResponse())
                self.SendMessage(CreateBase(response))
            except Exception as e:
                if not self.disabled:
                    print(f"Error receiving data from {self.deviceIp}: {e}")
                continue

    def Update(self, ip: str, infoResponse: dict) -> list[dict]:
        # Always trust the latest IP address
        self.deviceIp = ip
        errors : list[dict] = []
        if "channelIndex" not in infoResponse:
            errors.append(CreateError("MalformedMessage", "Info response missing 'channelIndex'."))
            return errors
        if "channelType" not in infoResponse:
            errors.append(CreateError("MalformedMessage", "Info response missing 'channelType'."))
            return errors
        channelIndex = infoResponse["channelIndex"]
        channelType = infoResponse["channelType"]
        if channelIndex == -1:
            if "deviceModel" not in infoResponse:
                errors.append(CreateError("MalformedMessage", "Info response missing 'deviceModel'."))
            if "deviceType" not in infoResponse:
                errors.append(CreateError("MalformedMessage", "Info response missing 'deviceType'."))
            if "virgilVersion" not in infoResponse:
                errors.append(CreateError("MalformedMessage", "Info response missing 'virgilVersion'."))
            if "channelCounts" not in infoResponse:
                errors.append(CreateError("MalformedMessage", "Info response missing 'channelCounts'."))
            if errors:
                return errors
            self.deviceModel = infoResponse["deviceModel"]
            self.deviceType = infoResponse["deviceType"]
            self.virgilVersion = infoResponse["virgilVersion"]
            self.channelCounts = infoResponse["channelCounts"]
            return
        infoResponse.pop("channelIndex")
        infoResponse.pop("channelType")
        infoResponse.pop("messageType")
        if (channelIndex,channelType) not in self.channels:
            self.channels[(channelIndex,channelType)] = {}
        self.channels[(channelIndex,channelType)].update(infoResponse)
        return None

    def End(self):
        """
        End the ongoing communication and clean up resources.
        """
        if self.conn:
            self.conn.close()
            self.conn = None
        self.disabled = True
        for conn in connections:
            if conn.connectedDevice == self.deviceName:
                conn.Remove()
        devices.pop(self.deviceName)

def GetIp():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('8.8.8.8', 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = '127.0.0.1'
    finally:
        s.close()
    return ip

def CreateBase(messages) -> dict:
    """
    Create a base message structure.
    If messages is a dict, it will be converted to a list.
    """
    if isinstance(messages, dict):
        messages = [messages]
    return {
        "transmittingDevice": selfName,
        "messages": messages
    }

def CreateError(errorValue : str, errorString : str) -> dict:
    """
    Create an error message.
    """
    return {
        "messageType" : "errorResponse",
        "errorValue" : errorValue,
        "errorString" : errorString
    }

def CreateChannelLink(selfIndex :int, selfType : str, channelIndex : int, channelType : str) -> dict:
    """
    Create a channel link message.
    """
    return {
        "messageType": "channelLink",
        "sendingChannelIndex": selfIndex,
        "sendingChannelType": selfType,
        "channelIndex": channelIndex,
        "channelType": channelType
    }

def CreateInfoRequest(channelIndex : int, channelType : str = "") -> dict:
    """
    Create an info request message.
    """
    if(channelType == ""):
        return {
            "messageType": "infoRequest",
            "channelIndex": channelIndex,
        }
    else:
        return {
            "messageType": "infoRequest",
            "channelIndex": channelIndex,
            "channelType": channelType
        }
    
def CreateInfoResponse(channelIndex : int, channelType : str = "") -> dict:
    """
    Create an info response message.
    """
    response : dict = {"messageType": "infoResponse", "channelIndex": channelIndex}
    
    # Handle device-level info requests (channelIndex = -1)
    if channelIndex == -1:
        response.update({
            "deviceModel": selfModel,
            "deviceType": selfType,
            "virgilVersion": "2.0.0",
            "channelCounts": {}  # You should populate this based on your device's channels
        })
        return response
    
    # Handle channel-specific info requests
    if channelType:
        response["channelType"] = channelType
    
    key = (channelIndex, channelType)
    if key not in channels:
        return CreateError("ChannelIndexInvalid", f"Channel index {channelIndex} out of range for {channelType} channels.")
    response.update(channels[key])
    return response

def CreateStatusUpdate(channelIndex: int, channelType: str = "", params : list[str] = None) -> dict:
    """
    Create a status update message.
    """
    #This doesn't account for things other then the values being changed, but it should be fine for now
    if (channelIndex, channelType) not in channels:
        return CreateError("ChannelIndexInvalid", f"Channel index {channelIndex} out of range for {channelType} channels.")
    response :dict = {"messageType": "statusUpdate", "channelIndex": channelIndex, "channelType": channelType}
    for key, value in channels[(channelIndex, channelType)].items():
        if key == "channelIndex" or key == "channelType":
            continue
        if key in params or not params:
            if isinstance(value, dict):
                response[key] = value.get("value", None)
            else:
                response[key] = value
    return response

def CreateEndResponse() -> dict:
    """
    Create an end response message.
    """
    return {
        "messageType": "endResponse",
    }

def ProcessParamChange(channelIndex: int, channelType: str, paramName: str, value: any) -> dict:
    """
    Process a parameter change message.
    """

    key = (channelIndex, channelType)
    if key not in channels:
        return CreateError("ChannelIndexInvalid", f"Channel index {channelIndex} out of range for {channelType} channels.")
    if paramName not in channels[key]:
        return CreateError("ParameterUnsupported", f"Channel {channelIndex} of type {channelType} does not have a parameter named {paramName}.")
    if channels[key][paramName]["readOnly"]:
        return CreateError("ParameterReadOnly", f"Parameter {paramName} is read-only and cannot be changed.")
    
    dataType = channels[key][paramName]["dataType"]
    if dataType == "number":
        if not isinstance(value, (int, float)):
            return CreateError("InvalidValueType", f"Parameter {paramName} must be a number.")
        minValue = channels[key][paramName].get("minValue", float('-inf'))
        maxValue = channels[key][paramName].get("maxValue", float('inf'))
        precision = channels[key][paramName].get("precision", 1.0)
        isValid = (value - minValue) % precision and value > minValue and value < maxValue
        if not isValid:
            return CreateError("ValueOutOfRange", f"Parameter {paramName} must be a number between {minValue} and {maxValue} with precision {precision}.")
    elif dataType == "bool":
        if not isinstance(value, bool):
            return CreateError("InvalidValueType", f"Parameter {paramName} must be a boolean.")
    elif dataType == "string":
        if not isinstance(value, str):
            return CreateError("InvalidValueType", f"Parameter {paramName} must be a string.")
    elif dataType == "enum":
        if value not in channels[key][paramName]["enumValues"]:
            return CreateError("InvalidValueType", f"Parameter {paramName} must be one of {channels[key][paramName]['enumValues']}.")
    else:
        return CreateError("InternalError", f"Parameter {paramName} has an unsupported data type: {dataType}.")

    channels[key][paramName]["value"] = value
    return []

def SendStatusUpdate(channelIndex: int, channelType: str, exclude : str, params: list[str]) -> dict:
    """
    Send a status update message.
    """
    response = CreateStatusUpdate(channelIndex, channelType, params)
    
    # Send to all subscribed devices except the one specified
    for name in subscriptionList.get((channelIndex, channelType), []):
        if name != exclude:
            try:
                device = devices[name]
                if device.isVirgilDevice and device.conn:
                    device.messageQueue.append(response)
            except Exception as e:
                print(f"Error sending message to device {name}: {e}")
    return response

def LoadConfig(filepath: str):
    """
    Load the channel configuration.
    """
    global selfName, selfModel, selfType
    with open(filepath, 'r') as f:
        config = json.load(f)
    selfName = config["Name"]
    selfModel = config["Model"]
    selfType = config["Type"]
    for channel in config.get("Channels", []):
        channelIndex = channel["channelIndex"]
        channelType = channel["channelType"]
        channels[(channelIndex, channelType)] = channel
    for conn in config.get("Connections", []):
        connections.append(DeviceConnection(
            connectedDevice = conn["name"],
            selfIndex = conn["selfIndex"],
            selfType = conn["selfType"],
            channelIndex = conn.get("channelIndex", None),
            channelType = conn.get("channelType", None)
        ))


#If this channel setup makes no sense, it's because it's a spectera 2x2 setup, which is the most complicated setup I could think of.
channels : dict[tuple[int,str],dict] = {
}

#This only has rx connections because that's all the dante device will know
#These IPs would be found using Dante discovery
connections : list[DeviceConnection]= [
]

devices : dict[str, DeviceInfo] = {
}

subscriptionList : dict[(int,str), list[str]] = {
}

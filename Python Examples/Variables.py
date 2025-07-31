from socket import socket
import threading
import time
import json

class DeviceConnection:
    def __init__(self, connectedDevice: str, deviceIp: str, channelIndex: int, channelType: str, selfIndex: int = None, selfType: str = None):
        self.connected = None
        self.connectedDevice = connectedDevice
        self.deviceIp = deviceIp
        self.channelIndex = channelIndex
        self.channelType = channelType
        self.selfIndex = selfIndex
        self.selfType = selfType


class DeviceInfo:
    name = ""
    model = ""
    deviceType = ""
    deviceIp = ""
    virgilVersion = ""
    channelCounts : dict[str, int] = {}
    channels : dict[int, dict] = {}
    isVirgilDevice : bool = None
    ongoingCommunication : bool = False
    conn : socket.socket = None
    messageQueue : list[dict] = []
    disabled = False

    def __init__(self, deviceName: str, ip: str, selfInit: bool = False, queue: list[dict] = []):
        # Start the connection logic in a new thread
        thread = threading.Thread(target=self.Run, args=(deviceName, ip, queue, selfInit), daemon=True)
        thread.start()

    def SendMessage(self, messages : dict):
        """
        Send a JSON object to the specified IP using TCP.
        """
        # Send via TCP
        self.conn.sendall(json.dumps(messages).encode('utf-8'))


    def ProcessMessage(self, raw : bytes, ip : str):
        # Always trust the latest IP address
        self.deviceIp = ip
        try:
            packet = json.loads(raw.decode('utf-8'))
        except json.JSONDecodeError:
            print(f"Failed to decode JSON from {ip}: {raw}")
            return CreateError("MalformedMessage", "The JSON received is malformed.")
        if "transmittingDevice" not in packet or not isinstance(packet["transmittingDevice"], str) or not packet["transmittingDevice"]:
            print("Message does not contain 'transmittingDevice'.")
            return CreateError("MalformedMessage", "The JSON received is missing 'transmittingDevice'.")
        if packet["transmittingDevice"] != selfName:
            print(f"Message from {packet['transmittingDevice']} is not for this device.")
            return CreateError("InternalError", f"Device Name mismatch: {packet['transmittingDevice']} != {selfName}")
        name = packet.get("transmittingDevice", "")
        if "messages" not in packet or not isinstance(packet["messages"], list) or not packet["messages"]:
            print("Message does not contain 'messages'.")
            return CreateError("MalformedMessage", "The JSON received is missing 'messages'.")
        returnMessages: list[dict] = []
        
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

            self.ongoingCommunication = True

            if msgType == "parameterCommand":
                raise NotImplementedError()
            
            elif msgType == "statusUpdate":
                if name in devices:
                    self.Update(ip, msg)

            elif msgType == "statusRequest":
                raise NotImplementedError()
            
            elif msgType == "channelLink":
                raise NotImplementedError()
            
            elif msgType == "channelUnlink":
                raise NotImplementedError()
            
            elif msgType == "infoRequest":
                raise NotImplementedError()
            
            elif msgType == "infoResponse":
                self.Update(ip, msg)

            elif msgType == "errorResponse":
                print(f"Error response received: {msg.get('error', {}).get('errorString', 'Unknown error')}")
            
            elif msgType == "subscribeMessage":
                raise NotImplementedError()
            
            elif msgType == "unsubscribeMessage":
                raise NotImplementedError()
            
            elif msgType == "endResponse":
                self.ongoingCommunication = False
                return None

            else:
                print(f"Unknown message type: {msgType}")
                returnMessages.append(CreateError("UnrecognizedCommand", f"Unknown message type: {msgType}"))
                continue
    
        if not returnMessages:
            self.ongoingCommunication = False
            return CreateEndResponse()
        return returnMessages

    def Run(self, deviceName: str, ip: str, queue: list[dict], selfInit: bool):
        self.conn = socket()
        self.deviceName = deviceName
        self.deviceIp = ip
        self.messageQueue = queue
        try:
            self.conn.connect((self.deviceIp, virgilPort))
            self.isVirgilDevice = True
        except Exception as e:
            self.isVirgilDevice = False
            return
        if selfInit and self.messageQueue:
            self.ongoingCommunication = True
            self.SendMessage(self.messageQueue.pop(0))
        while not self.disabled:
            try:
                if not self.ongoingCommunication and self.messageQueue:
                    self.ongoingCommunication = True
                    self.SendMessage(self.messageQueue.pop(0))
                data = self.conn.recv(4096)
                if not data:
                    print(f"Connection closed by remote device {self.deviceIp}")
                    self.End()
                response = self.ProcessMessage(data, self.deviceIp)
                if not response:
                    continue
                self.SendMessage(response)
            except Exception as e:
                if not self.disabled:
                    print(f"Error receiving data from {self.deviceIp}: {e}")
                continue

    def Update(self, infoResponse: dict):
        channelIndex = infoResponse["channelIndex"]
        if channelIndex == -1:
            self.deviceModel = infoResponse["deviceModel"]
            self.deviceType = infoResponse["deviceType"]
            self.virgilVersion = infoResponse["virgilVersion"]
            self.channelCounts = infoResponse["channelCounts"]
            return
        infoResponse.pop("channelIndex")
        infoResponse.pop("channelType")
        infoResponse.pop("messageType")
        if channelIndex not in self.channels:
            self.channels[channelIndex] = {}
        self.channels[channelIndex].update(infoResponse)

    def End(self):
        """
        End the ongoing communication and clean up resources.
        """
        if self.conn:
            self.conn.close()
            self.conn = None
        self.disabled = True
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
        "error": {
            "messageType" : "ErrorResponse",
            "errorValue" : errorValue,
            "errorString" : errorString
        }
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
    
def CreateEndResponse() -> dict:
    """
    Create an end response message.
    """
    return {
        "messageType": "endResponse",
    }



#If this channel setup makes no sense, it's because it's a spectera 2x2 setup, which is the most complicated setup I could think of.
txChannels : list[dict] = [
    {
        "channelIndex" : 0,
        "channelType" : "tx",
        "linkedChannels" : [],
        "gain" : {
            "unit" : "dB",
            "dataType" : "number",
            "minValue" : -10,
            "maxValue" : 50,
            "value" : 10,
            "precision" : 0.1,
            "readOnly" : False 
        },
        "rfEnable" : {
            "dataType" : "bool",
            "value" : True,
            "readOnly" : False
        },
        "squelch" : {
            "dataType" : "number",
            "unit" : "%",
            "minValue": 0,
            "maxValue": 100,
            "precision": 1,
            "value" : 100,
            "readOnly" : False
        },
        "deviceConnected" : {
            "dataType" : "bool",
            "value" : False,
            "readOnly" : True
        },
        "subDevice" : {
            "dataType" : "string",
            "value" : "disconnected",
            "readOnly" : True
        },
        "audioLevel" : {
            "dataType" : "number",
            "unit" : "dBFS",
            "value" : -256, # This is just a really low value to indicate no audio
            "readOnly" : True
        },
        "rfLevel" : {
            "dataType" : "number",
            "unit" : "%",
            "value" : 50,
            "readOnly" : True
        },
        "batteryLevel" : {
            "dataType" : "number",
            "unit" : "%",
            "value" : 100,
            "readOnly" : True
        }
    },
    {
        "channelIndex" : 1,
        "channelType" : "tx",
        "linkedChannels" : [],
        "gain" : {
            "unit" : "dB",
            "dataType" : "number",
            "minValue" : -10,
            "maxValue" : 50,
            "value" : 10,
            "precision" : 0.1,
            "readOnly" : False 
        },
        "rfEnable" : {
            "dataType" : "bool",
            "value" : True,
            "readOnly" : False
        },
        "squelch" : {
            "dataType" : "number",
            "unit" : "%",
            "minValue": 0,
            "maxValue": 100,
            "precision": 1,
            "value" : 100,
            "readOnly" : False
        },
        "deviceConnected" : {
            "dataType" : "bool",
            "value" : False,
            "readOnly" : True
        },
        "subDevice" : {
            "dataType" : "string",
            "value" : "disconnected",
            "readOnly" : True
        },
        "audioLevel" : {
            "dataType" : "number",
            "unit" : "dBFS",
            "value" : -256, # This is just a really low value to indicate no audio
            "readOnly" : True
        },
        "rfLevel" : {
            "dataType" : "number",
            "unit" : "%",
            "value" : 50,
            "readOnly" : True
        },
        "batteryLevel" : {
            "dataType" : "number",
            "unit" : "%",
            "value" : 100,
            "readOnly" : True
        }
    },
]
rxChannels : list[dict] = [
    {
        "channelIndex" : 0,
        "channelType" : "rx",
        "linkedChannels" : [],
        "rfEnable" : {
            "dataType" : "bool",
            "value" : True,
            "readOnly" : False
        },
        "deviceConnected" : {
            "dataType" : "bool",
            "value" : False,
            "readOnly" : True
        },
        "subDevice" : {
            "dataType" : "string",
            "value" : "disconnected",
            "readOnly" : True
        },
        "transmitPower" : {
            "dataType" : "enum",
            "enumValues" : ["low", "medium", "high"],
            "value" : "low",
            "readOnly" : False
        },
        "audioLevel" : {
            "dataType" : "number",
            "unit" : "dBFS",
            "value" : -256, # This is just a really low value to indicate no audio
            "readOnly" : True
        },
        "rfLevel" : {
            "dataType" : "number",
            "unit" : "%",
            "value" : 50,
            "readOnly" : True
        },
        "batteryLevel" : {
            "dataType" : "number",
            "unit" : "%",
            "value" : 100,
            "readOnly" : True
        }
    },
    {
        "channelIndex" : 0,
        "channelType" : "rx",
        "linkedChannels" : [],
        "rfEnable" : {
            "dataType" : "bool",
            "value" : True,
            "readOnly" : False
        },
        "deviceConnected" : {
            "dataType" : "bool",
            "value" : False,
            "readOnly" : True
        },
        "subDevice" : {
            "dataType" : "string",
            "value" : "disconnected",
            "readOnly" : True
        },
        "transmitPower" : {
            "dataType" : "enum",
            "enumValues" : ["low", "medium", "high"],
            "value" : "low",
            "readOnly" : False
        },
        "audioLevel" : {
            "dataType" : "number",
            "unit" : "dBFS",
            "value" : -256, # This is just a really low value to indicate no audio
            "readOnly" : True
        },
        "rfLevel" : {
            "dataType" : "number",
            "unit" : "%",
            "value" : 50,
            "readOnly" : True
        },
        "batteryLevel" : {
            "dataType" : "number",
            "unit" : "%",
            "value" : 100,
            "readOnly" : True
        }
    }
]
auxChannels : list[dict] = [
]

#This only has rx connections because that's all the dante device will know
#These IPs would be found using Dante discovery
connections : list[DeviceConnection]= [
    DeviceConnection("device1", "192.168.1.2", 0, "tx", 0, "rx"),
    DeviceConnection("device1", "192.168.1.2", 1, "tx", 1, "rx")
]

devices : dict[str, DeviceInfo] = {
}

txSubscribe : dict[int, list[str]] = {
    0: [],
    1: [],
}
rxSubscribe : dict[int, list[str]] = {
    0: [],
    1: [],
}
auxSubscribe : dict[int, list[str]] = {
}


virgilPort = 7889
selfName = "ExampleDevice"
selfModel = "ExampleModel"
selfType = "digitalStageBox"
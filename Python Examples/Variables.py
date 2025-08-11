from socket import socket
import traceback
import socket
import threading
import time
import json
import inspect
import struct

virgilPort = 7889

# Global variables for device identity
selfName = None
selfModel = None
selfType = None

gui = None

def SetGUIReference(reference):
    global gui
    gui = reference

def UpdateGUIDevices():
    """Thread-safe GUI update - can be called from any thread"""
    if gui:
        gui.deviceListUpdateSignal.emit()

def UpdateGUIValues():
    """Thread-safe GUI values update - can be called from any thread"""
    if gui:
        gui.valuesUpdateSignal.emit()

def PrintBlue(text: str):
    """
    Print text in blue color with line number.
    """
    import datetime
    frame = inspect.currentframe().f_back
    filename = frame.f_code.co_filename.split('\\')[-1]  # Just filename, not full path
    line_number = frame.f_lineno
    now = datetime.datetime.now().strftime('%I:%M:%S.%f')[:-3]  # 12Hr:min:sec.mil
    print(f"[{now} {filename}:{line_number}] \033[34m{text}\033[0m", flush=True)

def PrintGreen(text: str):
    """
    Print text in green color with line number.
    """
    import datetime
    frame = inspect.currentframe().f_back
    filename = frame.f_code.co_filename.split('\\')[-1]  # Just filename, not full path
    line_number = frame.f_lineno
    now = datetime.datetime.now().strftime('%I:%M:%S.%f')[:-3]  # 12Hr:min:sec.mil
    print(f"[{now} {filename}:{line_number}]\033[32m {text}\033[0m", flush=True)

def PrintRed(text: str):
    """
    Print text in red color with line number.
    """
    import datetime
    frame = inspect.currentframe().f_back
    filename = frame.f_code.co_filename.split('\\')[-1]  # Just filename, not full path
    line_number = frame.f_lineno
    now = datetime.datetime.now().strftime('%I:%M:%S.%f')[:-3]  # 12Hr:min:sec.mil
    print(f"[{now} {filename}:{line_number}] \033[31m{text}\033[0m", flush=True)

def PrintYellow(text: str):
    """
    Print text in yellow color with line number.
    """
    import datetime
    frame = inspect.currentframe().f_back
    filename = frame.f_code.co_filename.split('\\')[-1]  # Just filename, not full path
    line_number = frame.f_lineno
    now = datetime.datetime.now().strftime('%I:%M:%S.%f')[:-3]  # 12Hr:min:sec.mil
    print(f"[{now} {filename}:{line_number}] \033[33m{text}\033[0m", flush=True)

class DeviceConnection:
    def __init__(self, connectedDevice: str, selfIndex: int, selfType: str, channelIndex: int = None, channelType: str = None):
        self.connectedDevice = connectedDevice
        self.channelIndex = channelIndex
        self.channelType = channelType
        self.selfIndex = selfIndex
        self.selfType = selfType
        AddSubscription(connectedDevice, selfIndex, selfType)
        if not channelType:
            LinkInfo = {"deviceName" : self.connectedDevice}
        else:
            LinkInfo = {
                "deviceName" : self.connectedDevice,
                "channelIndex" : self.channelIndex,
                "channelType" : self.channelType
            }
        key = (self.selfIndex, self.selfType)
        if LinkInfo not in channels[key]["linkedChannels"]:
            channels[key]["linkedChannels"].append(LinkInfo)

        UpdateGUIDevices()

    def CheckForRemove(self, connectedDevice: str, channelIndex: int, channelType: str, selfIndex: int, selfType: str):
        #If we match the given info, delete ourselves
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
        if not self.channelIndex:
            LinkInfo = {"deviceName" : self.connectedDevice}
        else:
            LinkInfo = {
                "deviceName" : self.connectedDevice,
                "channelIndex" : self.channelIndex,
                "channelType" : self.channelType
            }
        key = (self.selfIndex, self.selfType)
        if LinkInfo in channels[key]["linkedChannels"]:
            channels[key]["linkedChannels"].remove(LinkInfo)
        connections.remove(self)
        UpdateGUIDevices()

        

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
    sock : socket.socket = None
    messageQueue : list[list[dict]] = []
    disabled = False
    # Internal buffer for TCP stream reassembly (length-prefixed framing)
    recv_buffer: bytearray = bytearray()


    def __init__(self, deviceName: str, ip: str, sock : socket.socket = None, startingMessage: bytes = None, queue: list[list[dict]] = []):
        #Just call Run in a separate Thread
        thread = threading.Thread(target=self.Run, args=(deviceName, ip, queue, sock, startingMessage), daemon=True)
        thread.start()

    def SendMessage(self, messages : dict):
        """
        Send a JSON object to the specified IP using TCP.
        """
        # Send via TCP with a 4-byte big-endian length prefix
        jsonInfo = json.dumps(CreateBase(messages))
        payload = jsonInfo.encode('utf-8')
        header = struct.pack('!I', len(payload))
        PrintBlue(f"Sending message to {self.deviceIp}: \n {jsonInfo}")
        self.sock.sendall(header + payload)


    def ProcessMessage(self, raw : bytes, ip : str):
        #Always trust the latest IP address because it might matter
        self.deviceIp = ip

        #Decode json safely
        try:
            packet = json.loads(raw.decode('utf-8'))
        except json.JSONDecodeError:
            PrintRed(f"Failed to decode JSON from {ip}: {raw}")
            return CreateError("MalformedMessage", "The JSON received is malformed.")
        
        PrintGreen(f"Received message from {ip}: \n {raw.decode('utf-8')}")

        #Check that all the required fields are present
        if "transmittingDevice" not in packet or not isinstance(packet["transmittingDevice"], str) or not packet["transmittingDevice"]:
            PrintRed("Message does not contain 'transmittingDevice'.")
            return CreateError("MalformedMessage", "The JSON received is missing 'transmittingDevice'.")
        if packet["transmittingDevice"] != self.deviceName:
            PrintRed(f"Message from {packet['transmittingDevice']} received packet not for it")
            return CreateError("InternalError", f"Device Name mismatch: {packet['transmittingDevice']} != {selfName}")
        name = packet.get("transmittingDevice", "")
        if "messages" not in packet or not isinstance(packet["messages"], list) or not packet["messages"]:
            PrintRed("Message does not contain 'messages'.")
            return CreateError("MalformedMessage", "The JSON received is missing 'messages'.")
        
        #Setup message returns
        returnMessages: list[dict] = []
        self.ongoingCommunication = True

        for msg in packet["messages"]:
            #Error Handling
            if not isinstance(msg, dict) or not msg:
                PrintRed("Invalid message format.")
                returnMessages.append(CreateError("MalformedMessage", "One of the messages is not a valid JSON object."))
                continue
            if "messageType" not in msg or not isinstance(msg["messageType"], str) or not msg["messageType"]:
                PrintRed("Message missing 'messageType'.")
                returnMessages.append(CreateError("MalformedMessage", "Message missing 'messageType'."))
                continue

            msgType = msg["messageType"]

            if msgType == "parameterCommand":
                #Error Handling
                if "channelIndex" not in msg or "channelType" not in msg:
                    PrintRed("Parameter command message missing 'channelIndex' or 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Parameter command message missing 'channelIndex' or 'channelType'."))
                    continue
                
                #Remove params that we don't need
                channelIndex = msg.pop("channelIndex")
                channelType = msg.pop("channelType")
                msg.pop("messageType")

                #Loop over every parameter, process them, and add errors if there are any
                for key,value in msg.items():
                    error = ProcessParamChange(channelIndex, channelType, key, value)
                    if error:
                        returnMessages.append(error)

                #Create a status update with changes params and send it to every device except the one we're communicating with
                response = SendStatusUpdate(channelIndex, channelType, name, list(msg.keys()))
                if response:
                    returnMessages.append(response)

            elif msgType == "statusUpdate":
                response = self.Update(ip, msg)
                #If there are errors/responses, add them
                if response:
                    returnMessages.extend(response)

            elif msgType == "statusRequest":
                #Error Handling
                if "channelIndex" not in msg or "channelType" not in msg:
                    PrintRed("Status request message missing 'channelIndex' or 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Status request message missing 'channelIndex' or 'channelType'."))
                    continue
                #Send a status update with every param
                returnMessages.append(CreateStatusUpdate(msg["channelIndex"], msg["channelType"]))
            
            elif msgType == "channelLink":
                #Error Handling
                if "channelIndex" not in msg or "channelType" not in msg:
                    PrintRed("Channel link message missing 'channelIndex' or 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Channel link message missing 'channelIndex' or 'channelType'."))
                    continue
                #Keep track of device and add subscription
                connections.append(DeviceConnection(
                    connectedDevice=name,
                    channelIndex=msg.get("sendingChannelIndex", None),
                    channelType=msg.get("sendingChannelType", None),
                    selfIndex=msg["channelIndex"],
                    selfType=msg["channelType"]
                ))

                if "sendingChannelType" in msg:
                    self.messageQueue.append(CreateInfoRequest(msg["sendingChannelIndex"], msg["sendingChannelType"]))

                response = SendStatusUpdate(msg["channelIndex"], msg["channelType"], name, ["linkedChannels"])
                if response:
                    returnMessages.append(response)


            elif msgType == "channelUnlink":
                #Error Handling
                if "channelIndex" not in msg or "channelType" not in msg:
                    PrintRed("Channel unlink message missing 'channelIndex' or 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Channel unlink message missing 'channelIndex' or 'channelType'."))
                    continue

                #Loop through and call a function that removes them if their params are matched
                for conn in connections:
                    conn.CheckForRemove(
                        connectedDevice=name,
                        channelIndex=msg.get("sendingChannelIndex", None),
                        channelType=msg.get("sendingChannelType", None),
                        selfIndex=msg["channelIndex"],
                        selfType=msg["channelType"],
                    )

                response = SendStatusUpdate(channelIndex, channelType, name, ["linkedChannels"])
                if response:
                    returnMessages.append(response)


            elif msgType == "infoRequest":
                #Error Handling
                if "channelIndex" not in msg:
                    PrintRed("Info request message missing 'channelIndex'.")
                    returnMessages.append(CreateError("MalformedMessage", "Info request message missing 'channelIndex'."))
                    continue
                # Device-level info requests (channelIndex = -1) don't require channelType
                if msg["channelIndex"] != -1 and "channelType" not in msg:
                    PrintRed("Info request message missing 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Info request message missing 'channelType'."))
                    continue

                channelType = msg.get("channelType", "")
                returnMessages.append(CreateInfoResponse(msg["channelIndex"], channelType))

            elif msgType == "infoResponse":
                #Same exact thing as statusUpdate
                response = self.Update(ip, msg)
                if response:
                    returnMessages.extend(response)

            elif msgType == "errorResponse":
                PrintRed(f"Error response received: {msg.get('errorString', 'Unknown error')}")
            
            elif msgType == "subscribeMessage":
                #Error Handling
                if "channelIndex" not in msg or "channelType" not in msg:
                    PrintRed("Subscribe message missing 'channelIndex' or 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Subscribe message missing 'channelIndex' or 'channelType'."))
                    continue
                    
                AddSubscription(name, msg["channelIndex"], msg["channelType"])
            
            elif msgType == "unsubscribeMessage":
                #Error Handling
                if "channelIndex" not in msg or "channelType" not in msg:
                    PrintRed("Unsubscribe message missing 'channelIndex' or 'channelType'.")
                    returnMessages.append(CreateError("MalformedMessage", "Unsubscribe message missing 'channelIndex' or 'channelType'."))
                    continue

                RemoveSubscription(name, msg["channelIndex"], msg["channelType"])

            elif msgType == "endResponse":
                #Immediately returns none and indicates end of communication
                self.ongoingCommunication = False
                return None

            else:
                PrintRed(f"Unknown message type: {msgType}")
                returnMessages.append(CreateError("UnrecognizedCommand", f"Unknown message type: {msgType}"))
                continue

        return returnMessages

    def Run(self, deviceName: str, ip: str, queue: list[list[dict]], sock: socket.socket = None, startingMessage: bytes = None):
        self.sock = sock
        self.deviceName = deviceName
        self.deviceIp = ip
        self.messageQueue = queue
    # Reset stream buffer for this connection
        self.recv_buffer = bytearray()
        PrintGreen(f"Device {self.deviceName} started with IP {self.deviceIp}.")
        PrintGreen(f"Device {self.deviceName} is {'connected' if self.sock else 'not connected'}.")
        try:
            #If we don't have a socket, create one
            #We would have a socket if this was created in the netListener
            if self.sock:
                #If we already have a socket, we are communicating
                self.ongoingCommunication = True
            else:
                self.ongoingCommunication = False
                self.sock = socket.socket()
                self.sock.connect((self.deviceIp, virgilPort))
            self.sock.settimeout(0.0)  # Non-blocking: returns immediately if no data
            #If there wasn't an exception by now, the device is a Virgil device and we're connected
            self.isVirgilDevice = True
            PrintGreen("Device Connected")
        except Exception as e:
            self.isVirgilDevice = False
            PrintRed(f"Failed to connect to device {self.deviceName} at {self.deviceIp}: {e}")
            return

        #If we have already received a message, process it
        #This would happen if it was created in the netListener
        if startingMessage:
            try:
                response = self.ProcessMessage(startingMessage, self.deviceIp)
                if not response:
                    #If we don't have a response, try to pull from the message queue
                    if self.messageQueue:
                        self.ongoingCommunication = True
                        response = self.messageQueue.pop(0)
                    #If the queue is empty and we are still communicating, indicate that we're done
                    elif self.ongoingCommunication:
                        self.ongoingCommunication = False
                        response = CreateEndResponse()
                    #Otherwise, just skip
                    else:
                        self.ongoingCommunication = False
                #Send the response if we have one
                if response:
                    self.SendMessage(response)
            except Exception as e:
                PrintRed(f"Error processing starting message: {e}")
                return
        #If we don't have a starting message, try to pull from the message queue
        elif self.messageQueue:
            self.ongoingCommunication = True
            self.SendMessage(self.messageQueue.pop(0))
        UpdateGUIDevices()
        #Main loop
        while not self.disabled:
            try:
                #Try to pull from the queue
                if self.messageQueue and not self.ongoingCommunication:
                    self.ongoingCommunication = True
                    self.SendMessage(self.messageQueue.pop(0))
                #Try to receive data from the socket
                try:
                    data = self.sock.recv(4096)
                except (BlockingIOError, TimeoutError):
                    continue
                except OSError as e:
                    # 10035: WSAEWOULDBLOCK (non-blocking no data), 10060: timeout
                    if getattr(e, 'errno', None) in (10035, 10060):
                        continue
                except Exception as e:
                    PrintGreen(f"Connection closed by remote device {self.deviceIp}")
                    self.End()
                    break
                #If we receive no data, the connection is closed
                if not data:
                    PrintGreen(f"Connection closed by remote device {self.deviceIp}")
                    self.End()
                    break
                # Append to buffer and process complete frames (4-byte big-endian length prefix)
                self.recv_buffer.extend(data)
                while True:
                    if len(self.recv_buffer) < 4:
                        # Not enough for header yet
                        break
                    msg_len = struct.unpack('!I', self.recv_buffer[:4])[0]
                    if len(self.recv_buffer) < 4 + msg_len:
                        # Wait for the full payload
                        break
                    # Extract one full message
                    start = 4
                    end = 4 + msg_len
                    message_bytes = bytes(self.recv_buffer[start:end])
                    del self.recv_buffer[:end]

                    # Process the complete message
                    response = self.ProcessMessage(message_bytes, self.deviceIp)
                    UpdateGUIValues()
                    if not response:
                        #If we don't have a response, try to pull from the message queue
                        if self.messageQueue:
                            self.ongoingCommunication = True
                            response = self.messageQueue.pop(0)
                        #If the queue is empty and we are still communicating, indicate that we're done
                        elif self.ongoingCommunication:
                            self.ongoingCommunication = False
                            response = CreateEndResponse()
                        #Otherwise, just skip
                        else:
                            continue
                    #Send the response
                    self.SendMessage(response)

            #Make sure it doesn't crash
            except Exception as e:
                if self.disabled:
                    break
                PrintRed(f"Error in device {self.deviceName} communication: {e} \n{traceback.format_exc()}")
                continue

    def Update(self, ip: str, infoResponse: dict) -> list[dict]:
        # Always trust the latest IP address
        self.deviceIp = ip
        errors : list[dict] = []

        #Error handling
        if "channelIndex" not in infoResponse:
            errors.append(CreateError("MalformedMessage", "Info response missing 'channelIndex'."))
            return errors
        channelIndex = infoResponse["channelIndex"]
        if "channelType" not in infoResponse and channelIndex != -1:
            errors.append(CreateError("MalformedMessage", "Info response missing 'channelType'."))
            return errors

        channelType = infoResponse.get("channelType")
        if channelIndex == -1:
            #Error handling for device level things
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
            #Update device information
            self.deviceModel = infoResponse["deviceModel"]
            self.deviceType = infoResponse["deviceType"]
            self.virgilVersion = infoResponse["virgilVersion"]
            self.channelCounts = infoResponse["channelCounts"]
            return
        
        #Remove for processing
        infoResponse.pop("channelIndex")
        infoResponse.pop("channelType")
        messageType = infoResponse.pop("messageType")

        #Create channel entry if it doesn't exist
        if (channelIndex,channelType) not in self.channels:
            self.channels[(channelIndex,channelType)] = {}

        #If the message is an infoResponse, it should overwrite the existing entry
        if messageType == "infoResponse":
            self.channels[(channelIndex,channelType)] = infoResponse
        else:
            # Merge infoResponse into the channel dict, preserving nested dicts (like 'gain')
            for key, value in infoResponse.items():
                if (
                    key in self.channels[(channelIndex, channelType)]
                    and isinstance(self.channels[(channelIndex, channelType)][key], dict)
                    and isinstance(value, dict)
                ):
                    # Update only the keys in the nested dict, e.g., update 'value' in 'gain'
                    self.channels[(channelIndex, channelType)][key].update(value)
                else:
                    # Overwrite or add new key
                    self.channels[(channelIndex, channelType)][key] = value
        #If we've reached here, we have no errors
        UpdateGUIValues()  # Update GUI when channel data changes
        return None

    def End(self):
        """
        End the ongoing communication and clean up resources.
        """
        #If socket exists, close it
        if self.sock:
            self.sock.close()
            self.sock = None
        #Stop main loop
        self.disabled = True
        #Unsubscribe from all connections
        for conn in connections:
            if conn.connectedDevice == self.deviceName and conn.channelType != "rx":
                conn.Remove()
        #Delete ourselves
        devices.pop(self.deviceName)
        UpdateGUIDevices()

def GetIp() -> str:
    """
    Creates a socket to get local IP address
    """
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
                response[key] = {"value": value.get("value", None)}
            else:
                #Exception for linkedChannels
                response[key] = value
    return response

def CreateEndResponse() -> dict:
    """
    Create an end response message.
    """
    return {
        "messageType": "endResponse",
    }

def CreateCommand(channelIndex: int, channelType: str, param: str, value: any) -> dict:
    """
    Create a parameterCommand.
    """
    return {
        "messageType": "parameterCommand",
        "channelIndex": channelIndex,
        "channelType": channelType,
        param: value
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
    UpdateGUIValues()  # Update GUI when parameter values change
    return None

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
                if device.isVirgilDevice and device.sock:
                    device.messageQueue.append(response)
            except Exception as e:
                PrintRed(f"Error sending message to device {name}: {e}")
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



channels : dict[tuple[int,str],dict] = {
}


connections : list[DeviceConnection]= [
]

devices : dict[str, DeviceInfo] = {
}

subscriptionList : dict[(int,str), list[str]] = {
}

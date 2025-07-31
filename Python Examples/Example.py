from Variables import *
from zeroconf import Zeroconf, ServiceInfo


# Look, I'm sorry about python. I don't like it, but I like c++ even less



selfIp = GetIp()

# Advertise the service using Zeroconf
info = ServiceInfo(
    type_="_virgil._udp.local.",
    name=f"{selfName}._virgil._udp.local.",
    port=virgilPort,
    properties={
        "model": selfModel,
        "deviceType": selfType
    },
    addresses=[socket.inet_aton(selfIp)]
)

zeroconf = Zeroconf()
zeroconf.register_service(info)

# Start NetListener in a separate thread
listener_thread = threading.Thread(target=NetListener, daemon=True)
listener_thread.start()


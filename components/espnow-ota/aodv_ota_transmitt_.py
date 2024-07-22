import logging
import time
from ctypes import *
from serial import Serial
from serial.tools.list_ports import comports
from progress.bar import IncrementalBar
from progress.spinner import Spinner
from enum import Enum
import sys

class OtaCommands(Enum):
    OTA_INIT = 101
    OTA_READY = 102
    OTA_DATA = 103
    OTA_RETRANS = 104
    OTA_DISCOVER = 105
    OTA_DISCOVER_RESPONSE = 106

global serial

logging.basicConfig(level="DEBUG")
logging.info("Starting Up")

if(len(sys.argv[1]) != 12):
    raise ValueError("MAC Adress needs to be 6 bytes in hex")

qos_sendheader = bytearray.fromhex("FF13AB0006")
sendheader = bytearray.fromhex("FF13AC0006")

preamble_len = len(sendheader)
payload_size = 232
sendheader.extend(bytearray.fromhex(sys.argv[1]))
qos_sendheader.extend(bytearray.fromhex(sys.argv[1]))

file_path = sys.argv[2]

file_handle = open(file_path, 'rb')

ota_data = bytearray(file_handle.read())
ota_data_len = len(ota_data)
packet_count = ota_data_len // payload_size
if (ota_data_len % payload_size) != 0: packet_count += 1
logging.info("Read %i bytes from binery --> %i Packets", len(ota_data), packet_count)

def read_msg():
    counter = 0
    while counter < 4:
        readback = serial.read(1)

        if(len(readback) == 0):
            raise TimeoutError("Partner Timeout")
        if(readback == qos_sendheader[counter:counter+1]):
            counter += 1
        else:
            counter = 0

    readlen = int.from_bytes(serial.read(1), "little")
    if(readlen == 0):
        raise TimeoutError("Partner Timeout")
    data = serial.read(readlen)
    if(len(data) != readlen):
        raise FormatError("Msg encoding Error!")
    #logging.debug("Read: %s", data.hex())
    return data

def send_payload_packet(num):
    send = bytearray(len(sendheader))
    send[:] = sendheader #deepcopy
    send.append(OtaCommands.OTA_DATA.value)
    send.extend(bytearray(c_uint32(num)))

    if(num == (ota_data_len // payload_size)):
        send.extend(ota_data[num*payload_size:])
    else:
        send.extend(ota_data[num*payload_size:(num + 1)*payload_size])

    send[preamble_len - 1] = len(send)-preamble_len
    #logging.debug("Len in header: %i real len %i", send[preamble_len - 1], len(send))
    serial.write(send)
    time.sleep(0.015)

ports = comports()
for port, _, hwid in ports:
    if '303A:4001' in hwid:
        serial = Serial(port, timeout=5)
        break

request_msg = bytearray(preamble_len)
request_msg[:] = qos_sendheader
request_msg.append(OtaCommands.OTA_INIT.value)
request_msg.extend(bytearray(c_uint32(len(ota_data))))

request_msg[preamble_len - 1] = len(request_msg)-preamble_len
serial.write(request_msg)
serial.flushInput()
#logging.debug("len of %i", len(request_msg))
#logging.debug("Sendheader: %s", request_msg.hex())

response_msg = read_msg()
if(response_msg[6] != OtaCommands.OTA_READY.value):
    raise NameError("Wrong Response after init")

logging.info("Got Init")
bar = IncrementalBar('Transmitting', max=(ota_data_len // payload_size) + 1)
for i in range(0, (ota_data_len // payload_size) + 1):
    send_payload_packet(i)
    bar.next()
bar.finish()
serial.timeout = 1

while True:
    time.sleep(2) # wait one esp timeout period to get packets to retransmitt to wait for esp 
    packets_to_send = []
    spinner = Spinner('Getting Retransmissions')
    while True:
        try:
            packets_to_send.append(int.from_bytes(read_msg()[7:10], "little"))
            spinner.next()
        except TimeoutError:
            #logging.info("Readtimeout --> no more retransmits sending received ones")
            break
    spinner.finish()
    if len(packets_to_send) == 0: break
    bar = IncrementalBar('Retransmitting', max=len(packets_to_send))
    for nums in packets_to_send:
        send_payload_packet(nums)
        bar.next()
    bar.finish()
logging.info("Done!")
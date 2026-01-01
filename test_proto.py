import serial
import time
import sys

port = '/dev/cu.usbmodem1101'
try:
    ser = serial.Serial(port, 115200, timeout=1)
    print(f"Opened {port}")
except Exception as e:
    print(f"Failed to open {port}: {e}")
    sys.exit(1)

# CMD defs
CMD_NOP = b'\x00'
CMD_CFG_SPI = b'\x01'
CMD_CS_CTRL = b'\x02'
CMD_SPI_WRITE = b'\x03'
CMD_SPI_READ = b'\x04'
STATUS_OK = 0

def flush_input():
    n = ser.in_waiting
    if n > 0:
        d = ser.read(n)
        print(f"Flushed {n} bytes: {d.hex()}")

def send_cmd(cmd, data=b'', wait_resp=True):
    # flush_input() 
    length = len(data)
    req = cmd + length.to_bytes(2, byteorder='little') + data
    print(f"TX: {req.hex()}")
    ser.write(req)
    ser.flush()
    
    if not wait_resp: return None, None
    
    # Read header: CMD STATUS LEN_L LEN_H
    # Try reading 1 byte at a time to see what we get
    head = b''
    for i in range(4):
        b = ser.read(1)
        if len(b) == 0:
            print("Timeout reading byte")
            break
        head += b
        
    if len(head) < 4:
        print(f"Timeout reading header, got: {head.hex()}")
        return None, None
    
    # print(f"RX Header: {head.hex()}")
    if head[0] != cmd[0]:
        print(f"Invalid CMD response: {head[0]:02x} (Expected {cmd[0]:02x})")
        # Don't return yet, read payload anyway if length looks sane?
        # return None, None
    if head[1] != STATUS_OK:
        print(f"Error status: {head[1]:02x}")
        # return None, None
        
    rlen = int.from_bytes(head[2:4], "little")
    payload = b''
    if rlen > 0:
        payload = ser.read(rlen)
        print(f"RX Payload: {payload.hex()}")
        
    return head, payload

print("Cleaning buffer...")
flush_input()

print("\n--- Ping (NOP) ---")
h, p = send_cmd(CMD_NOP) 
if p: print(f"Protocol Ver: {p[0]}")
time.sleep(0.5)

print("\n--- Config SPI 9MHz ---")
send_cmd(CMD_CFG_SPI, b'\x02') 
time.sleep(0.2)

print("\n--- Reset (W25Q Reset Sequence 66h/99h) ---")
# Although not strictly necessary if power cycle, good practice.
# But let's stick to simple ID read first to avoid complications.
time.sleep(0.1)

print("\n--- Reading ID (W25Q128FV) ---")
# CS Low
print("CS Low")
send_cmd(CMD_CS_CTRL, b'\x00')
time.sleep(0.1)

# Write 0x9F (Read JEDEC ID)
print("Write 0x9F")
send_cmd(CMD_SPI_WRITE, b'\x9f')
time.sleep(0.1)

# Read 3 bytes (MFR, Type, Cap) -> EF 40 18
print("Read 3 Bytes")
req = CMD_SPI_READ + (3).to_bytes(2, "little")
print(f"TX: {req.hex()}")
ser.write(req)
ser.flush()

# Expect Response
resp_head = ser.read(4)
if len(resp_head) < 4:
    print("Timeout RX Header")
else:
    print(f"RX Header: {resp_head.hex()}")
    rlen = int.from_bytes(resp_head[2:4], "little")
    if rlen > 0:
        id_bytes = ser.read(rlen)
        print(f"RX ID Data: {id_bytes.hex()}")
        if id_bytes.hex().lower() == "ef4018":
             print("SUCCESS: Detected W25Q128FV")
        else:
             print("FAILED: Incorrect ID (Check wiring)")

# CS High
print("CS High")
send_cmd(CMD_CS_CTRL, b'\x01')

ser.close()

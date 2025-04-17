import socket
from jump_table import jump_table

class io_samurai:
    def __init__(self, pico_ip="192.168.0.177", port=8888, timeout=1.0):
        """UDP kommunikáció inicializálása."""
        self.pico_ip = pico_ip
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("", port))
        self.sock.settimeout(timeout)
        self.jump_index_out = 1
        self.jump_index_inp = 1
        self.outputs = 0x00  # Kezdeti kimenetek (output-00 - output-07)
        self.inputs = 0x00   # Kezdeti bemenetek (input-00 - input-07)

    def jump_in_table_out(self, input1, input2):
        """
        Ugrás a táblában két bemenet bináris összege + 1 alapján (8 bites túlcsordulással).
        input1, input2: 0-255 közötti egész számok (bájtok)
        Visszatér: A tábla ((input1 + input2) % 256 + 1) % 256 indexén található érték
        """
        # Ellenőrzés, hogy a bemenetek érvényesek-e
        if not (0 <= input1 <= 255 and 0 <= input2 <= 255):
            raise ValueError("A bemeneteknek 0 és 255 között kell lenniük!")
        
        # 8 bites bináris összeadás túlcsordulással: (input1 + input2) % 256
        index = ((input1 + input2) + 1) % 256
        # +1 hozzáadása az indexhez, szintén 8 bites határon belül
        self.jump_index_out += (index) % 256
        self.jump_index_out %= 256  # Biztosítjuk, hogy az index 0-255 között maradjon
        
        # Táblában való ugrás
        result = jump_table[self.jump_index_out]
        return result

    def jump_in_table_inp(self, input1, input2, input3, input4):
        """
        Ugrás a táblában két bemenet bináris összege + 1 alapján (8 bites túlcsordulással).
        input1, input2: 0-255 közötti egész számok (bájtok)
        Visszatér: A tábla ((input1 + input2) % 256 + 1) % 256 indexén található érték
        """
       
        # 8 bites bináris összeadás túlcsordulással: (input1 + input2) % 256
        index = ((input1 + input2 + input3 + input4) + 1) % 256
        # +1 hozzáadása az indexhez, szintén 8 bites határon belül
        self.jump_index_inp += (index) % 256
        self.jump_index_inp %= 256  # Biztosítjuk, hogy az index 0-255 között maradjon
        
        # Táblában való ugrás
        result = jump_table[self.jump_index_inp]
        return result

    def send_request(self):
        """Kimenetek elküldése a Pico-nak."""
        outputs = self.outputs
        tx_buffer = bytearray(3)
        tx_buffer[0] = outputs & 0xFF         # output-08 - output-15 (LSB)
        tx_buffer[1] = (outputs >> 8) & 0xFF  # output-00 - output-07 (MSB)
        tx_buffer[2] = self.jump_in_table_out(tx_buffer[0], tx_buffer[1])
        # print(f" {tx_buffer[0]:02x} {tx_buffer[1]:02x} {tx_buffer[2]:02x}")
        self.sock.sendto(tx_buffer, (self.pico_ip, self.port))
        return f"{tx_buffer[1]:02X} {tx_buffer[0]:02X} (Checksum: {tx_buffer[2]:02X})"

    def receive_inputs(self):
        """Bemenetek fogadása a Pico-tól."""
        try:
            data, addr = self.sock.recvfrom(5)
            if len(data) == 5:
                calc_checksum = self.jump_in_table_inp(data[3], data[2], data[1], data[0])
                if calc_checksum == data[4]:
                    inputs = (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0]
                    self.inputs = inputs
                    return inputs, addr[0]
                else:
                    print(f"Buffer: {data[0]:02X} {data[1]:02X} {data[2]:02X} {data[3]:02X} {data[4]:02X}")
                    print(f"Checksum error! Expected {calc_checksum:02X}, got {data[4]:02X}")
                    exit()
                    return None, f"Checksum error! Expected {calc_checksum:02X}, got {data[4]:02X}"
            else:
                return None, f"Invalid packet size: {len(data)} bytes"
        except socket.timeout:
            return None, "No response from Pico"
        except OSError as e:
            return None, f"Socket error: {e}"

    # egyedik kimenet beállítása
    def set_ouput(self, output, state):
        """Kimenet beállítása."""
        if state:
            self.outputs |= (1 << output)
        else:
            self.outputs &= ~(1 << output)

    def get_input(self, input):
        """Bemenet lekérdezése."""
        if self.inputs & (1 << input):
            return True
        else:
            return False

    def update(self):
        self.send_request()
        self.receive_inputs()

    def close(self):
        """Socket lezárása."""
        self.sock.close()

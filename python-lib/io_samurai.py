# -- coding: utf-8 --

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
        self.outputs = 0  # Kezdeti kimenetek (output-00 - output-07)
        self.inputs = 0   # Kezdeti bemenetek (input-00 - input-07)
        self.analog_scale = 4095
        self.state = {
        'sent_count': 0,
        'last_inputs': 0,
        'last_addr': "N/A",
        'last_error': ""
        }

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
        self.jump_index_out += index
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
        self.jump_index_inp += index
        self.jump_index_inp %= 256  # Biztosítjuk, hogy az index 0-255 között maradjon

        # Táblában való ugrás
        result = jump_table[self.jump_index_inp]
        return result

    def send_request(self):
        """Kimenetek elküldése a Pico-nak."""
        tx_buffer = bytearray(3)
        tx_buffer[0] = self.outputs & 0xFF         # output-08 - output-15 (LSB)
        tx_buffer[1] = (self.outputs >> 8) & 0xFF  # output-00 - output-07 (MSB)
        tx_buffer[2] = self.jump_in_table_out(tx_buffer[0], tx_buffer[1])
        self.sock.sendto(tx_buffer, (self.pico_ip, self.port))
        return f"{tx_buffer[1]:02X} {tx_buffer[0]:02X} (Checksum: {tx_buffer[2]:02X})"

    def receive_inputs(self):
        """Bemenetek fogadása a Pico-tól."""
        try:
            data, addr = self.sock.recvfrom(5)
            if len(data) == 5:
                calc_checksum = self.jump_in_table_inp(data[3], data[2], data[1], data[0])
                if calc_checksum == data[4]:
                    self.inputs = (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0]
                    return self.inputs, addr[0]
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

    # Set Output state
    # 0-7: output-00 - output-07
    # 8: output-08 (low pass filter) enable/disable
    def set_ouput(self, out, state):
        """Kimenet beállítása."""
        if state:
            self.outputs |= (1 << out)
        else:
            self.outputs &= ~(1 << out)

    def get_output(self, output):
        """Kimenet lekérdezése."""
        if self.outputs & (1 << output):
            return True
        else:
            return False

    def get_input(self, input):
        """Bemenet lekérdezése."""
        if self.inputs & (1 << input):
            return True
        else:
            return False

    def get_analog_input(self):
        lower_byte = self.inputs>> 16 & 0xFF
        upper_byte = (self.inputs >> 24) & 0xFF
        # Az alsó és felső bájtok összevonása
        adc_value = (upper_byte << 8) | lower_byte
        # Az ADC érték skálázása a self.analog_scale alapján
        scaled_value = adc_value * (self.analog_scale / 4095)
        return scaled_value

    def enable_low_pass_filter(self):
        self.set_ouput(8, True)  # Enable low pass filter
    
    def disable_low_pass_filter(self):
        self.set_ouput(8, False)

    def update(self):
         # Network communication
        try:
            sent_msg = self.send_request()
            self.state['sent_count'] += 1
        except Exception as e:
            self.state['last_error'] = f"Send error: {str(e)}"
            print(f"Send error: {self.state['last_error']}")

        try:
            inputs, addr = self.receive_inputs()
            if inputs is not None:
                self.state['last_inputs'] = inputs
                self.state['last_addr'] = addr
                self.state['last_error'] = ""
        except Exception as e:
            self.state['last_error'] = str(e)
            print(f"Receive error: {self.state['last_error']}")

    def close(self):
        """Socket lezárása."""
        self.sock.close()

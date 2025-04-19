from io_samurai import io_samurai

bit = 0
state = 0


# connect to the io-samurai
samurai0 = io_samurai(pico_ip="192.168.0.177", port=8888, timeout=1.0)
#samurai1 = io_samurai(pico_ip="192.168.0.178", port=8888, timeout=1.0)
#samurai2 = io_samurai(pico_ip="192.168.0.179", port=8888, timeout=1.0)
#samurai3 = io_samurai(pico_ip="192.168.0.180", port=8888, timeout=1.0)

# set analog maximum for get_analog_input()
samurai0.analog_scale = 100
# enable low pass filter (pico side)
samurai0.enable_low_pass_filter()

while True:
    # send and receive data from the io-samurai
    samurai0.update()

    # read the state of the bit
    state = samurai0.get_input(bit)
    print(f"Bit {bit} state: {state}")
    # toggle the bit
    samurai0.set_ouput(bit, state)

    # read the analog value
    analog = samurai0.get_analog_input()
    print(f"Analog input: {analog:.2f}")

    # try to exit programmed to prevent checksum errors (pico side, only reset clear the checksum error)
    # do not wait because triggering the pico timeout error (100mS)

samurai.close()
from io_samurai import io_samurai

samurai = io_samurai(pico_ip="192.168.0.177", port=8888, timeout=1.0)

bit = 0
state = 0

while True:
    samurai.set_ouput(bit, not samurai.get_output(bit))
    bit = (bit + 1) % 8
    samurai.update()
    # do not wait because triggering the pico timeout error (100mS)

samurai.close()
import pygame
from io_samurai import io_samurai
import time
import os

os.environ['SDL_VIDEO_VSYNC'] = '1'  # V-Sync bekapcsolása
period = 0.002
# Constants
WIDTH, HEIGHT = 600, 400
FPS = 60  # 2 FPS (500 ms cycle time)

# Colors
WHITE = (0, 0, 0)
GRAY = (64,64,64)
BLACK = (255, 255, 255)
GREEN = (0, 128, 0)
RED = (128, 0, 0)

# UI Layout
OUTPUT_X = 100
OUTPUT_Y_ROWS = [150, 180]  # Y positions for two output rows
BIT_SIZE = 18
BIT_SPACING = 20
INPUT_X = 110
INPUT_Y_ROWS = [260, 290, 320, 350]

# Initial state
INITIAL_OUTPUTS = 0x0000
start_time = 0
counter = 0
benchmark = False

# Initialize Pygame
pygame.init()

screen = pygame.display.set_mode((WIDTH, HEIGHT),vsync=0)
pygame.display.set_caption("io-samurai UDP I/O Test")

font = pygame.font.SysFont("monospace", 16)

def format_bits(value, num_bits):
    """Format bits as list from MSB to LSB."""
    return [(value >> i) & 1 for i in range(num_bits-1, -1, -1)]

class UIData:
    """Container for UI-related data and pre-rendered elements"""
    def __init__(self):
        # Pre-render static text elements
        self.static_labels = [
            self.create_label("Test Program for I/O", 10, 10),
            self.create_label("Sent Outputs :", 10, 40),
            self.create_label("Sent Count:", 10, 60),
            self.create_label("Outputs (click to toggle):", 10, 100),
            *[self.create_label(text, 10, y) for text, y in [
                ("o15-o08:", 150),
                ("o07-o00:", 180),
                ("Received Inputs:", 230),
                ("I31-I24:", 260),
                ("I23-I16:", 290),
                ("I15-I08:", 320),
                ("I07-I00:", 350)
            ]],
            *[self.create_label(text, 300, y) for text, y in [
                ("From IP:", 260),
                ("Last Error:", 290),
                ("Press 'q' or close window to quit", 320)
            ]]
        ]
        
        # Create output bit rectangles for click detection
        self.output_rects = []
        for row_idx, y in enumerate(OUTPUT_Y_ROWS):
            for bit_idx in range(8):
                x = OUTPUT_X + bit_idx * BIT_SPACING
                bit_number = 15 - (row_idx * 8 + bit_idx)
                self.output_rects.append((
                    pygame.Rect(x, y, BIT_SIZE, BIT_SIZE),
                    bit_number
                ))

    def create_label(self, text, x, y):
        """Helper to create pre-rendered text surfaces"""
        return (font.render(text, True, BLACK), (x, y))

def draw_bits(surface, bits, x_start, y_start, color_fn=lambda b: GREEN if b else RED):
    """Generic function to draw a row of bits"""
    for i, bit in enumerate(bits):
        x = x_start + i * BIT_SPACING
        color = color_fn(bit)
        pygame.draw.rect(surface, color, (x, y_start, BIT_SIZE, BIT_SIZE))
        text = font.render(str(bit), True, WHITE)
        surface.blit(text, (x + 6, y_start + 2))

def start_timer():
    global start_time
    start_time = time.time()

def stop_timer():
    global start_time
    elapsed_time = time.time() - start_time
    return elapsed_time

def main():
    global counter, start_time
    try:
        comm = io_samurai()
    except Exception as e:
        print(f"Failed to initialize UDP: {e}")
        return

    ui = UIData()
    state = {
        'outputs': INITIAL_OUTPUTS,
        'sent_count': 0,
        'last_inputs': 0,
        'last_addr': "N/A",
        'last_error': ""
    }

    clock = pygame.time.Clock()
    running = True

    start_timer()
    
    while running:

        screen.fill(WHITE)

        # Event handling
        for event in pygame.event.get():
            if event.type == pygame.QUIT or \
            (event.type == pygame.KEYDOWN and event.key == pygame.K_q):
                running = False
            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                if not benchmark:
                    pos = pygame.mouse.get_pos()
                    for rect, bit in ui.output_rects:
                        if rect.collidepoint(pos):
                            state['outputs'] ^= (1 << bit)
                            break

        # Network communication
        try:
            comm.outputs = state['outputs']
            sent_msg = comm.send_request()
            state['sent_count'] += 1
        except Exception as e:
            state['last_error'] = f"Send error: {str(e)}"
            sent_msg = "Failed"
            print(f"Send error: {state['last_error']}")

        try:
            inputs, addr = comm.receive_inputs()
            if inputs is not None:
                state['last_inputs'] = inputs
                state['last_addr'] = addr
                state['last_error'] = ""
        except Exception as e:
            state['last_error'] = str(e)
            print(f"Receive error: {state['last_error']}")

        if not benchmark:
            # Blit static elements
            for surface, pos in ui.static_labels:
                screen.blit(surface, pos)

            # Draw dynamic text
            screen.blit(font.render(sent_msg, True, BLACK), (150, 40))
            screen.blit(font.render(str(state['sent_count']), True, BLACK), (150, 60))
            screen.blit(font.render(state['last_addr'], True, BLACK), (380, 260))
            screen.blit(font.render(state['last_error'], True, RED), (400, 290))

            # Draw output bits
            output_bits = format_bits(state['outputs'], 16)
            draw_bits(screen, output_bits[:8], OUTPUT_X, OUTPUT_Y_ROWS[0])
            draw_bits(screen, output_bits[8:], OUTPUT_X, OUTPUT_Y_ROWS[1])

            # Draw input bits
            input_bits = format_bits(state['last_inputs'], 32)
            for row, y in enumerate(INPUT_Y_ROWS):
                start = row * 8
                draw_bits(screen, input_bits[start:start+8], INPUT_X, y, color_fn=lambda b: GREEN if b else GRAY)
            pygame.display.flip()
            clock.tick(FPS)

    comm.close()
    pygame.quit()

if __name__ == "__main__":
    main()

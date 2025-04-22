# A simple speedometer using Pygame and io_samurai
# This code creates a speedometer that displays the speed in km/h
# and updates the needle position based on the analog input.

import pygame
import math
from io_samurai import io_samurai

samurai0 = io_samurai(pico_ip="192.168.0.176", port=8889, timeout=1.0)
samurai1 = io_samurai(pico_ip="192.168.0.177", port=8888, timeout=1.0)

# Initialize Pygame
pygame.init()

# Set up the display
WIDTH, HEIGHT = 800, 400
screen = pygame.display.set_mode((WIDTH, HEIGHT))
pygame.display.set_caption("Analog Speedometer - io_samurai")

# Colors
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
RED = (255, 0, 0)
GRAY = (100, 100, 100)

# Speedometer parameters
CENTER = (WIDTH // 2, HEIGHT // 2)
RADIUS = 150
MAX_SPEED = 240  # Max speed in km/h
MAX_ANGLE = 300  # Max angle for the speedometer
NEEDLE_LENGTH = 120

# Font for numbers
font = pygame.font.SysFont("arial", 20)
start_angle = 180

def draw_speedometer(speed, center=(200, 200), radius=150, needle_length=120):

    # Draw the outer circle
    pygame.draw.circle(screen, GRAY, center, radius, 5)
    pygame.draw.circle(screen, GRAY, center, radius - 10, 3)

    # Draw tick marks and numbers
    for i in range(0, MAX_SPEED + 1, 10):
        angle = math.radians(start_angle - (i / MAX_SPEED) * MAX_ANGLE)  # Map 0-240 to 135° to -135°
        # Major ticks (every 20 km/h)
        if i % 20 == 0:
            start_pos = (
                center[0] + (radius - 10) * math.cos(angle),
                center[1] - (radius - 10) * math.sin(angle)
            )
            end_pos = (
                center[0] + (radius - 30) * math.cos(angle),
                center[1] - (radius - 30) * math.sin(angle)
            )
            pygame.draw.line(screen, WHITE, start_pos, end_pos, 3)
            # Draw numbers
            text = font.render(str(i), True, WHITE)
            text_pos = (
                center[0] + (radius - 50) * math.cos(angle) - text.get_width() // 2,
                center[1] - (radius - 50) * math.sin(angle) - text.get_height() // 2
            )
            screen.blit(text, text_pos)
        # Minor ticks (every 10 km/h)
        else:
            start_pos = (
                center[0] + (radius - 10) * math.cos(angle),
                center[1] - (radius - 10) * math.sin(angle)
            )
            end_pos = (
                center[0] + (radius - 20) * math.cos(angle),
                center[1] - (radius - 20) * math.sin(angle)
            )
            pygame.draw.line(screen, WHITE, start_pos, end_pos, 1)

    # Draw the needle
    angle = math.radians(start_angle - (speed / MAX_SPEED) * MAX_ANGLE)  # Map speed to angle
    needle_end = (
        center[0] + needle_length * math.cos(angle),
        center[1] - needle_length * math.sin(angle)
    )

    # Draw speed text
    speed_text = font.render(f"{int(speed)} km/h", True, WHITE)
    screen.blit(speed_text, (center[0] - speed_text.get_width() // 2, center[1] + 30))

    pygame.draw.line(screen, RED, center, needle_end, 5)
    pygame.draw.circle(screen, RED, center, 10)  # Center hub


def main():
    clock = pygame.time.Clock()
    speed = 0
    increasing = True
    running = True

    # enable low pass filter on the io_samurai
    samurai0.analog_scale = MAX_SPEED
    samurai1.analog_scale = MAX_SPEED
    samurai0.enable_low_pass_filter()
    samurai1.enable_low_pass_filter()

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False

        samurai0.update()
        samurai1.update()
        speed = int(samurai0.get_analog_input())
        speed1 = int(samurai1.get_analog_input())
        # Draw the speedometer

            # Clear the screen
        screen.fill(BLACK)

        draw_speedometer(speed,center=(200, 200), radius=RADIUS)
        # Draw the speedometer for the second samurai
        draw_speedometer(speed1, center=(600, 200), radius=RADIUS)

        # Update display
        pygame.display.flip()
        clock.tick(60)  # 60 FPS

    pygame.quit()

if __name__ == "__main__":
    main()
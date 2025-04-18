import pygame
import math
import time
from io_samurai import io_samurai

# Initialize Pygame
pygame.init()

# Set up the display
WIDTH, HEIGHT = 400, 400
screen = pygame.display.set_mode((WIDTH, HEIGHT))
pygame.display.set_caption("Speedometer")

# Colors
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
RED = (255, 0, 0)
GRAY = (100, 100, 100)

# Speedometer parameters
CENTER = (WIDTH // 2, HEIGHT // 2)
RADIUS = 150
MAX_SPEED = 240  # Max speed in km/h
NEEDLE_LENGTH = 120

# Font for numbers
font = pygame.font.SysFont("arial", 20)
samurai = io_samurai()
start_angle = 180

def draw_speedometer(speed):
    # Clear the screen
    screen.fill(BLACK)

    # Draw the outer circle
    pygame.draw.circle(screen, WHITE, CENTER, RADIUS, 5)
    pygame.draw.circle(screen, GRAY, CENTER, RADIUS - 10, 3)

    # Draw tick marks and numbers
    for i in range(0, MAX_SPEED + 1, 10):
        angle = math.radians(start_angle - (i / MAX_SPEED) * 270)  # Map 0-240 to 135° to -135°
        # Major ticks (every 20 km/h)
        if i % 20 == 0:
            start_pos = (
                CENTER[0] + (RADIUS - 10) * math.cos(angle),
                CENTER[1] - (RADIUS - 10) * math.sin(angle)
            )
            end_pos = (
                CENTER[0] + (RADIUS - 30) * math.cos(angle),
                CENTER[1] - (RADIUS - 30) * math.sin(angle)
            )
            pygame.draw.line(screen, WHITE, start_pos, end_pos, 3)
            # Draw numbers
            text = font.render(str(i), True, WHITE)
            text_pos = (
                CENTER[0] + (RADIUS - 50) * math.cos(angle) - text.get_width() // 2,
                CENTER[1] - (RADIUS - 50) * math.sin(angle) - text.get_height() // 2
            )
            screen.blit(text, text_pos)
        # Minor ticks (every 10 km/h)
        else:
            start_pos = (
                CENTER[0] + (RADIUS - 10) * math.cos(angle),
                CENTER[1] - (RADIUS - 10) * math.sin(angle)
            )
            end_pos = (
                CENTER[0] + (RADIUS - 20) * math.cos(angle),
                CENTER[1] - (RADIUS - 20) * math.sin(angle)
            )
            pygame.draw.line(screen, WHITE, start_pos, end_pos, 1)

    # Draw the needle
    angle = math.radians(start_angle - (speed / MAX_SPEED) * 270)  # Map speed to angle
    needle_end = (
        CENTER[0] + NEEDLE_LENGTH * math.cos(angle),
        CENTER[1] - NEEDLE_LENGTH * math.sin(angle)
    )
    pygame.draw.line(screen, RED, CENTER, needle_end, 5)
    pygame.draw.circle(screen, RED, CENTER, 10)  # Center hub

    # Draw speed text
    speed_text = font.render(f"{int(speed)} km/h", True, WHITE)
    screen.blit(speed_text, (CENTER[0] - speed_text.get_width() // 2, CENTER[1] + RADIUS - 100))

def main():
    clock = pygame.time.Clock()
    speed = 0
    increasing = True
    running = True
    samurai.analog_scale = MAX_SPEED
    # enable low pass filter on the io_samurai
    samurai.enable_low_pass_filter()

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False

        samurai.update()
        speed = samurai.get_analog_input()
        # Draw the speedometer
        draw_speedometer(speed)

        # Update display
        pygame.display.flip()
        clock.tick(60)  # 60 FPS

    pygame.quit()

if __name__ == "__main__":
    main()
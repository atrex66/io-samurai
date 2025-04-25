def scale_value(x):
    x_min, x_max = 14, 4089
    y_min, y_max = 0, 4095
    return ((x - x_min) / (x_max - x_min)) * (y_max - y_min) + y_min

# Példák
test_values = [14, 4089, 2051.5]  # teszt értékek
for val in test_values:
    result = scale_value(val)
    print(f"Érték {val} a második skálán -> {result:.2f} az első skálán")
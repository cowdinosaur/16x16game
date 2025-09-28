# LED Matrix Interactive Gaming System

An interactive multi-game system for 16x16 WS2812 LED matrix controlled by ESP32-C3 with ToF sensor input, designed for exhibitions and public demonstrations.

## Hardware Requirements

- **ESP32-C3** Development Board
- **16x16 WS2812B** LED Matrix (256 LEDs)
- **VL53L0X** Time-of-Flight Distance Sensor
- **5V Power Supply** (minimum 15A for full brightness)

## Pin Configuration

| Component | Pin | GPIO |
|-----------|-----|------|
| WS2812 Data | D2 | GPIO 2 |
| ToF SDA | D8 | GPIO 8 |
| ToF SCL | D9 | GPIO 9 |

## Features

- 4 interactive games with gesture control
- Menu system with visual game selection
- Real-time hand tracking via ToF sensor (50-400mm range)
- 20 FPS gameplay with optimized rendering
- Automatic return to menu after game over

## Games

### 1. Pong
Classic paddle game against AI opponent
- **Objective**: First to 5 points wins
- **Control**: Move hand up/down to control blue paddle
- **AI**: Red paddle with adaptive difficulty

### 2. Flappy Bird
Navigate through scrolling pipes
- **Objective**: Avoid hitting pipes
- **Control**: Move hand up quickly to make bird jump
- **Difficulty**: Gravity increases over time

### 3. Catch
Catch falling items with basket
- **Objective**: Catch green items, avoid red ones
- **Control**: Move hand left/right to position basket
- **Lives**: 3 lives, lose one for missing good items or catching bad ones

### 4. Space Invaders
Defend against alien invasion
- **Objective**: Destroy all invaders
- **Control**: Move hand to position ship, quick movements to shoot
- **Challenge**: Invaders move faster as you progress

## Color Palette

### Menu Colors
| Element | Color | RGB Values |
|---------|-------|------------|
| Game 1 (Pong) | Red | (255, 0, 0) |
| Game 2 (Flappy) | Yellow | (255, 255, 0) |
| Game 3 (Catch) | Green | (0, 255, 0) |
| Game 4 (Invaders) | Cyan | (0, 255, 255) |
| Selection Border | White | (255, 255, 255) |
| Unselected | Dark Gray | (20, 20, 20) |

### In-Game Colors

#### Pong
| Element | Color | RGB Values |
|---------|-------|------------|
| Player Paddle | Blue | (0, 0, 255) |
| AI Paddle | Red | (255, 0, 0) |
| Ball | White | (255, 255, 255) |
| Center Line | Dark Gray | (40, 40, 40) |

#### Flappy Bird
| Element | Color | RGB Values |
|---------|-------|------------|
| Bird | Yellow | (255, 255, 0) |
| Pipes | Green | (0, 255, 0) |

#### Catch Game
| Element | Color | RGB Values |
|---------|-------|------------|
| Basket | Blue | (0, 0, 255) |
| Good Items | Teal | (0, 255, 128) |
| Bad Items | Red | (255, 0, 0) |
| Lives | Red | (255, 0, 0) |

#### Space Invaders
| Element | Color | RGB Values |
|---------|-------|------------|
| Player Ship | Cyan | (0, 255, 255) |
| Invaders | Green | (0, 255, 0) |
| Player Bullet | Yellow | (255, 255, 0) |
| Enemy Bullets | Red | (255, 0, 0) |

## Controls

### Menu Navigation
1. **Position hand** at distance to select game quadrant:
   - Top-left: Pong (0-100mm)
   - Top-right: Flappy Bird (100-200mm)
   - Bottom-left: Catch (200-300mm)
   - Bottom-right: Space Invaders (300-400mm)

2. **Wave hand quickly** (>100mm movement) to select game

### Game Controls
- **Distance mapping**: 50-400mm sensor range maps to 0-15 screen positions
- **Quick movements**: Trigger actions (jump, shoot)
- **Smooth tracking**: Continuous position control for paddles/baskets

## Technical Details

### Display System
- **Matrix Layout**: Zigzag pattern (alternating row direction)
- **Brightness**: 50/255 (adjustable in config)
- **Refresh Rate**: 20 FPS
- **Color Order**: GRB for WS2812B

### Sensor Configuration
- **Range**: 50-400mm operating distance
- **Resolution**: 16 discrete positions
- **Update Rate**: 20Hz
- **I2C Speed**: 400kHz

### Software Architecture
- **Framework**: ESP-IDF
- **LED Driver**: RMT peripheral for precise timing
- **Task Priority**: Game loop at priority 5
- **Stack Size**: 4096 bytes
- **Timing**: 50ms tick rate (20 FPS)

## Building and Flashing

```bash
# Build the project
pio run

# Upload to ESP32-C3
pio run -t upload

# Monitor serial output
pio device monitor
```

## Power Requirements

- **LED Matrix**: 60mA per LED at full white brightness
- **Maximum current**: 256 × 60mA = 15.36A
- **At 50/255 brightness**: ~3A typical
- **Recommended PSU**: 5V 10A minimum

## Exhibition Setup Tips

1. **Mounting**: Position sensor at comfortable hand height (waist level)
2. **Distance markers**: Add floor markers for optimal play distance
3. **Lighting**: Dim ambient lighting enhances LED visibility
4. **Instructions**: Simple signage showing hand gesture controls
5. **Auto-demo**: Games auto-return to menu after 3 seconds

## Troubleshooting

| Issue | Solution |
|-------|----------|
| No LED output | Check GPIO 2 connection and power supply |
| Sensor not responding | Verify I2C connections on GPIO 8/9 |
| Erratic controls | Ensure 50-400mm distance from sensor |
| Flickering LEDs | Increase power supply capacity |
| Game too fast/slow | Adjust vTaskDelay in game_task |

## Future Enhancements

- Score persistence with NVS
- Sound effects via I2S
- WiFi scoreboard sharing
- Additional games (Snake, Tetris)
- Difficulty settings
- Two-player modes with dual sensors

## License

MIT License - Feel free to modify for your exhibition needs!

## Safety Notes

- Use proper power supply with overcurrent protection
- Ensure adequate ventilation for LED matrix
- Secure all wiring to prevent trip hazards
- Consider UV/blue light exposure for extended viewing
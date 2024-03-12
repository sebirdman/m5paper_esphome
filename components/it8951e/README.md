```yaml
# example configuration:

spi:
  clk_pin: GPIO14
  mosi_pin: GPIO12
  miso_pin: GPIO13

display:
  - platform: it8951e
    id: m5paper_display
    cs_pin: GPIO15
    reset_pin: GPIO23
    reset_duration: 100ms
    busy_pin: GPIO27
    rotation: 0
    reversed: False
    update_interval: never
```
#!/bin/bash

west init -l iot-environmental-monitor
west update
west blobs fetch hal_espressif
west zephyr-export
west packages pip --install
west completion bash > "$HOME"/west-completion.bash
echo "source $HOME/west-completion.bash" >> "$HOME"/.bashrc
history -c

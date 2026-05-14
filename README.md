# Lightnet project

This project contains firmware for two devices:
* controller - a single control unit that controls many Panels
* panel - a panel device that can be connected with different panels and/or with the control unit

The control unit is connected to one (any) of the panel devices and each next panel device can be connected to the previous panel to one of theirs edge.
Panel device can have 3 or more edges (default 3, triangular shape) and any of the edges can be used to connect to any of other panel's edge.
All the devices create a tree structure where the controller unit is the starting point (root).

The controller unit initiates discovery process to initialize each panel, give it an ID and get to know its place on the tree.

`lib` folder also contains a webserver which runs on the Controller unit and exposes an API to control the devices through an external application.
                      
`schematic` folder contains electronics schematic diagrams of Controller and Panel.


# Fuses and bootloader setup:

In ArduinoIDE select target = Aruino UNO. Select port and burn the bootloader.

## atmega328p

Set fuses:

    C:\Users\przem\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17/bin/avrdude -C"C:\Users\przem\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17/etc/avrdude.conf" -c usbasp -p atmega328p -u -U lfuse:w:0xF7:m -U hfuse:w:0xD6:m -U efuse:w:0xFD:m

## atmega328pb

Erase first:

    C:\Users\przem\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17/bin/avrdude -C"C:\Users\przem\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17/etc/avrdude.conf" -c usbasp -p atmega328pb -e 

Set fuses

    C:\Users\przem\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17/bin/avrdude -C"C:\Users\przem\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17/etc/avrdude.conf" -c usbasp -p atmega328pb -B 32 -u -U lfuse:w:0xF7:m -U hfuse:w:0xD6:m -U efuse:w:0xF5:m
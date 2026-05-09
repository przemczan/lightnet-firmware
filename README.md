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

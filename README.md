# Drawpad

> Hack your **laptop touchpad** into a *drawing pad*

### Behaviour
After launching drawpad on the desired device (find it using `evtest(1)`),
it creates a transparent window.
Said window be comes the "playing field",
all absolute input coordinates retrieved are mapped to screen coordinates where clicks will be simulated.

For example,
if you drag and resize the window over GIMP's canvas area then switch tabs,
you can start drawing away.

### Requirements

#### Compile time
+ Xlib

#### Runtime
+ Linux
+ X11
+ Compositing window manager (optional, but highly recommended)

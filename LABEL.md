## Press-sure!

**Group members:** Jasper Boner and Edward Wermers  
**Materials:** Arduino Nano, 4×4 keypad, HC-SR04 ultrasonic sensor, two OLED displays, breadboard, jumper wires  
**Year:** 2026

It notices when someone is within 12 centimeters and watches for keypad presses.  
It calls you Nobody, Visitor, or Omnipresent, based on recent key presses.  
Then it displays your last key, total presses, and activity state on two OLEDs.  
It refuses to count a key press unless someone is detected nearby.

At startup, it waits four seconds before showing Nobody. A second OLED reports whether the ultrasonic sensor is responsive.

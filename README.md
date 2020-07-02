# Serial port over TCP
This project creates WiFi(TCP) to serial bridge.

The test project was built on D1 mini board connected to PC computer over USB as /dev/ttyUSB0 under Linux.
Modify the serial port device name in platformo.ini if you want to compile under Windows.

The project uses platformio build environment. 
[PlatformIO](https://platformio.org/) - Professional collaborative platform for embedded development.

# Building under Linux
* install PlatformIO
* enter project directory
* connect two Webmod D1 mini boards to PC computer over USB cable.
* type in terminal:
  platformio run
  platformio upload

You can also use IDE to build this project on Linux/Windows/Mac. My fvorite ones:
* [Code](https://code.visualstudio.com/) 
* [Atom](https://atom.io/)

# Building in Andruino IDE
You can also run it from Arduino by renaming the main.cpp file to serial_over_esp_now.ino and saving it in a directory with the same name.
* [install Arduino IDE](https://www.arduino.cc/en/main/OldSoftwareReleases)
* rename /src/main.cpp to serial_over_tcp.ino and place it in serial_over_tcp directory
* open serial_over_tcp.ino in Arduino IDE, select proper board or [install it](https://randomnerdtutorials.com/how-to-install-esp8266-board-arduino-ide/)
* compile and upload to board

Enjoy :-)

ESP-IDF Version: V5.3.3

Fix for Linux not detecting the ESP:
With the esp cam disconnected, run the following command:
tail -f /var/log/syslog
then, connect it through USB port.
Check the messages displayed on the terminal and see if there is any brltty message being displayed.
For some reason this brltty service messes up with the proper identification of the esp32 cam,
Disable it through the following commands:
sudo systemctl stop brltty-udev.service
sudo systemctl mask brltty-udev.service
sudo systemctl stop brltty.service
sudo systemctl disable brltty.service
If the esp is not being identified because of the brltty service, this will make the esp be detected
by your system.

Install docker to build and flash the project:

- sudo apt install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

Plug your esp32 into de USB port and run

- ls /dev/serial/by-id/

To find it's serial port.

Create a docker-compose.override.yml file to passthrough the usb port to the container(Example):

services:
  esp32:
    devices:
      - "/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0:/dev/ttyUSB0"

Build the docker image:
- docker compose build

Run the docker image:
- docker compose run esp32

Please refer to: https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/camera-application.html
If cam-hal messages are showing up.

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
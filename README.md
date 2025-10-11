## ESP32-NetworkScanner

The goal of this project is to create a network scanner utilizing ARP (Address Resolution Protocol) to identify devices on a Local Area Network (LAN). The objective is to develop an energy-efficient scanner capable of operating 24/7, with data accessible through a web interface.

### Web Interface

![WEBINTERFACE](https://raw.githubusercontent.com/liquidCS/ESP32-NetworkScanner/main/resources/esp32-ARP.png)

- **Green:** Device is currently online.
- **Red:** No device has ever been detected at this IP address.
- **Yellow:** Device was previously online but is currently offline.

## Install

1. Install [esp-idf](https://github.com/espressif/esp-idf)
2. clone this repo using `git clone https://github.com/liquidCS/ESP32-NetworkScanner.git`
3. cd into the project's folder using `cd ESP32-NetworkScanner`
4. Edit"WIFI_SSID" and "WIFI_PASSWORD" in main/wifiConnect.c  
5. Run `idf.py build` to build the project
6. Run `idf.py flash -p [COM port]` to flash the project to your esp32

## To-Do

- [x] Display vendors along with their corresponding MAC address.
- [ ] Support various subnet masks (theoretically functional, but not yet tested).
- [ ] Enhance scanning speed.
- [ ] Improve the web interface for better user experience.

## Technical Details

Instead of relying on the ping method, this project uses ARP for network scanning. Many computers and IoT devices do not respond to ping, so ARP was chosen in this project to scan devices on the network.

This project uses Espressif's esp-idf framework (version v5.4-dev), and the main thing it's doing is sending ARP packages this can be achieve using function in lwIP netif/etharp. Unfortunately after a certain version of esp-idf framework they have switch using their own esp-netif which does not include the etharp's functions. By using`esp_netif_get_netif_impl()` we can convert the esp-neif back to netif and access etharp function `etharp_request()`to send ARR packages to your desired destination. After sending the packet, we wait for a specified time to receive a response. We then use the `etharp_find_addr()` function to check if the IP address has responded. If the device has replied, its MAC address will be present in the ARP table and can be retrieved by this function.

Note: The speed of the scanning is limited by the size of the ARP table on the esp, but there might still be other solutions.

**Reference**

- [esp-neif](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_netif.html#esp-netif)
- [lwIP/etharp](https://github.com/m-labs/lwip/blob/master/src/netif/etharp.c)

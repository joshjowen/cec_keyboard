# CEC Keyboard
A small program written in C++ that allows HDMI-CEC commands to be mapped to keypresses on linux so that a remote control can be used for simple software navigation on a television. If enabled, a websocket server is created that allows CEC comands or key presses to be executed remotely.

## Installation
currently, this project must be built from source.
### Build dependencies
On Debian, Ubuntu, Raspbian the build dependencies can be install with apt by running:
```
sudo apt install git build-essential cmake libcec4-dev libyaml-cpp-dev libwebsocketpp-dev libboost-system-dev libjsoncpp-dev
```
### Source
Clone this repo into your home directory:
```
cd ~
git clone https://github.com/joshjowen/cec_keyboard.git
```
### Build
```
cd ~/cec_keyboard
mkdir -p build
cd build
cmake ..
make
```
### Install
After building, the binary can be installed into /usr/bin with:
```
sudo make install
```
### Autorun at boot
If you want the program to autorun at boot [cec_keyboard.service](https://github.com/joshjowen/cec_keyboard/cec_keyboard.service) is an example systemd script that starts it with the websocket server available on port 9091.

## Custom keymap
To use custom keymapping dump the current mapping by running the program as shown:
```
cec_keyboard -m > config.yaml
```
then modify the mappings as desired. for CEC_USER_CONTROL_CODE and KEY values that can be used, look in [ceckeymap.h](https://github.com/joshjowen/cec_keyboard/ceckeymap.h).
The new config file can be use with the '-c' switch, e.g.:
```
cec_keyboard -c [config file location]
```
## Websocket
The websocket server is only started if a port is provided, a port is given with the '-p' switch, e.g.:
```
cec_keyboard -p 9091
```
#### The command received from the websocket are JSON, with the format:
to send an enter key press:
```
{"target": "key", "command": "KEY_ENTER"}
```
To turn on device with logical address 0 (usually the TV):
```
{"target": "cec", "command": "on", "args": "0"}
```
To set the device as the active source:
```
{"target": "cec", "command": "activate"}
```
CEC commands that require arguments expect them in the same format as [cec-client](https://github.com/Pulse-Eight/libcec).
#### The following cec commands and arguments are recognised:
|Commands|args| | 
|---|---|---|
|"transmit"|bytes|transmit bytes.|
|"on" |address|power on the device with the given logical address.|
|"standby" |address|put the device with the given address in standby mode.|
|"set_addr_active" |physical_address|makes the specified physical address active.|
|"activate"| |make the CEC adapter the active source.|
|"deactivate"| |mark the CEC adapter as inactive source.|
|"volup"| |send a volume up command to the sound device if present.|
|"voldown"| |send a volume down command to the sound device if present.|
|"mute"| |send a mute/unmute command to the sound device if present.|

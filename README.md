# WebOS-8266

WebOS-8266 is a browser-based desktop operating system for the ESP8266. It serves a full web UI directly from the microcontroller and provides a desktop-style experience for controlling GPIO, managing files, monitoring system state, connecting to Wi-Fi networks, scheduling automation tasks, and using a built-in terminal.

The project is implemented as a single Arduino sketch and is designed to run on ESP8266 boards with LittleFS, OTA update support, mDNS, and a responsive web interface.

## Features

- Desktop-style web interface served directly from the ESP8266
- Responsive layout for desktop and mobile browsers
- Authentication with token-based session handling
- GPIO control, including digital output, PWM, servo control, and pin reading
- File management backed by LittleFS
- Wi-Fi management with scan, connect, and disconnect actions
- Built-in terminal with command execution
- Task scheduler for repeating automation actions
- Pin watcher system for event-driven monitoring
- System notifications and log history
- NTP time synchronization
- mDNS access at `webos-8266.local`
- OTA firmware updates

## Supported HTTP Endpoints

The sketch exposes a structured API for UI actions and automation.

- `GET /` - Main web application
- `POST /api/auth/login` - Authenticate the session
- `POST /api/auth/logout` - End the session
- `GET /api/system/info` - System information
- `GET /api/system/logs` - System logs
- `POST /api/system/reboot` - Reboot the device
- `GET /api/gpio/all` - List GPIO state
- `GET /api/gpio/set` - Set a GPIO output
- `GET /api/gpio/toggle` - Toggle a GPIO output
- `GET /api/gpio/pwm` - Set PWM output
- `GET /api/gpio/servo` - Set servo angle
- `GET /api/gpio/mode` - Change pin mode
- `GET /api/gpio/read` - Read a digital pin
- `GET /api/gpio/analog` - Read an analog value
- `GET /api/files` - List files in LittleFS
- `GET /api/files/read` - Read a file
- `POST /api/files/write` - Write a file
- `POST /api/files/rename` - Rename a file
- `POST /upload` - Upload a file to LittleFS
- `GET /api/wifi/status` - Wi-Fi status
- `GET /api/wifi/scan` - Scan for networks
- `POST /api/wifi/connect` - Connect to a network
- `POST /api/wifi/disconnect` - Disconnect Wi-Fi
- `GET /api/scheduler/list` - List scheduled tasks
- `POST /api/scheduler/add` - Add a scheduled task
- `POST /api/scheduler/delete` - Delete a scheduled task
- `POST /api/scheduler/toggle` - Enable or disable a scheduled task
- `GET /api/notifications` - Fetch notifications
- `POST /api/notifications/clear` - Clear notifications
- `POST /api/settings/set` - Update settings
- `POST /api/command` - Execute terminal commands

## Hardware Requirements

- ESP8266-based board such as NodeMCU, Wemos D1 mini, or similar
- USB cable and serial access for flashing
- Optional external components for GPIO, PWM, or servo testing

## Software Requirements

- Arduino IDE or PlatformIO
- ESP8266 board support package
- ESP8266 core libraries
- LittleFS support
- Servo support library

## Installation

1. Open `WebOS-8266.ino` in your Arduino environment.
2. Install the ESP8266 board package if it is not already available.
3. Select the correct ESP8266 board and serial port.
4. Build and upload the sketch.
5. Open the Serial Monitor at `115200` baud to view boot logs and the assigned IP address.

## Accessing The Interface

After startup, the device prints the access details to the serial console.

- Local network access: use the IP address shown in Serial Monitor
- mDNS access: `http://webos-8266.local`
- Wi-Fi access point mode: connect to `WebOS-8266`

### Default Credentials

The sketch currently ships with the following defaults:

- Username: `admin`
- Password: `admin`
- Access point password: `12345678`

Change these values before deploying the device in any shared or public environment.

## Configuration

The main configuration values are defined near the top of the sketch.

- Wi-Fi station SSID and password
- Access point SSID and password
- mDNS hostname
- NTP server and timezone offset
- Authentication enable flag
- Default username and password

If you want the device to join your existing Wi-Fi network, update the station credentials in the sketch and flash the board again.

## Runtime Behavior

At startup, WebOS-8266 performs the following steps:

1. Initializes GPIO, scheduler, and pin watcher subsystems
2. Mounts LittleFS and formats it if required
3. Connects to Wi-Fi or starts access point mode
4. Configures NTP time synchronization
5. Starts mDNS and OTA services
6. Registers the web UI and API routes
7. Launches the HTTP server

The main loop then handles web requests, OTA updates, scheduler ticks, pin watchers, and time synchronization checks.

## Files And Storage

LittleFS is used for file storage. From the web interface you can browse, upload, rename, read, and write files without a separate filesystem image.

## Security Notes

This project is intended for trusted networks and development environments unless you harden it first.

- Change all default credentials before production use
- Keep OTA access restricted to trusted networks
- Review which GPIO pins are exposed on your hardware before wiring external devices
- Disable authentication only if you fully understand the exposure it creates

## Project Status

This repository currently contains a single self-contained Arduino sketch. The code includes the server, UI, storage, device control, and automation logic in one file for ease of deployment.

## License

MIT License.

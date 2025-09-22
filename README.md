# tronbyt-matrixportal-m4
attempting to make a tronbyt with hardware that is a bit too old

## structure
1. ***firmware:*** PlatformIO-based environment to generate firmware for MatrixPortal M4
2. ***server:*** Server-side applications to support tronbyt
    a. *tronbyt-server* Docker Compose instance of pixlet
    b. *tronbyt-webp-png* Flask app for the server-side conversion of webp files into either pngs for animated gifs, original intent was just pngs

## background
The MatrixPortal M4 is much less powerful than more recent ESP32-based boards like the MatrixPortal S3, so the Tidbyt/Tronbyt code does not work for it directly. Examples include rendering webp files, but also flashing the firmware is not as straightforward.

## general approach
1. Set up the pixlet server as tronbyt-server, this was extremely easy with a Docker Compose instance.
    1. Secrets go in the tronbyt-server/.env file

        `SERVER_HOSTNAME_OR_IP="your_server_hostname_or_ip"`

        `SERVER_PORT=8000`

        `SERVER_PROTOCOL="http"`

        `SYSTEM_APPS_REPO="https://github.com/tronbyt/apps.git"`

        `PRODUCTION=1`

        `LOG-LEVEL=WARNING`

        `MAX_USERS=100`
    2. make the following folders, you may need to make at least the `tronbyt-server/app` folder writeable by all (a+w): 

        `tronbyt-server/app`

        `tronbyt-server/app/data`

        `tronbyt-server/app/users`
        
2. Set up the webp converter Flask app. Also pretty easy.
    1. `PIXLET_URL` should probably be a secret saved somewhere else, but the value here is a local-only IP
    2. also make the following folders. in one of my troubleshooting steps, a `tronbyt-webp-png/data` folder was also created but is otherwise unnecessary.

        `tronbyt-webp-png/app`

        `tronbyt-webp-png/app/data`

3. Load the firmware onto the MatrixPortal M4. This was more challenging because I have not worked with PlatformIO before, and the existing Tidbyt/Tronbyt projects all use it.
    1. Modified AnimatedGIF.h to rename all due to conflict with PNGdec

        `INTELSHORT(p)` to `INTELSHORT_GIF(p)`

        `INTELLONG(p)` to `INTELLONG_GIF(p)`

    2. Create `src/secrets.h`

        ```
        #pragma once

        // Your WiFi credentials
        const char SSID[] = "YOUR_WIFI_SSID";
        const char PASS[] = "YOUR_WIFI_PASSWORD";

        // Pixlet server
        const char* FEED_URL = "http://HOSTNAME_OR_IP:8001/feed";
        const int FEED_PORT = 8001;
        ```

    3. Note: one of the v1.8.x versions of WiFiNINA library included when the project was generated resulted in an unstable connection that would cause downloads over 1277 bytes to fail, include this is the platformio.ini instead: `https://github.com/adafruit/WiFiNINA/archive/refs/heads/master.zip`

## known issues
- animated webp to gif conversion is not perfect, is it an issue with not looping when it should?

## to do
- automatically dim or turn off at night
- can we build a pollen forecast app using a free or public API? `https://developers.google.com/maps/documentation/pollen/overview`
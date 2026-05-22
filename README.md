# RTSP-ZL-Player

A minimal C++/GStreamer RTSP viewer tuned for zero latency.
Built because off-the-shelf players (VLC, default `ffplay`) buffer too much
for live monitoring use.

## Streams it was built for

- `rtsp://rpi5.local:8555/cam`
- `rtsp://rpi5.local:8554/hdmi`

Any RTSP URL works - as long as it supports UDP as the transport.

## Dependencies (Ubuntu/Debian)

```sh
sudo apt install \
    cmake \
    libgstreamer1.0-dev \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav
```

## Build

```sh
cmake -S . -B build
cmake --build build -j
```

## Run

```sh
./build/rtsp-zl-player rtsp://rpi5.local:8555/cam
./build/rtsp-zl-player rtsp://rpi5.local:8554/hdmi
```

## How the latency is minimised

Pipeline: `rtspsrc → decodebin → videoconvert → autovideosink`

| Knob | Value | Effect |
| --- | --- | --- |
| `rtspsrc latency` | `0` | No jitter-buffer delay |
| `rtspsrc protocols` | `UDP` | Avoid TCP retransmit lag |
| `rtspsrc buffer-mode` | `none` | Disable smoothing buffer |
| `rtspsrc do-retransmission` | `false` | Drop lost packets, don't wait |
| `rtspsrc drop-on-latency` | `true` | Drop late frames rather than queue |
| sink `sync` | `false` | Render frames as soon as decoded, ignore PTS |

If software decoding becomes the bottleneck, swap `decodebin` for a hardware
decoder (`vaapih264dec`, `nvh264dec`, `v4l2h264dec`, …).

# Build QPixelDesigner on Ubuntu 24.04.
#   docker build -t qpixeldesigner:ubuntu24 .
#   docker run --rm --network=host -e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix qpixeldesigner:ubuntu24
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        qt6-base-dev \
        qt6-base-dev-tools \
        qt6-declarative-dev \
        libgl1-mesa-dev \
        fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j"$(nproc)" \
 && export QT_QPA_PLATFORM=offscreen \
 && timeout 10 ./build/QPixelDesigner; test $? -eq 124

CMD ["/app/build/QPixelDesigner"]

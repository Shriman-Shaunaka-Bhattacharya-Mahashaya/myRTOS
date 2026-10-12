# ==============================================================================
# myRTOS Containerized Browser-Based Simulation Environment
# ==============================================================================
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Install C++20 build tools, OpenGL graphics libraries, and noVNC web streaming services
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libx11-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    xvfb \
    x11vnc \
    novnc \
    websockify \
    openbox \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy repository source files
COPY CMakeLists.txt ./
COPY kernel/ ./kernel/
COPY port/ ./port/
COPY hw/ ./hw/
COPY ui/ ./ui/
COPY tests/ ./tests/
COPY examples/ ./examples/
COPY entrypoint.sh ./

# Configure and compile with Ninja (forcing X11 backend for GLFW)
RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGLFW_BUILD_WAYLAND=OFF \
    && cmake --build build

# Verify kernel by running automated unit test suite inside container
RUN ./build/myrtos_tests

# Make startup script executable
RUN chmod +x /app/entrypoint.sh

# Expose noVNC web browser port
EXPOSE 8080

ENTRYPOINT ["/app/entrypoint.sh"]

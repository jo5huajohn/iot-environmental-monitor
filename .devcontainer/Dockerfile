FROM mcr.microsoft.com/devcontainers/base:trixie

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install --no-install-recommends -y git cmake \
    ninja-build gperf ccache dfu-util device-tree-compiler \
    wget python3-dev python3-pip python3-venv python3-tk xz-utils \
    file make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1 \ 
    && rm -rf /var/lib/apt/lists/*

RUN pip install west --break-system-packages

ARG ZEPHYR_VERSION="v4.3.0"
RUN BASE="https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/${ZEPHYR_VERSION}/scripts" \
    && wget -q -P /tmp/requirements \
        "${BASE}/requirements.txt" \
        "${BASE}/requirements-base.txt" \
        "${BASE}/requirements-build-test.txt" \
        "${BASE}/requirements-run-test.txt" \
        "${BASE}/requirements-extras.txt" \
        "${BASE}/requirements-compliance.txt" \
    && pip install --break-system-packages \
        -r /tmp/requirements/requirements.txt \
    && rm -rf /tmp/requirements

RUN pip install --break-system-packages esptool>=5.0.2

ARG ZEPHYR_SDK_VERSION="0.17.4"
RUN cd /opt \
    && wget -q https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64.tar.xz \
    && wget -qO - https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/sha256.sum \
       | shasum --check --ignore-missing \
    && tar -xf zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64.tar.xz \
    && rm zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64.tar.xz \
    && cd zephyr-sdk-${ZEPHYR_SDK_VERSION} \
    && ./setup.sh -t all -h -c

RUN curl https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-unknown-linux-gnu/nrfutil -o /usr/local/bin/nrfutil \
    && chmod +x /usr/local/bin/nrfutil && nrfutil install device nrf5sdk-tools completion

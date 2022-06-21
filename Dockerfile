# Build development environment
# docker build --tag esp-dev-env .

# Launch development environment
# docker run --device /dev/bus/usb/ --interactive --rm --tty --volume "$(pwd)":/host-volume/ esp-dev-env

# _**NOTE:** In order to utilize DFU and debugging functionality, you must
# install (copy) the `.rules` file related to your debugging probe into the
# `/etc/udev/rules.d` directory of the host machine and restart the host._

# _**NOTE:** The debugging probe must be attached while the container is
# launched in order for it to be accessible from inside the container._

# Define global arguments
ARG ARDUINO_CLI_VERSION=0.24.0
ARG DEBIAN_FRONTEND="noninteractive"
ARG ESP_IDF_VERSION=master
ARG UID=1000
ARG USER=maker

# POSIX compatible (Linux/Unix) base image
FROM debian:stable-slim

# Import global arguments
ARG ARDUINO_CLI_VERSION
ARG DEBIAN_FRONTEND
ARG ESP_IDF_VERSION
ARG UID
ARG USER

# Define local arguments
ARG BINDIR=/usr/local/bin
ARG ECHO_BC_FILE='$bcfile'

# Create Non-Root User
RUN ["dash", "-c", "\
    addgroup \
     --gid ${UID} \
     \"${USER}\" \
 && adduser \
     --disabled-password \
     --gecos \"\" \
     --ingroup \"${USER}\" \
     --uid ${UID} \
     \"${USER}\" \
 && usermod \
     --append \
     --groups \"dialout,plugdev\" \
     \"${USER}\" \
"]
ENV PATH="/home/${USER}/.local/bin:${PATH}"

# Install Arduino Prerequisites
RUN ["dash", "-c", "\
    apt-get update --quiet \
 && apt-get install --assume-yes --no-install-recommends --quiet \
     bash \
     bash-completion \
     ca-certificates \
     curl \
     python-is-python3 \
     python3 \
     python3-pip \
     ssh \
     tree \
 && pip install \
     adafruit-nrfutil \
     pyserial \
"]

# Download/Install Arduino CLI
RUN ["dash", "-c", "\
    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=${BINDIR} sh -s ${ARDUINO_CLI_VERSION} \
 && mkdir -p /etc/bash_completion.d/ \
 && arduino-cli completion bash > /etc/bash_completion.d/arduino-cli.sh \
 && echo >> /etc/bash.bashrc \
 && echo \"for bcfile in /etc/bash_completion.d/* ; do\" >> /etc/bash.bashrc \
 && echo \"    [ -f \\\"${ECHO_BC_FILE}\\\" ] && . \\\"${ECHO_BC_FILE}\\\"\" >> /etc/bash.bashrc \
 && echo \"done\" >> /etc/bash.bashrc \
 && echo \"if [ -f /etc/bash_completion ]; then\" >> /etc/bash.bashrc \
 && echo \"    . /etc/bash_completion\" >> /etc/bash.bashrc \
 && echo \"fi\" >> /etc/bash.bashrc \
"]

# Step 1. Install ESP-IDF Prerequisites
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/linux-setup.html#install-prerequisites
RUN ["dash", "-c", "\
    apt-get update --quiet \
 && apt-get install --assume-yes --no-install-recommends --quiet \
     bison \
     ccache \
     cmake \
     dfu-util \
     flex \
     gcc `# [pip] gevent` \
     git \
     gperf \
     libffi-dev \
     libpython2.7 \
     libssl-dev \
     libtool `# [pip] gevent` \
     libusb-1.0-0 \
     make `# [pip] gevent` \
     nano \
     ninja-build \
     python3-dev `# [pip] gevent` \
     python3-pip \
     python3-setuptools \
     python3-venv \
     udev \
     wget \
 && pip install \
     'bitstring>=3.1.6' \
     'construct==2.10.54' \
     'cryptography>=2.1.4' \
     'future>=0.15.2' \
     'gdbgui==0.13.2.0' \
     'idf-component-manager~=1.0' \
     'itsdangerous<2.1' \
     'jinja2<3.1' \
     'kconfiglib==13.7.1' \
     'pyelftools>=0.22' \
     'pygdbmi<=0.9.0.2' \
     'pyparsing>=2.0.3,<2.4.0' \
     'python-socketio<5' \
     'reedsolo>=1.5.3,<=1.5.4' \
 && apt-get clean \
 && apt-get purge \
 && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* \
"]

# Install IDF as non-root user
WORKDIR /home/${USER}/
USER ${USER}

# Step 2. Get ESP-IDF
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html#step-2-get-esp-idf
RUN ["dash", "-c", "\
    mkdir esp \
 && cd esp/ \
 && git clone --recursive https://github.com/espressif/esp-idf.git -b ${ESP_IDF_VERSION} \
"]

# Step 3. Set up the tools
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html#step-3-set-up-the-tools
RUN ["dash", "-c", "\
    cd ./esp/esp-idf \
 && ./install.sh esp32 \
"]

# Install Arduino CLI as root user
WORKDIR /home/root/
USER root

# Download/Install Arduino CLI
RUN ["dash", "-c", "\
    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=${BINDIR} sh -s ${ARDUINO_CLI_VERSION} \
 && mkdir -p /etc/bash_completion.d/ \
 && arduino-cli completion bash > /etc/bash_completion.d/arduino-cli.sh \
 && echo >> /etc/bash.bashrc \
 && echo \"for bcfile in /etc/bash_completion.d/* ; do\" >> /etc/bash.bashrc \
 && echo \"    [ -f \\\"${ECHO_BC_FILE}\\\" ] && . \\\"${ECHO_BC_FILE}\\\"\" >> /etc/bash.bashrc \
 && echo \"done\" >> /etc/bash.bashrc \
 && echo \"if [ -f /etc/bash_completion ]; then\" >> /etc/bash.bashrc \
 && echo \"    . /etc/bash_completion\" >> /etc/bash.bashrc \
 && echo \"fi\" >> /etc/bash.bashrc \
"]

# Configure Arduino CLI as non-root user
WORKDIR /home/${USER}/
USER ${USER}

# Configure Arduino CLI
RUN ["dash", "-c", "\
    arduino-cli config init \
 && arduino-cli config add board_manager.additional_urls \
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json \
 && arduino-cli core update-index \
 && arduino-cli core install esp32:esp32 \
 && arduino-cli lib install \"Blues Wireless Notecard\" \
 && arduino-cli lib install \"Blues Wireless Notecard Auxiliary Wi-Fi\" \
 && arduino-cli lib install \"LM75A Arduino library\" \
"]

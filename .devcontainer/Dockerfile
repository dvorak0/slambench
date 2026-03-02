FROM ubuntu:22.04

# Install build essentials, Python 3.10 toolchain, sudo, and deps for symforce build
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        python3 python3-pip python3-dev python3-venv \
        build-essential cmake ninja-build libgmp-dev \
        git curl ca-certificates sudo && \
    rm -rf /var/lib/apt/lists/*

# Upgrade pip
RUN python3 -m pip install --no-cache-dir --upgrade pip

# Provide SCM version hints (build context lacks .git metadata)
ENV SETUPTOOLS_SCM_PRETEND_VERSION_FOR_SYMFORCE=0.0.0 \
    SETUPTOOLS_SCM_PRETEND_VERSION_FOR_SKYMARSHAL=0.0.0 \
    SETUPTOOLS_SCM_PRETEND_VERSION=0.0.0

# Copy symforce submodule and build/install
WORKDIR /opt/symforce
COPY symforce/ /opt/symforce/
RUN python3 -m pip install --no-cache-dir -e .[dev] && \
    mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j"$(nproc)" bundle_adjustment_in_the_large_example

# Create a non-root user for devcontainer sessions
ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=$USER_UID
RUN groupadd --gid $USER_GID $USERNAME && \
    useradd --uid $USER_UID --gid $USER_GID -m $USERNAME && \
    echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/$USERNAME && \
    chmod 0440 /etc/sudoers.d/$USERNAME

# Switch to non-root user by default
USER $USERNAME
ENV HOME=/home/$USERNAME

# Back to workspace mount point
WORKDIR /workspace

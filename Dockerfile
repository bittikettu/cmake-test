FROM ubuntu AS cmakedev

WORKDIR /builddir
# Install dependencies and set timezone
RUN apt-get update && apt-get install -y \
    tzdata locales \
    && echo "Europe/Helsinki" > /etc/timezone \
    && ln -sf /usr/share/zoneinfo/Europe/Helsinki /etc/localtime \
    && dpkg-reconfigure -f noninteractive tzdata \
    && locale-gen fi_FI.UTF-8 \
    && update-locale LANG=fi_FI.UTF-8 \
    && apt-get install -y cmake clang git ninja-build clangd gdb lldb liblldb-dev\
    && apt-get clean autoclean \
    && apt-get autoremove --yes\
    && rm -rf /var/lib/{apt,dpkg,cache,log} \
    && git clone https://github.com/lldb-tools/lldb-mi.git && cd lldb-mi && mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. \
    && make -j$(nproc) && make install

# Set environment variables
ENV LANG=fi_FI.UTF-8
ENV LC_ALL=fi_FI.UTF-8
ENV TZ=Europe/Helsinki

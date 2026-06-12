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

# ============================================================================
# WebAssembly build — compiles the raylib app to WASM with Emscripten.
#
#   docker build -t vfd-9000-web .
#   docker run --rm -p 8080:80 vfd-9000-web      # open http://localhost:8080
#
# The native dev image is the 'cmakedev' stage above:
#   docker build --target cmakedev -t vfd-9000-dev .
# ============================================================================
FROM emscripten/emsdk:3.1.64 AS webbuild
WORKDIR /src
COPY . .
# raylib is fetched and rebuilt for the browser (PLATFORM=Web set by
# src_raylib/CMakeLists.txt when EMSCRIPTEN); boot.mp3 is packed into the
# preload bundle. Output lands in build-web/src_raylib/index.{html,js,wasm,data}.
RUN emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build-web --parallel

# ---- static file server for the generated site ----
FROM nginx:alpine AS web
COPY --from=webbuild /src/build-web/src_raylib/index.html /usr/share/nginx/html/
COPY --from=webbuild /src/build-web/src_raylib/index.js   /usr/share/nginx/html/
COPY --from=webbuild /src/build-web/src_raylib/index.wasm /usr/share/nginx/html/
COPY --from=webbuild /src/build-web/src_raylib/index.data /usr/share/nginx/html/
EXPOSE 80

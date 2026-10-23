FROM --platform=linux/arm64 ubuntu:24.04

RUN sed -i 's@//.*archive.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g' /etc/apt/sources.list.d/ubuntu.sources && \
    sed -i 's@//.*security.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g' /etc/apt/sources.list.d/ubuntu.sources

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential make cmake libelf-dev ca-certificates wget unzip && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN gcc -shared -fPIC -o liblog.so fake_android.c && \
    gcc -shared -fPIC -o libandroid.so fake_android.c && \
    g++ -std=c++17 -o server server.cpp -ldl

RUN wget -q https://github.com/termux/termux-elf-cleaner/archive/refs/heads/master.zip && \
    unzip -q master.zip && cd termux-elf-cleaner-master && \
    cmake . && make && make install && cd .. && \
    termux-elf-cleaner libmmai.so && \
    apt-get update && apt-get install -y --no-install-recommends patchelf && \
    echo "===== 诊断 1: libmmai.so 原始 NEEDED =====" && \
    patchelf --print-needed libmmai.so && \
    echo "===== 诊断 2: 替换 libc.so -> libc.so.6 =====" && \
    patchelf --replace-needed libc.so libc.so.6 libmmai.so && \
    echo "===== 诊断 3: 替换后 NEEDED =====" && \
    patchelf --print-needed libmmai.so && \
    echo "===== 诊断 4: 设置 RPATH =====" && \
    patchelf --set-rpath '$ORIGIN' libmmai.so && \
    patchelf --print-rpath libmmai.so && \
    echo "===== 诊断 5: 系统 libc.so 是什么 =====" && \
    file /lib/aarch64-linux-gnu/libc.so || true && \
    ls -la /lib/aarch64-linux-gnu/libc.so* && \
    echo "===== 诊断 6: /app 目录内容 =====" && \
    ls -la /app && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

EXPOSE 8080

CMD ["./server"]

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
    patchelf --set-rpath '$ORIGIN' libmmai.so && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

EXPOSE 8080

CMD ["./server"]

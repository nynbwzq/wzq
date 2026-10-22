FROM --platform=linux/arm64 ubuntu:24.04

# 1. 替换为清华源，加速下载
RUN sed -i 's@//.*archive.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g' /etc/apt/sources.list.d/ubuntu.sources && \
    sed -i 's@//.*security.ubuntu.com@//mirrors.tuna.tsinghua.edu.cn@g' /etc/apt/sources.list.d/ubuntu.sources

# 防止安装时弹出交互界面卡住
ENV DEBIAN_FRONTEND=noninteractive

# 2. 安装依赖 (24.04自带的cmake就是新版，直接过)
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ gcc cmake libelf-dev ca-certificates wget unzip && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# 3. 编译伪造库和 C++ 服务器
RUN gcc -shared -fPIC -o liblog.so fake_android.c && \
    gcc -shared -fPIC -o libandroid.so fake_android.c && \
    g++ -std=c++17 -o server server.cpp -ldl

# 4. 下载并编译 termux-elf-cleaner 洗掉安卓依赖
RUN wget -q https://github.com/termux/termux-elf-cleaner/archive/refs/heads/master.zip && \
    unzip -q master.zip && \
    cd termux-elf-cleaner-master && \
    cmake . && make && make install && cd .. && \
    termux-elf-cleaner libmmai.so

EXPOSE 8080
CMD ["./server"]

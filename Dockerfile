FROM --platform=linux/arm64 ubuntu:22.04
RUN apt-get update && apt-get install -y g++ gcc cmake libelf-dev git && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN gcc -shared -fPIC -o liblog.so fake_android.c && \
    gcc -shared -fPIC -o libandroid.so fake_android.c && \
    g++ -std=c++17 -o server server.cpp -ldl
RUN git clone https://github.com/termux/termux-elf-cleaner.git && \
    cd termux-elf-cleaner && cmake . && make && make install && cd .. && \
    termux-elf-cleaner libmmai.so
EXPOSE 8080
CMD ["./server"]
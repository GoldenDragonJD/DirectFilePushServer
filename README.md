# DirectFilePushServer

A lightweight server application that acts as the hub for the client application [DirectFilePushClient]("https://github.com/GoldenDragonJD/DirectFilePushClient") to connect to.

My goal with this project was to allow for any size file transfer to be done without anyone needing to portforward
or pay a cloud service to upload their files onto. The main thought was speed when designing this, 
so I decided to first time c++ for this project,
but I'd say with the help of ChatGPT I did pretty good. I'm open to any feedback so please feel free to share some.

---

## Features

- Supports sending files, folders, and messages between a pair of connected clients
- Lightweight and easy to deploy
- Cross-platform un-friendly linux only!
- Controllable Memory Usage

---

## Key Insight

During testing, I found that each client only takes up around 16.38 KB of memory, this does not count for the memory allocated
for transferring the files i.e. CHUNK_SIZE. The testing parameters were 4 clients, server set to 1024 bytes or 1KB
with the parameter ```--chunk-size 1024``` and a range of actions such as messaging, transferring a folder of files while having
encryption toggled on and off. After constantly monitoring the memory usage it stayed and did not go above 299.01 KB.
After letting the server idle for a bit I noticed that the RAM usage went down to around 51KB and running the test again with
the same test cases, the RAM usage went up to 214.07KB, so keep this information in mind when on of the key features being
**Lightweight**.

---

## Tech Stack

- **Language:** C++  
- **Networking:** TCP sockets using Linux API
- **Serialization:** JSON  

---

## Getting Started

### Prerequisites

Make sure you have the following installed:

- CMake 3.28.3+ (optional)
- g++ or A C++20-compatible compiler

---

## Build & Run

### g++ only Method
```bash
sudo apt update
sudo apt install g++ -y
git clone https://github.com/GoldenDragonJD/DirectFilePushServer.git
cd DirectFilePushServer
g++ -std=c++20 main.cpp -o DirectFilePushServer
```

### cmake Method
```bash
sudo apt update
sudo apt install cmake g++ -y
git clone https://github.com/GoldenDragonJD/DirectFilePushServer.git
cd DirectFilePushServer
cmake .
cmake --build .
```
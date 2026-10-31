# BidMatrix

BidMatrix is a real-time auction platform featuring second-price (Vickrey) settlement, powered by a C++ backend utilizing custom Binary Search Tree (Max-BST) data structures and an interactive web frontend.

## Features

- **Real-Time Bidding**: Fast in-memory bid placement and updates.
- **Max-BST Engine**: Custom binary search tree structure for optimal bid hierarchy and retrieval.
- **Vickrey Second-Price Settlement**: The highest bidder wins, but pays the second-highest bid price.
- **Automated Expiry Checker**: Background thread that settles expired auctions automatically.
- **Web Interface**: Clean, responsive frontend for monitoring live auctions, viewing closed results, and creating auctions.

## Project Structure

```
bidmatrix_project/
├── backend/
│   ├── include/       # Header files (BST, models, HTTP server, auction manager)
│   ├── src/           # C++ source code (main.cpp)
│   ├── Makefile       # Linux/Unix build file
│   └── build.bat      # Windows compilation script
├── frontend/          # Web dashboard and UI assets
├── start.bat          # One-click launcher for Windows
└── .gitignore
```

## Getting Started

### Prerequisites

- GCC / MinGW with `g++` (C++14 support)
- Windows / Linux environment

### Build & Run

**Windows:**
1. Run `backend\build.bat` to compile the backend executable (`bidmatrix.exe`).
2. Run `start.bat` to launch the server and open the web dashboard in your browser (`http://localhost:8080`).

**Manual:**
```bash
cd backend
g++ -std=c++14 -O2 -Wall -Wextra -I include -o ../bidmatrix.exe src/main.cpp -lws2_32
cd ..
./bidmatrix.exe
```
Open `http://localhost:8080` in your web browser.

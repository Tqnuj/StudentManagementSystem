# Student Management System

A college assignment project using **C++ for the backend** and **HTML, CSS, and JavaScript for the frontend**.

### Features
- Add, view, edit, and delete student records.
- Search and filter students.
- Store student details in a CSV file.

### Technologies
- C++17
- HTML, CSS, JavaScript
- CSV file storage

### Requirements
- Windows
- GCC/G++ compiler (C++17)
- MSYS2 MINGW64
- Google Chrome or another web browser

### How to Run Locally

**1. Clone the repository**

Open the MSYS2 MINGW64 terminal and run:

```bash
git clone https://github.com/Tqnj/StudentManagementSystem.git
```

**2. Open the project folder**

```bash
cd StudentManagementSystem
```

**3. Compile the C++ backend**

```bash
g++ -std=c++17 -O2 backend/main.cpp -o student_server.exe -lws2_32
```

**4. Start the server**

```bash
./student_server.exe
```

**5. Open the application**

Open your browser and visit:

http://127.0.0.1:8080

Keep the terminal open while using the application. Press `Ctrl + C` to stop the server.

### Data Storage

Student records are stored locally in `data/students.csv`.

### Learning Objectives

This project demonstrates C++ functions, data structures, file handling, input validation, and basic communication between a web frontend and a backend.

### Author
**Tanuj Kumar**  
Computer Science and Engineering (CSE)

**Purpose:** Academic assignment and learning project.

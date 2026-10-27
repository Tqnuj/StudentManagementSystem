#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
echo
echo "CampusDesk - Student Management System"
echo "---------------------------------------"
g++ -std=c++17 -O2 backend/main.cpp -o student_server
echo
echo "Starting server. Keep this terminal open."
echo "Open http://127.0.0.1:8080 in your browser."
echo
exec ./student_server

@echo off
start cmd /k "cd backend && admission_server.exe"
start cmd /k "cd frontend && python -m http.server 3000" 
@echo off
rem Installs Xtreme Lab: asks Windows for permission, then runs Install.ps1 next to this file.
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File \"%~dp0Install.ps1\"'"

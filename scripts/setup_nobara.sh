#!/bin/bash
# =============================================================================
# Setup script per Nobara Linux 44 (Fedora-based)
# Installa tutte le dipendenze necessarie per compilare la DAW.
# =============================================================================

set -e

echo "=========================================="
echo " FL Studio Clone — Setup su Nobara Linux"
echo "=========================================="

# --- 1. Aggiorna sistema ---
echo "[1/5] Aggiornamento sistema..."
sudo dnf upgrade --refresh -y

# --- 2. Dipendenze di build ---
echo "[2/5] Installazione dipendenze di build..."
sudo dnf install -y \
    gcc-c++ \
    cmake \
    git \
    ninja-build \
    make

# --- 3. Dipendenze JUCE e audio ---
echo "[3/5] Installazione dipendenze audio e GUI..."
sudo dnf install -y \
    pipewire-devel \
    pipewire-jack-audio-connection-kit-devel \
    libjack2-devel \
    alsa-lib-devel \
    fontconfig-devel \
    freetype-devel \
    mesa-libGLU-devel \
    gtk3-devel \
    # Aggiunte per VST3 supporto
    vulkan-devel \
    vulkan-tools

# --- 4. Verifica PipeWire ---
echo "[4/5] Verifica PipeWire..."
if command -v pw-cli &> /dev/null; then
    PW_VERSION=$(pw-cli --version 2>/dev/null || echo "sconosciuto")
    echo "  ✓ PipeWire: $PW_VERSION"
else
    echo "  ✗ PipeWire non trovato — installazione..."
    sudo dnf install -y pipewire pipewire-pulse
fi

# --- 5. Verifica servizi ---
echo "[5/5] Verifica servizi audio..."
if systemctl --user is-active pipewire &> /dev/null; then
    echo "  ✓ PipeWire service: active"
else
    echo "  ⚠ PipeWire service: inactive — avvio..."
    systemctl --user start pipewire
    systemctl --user enable pipewire
fi

if systemctl --user is-active pipewire-pulse &> /dev/null; then
    echo "  ✓ PipeWire-Pulse service: active"
else
    echo "  ⚠ PipeWire-Pulse service: inactive — avvio..."
    systemctl --user start pipewire-pulse
    systemctl --user enable pipewire-pulse
fi

echo ""
echo "=========================================="
echo " Setup completato!"
echo ""
echo " Prossimi passi:"
echo "   1. cd flstudio-clone"
echo "   2. mkdir build && cd build"
echo "   3. cmake .. -G Ninja"
echo "   4. cmake --build ."
echo "   5. ./FLStudioClone"
echo "=========================================="
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/MainWindow.h"

// =============================================================================
// Entry point — JUCE Application
// Su Nobara Linux 44 (Fedora-based), PipeWire è il backend audio default.
// JUCE rileva automaticamente PipeWire quando disponibile.
// =============================================================================

int main(int /*argc*/, char* /*argv*/[]) {
    // Info di avvio
    std::cout << "========================================\n"
              << " FL Studio Clone DAW v0.1.0\n"
              << " Platform: Nobara Linux 44 (Fedora)\n"
              << " Audio Backend: PipeWire (via JUCE)\n"
              << " GUI: JUCE 8 / KDE Plasma compatible\n"
              << "========================================\n";

    // Init JUCE (registra plugins, font, ecc.)
    juce:: initialiseJuce_GUI();

    // Crea e mostra la finestra principale
    auto mainWindow = std::make_unique<MainWindow>();

    // Esegui il message loop JUCE
    juce::MessageManager::getInstance()->runDispatchLoop();

    return 0;
}
#if defined(__APPLE__)

#include <aquamarine/backend/Backend.hpp>

using namespace Aquamarine;
using namespace Hyprutils::Memory;
#define SP CSharedPointer

SP<CSession> Aquamarine::CSession::attempt(Hyprutils::Memory::CSharedPointer<CBackend> backend_) {
    return nullptr;
}

Aquamarine::CSession::~CSession() {
    sessionDevices.clear();
    libinputDevices.clear();
}

void Aquamarine::CSession::onReady() {
    ;
}

void Aquamarine::CSession::dispatchUdevEvents() {
    ;
}

void Aquamarine::CSession::dispatchLibinputEvents() {
    ;
}

void Aquamarine::CSession::dispatchLibseatEvents() {
    ;
}

void Aquamarine::CSession::dispatchPendingEventsAsync() {
    dispatchLibseatEvents();
    dispatchLibinputEvents();
}

std::vector<Hyprutils::Memory::CSharedPointer<SPollFD>> Aquamarine::CSession::pollFDs() {
    return {};
}

bool Aquamarine::CSession::switchVT(uint32_t vt) {
    return false;
}

#endif

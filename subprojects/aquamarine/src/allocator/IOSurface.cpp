#if defined(__APPLE__)

#include <algorithm>
#include <aquamarine/allocator/IOSurface.hpp>
#include <aquamarine/backend/Backend.hpp>
#include <aquamarine/allocator/Swapchain.hpp>
#include <aquamarine/output/Output.hpp>
#include "FormatUtils.hpp"
#include "Shared.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreVideo/CVPixelBuffer.h>
#include <mach/kern_return.h>
#include <optional>

using namespace Aquamarine;
using namespace Hyprutils::Memory;
#define SP CSharedPointer

static SDRMFormat guessFormatFrom(std::vector<SDRMFormat> formats, bool cursor, bool scanout) {
    if (formats.empty())
        return SDRMFormat{};

    if (!cursor) {
        /*
            Try to find 10bpp formats first, as they offer better color precision.
            For cursors, don't, as these almost never support that.
        */
        if (!scanout) {
            if (auto it = std::ranges::find_if(formats, [](const auto& f) { return f.drmFormat == DRM_FORMAT_ARGB2101010 || f.drmFormat == DRM_FORMAT_ABGR2101010; });
                it != formats.end())
                return *it;
        }

        if (auto it = std::ranges::find_if(formats, [](const auto& f) { return f.drmFormat == DRM_FORMAT_XRGB2101010 || f.drmFormat == DRM_FORMAT_XBGR2101010; });
            it != formats.end())
            return *it;
    }

    if (!scanout || cursor /* don't set opaque for cursor plane */) {
        if (auto it = std::ranges::find_if(formats, [](const auto& f) { return f.drmFormat == DRM_FORMAT_ARGB8888 || f.drmFormat == DRM_FORMAT_ABGR8888; }); it != formats.end())
            return *it;
    }

    if (auto it = std::ranges::find_if(formats, [](const auto& f) { return f.drmFormat == DRM_FORMAT_XRGB8888 || f.drmFormat == DRM_FORMAT_XBGR8888; }); it != formats.end())
        return *it;

    for (auto const& f : formats) {
        auto name = fourccToName(f.drmFormat);

        /* 10 bpp RGB */
        if (name.contains("30"))
            return f;
    }

    for (auto const& f : formats) {
        auto name = fourccToName(f.drmFormat);

        /* 8 bpp RGB */
        if (name.contains("24"))
            return f;
    }

    return formats.at(0);
}

static std::optional<OSType> pixelFormatFrom(uint32_t drmFormat) {
    switch (drmFormat) {
        case DRM_FORMAT_XRGB8888:
        case DRM_FORMAT_ARGB8888: return kCVPixelFormatType_32BGRA;
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_ABGR8888: return kCVPixelFormatType_32RGBA;
        case DRM_FORMAT_RGBX8888:
        case DRM_FORMAT_RGBA8888: return kCVPixelFormatType_32ABGR;
        case DRM_FORMAT_BGRX8888:
        case DRM_FORMAT_BGRA8888: return kCVPixelFormatType_32ARGB;
        case DRM_FORMAT_XRGB2101010:
        case DRM_FORMAT_ARGB2101010: return kCVPixelFormatType_ARGB2101010LEPacked;
        default: return std::nullopt;
    }
}

static void setNumber(CFMutableDictionaryRef dict, CFStringRef key, int32_t value) {
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
}

Aquamarine::CIOSurfaceAttachment::CIOSurfaceAttachment(IOSurfaceRef surface_) : surface(surface_) {
    if (surface)
        CFRetain(surface);
}

Aquamarine::CIOSurfaceAttachment::~CIOSurfaceAttachment() {
    if (surface)
        CFRelease(surface);
}

Aquamarine::CIOSurfaceBuffer::CIOSurfaceBuffer(const SAllocatorBufferParams& params, Hyprutils::Memory::CWeakPointer<CIOSurfaceAllocator> allocator_,
                                               Hyprutils::Memory::CSharedPointer<CSwapchain> swapchain) : allocator(allocator_) {
    if (!allocator)
        return;

    attrs.size   = params.size;
    attrs.format = params.format;
    size         = attrs.size;

    const bool CURSOR           = params.cursor && params.scanout;
    const bool EXPLICIT_SCANOUT = params.scanout && swapchain->currentOptions().scanoutOutput && !params.multigpu;

    TRACE(allocator->backend->log(AQ_LOG_TRACE,
                                  std::format("IOSurface: Allocating a buffer: size {}, format {}, cursor: {}, scanout: {}", attrs.size, fourccToName(attrs.format), CURSOR,
                                              params.scanout)));

    const auto FORMATS = CURSOR ? swapchain->backendImpl->getCursorFormats() :
                                  (EXPLICIT_SCANOUT ? swapchain->currentOptions().scanoutOutput->getRenderFormats() : swapchain->backendImpl->getRenderFormats());

    TRACE(allocator->backend->log(AQ_LOG_TRACE, std::format("IOSurface: Available formats: {}", FORMATS.size())));

    if (attrs.format == DRM_FORMAT_INVALID) {
        attrs.format = guessFormatFrom(FORMATS, CURSOR, params.scanout).drmFormat;
        if (attrs.format != DRM_FORMAT_INVALID)
            allocator->backend->log(AQ_LOG_DEBUG, std::format("IOSurface: Automatically selected format {} for new IOSurface buffer", fourccToName(attrs.format)));
    }

    if (attrs.format == DRM_FORMAT_INVALID) {
        allocator->backend->log(AQ_LOG_ERROR, "IOSurface: Failed to allocate an IOSurface buffer: no format found");
        return;
    }

    if (std::ranges::none_of(FORMATS, [this](const auto& f) { return f.drmFormat == attrs.format; })) {
        allocator->backend->log(AQ_LOG_ERROR,
                                std::format("IOSurface: Failed to allocate an IOSurface buffer: format {} isn't supported by primary backend", fourccToName(attrs.format)));
        return;
    }

    const auto PIXEL_FORMAT = pixelFormatFrom(attrs.format);
    if (!PIXEL_FORMAT) {
        allocator->backend->log(AQ_LOG_ERROR,
                                std::format("IOSurface: Failed to allocate an IOSurface buffer: format {} has no IOSurface pixel format", fourccToName(attrs.format)));
        return;
    }

    CFMutableDictionaryRef properties = CFDictionaryCreateMutable(kCFAllocatorDefault, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    setNumber(properties, kIOSurfaceWidth, (int32_t)attrs.size.x);
    setNumber(properties, kIOSurfaceHeight, (int32_t)attrs.size.y);
    setNumber(properties, kIOSurfaceBytesPerElement, 4);
    setNumber(properties, kIOSurfacePixelFormat, (int32_t)*PIXEL_FORMAT);

    surface = IOSurfaceCreate(properties);
    CFRelease(properties);

    if (!surface) {
        allocator->backend->log(AQ_LOG_ERROR, "IOSurface: Failed to allocate an IOSurface buffer: surface null");
        return;
    }

    attrs.planes        = 1;
    attrs.strides.at(0) = IOSurfaceGetBytesPerRow(surface);

    attachments.add(makeShared<CIOSurfaceAttachment>(surface));

    allocator->backend->log(AQ_LOG_DEBUG,
                            std::format("IOSurface: Allocated a new buffer with size {} and format {} with IOSurface id {}, bytes per row {}", attrs.size, fourccToName(attrs.format),
                                        IOSurfaceGetID(surface), attrs.strides.at(0)));
}

Aquamarine::CIOSurfaceBuffer::~CIOSurfaceBuffer() {
    events.destroy.emit();
    if (surface) {
        if (mapping)
            IOSurfaceUnlock(surface, 0, nullptr);
        CFRelease(surface);
    }
}

eBufferCapability Aquamarine::CIOSurfaceBuffer::caps() {
    return Aquamarine::eBufferCapability::BUFFER_CAPABILITY_DATAPTR;
}

eBufferType Aquamarine::CIOSurfaceBuffer::type() {
    return Aquamarine::eBufferType::BUFFER_TYPE_MISC;
}

void Aquamarine::CIOSurfaceBuffer::update(const Hyprutils::Math::CRegion& damage) {
    ;
}

bool Aquamarine::CIOSurfaceBuffer::isSynchronous() {
    return false;
}

bool Aquamarine::CIOSurfaceBuffer::good() {
    return surface;
}

SDMABUFAttrs Aquamarine::CIOSurfaceBuffer::dmabuf() {
    return attrs;
}

std::tuple<uint8_t*, uint32_t, size_t> Aquamarine::CIOSurfaceBuffer::beginDataPtr(uint32_t flags) {
    if (mapping)
        allocator->backend->log(AQ_LOG_ERROR, "beginDataPtr is called a second time without calling endDataPtr first. Returning old mapping");
    else if (IOSurfaceLock(surface, 0, nullptr) == KERN_SUCCESS)
        mapping = (uint8_t*)IOSurfaceGetBaseAddress(surface);

    return {mapping, attrs.format, IOSurfaceGetBytesPerRow(surface) * (size_t)attrs.size.y};
}

void Aquamarine::CIOSurfaceBuffer::endDataPtr() {
    if (mapping) {
        IOSurfaceUnlock(surface, 0, nullptr);
        mapping = nullptr;
    }
}

void CIOSurfaceAllocator::destroyBuffers() {
    for (auto& buf : buffers) {
        buf.reset();
    }
}

CIOSurfaceAllocator::~CIOSurfaceAllocator() = default;

SP<CIOSurfaceAllocator> Aquamarine::CIOSurfaceAllocator::create(Hyprutils::Memory::CWeakPointer<CBackend> backend_) {
    auto allocator = SP<CIOSurfaceAllocator>(new CIOSurfaceAllocator(backend_));

    backend_->log(AQ_LOG_DEBUG, "Created an IOSurface allocator");

    allocator->self = allocator;

    return allocator;
}

Aquamarine::CIOSurfaceAllocator::CIOSurfaceAllocator(Hyprutils::Memory::CWeakPointer<CBackend> backend_) : backend(backend_) {
    ;
}

SP<IBuffer> Aquamarine::CIOSurfaceAllocator::acquire(const SAllocatorBufferParams& params, Hyprutils::Memory::CSharedPointer<CSwapchain> swapchain_) {
    if (params.size.x < 1 || params.size.y < 1) {
        backend->log(AQ_LOG_ERROR, std::format("Couldn't allocate an IOSurface buffer with invalid size {}", params.size));
        return nullptr;
    }

    auto newBuffer = SP<CIOSurfaceBuffer>(new CIOSurfaceBuffer(params, self, swapchain_));

    if (!newBuffer->good()) {
        backend->log(AQ_LOG_ERROR, std::format("Couldn't allocate an IOSurface buffer with size {} and format {}", params.size, fourccToName(params.format)));
        return nullptr;
    }

    buffers.emplace_back(newBuffer);
    std::erase_if(buffers, [](const auto& b) { return b.expired(); });
    return newBuffer;
}

Hyprutils::Memory::CSharedPointer<CBackend> Aquamarine::CIOSurfaceAllocator::getBackend() {
    return backend.lock();
}

int Aquamarine::CIOSurfaceAllocator::drmFD() {
    return -1;
}

eAllocatorType Aquamarine::CIOSurfaceAllocator::type() {
    return AQ_ALLOCATOR_TYPE_IOSURFACE;
}

#endif
